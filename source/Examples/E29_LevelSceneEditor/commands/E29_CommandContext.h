#ifndef E29_COMMAND_CONTEXT_H
#define E29_COMMAND_CONTEXT_H
#pragma once

// e29_command_context - the "database" every E29 xundo command mutates, retrieved via
// command_base::get<e29_command_context>(). Direct port of E27_NodeOS's own
// node_os_command_context/BackupSelection/RestoreSelection (Editor/NodeOS_CommandContext.h) - see
// [[e29_command_undo_system_plan]] (memory) for the full phased plan this is step 1 of.
//
// Deliberately holds ONLY editor_state& - NOT a xecs::game_mgr::instance& the way E27_NodeOS's own
// context holds its node/link vectors directly. E29's pGameMgr is a unique_ptr that gets destroyed
// and reconstructed on every hot-reload (RebuildWorld, E29_GamePlugin.h) - a reference captured once
// at construction would dangle the moment the first reload happened, exactly the class of bug this
// whole session's earlier work (tree preservation, the m_ComponentInfoMap stale-pointer fix) was
// about. editor_state itself is never reconstructed this way (RebuildWorld mutates its fields, never
// replaces the object), so a reference to it is safe to hold - GameMgr access instead goes through
// e29::g_pGameMgr, the existing global E29_GamePlugin.h's own RebuildWorld already keeps correctly
// rebound after every reload (`g_pGameMgr = pGameMgr.get();`), rather than this context inventing a
// second, separately-maintained pointer to keep in sync.
//
// Meant to be included after editor_state (and e29::g_pGameMgr/e29::Debugger) are already defined -
// via the kit umbrella (E29_LevelSceneEditorKit.h), or a caller that already includes it - same
// convention every other extracted kit/plugin module in this project already follows, rather than
// self-including the umbrella here (this header is itself reached FROM WITHIN the umbrella, via
// kit/E29_Panel_LevelTree.h - a self-include would just bounce off E29_LevelSceneEditorKit.h's own
// include guard at that point, working by accident rather than by design).
#include "dependencies/xundo/source/xundo_system.h"

namespace e29::commands
{
    struct e29_command_context
    {
        editor_state& m_State;
    };

    // Shared by select_cmd/toggle_multi_select_cmd/clear_selection_cmd - all three snapshot/restore
    // the exact same fields. m_SelectedEntity (the live runtime handle) is deliberately NOT part of
    // the snapshot - it's a cache, always re-resolved fresh from {m_SelectedEntityScene,
    // m_SelectedEntityId} via the scene's own m_LocalToRuntime map on restore (through
    // e29::g_pGameMgr - see this file's own top comment for why), matching this project's own
    // standing rule of never carrying a raw runtime handle across a boundary where the world could
    // have changed underneath it - an Undo/Redo step is exactly such a boundary, potentially long
    // after the entity in question was last touched.
    inline void BackupSelection(e29_command_context& Ctx, xundo::undo_file& File) noexcept
    {
        auto& S = Ctx.m_State;
        File.Write(S.m_SelectedEntityId);
        File.Write(S.m_SelectedEntityScene);
        File.Write(static_cast<std::uint32_t>(S.m_MultiSelectedEntityIds.size()));
        for (auto Id : S.m_MultiSelectedEntityIds) File.Write(Id);
        File.Write(static_cast<std::uint32_t>(S.m_MultiSelectOrder.size()));
        for (auto Id : S.m_MultiSelectOrder) File.Write(Id);
        File.Write(S.m_MultiSelectScene);
    }

    inline void RestoreSelection(e29_command_context& Ctx, xundo::undo_file& File) noexcept
    {
        auto& S = Ctx.m_State;

        File.Read(S.m_SelectedEntityId);
        File.Read(S.m_SelectedEntityScene);

        S.m_SelectedEntity = {};
        if (S.m_SelectedEntityId != xecs::scene::invalid_permanent_id_v && e29::g_pGameMgr)
        {
            if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(S.m_SelectedEntityScene))
            {
                if (auto It = pScene->m_LocalToRuntime.find(S.m_SelectedEntityId); It != pScene->m_LocalToRuntime.end())
                    S.m_SelectedEntity = It->second;
            }
        }

        std::uint32_t Count = 0;
        File.Read(Count);
        S.m_MultiSelectedEntityIds.clear();
        for (std::uint32_t i = 0; i < Count; ++i)
        {
            xecs::scene::permanent_id Id{};
            File.Read(Id);
            S.m_MultiSelectedEntityIds.insert(Id);
        }

        File.Read(Count);
        S.m_MultiSelectOrder.clear();
        S.m_MultiSelectOrder.reserve(Count);
        for (std::uint32_t i = 0; i < Count; ++i)
        {
            xecs::scene::permanent_id Id{};
            File.Read(Id);
            S.m_MultiSelectOrder.push_back(Id);
        }

        File.Read(S.m_MultiSelectScene);
        S.m_bEntityInspectorDirty = true;
    }

    // Wraps xundo::system::Execute with logging - EVERY command, not just failures, so the log is a
    // genuine audit trail of everything that happened (the same log an external CLI-driven agent
    // would see once a later phase exposes it) - direct port of E27_NodeOS's own Run()
    // (Editor/NodeOS_CommandBuilders.h).
    inline void Run(xundo::system& System, const std::string& Cmd) noexcept
    {
        if (auto Err = System.Execute(Cmd); !Err.empty())
            Debugger(std::format("E29: command failed: '{}' ({})", Cmd, Err));
    }
}

#endif // E29_COMMAND_CONTEXT_H
