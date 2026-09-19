#ifndef XEDITOR_RESOURCE_TAB_H
#define XEDITOR_RESOURCE_TAB_H
#pragma once

// Main editor root tabs: resource-type icon + asset name, slightly taller than nested tabs.
//
// Main DockSpace uses AutoHideTabBar — with a single root editor the tab bar is hidden and
// layout is normal. When a second editor (Texture) docks in, the tab bar appears. Its height
// comes from FramePadding during BeginRendering's DockSpace. BeginDocked also needs that same
// tall FramePadding so TitleBarHeight matches; MenuBarHeight must then be restored to the
// normal theme size so the toolbar is not oversized or clipped.
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

    // Around BeginRendering (DockSpace tab height) AND around root ImGui::Begin (TitleBarHeight).
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

    // Call immediately after PopMainDockTabStyle following Begin: keep TitleBarHeight from
    // the tall Begin (matches tall tabs when the bar is visible; 0 when AutoHide hides it)
    // but always restore MenuBarHeight to the normal theme size.
    inline void NormalizeMenuBarAfterTallBegin() noexcept
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (!w || !(w->Flags & ImGuiWindowFlags_MenuBar))
            return;

        ImGuiContext& g = *ImGui::GetCurrentContext();
        const float OldDecoY1 = w->DecoOuterSizeY1;

        // Style is already popped — FramePadding is the normal theme value.
        w->MenuBarHeight = w->DC.MenuBarOffset.y + g.FontSize + g.Style.FramePadding.y * 2.0f;
        w->DecoOuterSizeY1 = w->TitleBarHeight + w->MenuBarHeight;

        if (w->DockIsActive)
            w->OuterRectClipped.Min.y = w->Pos.y + w->TitleBarHeight;
        w->InnerRect.Min.y = w->Pos.y + w->DecoOuterSizeY1;
        w->InnerClipRect.Min.y = ImFloor(0.5f + w->InnerRect.Min.y);

        const float Delta = w->DecoOuterSizeY1 - OldDecoY1;
        w->WorkRect.Min.y += Delta;
        w->ParentWorkRect.Min.y += Delta;
        w->ContentRegionRect.Min.y += Delta;

        w->DC.CursorStartPos.y = (float)((double)w->Pos.y + w->WindowPadding.y - (double)w->Scroll.y + w->DecoOuterSizeY1);
        w->DC.CursorPos = w->DC.CursorStartPos;
        w->DC.CursorPosPrevLine = w->DC.CursorPos;
        w->DC.CursorMaxPos = w->DC.CursorStartPos;
        w->DC.IdealMaxPos = w->DC.CursorStartPos;
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

    inline void FormatEditorRootTabTitle(char* Buf, size_t BufSize, const char* DisplayName, const char* StableIdAfterHashHashHash) noexcept
    {
        std::snprintf(Buf, BufSize, "%s%s###%s", kEditorTabIconSpacer, DisplayName ? DisplayName : "<unnamed>", StableIdAfterHashHashHash);
    }

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
