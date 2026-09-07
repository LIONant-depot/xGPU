#ifndef E29_PLAY_SESSION_H
#define E29_PLAY_SESSION_H
#pragma once

// Extracted from E29_GamePlugin.h (mechanical move, phase 3 of the kit split - see the umbrella
// file's own top comment). Play-session orchestration: the V1/Vn snapshot bridge
// (GetReloadBridgeSnapshotPath/SaveSnapshot/LoadSnapshot), the CaptureOpenScenes/ReattachOpenScenes
// tree-preservation trick, the shared RebuildWorld skeleton every reload goes through, and the three
// entry points a caller actually drives from (StartGameReload/PollGameReload/StopPlaySession) -
// ReregisterAlreadyLoadedPlugin is bundled in here rather than E29_GamePluginLoad.h since its one
// and only caller is RebuildWorld, right below it. Meant to be included via the umbrella
// (E29_GamePlugin.h) only, after E29_GamePluginLog.h/E29_GamePluginBuild.h/E29_GamePluginLoad.h.

namespace e29
{
    //---------------------------------------------------------------------------
    // V1 vs Vn, direct user model: "when you hit play you snapshot the current state and save it
    // (V1); when you hit pause you may recompile etc - we call these ones Vn (n>1); when you hit stop
    // you only care about reloading V1, all other ones are 100% irrelevant, because the point is get
    // back to normal editing" - and, critically, "like Unity the tree represents the current truth
    // of the scenes": entities can die or get created (dumped into the default folder) while playing,
    // so Stop must put the Level tree back exactly as it was before Play, not just restore raw
    // component values. V1 is therefore the REAL Scene/Level/Prefab disk save (SaveEverything/
    // OpenLevel - already Scene-aware, already the proven mechanism that reconstructs the tree
    // correctly) taken once at Play-entry - not a raw binary dump. Vn does NOT need any of that
    // (direct user confirmation: "V1 is the only one that needs to serialize [the tree]... Vn does
    // not need that") - it stays the fast, ephemeral, scene-unaware xecs::game_mgr::instance::
    // SerializeGameState bridge below, purely to keep gameplay itself continuous across a mid-play
    // Game.dll reload, never touching the real saved assets and never read back by Stop.
    inline std::wstring GetReloadBridgeSnapshotPath() noexcept
    {
        static const std::wstring s_Path = (std::filesystem::temp_directory_path() / L"xGPU_E29_ReloadBridge.bin").wstring();
        return s_Path;
    }

    inline bool SaveSnapshot( xecs::game_mgr::instance& GameMgr, const std::wstring& Path ) noexcept
    {
        const std::string PathA{ std::filesystem::path(Path).string() };
        if (auto Err = GameMgr.SerializeGameState(PathA.c_str(), /*isRead*/false, /*isBinary*/true); Err)
        {
            LogGamePlugin(std::format("Game.dll: snapshot save failed: {}", Err.getMessage()));
            return false;
        }
        return true;
    }

    inline bool LoadSnapshot( xecs::game_mgr::instance& GameMgr, const std::wstring& Path ) noexcept
    {
        const std::string PathA{ std::filesystem::path(Path).string() };
        if (auto Err = GameMgr.SerializeGameState(PathA.c_str(), /*isRead*/true, /*isBinary*/true); Err)
        {
            LogGamePlugin(std::format("Game.dll: snapshot restore failed: {}", Err.getMessage()));
            return false;
        }
        return true;
    }

    //---------------------------------------------------------------------------
    // Diagnostic only - the Level tree can't show anything meaningful right after a raw snapshot
    // restore (no Scene ever gets reopened - see persist_mode's own comment), so this is the one way
    // to actually confirm real entity/component data survived the round trip rather than just
    // guessing from an empty-looking tree. Left in permanently (not added-then-reverted) per this
    // project's own persistent-diagnostic-logging convention - logged to the Game.dll Log panel,
    // which is already visible, after every single reload regardless of which persist_mode ran.
    //---------------------------------------------------------------------------
    inline void LogWorldEntityCount( xecs::game_mgr::instance& GameMgr, const char* pLabel ) noexcept
    {
        int nArchetypes = 0;
        int nEntities    = 0;
        for (auto& pArchetype : GameMgr.m_ArchetypeMgr.m_lArchetype)
        {
            ++nArchetypes;
            for (auto pF = pArchetype->getFamilyHead(); pF; pF = pF->m_Next.get())
                for (auto pP = &pF->m_DefaultPool; pP; pP = pP->m_Next.get())
                    nEntities += pP->Size();
        }
        LogGamePlugin(std::format("Game.dll: [{}] world now has {} archetype(s), {} live entit(y/ies)", pLabel, nArchetypes, nEntities));
    }

