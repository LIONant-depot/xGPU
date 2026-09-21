#include "source/Examples/E29_LevelSceneEditor/E29_LevelSceneEditorKit.h"



// Moved ahead of E29_GamePlugin.h (was line 22, below) - E29_GamePluginBuild.h's own



// BuildGamePluginIfStale now needs e29::g_ScriptConfig (GetLatestModuleSourceWriteTime,



// E29_GameModuleSources.h) for its own staleness check, so g_ScriptConfig must already be declared



// by the time the umbrella below compiles. E10_AssetMgr.h (e10::g_LibMgr) is already visible via



// E29_LevelSceneEditorKit.h just above, so this is the only reordering actually needed.



#include "source/Examples/E29_LevelSceneEditor/kit/E29_ProjectScriptConfig.h"



#include "dependencies/xECSV2/src/xecs_plugin_api.h"



#include "source/Examples/E29_LevelSceneEditor/E29_GamePlugin.h"
#include "source/Examples/E29_LevelSceneEditor/kit/E29_Panel_PlayTransport.h"



#include "dependencies/xundo/source/xundo_history.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Selection.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandConsolePipe.h"



#include "dependencies/xeditor/include/xeditor/host.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Chat.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Level.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_SceneDependency.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_LibraryDependency.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Workspace.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_PlaySession.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_SceneOrganization.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetBrowser.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetFiles.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Scripting.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_MakePrefab.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Compilation.h"



#include "source/Examples/E29_LevelSceneEditor/extensions/source_control/E29_Commands_SourceControl.h"



#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_TextureEditor.h"



#include "source/Examples/E29_LevelSceneEditor/E29_LevelDocument.h"



#include "source/Examples/E29_LevelSceneEditor/kit/E29_IdleWork.h"



#include "source/Examples/E29_LevelSceneEditor/kit/E29_ComponentCompatibility.h"



#include "source/Examples/E29_LevelSceneEditor/E29_Theme.h"



#include "source/Examples/E29_LevelSceneEditor/E29_EditorTabs.h"



#include "source/Tools/Editor/xeditor_resource_tab.h"



#include "source/Examples/E29_LevelSceneEditor/E29_Diagnostics.h"



#include "ximgui_toolbar.h"







//-----------------------------------------------------------------------------------



//



// E29 - Level + Scene editor.



//



// First real consumer of xECSV2's scene system (dependency-ordered scene load/unload, permanent-ID



// entity serialization - see dependencies/xECSV2/doc/xecs_scene*.md) as actual, resource-pipeline-



// integrated Level/Scene resource types: browse/create Levels (a list of member Scenes) and Scenes



// (dependency edges to parent scenes + the entities that live in them), create/delete entities,



// edit their components through the same xproperty inspector every other editor uses.



//



// Everything reusable (Level tree UI, Entity Properties panel, prefab authoring/instancing,



// folder/entity bookkeeping, the modal error popup, the entity-reference/prefab-override inspector



// wiring) now lives in E29_LevelSceneEditorKit.h - this file is just the demo content (its own



// `transform` starter component) and the example's own setup/main loop wiring it together.



//



//-----------------------------------------------------------------------------------







namespace e29



{



    struct transform



    {



        constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "Transform" };







        // Position/Rotation/Scale via xmath::fvec3 - same shape Unity/Unreal/Godot's own Transform



        // uses, and the same convention this codebase's own descriptors already follow (see



        // xskeleton_desc::transform in xskeleton_descriptor.h: Scale/Rotation/Translation as



        // xmath::fvec3, Rotation stored in radians as Euler angles).



        xmath::fvec3 m_Position = xmath::fvec3::fromZero();



        xmath::fvec3 m_Rotation = xmath::fvec3::fromZero(); // radians



        xmath::fvec3 m_Scale    = xmath::fvec3::fromOne();







        XPROPERTY_DEF



