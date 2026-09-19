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
    struct inspector_panel
    {
        xproperty::inspector            m_Inspector;
        xproperty::settings::context    m_Context{};

        explicit inspector_panel(const char* Name) noexcept : m_Inspector(Name)
        {
            // Same readability scalars E10 applies to its property dialogs.
            m_Inspector.m_Settings.m_ColorVScalar1 = 0.270f * 1.4f;
            m_Inspector.m_Settings.m_ColorVScalar2 = 0.305f * 1.4f;
            m_Inspector.m_Settings.m_ColorSScalar  = 0.26f  * 1.4f;
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
            m_Inspector.ShowEmbedded(m_Context);
        }
    };
}

#endif // XEDITOR_INSPECTOR_H
