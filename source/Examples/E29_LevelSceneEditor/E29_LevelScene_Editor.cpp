#include "source/Examples/E29_LevelSceneEditor/E29_LevelSceneEditorKit.h"

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
    // ECS setup - first xGPU example to own an xecs::game_mgr::instance.
    //
    xecs::game_mgr::instance GameMgr;
    GameMgr.RegisterComponents<e29::name, e29::transform, xecs::editor::prefab_instance, xecs::component::entity_reference>();
    GameMgr.RegisterSystems<>(); // locks component bit IDs - required even with zero systems

    //
    // Project path (same lookup every editor example uses)
    //
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

            GameMgr.m_SceneMgr.m_ProjectPath  = e10::g_LibMgr.m_ProjectPath;
            GameMgr.m_LevelMgr.m_ProjectPath  = e10::g_LibMgr.m_ProjectPath;
            GameMgr.m_PrefabMgr.m_ProjectPath = e10::g_LibMgr.m_ProjectPath;
        }
    }

    //
    // Asset browser + editor state
    //
    e10::assert_browser  AsserBrowser;
    e29::editor_state    State;

    // Lets entity_to_prefab_drop::OnDrop (a static, globally-registered object) reach the live
    // GameMgr/State at drop time - see their own declaration comment for why this is safe here.
    e29::g_pGameMgr = &GameMgr;
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
    InspectorBridge.RegisterCallbacks(EntityInspector, GameMgr, State);

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
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
                    e29::SaveEverything(GameMgr, State);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // The menu item above only ever LABELS "Ctrl+S" - ImGui::MenuItem's shortcut string is
        // purely decorative and doesn't bind anything on its own. Checked once per frame,
        // unconditionally (not gated behind the File menu being open).
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            e29::SaveEverything(GameMgr, State);

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);
        e29::g_AssetBrowserPopup.RenderAsPopup(e10::g_LibMgr, xresource::g_Mgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false)
        {
            if (NewAsset.m_Type == xecs::level::type_guid_v) e29::OpenLevel(GameMgr, State, NewAsset);
            else if (NewAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(GameMgr, State, NewAsset);
        }
        else if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false)
        {
            if (SelAsset.m_Type == xecs::level::type_guid_v) e29::OpenLevel(GameMgr, State, SelAsset);
            else if (SelAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(GameMgr, State, SelAsset);
        }

        e29::RenderLevelTreePanel(GameMgr, State);
        e29::RenderEntityPropertiesPanel(GameMgr, State, EntityInspector, InspectorBridge);

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();
    return 0;
}
