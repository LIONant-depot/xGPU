#ifndef E29_GAME_PLUGIN_H
#define E29_GAME_PLUGIN_H
#pragma once

// Phase 8 of the xECSV2 type-registration architecture plan: E29's own host-side half of the
// hot-reloadable "Game.dll" - loading/unloading the plugin and the full destroy-and-recreate-world
// reload sequence (Phase 8A - "full-world reconstruction", the smallest unload-safety surface;
// see the plan's own Phase 8 section for the entity-preserving 8B milestone this deliberately does
// NOT attempt). Mirrors E27_NodeOS's own ReloadPlugin as closely as this project's much simpler
// registration model allows - see xecs_plugin_api.h's own comment for why no virtual node/factory
// interfaces are needed here at all.
//
// Must be included after both xecs.h and xecs_plugin_api.h.
#include <Windows.h>

namespace e29
{
    // One loaded Game.dll generation. Slot is fixed at 1 (0 is xecs::plugin::host_v, reserved for
    // the host's own registrations) - E29 only ever hosts one game plugin at a time, so there is
    // only ever one non-host slot to assign.
    struct game_plugin_state
    {
        HMODULE                m_hModule       = nullptr;
        xecs::plugin::token     m_Token         = {};
        std::wstring            m_DllPath;

        bool isLoaded(void) const noexcept { return m_hModule != nullptr; }
    };

    //---------------------------------------------------------------------------
    // Loads Plugin.m_DllPath (must already be set) and calls its XecsPlugin_RegisterComponents -
    // the FIRST of the two-call sequence xecs_plugin_api.h's own comment requires (every
    // RegisterComponents call, host's and the plugin's, must happen before ANY RegisterSystems
    // call). Returns false (Plugin left untouched, exactly as before the call) on any failure -
    // missing file, missing export, nothing else to do differently: the caller keeps running
    // without a game loaded, same as if this was never called.
    //---------------------------------------------------------------------------
    inline bool LoadGamePluginComponents( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin, std::uint32_t Generation ) noexcept
    {
        assert( Plugin.isLoaded() == false );

        // Plain printf, deliberately NOT e29::Debugger() - Debugger() also arms the modal error-
        // popup (RenderErrorPopup, checked once per frame from the main loop's own top-level
        // scope). This function's very first caller (E29_LevelScene_Editor.cpp's own startup code)
        // calls it BEFORE the main loop has rendered even one frame - confirmed empirically (a
        // deterministic, 100%-reproducible "Missing EndChild()" ImGui assertion on frame 1,
        // isolated by bisecting against the pre-Phase-8 file and adding per-call-site printf
        // checkpoints) that arming the popup flag that early breaks ImGui's window-stack
        // bookkeeping - every OTHER Debugger() call site in this codebase fires from inside an
        // already-running frame, which this startup call path is not. A missing/failed-to-load
        // Game.dll is an expected, benign condition anyway (nothing has been built yet on a fresh
        // checkout) - a plain log line is the right amount of ceremony for it, not a modal.
        HMODULE hModule = LoadLibraryW(Plugin.m_DllPath.c_str());
        if (hModule == nullptr)
        {
            std::printf("[Game.dll] LoadLibrary failed for %ls\n", Plugin.m_DllPath.c_str()); std::fflush(stdout);
            return false;
        }

        auto* pRegisterComponents = reinterpret_cast<xecs_plugin_pfn_register_components*>(GetProcAddress(hModule, XECS_PLUGIN_REGISTER_COMPONENTS_NAME));
        if (pRegisterComponents == nullptr)
        {
            std::printf("[Game.dll] Missing export %s\n", XECS_PLUGIN_REGISTER_COMPONENTS_NAME); std::fflush(stdout);
            FreeLibrary(hModule);
            return false;
        }

        Plugin.m_hModule = hModule;
        Plugin.m_Token   = { .m_Slot = 1, .m_Generation = Generation };
        pRegisterComponents(GameMgr, Plugin.m_Token);
        return true;
    }

