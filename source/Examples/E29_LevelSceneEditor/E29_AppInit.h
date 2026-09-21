#pragma once

namespace e29
{
    inline int app::Init()
    {



        e29::diagnostics::Start();

        e29::diagnostics::InstallCrtReportHook();

        e29::diagnostics::InstallTerminateHandler();

        e29::diagnostics::Log("startup: E29_Example begin");

        e29::diagnostics::Log("startup: creating xgpu instance");

        if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_pLogErrorFunc = e29::Debugger, .m_pLogWarning = e29::Debugger }); Err)

        {

            e29::diagnostics::Log("startup: xgpu instance creation failed");

            e29::diagnostics::RemoveCrtReportHook();

            e29::diagnostics::RemoveTerminateHandler();

            e29::diagnostics::Stop();

            return xgpu::getErrorInt(Err);

        }

        e29::diagnostics::Log("startup: creating xgpu device");

        if (auto Err = Instance.Create(Device); Err)

        {

            e29::diagnostics::Log("startup: xgpu device creation failed");

            e29::diagnostics::RemoveCrtReportHook();

            e29::diagnostics::RemoveTerminateHandler();

            e29::diagnostics::Stop();

            return xgpu::getErrorInt(Err);

        }

        e29::diagnostics::Log("startup: creating main window");

        if (auto Err = Device.Create(MainWindow, {}); Err)

        {

            e29::diagnostics::Log("startup: main window creation failed");

            e29::diagnostics::RemoveCrtReportHook();

            e29::diagnostics::RemoveTerminateHandler();

            e29::diagnostics::Stop();

            return xgpu::getErrorInt(Err);

        }



        e29::diagnostics::Log("startup: initializing resource manager");

        xresource::g_Mgr.Initiallize(20000);

        ResourceMgrUserData.m_Device = Device;

        xresource::g_Mgr.setUserData(&ResourceMgrUserData, false);



        //

        // Setup Imgui interface

        //

        e29::diagnostics::Log("startup: xgpu/imgui CreateInstance begin");

        e29::g_pTextureEditorDevice = &Device;

        xgpu::tools::imgui::CreateInstance(MainWindow);

        e29::diagnostics::Log("startup: xgpu/imgui CreateInstance complete");

        e29::diagnostics::Log("startup: applying E29 theme begin");

        e29::theme::ApplyUnityInspiredTheme();

        e29::diagnostics::Log("startup: applying E29 theme complete");



        // io.FontDefault (not a per-frame PushFont) - xgpu::tools::imgui::BeginRendering() calls

        // ImGui::DockSpace() internally, BEFORE E29's own render code ever runs, and ImGui's docking tab

        // bar renders using whatever font is current AT THAT POINT - a PushFont in E29's own loop (after

        // BeginRendering returns) is too late to affect it, which is exactly why the dock tab labels

        // ("Resources"/"Assets"/...) kept rendering in the old default Consolas even after every panel's

        // own content switched to Segoe UI. Overriding io.FontDefault instead affects ImGui::NewFrame()'s

        // own g.Font reset, which runs before DockSpace() - this is process-global IO state, but safe here

        // because every xGPU example is its own separate process (E10/E19-28 never call this line).

        e29::diagnostics::Log("startup: selecting E29 default font begin");

        ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->Fonts[4];

        e29::diagnostics::Log("startup: selecting E29 default font complete");



        //

        // ECS setup - first xGPU example to own an xecs::game_mgr::instance. A unique_ptr (not a plain

        // stack value) specifically so Phase 8's PollGameReload can destroy and reconstruct the whole world

        // in place - see E29_GamePlugin.h's own comment on why that's the correct, sufficient operation

        // for a hot reload rather than something narrower.

        //

        e29::diagnostics::Log("startup: constructing ECS game manager begin");
        pGameMgr = std::make_unique<xecs::game_mgr::instance>();

        e29::diagnostics::Log("startup: constructing ECS game manager complete");



        e29::diagnostics::Log("startup: registering host components begin");

        RegisterHostComponents(*pGameMgr);

        e29::diagnostics::Log("startup: registering host components complete");



        // E29's sample Game.dll (source/Examples/E29_LevelSceneEditor/GameProject/E29_Game.cpp) -

        // loading it here, BEFORE RegisterSystems below locks the component registry, is what makes an

        // initial load possible without a full reload; only a SUBSEQUENT swap (hot reload while already

        // running) needs PollGameReload's destroy-and-recreate sequence. Missing/failing to load is not

        // an error - E29 runs exactly as before with no game loaded, matching the "user builds it, or

        // E29 does" direction: nothing has been built yet on a fresh checkout, and that's fine.

        //

        // Synchronous here (unlike a live focus-regain/Play-triggered recompile, which never blocks the

        // render loop - see StartGameReload/PollGameReload) - this runs before the window has rendered its first

        // frame at all, so there's no live UI to freeze yet; a one-time pause here on a fresh checkout

        // is a materially different, much smaller cost than freezing an editor the user is actively

        // working in.

        //

        // Gated behind XECS_BUILD_SHARED (only defined when CMake's XECS_BUILD_SHARED_LIBRARY option is

        // ON - see CMakeLists.txt and E29_Game.cpp's own top comment): a Game.dll only makes sense when

        // xECSV2 itself is a shared library, since it depends on RegisterComponents/RegisterSystems

        // mutating the ONE shared, cross-module component registry - in the default (non-shared) build,

        // the E29_Game CMake target isn't even defined, so BuildGamePluginIfStale's own cmake invocation

        // would just fail with "target not found" every single time it ran (once per focus-regain,

        // forever). Confirmed live: that failure also silently cancelled every Play request, since

        // PollGameReload's Failed branch clears State.m_bPlayRequested unconditionally - without this

        // guard, Play never actually worked in the default build config at all.

