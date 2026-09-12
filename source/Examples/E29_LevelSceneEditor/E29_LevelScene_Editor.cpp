#include "source/Examples/E29_LevelSceneEditor/E29_LevelSceneEditorKit.h"
#include "dependencies/xECSV2/src/xecs_plugin_api.h"
#include "source/Examples/E29_LevelSceneEditor/E29_GamePlugin.h"
#include "dependencies/xundo/source/xundo_history.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Selection.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandConsolePipe.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Chat.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Level.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Workspace.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_PlaySession.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_SceneOrganization.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetBrowser.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetFiles.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_MakePrefab.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Compilation.h"
#include "source/Examples/E29_LevelSceneEditor/kit/E29_IdleWork.h"
#include "source/Examples/E29_LevelSceneEditor/E29_Theme.h"

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
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_pLogErrorFunc = e29::Debugger, .m_pLogWarning = e29::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xresource::g_Mgr.Initiallize(20000);

    //
    // Setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);
    e29::theme::ApplyUnityInspiredTheme();

    // io.FontDefault (not a per-frame PushFont) - xgpu::tools::imgui::BeginRendering() calls
    // ImGui::DockSpace() internally, BEFORE E29's own render code ever runs, and ImGui's docking tab
    // bar renders using whatever font is current AT THAT POINT - a PushFont in E29's own loop (after
    // BeginRendering returns) is too late to affect it, which is exactly why the dock tab labels
    // ("Resources"/"Assets"/...) kept rendering in the old default Consolas even after every panel's
    // own content switched to Segoe UI. Overriding io.FontDefault instead affects ImGui::NewFrame()'s
    // own g.Font reset, which runs before DockSpace() - this is process-global IO state, but safe here
    // because every xGPU example is its own separate process (E10/E19-28 never call this line).
    ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->Fonts[4];

    //
    // ECS setup - first xGPU example to own an xecs::game_mgr::instance. A unique_ptr (not a plain
    // stack value) specifically so Phase 8's PollGameReload can destroy and reconstruct the whole world
    // in place - see E29_GamePlugin.h's own comment on why that's the correct, sufficient operation
    // for a hot reload rather than something narrower.
    //
    auto pGameMgr = std::make_unique<xecs::game_mgr::instance>();
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

    RegisterHostComponents(*pGameMgr);

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
        e29::BuildGamePluginIfStale(GamePlugin);
        e29::LoadGamePluginComponents(*pGameMgr, GamePlugin, /*Generation*/ 1);
    }
#else
    e29::LogGamePlugin("Game.dll: this build was configured without XECS_BUILD_SHARED_LIBRARY (see CMakeLists.txt) - Game.dll support is disabled, Play just ticks the host's own systems.");
