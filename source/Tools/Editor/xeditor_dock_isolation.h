#ifndef XEDITOR_DOCK_ISOLATION_H
#define XEDITOR_DOCK_ISOLATION_H
#pragma once

// Generalizes E29_LevelSceneEditor/E29_EditorTabs.h's own ApplyDockClassToTree/ClassId/
// DockingAllowUnclassed pattern - real, working, single-instance dock isolation - so it works for
// N simultaneously open full-editor instances instead of one hardcoded literal
// (kParentEditorDockClassId = 0xE290A17u) that only ever worked because exactly one E29 existed
// at a time. Keyed by the open resource's own full_guid: two open Texture editors (or a Texture
// editor and any other editor) never let their panels cross-dock. Embedded-viewer hosts must NOT
// call this - they are a rect handed down by a parent, not an independent dockable panel group.
#include "dependencies/xresource_guid/source/xresource_guid.h" // also declares std::hash<xresource::full_guid>
#include "imgui.h"

namespace xeditor
{
    inline void ApplyDockClassToTree(ImGuiDockNode* pNode, const ImGuiWindowClass& WindowClass) noexcept
    {
        if (pNode == nullptr) return;
        pNode->WindowClass = WindowClass;
        ApplyDockClassToTree(pNode->ChildNodes[0], WindowClass);
        ApplyDockClassToTree(pNode->ChildNodes[1], WindowClass);
    }

    // Derives a per-resource ClassId from the resource's own guid (rather than a hardcoded
    // per-example literal) so isolation is per open-editor-instance, not per resource type.
    inline ImGuiWindowClass DockClassForResource(xresource::full_guid Guid) noexcept
    {
        ImGuiWindowClass WindowClass;
        WindowClass.ClassId              = static_cast<ImGuiID>(std::hash<xresource::full_guid>{}(Guid) | 0x1u); // never 0 - ImGui treats ClassId 0 as "no class"
        WindowClass.DockingAllowUnclassed = false;
        WindowClass.DockingAlwaysTabBar   = true;
        return WindowClass;
    }

    // Call once per frame from the resource's own full-editor host, immediately after its own
    // nested ImGui::DockSpace(...) call - same placement E29's own RenderParentEditorDockspace
    // uses.
    inline void IsolateEditorDockspace(ImGuiID DockspaceId, xresource::full_guid Guid) noexcept
    {
        const auto WindowClass = DockClassForResource(Guid);
        ApplyDockClassToTree(ImGui::DockBuilderGetNode(DockspaceId), WindowClass);
    }
}

#endif // XEDITOR_DOCK_ISOLATION_H
