#ifndef E29_COMMANDS_WORKSPACE_H
#define E29_COMMANDS_WORKSPACE_H
#pragma once

// Workspace + discovery commands - added proactively, direct user invitation: "Feel free to add
// commands that will be helpful to you or any missing command." Two real gaps this session's own
// live CLI-driven testing actually hit:
//
//   Undo/Redo were never exposed at all - only Ctrl+Z/Y in the UI. An AI/script-driven session had no
//   way to correct a mistake without touching a keyboard. Save was in the same boat - the only way to
//   persist changes to disk was File>Save or Ctrl+S.
//
//   DescribeEntity/ListComponentTypes close the discovery gap SetProperty/AddComponent always had:
//   both need a component's exact 16-hex-digit guid, and SetProperty ALSO needs a property's exact
//   path and TypeGuid - none of which were discoverable through ANY command. This session's own
//   testing had to read a raw .entity file off disk by hand to get this information even once - the
//   whole point of phase 5+ was to never need that.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    //================================================================================================
    // Undo/Redo - thin wrappers around xundo::system::Undo()/Redo() (the SAME System this command is
    // itself registered on, via query_command_base's own m_System). Deliberately Query, not Edit -
    // undoing/redoing must never itself become a new undo-able step (Execute() logs every successful
    // Edit command to history; wrapping Undo() in an Edit command would record "undo the undo" as its
    // own history entry, which makes no sense). GetUndoIndex() is the only public signal available to
    // tell "nothing happened" apart from "something did" - compared before/after rather than trusting
    // Undo()/Redo()'s own void-ish `system&` return.
    //================================================================================================
    struct undo_query_cmd : xundo::query_command_base
    {
        undo_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Undo", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Undoes the last step. Usage: Undo"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            const auto Before = m_System.GetUndoIndex();
            m_System.Undo();
            return m_System.GetUndoIndex() == Before ? "Nothing to undo" : "Undone";
        }
    };

    struct redo_query_cmd : xundo::query_command_base
    {
        redo_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Redo", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Redoes the next step. Usage: Redo"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            const auto Before = m_System.GetUndoIndex();
            m_System.Redo();
            return m_System.GetUndoIndex() == Before ? "Nothing to redo" : "Redone";
        }
    };

    //================================================================================================
    // Save - wraps e29::SaveEverything (E29_LevelSceneEditorKit.h), the SAME single "Save" action
    // File>Save/Ctrl+S already trigger. Gated on !State.isPlaying(), matching the existing Save-gating
    // rule exactly ([[e29_save_gating_and_persist_mode_unify]] memory) - Play already blocks Save in
    // the UI, and there's no reason a command should be allowed to bypass that.
    //================================================================================================
    struct save_query_cmd : xundo::query_command_base
    {
        save_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Save", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Saves the currently open Level/Scenes to disk (blocked while Play/Paused). Usage: Save"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            if (!e29::g_pGameMgr) return "Save: no game world";
            auto& State = get<e29_command_context>().m_State;
            if (State.isPlaying()) return "Save: blocked while Play/Paused";
            e29::SaveEverything(*e29::g_pGameMgr, State);
            return "Saved";
        }
    };

    //================================================================================================
    // DescribeEntity - every component on an entity, with every property's path/current value/type
    // guid - everything needed to build a working SetProperty (or confirm what AddComponent/
    // RemoveComponent already did). Reuses the exact xproperty::sprop::collector pattern already
    // proven safe with `noexcept` in phase 2/3/4's own component-snapshot code (unlike
    // [[xgpu_xcontainer_noexcept_lambda_trait_trap]]'s own FindAsReadOnly callback, this one's fine).
    // Property paths are shown RAW, not Base64 - trivial for a human or AI to encode when building the
    // actual SetProperty call, and far more readable here than a wall of base64 would be.
    //================================================================================================
    struct describe_entity_query_cmd : xundo::query_command_base
    {
        describe_entity_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "DescribeEntity", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every component on an entity, with each property's path/value/TypeGuid - everything SetProperty needs. Usage: DescribeEntity -Scene hexguid -Id hexid"; }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits",      true, 1);
            m_hId    = m_Parser.addOption("Id",    "Entity permanent_id, 8 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg)) return "DescribeEntity: bad arguments";
            if (!e29::g_pGameMgr) return "DescribeEntity: no game world";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return std::format("DescribeEntity: Scene {} is not open", FormatSceneGuid(SceneGuid));
            auto It = pScene->m_LocalToRuntime.find(Id);
            if (It == pScene->m_LocalToRuntime.end()) return "DescribeEntity: entity not found";
            auto Entity = It->second;

            auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
            if (!Details.m_pPool) return "DescribeEntity: entity has no components";
            auto DataSpan = Details.m_pPool->m_pArchetype->getDataComponentInfos();

            std::string Out;
            for (auto pInfo : DataSpan)
            {
                // Internal bookkeeping components (entity self-identity, parent/children, prefab
                // plumbing, etc.) are not addable/settable via AddComponent/SetProperty - same
                // filter ListComponentTypes uses. Skipping them here isn't just cosmetic: the
                // entity's own self-identity component is exactly the kind of type
                // FormatPropertyValue below has to special-case for everyone else - no need to walk
                // it here at all since it's never addressable via SetProperty anyway.
                if (e29::IsInternalComponent(pInfo)) continue;
                Out += std::format("[{:016X}] {}\n", pInfo->m_Guid.m_Value, pInfo->m_pName);
                if (!pInfo->m_pPropertyTable) continue;
                const auto iType = Details.m_pPool->findIndexComponentFromInfo(*pInfo);
                if (iType < 0) continue;
                auto* pData = &Details.m_pPool->m_pComponent[iType][Details.m_PoolIndex.m_Value * pInfo->m_Size];

                xproperty::settings::context Context;
                xproperty::sprop::collector(pData, *pInfo->m_pPropertyTable, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    std::array<char, 256> Buffer{};
                    const auto Len = FormatPropertyValue(Buffer, Data);
                    const std::string ValueStr(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
                    const std::uint32_t TypeGuid = Data.m_pType ? Data.m_pType->m_GUID : 0;
                    Out += std::format("    {} = {}  (TypeGuid {:08X})\n", pPropertyName, ValueStr, TypeGuid);
                });
            }
            return Out;
        }

        xcmdline::parser::handle m_hScene, m_hId;
    };

    //================================================================================================
    // ListComponentTypes - every registered DATA component type (guid + name) that AddComponent can
    // actually add - same iteration + IsInternalComponent filter the Entity Properties panel's own
    // "Add Component" combo already uses (E29_Panel_EntityProperties.h), so this lists exactly what
    // that UI would offer, not a superset that would fail if handed to AddComponent.
    //================================================================================================
    struct list_component_types_query_cmd : xundo::query_command_base
    {
        list_component_types_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListComponentTypes", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every addable component type (guid + name). Usage: ListComponentTypes"; }
        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override
        {
            std::string Out;
            for (auto& Pair : xecs::component::mgr::s_Registry.m_ComponentInfoMap)
            {
                auto* pInfo = Pair.second;
                if (pInfo->m_TypeID != xecs::component::type::id::DATA) continue;
                if (e29::IsInternalComponent(pInfo)) continue;
                Out += std::format("{:016X}  {}\n", pInfo->m_Guid.m_Value, pInfo->m_pName);
            }
            return Out;
        }
    };
}

#endif // E29_COMMANDS_WORKSPACE_H
