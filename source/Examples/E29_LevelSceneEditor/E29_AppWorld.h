#pragma once

namespace e29
{
    // This editor's world: a fresh one with its systems, project paths, System Registry order and inspector wiring.
    // (Component types are registered once for the whole process, not per world.)
    inline void app::CreateWorld()
    {
        pGameMgr = std::make_unique<xecs::game_mgr::instance>();

        RegisterHostSystems(*pGameMgr);
        RegisterGamePluginSystems(*pGameMgr, GamePlugin);

        pGameMgr->m_SceneMgr.m_ProjectPath  = ProjectPath;
        pGameMgr->m_LevelMgr.m_ProjectPath  = ProjectPath;
        pGameMgr->m_PrefabMgr.m_ProjectPath = ProjectPath;
        pGameMgr->m_SystemMgr.m_ProjectPath = ProjectPath;
        if (auto Err = pGameMgr->m_SystemMgr.Load(); Err)
            xeditor::NotifyError(std::format("Failed to load System Registry order: {}", Err.getMessage()));


        InspectorBridge.RegisterCallbacks(EntityInspector, *pGameMgr, State, E29Undo);
    }

    // Repopulates a freshly created world: from the raw snapshot taken just before a Game.dll reload, or by reopening the
    // level from disk (Stop: V1 was a real save, so "reload from disk" already means "restore V1").
    inline void app::RestoreWorld(persist_mode PersistMode)
    {
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
            ReattachOpenScenes(*pGameMgr, std::move(m_ReloadCapture));

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

    }

    // ---- Game.dll reload events (see game_module_events)
    inline void app::CollectRequiredComponents(std::vector<xecs::scene::component_dependency>& Out)
    {
        for (auto& SceneGuid : State.m_OpenScenes)
            for (auto& Dep : xecs::scene::LoadSceneComponentDependencies(ProjectPath, SceneGuid))
                Out.push_back(Dep);
    }

    inline void app::BeforeReload()
    {
        // Captured before the destroy, only for the raw snapshot bridge: entity ids round-trip identical through it.
        m_ReloadCapture = CaptureOpenScenes(*pGameMgr, State);
        SaveSnapshot(*pGameMgr, GetReloadBridgeSnapshotPath());
        pGameMgr.reset();
    }

    inline void app::AfterReload()
    {
        CreateWorld();
        RestoreWorld(persist_mode::RawSnapshotBridge);
    }

    //---------------------------------------------------------------------------
    // The "Stop" button's own handler - always discards whatever a play session did (including any
    // mid-play raw-snapshot reloads along the way - see BeforeReload/AfterReload) in favor of a
    // proper, fully correct reload from the last real disk save (the one Play itself made on the way
    // in - see the Play button's own handler in E29_LevelScene_Editor.cpp). No DLL swap here: Stop
    // doesn't imply a code change, so the currently loaded generation is re-registered in place
    // (bSwapDll=false) rather than paying for an unload/reload cycle it doesn't need.
    //
    // KeepCommands is whatever RequestStop/the confirmation modal already decided (empty = nothing to
    // keep, the common case) - JumpTo() properly Undoes every play-session entry off the still-live
    // pre-Stop world (cheap - about to be replaced anyway) BEFORE it's replaced, then the usual V1/
    // OpenLevel restore runs unchanged, then TruncateRedoBranch() guarantees the stale play-session
    // tail is gone from history even when KeepCommands is empty (without it, those entries would sit
    // Redo()-able, reopening the exact "Ctrl+Z/Redo after Stop gets weird" hazard this whole pass
    // exists to close) - and only then are the kept commands replayed, as brand-new SetProperty calls
    // against the just-restored scene (prefab-override bookkeeping included, same as any manual edit).
    //
    // Reapplied as ONE grouped command (RunGroup, E29_CommandContext.h - the exact same "N sub-
    // commands, one history entry" primitive multi-item asset Delete/Paste already uses), not one
    // Run() per property - direct user correction: the user's own "yes, keep these" answer is itself
    // one decision, so undoing it should be one Ctrl+Z, not N separate steps to peel back one property
    // at a time. Stop itself (the Playing->Stopped transition) still isn't undo-routed - same
    // "transport state, not scene content" reasoning stop_query_cmd's own comment already gives - only
    // the merged property VALUES are.
    //---------------------------------------------------------------------------
    inline void app::StopPlay(const std::vector<std::string>& KeepCommands)
    {
        auto& Undo = LevelDocUndo();
        Undo.JumpTo(State.m_PlayHistoryBoundary);

        // Only THIS editor's world is rebuilt: the component registry belongs to the whole process and stays as it is.
        pGameMgr->Stop();
        pGameMgr.reset();
        CreateWorld();
        RestoreWorld(persist_mode::RestoreFromV1);

        Undo.TruncateRedoBranch();
        if (const auto Surviving = FilterSurvivingTargets(*pGameMgr, KeepCommands); !Surviving.empty())
        {
            [[maybe_unused]] const bool bAllApplied = xeditor::RunGroup(Undo, "Keep Play Mode Changes", Surviving);
        }

        State.m_PlayState = editor_state::play_state::Stopped;
        xeditor::host::current()->end_play(&State);
    }
}
