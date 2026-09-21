#ifndef E29_EDITOR_TABS_H
#define E29_EDITOR_TABS_H
#pragma once
#include <utility>
#include "source/xGPU.h"
#include "dependencies/xeditor/include/xeditor/diagnostics.h"
#include "imgui_internal.h"
#include "source/Tools/Editor/xeditor_resource_tab.h"
#include "source/Tools/Editor/xeditor_dock_isolation.h"
#include "dependencies/xeditor/include/xeditor/full_editor_shell.h"
#include <algorithm>
#include <cstdint>
// Level Editor is a peer full-editor root (like Texture). Host services live in the Host Drawer. Its dockspace hosts
// every editor tool directly: Level Editor, Inspector, Browser, Commands, and so on. Those tools
// use a shared docking class, so they can group and split normally with one another while remaining
// unable to dock into the application root.
namespace e29::editor_tabs
{

    // Display name is filled each frame; ### id must stay fixed for focus/dock.
    inline constexpr char kLevelEditorWindowId[] = "E29.LevelEditor";
    inline constexpr char kLevelEditorWindow[] = "Level Editor###E29.LevelEditor"; // legacy alias (logs)
    inline constexpr char kLevelEditorDockspaceId[] = "E29.LevelEditor.Dockspace.V2";
    inline constexpr char kResourceBrowserWindow[] = "Resource Browser###E29.LevelEditor.ResourceBrowser";
    inline constexpr char kEditorWindow[] = "Editor###E29.LevelEditor.Editor";
    inline constexpr char kLevelTreeWindow[] = "Level Tree###E29.LevelEditor.LevelTree";
     inline constexpr char kInspectorWindow[] = "Inspector###E29.LevelEditor.Inspector";
    inline constexpr char kSystemRegistryWindow[] = "System Registry###E29.LevelEditor.SystemRegistry";
    inline constexpr char kGamePluginLogWindow[] = "\xEE\x9F\x83 Log###E29.LevelEditor.GamePluginLog";
    inline constexpr char kCommandConsoleWindow[] = "\xEE\xA3\xBD Commands###E29.LevelEditor.CommandConsole";
    inline constexpr char kSourceControlWindow[] = "Source Control###E29.LevelEditor.SourceControl";

    // Isolation guid for this Level editor instance (set each frame from RenderParentEditorDockspace).

    // Uses xeditor::DockClassForResource ??? same path as Texture ??? instead of a hardcoded ClassId.
    inline xresource::full_guid g_LevelEditorDockGuid{};

    inline ImGuiWindowClass ParentEditorDockClass() noexcept
    {
        if (!g_LevelEditorDockGuid.empty())
            return xeditor::DockClassForResource(g_LevelEditorDockGuid);
        // No level open yet: stable fallback (legacy constant) so panels still share one class.
        ImGuiWindowClass WindowClass;
        WindowClass.ClassId = 0xE290A17u;
        WindowClass.DockingAllowUnclassed = false;
        WindowClass.DockingAlwaysTabBar = true;
        return WindowClass;
    }

    inline void SetNextParentEditorToolClass() noexcept
    {
        const ImGuiWindowClass WindowClass = ParentEditorDockClass();
        ImGui::SetNextWindowClass(&WindowClass);
    }

    // Prefer this name ??? Level is a peer editor, not the app parent shell.

    inline void SetNextLevelEditorToolClass() noexcept { SetNextParentEditorToolClass(); }

    inline void ApplyDockClassToTree(ImGuiDockNode* pNode, const ImGuiWindowClass& WindowClass) noexcept
    {
        if (pNode == nullptr)
            return;
        pNode->WindowClass = WindowClass;
        ApplyDockClassToTree(pNode->ChildNodes[0], WindowClass);
        ApplyDockClassToTree(pNode->ChildNodes[1], WindowClass);
    }
        // Level peer-editor default layout only (DESIGN 4.5). Host services live in the

    // Host Drawer ??? do not dock Resources/SC/Idle/Log/Commands here.

