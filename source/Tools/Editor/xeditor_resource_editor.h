#ifndef XEDITOR_RESOURCE_EDITOR_H
#define XEDITOR_RESOURCE_EDITOR_H
#pragma once

// A resource editor opened from the asset browser: one open resource, its undo system and its window. A resource type
// registers a factory; the host keeps the open editors (open_resource_editors), renders them and lists them in
// xeditor::host so the command console reaches each one as Name\Command.
#include "dependencies/xeditor/include/xeditor/host.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_AssetMgr.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace xeditor
{
    struct resource_editor
    {
        virtual                     ~resource_editor()          = default;
        virtual IDocument&          getDocument()               noexcept = 0;
        virtual xundo::system&      getUndo()                   noexcept = 0;
        virtual void                Render()                    noexcept = 0;
        virtual bool                isLoaded()            const noexcept { return true; }       // the resource's data could be read

        void Focus() noexcept { m_bOpen = true; m_bRequestFocus = true; }

        bool m_bOpen         = true;         // the window's close button clears it; the host then drops the editor
        bool m_bRequestFocus = false;
    };

    using resource_editor_factory = std::function<std::unique_ptr<resource_editor>(xresource::full_guid, e10::library::guid, xgpu::device*)>;

    inline std::unordered_map<xresource::type_guid, resource_editor_factory>& ResourceEditorFactories() noexcept
    {
        static std::unordered_map<xresource::type_guid, resource_editor_factory> s_Map;
        return s_Map;
    }

    // A resource type's editor header declares one of these at namespace scope: `inline const xeditor::auto_register_resource_editor g_Registration{ type, factory };`
    struct auto_register_resource_editor
    {
        auto_register_resource_editor(xresource::type_guid Type, resource_editor_factory Factory) noexcept { ResourceEditorFactories()[Type] = std::move(Factory); }
    };

    // The editors a host has open. Provided to the host as a service so commands and the asset browser reach it.
    struct open_resource_editors
    {
        std::vector<std::unique_ptr<resource_editor>> m_List;
        xgpu::device*                                 m_pDevice = nullptr;     // handed to every editor for its GPU work (null: no preview)

        resource_editor* Find(xresource::full_guid Guid) noexcept
        {
            for (auto& E : m_List) if (E && E->getDocument().getGuid() == Guid) return E.get();
            return nullptr;
        }

        // The library a resource lives in (empty when no open library has it).
        static e10::library::guid FindLibraryOf(xresource::full_guid Guid) noexcept
        {
            for (auto& Lib : e10::g_LibMgr.m_mLibraryDB)
            {
                bool bFound = false;
                Lib.second->m_InfoByTypeDataBase.FindAsReadOnly(Guid.m_Type, [&](const std::unique_ptr<e10::library_db::info_db>& InfoDB)
                {
                    InfoDB->m_InfoDataBase.FindAsReadOnly(Guid.m_Instance, [&](const e10::library_db::info_node&) { bFound = true; });
                });
                if (bFound) return Lib.first;
            }
            return {};
        }

        static bool HasEditorFor(xresource::type_guid Type) noexcept { return ResourceEditorFactories().contains(Type); }

        // Focuses the editor already open for this resource, or opens one. Null when the type has no editor.
        resource_editor* Open(xresource::full_guid Guid, e10::library::guid LibraryGuid) noexcept
        {
            if (auto* pOpen = Find(Guid)) { pOpen->Focus(); return pOpen; }
            auto It = ResourceEditorFactories().find(Guid.m_Type);
            if (It == ResourceEditorFactories().end()) return nullptr;
            m_List.push_back(It->second(Guid, LibraryGuid, m_pDevice));
            return m_List.back().get();
        }

        // Lists the open editors in the host's sessions (document and undo borrowed) so Name\Command and `list` reach them. Only open
        // editors are listed: a closed one is dropped by RenderAll next frame and must not stay borrowed.
        void SyncToHost(host& Host) noexcept
        {
            std::erase_if(Host.m_Sessions, [&](std::unique_ptr<session>& U) noexcept
            {
                if (!U || !U->is_borrowed()) return false;
                for (auto& E : m_List)
                    if (E && E->m_bOpen && U->m_pBorrowedUndo == &E->getUndo()) return false;
                return true;
            });

            for (auto& E : m_List)
            {
                if (!E || !E->m_bOpen) continue;
                session* pHit = nullptr;
                for (auto& U : Host.m_Sessions)
                    if (U && U->m_pBorrowedUndo == &E->getUndo()) { pHit = U.get(); break; }
                if (!pHit)
                {
                    Host.m_Sessions.push_back(std::make_unique<session>());
                    pHit = Host.m_Sessions.back().get();
                }
                pHit->m_pBorrowedDocument = &E->getDocument();
                pHit->m_pBorrowedUndo     = &E->getUndo();
            }
        }

        // Once a frame: drops the closed editors and renders the rest.
        void RenderAll() noexcept
        {
            if (auto* pHost = host::current()) SyncToHost(*pHost);
            std::erase_if(m_List, [](auto& E) noexcept { return !E || !E->m_bOpen; });
            for (auto& E : m_List) E->Render();
        }
    };
}

#endif // XEDITOR_RESOURCE_EDITOR_H
