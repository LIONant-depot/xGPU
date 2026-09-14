#ifndef E29_EDITOR_TABS_H
#define E29_EDITOR_TABS_H
#pragma once

#include "source/xGPU.h"
#include "source/Examples/E29_LevelSceneEditor/E29_Diagnostics.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdint>

// Parent Editor Window is the sole application-level, main-dockable window. Its dockspace hosts
// every editor tool directly: Level Editor, Inspector, Browser, Commands, and so on. Those tools
// use a shared docking class, so they can group and split normally with one another while remaining
// unable to dock into the application root.
namespace e29::editor_tabs
{
    inline constexpr char kLevelEditorWindow[] = "Level Editor###E29.LevelEditor";
    inline constexpr char kLevelEditorDockspaceId[] = "E29.LevelEditor.Dockspace.V1";

    inline constexpr char kResourceBrowserWindow[] = "Resource Browser###E29.LevelEditor.ResourceBrowser";
    inline constexpr char kEditorWindow[] = "Editor###E29.LevelEditor.Editor";
    inline constexpr char kLevelTreeWindow[] = "Level Tree###E29.LevelEditor.LevelTree";
     inline constexpr char kInspectorWindow[] = "Inspector###E29.LevelEditor.Inspector";
    inline constexpr char kSystemRegistryWindow[] = "System Registry###E29.LevelEditor.SystemRegistry";
    inline constexpr char kIdleWorkWindow[] = "Idle Work###E29.LevelEditor.IdleWork";
    inline constexpr char kGamePluginLogWindow[] = "\xEE\x9F\x83 Log###E29.LevelEditor.GamePluginLog";
    inline constexpr char kCommandConsoleWindow[] = "\xEE\xA3\xBD Commands###E29.LevelEditor.CommandConsole";
    inline constexpr ImGuiID kParentEditorDockClassId = 0xE290A17u;

    inline ImGuiWindowClass ParentEditorDockClass() noexcept
    {
        ImGuiWindowClass WindowClass;
        WindowClass.ClassId = kParentEditorDockClassId;
        WindowClass.DockingAllowUnclassed = false;
        WindowClass.DockingAlwaysTabBar = true;
        return WindowClass;
    }

    inline void SetNextParentEditorToolClass() noexcept
    {
        const ImGuiWindowClass WindowClass = ParentEditorDockClass();
        ImGui::SetNextWindowClass(&WindowClass);
    }

    inline void ApplyDockClassToTree(ImGuiDockNode* pNode, const ImGuiWindowClass& WindowClass) noexcept
    {
        if (pNode == nullptr)
            return;
        pNode->WindowClass = WindowClass;
        ApplyDockClassToTree(pNode->ChildNodes[0], WindowClass);
        ApplyDockClassToTree(pNode->ChildNodes[1], WindowClass);
    }

    inline void BuildParentEditorDefaultLayout(ImGuiID DockspaceId, ImVec2 Size) noexcept
    {
        if (ImGui::DockBuilderGetNode(DockspaceId) != nullptr)
            return;

        ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(DockspaceId, Size);

        ImGuiID Remaining = DockspaceId;
        ImGuiID Left = 0;
        ImGuiID Right = 0;
        ImGuiID Bottom = 0;
        ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Left,  0.22f, &Left,   &Remaining);
        ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Right, 0.28f, &Right,  &Remaining);
        ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Down,  0.30f, &Bottom, &Remaining);

        ImGui::DockBuilderDockWindow(kEditorWindow,              Remaining);
        ImGui::DockBuilderDockWindow(kResourceBrowserWindow,     Left);
        ImGui::DockBuilderDockWindow(kLevelTreeWindow,           Left);
        ImGui::DockBuilderDockWindow(kInspectorWindow,           Right);
        ImGui::DockBuilderDockWindow(kSystemRegistryWindow,      Right);
        ImGui::DockBuilderDockWindow(kCommandConsoleWindow,      Bottom);
        ImGui::DockBuilderDockWindow(kIdleWorkWindow,            Bottom);
        ImGui::DockBuilderDockWindow(kGamePluginLogWindow,       Bottom);
        ImGui::DockBuilderFinish(DockspaceId);
    }

    // The dockspace remains alive while Parent Editor Window is hidden by another root tab. Dear
    // ImGui otherwise detaches every direct child after one hidden frame.
    template<typename T_RENDER_PARENT_TOOLBAR>
    inline bool RenderParentEditorDockspace(T_RENDER_PARENT_TOOLBAR&& RenderParentToolbar) noexcept
    {
        ImGuiWindowClass ParentWindowClass;
        ParentWindowClass.DockingAlwaysTabBar = true;
        ImGui::SetNextWindowClass(&ParentWindowClass);
        ImGui::SetNextWindowSize(ImVec2(1280.0f, 800.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool bParentVisible = ImGui::Begin(kLevelEditorWindow, nullptr, ImGuiWindowFlags_MenuBar);
        diagnostics::Log("window begin: %s visible=%d", kLevelEditorWindow, bParentVisible ? 1 : 0);
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
        ApplyDockClassToTree(ImGui::DockBuilderGetNode(ParentDockspaceId), ParentDockClass);
        ImGui::End();
        diagnostics::Log("window end: %s", kLevelEditorWindow);
        ImGui::PopStyleVar();
        return bParentVisible;
    }
}

#endif // E29_EDITOR_TABS_H
