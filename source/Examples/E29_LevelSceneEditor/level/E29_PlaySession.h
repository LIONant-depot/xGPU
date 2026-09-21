#ifndef E29_PLAY_SESSION_H
#define E29_PLAY_SESSION_H
#pragma once

// Extracted from E29_GamePlugin.h (mechanical move, phase 3 of the kit split - see the umbrella
// file's own top comment). Play-session orchestration: the V1/Vn snapshot bridge
// (GetReloadBridgeSnapshotPath/SaveSnapshot/LoadSnapshot), the CaptureOpenScenes/ReattachOpenScenes
// tree-preservation trick, the host-wide ReloadGameModule every Game.dll reload goes through, and the three
// entry points a caller actually drives from (StartGameReload/PollGameReload/StopPlaySession) -
// ReregisterAlreadyLoadedPlugin is bundled in here rather than E29_GamePluginLoad.h since its one
// and only callers are BeforeReload/AfterReload/StopPlay (E29_AppWorld.h). Meant to be included via the umbrella
// (E29_GamePlugin.h) only, after E29_GamePluginLog.h/E29_GamePluginBuild.h/E29_GamePluginLoad.h.

// xeditor::Run (StopPlaySession's own "keep property tweaks" pass, right below) - included
// directly rather than relying on the .cpp's own later include of it, same "a file that names a
// symbol should include what declares it" reasoning every other kit/plugin file here already follows.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_ComponentCompatibility.h"
#include <sstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <optional>

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
        // Computed HERE, on the main thread, and captured by value - NOT re-computed inside the
        // background task. See BuildGamePluginIfStale's own comment on ModuleSourceTime for why: it
        // reads e29::g_ScriptConfig/e10::g_LibMgr, neither safe to touch from the background thread
        // this function's lambda runs on.
        const auto ModuleSourceTime = GetLatestModuleSourceWriteTime();
        Plugin.m_BuildFuture = std::async(std::launch::async, [&Plugin, ModuleSourceTime]() noexcept
        {
            return BuildGamePluginIfStale(Plugin, ModuleSourceTime);
        });
    }

    //---------------------------------------------------------------------------
    // The transport state machine. The menu-bar transport, the editor toolbar and the CLI commands all go
    // through these, so they cannot drift apart. The process-wide single-Play lock (xeditor::host) is
    // taken by RequestPlay and released only where a play session really ends: StopPlaySession, or
    // CancelPlayRequest when the build a pending Play was waiting for fails.
    //---------------------------------------------------------------------------

    // Stopped/Paused -> Playing. Writes V1 (the real disk save Stop restores from - it must be disk, not the
    // fast binary Vn bridge, because Stop needs the Level tree back) and marks the undo point Stop rewinds to.
    inline void EnterPlaying( editor_context& Ed ) noexcept
    {
        SaveEverything(Ed.World(), Ed.State());
        Ed.State().m_PlayHistoryBoundary = Ed.m_Undo.GetUndoIndex();
        Ed.State().m_PlayState           = editor_state::play_state::Playing;
    }

    // A pending Play that will never start (its build failed): drop it and release the Play lock it took.
    inline void CancelPlayRequest( editor_state& State ) noexcept
    {
        if (!State.m_bPlayRequested) return;
        State.m_bPlayRequested = false;
        State.m_bStepOneFrame  = false;
        xeditor::host::current()->end_play(&State);
    }

    inline void RequestResume( editor_state& State ) noexcept
    {
        if (State.m_PlayState == editor_state::play_state::Paused) State.m_PlayState = editor_state::play_state::Playing;
    }

    // From Stopped: recompile-check first (shared builds; PollGameReload then calls EnterPlaying) or enter
    // Playing directly. From Paused: resume. Returns a short status for the CLI; the buttons ignore it.
    inline std::string RequestPlay( editor_context& Ed, game_plugin_state& Plugin ) noexcept
    {
        auto& State = Ed.State();
        using play_state = editor_state::play_state;
        if (State.m_PlayState == play_state::Playing) return "Play: already playing";
        if (Plugin.m_bBuilding)                       return "Play: a build is already in flight";
        if (State.m_PlayState == play_state::Paused)  { RequestResume(State); return "Resumed"; }

        auto& Host = *xeditor::host::current();
        if (!Host.try_begin_play(&State))
        {
            xeditor::diagnostics::Log("Play refused: another Play session is already active");
            State.m_bPlayBusyPopup = true;
            return "Play: another Play session is already active";
        }
#if defined(XECS_BUILD_SHARED)
        State.m_bPlayRequested = true;
        StartGameReload(Plugin);
        return "Play requested (recompile-check in progress)";
#else
        EnterPlaying(Ed);
        return "Playing";
#endif
    }

    inline std::string RequestPause( editor_state& State ) noexcept
    {
        if (State.m_PlayState != editor_state::play_state::Playing) return "Pause: not playing";
        State.m_PlayState = editor_state::play_state::Paused;
        return "Paused";
    }

    // One frame. From Paused: one tick, stays Paused. From Stopped: starts Play, runs the first tick, lands Paused.
    inline std::string RequestStep( editor_context& Ed, game_plugin_state& Plugin ) noexcept
    {
        auto& State = Ed.State();
        using play_state = editor_state::play_state;
        if (State.m_PlayState == play_state::Playing) return "Step: pause first";
        if (State.m_PlayState == play_state::Stopped)
        {
            const std::string Result = RequestPlay(Ed, Plugin);
            if (State.m_PlayState == play_state::Stopped && !State.m_bPlayRequested) return Result; // refused
        }
        State.m_bStepOneFrame = true;
        return "Step";
    }

    // How a world is persisted across the destroy/recreate it always does - the ONE thing
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
    // Walks every currently open scene's own live entities, finds every one that actually HAS one of
    // MissingGuids (via its archetype's own component bits, resolved through the registry the OLD
    // generation - still fully loaded and running at the point this is called, from the confirm
    // modal's own "Strip and Continue" button - is still registered against), and removes it via the
    // normal, undo-tracked command bus (so the user can Undo this later if it turns out to be wrong).
    // Best-effort: a guid the OLD registry itself doesn't resolve either (shouldn't happen - it came
    // straight out of this session's own live world moments ago) is silently skipped.
    //---------------------------------------------------------------------------
    // Declared (not defined - see scene/commands/E29_Commands_MakePrefab.h for the inline definition this
    // refers to) here too since this file's own place in the umbrella include order is earlier than
    // that one - inline variables have external linkage, so a plain extern declaration anywhere in
    // the same program is enough to use it, no redefinition risk.
    inline void StripMissingComponentsFromOpenScenes( editor_context& Ed, const std::vector<xecs::scene::component_dependency>& MissingDeps ) noexcept
    {
        auto*          pWorld   = &Ed.World();
        auto*          pState   = &Ed.State();
        xundo::system* pDocUndo = &Ed.m_Undo;

        for (auto& SceneGuid : pState->m_OpenScenes)
        {
            auto* pScene = pWorld->m_SceneMgr.Find(SceneGuid);
            if (!pScene) continue;

            for (auto& Pair : pScene->m_LocalToRuntime)
            {
                const auto Id     = Pair.first;
                auto&      Entity = Pair.second;

                auto& EDetails = pWorld->m_ComponentMgr.getEntityDetails(Entity);
                if (!EDetails.m_pPool || !EDetails.m_pPool->m_pArchetype) continue;
                auto& Bits = EDetails.m_pPool->m_pArchetype->getComponentBits();

                for (auto& Dep : MissingDeps)
                {
                    auto* pInfo = pWorld->m_ComponentMgr.findComponentTypeInfo(Dep.m_Guid);
                    if (!pInfo || !Bits.getBit(pInfo->m_BitID)) continue;

                    xeditor::Run(*pDocUndo, std::format("RemoveComponent -Scene {} -Id {} -Component {:016X}"
                        , commands::FormatSceneGuid(SceneGuid)
                        , commands::FormatEntityId(Id)
                        , Dep.m_Guid.m_Value
                        ));
                }
            }
        }
    }

    //---------------------------------------------------------------------------
    // Called once per frame from the main loop. Reads/writes the single-instance globals (g_PendingReloadCompatibility,
    // g_pGamePlugin) and strips the components from the editor's own scenes.
    //---------------------------------------------------------------------------
    inline void RenderReloadCompatibilityModal(editor_context& Ed) noexcept
    {
        if (g_PendingReloadCompatibility.has_value())
            ImGui::OpenPopup("Game.dll Reload - Missing Components");

        if (ImGui::BeginPopupModal("Game.dll Reload - Missing Components", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (g_PendingReloadCompatibility.has_value())
            {
                ImGui::Text("The new Game.dll build no longer has %zu component type(s)\nthat currently-open scenes use:", g_PendingReloadCompatibility->m_Missing.size());
                for (auto& Dep : g_PendingReloadCompatibility->m_Missing)
                    ImGui::BulletText("%s", Dep.m_Name.c_str());
                ImGui::Separator();
                ImGui::TextWrapped(
                    "Strip and Continue: removes these components from the affected entities\n"
                    "(undoable) and reloads.\n"
                    "Cancel: keeps the current generation running - fix your script and try again."
                );
                ImGui::Separator();

                if (ImGui::Button("Strip and Continue", ImVec2(160, 0)))
                {
                    StripMissingComponentsFromOpenScenes(Ed, g_PendingReloadCompatibility->m_Missing);
                    g_PendingReloadCompatibility.reset();
                    ImGui::CloseCurrentPopup();
                    if (g_pGamePlugin) StartGameReload(*g_pGamePlugin);
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    g_PendingReloadCompatibility.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    //---------------------------------------------------------------------------
    // Splits one flat "-Key value -Key2 value2 ..." command string (the exact shape every command in
    // this system's own std::format calls already produces - no embedded spaces, every value here is
    // hex or Base64) into a name->value map. A throwaway xcmdline::parser/command_base could do this
    // too, but that machinery exists to VALIDATE input as it's typed; this is just reading text this
    // same codebase already wrote, so a plain split is enough. Token[0] (the command name itself,
    // e.g. "SetProperty") is skipped - callers that need it already filtered on it before calling this.
    //---------------------------------------------------------------------------
    inline std::unordered_map<std::string, std::string> ParseFlatArgs(const std::string& CmdStr) noexcept
    {
        std::unordered_map<std::string, std::string> Out;
        std::istringstream Stream(CmdStr);
        const std::vector<std::string> Tokens{ std::istream_iterator<std::string>(Stream), std::istream_iterator<std::string>() };
        for (std::size_t i = 1; i + 1 < Tokens.size(); i += 2)
        {
            if (Tokens[i].empty() || Tokens[i][0] != '-') continue;
            Out[Tokens[i].substr(1)] = Tokens[i + 1];
        }
        return Out;
    }

    // One property, on one entity, that changed at least once while Playing - After is whatever it
    // was left at when Stop was pressed (the LAST SetProperty seen for this exact Scene/Id/Component/
    // Path); Before is the value it had the FIRST time it changed during this play session, which is
    // exactly the value V1 already captured at Play-entry (Play always writes V1 before anything can
    // change) - reusing it here means Undo-ing this "keep" back to what it truly was pre-Play needs no
    // separate live read of the just-restored entity.
    struct kept_property_tweak
    {
        std::string m_Scene, m_Id, m_Component, m_Path, m_TypeGuid, m_Before, m_After;
    };

    //---------------------------------------------------------------------------
    // Walks every history entry pushed since Play started (State.m_PlayHistoryBoundary), keeping only
    // SetProperty entries - direct user request/scope decision: only plain property edits are ever
    // carried back into the persistent scene, never anything structural (CreateEntity/AddComponent/
    // etc, if they somehow also happened while Playing) - matches Unreal's own "Keep Simulation
    // Changes", which is similarly restricted to actors that already existed before simulating (see
    // documentation/E29_LevelSceneEditor/playmode_keep_property_tweaks.md for the full research/design). Deduplicated by
    // (Scene, Id, Component, Path) - a property dragged back and forth several times during one play
    // session collapses to a single entry, not a replay of every intermediate value.
    //---------------------------------------------------------------------------
    inline std::vector<kept_property_tweak> CollectPlayModePropertyTweaks(xundo::system& Undo, int BoundaryIndex) noexcept
    {
        std::vector<kept_property_tweak> Tweaks;
        std::unordered_map<std::string, std::size_t> KeyToIndex;

        const auto Count = static_cast<int>(Undo.GetHistoryCount());
        for (int i = BoundaryIndex; i < Count; ++i)
        {
            const std::string& Cmd = Undo.GetHistoryCommandString(static_cast<std::size_t>(i));
            if (Cmd.compare(0, 12, "SetProperty ") != 0) continue;

            const auto Args = ParseFlatArgs(Cmd);
            const auto ItScene = Args.find("Scene");     if (ItScene == Args.end()) continue;
            const auto ItId    = Args.find("Id");        if (ItId    == Args.end()) continue;
            const auto ItComp  = Args.find("Component"); if (ItComp  == Args.end()) continue;
            const auto ItPath  = Args.find("Path");      if (ItPath  == Args.end()) continue;
            const auto ItType  = Args.find("TypeGuid");  if (ItType  == Args.end()) continue;
            const auto ItAfter = Args.find("After");     if (ItAfter == Args.end()) continue;
            const auto ItBefore = Args.find("Before");   if (ItBefore == Args.end()) continue;

            const std::string Key = ItScene->second + '|' + ItId->second + '|' + ItComp->second + '|' + ItPath->second;
            if (auto KIt = KeyToIndex.find(Key); KIt != KeyToIndex.end())
            {
                Tweaks[KIt->second].m_After = ItAfter->second; // later entries win - keep the LAST value
            }
            else
            {
                KeyToIndex[Key] = Tweaks.size();
                Tweaks.push_back({ ItScene->second, ItId->second, ItComp->second, ItPath->second, ItType->second, ItBefore->second, ItAfter->second });
            }
        }
        return Tweaks;
    }

    //---------------------------------------------------------------------------
    // Formats each collected tweak into the exact "SetProperty ..." command string StopPlaySession
    // will later Run() if the answer to "keep these?" turns out to be yes - done up front (rather than
    // carrying the kept_property_tweak structs themselves all the way to Stop time) so
    // editor_state::m_PendingKeepTweaksCommands (E29_LevelSceneEditorKit.h, needed by the confirmation
    // modal below - which is declared and used well before this file's own types would otherwise be
    // visible there) can just be a plain std::vector<std::string>, no new type dependency.
    //---------------------------------------------------------------------------
    inline std::vector<std::string> CollectPlayModeKeepCommands(xundo::system& Undo, int BoundaryIndex) noexcept
    {
        std::vector<std::string> Out;
        for (auto& T : CollectPlayModePropertyTweaks(Undo, BoundaryIndex))
        {
            Out.push_back(std::format("SetProperty -Scene {} -Id {} -Component {} -Path {} -TypeGuid {} -Before {} -After {}"
                , T.m_Scene, T.m_Id, T.m_Component, T.m_Path, T.m_TypeGuid, T.m_Before, T.m_After
                ));
        }
        return Out;
    }

    //---------------------------------------------------------------------------
    // Drops any kept-tweak command whose target entity did NOT survive the V1 restore (created only
    // during Play - the one case this feature was deliberately never meant to touch, matching
    // Unreal's own "actors already in the level" restriction). Filtering here, BEFORE the group runs,
    // rather than letting a stale target fail inside it matters because xundo's own
    // Execute(group_name, Cmds) (xundo_system.h) aborts the ENTIRE group - and pushes NO history entry
    // at all - the moment any one sub-command's Redo() fails. Without this filter, one stale target
    // among several legitimate kept properties would silently swallow every other one alongside it
    // AND leave the ones that already ran for real un-recorded (mutated, but with no undo entry to
    // revert them).
    //---------------------------------------------------------------------------
    inline std::vector<std::string> FilterSurvivingTargets(xecs::game_mgr::instance& GameMgr, const std::vector<std::string>& Commands) noexcept
    {
        std::vector<std::string> Out;
        Out.reserve(Commands.size());
        for (auto& Cmd : Commands)
        {
            const auto Args = ParseFlatArgs(Cmd);
            const auto ItScene = Args.find("Scene"); if (ItScene == Args.end()) continue;
            const auto ItId    = Args.find("Id");    if (ItId    == Args.end()) continue;

            const auto SceneGuid = e29::commands::ParseSceneGuid(ItScene->second);
            const auto Id        = e29::commands::ParseEntityId(ItId->second);
            auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid);
            if (pScene && pScene->m_LocalToRuntime.contains(Id))
                Out.push_back(Cmd);
        }
        return Out;
    }

    //---------------------------------------------------------------------------
    // The single decision point for BOTH real Stop triggers (the menu-bar button, via
    // std::nullopt - it never knows the answer up front; the CLI Stop command, via its own -Keep
    // argument when given) - direct user request for a lightweight confirmation rather than either
    // silent always-keep or a full per-entity/per-property picker ("we do not need Unreal's per-entity
    // or per-property selection... we can always add that later, the core system is in place now").
    //
    // KeepOverride already decided (an explicit -Keep, or the confirmation modal's own Keep/Discard
    // button): resolves m_PendingKeepTweaksCommands right now and flags the real (deferred)
    // StopPlaySession to run next frame. KeepOverride absent: finds out whether there's even anything
    // to ask about; if not, Stops immediately same as always; if so, stages the pending commands,
    // freezes the world (Paused - nothing else should happen mid-question) and sets
    // m_bAwaitingKeepTweaksAnswer so RenderKeepTweaksModal (below) opens the dialog on the very next
    // frame - the real Stop stays on hold until that dialog (or a script's own follow-up -Keep call)
    // answers it. Returns a short status string - useful for a CLI/AI caller, ignored by the button.
    //---------------------------------------------------------------------------
    inline std::string RequestStop(editor_context& Ed, std::optional<bool> KeepOverride) noexcept
    {
        auto& State = Ed.State();
        if (State.m_PlayState == editor_state::play_state::Stopped) return "Stop: already stopped";

        if (KeepOverride.has_value())
        {
            State.m_PendingKeepTweaksCommands = *KeepOverride ? CollectPlayModeKeepCommands(Ed.m_Undo, State.m_PlayHistoryBoundary) : std::vector<std::string>{};
            State.m_bAwaitingKeepTweaksAnswer = false;
            State.m_bStopRequested = true;
            return "Stop requested";
        }

        auto Pending = CollectPlayModeKeepCommands(Ed.m_Undo, State.m_PlayHistoryBoundary);
        if (Pending.empty())
        {
            State.m_PendingKeepTweaksCommands.clear();
            State.m_bStopRequested = true;
            return "Stop requested";
        }

        State.m_PendingKeepTweaksCommands = std::move(Pending);
        State.m_bAwaitingKeepTweaksAnswer = true;
        State.m_PlayState = editor_state::play_state::Paused;
        return std::format("Stop: {} propert{} changed during Play - pass -Keep true|false, or answer the confirmation dialog"
            , State.m_PendingKeepTweaksCommands.size(), State.m_PendingKeepTweaksCommands.size() == 1 ? "y" : "ies");
    }

    //---------------------------------------------------------------------------
    // "You changed N properties while Playing - keep them?" - renders every frame regardless (same
    // ImGui::OpenPopup/BeginPopupModal-every-frame convention the Asset Tree's own
    // RenderPendingConfirmationModal already established, E10_asset_browser_files_tab.h), so the
    // dialog keeps showing across frames until answered, including one opened by a CLI Stop call with
    // no -Keep (RequestStop sets the exact same m_bAwaitingKeepTweaksAnswer flag either way). Only
    // decides Keep-vs-Discard here; the real Stop itself still runs at the usual deferred, safe frame
    // boundary (RequestStop just re-flags m_bStopRequested).
    //---------------------------------------------------------------------------
    inline void RenderKeepTweaksModal(editor_context& Ed) noexcept
    {
        auto& State = Ed.State();
        if (State.m_bAwaitingKeepTweaksAnswer)
            ImGui::OpenPopup("Keep Play Mode Changes?");

        if (ImGui::BeginPopupModal("Keep Play Mode Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto Count = State.m_PendingKeepTweaksCommands.size();
            ImGui::Text("You changed %zu propert%s while Playing.", Count, Count == 1 ? "y" : "ies");
            ImGui::TextWrapped("Keep them in the scene, or discard and revert to how it was before Play?");
            ImGui::Separator();

            if (ImGui::Button("Keep", ImVec2(120.0f, 0.0f)))
            {
                RequestStop(Ed, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f)))
            {
                RequestStop(Ed, false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
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
    // On Rebuilt: runs the full destroy/recreate/DLL-swap sequence via ReloadGameModule, always via the
    // raw in-memory snapshot bridge (persist_mode::RawSnapshotBridge) regardless of Play state - see
    // persist_mode's own comment for why this reload never touches disk on its own anymore. If a Play
    // was ALSO requested (the user pressed Play while a rebuild happened to be needed), enters play
    // directly afterward - but MUST write V1 explicitly here (see below), since the reload itself no
    // longer does that as a side effect the way the old DiskSaveAndReload mode used to.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN >
    bool PollGameReload( editor_context& Ed, game_plugin_state& Plugin, T_REGISTER_HOST_COMPONENTS_FN&& RegisterHostComponents ) noexcept
    {
        auto& State = Ed.State();
        if (!Plugin.m_bBuilding) return false;
        if (Plugin.m_BuildFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;

        const build_result Result = Plugin.m_BuildFuture.get();
        Plugin.m_bBuilding = false;

        if (Result == build_result::Failed)
        {
            CancelPlayRequest(State);
            return false;
        }

        if (Result == build_result::UpToDate)
        {
            if (State.m_bPlayRequested)
            {
                State.m_bPlayRequested = false;
                EnterPlaying(Ed);
            }
            return false;
        }

        // Result == build_result::Rebuilt
        const bool bLoaded = ReloadGameModule(Plugin, RegisterHostComponents);

        if (State.m_bPlayRequested)
        {
            State.m_bPlayRequested = false;
                EnterPlaying(Ed);
        }

        return bLoaded;
    }

} // namespace e29

#endif // E29_PLAY_SESSION_H
