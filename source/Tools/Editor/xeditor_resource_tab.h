#ifndef XEDITOR_RESOURCE_TAB_H
#define XEDITOR_RESOURCE_TAB_H
#pragma once

// Main editor root tabs: resource-type icon + asset name (not "Level Editor" /
// "Texture Editor"), slightly taller than nested tool tabs.
//
// Dock tab HEIGHT is set in DockNodeCalcTabBarLayout from FramePadding when the
// MAIN DockSpace runs (BeginRendering). BeginDocked still keeps a hidden title
// bar for offset — bump ONLY TitleBarHeight after Begin (see
// ApplyMainDockTabTitleBarOffset) so MenuBar/toolbar stay normal height.
#include "dependencies/imgui/imgui.h"
#include "dependencies/imgui/imgui_internal.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"
#include <cstdio>
#include <string>

namespace xeditor
{
    inline constexpr float kEditorTabExtraPadY = 3.0f;
    inline constexpr float kEditorTabIconPx    = 18.0f;
    inline constexpr const char* kEditorTabIconSpacer = "     ";

    // Wrap ONLY BeginRendering / EnableDocking (main DockSpace tab bar height).
    inline void PushMainDockTabStyle() noexcept
    {
        const ImGuiStyle& S = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
            ImVec2(S.FramePadding.x + 4.0f, S.FramePadding.y + kEditorTabExtraPadY));
    }

    inline void PopMainDockTabStyle() noexcept
    {
        ImGui::PopStyleVar(1);
    }

    // After ImGui::Begin on a main-dock editor root: set the hidden title-bar
    // offset to the *actual* TabBar height so MenuBar sits flush under the tabs.
    // Do not guess from kEditorTabExtraPadY — overshoot makes the toolbar chrome
    // look oversized / stretches centered items.
    inline void ApplyMainDockTabTitleBarOffset() noexcept
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (!w || !w->DockIsActive || !w->DockNode)
            return;
        if (w->DockNode->IsHiddenTabBar() || w->DockNode->IsNoTabBar())
            return;
        ImGuiTabBar* TabBar = w->DockNode->TabBar;
        if (!TabBar)
            return;

        const float TabH = TabBar->BarRect.GetHeight();
        if (TabH <= 1.0f)
            return;
        const float Delta = TabH - w->TitleBarHeight;
        if (Delta <= 0.5f)
            return; // already matched (or tabs shorter)

        w->TitleBarHeight += Delta;
        w->DecoOuterSizeY1 += Delta;
        w->OuterRectClipped.Min.y += Delta;
        w->InnerRect.Min.y += Delta;
        w->InnerClipRect.Min.y += Delta;
        w->WorkRect.Min.y += Delta;
        w->ParentWorkRect.Min.y += Delta;
        w->ContentRegionRect.Min.y += Delta;
        w->DC.CursorStartPos.y += Delta;
        w->DC.CursorPos.y += Delta;
        w->DC.CursorPosPrevLine.y += Delta;
        w->DC.CursorMaxPos.y += Delta;
        w->DC.IdealMaxPos.y += Delta;
    }

    inline std::string ResolveResourceDisplayName(e10::library::guid LibraryGuid, xresource::full_guid Guid, const char* Fallback) noexcept
    {
        std::string Name;
        e10::g_LibMgr.getNodeInfo(LibraryGuid, Guid, [&](e10::library_db::info_node& Node)
        {
            Name = Node.m_Info.m_Name;
        });
        if (Name.empty() && Fallback && *Fallback) Name = Fallback;
        if (Name.empty()) Name = "<unnamed>";
        return Name;
    }

    inline std::string ResolveResourceDisplayName(xresource::full_guid Guid, const char* Fallback) noexcept
    {
        std::string Name;
        if (!Guid.empty())
        {
            auto FullGuid = xresource::g_Mgr.getFullGuid(Guid);
            e10::g_LibMgr.getNodeInfo(FullGuid, [&](e10::library_db::info_node& Node)
            {
                Name = Node.m_Info.m_Name;
            });
        }
        if (Name.empty() && Fallback && *Fallback) Name = Fallback;
        if (Name.empty()) Name = "<unnamed>";
        return Name;
    }

    // "DisplayName###StableId" — ### keeps ImGui ID fixed when the display name changes.
    inline void FormatEditorRootTabTitle(char* Buf, size_t BufSize, const char* DisplayName, const char* StableIdAfterHashHashHash) noexcept
    {
        std::snprintf(Buf, BufSize, "%s%s###%s", kEditorTabIconSpacer, DisplayName ? DisplayName : "<unnamed>", StableIdAfterHashHashHash);
    }

    // Call every frame after Begin (even when Begin returns false / tab not selected).
    inline void DrawEditorRootTabIcon(xgpu::device* pDevice, xresource::type_guid TypeGuid, int IconIndex = 0) noexcept
    {
        if (!pDevice) return;
        e10::EnsureIconAtlasTexture(e10::g_LibMgr.m_AssetPluginsDB, *pDevice);
        auto Icon = e10::g_LibMgr.m_AssetPluginsDB.getIconRef(TypeGuid, IconIndex);
        if (!Icon.isValid()) return;

        ImGuiWindow* Window = ImGui::GetCurrentWindow();
        if (!Window || !Window->DockNode) return;
        ImGuiTabBar* TabBar = Window->DockNode->TabBar;
        if (!TabBar) return;

        ImGuiTabItem* Tab = ImGui::TabBarFindTabByID(TabBar, Window->TabId);
        if (!Tab) return;

        const float PadX = TabBar->FramePadding.x;
        const float TabH = TabBar->BarRect.GetHeight();
        float IconSz = kEditorTabIconPx;
        if (IconSz + 2.0f > TabH)
            IconSz = ImMax(10.0f, TabH - 4.0f);
        const float Y = TabBar->BarRect.Min.y + (TabH - IconSz) * 0.5f;
        const ImVec2 Min(TabBar->BarRect.Min.x + Tab->Offset + PadX, Y);
        const ImVec2 Max(Min.x + IconSz, Min.y + IconSz);
        ImGui::GetForegroundDrawList()->AddImage(
            (ImTextureRef)(void*)Icon.m_pTexture, Min, Max,
            ImVec2(Icon.m_U0, Icon.m_V0), ImVec2(Icon.m_U1, Icon.m_V1));
    }
}

#endif // XEDITOR_RESOURCE_TAB_H