#endif

    RegisterHostSystems(*pGameMgr);
    e29::RegisterGamePluginSystems(*pGameMgr, GamePlugin);

    //
    // Project path (same lookup every editor example uses) - kept around (not just a local) so
    // PollGameReload can re-apply it to a freshly reconstructed pGameMgr.
    //
    std::wstring ProjectPath;
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos)
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;

            TCHAR LIONantProject[] = L"\\example.lionprj";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e29::Debugger(Err.getMessage());
                return 1;
            }

            ImGuiIO& io = ImGui::GetIO();
            static std::string IniSave = std::format("{}/Assets/imgui_e29.ini", xstrtool::To(szFileName));
            io.IniFilename = IniSave.c_str();

            ProjectPath = e10::g_LibMgr.m_ProjectPath;
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
        }
    }

    //
    // Asset browser + editor state
    //
    e10::assert_browser  AsserBrowser;
    e29::editor_state    State;

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
    AsserBrowser.Show(true);

    //
    // Command/undo system - phase 1 of [[e29_command_undo_system_plan]] (memory): selection only,
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
    if (auto Err = E29Undo.Init({}, false); !Err.empty())
        e29::Debugger(std::format("E29: xundo Init failed: {}", Err));
    e29::g_pUndo = &E29Undo;
    e29::commands::select_cmd             CmdSelect(E29Undo, &CmdContext);
    e29::commands::toggle_multi_select_cmd CmdToggleMultiSelect(E29Undo, &CmdContext);
    e29::commands::clear_selection_cmd    CmdClearSelection(E29Undo, &CmdContext);
    e29::commands::set_property_cmd       CmdSetProperty(E29Undo, &CmdContext);
    e29::commands::add_component_cmd      CmdAddComponent(E29Undo, &CmdContext);
    e29::commands::remove_component_cmd   CmdRemoveComponent(E29Undo, &CmdContext);
    e29::commands::create_entity_cmd      CmdCreateEntity(E29Undo, &CmdContext);
    e29::commands::delete_entity_cmd      CmdDeleteEntity(E29Undo, &CmdContext);
    e29::commands::say_query_cmd          CmdSay(E29Undo, &CmdContext);
    e29::commands::get_log_query_cmd      CmdGetLog(E29Undo, &CmdContext);
    e29::commands::open_level_cmd         CmdOpenLevel(E29Undo, &CmdContext);
    e29::commands::close_scene_cmd        CmdCloseScene(E29Undo, &CmdContext);
    e29::commands::list_levels_query_cmd  CmdListLevels(E29Undo, &CmdContext);
    e29::commands::list_scenes_query_cmd  CmdListScenes(E29Undo, &CmdContext);
    e29::commands::list_entities_query_cmd CmdListEntities(E29Undo, &CmdContext);
    e29::commands::list_folders_query_cmd CmdListFolders(E29Undo, &CmdContext);
    e29::commands::undo_query_cmd         CmdUndo(E29Undo, &CmdContext);
    e29::commands::redo_query_cmd         CmdRedo(E29Undo, &CmdContext);
    e29::commands::save_query_cmd         CmdSave(E29Undo, &CmdContext);
    e29::commands::describe_entity_query_cmd CmdDescribeEntity(E29Undo, &CmdContext);
    e29::commands::list_component_types_query_cmd CmdListComponentTypes(E29Undo, &CmdContext);
    e29::commands::set_entity_reference_cmd CmdSetEntityReference(E29Undo, &CmdContext);
    e29::commands::play_query_cmd         CmdPlay(E29Undo, &CmdContext);
    e29::commands::pause_query_cmd        CmdPause(E29Undo, &CmdContext);
    e29::commands::stop_query_cmd         CmdStop(E29Undo, &CmdContext);
    e29::commands::get_play_state_query_cmd CmdGetPlayState(E29Undo, &CmdContext);
    e29::commands::instantiate_prefab_cmd CmdInstantiatePrefab(E29Undo, &CmdContext);
    e29::commands::move_to_folder_cmd     CmdMoveToFolder(E29Undo, &CmdContext);
    e29::commands::create_folder_cmd      CmdCreateFolder(E29Undo, &CmdContext);
    e29::commands::delete_folder_cmd      CmdDeleteFolder(E29Undo, &CmdContext);
    e29::commands::list_assets_query_cmd  CmdListAssets(E29Undo, &CmdContext);
    e29::commands::describe_asset_query_cmd CmdDescribeAsset(E29Undo, &CmdContext);
    e29::commands::rename_asset_cmd       CmdRenameAsset(E29Undo, &CmdContext);
    e29::commands::move_asset_cmd         CmdMoveAsset(E29Undo, &CmdContext);
    e29::commands::delete_asset_cmd       CmdDeleteAsset(E29Undo, &CmdContext);
    e29::commands::restore_asset_cmd      CmdRestoreAsset(E29Undo, &CmdContext);
    e29::commands::create_asset_cmd       CmdCreateAsset(E29Undo, &CmdContext);
    e29::commands::save_assets_query_cmd  CmdSaveAssets(E29Undo, &CmdContext);
    e29::commands::rename_asset_file_cmd  CmdRenameAssetFile(E29Undo, &CmdContext);
    e29::commands::move_asset_file_cmd    CmdMoveAssetFile(E29Undo, &CmdContext);
    e29::commands::delete_asset_file_cmd  CmdDeleteAssetFile(E29Undo, &CmdContext);
    e29::commands::restore_asset_file_cmd CmdRestoreAssetFile(E29Undo, &CmdContext);
    e29::commands::copy_asset_file_cmd    CmdCopyAssetFile(E29Undo, &CmdContext);
    e29::commands::make_prefab_cmd        CmdMakePrefab(E29Undo, &CmdContext);
    e29::commands::make_prefab_variant_cmd CmdMakePrefabVariant(E29Undo, &CmdContext);
    e29::commands::recompile_all_query_cmd    CmdRecompileAll(E29Undo, &CmdContext);
    e29::commands::recompile_errors_query_cmd CmdRecompileErrors(E29Undo, &CmdContext);
    e29::commands::compile_start_query_cmd    CmdCompileStart(E29Undo, &CmdContext);
    e29::commands::compile_pause_query_cmd    CmdCompilePause(E29Undo, &CmdContext);
    e29::commands::compile_auto_query_cmd     CmdCompileAuto(E29Undo, &CmdContext);
    e29::commands::compile_status_query_cmd   CmdCompileStatus(E29Undo, &CmdContext);
    e29::commands::run_sanity_check_query_cmd CmdRunSanityCheck(E29Undo, &CmdContext);
    e29::idle_work_state                  IdleWork;
    xundo::history                        E29History;
    E29History.AddSystem("E29", 1, E29Undo);

    // Command Console named pipe - phase 5 of [[e29_command_undo_system_plan]] (memory). Lets an
    // external process (E29CLI.cpp, a script, an AI) drive E29 through E29History.Route() with no UI
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
    xproperty::inspector          EntityInspector("Entity Properties");
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

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        // No more manual "Reload Game" button - recompiling is something the editor just does for
        // you, per direct user direction to follow Unity's own model. Two automatic triggers only:
        // the window regaining OS focus (the user tabbed back in after editing code - checked here,
        // unconditionally, every frame, since ConsumeWindowFocusGained is edge-triggered/self-
        // consuming and cheap to poll) and the Play button itself (see its own handler below, which
        // sets State.m_bPlayRequested and calls StartGameReload the same way).
