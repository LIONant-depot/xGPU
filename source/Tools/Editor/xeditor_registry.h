#ifndef XEDITOR_REGISTRY_H
#define XEDITOR_REGISTRY_H
#pragma once

// Compiled-in registration table, type_guid -> editor_descriptor. A resource type's own header
// calls Register() once, at static-init time, from its own translation unit (mirrors this
// codebase's established "a type registers itself" idiom, e.g. E29_REGISTER_COMPONENT) - the
// editor genuinely belongs to the plugin/resource it represents, per direct user instruction,
// rather than being declared in a separate, unrelated system.
#include "xeditor_types.h"
#include <unordered_map>

namespace xeditor
{
    class registry
    {
    public:
        static registry& Get() noexcept
        {
            static registry Instance;
            return Instance;
        }

        void Register(editor_descriptor Descriptor) noexcept
        {
            const auto TypeGuid = Descriptor.m_TypeGuid;
            m_Map[TypeGuid] = std::move(Descriptor);
        }

        const editor_descriptor* Resolve(xresource::type_guid TypeGuid) const noexcept
        {
            auto It = m_Map.find(TypeGuid);
            return It == m_Map.end() ? nullptr : &It->second;
        }

    private:
        std::unordered_map<xresource::type_guid, editor_descriptor> m_Map;
    };

    // Convenience for a type's own static-init registration call, e.g.:
    //   static xeditor::auto_register s_Reg{ MakeMyEditorDescriptor() };
    struct auto_register
    {
        explicit auto_register(editor_descriptor Descriptor) noexcept
        {
            registry::Get().Register(std::move(Descriptor));
        }
    };
}

#endif // XEDITOR_REGISTRY_H