#if defined(XECS_BUILD_SHARED)

        {

            TCHAR szModulePath[MAX_PATH];

            GetModuleFileName(NULL, szModulePath, MAX_PATH);

            std::filesystem::path GameDllPath = std::filesystem::path(szModulePath).parent_path() / L"E29_Game.dll";

            GamePlugin.m_CompiledDllPath = GameDllPath.wstring();

            // Synchronous, main-thread-only call site (see this block's own top comment) - safe to call

            // GetLatestModuleSourceWriteTime() directly here, unlike StartGameReload's own background-

            // thread call site (see BuildGamePluginIfStale's own comment on why that one takes it as a

            // precomputed parameter instead).

            e29::BuildGamePluginIfStale(GamePlugin, e29::GetLatestModuleSourceWriteTime());

            e29::LoadGamePluginComponents(*pGameMgr, GamePlugin, /*Generation*/ 1);

        }

#else

        e29::LogGamePlugin("Game.dll: this build was configured without XECS_BUILD_SHARED_LIBRARY (see CMakeLists.txt) - Game.dll support is disabled, Play just ticks the host's own systems.");

#endif



        e29::diagnostics::Log("startup: registering host systems begin");

        RegisterHostSystems(*pGameMgr);

        e29::diagnostics::Log("startup: registering host systems complete");

        e29::diagnostics::Log("startup: registering game plugin systems begin");

        e29::RegisterGamePluginSystems(*pGameMgr, GamePlugin);

        e29::diagnostics::Log("startup: registering game plugin systems complete");

        {

            e29::diagnostics::Log("startup: opening project begin");

            TCHAR szModulePath[MAX_PATH];

            GetModuleFileName(NULL, szModulePath, MAX_PATH);



            std::filesystem::path RepoRoot;

            for (std::filesystem::path Dir = std::filesystem::path(szModulePath).parent_path(); ; )

            {

                std::error_code Ec;

                if (std::filesystem::exists(Dir / L"example.lionprj" / L"Cache" / L"Plugins", Ec) && !Ec)

                {

                    RepoRoot = Dir;

                    break;

                }

                const std::filesystem::path Parent = Dir.parent_path();

                if (Parent.empty() || Parent == Dir)

                    break; // reached the filesystem root without finding a bootstrapped project

                Dir = Parent;

            }



            if (!RepoRoot.empty())

            {

                const std::wstring ProjectPathW = (RepoRoot / L"example.lionprj").wstring();

                TCHAR szFileName[MAX_PATH];

                wcscpy_s(szFileName, MAX_PATH, ProjectPathW.c_str());



                const std::filesystem::path ProjectPathForLog(szFileName);

                const std::filesystem::path PluginPathForLog = ProjectPathForLog / "cache" / "plugins";

                std::error_code PluginPathError;

                const bool bPluginPathExists = std::filesystem::exists(PluginPathForLog, PluginPathError);

                e29::diagnostics::Log

                ( "startup: project path=%s plugin path=%s exists=%d ec=%d"

                , ProjectPathForLog.string().c_str(), PluginPathForLog.string().c_str()

                , bPluginPathExists ? 1 : 0, PluginPathError.value()

                );

                if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)

                {

                    e29::Debugger(Err.getMessage());

                    e29::diagnostics::Log("startup: opening project failed");

                    e29::diagnostics::RemoveCrtReportHook();

                    e29::diagnostics::RemoveTerminateHandler();

                    e29::diagnostics::Stop();

                    return 1;

                }

                e29::diagnostics::Log("startup: opening project complete");



                ImGuiIO& io = ImGui::GetIO();

                static std::string IniSave = std::format("{}/Assets/imgui_e29.ini", xstrtool::To(szFileName));

                io.IniFilename = IniSave.c_str();



                ProjectPath = e10::g_LibMgr.m_ProjectPath;

                xresource::g_Mgr.setRootPath(std::format(L"{}//Cache//Resources//Platforms//Windows", e10::g_LibMgr.m_ProjectPath));

                pGameMgr->m_SceneMgr.m_ProjectPath  = ProjectPath;

                pGameMgr->m_LevelMgr.m_ProjectPath  = ProjectPath;

                pGameMgr->m_PrefabMgr.m_ProjectPath = ProjectPath;

                pGameMgr->m_SystemMgr.m_ProjectPath = ProjectPath;



                // Applies whatever Update-system order/enabled state was last saved through the System

                // Registry panel - must run AFTER RegisterSystems<...>() above has populated

                // m_SystemMgr's own update-system list; a missing file (nothing saved yet) is not an

                // error, registration order simply stands as-is.

                if (auto Err = pGameMgr->m_SystemMgr.Load(); Err)

                    e29::Debugger(std::format("Failed to load System Registry order: {}", Err.getMessage()));



                if (auto Err = e29::LoadScriptConfig(ProjectPath, e29::g_ScriptConfig); Err)

                    e29::Debugger(std::format("Failed to load Script.config.txt: {}", Err.getMessage()));

                // Keeps GameProject\E29_Game_Modules.cmake in sync with whatever was actually persisted,

                // regardless of how it got there (a fresh checkout may have no fragment yet at all).

                e29::RegenerateGameModuleSources();

            }

            else

            {

                e29::diagnostics::Log("startup: could not locate a bootstrapped example.lionprj above the executable");

            }

        }

        e29::diagnostics::Log("startup: editor state and asset browser constructed");



        // Lets entity_to_prefab_drop::OnDrop (a static, globally-registered object) reach the live

        // GameMgr/State at drop time - see their own declaration comment for why this is safe here.

        // Rebound by PollGameReload after a hot reload replaces *pGameMgr with a fresh instance.

        e29::g_pGameMgr    = pGameMgr.get();

        e29::g_pState      = &State;

        e29::g_pGamePlugin = &GamePlugin;

        e29::g_MakePrefabDropHandler = &e29::MakePrefabDropViaCommands;



        // Visible from the start and never closable - browsing/creating Levels and Scenes is this

        // editor's primary activity (not an occasional lookup), so it's a permanent, dockable part of the

        // layout rather than a modal picker: DOCKABLE drops the bottom Close button and lets it dock like

        // Level Editor/Entity Properties instead of floating as an undockable overlay. (The "+" pickers

        // elsewhere in this file use a separate e10::assert_browser instance, e29::g_AssetBrowserPopup,

        // which stays at the POPUP default.)

        AsserBrowser.setDisplayMode(e10::assert_browser::display_mode::DOCKABLE);

        AsserBrowser.SetWindowName(e29::editor_tabs::kResourceBrowserWindow);

            // Host Drawer owns Resources/Assets/Compilation/Project Settings — not Parent dock class.

        AsserBrowser.Show(true);

        EditorHost.m_pExternalWorkspace = &E29Undo;

        if (auto Err = E29Undo.Init({}, false); !Err.empty())

            e29::Debugger(std::format("E29: xundo Init failed: {}", Err));

        e29::RegisterLevelEditorDescriptor();

        EditorHost.provide(LevelHostSession);
        LevelHostSession.EnsureCreated(State, pGameMgr.get());
        Commands.emplace(E29Undo, LevelDocUndo(), &CmdContext);

        E29History.AddSystem("E29", 1, E29Undo);

        std::thread(e29::CommandConsolePipeThreadMain, std::ref(ConsolePipeBridge)).detach();



        // Lets e29::commands::Run() (E29_CommandContext.h, called by every UI-driven command - tree

        // clicks, property edits, add/remove component, create/delete entity) log into this SAME console

        // log too, not just pipe-driven/console-typed commands - direct user report: "route the users

        // commands there as well... nothing showing up there yet."

        e29::commands::g_pConsoleLog = &ConsoleLog;

        // xproperty's default row tint (s_ColorCategories, xPropertyImGuiInspector.cpp) is a set of bright

        // matplotlib-style categorical colors, tuned against ImGui's stock dark theme - against

        // E29_Theme.h's darker/flatter Unity palette they read as a clashing, too-bright/too-saturated mess

        // (direct user feedback: "the inspector right now looks horrible"). m_bRenderBackgroundDepth alone

        // only stops DIFFERENT depths getting different hues - every row (including "Value"/"Target"

        // leaves) still tinted from s_ColorCategories[0] (a light peachy tan). Real Unity's own Inspector

        // doesn't tint rows at all - flat background, thin separators only - so both are disabled outright.

        EntityInspector.m_Settings.m_bRenderBackgroundDepth   = false;

        EntityInspector.m_Settings.m_bRenderLeftBackground    = false;

        EntityInspector.m_Settings.m_bRenderRightBackground   = false;

        // The inspector's own row spacing (m_FramePadding/m_ItemSpacing/m_TableFramePadding) is NOT tied

        // to the ambient ImGuiStyle at all - Show() explicitly pushes these per-instance values on top

        // (xPropertyImGuiInspector.cpp), which is why E29_Theme.h's global FramePadding/ItemSpacing

        // reduction had zero visible effect on these rows (direct user report, with a comparison

        // screenshot against Unity's own tightly-packed Transform/Position/Rotation/Scale rows: "Button

        // spacing in ours still much larger vertically... headers too"). Tightened to match.

        EntityInspector.m_Settings.m_FramePadding      = ImVec2(4.0f, 3.0f);   // was {1, 3.5} - +2px per direct user follow-up (the buttons/fields themselves, not the gap between rows)

        // ItemSpacing.x specifically: this is what leaves an unpainted gap between a component header's

        // own left box (TreeNodeEx) and its right-column fill (a separate AddRectFilled call) - confirmed

        // by direct pixel measurement (an ~8px strip of raw background showing through at exactly 2x this

        // value) after a direct user follow-up with a screenshot: "the dark divider that breaks the

        // background color of the header... literally breaks it in two". Not a border/color issue (already

        // checked) - genuinely unpainted space between two separately-drawn rects, from the columns' own

        // gap reservation. Same fix as plugin_tab's own m_Settings override.

        EntityInspector.m_Settings.m_ItemSpacing       = ImVec2(1.0f, 1.0f);   // was {0.5, 2.0}, then {4, 1}

        EntityInspector.m_Settings.m_TableFramePadding = ImVec2(4.0f, 1.0f);   // was {2, 6}

        e29::WireResourcePickerCallbacks(EntityInspector);

        InspectorBridge.RegisterCallbacks(EntityInspector, *pGameMgr, State, E29Undo);

        e29::RegisterAssetBrowserCallbacks(AsserBrowser, E29Undo, MainWindow);



        // "Scripting" section in the merged Plugins/Project Settings tab (e10::plugin_tab,

        // E10_asset_browser_plugin_tab.h) - the project's Script-Module build-membership list

        // (Project.config\Script.config.txt, e29::g_ScriptConfig.m_ModuleRefs), rendered as a normal

        // xproperty::inspector array field (WireResourcePickerCallbacks already gives every full_guid

        // element a working click-to-browse picker for free, and the array gets the standard Unity-style

        // insert/delete/drag controls - see xproperty_array_element_controls). NOT the Dependencies-node

        // drag-drop pattern an earlier pass here used - that assumed the Resources tab could be docked

        // and visible AT THE SAME TIME as this one, which is false: "Project Settings" and "Resources"

        // are tabs in the SAME tab strip, mutually exclusive on screen, so a drag source and this drop

        // target could never both be visible. The inspector's own click-to-open-popup picker has no such

        // docking assumption at all - direct user correction.

        //

        // No m_OnPropertyChanged hook wired here (unlike EntityInspector's own edits, which route through

        // InspectorBridge into the undo system) - a snapshot/diff around the render call instead: simple,

        // catches every mutation kind the array control can make (insert/delete/reorder/reassign)

        // uniformly, and doesn't require raw edits here to go through xundo the way every other project-

        // level-settings edit in this codebase already doesn't either (Library.config.txt's own

        // ParentLibraries has no raw-inspector-edit path at all, only command-driven Add/Remove).

        //

        // Reuses plugin_tab's OWN inherited xproperty::inspector (passed in by RightPanel()) rather than

        // carrying a second, redundant instance - direct user correction: "you have one inspector

        // working with the plugin... why did you reinvent the wheel?". WireResourcePickerCallbacks is

        // registered lazily, once, the first time this section is actually selected - E29 has no way to

        // reach plugin_tab's own instance ahead of time (it's created generically inside assert_browser's

        // own tab list), so "wire on first use" is the only hook point available, not a startup call.

        AsserBrowser.m_ExtraPluginTabSections.push_back(

        {

            "Scripting",

            [](xproperty::inspector& Inspector)

            {

                static bool bWired = false;

                if (!bWired) { e29::WireResourcePickerCallbacks(Inspector); bWired = true; }



                // REAL BUG FOUND LIVE (2026-09-19): rebuilding (clear/AppendEntity/AppendEntityComponent)

                // on EVERY frame - not just when the data actually changed - makes every widget's ImGui id

                // unstable, so a tree node's own open/closed state can never persist between frames; the

                // ModuleRefs array node fought itself and flickered continuously the instant it was

                // expanded. Same documented failure mode as xproperty_inspector_must_persist_across_frames/

                // xgpu_imgui_per_frame_rebuild_activeid_bug - rebuild ONLY when s_BuiltWith says the

                // structure is stale (first render, or the data changed since the frame that built it,

                // whether from this same UI or an external CLI command), never unconditionally.

                static std::vector<xresource::full_guid> s_BuiltWith;

                if (e29::g_ScriptConfig.m_ModuleRefs != s_BuiltWith)

                {

                    Inspector.clear();

                    Inspector.AppendEntity();

                    Inspector.AppendEntityComponent(*xproperty::getObjectByType<e29::script_config>(), &e29::g_ScriptConfig);

                }



                // Separate, frame-local snapshot - did THIS ShowEmbedded call itself edit the array (an

                // insert/delete/reassign via the array's own controls)? Distinct from the staleness check

                // above, which would otherwise misfire a save on the very first render of an already-

                // populated list (stale-vs-s_BuiltWith is true then too, but nothing was actually edited).

                const auto BeforeThisRender = e29::g_ScriptConfig.m_ModuleRefs;



                xproperty::settings::context Context;

                Inspector.ShowEmbedded(Context);



                if (e29::g_ScriptConfig.m_ModuleRefs != BeforeThisRender)

                {

                    if (auto Err = e29::SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, e29::g_ScriptConfig); Err)

                        e29::Debugger(std::format("Failed to save Script.config.txt: {}", Err.getMessage()));

                    e29::RegenerateGameModuleSources();

                }

                s_BuiltWith = e29::g_ScriptConfig.m_ModuleRefs;

            }

        });



        // Source Control status/lock badges (Phase 3) - wired directly here rather than inside

        // RegisterAssetBrowserCallbacks (kit/E29_LevelSceneEditorKit.h): that function is defined in a

        // header included FIRST in this .cpp, before commands/E29_Commands_SourceControl.h and

        // extensions/source_control/E29_SourceControlStatus.h are - a lambda body referencing their

        // symbols from inside that header would fail to compile. This call site, further down the

        // .cpp, is past every needed include.

        //

        // Two separate hooks, not one combined value (direct user design decision, see

        // asset_status_badge/asset_lock_badge's own comment in E10_AssetBrowser.h): a file can be both

        // modified AND locked by you at once, and the lock signal must stay visible either way.

        AsserBrowser.m_OnGetAssetStatusBadge = [](e10::library::guid LibraryGuid, const std::wstring& RelativePath) -> int

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return static_cast<int>(e10::asset_status_badge::None);



            // Untracked (new, not yet known to source control) vs Modified (tracked, has changes) -

            // direct user distinction: "usually most editors have a small + signifying a new file...

            // the dot does usually mean modified". GetCachedFileStatus only ever holds entries git

            // itself reported as changed (see its own comment) - a path present here but with neither

            // flag set (e.g. staged-only) still reads as Modified, matching "not clean" being the

            // meaningful signal for those.

            if (auto Status = e10::source_control::GetCachedFileStatus(RootPath, RelativePath))

                return static_cast<int>(Status->untracked ? e10::asset_status_badge::Untracked : e10::asset_status_badge::Modified);



            // Not in the changed-files cache: Clean if this root has actually been scanned at least

            // once, None (draw nothing) if it hasn't - GetLastRefreshTime is the only way to tell

            // "checked, all good" apart from "haven't checked yet" (see asset_status_badge's own

            // comment on why None and Clean are different values, not the same thing).

            return static_cast<int>(e10::source_control::GetLastRefreshTime(RootPath)

                ? e10::asset_status_badge::Clean : e10::asset_status_badge::None);

        };



        AsserBrowser.m_OnGetAssetLockBadge = [](e10::library::guid LibraryGuid, const std::wstring& RelativePath) -> int

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return static_cast<int>(e10::asset_lock_badge::None);



            if (auto Lock = e10::source_control::GetCachedLockStatus(RootPath, RelativePath))

            {

                return static_cast<int>(Lock->ownership == sc::LockOwnership::CurrentUser

                    ? e10::asset_lock_badge::LockedByMe : e10::asset_lock_badge::LockedByOther);

            }

            return static_cast<int>(e10::asset_lock_badge::None);

        };



        AsserBrowser.m_OnGetSourceControlRevision = []() -> std::uint64_t

        {

            return e10::source_control::SourceControlRevision().load(std::memory_order_relaxed);

        };



        // Lock-before-edit gating (Phase 4B). Always calls PrepareEdit rather than pre-filtering with the

        // status cache: PrepareEdit already runs BatchIsLfsTracked internally and reports success

        // trivially for a non-LFS/text file (see sc_git_lfs_provider.hpp's own PrepareEdit), so there's no

        // separate "is this even lockable" check to duplicate here - one call already covers both "not

        // lockable" and "lockable and I got/kept the lock" as success, and only "lockable but someone else

        // holds it" (or another Require failure) as the one real refusal case files_tab needs to ask about.

        AsserBrowser.m_OnBeforeOpenAssetFile = [](e10::library::guid LibraryGuid, const std::wstring& RelativePath) -> bool

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return true; // not a recognized library - nothing to gate



            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);

            if (!pWorkspace) return true; // not a git working tree



            sc::PrepareEditRequest Request;

            Request.paths = { sc::WorkspacePath{ RelativePath } };

            Request.policy.lockRequirement = sc::LockRequirement::Require;



            const auto Result = pWorkspace->PrepareEdit(Request);

            if (Result.files.empty()) return true; // shouldn't happen - fail open rather than block



            // Same immediate-cache-update fix as the SourceControlLock/Unlock commands - "Open for Edit"

            // reaches PrepareEdit directly (not through the command bus), so it needs its own copy of

            // this rather than relying on theirs.

            if (Result.files.front().coordination.lock)

                e10::source_control::PublishSingleLock(RootPath, RelativePath, Result.files.front().coordination.lock);



            return Result.files.front().OperationSucceeded();

        };



        // Demand-driven scan priority (direct user request, 2026-09-17): when the Asset Tree navigates

        // to a real folder, that folder's status/lock data is requested at HIGH priority right away,

        // rather than waiting for its turn in the idle-triggered background sweep.

        AsserBrowser.m_OnFolderNavigated = [](e10::library::guid LibraryGuid, const std::wstring& RelativeFolderPath)

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return;

            e29::source_control::RequestPriorityScan(RootPath, RelativeFolderPath);

        };



        // Editor Framework: double-click a Texture resource opens the standalone Texture editor

        // (Plugins/xtexture.plugin/source/Editor/xtexture_editor.h) - direct type check for now rather

        // than the generic xeditor::registry lookup (that registry's CreateDocument/CreateUI factories

        // aren't wired up yet - real follow-up work, not done under today's time budget). Every other

        // resource type's double-click behavior is unchanged (today's inert setSelection-only default).

        AsserBrowser.m_OnOpenAsset = [](e10::library::guid LibraryGuid, xresource::full_guid AssetGuid)
            {
                if (AssetGuid.m_Type == xecs::level::type_guid_v)
                {
                    if (e29::g_pGameMgr == nullptr || e29::g_pState == nullptr || e29::FindLevelUndo() == nullptr)
                        return;
                    if (e29::RequestOpenLevel(*e29::g_pGameMgr, *e29::g_pState, *e29::FindLevelUndo(), AssetGuid, /*bStartGameReload*/ true))
                        e29::g_pState->m_bPendingStartGameReloadAfterOpen = true;
                    return;
                }
                if (AssetGuid.m_Type != xrsc::texture_type_guid_v) return;
                for (auto& S : e29::g_OpenTextureEditors)
                    if (S && S->m_Document.getGuid() == AssetGuid) { S->Focus(); return; }
                e29::g_OpenTextureEditors.push_back(std::make_unique<xtexture_editor::session>(AssetGuid, LibraryGuid, e29::g_pTextureEditorDevice));
            };
    // Manual Lock/Unlock from the Asset Tree's own right-click menu (direct user request, 2026-09-17:

        // "we should always give the user the manual option to do it... just in case the user is doing

        // something special"). RunQuery(), not Run() - SourceControlLock/Unlock are query_command_base

        // (see E29_CommandContext.h's own RunQuery comment for the real, previously-latent bug this

        // fixes: Run()'s plain Execute() call can never find a query-registered command). Source

        // Control* already no-op safely on a non-lockable/already-in-the-requested-state file, so no

        // pre-filtering here.

        AsserBrowser.m_OnLockAssetFile = [this](e10::library::guid LibraryGuid, const std::wstring& RelativePath)

        {

            e29::commands::RunQuery(E29Undo, std::format("SourceControlLock -Library {} -Path {}"

                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelativePath)));

        };

        AsserBrowser.m_OnUnlockAssetFile = [this](e10::library::guid LibraryGuid, const std::wstring& RelativePath)

        {

            e29::commands::RunQuery(E29Undo, std::format("SourceControlUnlock -Library {} -Path {}"

                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelativePath)));

        };



        // "SC Revert" from the Resources tab's per-tile "Resource Menu" and the Assets tab's own

        // RowContext menu (file or folder row) - direct user request. RelativePath may name either a

        // single file or a folder; RunRevertUnderFolder's own KeyIsUnderPrefix match already treats an

        // exact-match (a file naming itself) and a prefix-match (a folder) uniformly, so this callback

        // never needs to know which kind of path it was handed.

        AsserBrowser.m_OnRevertAssetPath = [this](e10::library::guid LibraryGuid, const std::wstring& RelativePath)

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return;

            e29::commands::RunRevertUnderFolder(E29Undo, LibraryGuid, RootPath, RelativePath);

        };

        // Registered here (one-time setup), NOT lazily on the first render frame as this used to be (an

        // "if (EditorToolbarHost.m_Items.empty())" check inside the per-frame code) - moved after finding

        // a real bug: ImGui loads io.IniFilename automatically during the FIRST ImGui::NewFrame() call,

        // which happens BEFORE the per-frame render code below ever runs once. With items only created

        // lazily on that first render, RegisterSettingsHandler's own ReadLineFn (below) fired against a

        // still-EMPTY m_Items during the actual ini load, found nothing named "Editor"/"Scene" to apply the

        // saved edge/position to, and silently discarded it - by the time m_Items.push_back finally ran a

        // moment later, the loaded data was already gone, so it looked LOADED (m_bInitialized flips true)

        // but the values were just the hardcoded push_back defaults the whole time. Items must exist

        // before the ini load happens, not after.

        EditorToolbarHost.m_Items.push_back

        ({ "Editor", ximgui::toolbar::toolbar_host_edge::Top, ximgui::toolbar::axis::Horizontal

         , ImVec2(EditorToolbarWidth, EditorToolbarHeight), ImVec2(32.0f, 250.0f), ImVec2(24.0f, 24.0f) });

        EditorToolbarHost.m_Items.push_back

        ({ "Scene", ximgui::toolbar::toolbar_host_edge::Top, ximgui::toolbar::axis::Horizontal

         , ImVec2(SceneToolbarWidth, EditorToolbarHeight), ImVec2(32.0f, 250.0f), ImVec2(24.0f, 72.0f) });



        // toolbar_host_state has no serialization of its own (a real gap - direct user report: "the

        // toolbars don't seem to save their position") - this persists each toolbar's dragged-to edge/

        // position/order into the SAME imgui_e29.ini this app already writes every docked window's

        // position into (io.IniFilename, set above). Must run before the first ImGui::NewFrame() of this

        // run (it does - this is one-time setup code executed before "entering frame loop" below) AND

        // after the items above are registered (see this block's own comment for why the order matters).

        ximgui::toolbar::RegisterSettingsHandler(EditorToolbarHost, "E29Toolbar");



        e29::diagnostics::Log("startup: initialization complete, entering frame loop");

            // Host service hooks (10.C.1.4): Idle Work + SC idle + Game.dll focus-reload.
        EditorHost.m_OnPumpServices = [&]() noexcept
        {
            if (pGameMgr)
                e29::PumpIdleWork(IdleWork, *pGameMgr, State);
            e29::source_control::PumpSourceControlIdleWork(IdleWork);
        };
        EditorHost.m_OnFocusRegain = [&]() noexcept
        {
#if defined(XECS_BUILD_SHARED)
            e29::StartGameReload(GamePlugin);
#endif
        };



        return 0;
    }

    inline void app::Run()
    {
        while (Instance.ProcessInputEvents()) Frame();
    }

    inline void app::Shutdown()
    {







        e29::diagnostics::Log("shutdown: frame loop ended");



        // Plugin systems live in GameMgr but their DestroyFunction code is in the DLL.

        // Tear down GameMgr (and host/texture bridges) BEFORE FreeLibrary, or ~mgr

        // jumps into unmapped memory on exit (callstack: ~mgr <- E29_Example).

        e29::g_OpenTextureEditors.clear();

        EditorHost.withdraw<e29::level_host_session>();
        EditorHost.release_current();

        e29::g_pGameMgr = nullptr;

        pGameMgr.reset();

        e29::UnloadGamePlugin(GamePlugin);



        e29::diagnostics::Log("shutdown: game plugin unloaded");







        e29::diagnostics::Log("shutdown: xgpu/imgui Shutdown begin");



        xgpu::tools::imgui::Shutdown();



        e29::diagnostics::Log("shutdown: xgpu/imgui Shutdown complete");



        e29::diagnostics::RemoveCrtReportHook();



        e29::diagnostics::RemoveTerminateHandler();



        e29::diagnostics::Log("shutdown: E29_Example return");



        e29::diagnostics::Stop();



    }
}