#if defined(XECS_BUILD_SHARED)
        if (xgpu::tools::imgui::ConsumeWindowFocusGained())
            e29::StartGameReload(GamePlugin);
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
        ( pGameMgr, State, GamePlugin, EntityInspector, InspectorBridge, E29Undo, ProjectPath
        , RegisterHostComponents, RegisterHostSystems
        );

        // A no-op unless a pipe client (E29CLI.cpp) has a request waiting - see
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
            ( pGameMgr, State, GamePlugin, EntityInspector, InspectorBridge, E29Undo, ProjectPath
            , RegisterHostComponents, RegisterHostSystems
            , State.m_PendingKeepTweaksCommands
            );
            State.m_PendingKeepTweaksCommands.clear();
        }

        if (xgpu::tools::imgui::BeginRendering(true)) continue;

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
        if (pGameMgr)
            e29::PumpIdleWork(IdleWork, *pGameMgr, State);

        e29::RenderErrorPopup();
        e29::RenderKeepTweaksModal(State, E29Undo);

        //
        // Main menu bar - same "File > Asset Browser..."/"Save Project" pattern every other editor
        // example uses (see E24_AnimPackage_Editor.cpp's identical menu). AsserBrowser.Render() is a
        // no-op until Show(true) is called at least once - every other example gates that behind this
        // exact menu item, not an always-on window.
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Asset Browser..."))
                    AsserBrowser.Show(true);

                ImGui::Separator();
                // Gated while Playing/Paused, direct user request after an external review correctly
                // flagged it: V1 (the disk save Stop reverts to) is the SAME file SaveEverything
                // writes - an unguarded Save mid-play-session would overwrite that revert point with
                // in-flight play-mode mutations, silently defeating "Stop restores exactly what it
                // was before Play." Neither Unity nor Unreal lets you commit play-mode state into the
                // real project this way. The Ctrl+S shortcut below is gated identically.
                ImGui::BeginDisabled(State.isPlaying());
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                    e29::SaveEverything(*pGameMgr, State);
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }

            // Recompile status - purely informational now (no button; see the focus-regain/Play
            // triggers above). Only shown while a background build is actually running, so the menu
            // bar stays quiet the rest of the time.
            if (GamePlugin.m_bBuilding)
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
                ImGui::TextDisabled("Game.dll: building...");
            }

            // Play / Pause / Stop transport - matches Unity's own: Play (Stopped -> Playing) kicks
            // off a recompile-check first (StartGameReload) and defers actually entering play until
            // that resolves (State.m_bPlayRequested - see PollGameReload), so play never starts
            // against a DLL that might still be mid-rebuild; Play again while Paused is just a
            // resume, no check needed (nothing about the code could have changed while already
            // mid-session without already having gone through a reload). Pause halts ticking without
            // touching the world at all - Stop is the only transition that discards anything, via
            // StopPlaySession's own proper disk-based revert (see E29_GamePlugin.h).
            ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
            ImGui::BeginDisabled(State.m_PlayState == e29::editor_state::play_state::Playing || GamePlugin.m_bBuilding);
            if (ImGui::Button(State.m_PlayState == e29::editor_state::play_state::Paused ? "Resume" : "Play"))
            {
                if (State.m_PlayState == e29::editor_state::play_state::Stopped)
                {
#if defined(XECS_BUILD_SHARED)
                    State.m_bPlayRequested = true;
                    e29::StartGameReload(GamePlugin);
#else
                    // No Game.dll in this build config - nothing to recompile-check, so skip
                    // straight to what PollGameReload's own UpToDate branch does: write V1 (Stop's
                    // revert point) and enter Play directly.
                    e29::SaveEverything(*pGameMgr, State);
                    State.m_PlayHistoryBoundary = E29Undo.GetUndoIndex();
                    State.m_PlayState = e29::editor_state::play_state::Playing;
#endif
                }
                else // Paused -> Playing, plain resume
                {
                    State.m_PlayState = e29::editor_state::play_state::Playing;
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine(ImGui::GetWindowWidth() - 115.0f);
            ImGui::BeginDisabled(State.m_PlayState != e29::editor_state::play_state::Playing);
            if (ImGui::Button("Pause"))
                State.m_PlayState = e29::editor_state::play_state::Paused;
            ImGui::EndDisabled();

            ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
            ImGui::BeginDisabled(State.m_PlayState == e29::editor_state::play_state::Stopped);
            // Deferred to the top of next frame (State.m_bStopRequested, consumed alongside
            // PollGameReload above) rather than run here directly - StopPlaySession does the same
            // heavy destroy-and-recreate-the-world work PollGameReload does, and this click happens
            // nested inside the still-active BeginMainMenuBar()/EndMainMenuBar() scope, which is
            // exactly the "corrupts ImGui's window-stack bookkeeping" bug this file's own comment
            // above already warns about. RequestStop (E29_PlaySession.h) decides right here whether
            // there's anything to ask about - if there is, it opens the "keep these?" confirmation
            // (RenderKeepTweaksModal, called every frame below) instead of setting the flag directly.
            if (ImGui::Button("Stop"))
                e29::RequestStop(State, E29Undo, std::nullopt);
            ImGui::EndDisabled();

            ImGui::EndMainMenuBar();
        }

        // The menu item above only ever LABELS "Ctrl+S" - ImGui::MenuItem's shortcut string is
        // purely decorative and doesn't bind anything on its own. Checked once per frame,
        // unconditionally (not gated behind the File menu being open).
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !State.isPlaying())
            e29::SaveEverything(*pGameMgr, State);

        // Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z for Redo) - same shortcut convention as E27_NodeOS's own
        // (E27_NodeOS_Editor.cpp), guarded by WantTextInput so typing "z" into a property text field
        // never gets mistaken for an undo shortcut. Gated on !State.isPlaying() - same gate Ctrl+S
        // already has just above - since undoing/redoing a structural command (CreateEntity/
        // DeleteEntity/AddComponent/RemoveComponent) against the live ticking world is untested
        // territory and could interact badly with the V1/Vn snapshot-restore sequence Play/Stop
        // relies on ([[e29_command_undo_known_gaps]]). The Undo/Redo QUERY commands
        // (E29_Commands_Workspace.h) carry the same gate, so a CLI/Console-driven agent can't bypass
        // what the UI shortcut refuses either.
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !State.isPlaying())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().KeyShift) E29Undo.Undo();
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyShift)) E29Undo.Redo();
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
        if (State.m_PlayState == e29::editor_state::play_state::Playing)
            pGameMgr->Run();

        AsserBrowser.SetDevice(Device);
        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);
        e29::g_AssetBrowserPopup.SetDevice(Device);
        e29::g_AssetBrowserPopup.RenderAsPopup(e10::g_LibMgr, xresource::g_Mgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false)
        {
            if (NewAsset.m_Type == xecs::level::type_guid_v)
            {
                e29::OpenLevel(*pGameMgr, State, NewAsset);
                // Opening a Level is also a natural "am I looking at current code" moment, same as
                // regaining window focus or pressing Play - direct user request: code added/removed
                // since the last check (e.g. edited while this Level wasn't even open yet) should be
                // reflected the moment a Level is loaded, not only on the next focus-regain/Play. A
                // no-op if a build is already in flight (see StartGameReload's own guard); edge-
                // triggered here too (getNewAsset()/getSelectedAsset() only return non-empty once per
                // actual selection - see E10_AssetBrowser.h), so this can't spam a build per frame.
#if defined(XECS_BUILD_SHARED)
                e29::StartGameReload(GamePlugin);
#endif
            }
            else if (NewAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, NewAsset);
        }
        else if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false)
        {
            if (SelAsset.m_Type == xecs::level::type_guid_v)
            {
                e29::OpenLevel(*pGameMgr, State, SelAsset);
#if defined(XECS_BUILD_SHARED)
                e29::StartGameReload(GamePlugin);
#endif
            }
            else if (SelAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, SelAsset);
        }

        e29::RenderLevelTreePanel(*pGameMgr, State, E29Undo);
        e29::RenderEntityPropertiesPanel(*pGameMgr, State, EntityInspector, InspectorBridge, E29Undo);
        e29::RenderSystemRegistryPanel(*pGameMgr, State);
        e29::RenderIdleWorkPanel(IdleWork, pGameMgr.get(), State);
        e29::RenderGamePluginLogPanel();
        e29::DrawCommandConsolePanel(E29History, ConsoleLog);

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    e29::UnloadGamePlugin(GamePlugin);

    xgpu::tools::imgui::Shutdown();
    return 0;
}