    //---------------------------------------------------------------------------
    // The Vn (RawSnapshotBridge) tree-preservation trick - direct user insight: "the raw
    // serialization just needs to make sure entities are restored with the same exact ID" (confirmed
    // empirically: xecs::component::entity's own m_Value round-trips bit-for-bit identical through
    // SerializeGameState - the write path's own "GlobalEntities" record restores each entity's
    // Validation flag at its EXACT original global-info slot index, not a freshly reallocated one -
    // see xecs_game_mgr.cpp's own comment on that record). Since the entity VALUES a Scene's
    // m_LocalToRuntime/m_RuntimeToLocal maps reference never change, NO translation is needed at all
    // - moving the whole xecs::scene::instance object out before the destroy and back in after the
    // restore is sufficient; its maps are still valid, unmodified, pointing at the exact same entity
    // values that come back. Folders/parent-scene edges/pending-changes/everything else about the
    // scene comes along for free in the same move, for the same reason RestoreFromV1 doesn't need
    // any of this at all (it goes through OpenLevel instead).
    //---------------------------------------------------------------------------
    inline std::vector<std::unique_ptr<xecs::scene::instance>> CaptureOpenScenes
    ( xecs::game_mgr::instance& GameMgr
    , const editor_state&       State
    ) noexcept
    {
        std::vector<std::unique_ptr<xecs::scene::instance>> Captured;
        for (auto& SceneGuid : State.m_OpenScenes)
        {
            if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
            {
                LogGamePlugin(std::format("Game.dll: [Vn capture] scene {:016X} - {} entit(y/ies), {} folder(s)",
                    SceneGuid.m_Instance.m_Value, pScene->m_LocalToRuntime.size(), pScene->m_Folders.size()));
                Captured.push_back(std::make_unique<xecs::scene::instance>(std::move(*pScene)));
            }
            else
            {
                LogGamePlugin(std::format("Game.dll: [Vn capture] scene {:016X} NOT FOUND in SceneMgr", SceneGuid.m_Instance.m_Value));
            }
        }
        LogGamePlugin(std::format("Game.dll: [Vn capture] {} of {} open scene(s) captured", Captured.size(), State.m_OpenScenes.size()));
        return Captured;
    }

    inline void ReattachOpenScenes
    ( xecs::game_mgr::instance&                              GameMgr
    , std::vector<std::unique_ptr<xecs::scene::instance>>&&  Captured
    ) noexcept
    {
        for (auto& pScene : Captured)
        {
            const auto SceneGuid = pScene->m_Guid;
            auto& NewScene = GameMgr.m_SceneMgr.FindOrCreate(SceneGuid);
            NewScene = std::move(*pScene);
            LogGamePlugin(std::format("Game.dll: [Vn reattach] scene {:016X} - {} entit(y/ies), {} folder(s), state={}",
                SceneGuid.m_Instance.m_Value, NewScene.m_LocalToRuntime.size(), NewScene.m_Folders.size(), (int)NewScene.m_State));
        }
    }