    // The SECOND call of the two-call sequence - only meaningful once every RegisterComponents
    // call (host's own, done by the caller, and the plugin's, done by
    // LoadGamePluginComponents above) has already happened. A no-op if no plugin is loaded.
    inline void RegisterGamePluginSystems( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pRegisterSystems = reinterpret_cast<xecs_plugin_pfn_register_systems*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_REGISTER_SYSTEMS_NAME)))
            pRegisterSystems(GameMgr);
    }

    //---------------------------------------------------------------------------
    // Detach/quiesce + unload - called with the OLD world already destroyed (see ReloadGame
    // below), so xecs::component::mgr::UnregisterPlugin's own full-reset is exactly the correct,
    // sufficient operation (see its own comment for why). A no-op if nothing is loaded.
    //---------------------------------------------------------------------------
    inline void UnloadGamePlugin( game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pUnregister = reinterpret_cast<xecs_plugin_pfn_unregister*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_UNREGISTER_NAME)))
            pUnregister(Plugin.m_Token);

        xecs::component::mgr::UnregisterPlugin(Plugin.m_Token);

        FreeLibrary(Plugin.m_hModule);
        Plugin.m_hModule = nullptr;
        Plugin.m_Token   = {};
    }

    //---------------------------------------------------------------------------
    // Phase 8A's full reload sequence: Save -> destroy the runtime world -> unregister/unload the
    // old plugin generation -> load the new one -> create a fresh world -> reload the persistent
    // world into it. Same "abort only if Save fails; everything after is best-effort" philosophy
    // E27_NodeOS's own ReloadPlugin uses (see its own comment) - once the world is destroyed there
    // is no partial state worth preserving over just getting back to A working world.
    //
    // pGameMgr, InspectorBridge and EntityInspector are all rebound in place (pGameMgr reset and
    // reconstructed; InspectorBridge.RegisterCallbacks re-run against the new instance - its own
    // callbacks are stored as std::function MEMBERS specifically so they can be rebound like this,
    // see its own declaration comment) - the caller's own references/pointers to these three stay
    // valid across the call; only their CONTENTS change. g_pGameMgr is updated to match.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    bool ReloadGame
    ( std::unique_ptr<xecs::game_mgr::instance>&  pGameMgr
    , editor_state&                               State
    , game_plugin_state&                          Plugin
    , xproperty::inspector&                       EntityInspector
    , entity_inspector_bridge&                    InspectorBridge
    , const std::wstring&                         ProjectPath
    , T_REGISTER_HOST_COMPONENTS_FN&&              RegisterHostComponents  // (xecs::game_mgr::instance&) noexcept - e.g. registers e29::name/transform/etc
    , T_REGISTER_HOST_SYSTEMS_FN&&                 RegisterHostSystems     // (xecs::game_mgr::instance&) noexcept - e.g. registers e29::tick_logger_a/b
    ) noexcept
    {
        SaveEverything(*pGameMgr, State);

        const bool bHadPlugin = Plugin.isLoaded();
        const std::uint32_t NextGeneration = Plugin.m_Token.m_Generation + 1;

        // Destroy the entire runtime world FIRST - by the time UnloadGamePlugin's own
        // UnregisterPlugin(Token) call resets the shared component registry, nothing still
        // depends on any current BitID assignment (see xecs_component_mgr.h's own comment on
        // UnregisterPlugin for exactly why that ordering is what makes a full reset correct here).
        pGameMgr.reset();

        if (bHadPlugin)
        {
            UnloadGamePlugin(Plugin);
        }
        else
        {
            // No plugin was ever loaded, but a fresh world still needs a fully blank registry to
            // register everything into from scratch (built-ins + the host's own types) - the same
            // "create a fresh world" starting point a plugin-driven reload gets via UnloadGamePlugin.
            xecs::component::mgr::resetRegistrations();
        }

        pGameMgr = std::make_unique<xecs::game_mgr::instance>();

        RegisterHostComponents(*pGameMgr);

        const bool bLoaded = LoadGamePluginComponents(*pGameMgr, Plugin, NextGeneration);

        RegisterHostSystems(*pGameMgr);
        RegisterGamePluginSystems(*pGameMgr, Plugin);

        pGameMgr->m_SceneMgr.m_ProjectPath  = ProjectPath;
        pGameMgr->m_LevelMgr.m_ProjectPath  = ProjectPath;
        pGameMgr->m_PrefabMgr.m_ProjectPath = ProjectPath;
        pGameMgr->m_SystemMgr.m_ProjectPath = ProjectPath;
        if (auto Err = pGameMgr->m_SystemMgr.Load(); Err)
            Debugger(std::format("Failed to load System Registry order: {}", Err.getMessage()));

        g_pGameMgr = pGameMgr.get();
        InspectorBridge.RegisterCallbacks(EntityInspector, *pGameMgr, State);

        // State.m_SelectedEntity is the only RUNTIME handle here (m_GlobalInfoIndex/m_Validation -
        // meaningless once pGameMgr.reset() destroyed the world it indexed into). Everything else
        // E29 tracks selection with (m_SelectedEntityId, m_SelectedEntityScene,
        // m_MultiSelectedEntityIds/Order, m_MultiSelectScene) is already a STABLE identity
        // (permanent_id / scene guid), untouched by the world being destroyed and rebuilt - clearing
        // those too would silently drop the user's selection on every single reload for no reason.
        // Cleared here, then RE-RESOLVED below once OpenLevel has repopulated the scene fresh, via
        // the scene's own m_LocalToRuntime - the exact same permanent_id -> live-handle lookup every
        // other entity-migrating code path in this Kit already relies on (see e.g.
        // CreatePrefabFromGroupRoot's identical Scene.m_LocalToRuntime.find(Id) pattern).
        State.m_SelectedEntity        = {};
        State.m_bEntityInspectorDirty = true;

        if (!State.m_CurrentLevel.empty())
            OpenLevel(*pGameMgr, State, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });

        // Re-resolve the selection against the freshly reloaded scene. The common case - nothing
        // about this specific entity changed, only the runtime world it lives in was rebuilt -
        // picks selection (and the Entity Properties panel) back up right where it was; if the
        // entity is genuinely gone (e.g. deleted on disk since the last save), this falls back to
        // no selection rather than holding a permanent_id that no longer resolves to anything.
        if (State.m_SelectedEntityId != xecs::scene::invalid_permanent_id_v)
        {
            if (auto* pScene = pGameMgr->m_SceneMgr.Find(State.m_SelectedEntityScene))
            {
                if (auto It = pScene->m_LocalToRuntime.find(State.m_SelectedEntityId); It != pScene->m_LocalToRuntime.end())
                    State.m_SelectedEntity = It->second;
                else
                    State.m_SelectedEntityId = xecs::scene::invalid_permanent_id_v;
            }
            else
            {
                State.m_SelectedEntityId = xecs::scene::invalid_permanent_id_v;
            }
        }

        return bLoaded;
    }
}

#endif
