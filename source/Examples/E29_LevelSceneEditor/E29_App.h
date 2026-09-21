#pragma once

#include "source/Examples/E29_LevelSceneEditor/E29_LevelSceneEditorKit.h"

// Moved ahead of E29_GamePlugin.h (was line 22, below) - E29_GamePluginBuild.h's own

// BuildGamePluginIfStale now needs e29::g_ScriptConfig (GetLatestModuleSourceWriteTime,

// E29_GameModuleSources.h) for its own staleness check, so g_ScriptConfig must already be declared

// by the time the umbrella below compiles. E10_AssetMgr.h (e10::g_LibMgr) is already visible via

// E29_LevelSceneEditorKit.h just above, so this is the only reordering actually needed.

#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_ProjectScriptConfig.h"

#include "dependencies/xECSV2/src/xecs_plugin_api.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_GamePlugin.h"
#include "source/Examples/E29_LevelSceneEditor/level/E29_Panel_PlayTransport.h"

#include "dependencies/xundo/source/xundo_history.h"

#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

#include "source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_Selection.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/command_console/E29_CommandConsolePipe.h"

#include "dependencies/xeditor/include/xeditor/host.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/command_console/E29_Commands_Chat.h"

#include "source/Examples/E29_LevelSceneEditor/level/commands/E29_Commands_Level.h"

#include "source/Examples/E29_LevelSceneEditor/level/commands/E29_Commands_SceneDependency.h"

#include "source/Examples/E29_LevelSceneEditor/level/commands/E29_Commands_LibraryDependency.h"

#include "source/Examples/E29_LevelSceneEditor/level/commands/E29_Commands_Workspace.h"

#include "source/Examples/E29_LevelSceneEditor/level/commands/E29_Commands_PlaySession.h"

#include "source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_SceneOrganization.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_AssetBrowser.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_AssetFiles.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_Commands_Scripting.h"

#include "source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_MakePrefab.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_Compilation.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/source_control/E29_Commands_SourceControl.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_TextureEditor.h"

#include "source/Examples/E29_LevelSceneEditor/level/E29_LevelDocument.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/idle_work/E29_IdleWork.h"

#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_ComponentCompatibility.h"

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








#include "source/Examples/E29_LevelSceneEditor/E29_DemoContent.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandSet.h"

//-----------------------------------------------------------------------------------
// e29::app - the editor process: window, world, editor state, command surface and the frame loop.
// Members are declared in startup order (destroyed in reverse). Init/Frame/Shutdown are defined in
// E29_AppInit.h / E29_AppFrame.h.
//-----------------------------------------------------------------------------------
namespace e29
{
    struct app
    {







        xgpu::instance Instance;








        xgpu::device Device;








        xgpu::window MainWindow;








        // Same wiring E10 does: texture (and other) loaders Destroy via UserData.m_Device.



        // Without this, RegisterResource/ReleaseRef (Texture editor preview reload after Compile)



        // crashes in device::Destroy on a default-constructed empty device handle.



        resource_mgr_user_data ResourceMgrUserData{};




        e29::game_plugin_state GamePlugin;








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








        //



        // Asset browser + editor state



        //



        e10::assert_browser  AsserBrowser;




        e29::editor_state    State;








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




        e29::level_host_session LevelHostSession;




        e29::idle_work_state                  IdleWork;




        xundo::history                        E29History;








        // Command Console named pipe - phase 5 of documentation/E29_LevelSceneEditor/command_undo_system_plan.md. Lets an



        // external process (xeditorcli, a script, an AI) drive E29 through E29History.Route() with no UI



        // automation - see extensions/command_console/E29_CommandConsolePipe.h's own top comment for the full threading



        // reasoning. Detached, not joined - a local dev/debug feature, dies with the process, same as



        // E27_NodeOS's own identical pipe thread.



        std::vector<e29::console_log_entry> ConsoleLog;




        e29::command_console_pipe_bridge    ConsolePipeBridge;




        e29::entity_inspector_bridge  InspectorBridge;




        std::uint64_t FrameNumber = 0;




        static constexpr float EditorToolbarWidth = 570.0f;




        static constexpr float SceneToolbarWidth = 390.0f;




        static constexpr float EditorToolbarHeight = 20.0f;




        static constexpr float EditorToolbarFontScale = 1.0f;




        static constexpr float EditorToolbarItemSpacing = 2.0f;








        //



        // Main Loop



        //



        ximgui::toolbar::toolbar_host_state EditorToolbarHost;




        int SceneTool = 0; // Q=select, W=move, E=rotate, R=scale, F=frame




        bool bPivotCenter = true;




        bool bLocalSpace = false;




        bool bGridVisible = true;

        std::unique_ptr<xecs::game_mgr::instance> pGameMgr;








        // Registers e29's own demo content - kept as a local lambda (not inlined at each of the two call



        // sites below) so PollGameReload can re-run the exact same host-registration sequence after a



        // reload, matching what startup does here.



        static constexpr auto RegisterHostComponents = []( xecs::game_mgr::instance& GameMgr ) noexcept



        {



            GameMgr.RegisterComponents<e29::name, e29::transform, xecs::editor::prefab_instance, xecs::component::entity_reference>();



        };




        static constexpr auto RegisterHostSystems = []( xecs::game_mgr::instance& GameMgr ) noexcept



        {



            GameMgr.RegisterSystems<e29::tick_logger_a, e29::tick_logger_b>();



        };








        //



        // Entity component inspector - the currently-selected entity's components. The resource-picker



        // callbacks are stateless (WireResourcePickerCallbacks); the prefab-override/entity-reference



        // ones need live GameMgr/State access, so they're bundled into entity_inspector_bridge (kit).



        //



        xproperty::inspector          EntityInspector{ "Inspector" };



        std::optional<command_set> Commands;

        void RenderParentEditorToolbar();
        void RenderEditorToolbar(const char* Name, ximgui::toolbar::axis Axis);
        void DrawDrawerTab(int TabIndex);
        void WireAssetBrowser();

        int  Init();       // 0 on success, otherwise the process exit code
        void Frame();      // one iteration of the main loop
        void Run();        // Frame() until the window closes
        void Shutdown();
    };
}