    //---------------------------------------------------------------------------
    // Re-registers an ALREADY-loaded plugin module's components against a freshly reset registry,
    // without touching the DLL itself at all (no FreeLibrary/LoadLibrary, no new shadow copy, same
    // xecs::plugin::token/generation as before) - the world still has to be destroyed and recreated
    // (the component registry is reset process-wide the moment ANY plugin generation changes owner,
    // and a fresh xecs::game_mgr::instance needs everything re-registered into it from scratch), but
    // the CODE didn't change, so there's no reason to pay for a fresh compile-output copy or a
    // FreeLibrary/LoadLibrary cycle. Used by StopPlaySession, where "as fast as possible" applies just
    // as much as it does to the play-session snapshot above - Stop is not a recompile, it's "throw
    // away the play session's world and rebuild a clean one".
    //---------------------------------------------------------------------------
    inline void ReregisterAlreadyLoadedPlugin( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pRegisterComponents = reinterpret_cast<xecs_plugin_pfn_register_components*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_REGISTER_COMPONENTS_NAME)))
            pRegisterComponents(GameMgr, Plugin.m_Token);
    }

    //---------------------------------------------------------------------------
    // Step 1 of 2 - a recompile-CHECK, not a user-facing "reload" action anymore (there is no more
    // manual "Reload Game" button - matches Unity's own model: recompiling is something the editor
    // just does for you). Called automatically from two places only, per direct user direction: once
    // on the frame the app window regains OS focus (xgpu::tools::imgui::ConsumeWindowFocusGained -
    // "the user tabbed back in after editing code"), and once when the Play button is pressed
    // (Stopped -> Playing only - see editor_state::m_bPlayRequested). Kicks off
    // BuildGamePluginIfStale on a background thread and returns immediately; does NOT touch
    // pGameMgr/the world/the currently loaded generation AT ALL - that's the whole point (see
    // game_plugin_state's own comment). A no-op if a build is already in flight.
    //---------------------------------------------------------------------------
    inline void StartGameReload( game_plugin_state& Plugin ) noexcept
    {
        if (Plugin.m_bBuilding) return;

        Plugin.m_bBuilding  = true;
        Plugin.m_BuildFuture = std::async(std::launch::async, [&Plugin]() noexcept
        {
            return BuildGamePluginIfStale(Plugin);
        });
    }

    // How RebuildWorld persists the world across the destroy/recreate it always does - the ONE thing
    // that genuinely differs between "a normal reload" and "Stop", beyond just which DLL-swap
    // strategy applies. Direct user model: Play writes ONE snapshot ("V1", the REAL Scene/Level/
    // Prefab disk save - see GetReloadBridgeSnapshotPath's own comment for why this must be disk, not
    // the fast binary dump) the moment it starts; every mid-play/paused reload afterward writes its
    // own throwaway "Vn" (n>1, the fast binary bridge) purely to keep gameplay continuous across that
    // one reload - Stop only ever cares about V1, every Vn is 100% irrelevant to it, because the
    // whole point of Stop is getting back to normal editing - Level tree included - exactly as it was
    // before Play, matching Unity's own Play/Stop semantics.
    //
    //   RawSnapshotBridge - EVERY code-triggered reload (Playing, Paused, AND plain edit-mode) lands
    //                       here now - the world must be destroyed anyway (a Game.dll swap), so
    //                       write/read this reload's own throwaway "Vn" (GetReloadBridgeSnapshotPath -
    //                       overwritten every cycle) so whatever the user currently has - gameplay
    //                       state while Playing, or just unsaved edits while Stopped - survives the
    //                       destroy/recreate intact, entirely in memory, without touching the real
    //                       saved project on disk. Scene-unaware, and deliberately so (Vn never needs
    //                       the tree, only V1 does) - CaptureOpenScenes/ReattachOpenScenes carry the
    //                       tree across separately, snapshot-independent.
    //   RestoreFromV1     - Stop. Never saves anything - there's nothing worth saving; whatever the
    //                       play session's raw Vn bridging left the world in is 100% discarded.
    //                       Reloads via OpenLevel - correct precisely because V1 was itself a real
    //                       disk save (written explicitly by Play, or by PollGameReload's own
    //                       UpToDate/Rebuilt branches right before flipping to Playing - never as a
    //                       silent side effect of an edit-mode reload), so "reload from disk" already
    //                       means "reload V1", nothing more needs building.
    //
    // There used to be a third mode, DiskSaveAndReload, used for every edit-mode (not-playing)
    // reload - it saved the real Scene/Level/Prefab assets to disk unconditionally as part of the
    // reload. Removed per direct user request after an external review correctly flagged it: tabbing
    // back into the editor after an unrelated code edit would silently commit whatever was in the
    // scene to disk, with no explicit Save action from the user - surprising, and unlike Unity/Unreal,
    // neither of which persists anything to the real project on a domain reload / Live Coding patch.
    // RawSnapshotBridge already does everything DiskSaveAndReload needed (preserve current state
    // across the destroy/recreate) without the disk write, so switching every reload to it was a
    // straight subtraction, not a new code path - see PollGameReload's own comment for the one place
    // that used to get V1 "for free" as DiskSaveAndReload's side effect and now writes it explicitly.
    enum class persist_mode : std::uint8_t { RawSnapshotBridge, RestoreFromV1 };

    //---------------------------------------------------------------------------
    // The one shared "destroy the world and rebuild it" skeleton - every reload E29 ever does
    // (a genuine Game.dll recompile, or just discarding a play session on Stop) is exactly this same
    // sequence, differing only in two independent axes, both parameterized rather than duplicated:
    //
    //   bSwapDll     - true: the code actually changed (a Rebuilt generation) - unload the old
    //                  module (if any), copy+load the new one, bump the plugin token's generation.
    //                  false: the module already loaded is still perfectly good (Stop, or a
    //                  recompile check that found nothing to do) - just re-run its OWN
    //                  RegisterComponents against the freshly reset registry (see
    //                  ReregisterAlreadyLoadedPlugin), same token/generation, no DLL I/O at all.
    //
    //   PersistMode  - see persist_mode's own comment above.
    //
    // pGameMgr, InspectorBridge and EntityInspector are all rebound in place (pGameMgr reset and
    // reconstructed; InspectorBridge.RegisterCallbacks re-run against the new instance - its own
    // callbacks are stored as std::function MEMBERS specifically so they can be rebound like this,
    // see its own declaration comment) - the caller's own references/pointers to these three stay
    // valid across the call; only their CONTENTS change. g_pGameMgr is updated to match.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    bool RebuildWorld
    ( std::unique_ptr<xecs::game_mgr::instance>&  pGameMgr
    , editor_state&                               State
    , game_plugin_state&                          Plugin
    , xproperty::inspector&                       EntityInspector
    , entity_inspector_bridge&                    InspectorBridge
    , const std::wstring&                         ProjectPath
    , T_REGISTER_HOST_COMPONENTS_FN&&              RegisterHostComponents
    , T_REGISTER_HOST_SYSTEMS_FN&&                 RegisterHostSystems
    , bool                                        bSwapDll
    , persist_mode                                PersistMode
    ) noexcept
    {
        // Captured BEFORE the destroy, only for the Vn bridge - see CaptureOpenScenes/
        // ReattachOpenScenes's own comment for why this is safe with zero translation (entity IDs
        // round-trip identical through the raw snapshot). Empty for every other PersistMode.
        auto CapturedScenes = (PersistMode == persist_mode::RawSnapshotBridge)
            ? CaptureOpenScenes(*pGameMgr, State)
            : std::vector<std::unique_ptr<xecs::scene::instance>>{};

        switch (PersistMode)
        {
        case persist_mode::RawSnapshotBridge: SaveSnapshot(*pGameMgr, GetReloadBridgeSnapshotPath()); break;
        case persist_mode::RestoreFromV1:     /* nothing worth saving */                              break;
        }

        const bool bHadPlugin = Plugin.isLoaded();

        // Destroy the entire runtime world FIRST - by the time UnloadGamePlugin's own
        // UnregisterPlugin(Token) call (or, for a non-DLL-swap reload, the plain
        // xecs::component::mgr::resetRegistrations() below) resets the shared component registry,
        // nothing still depends on any current BitID assignment (see xecs_component_mgr.h's own
        // comment on UnregisterPlugin for exactly why that ordering is what makes a full reset
        // correct here).
        pGameMgr.reset();

        if (bSwapDll)
        {
            if (bHadPlugin) UnloadGamePlugin(Plugin);
            else             xecs::component::mgr::resetRegistrations();
        }
        else
        {
            // No DLL swap - the module (if any) stays loaded exactly as it is; only the registry
            // needs resetting so the fresh instance below has a blank slate to register into.
            xecs::component::mgr::resetRegistrations();
        }

        pGameMgr = std::make_unique<xecs::game_mgr::instance>();

        RegisterHostComponents(*pGameMgr);

        const bool bLoaded = bSwapDll
            ? LoadGamePluginComponents(*pGameMgr, Plugin, Plugin.m_Token.m_Generation + 1)
            : (ReregisterAlreadyLoadedPlugin(*pGameMgr, Plugin), Plugin.isLoaded());

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
        // (permanent_id / scene guid) - cleared here, then RE-RESOLVED below once the scene has been
        // repopulated, via the scene's own m_LocalToRuntime (the exact same permanent_id -> live-
        // handle lookup every other entity-migrating code path in this Kit already relies on).
        State.m_SelectedEntity        = {};
        State.m_bEntityInspectorDirty = true;

        if (PersistMode == persist_mode::RawSnapshotBridge)
        {
            LoadSnapshot(*pGameMgr, GetReloadBridgeSnapshotPath());
            // Reattach the Scenes captured before the destroy - see CaptureOpenScenes/
            // ReattachOpenScenes's own comment for why zero translation is needed (entity IDs
            // round-trip identical through the raw snapshot). Repopulates State.m_OpenScenes'
            // worth of Scene objects (folders, entities, everything) so the Level tree survives a
            // mid-play reload intact, not just once Stop runs.
            ReattachOpenScenes(*pGameMgr, std::move(CapturedScenes));

            // The Level tree itself is gated on GameMgr.m_LevelMgr.Find(State.m_CurrentLevel) - a
            // SEPARATE manager from m_SceneMgr, ALSO destroyed by pGameMgr.reset() above, and the
            // reattached Scenes above don't touch it at all. Unlike a Scene, a Level holds no runtime
            // entity state whatsoever (just its own name + a list of member Scene guids - see
            // xecs_level.h's own instance struct) - a cheap descriptor-only disk read (Load, NOT
            // Activate - Activate would re-run EnsureLoaded on every member Scene, re-loading
            // entities from disk and clobbering the live, just-reattached Scene objects above) is
            // all it needs, and is exactly what was missing: without this, GameMgr.m_LevelMgr.Find()
            // returned nullptr and RenderLevelTreePanel rendered nothing at all - visually identical
            // to a genuinely empty tree, even though the Scenes/entities themselves were fine.
            if (!State.m_CurrentLevel.empty())
                pGameMgr->m_LevelMgr.Load(State.m_CurrentLevel);
        }
        else if (!State.m_CurrentLevel.empty())
        {
            // Only persist_mode::RestoreFromV1 (Stop) ever lands here now - the only other mode,
            // RawSnapshotBridge, is caught by the `if` above. V1 IS a real disk save (see
            // persist_mode's own comment), so "reload from disk" already means "reload V1" for Stop;
            // nothing extra to build. This is what correctly restores the Level tree exactly as it
            // was before Play - entities that died or got created (into the default folder) during
            // the play session are discarded, matching Unity's own Play/Stop semantics.
            OpenLevel(*pGameMgr, State, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });
        }

        LogWorldEntityCount(*pGameMgr, PersistMode == persist_mode::RawSnapshotBridge ? "Vn restore" : "V1/disk restore");

        // Re-resolve the selection against the freshly reloaded scene. The common case - nothing
        // about this specific entity changed, only the runtime world it lives in was rebuilt -
        // picks selection (and the Entity Properties panel) back up right where it was; if the
        // entity is genuinely gone (e.g. deleted on disk since the last save, or no Scene was
        // reopened at all - the raw-snapshot case above), this falls back to no selection rather
        // than holding a permanent_id that no longer resolves to anything.
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

    //---------------------------------------------------------------------------
    // The "Stop" button's own handler - always discards whatever a play session did (including any
    // mid-play raw-snapshot reloads along the way - see RebuildWorld's own comment) in favor of a
    // proper, fully correct reload from the last real disk save (the one Play itself made on the way
    // in - see the Play button's own handler in E29_LevelScene_Editor.cpp). No DLL swap here: Stop
    // doesn't imply a code change, so the currently loaded generation is re-registered in place
    // (bSwapDll=false) rather than paying for an unload/reload cycle it doesn't need.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    void StopPlaySession
    ( std::unique_ptr<xecs::game_mgr::instance>&  pGameMgr
    , editor_state&                               State
    , game_plugin_state&                          Plugin
    , xproperty::inspector&                       EntityInspector
    , entity_inspector_bridge&                    InspectorBridge
    , const std::wstring&                         ProjectPath
    , T_REGISTER_HOST_COMPONENTS_FN&&              RegisterHostComponents
    , T_REGISTER_HOST_SYSTEMS_FN&&                 RegisterHostSystems
    ) noexcept
    {
        pGameMgr->Stop();
        RebuildWorld
        ( pGameMgr, State, Plugin, EntityInspector, InspectorBridge, ProjectPath
        , RegisterHostComponents, RegisterHostSystems
        , /*bSwapDll*/ false, persist_mode::RestoreFromV1
        );
        State.m_PlayState = editor_state::play_state::Stopped;
    }

    //---------------------------------------------------------------------------
    // Step 2 of 2 - call once per frame, at a clean frame boundary (BEFORE BeginRendering, never
    // mid-frame - confirmed empirically that running the heavy world-rebuild synchronously inside an
    // active ImGui frame corrupts its window-stack bookkeeping). A no-op unless a build is both
    // in-flight AND finished (checked via a non-blocking wait_for), so safe to call unconditionally
    // every frame regardless of Plugin.m_bBuilding's current value.
    //
    // On a FAILED build: stops here, and cancels any pending Play request (m_bPlayRequested) rather
    // than starting a play session against a known-broken build. The currently loaded generation (if
    // any) was never unloaded, never touched - it just keeps running exactly as it was.
    //
    // On UpToDate (checked but nothing needed rebuilding - the common case once this runs on every
    // focus-regain, not just an explicit click): no world-touching reload at all. If a Play was
    // requested, it can proceed directly - SaveEverything below gives Stop a fresh, correct revert
    // point, and Play just keeps ticking the SAME live world (no reason to tear anything down over a
    // check that found nothing to do).
    //
    // On Rebuilt: runs the full destroy/recreate/DLL-swap sequence via RebuildWorld, always via the
    // raw in-memory snapshot bridge (persist_mode::RawSnapshotBridge) regardless of Play state - see
    // persist_mode's own comment for why this reload never touches disk on its own anymore. If a Play
    // was ALSO requested (the user pressed Play while a rebuild happened to be needed), enters play
    // directly afterward - but MUST write V1 explicitly here (see below), since the reload itself no
    // longer does that as a side effect the way the old DiskSaveAndReload mode used to.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    bool PollGameReload
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
        if (!Plugin.m_bBuilding) return false;
        if (Plugin.m_BuildFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;

        const build_result Result = Plugin.m_BuildFuture.get();
        Plugin.m_bBuilding = false;

        if (Result == build_result::Failed)
        {
            State.m_bPlayRequested = false;
            return false;
        }

        if (Result == build_result::UpToDate)
        {
            if (State.m_bPlayRequested)
            {
                State.m_bPlayRequested = false;
                // Write V1 - the real disk save Stop will restore from (see persist_mode's own
                // comment for why this must be disk, not the fast binary Vn bridge - Stop needs the
                // Level tree back, not just raw component values).
                SaveEverything(*pGameMgr, State);
                State.m_PlayState = editor_state::play_state::Playing;
            }
            return false;
        }

        // Result == build_result::Rebuilt
        const bool bLoaded = RebuildWorld
        ( pGameMgr, State, Plugin, EntityInspector, InspectorBridge, ProjectPath
        , RegisterHostComponents, RegisterHostSystems
        , /*bSwapDll*/ true, persist_mode::RawSnapshotBridge
        );

        if (State.m_bPlayRequested)
        {
            State.m_bPlayRequested = false;
            // Write V1 explicitly, same as the UpToDate branch above - the reload just above used
            // RawSnapshotBridge (never touches disk), so unlike before this removed the
            // DiskSaveAndReload mode, V1 is no longer a free side effect of the reload itself.
            SaveEverything(*pGameMgr, State);
            State.m_PlayState = editor_state::play_state::Playing;
        }

        return bLoaded;
    }

} // namespace e29

#endif // E29_PLAY_SESSION_H
