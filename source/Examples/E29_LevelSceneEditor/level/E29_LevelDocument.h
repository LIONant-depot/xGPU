#ifndef E29_LEVEL_DOCUMENT_H
#define E29_LEVEL_DOCUMENT_H
#pragma once

// Level resource as xeditor::IDocument. Session (not the document) owns undo.
// Guid/display name track State.m_CurrentLevel; Save wires to SaveEverything.

#include "dependencies/xeditor/include/xeditor/types.h"
#include "dependencies/xeditor/include/xeditor/registry.h"
#include "dependencies/xeditor/include/xeditor/session.h"
#include "dependencies/xeditor/include/xeditor/host.h"

namespace e29
{


    // Forward (defined below with Ensure/IsLevelWritable).
    inline void ReleaseLevelEditAccess(xeditor::host& Host, xeditor::session& Sess, editor_state& State) noexcept;

    struct LevelDocument final : xeditor::IDocument
    {
        editor_context* m_pEd = nullptr;    // the editor this document belongs to; its undo holds the dirty watermark

        void Bind(editor_context& Ed) noexcept { m_pEd = &Ed; }

        xresource::full_guid CurrentGuid() const noexcept
        {
            if (!m_pEd || m_pEd->State().m_CurrentLevel.empty()) return {};
            return xresource::full_guid{ m_pEd->State().m_CurrentLevel.m_Instance, xecs::level::type_guid_v };
        }

        xresource::full_guid getGuid() const noexcept override { return CurrentGuid(); }

        std::string getDisplayName() const noexcept override
        {
            if (!m_pEd || m_pEd->State().m_CurrentLevel.empty()) return {};
            std::string Name;
            e10::RemapGUIDToString(Name, CurrentGuid());
            return Name.empty() ? std::string("Level") : Name;
        }

        bool Load() noexcept override
        {
            return m_pEd && !m_pEd->State().m_CurrentLevel.empty();
        }

        std::string Save() noexcept override
        {
            if (!m_pEd) return "LevelDocument: not bound";
            auto& State = m_pEd->State();
            if (State.m_CurrentLevel.empty() && State.m_OpenScenes.empty())
                return "LevelDocument: nothing open";
            SaveEverything(m_pEd->World(), State);
            MarkDocumentClean(State, m_pEd->m_Undo);
            // Save restores just-loaded: drop write locks so peers can edit again.
            if (auto* pHost = xeditor::host::current())
            {
                for (auto& S : pHost->m_Sessions)
                {
                    if (!S || S->m_Document.get() != this) continue;
                    ReleaseLevelEditAccess(*pHost, *S, State);
                    break;
                }
            }
            return {};
        }

        bool isDirty() const noexcept override
        {
            return m_pEd && HasUnsavedDocumentChanges(m_pEd->State(), m_pEd->m_Undo);
        }
    };


    // Edit vs view (DESIGN 4.2):
    // - Clean / just-loaded: no locks; every session can view and may start editing.
    // - First mutation claims Level + selected scene(s) (no dialog); other open scenes stay free.
    // - Successful save (clean undo): release locks - back to just-loaded.
    // - Sync checks every frame: release when clean; acquire only via mutation gate.

    // True if this Level session may mutate (no other writer holds Level/scene locks).
    inline bool IsLevelWritable(xeditor::host* pHost, xeditor::session* pSess, const editor_state& State) noexcept
    {
        if (pHost == nullptr || pSess == nullptr) return true;
        if (!State.m_CurrentLevel.empty())
        {
            const xresource::full_guid LevelGuid{ State.m_CurrentLevel.m_Instance, xecs::level::type_guid_v };
            if (!pHost->can_write(LevelGuid, pSess)) return false;
        }
        for (const auto& SceneInst : State.m_OpenScenes)
        {
            const xresource::full_guid SceneGuid{ SceneInst.m_Instance, xecs::scene::type_guid_v };
            if (!pHost->can_write(SceneGuid, pSess)) return false;
        }
        return true;
    }