        ( "Transform", transform



        , obj_member<"Position", &transform::m_Position>



        , obj_member<"Rotation", &transform::m_Rotation>



        , obj_member<"Scale",    &transform::m_Scale>



        )



    };



    XPROPERTY_REG(transform)







    // Two trivial demo Update systems - E29 otherwise registers ZERO systems (RegisterSystems<>() is



    // called empty, purely to lock component bit IDs), so the new System Registry panel/Play-Stop



    // toggle would have nothing real to list/reorder/enable/observe without these. Each just



    // printf's once per tick (flushed unconditionally, per this project's persistent-diagnostic-



    // logging preference) since E29 has no viewport to observe a "real" effect through - reordering



    // them in the System Registry panel changes which line prints first; disabling one stops its own



    // line, which is the whole verification surface for that feature.



    //



    // typedef_v deliberately leaves m_Guid at its default (xecs::system::type::details::CreateInfo's



    // own type::guid{__FUNCSIG__} fallback) rather than hand-typing an explicit guid string per



    // system - that automatic, zero-boilerplate identity is worth keeping for the common case. It IS



    // technically less durable than an explicit guid (SystemOrder.config.txt persists it, and a



    // rename or compiler-formatting change could shift it) - but the actual blast radius is small:



    // Load() already treats an unmatched saved guid as "not registered any more" and just skips it



    // (see its own comment), so the worst case is a silently-reset reorder/enable preference, never a



    // crash or corrupted state. Worth switching to an explicit guid on a case-by-case basis for



    // anything where that reset would actually matter (a real gameplay system whose saved order



    // matters for correctness, not a demo).



    struct tick_logger_a : xecs::system::instance



    {



        constexpr static auto typedef_v = xecs::system::type::update{ .m_pName = "Tick Logger A" };







        tick_logger_a(xecs::game_mgr::instance& GameMgr) noexcept : xecs::system::instance(GameMgr) {}







        void OnUpdate(void) noexcept



        {



            std::printf("[System] Tick Logger A\n");



            std::fflush(stdout);



        }



    };







    struct tick_logger_b : xecs::system::instance



    {



        constexpr static auto typedef_v = xecs::system::type::update{ .m_pName = "Tick Logger B" };







        tick_logger_b(xecs::game_mgr::instance& GameMgr) noexcept : xecs::system::instance(GameMgr) {}







        void OnUpdate(void) noexcept



        {



            std::printf("[System] Tick Logger B\n");



            std::fflush(stdout);



        }



    };



}







//-----------------------------------------------------------------------------------







int E29_Example()



