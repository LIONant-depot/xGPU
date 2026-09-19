#ifndef XEDITOR_RESOURCE_TAB_H
#define XEDITOR_RESOURCE_TAB_H
#pragma once

// Main editor root tabs: show resource-type icon + asset name (not "Level Editor" /
// "Texture Editor"), with slightly taller tabs than the default theme.
#include "dependencies/imgui/imgui.h"
#include "dependencies/imgui/imgui_internal.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"
#include <cstdio>
#include <string>

namespace xeditor
{
    // Extra vertical padding for editor-root dock tabs (theme FramePadding.y is 1).
    inline constexpr float kEditorTabExtraPadY = 5.0f;
    inline constexpr float kEditorTabIconPx    = 18.0f;
    // Leading spaces reserved in the tab label so text clears the icon we paint over the tab.
    inline constexpr const char* kEditorTabIconSpacer = "     ";

    inline void PushEditorRootTabStyle() noexcept
    {
        const ImGuiStyle& S = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
            ImVec2(S.FramePadding.x + 4.0f, S.FramePadding.y + kEditorTabExtraPadY));
    }

    inline void PopEditorRootTabStyle() noexcept
    {
        ImGui::PopStyleVar(1);
    }

    // Resolve m_Info.m_Name for a resource; falls back to Fallback if missing.
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

    // Library-agnostic resolve (same path as e29::RemapGUIDToString).
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

    // Build "     AssetName###StableId" for ImGui::Begin. StableId must stay fixed so focus/dock persist.
    inline void FormatEditorRootTabTitle(char* Buf, size_t BufSize, const char* DisplayName, const char* StableIdAfterHashHashHash) noexcept
    {
        std::snprintf(Buf, BufSize, "%s%s###%s", kEditorTabIconSpacer, DisplayName ? DisplayName : "<unnamed>", StableIdAfterHashHashHash);
    }

    // Paint the plugin type icon onto this window's dock tab (call right after Begin returns true).
    inline void DrawEditorRootTabIcon(xgpu::device* pDevice, xresource::type_guid TypeGuid, int IconIndex = 0) noexcept
    {
        if (!pDevice) return;
        e10::EnsureIconAtlasTexture(e10::g_LibMgr.m_AssetPluginsDB, *pDevice);
        auto Icon = e10::g_LibMgr.m_AssetPluginsDB.getIconRef(TypeGuid, IconIndex);
        if (!Icon.isValid()) return;

        ImGuiWindow* Window = ImGui::GetCurrentWindow();
        if (!Window || !Window->DockNode || !Window->DockIsActive) return;
        ImGuiTabBar* TabBar = Window->DockNode->TabBar;
        if (!TabBar) return;

        ImGuiTabItem* Tab = ImGui::TabBarFindTabByID(TabBar, Window->TabId);
        if (!Tab) return;

        const float PadX = ImGui::GetStyle().FramePadding.x;
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