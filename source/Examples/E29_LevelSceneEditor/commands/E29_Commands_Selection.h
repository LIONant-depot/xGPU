#ifndef E29_COMMANDS_SELECTION_H
#define E29_COMMANDS_SELECTION_H
#pragma once

// Selection commands - phase 1 of [[e29_command_undo_system_plan]] (memory), the simplest slice:
// selection has no world-mutating side effect, so these are the safest place to prove the whole
// xundo wiring (system, undo_file snapshot/restore, Ctrl+Z/Y, real UI call sites routed through
// Execute() instead of direct State mutation) before tackling property editing/component add-remove/
// entity create-delete, which all need real data snapshots on top of this same shape.
//
// Split into TWO edit commands (Select, ToggleMultiSelect) rather than E27_NodeOS's single Select
// covering everything - E29's selection model is richer than NodeOS's (a separate PRIMARY selection
// driving the Properties panel, plus an independent ctrl-click multi-select set used only by
// "Make Prefab" - see editor_state's own comment) and the two existing UI behaviors already
// deliberately touch different subsets: a plain click sets primary AND resets multi-select to just
// that entity; a ctrl-click only ever toggles multi-select membership, primary never moves. Forcing
// both into one command with a pile of optional flags (E27's own approach, appropriate for ITS
// flatter selection model) would obscure that distinction instead of expressing it.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    // ParseSceneGuid/FormatSceneGuid moved to E29_CommandContext.h - shared with
    // E29_Commands_PropertyEdit.h, not selection-specific.

    //================================================================================================
    // Select - the "plain click" behavior: sets the primary selection (drives the Entity Properties
    // panel) AND resets the multi-select set to just this one entity, matching every existing plain-
    // click call site (see RenderLevelTreePanel's own click handler, which this replaces).
    //================================================================================================
    struct select_cmd : xundo::command_base
    {
        select_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "Select", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Sets the primary selection and resets multi-select to just this entity (a plain click). Usage: Select -Scene hexguid -Id hexid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits", true, 1);
            m_hId    = m_Parser.addOption("Id",    "Entity permanent_id, 8 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg)) return "Select: bad arguments";

            auto& Ctx        = get<e29_command_context>();
            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id         = ParseEntityId(std::get<std::string>(IdArg));

            if (!e29::g_pGameMgr) return "Select: no live GameMgr";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return "Select: scene not open";
            auto It = pScene->m_LocalToRuntime.find(Id);
            if (It == pScene->m_LocalToRuntime.end()) return "Select: entity not found in scene";

            auto& S = Ctx.m_State;
            S.m_MultiSelectedEntityIds = { Id };
            S.m_MultiSelectOrder       = { Id };
            S.m_MultiSelectScene       = SceneGuid;
            S.m_SelectedEntityId       = Id;
            S.m_SelectedEntity         = It->second;
            S.m_SelectedEntityScene    = SceneGuid;
            S.m_bEntityInspectorDirty  = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<e29_command_context>(), File); }
        void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<e29_command_context>(), File); }

        xcmdline::parser::handle m_hScene, m_hId;
    };

    //================================================================================================
    // ToggleMultiSelect - the "ctrl-click" behavior: adds or removes exactly one entity from the
    // multi-select set, WITHOUT touching the primary selection - matches the existing ctrl-click call
    // site exactly (a plain click elsewhere already clears multi-select via Select above, so this
    // command has no separate "clear" concern of its own to worry about).
    //================================================================================================
    struct toggle_multi_select_cmd : xundo::command_base
    {
        toggle_multi_select_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ToggleMultiSelect", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Adds/removes one entity from the multi-select set without touching the primary selection (a ctrl-click). Usage: ToggleMultiSelect -Scene hexguid -Id hexid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits", true, 1);
            m_hId    = m_Parser.addOption("Id",    "Entity permanent_id, 8 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg)) return "ToggleMultiSelect: bad arguments";

            auto& Ctx        = get<e29_command_context>();
            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id         = ParseEntityId(std::get<std::string>(IdArg));
            auto& S = Ctx.m_State;

            // A ctrl-click starting a NEW multi-select scope (different scene, or nothing selected
            // yet) starts fresh with just this one entity - matches the existing inline handler this
            // replaces exactly.
            if (S.m_MultiSelectScene != SceneGuid)
            {
                S.m_MultiSelectedEntityIds.clear();
                S.m_MultiSelectOrder.clear();
                S.m_MultiSelectScene = SceneGuid;
            }

            if (S.m_MultiSelectedEntityIds.contains(Id))
            {
                S.m_MultiSelectedEntityIds.erase(Id);
                std::erase(S.m_MultiSelectOrder, Id);
            }
            else
            {
                S.m_MultiSelectedEntityIds.insert(Id);
                S.m_MultiSelectOrder.push_back(Id);
            }
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<e29_command_context>(), File); }
        void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<e29_command_context>(), File); }

        xcmdline::parser::handle m_hScene, m_hId;
    };

    //================================================================================================
    // ClearSelection - a dedicated, self-describing command for "select nothing" - same reasoning as
    // E27_NodeOS's own clear_selection_cmd: a bare command name in the undo history/console log reads
    // as exactly what it did, rather than a reader having to infer "Select with no matching entity"
    // meant deselect.
    //================================================================================================
    struct clear_selection_cmd : xundo::command_base
    {
        clear_selection_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ClearSelection", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Deselects everything (primary selection and multi-select). Usage: ClearSelection"; }
        void RegisterArguments() noexcept override {} // takes no arguments at all

        std::string Redo() noexcept override
        {
            auto& S = get<e29_command_context>().m_State;
            S.m_SelectedEntityId       = xecs::scene::invalid_permanent_id_v;
            S.m_SelectedEntity         = {};
            S.m_SelectedEntityScene    = {};
            S.m_MultiSelectedEntityIds.clear();
            S.m_MultiSelectOrder.clear();
            S.m_MultiSelectScene       = {};
            S.m_bEntityInspectorDirty  = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<e29_command_context>(), File); }
        void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<e29_command_context>(), File); }
    };
}

#endif // E29_COMMANDS_SELECTION_H