{



    e29::diagnostics::Start();



    e29::diagnostics::InstallCrtReportHook();



    e29::diagnostics::InstallTerminateHandler();



    e29::diagnostics::Log("startup: E29_Example begin");







    xgpu::instance Instance;



    e29::diagnostics::Log("startup: creating xgpu instance");



    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_pLogErrorFunc = e29::Debugger, .m_pLogWarning = e29::Debugger }); Err)



    {



        e29::diagnostics::Log("startup: xgpu instance creation failed");



        e29::diagnostics::RemoveCrtReportHook();



        e29::diagnostics::RemoveTerminateHandler();



        e29::diagnostics::Stop();



        return xgpu::getErrorInt(Err);



    }







    xgpu::device Device;



    e29::diagnostics::Log("startup: creating xgpu device");



    if (auto Err = Instance.Create(Device); Err)



    {



        e29::diagnostics::Log("startup: xgpu device creation failed");



        e29::diagnostics::RemoveCrtReportHook();



        e29::diagnostics::RemoveTerminateHandler();



        e29::diagnostics::Stop();



        return xgpu::getErrorInt(Err);



    }







    xgpu::window MainWindow;



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







    // Same wiring E10 does: texture (and other) loaders Destroy via UserData.m_Device.



    // Without this, RegisterResource/ReleaseRef (Texture editor preview reload after Compile)



    // crashes in device::Destroy on a default-constructed empty device handle.



    resource_mgr_user_data ResourceMgrUserData{};



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



    auto pGameMgr = std::make_unique<xecs::game_mgr::instance>();



    e29::diagnostics::Log("startup: constructing ECS game manager complete");



    e29::game_plugin_state GamePlugin;







    // Registers e29's own demo content - kept as a local lambda (not inlined at each of the two call



    // sites below) so PollGameReload can re-run the exact same host-registration sequence after a



    // reload, matching what startup does here.



    auto RegisterHostComponents = []( xecs::game_mgr::instance& GameMgr ) noexcept



    {



        GameMgr.RegisterComponents<e29::name, e29::transform, xecs::editor::prefab_instance, xecs::component::entity_reference>();



    };



    auto RegisterHostSystems = []( xecs::game_mgr::instance& GameMgr ) noexcept



    {



        GameMgr.RegisterSystems<e29::tick_logger_a, e29::tick_logger_b>();



    };







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







    //



    // Project path (same lookup every editor example uses) - kept around (not just a local) so



    // PollGameReload can re-apply it to a freshly reconstructed pGameMgr.



    //



    // Historically this located the repo root by searching the executable's own path for the



    // first literal "xGPU" substring and assumed everything up to (and including) it was the repo



    // root. That breaks the moment the checkout itself sits under a directory that ALSO contains



    // "xGPU" earlier in the path - e.g. a git worktree at .../copilot-worktrees/xGPU/<branch>/... -



    // the substring match fires on the outer container folder, which has no example.lionprj of its



    // own, and OpenProject/plugin enumeration then aborts. Fixed by walking UP the executable's own



    // ancestor directories and picking the first (closest) one that actually has a bootstrapped



    // example.lionprj\Cache\Plugins - i.e. finding the repo root structurally, never by name.



    std::wstring ProjectPath;



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







    //



    // Asset browser + editor state



    //



    e10::assert_browser  AsserBrowser;



    e29::editor_state    State;



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







    //



    // Command/undo system - phase 1 of documentation/E29_LevelSceneEditor/command_undo_system_plan.md: selection only,



    // the simplest slice, wired end to end (real click sites routed through Execute(), Ctrl+Z/Y) to



    // prove the whole shape before tackling property editing/component add-remove/entity



    // create-delete on top of it. Direct port of E27_NodeOS's own xundo wiring



    // (E27_NodeOS_Editor.cpp) - one xundo::system per "document" (just E29's own editor state here),



    // one xundo::history addressing it under the "E29" namespace for the CLI/Command-Console work a



    // later phase adds. bAutoLoadSave=false, same reasoning as E27's own comment: a fresh undo stack



    // each run, a stale on-disk history from a previous session's differently-shaped scene would be



    // more confusing than useful.



    //



    e29::commands::e29_command_context CmdContext{ State };



    xundo::system                      E29Undo;



    xeditor::host                     EditorHost;



    EditorHost.m_pExternalWorkspace = &E29Undo;



    if (auto Err = E29Undo.Init({}, false); !Err.empty())



        e29::Debugger(std::format("E29: xundo Init failed: {}", Err));



    e29::RegisterLevelEditorDescriptor();



    e29::level_host_session LevelHostSession;



    EditorHost.provide(LevelHostSession);
    xundo::system& LevelUndo = LevelHostSession.EnsureCreated(State, pGameMgr.get()).m_Undo;



    e29::commands::open_texture_editor_cmd CmdOpenTextureEditor(E29Undo, &CmdContext);



    e29::commands::texture_editor_command_cmd CmdTextureEditorCommand(E29Undo, &CmdContext);



    e29::commands::select_cmd             CmdSelect(LevelUndo, &CmdContext);



    e29::commands::toggle_multi_select_cmd CmdToggleMultiSelect(LevelUndo, &CmdContext);



    e29::commands::clear_selection_cmd    CmdClearSelection(LevelUndo, &CmdContext);



    e29::commands::set_property_cmd       CmdSetProperty(LevelUndo, &CmdContext);



    e29::commands::revert_override_cmd    CmdRevertOverride(LevelUndo, &CmdContext);



    e29::commands::apply_overrides_cmd    CmdApplyOverrides(LevelUndo, &CmdContext);



    e29::commands::revert_hierarchy_overrides_cmd CmdRevertHierarchyOverrides(LevelUndo, &CmdContext);



    e29::commands::revert_all_overrides_cmd       CmdRevertAllOverrides(LevelUndo, &CmdContext);



    e29::commands::add_component_cmd      CmdAddComponent(LevelUndo, &CmdContext);



    e29::commands::remove_component_cmd   CmdRemoveComponent(LevelUndo, &CmdContext);



    e29::commands::create_entity_cmd      CmdCreateEntity(LevelUndo, &CmdContext);



    e29::commands::delete_entity_cmd      CmdDeleteEntity(LevelUndo, &CmdContext);



    e29::commands::say_query_cmd          CmdSay(E29Undo, &CmdContext);



    e29::commands::get_log_query_cmd      CmdGetLog(E29Undo, &CmdContext);



    e29::commands::open_level_cmd         CmdOpenLevel(E29Undo, &CmdContext);



    e29::commands::close_scene_cmd        CmdCloseScene(LevelUndo, &CmdContext);



    e29::commands::add_scene_cmd          CmdAddScene(LevelUndo, &CmdContext);



    e29::commands::remove_scene_cmd       CmdRemoveScene(LevelUndo, &CmdContext);



    e29::commands::add_scene_dependency_cmd    CmdAddSceneDependency(LevelUndo, &CmdContext);



    e29::commands::remove_scene_dependency_cmd CmdRemoveSceneDependency(LevelUndo, &CmdContext);



    e29::commands::add_library_dependency_cmd    CmdAddLibraryDependency(LevelUndo, &CmdContext);



    e29::commands::remove_library_dependency_cmd CmdRemoveLibraryDependency(LevelUndo, &CmdContext);



    e29::commands::create_library_query_cmd      CmdCreateLibrary(E29Undo, &CmdContext);



    e29::commands::list_legal_reference_libraries_query_cmd CmdListLegalReferenceLibraries(E29Undo, &CmdContext);



    e29::commands::list_levels_query_cmd  CmdListLevels(E29Undo, &CmdContext);



    e29::commands::list_scenes_query_cmd  CmdListScenes(LevelUndo, &CmdContext);



    e29::commands::list_entities_query_cmd CmdListEntities(LevelUndo, &CmdContext);



    e29::commands::list_folders_query_cmd CmdListFolders(LevelUndo, &CmdContext);



    e29::commands::audit_component_usage_query_cmd CmdAuditComponentUsage(LevelUndo, &CmdContext);



    e29::commands::undo_query_cmd         CmdUndo(E29Undo, &CmdContext);



    e29::commands::redo_query_cmd         CmdRedo(E29Undo, &CmdContext);



    e29::commands::undo_query_cmd         CmdLevelUndo(LevelUndo, &CmdContext);



    e29::commands::redo_query_cmd         CmdLevelRedo(LevelUndo, &CmdContext);



    e29::commands::save_query_cmd         CmdSave(E29Undo, &CmdContext);



    e29::commands::close_query_cmd        CmdClose(E29Undo, &CmdContext);



    e29::commands::serialize_roundtrip_query_cmd CmdSerializeRoundtrip(LevelUndo, &CmdContext);



    e29::commands::describe_entity_query_cmd CmdDescribeEntity(LevelUndo, &CmdContext);



    e29::commands::list_component_types_query_cmd CmdListComponentTypes(LevelUndo, &CmdContext);



    e29::commands::set_entity_reference_cmd CmdSetEntityReference(LevelUndo, &CmdContext);



    e29::commands::play_query_cmd         CmdPlay(E29Undo, &CmdContext);



    e29::commands::pause_query_cmd        CmdPause(E29Undo, &CmdContext);
    e29::commands::step_query_cmd         CmdStep(E29Undo, &CmdContext);



    e29::commands::stop_query_cmd         CmdStop(E29Undo, &CmdContext);



    e29::commands::get_play_state_query_cmd CmdGetPlayState(E29Undo, &CmdContext);



    e29::commands::instantiate_prefab_cmd CmdInstantiatePrefab(LevelUndo, &CmdContext);



    e29::commands::move_to_folder_cmd     CmdMoveToFolder(LevelUndo, &CmdContext);



    e29::commands::create_folder_cmd      CmdCreateFolder(LevelUndo, &CmdContext);



    e29::commands::delete_folder_cmd      CmdDeleteFolder(LevelUndo, &CmdContext);



    e29::commands::list_assets_query_cmd  CmdListAssets(E29Undo, &CmdContext);



    e29::commands::describe_asset_query_cmd CmdDescribeAsset(E29Undo, &CmdContext);



    e29::commands::rename_asset_cmd       CmdRenameAsset(E29Undo, &CmdContext);



    e29::commands::move_asset_cmd         CmdMoveAsset(E29Undo, &CmdContext);



    e29::commands::delete_asset_cmd       CmdDeleteAsset(E29Undo, &CmdContext);



    e29::commands::restore_asset_cmd      CmdRestoreAsset(E29Undo, &CmdContext);



    e29::commands::create_asset_cmd       CmdCreateAsset(E29Undo, &CmdContext);



    e29::commands::save_assets_query_cmd  CmdSaveAssets(E29Undo, &CmdContext);



    e29::commands::add_script_source_file_cmd        CmdAddScriptSourceFile(E29Undo, &CmdContext);



    e29::commands::remove_script_source_file_cmd      CmdRemoveScriptSourceFile(E29Undo, &CmdContext);



    e29::commands::list_script_source_files_query_cmd CmdListScriptSourceFiles(E29Undo, &CmdContext);



    e29::commands::add_project_module_reference_cmd          CmdAddProjectModuleReference(E29Undo, &CmdContext);



    e29::commands::remove_project_module_reference_cmd       CmdRemoveProjectModuleReference(E29Undo, &CmdContext);



    e29::commands::list_project_module_references_query_cmd  CmdListProjectModuleReferences(E29Undo, &CmdContext);



    e29::commands::set_script_source_file_content_cmd         CmdSetScriptSourceFileContent(E29Undo, &CmdContext);



    e29::commands::rename_script_source_file_cmd               CmdRenameScriptSourceFile(E29Undo, &CmdContext);



    e29::commands::regenerate_project_module_sources_query_cmd CmdRegenerateProjectModuleSources(E29Undo, &CmdContext);



    e29::commands::rename_asset_file_cmd  CmdRenameAssetFile(E29Undo, &CmdContext);



    e29::commands::move_asset_file_cmd    CmdMoveAssetFile(E29Undo, &CmdContext);



    e29::commands::delete_asset_file_cmd  CmdDeleteAssetFile(E29Undo, &CmdContext);



    e29::commands::restore_asset_file_cmd CmdRestoreAssetFile(E29Undo, &CmdContext);



    e29::commands::copy_asset_file_cmd    CmdCopyAssetFile(E29Undo, &CmdContext);



    e29::commands::make_prefab_cmd        CmdMakePrefab(LevelUndo, &CmdContext);



    e29::commands::make_prefab_variant_cmd CmdMakePrefabVariant(LevelUndo, &CmdContext);



    e29::commands::recompile_all_query_cmd    CmdRecompileAll(E29Undo, &CmdContext);



    e29::commands::recompile_errors_query_cmd CmdRecompileErrors(E29Undo, &CmdContext);



    e29::commands::compile_start_query_cmd    CmdCompileStart(E29Undo, &CmdContext);



    e29::commands::compile_pause_query_cmd    CmdCompilePause(E29Undo, &CmdContext);



    e29::commands::compile_auto_query_cmd     CmdCompileAuto(E29Undo, &CmdContext);



    e29::commands::compile_status_query_cmd   CmdCompileStatus(E29Undo, &CmdContext);



    e29::commands::run_sanity_check_query_cmd CmdRunSanityCheck(E29Undo, &CmdContext);



    e29::commands::source_control_status_query_cmd  CmdSourceControlStatus(E29Undo, &CmdContext);



    e29::commands::source_control_depot_status_query_cmd CmdSourceControlDepotStatus(E29Undo, &CmdContext);



    e29::commands::source_control_refresh_query_cmd CmdSourceControlRefresh(E29Undo, &CmdContext);



    e29::commands::source_control_list_locks_query_cmd CmdSourceControlListLocks(E29Undo, &CmdContext);



    e29::commands::source_control_lock_query_cmd    CmdSourceControlLock(E29Undo, &CmdContext);



    e29::commands::source_control_unlock_query_cmd  CmdSourceControlUnlock(E29Undo, &CmdContext);



    e29::commands::source_control_revert_query_cmd  CmdSourceControlRevert(E29Undo, &CmdContext);



    e29::commands::source_control_stage_query_cmd   CmdSourceControlStage(E29Undo, &CmdContext);



    e29::commands::source_control_commit_query_cmd  CmdSourceControlCommit(E29Undo, &CmdContext);



    e29::commands::source_control_pull_query_cmd    CmdSourceControlPull(E29Undo, &CmdContext);



    e29::commands::source_control_push_query_cmd    CmdSourceControlPush(E29Undo, &CmdContext);



    e29::idle_work_state                  IdleWork;



    xundo::history                        E29History;



    E29History.AddSystem("E29", 1, E29Undo);







    // Command Console named pipe - phase 5 of documentation/E29_LevelSceneEditor/command_undo_system_plan.md. Lets an



    // external process (xeditorcli, a script, an AI) drive E29 through E29History.Route() with no UI



    // automation - see commands/E29_CommandConsolePipe.h's own top comment for the full threading



    // reasoning. Detached, not joined - a local dev/debug feature, dies with the process, same as



    // E27_NodeOS's own identical pipe thread.



    std::vector<e29::console_log_entry> ConsoleLog;



    e29::command_console_pipe_bridge    ConsolePipeBridge;



    std::thread(e29::CommandConsolePipeThreadMain, std::ref(ConsolePipeBridge)).detach();







    // Lets e29::commands::Run() (E29_CommandContext.h, called by every UI-driven command - tree



    // clicks, property edits, add/remove component, create/delete entity) log into this SAME console



    // log too, not just pipe-driven/console-typed commands - direct user report: "route the users



    // commands there as well... nothing showing up there yet."



    e29::commands::g_pConsoleLog = &ConsoleLog;







    //



    // Entity component inspector - the currently-selected entity's components. The resource-picker



    // callbacks are stateless (WireResourcePickerCallbacks); the prefab-override/entity-reference



    // ones need live GameMgr/State access, so they're bundled into entity_inspector_bridge (kit).



    //



    xproperty::inspector          EntityInspector("Inspector");



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



    e29::entity_inspector_bridge  InspectorBridge;



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



    AsserBrowser.m_OnLockAssetFile = [&E29Undo](e10::library::guid LibraryGuid, const std::wstring& RelativePath)



    {



        e29::commands::RunQuery(E29Undo, std::format("SourceControlLock -Library {} -Path {}"



            , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelativePath)));



    };



    AsserBrowser.m_OnUnlockAssetFile = [&E29Undo](e10::library::guid LibraryGuid, const std::wstring& RelativePath)



    {



        e29::commands::RunQuery(E29Undo, std::format("SourceControlUnlock -Library {} -Path {}"



            , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelativePath)));



    };







    // "SC Revert" from the Resources tab's per-tile "Resource Menu" and the Assets tab's own



    // RowContext menu (file or folder row) - direct user request. RelativePath may name either a



    // single file or a folder; RunRevertUnderFolder's own KeyIsUnderPrefix match already treats an



    // exact-match (a file naming itself) and a prefix-match (a folder) uniformly, so this callback



    // never needs to know which kind of path it was handed.



    AsserBrowser.m_OnRevertAssetPath = [&E29Undo](e10::library::guid LibraryGuid, const std::wstring& RelativePath)



    {



        const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);



        if (RootPath.empty()) return;



        e29::commands::RunRevertUnderFolder(E29Undo, LibraryGuid, RootPath, RelativePath);



    };







    //



    // Main Loop



    //



    static ximgui::toolbar::toolbar_host_state EditorToolbarHost;



    static constexpr float EditorToolbarWidth = 570.0f;



    static constexpr float SceneToolbarWidth = 390.0f;



    static constexpr float EditorToolbarHeight = 20.0f;



    static constexpr float EditorToolbarFontScale = 1.0f;



    static constexpr float EditorToolbarItemSpacing = 2.0f;



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



    static int SceneTool = 0; // Q=select, W=move, E=rotate, R=scale, F=frame



    static bool bPivotCenter = true;



    static bool bLocalSpace = false;



    static bool bGridVisible = true;







    e29::diagnostics::Log("startup: initialization complete, entering frame loop");



    std::uint64_t FrameNumber = 0;



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



    while (Instance.ProcessInputEvents())



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



        // the startup Debugger()-timing bug this session already found and fixed): the actual



        // destroy-and-recreate-the-whole-world sequence is heavy enough, and doing it nested inside



        // an active ImGui::BeginMainMenuBar()/EndMainMenuBar() scope, that running it synchronously



        // corrupted ImGui's window-stack bookkeeping the same way. Running it here instead gives it



        // a clean "no active ImGui frame" execution context, same as every other safe Debugger()/



        // heavy-state-mutation call site in this file.



        // PollGameReload itself is a no-op (returns immediately) unless a build kicked off by



        // StartGameReload (focus-regain or Play, above) is both in-flight and finished - see its own



        // comment.



        e29::PollGameReload



        ( pGameMgr, State, GamePlugin, EntityInspector, InspectorBridge, ProjectPath



        , RegisterHostComponents, RegisterHostSystems



        );







        // A no-op unless a pipe client (xeditorcli) has a request waiting - see



        // commands/E29_CommandConsolePipe.h's own comment for why this must run here (same clean



        // frame boundary as PollGameReload above) rather than after BeginRendering the way E27's own



        // equivalent pump does. Comparing ConsoleLog's size before/after (rather than threading a new



        // parameter into PumpCommandConsolePipe itself) is how Idle Work (kit/E29_IdleWork.h) learns a



        // CLI/AI command actually ran this frame - it only ever appends, never shrinks, so a size



        // change means real activity happened.



        const auto ConsoleLogCountBefore = ConsoleLog.size();



        e29::PumpCommandConsolePipe(ConsolePipeBridge, E29History, ConsoleLog);



        if (ConsoleLog.size() != ConsoleLogCountBefore)



            e29::NotifyActivity(IdleWork);







        // Deferred "Stop" click (see the button's own comment) - runs here, same clean frame



        // boundary as PollGameReload above, never nested inside an active ImGui menu-bar scope.



        // State.m_PendingKeepTweaksCommands has already been decided by the time this flag is set



        // (RequestStop/E29_PlaySession.h resolves it immediately - either right away, via -Keep or the



        // confirmation modal's own button, or finds nothing to ask about) - never re-collected here.



        if (State.m_bStopRequested)



        {



            State.m_bStopRequested = false;



            e29::StopPlaySession



            ( pGameMgr, State, GamePlugin, EntityInspector, InspectorBridge, ProjectPath



            , RegisterHostComponents, RegisterHostSystems



            , State.m_PendingKeepTweaksCommands



            );



            State.m_PendingKeepTweaksCommands.clear();



        }







        // The main dockspace hosts complete editor contexts. The Level Editor context in turn owns



        // its private nested dockspace for tools such as Level, Inspector, and Commands.



        if (xgpu::tools::imgui::BeginRendering(true))



        {



            e29::diagnostics::Log("frame %llu BeginRendering skipped", static_cast<unsigned long long>(FrameNumber));



            continue;



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







        auto RenderParentEditorToolbar = [&]()



        {



            if (!ImGui::BeginMenuBar())



                return;







            if (ImGui::BeginMenu("File"))



            {



                if (ImGui::MenuItem("Asset Browser..."))



                    AsserBrowser.Show(true);







                ImGui::Separator();



                const bool bCanSave = !State.isPlaying()



                    && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty())



                    && e29::HasUnsavedDocumentChanges(State, LevelUndo);



                ImGui::BeginDisabled(!bCanSave);



                if (ImGui::MenuItem("Save", "Ctrl+S"))



                {



                    e29::SaveEverything(*pGameMgr, State);



                    e29::MarkDocumentClean(State, LevelUndo);



                }



                ImGui::EndDisabled();







                const bool bCanClose = !State.isPlaying()



                    && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty());



                ImGui::BeginDisabled(!bCanClose);



                if (ImGui::MenuItem("Close"))



                    e29::RequestCloseLevel(*pGameMgr, State, LevelUndo);



                ImGui::EndDisabled();



                ImGui::EndMenu();



            }







            if (GamePlugin.m_bBuilding)



            {



                ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);



                ImGui::TextDisabled("Game.dll: building...");



            }







            e29::RenderPlayTransport(State, GamePlugin, { ImVec2(30.0f, 0.0f), true, true });



            ImGui::EndMenuBar();



        };







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
                RenderParentEditorToolbar, LevelTabName.c_str(), &Device, xecs::level::type_guid_v, LevelDockGuid, &bLevelTabOpen);
            if (!bLevelTabOpen)
            {
                e29::RequestCloseLevel(*pGameMgr, State, LevelUndo);
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







        e29::RenderErrorPopup();



        e29::RenderKeepTweaksModal(State);



        e29::RenderRemoveDependencyConfirmModal(E29Undo);



        e29::RenderSaveBeforeCloseModal(*pGameMgr, State, LevelUndo);



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



            && e29::HasUnsavedDocumentChanges(State, LevelUndo))



        {



            e29::SaveEverything(*pGameMgr, State);



            e29::MarkDocumentClean(State, LevelUndo);



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



            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().KeyShift) LevelUndo.Undo();



            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyShift)) LevelUndo.Redo();



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





        // Host Drawer (xeditor::host): one call — Space + all OS-window manifestations. Editors do not wire this.

        EditorHost.m_OnDrawerTab = [&](int TabIndex, const char* /*TabName*/)

            {

                switch (TabIndex)

                {

                case 0:

                    AsserBrowser.SetDevice(Device);

                    AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Resources");

                    break;

                case 1:

                    AsserBrowser.SetDevice(Device);

                    AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Assets");

                    break;

                case 2:

                    e29::RenderSourceControlPanel(E29Undo, /*bEmbedded*/ true);

                    break;

                case 3:

                    e29::RenderIdleWorkPanel(IdleWork, pGameMgr.get(), State, /*bEmbedded*/ true);

                    break;

                case 4:

                    e29::RenderGamePluginLogPanel(/*bEmbedded*/ true);

                    break;

                case 5:

                    e29::DrawCommandConsolePanel(E29History, ConsoleLog, /*bEmbedded*/ true);

                    break;

                case 6:

                    AsserBrowser.SetDevice(Device);

                    AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Compilation");

                    break;

                case 7:

                    AsserBrowser.SetDevice(Device);

                    AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Project Settings");

                    break;

                default:

                    break;

                }

            };








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



                if (e29::RequestOpenLevel(*pGameMgr, State, LevelUndo, NewAsset, /*bStartGameReload*/ true))



                    e29::StartGameReload(GamePlugin);



