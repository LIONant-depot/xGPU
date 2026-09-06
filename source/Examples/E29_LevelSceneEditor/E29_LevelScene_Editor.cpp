#include "source/Examples/E29_LevelSceneEditor/E29_LevelSceneEditorKit.h"
#include "dependencies/xECSV2/src/xecs_plugin_api.h"
#include "source/Examples/E29_LevelSceneEditor/E29_GamePlugin.h"

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
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.85f;

    //
    // ECS setup - first xGPU example to own an xecs::game_mgr::instance. A unique_ptr (not a plain
    // stack value) specifically so Phase 8's ReloadGame can destroy and reconstruct the whole world
    // in place - see E29_GamePlugin.h's own comment on why that's the correct, sufficient operation
    // for a hot reload rather than something narrower.
    //
    auto pGameMgr = std::make_unique<xecs::game_mgr::instance>();
    e29::game_plugin_state GamePlugin;

    // Registers e29's own demo content - kept as a local lambda (not inlined at each of the two call
    // sites below) so ReloadGame can re-run the exact same host-registration sequence after a
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
    // running) needs ReloadGame's destroy-and-recreate sequence. Missing/failing to load is not an
    // error - E29 runs exactly as before with no game loaded, matching the "user builds it, or E29
    // does" direction: nothing has been built yet on a fresh checkout, and that's fine.
    {
        TCHAR szModulePath[MAX_PATH];
        GetModuleFileName(NULL, szModulePath, MAX_PATH);
        std::filesystem::path GameDllPath = std::filesystem::path(szModulePath).parent_path() / L"E29_Game.dll";
        GamePlugin.m_DllPath = GameDllPath.wstring();
        e29::LoadGamePluginComponents(*pGameMgr, GamePlugin, /*Generation*/ 1);
    }

    RegisterHostSystems(*pGameMgr);
    e29::RegisterGamePluginSystems(*pGameMgr, GamePlugin);

    //
    // Project path (same lookup every editor example uses) - kept around (not just a local) so
    // ReloadGame can re-apply it to a freshly reconstructed pGameMgr.
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
    // Rebound by ReloadGame after a hot reload replaces *pGameMgr with a fresh instance.
    e29::g_pGameMgr = pGameMgr.get();
    e29::g_pState   = &State;

    // Visible from the start and never closable - browsing/creating Levels and Scenes is this
    // editor's primary activity (not an occasional lookup), so it's a permanent, dockable part of the
    // layout rather than a modal picker: DOCKABLE drops the bottom Close button and lets it dock like
    // Level Editor/Entity Properties instead of floating as an undockable overlay. (The "+" pickers
    // elsewhere in this file use a separate e10::assert_browser instance, e29::g_AssetBrowserPopup,
    // which stays at the POPUP default.)
    AsserBrowser.setDisplayMode(e10::assert_browser::display_mode::DOCKABLE);
    AsserBrowser.Show(true);

    //
    // Entity component inspector - the currently-selected entity's components. The resource-picker
    // callbacks are stateless (WireResourcePickerCallbacks); the prefab-override/entity-reference
    // ones need live GameMgr/State access, so they're bundled into entity_inspector_bridge (kit).
    //
    xproperty::inspector          EntityInspector("Entity Properties");
    e29::entity_inspector_bridge  InspectorBridge;
    e29::WireResourcePickerCallbacks(EntityInspector);
    InspectorBridge.RegisterCallbacks(EntityInspector, *pGameMgr, State);

    //
    // Main Loop
    //
    // Set by the "Reload Game" button (see below) and consumed here, BEFORE BeginRendering starts
    // this frame - not run synchronously at the point of the click. Confirmed empirically (same
    // methodology as the startup Debugger()-timing bug this session already found and fixed):
    // ReloadGame's destroy-and-recreate-the-whole-world sequence is heavy enough, and the click
    // itself happens nested inside an active ImGui::BeginMainMenuBar()/EndMainMenuBar() scope, that
    // running it synchronously corrupted ImGui's window-stack bookkeeping the same way. Running it
    // here instead gives it a clean "no active ImGui frame" execution context, same as every other
    // safe Debugger()/heavy-state-mutation call site in this file.
    bool bReloadGameRequested = false;

    while (Instance.ProcessInputEvents())
    {
        if (bReloadGameRequested)
        {
            bReloadGameRequested = false;
            e29::ReloadGame
            ( pGameMgr, State, GamePlugin, EntityInspector, InspectorBridge, ProjectPath
            , RegisterHostComponents, RegisterHostSystems
            );
        }

        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        e29::RenderErrorPopup();

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
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                    e29::SaveEverything(*pGameMgr, State);
                ImGui::EndMenu();
            }

            // Phase 8 of the xECSV2 type-registration architecture plan - hot-reloads
            // E29_Game.dll: destroys the whole runtime world, unloads the current plugin
            // generation, reloads the (possibly just-rebuilt-by-the-user) DLL, and reconstructs a
            // fresh world with everything re-registered - see E29_GamePlugin.h's own ReloadGame for
            // the exact sequence. Only sets a flag here - the main loop's own top (before
            // BeginRendering) is where ReloadGame actually runs; see that flag's own declaration
            // comment for why running it synchronously, right here, corrupted ImGui's own state.
            ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
            if (ImGui::Button("Reload Game"))
                bReloadGameRequested = true;

            // Minimal Play/Stop toggle - just flips the flag; the actual GameMgr.Run()/Stop() calls
            // happen once per frame below, unconditionally, regardless of which way this just
            // flipped (both are internally gated on GameMgr.m_isRunning, so that's safe/idempotent
            // and keeps this button dead simple).
            ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);
            if (ImGui::Button(State.m_bPlaying ? "Stop" : "Play"))
                State.m_bPlaying = !State.m_bPlaying;

            ImGui::EndMainMenuBar();
        }

        // The menu item above only ever LABELS "Ctrl+S" - ImGui::MenuItem's shortcut string is
        // purely decorative and doesn't bind anything on its own. Checked once per frame,
        // unconditionally (not gated behind the File menu being open).
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            e29::SaveEverything(*pGameMgr, State);

        // GameMgr.Run()/Stop() already exist and do everything needed: Run() ticks every enabled
        // Update system in its current order (via m_SystemMgr.Run()) and, on the Stopped->Running
        // transition, snapshots the System Registry's current order/enabled state; Stop() restores
        // that snapshot on the reverse transition. E29 has no viewport yet, so "Play" here only means
        // "the ECS's own systems tick" - proving the System Registry feature, not adding a game view.
        if (State.m_bPlaying) pGameMgr->Run();
        else                  pGameMgr->Stop();

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);
        e29::g_AssetBrowserPopup.RenderAsPopup(e10::g_LibMgr, xresource::g_Mgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false)
        {
            if (NewAsset.m_Type == xecs::level::type_guid_v) e29::OpenLevel(*pGameMgr, State, NewAsset);
            else if (NewAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, NewAsset);
        }
        else if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false)
        {
            if (SelAsset.m_Type == xecs::level::type_guid_v) e29::OpenLevel(*pGameMgr, State, SelAsset);
            else if (SelAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(*pGameMgr, State, SelAsset);
        }

        e29::RenderLevelTreePanel(*pGameMgr, State);
        e29::RenderEntityPropertiesPanel(*pGameMgr, State, EntityInspector, InspectorBridge);
        e29::RenderSystemRegistryPanel(*pGameMgr, State);

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    e29::UnloadGamePlugin(GamePlugin);

    xgpu::tools::imgui::Shutdown();
    return 0;
}