    // Claim Level + scenes the user is actually editing (selection), not every open scene.
    inline bool EnsureLevelEditAccess(xeditor::host& Host, xeditor::session& Sess, editor_state& State) noexcept
    {
        if (!State.m_CurrentLevel.empty())
        {
            const xresource::full_guid LevelGuid{ State.m_CurrentLevel.m_Instance, xecs::level::type_guid_v };
            if (!Host.try_acquire_write(LevelGuid, &Sess))
                return false;
        }
        auto TryScene = [&](const xecs::scene::guid& SceneInst) noexcept -> bool
        {
            if (SceneInst.empty()) return true;
            const xresource::full_guid SceneGuid{ SceneInst.m_Instance, xecs::scene::type_guid_v };
            return Host.try_acquire_write(SceneGuid, &Sess);
        };
        if (!TryScene(State.m_SelectedEntityScene)) return false;
        if (!TryScene(State.m_MultiSelectScene)) return false;
        return true;
    }

    inline void ReleaseLevelEditAccess(xeditor::host& Host, xeditor::session& Sess, editor_state& State) noexcept
    {
        if (!State.m_CurrentLevel.empty())
        {
            const xresource::full_guid LevelGuid{ State.m_CurrentLevel.m_Instance, xecs::level::type_guid_v };
            Host.release_write(LevelGuid, &Sess);
        }
        for (const auto& SceneInst : State.m_OpenScenes)
        {
            const xresource::full_guid SceneGuid{ SceneInst.m_Instance, xecs::scene::type_guid_v };
            Host.release_write(SceneGuid, &Sess);
        }
    }

    inline void RegisterLevelEditorDescriptor() noexcept
    {
        xeditor::editor_descriptor Desc;
        Desc.m_TypeGuid          = xecs::level::type_guid_v;
        Desc.m_TypeName          = "Level";
        Desc.m_bSupportsHeadless = true;
        Desc.m_CreateDocument    = [](xresource::full_guid) -> std::unique_ptr<xeditor::IDocument>
        {
            return std::make_unique<LevelDocument>();
        };
        xeditor::registry::Get().Register(std::move(Desc));
    }

    // Long-lived Level session: owned here when closed; in Host.m_Sessions while a Level is open. The session object
    // never moves (only its owning pointer does), so its undo is a stable address for the editor context to refer to.
    struct level_host_session
    {
        std::unique_ptr<xeditor::session> Owned;
        xeditor::session*                 pLive = nullptr; // Owned.get() or entry inside Host
        bool                              bInHost = false;

        level_host_session() noexcept
        {
            Owned = std::make_unique<xeditor::session>();
            Owned->m_Document = std::make_unique<LevelDocument>();
            if (auto Err = Owned->m_Undo.Init({}, false); !Err.empty())
                xeditor::NotifyError(std::format("E29: Level session xundo Init failed: {}", Err));
            pLive = Owned.get();
        }

        xundo::system& Undo() noexcept { return pLive->m_Undo; }

        void Bind(editor_context& Ed) noexcept { static_cast<LevelDocument*>(pLive->m_Document.get())->Bind(Ed); }

        void Sync(xeditor::host& Host, editor_state& State) noexcept
        {
            const bool bWant = !State.m_CurrentLevel.empty();

            if (bWant && !bInHost)
            {
                Host.m_Sessions.push_back(std::move(Owned));
                pLive   = Host.m_Sessions.back().get();
                bInHost = true;
            }
            else if (!bWant && bInHost)
            {
                if (pLive) ReleaseLevelEditAccess(Host, *pLive, State);
                for (auto It = Host.m_Sessions.begin(); It != Host.m_Sessions.end(); ++It)
                {
                    if (It->get() != pLive) continue;
                    Owned   = std::move(*It);
                    Host.m_Sessions.erase(It);
                    pLive   = Owned.get();
                    bInHost = false;
                    break;
                }
            }
        
            // Every frame (DESIGN 4.2): clean => unlocked (same as just-loaded). Dirty locks
            // are claimed only by TryGateLevelMutation on first edit - never by Sync.
            if (bWant && pLive && !HasUnsavedDocumentChanges(State, pLive->m_Undo))
            {
                ReleaseLevelEditAccess(Host, *pLive, State);
            }
        }
    };

    // Claims the Level and the selected scenes for the session before its first edit (DESIGN 4.2); the host runs it
    // before every edit command.
    inline bool TryGateLevelMutation(xundo::system& System) noexcept
    {
        auto* pHost = xeditor::host::current();
        auto* pEd   = FindEditorContext();
        if (pHost == nullptr || pEd == nullptr || &System != &pEd->m_Undo) return true;
        for (auto& S : pHost->m_Sessions)
            if (S && &S->undo() == &System) return EnsureLevelEditAccess(*pHost, *S, pEd->State());
        return true;
    }
}

#endif // E29_LEVEL_DOCUMENT_H