#else



                e29::RequestOpenLevel(*pGameMgr, State, LevelUndo, NewAsset, /*bStartGameReload*/ false);



#endif



            }



            else if (NewAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, NewAsset);



        }



        else if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false)



        {



            if (SelAsset.m_Type == xecs::level::type_guid_v)



            {



#if defined(XECS_BUILD_SHARED)



                if (e29::RequestOpenLevel(*pGameMgr, State, LevelUndo, SelAsset, /*bStartGameReload*/ true))



                    e29::StartGameReload(GamePlugin);



#else



                e29::RequestOpenLevel(*pGameMgr, State, LevelUndo, SelAsset, /*bStartGameReload*/ false);



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



        e29::RenderLevelTreePanel(*pGameMgr, State, E29Undo, !bLevelWritable);



        e29::diagnostics::Log("frame %llu level tree render end", static_cast<unsigned long long>(FrameNumber));



        // Level drop from Resources onto Level Tree (deferred during panel draw) - goes through



        // RequestOpenLevel so a dirty open Level prompts Save/Don't Save/Cancel first.



        if (e29::FlushPendingOpenLevelFromTree(*pGameMgr, State, LevelUndo))



            e29::StartGameReload(GamePlugin);



        e29::diagnostics::Log("frame %llu entity properties render begin", static_cast<unsigned long long>(FrameNumber));



        e29::editor_tabs::SetNextLevelEditorToolClass();



        e29::RenderEntityPropertiesPanel(*pGameMgr, State, EntityInspector, InspectorBridge, E29Undo, !bLevelWritable);



        e29::diagnostics::Log("frame %llu entity properties render end", static_cast<unsigned long long>(FrameNumber));



        e29::diagnostics::Log("frame %llu system registry render begin", static_cast<unsigned long long>(FrameNumber));



        e29::editor_tabs::SetNextLevelEditorToolClass();



        e29::RenderSystemRegistryPanel(*pGameMgr, State);



        e29::diagnostics::Log("frame %llu system registry render end", static_cast<unsigned long long>(FrameNumber));







        auto RenderEditorToolbar = [&](const char* Name, ximgui::toolbar::axis Axis)



        {



            const bool bHorizontal = Axis == ximgui::toolbar::axis::Horizontal;



            const float ButtonHeight = bHorizontal ? EditorToolbarHeight - 4.0f : 28.0f;



            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorToolbarItemSpacing, ImGui::GetStyle().ItemSpacing.y));



            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * EditorToolbarFontScale);



            bool bFirstButton = true;



            auto ToolbarButton = [&](const char* LongLabel, const char* ShortLabel, bool bActive, bool bDisabled, auto&& OnClick)



            {



                if (bHorizontal && !bFirstButton)



                    ImGui::SameLine();



                bFirstButton = false;



                if (bActive)



                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);



                ImGui::BeginDisabled(bDisabled);



                if (ImGui::Button(bHorizontal ? LongLabel : ShortLabel, bHorizontal



                    ? ImVec2(52.0f, ButtonHeight) : ImVec2(32.0f, ButtonHeight)))



                    OnClick();



                ImGui::EndDisabled();



                if (bActive)



                    ImGui::PopStyleColor();



                if (ImGui::IsItemHovered())



                {



                    ImGui::BeginTooltip();



                    ImGui::TextUnformatted(LongLabel);



                    ImGui::EndTooltip();



                }



            };



            auto ToolbarSeparator = [&]()



            {



                if (bHorizontal)



                {



                    ImGui::SameLine();



                    ImGui::TextDisabled("|");



                }



                else



                {



                    ImGui::Separator();



                }



            };







            if (std::strcmp(Name, "Editor") == 0)



            {



                const bool bCanSave = !State.isPlaying()



                    && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty())



                    && e29::HasUnsavedDocumentChanges(State, LevelUndo);



                ToolbarButton("Save", "S", false, !bCanSave, [&]()



                {



                    e29::SaveEverything(*pGameMgr, State);



                    e29::MarkDocumentClean(State, LevelUndo);



                });



                ToolbarButton("Undo", "U", false, State.isPlaying(), [&]() { LevelUndo.Undo(); });



                ToolbarButton("Redo", "R", false, State.isPlaying(), [&]() { LevelUndo.Redo(); });



                ToolbarButton("Assets", "A", false, false, [&]() { EditorHost.open_drawer_tab(ImGui::GetMainViewport(), 1); });



                ToolbarSeparator();







                e29::RenderPlayTransport(State, GamePlugin, { ImVec2(52.0f, ButtonHeight), bHorizontal, false });
                bFirstButton = false;



                ToolbarSeparator();



                ToolbarButton("Hierarchy", "H", false, false, [&]() { ImGui::SetWindowFocus(e29::editor_tabs::kLevelTreeWindow); });



                ToolbarButton("Inspector", "I", false, false, [&]() { ImGui::SetWindowFocus(e29::editor_tabs::kInspectorWindow); });



                ToolbarButton("Systems", "Y", false, false, [&]() { ImGui::SetWindowFocus(e29::editor_tabs::kSystemRegistryWindow); });



            }



            else



            {



                auto SceneButton = [&](const char* Label, int ToolIndex, const char* Tooltip)



                {



                    if (bHorizontal && !bFirstButton)



                        ImGui::SameLine();



                    bFirstButton = false;



                    if (SceneTool == ToolIndex)



                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);



                    if (ImGui::Button(Label, ImVec2(32.0f, ButtonHeight)))



                        SceneTool = ToolIndex;



                    if (SceneTool == ToolIndex)



                        ImGui::PopStyleColor();



                    if (ImGui::IsItemHovered())



                    {



                        ImGui::BeginTooltip();



                        ImGui::TextUnformatted(Tooltip);



                        ImGui::EndTooltip();



                    }



                };



                SceneButton("Q", 0, "Select tool");



                SceneButton("W", 1, "Move tool");



                SceneButton("E", 2, "Rotate tool");



                SceneButton("R", 3, "Scale tool");



                SceneButton("F", 4, "Frame selected");



                ToolbarSeparator();







                auto SceneToggle = [&](const char* LongLabel, const char* ShortLabel, bool& bValue)



                {



                    if (bHorizontal)



                        ImGui::SameLine();



                    if (ImGui::Button(bHorizontal ? LongLabel : ShortLabel



                        , bHorizontal ? ImVec2(58.0f, ButtonHeight) : ImVec2(32.0f, ButtonHeight)))



                        bValue = !bValue;



                    if (ImGui::IsItemHovered())



                    {



                        ImGui::BeginTooltip();



                        ImGui::TextUnformatted(LongLabel);



                        ImGui::EndTooltip();



                    }



                };



                SceneToggle("Pivot", "P", bPivotCenter);



                SceneToggle("Local", "L", bLocalSpace);



                SceneToggle("Grid", "G", bGridVisible);



            }



            ImGui::PopFont();



            ImGui::PopStyleVar();



        };







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



            , RenderEditorToolbar



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

        e29::RenderReloadCompatibilityModal();







        }







        // Editor Framework: this window is a root-level PEER of "Level Editor" (like Level Editor



        // itself, not one of its internal child panels), so it must render every frame



        // unconditionally - gating it behind bParentEditorVisible (as every internal panel above



        // correctly is) meant it silently stopped calling ImGui::Begin() the instant the user



        // clicked its own tab (since that made Level Editor itself the hidden one), which is



        // exactly why clicking the tab looked like it did nothing.



        LevelHostSession.Sync(EditorHost, State, pGameMgr.get());
        e29::SyncOpenTextureEditorsToHost(EditorHost);
        e29::RenderOpenTextureEditors();
        // Host Drawer last so it stacks above Level/Texture peer windows (same OS window).
        EditorHost.draw_host_drawers();



        xgpu::tools::imgui::Render();



        MainWindow.PageFlip();



        xresource::g_Mgr.OnEndFrameDelegate();



        e29::diagnostics::Log("frame %llu end", static_cast<unsigned long long>(FrameNumber));



    }







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



    return 0;



}