    inline void BuildParentEditorDefaultLayout(ImGuiID DockspaceId, ImVec2 Size) noexcept
    {
        if (ImGui::DockBuilderGetNode(DockspaceId) != nullptr)
            return;
        ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(DockspaceId, Size);
        ImGuiID Remaining = DockspaceId;
        ImGuiID Left = 0;
        ImGuiID Right = 0;
        ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Left,  0.22f, &Left,  &Remaining);
        ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Right, 0.28f, &Right, &Remaining);
        ImGui::DockBuilderDockWindow(kEditorWindow,         Remaining);
        ImGui::DockBuilderDockWindow(kLevelTreeWindow,      Left);
        ImGui::DockBuilderDockWindow(kInspectorWindow,      Right);
        ImGui::DockBuilderDockWindow(kSystemRegistryWindow, Right);
        ImGui::DockBuilderFinish(DockspaceId);
    }

    // The dockspace remains alive while the Level peer root is hidden by another root tab. Dear

    // ImGui otherwise detaches every direct child after one hidden frame.

    template<typename T_RENDER_PARENT_TOOLBAR>

    inline bool RenderParentEditorDockspace(
        T_RENDER_PARENT_TOOLBAR&& RenderParentToolbar,
        const char* DisplayName = "Level",
        xgpu::device* pDevice = nullptr,
        xresource::type_guid TypeGuid = {},
        xresource::full_guid DockGuid = {},
        bool* pOpen = nullptr) noexcept
    {
        g_LevelEditorDockGuid = DockGuid;
        char Title[256];
        xeditor::FormatEditorRootTabTitle(Title, sizeof(Title),
            (DisplayName && *DisplayName) ? DisplayName : "Level",
            kLevelEditorWindowId);
        ImGuiWindowClass ParentWindowClass;
        ParentWindowClass.DockingAlwaysTabBar = true;
        ImGui::SetNextWindowClass(&ParentWindowClass);
        xeditor::SetNextPeerEditorDockedInMainHost(ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(1280.0f, 800.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        // Title is "Name###E29.LevelEditor" ??? stable id; normal theme tab/menu sizes.
        const bool bParentVisible = ImGui::Begin(Title, pOpen, ImGuiWindowFlags_MenuBar);
        {
            const xresource::type_guid IconType = TypeGuid.empty()
                ? xresource::type_guid(xresource::guid_generator::Instance64FromString("Level"))
                : TypeGuid;
            xeditor::DrawEditorRootTabIcon(pDevice, IconType); // every frame, selected or not
        }
        xeditor::diagnostics::Log("window begin: %s visible=%d", Title, bParentVisible ? 1 : 0);
        const ImGuiID ParentDockspaceId = ImGui::GetID(kLevelEditorDockspaceId);
        const ImGuiWindowClass ParentDockClass = ParentEditorDockClass();
        if (bParentVisible)
        {
            RenderParentToolbar();
            BuildParentEditorDefaultLayout(ParentDockspaceId, ImGui::GetContentRegionAvail());
            ImGui::DockSpace(ParentDockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &ParentDockClass);
        }
        else
            ImGui::DockSpace(ParentDockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_KeepAliveOnly, &ParentDockClass);
        xeditor::FinishFullEditorDockspace(ParentDockspaceId, DockGuid);
        ImGui::End();
        xeditor::diagnostics::Log("window end: %s", Title);
        ImGui::PopStyleVar();
        return bParentVisible;
    }

    // Preferred name - Level is a peer editor root (Texture-shaped), not the app shell.
    template<typename T_RENDER_PARENT_TOOLBAR>
    inline bool RenderLevelEditorDockspace(
        T_RENDER_PARENT_TOOLBAR&& RenderParentToolbar,
        const char* DisplayName = "Level",
        xgpu::device* pDevice = nullptr,
        xresource::type_guid TypeGuid = {},
        xresource::full_guid DockGuid = {},
        bool* pOpen = nullptr) noexcept
    {
        return RenderParentEditorDockspace(
            std::forward<T_RENDER_PARENT_TOOLBAR>(RenderParentToolbar),
            DisplayName, pDevice, TypeGuid, DockGuid, pOpen);
    }

}
#endif // E29_EDITOR_TABS_H
