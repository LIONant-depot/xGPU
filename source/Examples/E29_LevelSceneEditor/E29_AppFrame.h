#pragma once

namespace e29
{
    inline void app::Frame()
    {



        ++FrameNumber;



        e29::diagnostics::Log("frame %llu begin", static_cast<unsigned long long>(FrameNumber));



        // No more manual "Reload Game" button - recompiling is something the editor just does for



        // you, per direct user direction to follow Unity's own model. Two automatic triggers only:



        // the window regaining OS focus (the user tabbed back in after editing code - checked here,



        // unconditionally, every frame, since ConsumeWindowFocusGained is edge-triggered/self-



        // consuming and cheap to poll) and the Play button itself (see its own handler below, which



        // sets State.m_bPlayRequested and calls StartGameReload the same way).



#if defined(XECS_BUILD_SHARED)



        if (xgpu::tools::imgui::ConsumeWindowFocusGained())



            EditorHost.on_focus_regain();



#else



        xgpu::tools::imgui::ConsumeWindowFocusGained(); // still consume the edge - just nothing to react to without a Game.dll



#endif







        // Checked unconditionally, every frame, BEFORE BeginRendering starts this frame - not run



        // synchronously at the point of a button click. Confirmed empirically (same methodology as



        // the startup xeditor::NotifyError()-timing bug this session already found and fixed): the actual



        // destroy-and-recreate-the-whole-world sequence is heavy enough, and doing it nested inside



        // an active ImGui::BeginMainMenuBar()/EndMainMenuBar() scope, that running it synchronously



        // corrupted ImGui's window-stack bookkeeping the same way. Running it here instead gives it



        // a clean "no active ImGui frame" execution context, same as every other safe xeditor::NotifyError()/



        // heavy-state-mutation call site in this file.



        // PollGameReload itself is a no-op (returns immediately) unless a build kicked off by



        // StartGameReload (focus-regain or Play, above) is both in-flight and finished - see its own



        // comment.



        e29::PollGameReload(CmdContext, GamePlugin, RegisterHostComponents);







        // A no-op unless a pipe client (xeditorcli) has a request waiting - see



        // extensions/command_console/E29_CommandConsolePipe.h's own comment for why this must run here (same clean



        // frame boundary as PollGameReload above) rather than after BeginRendering the way E27's own



        // equivalent pump does. Comparing EditorHost.m_ConsoleLog's size before/after (rather than threading a new



        // parameter into PumpCommandConsolePipe itself) is how Idle Work (extensions/idle_work/E29_IdleWork.h) learns a



        // CLI/AI command actually ran this frame - it only ever appends, never shrinks, so a size



        // change means real activity happened.



        const auto ConsoleLogCountBefore = EditorHost.m_ConsoleLog.size();



        e29::PumpCommandConsolePipe(ConsolePipeBridge, E29History, EditorHost.m_ConsoleLog);



        if (EditorHost.m_ConsoleLog.size() != ConsoleLogCountBefore)



            e29::NotifyActivity(IdleWork);







        // Deferred "Stop" click (see the button's own comment) - runs here, same clean frame



        // boundary as PollGameReload above, never nested inside an active ImGui menu-bar scope.



        // State.m_PendingKeepTweaksCommands has already been decided by the time this flag is set



        // (RequestStop/E29_PlaySession.h resolves it immediately - either right away, via -Keep or the



        // confirmation modal's own button, or finds nothing to ask about) - never re-collected here.



        if (State.m_bStopRequested)



        {



            State.m_bStopRequested = false;



            StopPlay(State.m_PendingKeepTweaksCommands);



            State.m_PendingKeepTweaksCommands.clear();



        }







        // The main dockspace hosts complete editor contexts. The Level Editor context in turn owns



        // its private nested dockspace for tools such as Level, Inspector, and Commands.



        if (xgpu::tools::imgui::BeginRendering(true))



        {



            e29::diagnostics::Log("frame %llu BeginRendering skipped", static_cast<unsigned long long>(FrameNumber));



            return;



        }



        e29::diagnostics::Log("frame %llu BeginRendering complete", static_cast<unsigned long long>(FrameNumber));







        // Real mouse/keyboard activity this frame resets Idle Work's clock AND cancels any in-flight



        // idle task ASAP (direct user request) - checked right after BeginRendering (which polls this



        // frame's input), same as every other per-frame pump here. CLI/pipe activity (below) only



        // resets the clock, never cancels - see RequestIdleWorkCancel's own comment for why (an



        // AI/script command that itself STARTS idle work, e.g. RunSanityCheck, must not immediately



        // kill the very thing it just asked for).



        if (e29::DetectUserInputActivity())



        {



            e29::NotifyActivity(IdleWork);



            e29::RequestIdleWorkCancel();



        }



        EditorHost.pump_services();














        std::string LevelTabName;



        if (!State.m_CurrentLevel.empty())



            e29::RemapGUIDToString(LevelTabName, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });



        else



            LevelTabName = "Level";



        xresource::full_guid LevelDockGuid{};



        if (!State.m_CurrentLevel.empty())



        {



            LevelDockGuid.m_Instance = State.m_CurrentLevel.m_Instance;



            LevelDockGuid.m_Type     = xecs::level::type_guid_v;



        }



        if (State.m_bAwaitingSaveBeforeClose)
            State.m_bLevelEditorOpen = true;

        const bool bSkipLevelPeer =
            !State.m_bLevelEditorOpen
            && State.m_CurrentLevel.empty()
            && State.m_OpenScenes.empty()
            && !State.m_bAwaitingSaveBeforeClose;

        bool bParentEditorVisible = false;
        if (!bSkipLevelPeer)
        {
            bool bLevelTabOpen = State.m_bLevelEditorOpen;
            bParentEditorVisible = e29::editor_tabs::RenderLevelEditorDockspace(
                [this]() { RenderParentEditorToolbar(); }, LevelTabName.c_str(), &Device, xecs::level::type_guid_v, LevelDockGuid, &bLevelTabOpen);
            if (!bLevelTabOpen)
            {
                e29::RequestCloseLevel(*pGameMgr, State, CmdContext.m_Undo);
                State.m_bLevelEditorOpen =
                    State.m_bAwaitingSaveBeforeClose
                    || !State.m_CurrentLevel.empty()
                    || !State.m_OpenScenes.empty();
            }
            else
            {
                State.m_bLevelEditorOpen = true;
            }
        }



        e29::diagnostics::Log



        ( "frame %llu Parent Editor Window visible=%d"



        , static_cast<unsigned long long>(FrameNumber), bParentEditorVisible ? 1 : 0



        );



        if (bParentEditorVisible)



        {



            e29::diagnostics::Log("frame %llu Parent Editor Window active", static_cast<unsigned long long>(FrameNumber));







        EditorHost.m_Notifier.render();



        e29::RenderKeepTweaksModal(CmdContext);



        e29::RenderRemoveDependencyConfirmModal(E29Undo);



        e29::RenderSaveBeforeCloseModal(*pGameMgr, State, CmdContext.m_Undo);



        // Modal may have opened a Level after Save/Don't Save - same reload kick as an



        // immediate RequestOpenLevel that returned true.



        if (State.m_bPendingStartGameReloadAfterOpen)



        {



            State.m_bPendingStartGameReloadAfterOpen = false;



#if defined(XECS_BUILD_SHARED)



            e29::StartGameReload(GamePlugin);



#endif



        }







        // The menu item above only ever LABELS "Ctrl+S" - ImGui::MenuItem's shortcut string is



        // purely decorative and doesn't bind anything on its own. Checked once per frame,



        // unconditionally (not gated behind the File menu being open).



        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)



            && !State.isPlaying()



            && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty())



            && e29::HasUnsavedDocumentChanges(State, CmdContext.m_Undo))



        {



            e29::SaveEverything(*pGameMgr, State);



            e29::MarkDocumentClean(State, CmdContext.m_Undo);



        }







        // Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z for Redo) - same shortcut convention as E27_NodeOS's own



        // (E27_NodeOS_Editor.cpp), guarded by WantTextInput so typing "z" into a property text field



        // never gets mistaken for an undo shortcut. Gated on !State.isPlaying() - same gate Ctrl+S



        // already has just above - since undoing/redoing a structural command (CreateEntity/



        // DeleteEntity/AddComponent/RemoveComponent) against the live ticking world is untested



        // territory and could interact badly with the V1/Vn snapshot-restore sequence Play/Stop



        // relies on (documentation/E29_LevelSceneEditor/command_undo_known_gaps.md). The Undo/Redo QUERY commands



        // (E29_Commands_Workspace.h) carry the same gate, so a CLI/Console-driven agent can't bypass



        // what the UI shortcut refuses either.



        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !State.isPlaying())



        {



            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().KeyShift) CmdContext.m_Undo.Undo();



            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyShift)) CmdContext.m_Undo.Redo();



        }


        // GameMgr.Run() ticks every enabled Update system in its current order (via



        // m_SystemMgr.Run()) and, on the Stopped->Running transition, snapshots the System



        // Registry's current order/enabled state for GameMgr.Stop() to restore later. Only called



        // while actually Playing - Paused deliberately calls neither Run() nor Stop() every frame



        // (the world just sits there, unticked, exactly as it was); Stop() itself is no longer an



        // idempotent per-frame call at all, it's the one-shot StopPlaySession triggered by the Stop



        // button above (see its own comment for why a full world-rebuild can't run unconditionally



        // every frame the way this simpler Run()/Stop() toggle used to). E29 has no viewport yet, so



        // "Play" here only means "the ECS's own systems tick" - proving the System Registry feature,



        // not adding a game view.



        }













        // The Step button's one-shot flag is consumed HERE, at the same real tick gate, regardless of



        // which state it was set from - see editor_state::m_bStepOneFrame's own comment.



        if (State.m_PlayState == e29::editor_state::play_state::Playing)



        {



            pGameMgr->Run();



            if (State.m_bStepOneFrame)



            {



                State.m_bStepOneFrame = false;



                State.m_PlayState = e29::editor_state::play_state::Paused; // Stopped->Step: run exactly the first tick, then land Paused



            }



        }



        else if (State.m_PlayState == e29::editor_state::play_state::Paused && State.m_bStepOneFrame)



        {



            State.m_bStepOneFrame = false;



            pGameMgr->Run(); // one tick, stays Paused



        }







        // Asset open drain always (drawer works with Level peer closed).
        {
        // Asset browser windows live in the Host Drawer (Resources/Assets/Compilation tabs).
        // Keep popup picker + selection drain here; EnsureInitialized so getNewAsset works.

        AsserBrowser.SetDevice(Device);

        AsserBrowser.EnsureInitialized(e10::g_LibMgr, xresource::g_Mgr);



        e29::g_AssetBrowserPopup.SetDevice(Device);

        e29::g_AssetBrowserPopup.RenderAsPopup(e10::g_LibMgr, xresource::g_Mgr);







        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false)



        {



            if (NewAsset.m_Type == xecs::level::type_guid_v)



            {



                // Close-current-first when dirty (Save/Don't Save/Cancel); same OpenLevel +



                // StartGameReload path once the document action finishes.



#if defined(XECS_BUILD_SHARED)



                if (e29::RequestOpenLevel(*pGameMgr, State, CmdContext.m_Undo, NewAsset, /*bStartGameReload*/ true))



                    e29::StartGameReload(GamePlugin);



#else



                e29::RequestOpenLevel(*pGameMgr, State, CmdContext.m_Undo, NewAsset, /*bStartGameReload*/ false);



#endif



            }



            else if (NewAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, NewAsset);



        }



        else if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false)



        {



            if (SelAsset.m_Type == xecs::level::type_guid_v)



            {



#if defined(XECS_BUILD_SHARED)



                if (e29::RequestOpenLevel(*pGameMgr, State, CmdContext.m_Undo, SelAsset, /*bStartGameReload*/ true))



                    e29::StartGameReload(GamePlugin);



#else



                e29::RequestOpenLevel(*pGameMgr, State, CmdContext.m_Undo, SelAsset, /*bStartGameReload*/ false);



#endif



            }



            else if (SelAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, SelAsset);



        }







        
        } // asset open drain

        // Level peer panels (Tree/Inspector/Systems/Editor) only while Level root is open.
        if (bParentEditorVisible)
        {
// Read-only when another session holds Level/scene write locks (DESIGN 4.2).

        
        if (State.m_bPlayBusyPopup)
        {
            ImGui::OpenPopup("##PlayBusy");
            State.m_bPlayBusyPopup = false;
        }
        if (ImGui::BeginPopupModal("##PlayBusy", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Play is already active in another Level editor.");
            ImGui::Text("Stop that Play first, then try again.");
            if (ImGui::Button("OK", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        const bool bLevelWritable = e29::IsLevelWritable(&EditorHost, LevelHostSession.pLive, State);

        if (!bLevelWritable)

        {

            e29::editor_tabs::SetNextLevelEditorToolClass();

            if (ImGui::Begin("##LevelReadOnlyBanner", nullptr,

                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))

            {

                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),

                    "Read-only: this Level/scene is being edited in another session.");

            }

            ImGui::End();

        }



        e29::diagnostics::Log("frame %llu level tree render begin", static_cast<unsigned long long>(FrameNumber));



        e29::editor_tabs::SetNextLevelEditorToolClass();



        e29::RenderLevelTreePanel(CmdContext, E29Undo, !bLevelWritable);



        e29::diagnostics::Log("frame %llu level tree render end", static_cast<unsigned long long>(FrameNumber));



        // Level drop from Resources onto Level Tree (deferred during panel draw) - goes through



        // RequestOpenLevel so a dirty open Level prompts Save/Don't Save/Cancel first.



        if (e29::FlushPendingOpenLevelFromTree(*pGameMgr, State, CmdContext.m_Undo))



            e29::StartGameReload(GamePlugin);



        e29::diagnostics::Log("frame %llu entity properties render begin", static_cast<unsigned long long>(FrameNumber));



        e29::editor_tabs::SetNextLevelEditorToolClass();



        e29::RenderEntityPropertiesPanel(CmdContext, EntityInspector, InspectorBridge, !bLevelWritable);



        e29::diagnostics::Log("frame %llu entity properties render end", static_cast<unsigned long long>(FrameNumber));



        e29::diagnostics::Log("frame %llu system registry render begin", static_cast<unsigned long long>(FrameNumber));



        e29::editor_tabs::SetNextLevelEditorToolClass();



        e29::RenderSystemRegistryPanel(*pGameMgr, State);



        e29::diagnostics::Log("frame %llu system registry render end", static_cast<unsigned long long>(FrameNumber));














        ImGui::SetNextWindowPos(ImVec2(250.0f, 90.0f), ImGuiCond_FirstUseEver);



        ImGui::SetNextWindowSize(ImVec2(1050.0f, 480.0f), ImGuiCond_FirstUseEver);



        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));



        e29::editor_tabs::SetNextLevelEditorToolClass();



        const bool bEditorWindowVisible = ImGui::Begin(e29::editor_tabs::kEditorWindow);



        e29::diagnostics::Log("window begin: %s visible=%d", e29::editor_tabs::kEditorWindow, bEditorWindowVisible ? 1 : 0);



        if (bEditorWindowVisible)



        {



            e29::diagnostics::Log("toolbar host render begin");



            ximgui::toolbar::RenderToolbarHost



            ( EditorToolbarHost



            , ImGui::GetContentRegionAvail()



            , [this](const char* Name, ximgui::toolbar::axis Axis) { RenderEditorToolbar(Name, Axis); }



            , [&]()



            {



                ImGui::TextDisabled("Editor");



            }



            );



            e29::diagnostics::Log("toolbar host render end");



        }



        ImGui::End();



        e29::diagnostics::Log("window end: %s", e29::editor_tabs::kEditorWindow);



        ImGui::PopStyleVar();







        // Host services (Idle / Log / Commands / SC) live in the Host Drawer (Space).

        e29::RenderReloadCompatibilityModal(CmdContext);







        }







        // Editor Framework: this window is a root-level PEER of "Level Editor" (like Level Editor



        // itself, not one of its internal child panels), so it must render every frame



        // unconditionally - gating it behind bParentEditorVisible (as every internal panel above



        // correctly is) meant it silently stopped calling ImGui::Begin() the instant the user



        // clicked its own tab (since that made Level Editor itself the hidden one), which is



        // exactly why clicking the tab looked like it did nothing.



        LevelHostSession.Sync(EditorHost, State);
        e29::SyncOpenTextureEditorsToHost(EditorHost);
        e29::RenderOpenTextureEditors();
        // Host Drawer last so it stacks above Level/Texture peer windows (same OS window).
        EditorHost.draw_host_drawers();



        xgpu::tools::imgui::Render();



        MainWindow.PageFlip();



        xresource::g_Mgr.OnEndFrameDelegate();



        e29::diagnostics::Log("frame %llu end", static_cast<unsigned long long>(FrameNumber));



    }
}
