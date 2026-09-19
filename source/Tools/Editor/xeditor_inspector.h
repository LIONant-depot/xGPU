#ifndef XEDITOR_INSPECTOR_H
#define XEDITOR_INSPECTOR_H
#pragma once

// Shared ImGui property-inspector helper for the editor framework. Every open editor
// session owns its OWN instance (never a static shared across sessions). Thin wrapper
// around xproperty::inspector so Texture/Material/... editors do not re-copy the same
// AppendEntity / theme / ShowEmbedded boilerplate.
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"

namespace xeditor
{
    // Same inspector chrome the Level Editor Entity Properties panel uses.
    // Row tint off + tight Unity-like spacing - not E10's ColorVScalar readability multipliers.
    inline void ApplyLevelEditorInspectorTheme(xproperty::inspector& Inspector) noexcept
    {
        Inspector.m_Settings.m_bRenderBackgroundDepth = false;
        Inspector.m_Settings.m_bRenderLeftBackground  = false;
        Inspector.m_Settings.m_bRenderRightBackground = false;
        Inspector.m_Settings.m_FramePadding            = ImVec2(4.0f, 3.0f);
        Inspector.m_Settings.m_ItemSpacing             = ImVec2(1.0f, 1.0f);
        Inspector.m_Settings.m_TableFramePadding       = ImVec2(4.0f, 1.0f);
    }

    // Mirror Entity Properties: framed headers use ImGuiCol_Header, but E29_Theme sets that to
    // selection blue. Push the slightly-lighter grey (Inspector Titlebar #3E3E3E) while showing.
    inline void PushLevelEditorInspectorHeaderColors() noexcept
    {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0x3E / 255.0f, 0x3E / 255.0f, 0x3E / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
    }

    inline void PopLevelEditorInspectorHeaderColors() noexcept
    {
        ImGui::PopStyleColor(3);
    }

    struct inspector_panel
    {
        xproperty::inspector            m_Inspector;
        xproperty::settings::context    m_Context{};

        explicit inspector_panel(const char* Name) noexcept : m_Inspector(Name)
        {
            ApplyLevelEditorInspectorTheme(m_Inspector);
        }

        void Clear() noexcept
        {
            m_Inspector.clear();
        }

        void BindObject(const xproperty::type::object& PropObject, void* pBase, void* pUserData = nullptr) noexcept
        {
            m_Inspector.clear();
            m_Inspector.AppendEntity();
            m_Inspector.AppendEntityComponent(PropObject, pBase, pUserData);
        }

        void AppendComponent(const xproperty::type::object& PropObject, void* pBase, void* pUserData = nullptr) noexcept
        {
            m_Inspector.AppendEntityComponent(PropObject, pBase, pUserData);
        }

        template<typename TFn>
        void OnChange(TFn&& Fn) noexcept
        {
            m_Inspector.m_OnChangeEvent.m_Delegates.clear();
            m_Inspector.m_OnChangeEvent.Register<&std::decay_t<TFn>::operator()>(Fn);
        }

        void Show() noexcept
        {
            PushLevelEditorInspectorHeaderColors();
            m_Inspector.ShowEmbedded(m_Context);
            PopLevelEditorInspectorHeaderColors();
        }
    };
}

#endif // XEDITOR_INSPECTOR_H