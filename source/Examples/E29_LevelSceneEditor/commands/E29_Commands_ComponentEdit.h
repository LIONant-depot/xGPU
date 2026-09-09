#ifndef E29_COMMANDS_COMPONENT_EDIT_H
#define E29_COMMANDS_COMPONENT_EDIT_H
#pragma once

// Add/Remove Component - phase 3 of [[e29_command_undo_system_plan]] (memory). Replaces
// E29_Panel_EntityProperties.h's own DIRECT calls to GameMgr.AddOrRemoveComponents (the "Add
// Component" combo and the component header's "[X]") with real xundo commands, same pattern as
// phase 2's SetProperty (commands/E29_Commands_PropertyEdit.h, included below for
// ResolvePropertyTarget/SetLivePropertyValue reuse).
//
// AddOrRemoveComponents ALWAYS migrates the entity to a new handle (a real archetype change, not an
// in-place mutation) - every call site in this codebase (E29_Panel_EntityProperties.h,
// E29_PrefabAuthoring.h, E29_PrefabOverrides.h) manually remaps the owning scene's
// m_LocalToRuntime/m_RuntimeToLocal maps afterward; MigrateEntityComponents (below) is that same
// remap sequence, shared so both commands' Redo()/Undo() don't each reimplement it slightly
// differently.
//
// Undo of ADD is simply "remove it again" - no data needs preserving, since by xundo's own stepping
// contract (xundo::system always Undoes/Redoes ONE STEP AT A TIME, even when jumping several entries
// - see xundo_system.h's own RewindTo/FastForwardTo) any property edits made to the newly-added
// component after this Add already got their OWN Undo() run first, strictly in reverse order, before
// this command's Undo() is ever reached - so the component is guaranteed back to its
// just-added/default-constructed state by the time this runs.
//
// Undo of REMOVE is the harder direction: xecs::pool::instance::MoveInFromPool (the migration's own
// implementation - dependencies/xECSV2/src/details/xecs_pool_inline.h) calls the removed component's
// destructor and the slot is simply gone - no snapshot exists anywhere in the ECS layer itself.
// BackupCurrenState (always called BEFORE Redo() - see set_property_cmd's own comment for the
// confirmed ordering) walks the doomed component's CURRENT live values via
// xproperty::sprop::collector (same API entity_inspector_bridge's own "Revert Override" action uses,
// E29_LevelSceneEditorKit.h) and records a {Path, TypeGuid, ValueStr} list - the same string-based
// snapshot shape SetProperty already uses, not a raw memcpy (this codebase's components can hold
// non-trivial members like std::string/std::vector, which a memcpy would corrupt - see
// [[xecs_scratch_buffer_construct_before_copy]] for a related, already-hit case of exactly that
// class of bug). Undo re-adds the component (fresh, default-constructed) then replays every
// snapshotted value back onto it via SetLivePropertyValue - restoring it exactly as it was,
// including whatever value a prefab override had it set to.
//
// Deliberately does NOT touch xecs::editor::prefab_instance::m_lComponents/m_PropertyOverrides or
// m_ComponentDiffs on either Add or Remove - confirmed by dedicated research before writing this
// file: today's existing (pre-command) Add/Remove Component UI code doesn't touch them either, and
// m_ComponentDiffs is fully recomputed from scratch at save time (RefreshPrefabInstanceOverlayRecord,
// xecs_reference_remap_inline.h) rather than incrementally maintained - unlike m_PropertyOverrides
// (phase 2's own bug class), so there's no equivalent "stale after remove" hazard for it. A removed
// component's own property-override entries CAN be left orphaned in m_lComponents by this - a
// PRE-EXISTING gap in the app today, not one this phase introduces; flagged to the user rather than
// silently changed, since fixing it would be new behavior beyond "wrap the existing action in a
// command."
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_PropertyEdit.h"

namespace e29::commands
{
    // Resolves {SceneGuid, Id} to a live entity handle WITHOUT requiring any particular component to
    // be present - unlike ResolvePropertyTarget (E29_Commands_PropertyEdit.h), which deliberately
    // fails when the named component is absent (correct for property editing, wrong here: Add's
    // whole point is operating on an entity that does NOT yet have the component).
    inline xecs::component::entity ResolveEntityHandle(xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id) noexcept
    {
        if (!e29::g_pGameMgr) return {};
        auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
        if (!pScene) return {};
        auto It = pScene->m_LocalToRuntime.find(Id);
        return It != pScene->m_LocalToRuntime.end() ? It->second : xecs::component::entity{};
    }

    // Runs GameMgr.AddOrRemoveComponents, then the caller-responsibility remap every existing call
    // site already does by hand (E29_Panel_EntityProperties.h lines 65-71/148-154): erase the old
    // runtime handle, re-point both scene maps at the new one, mark the entity dirty, and - only if
    // this entity happens to be the one currently selected in the UI - refresh State so the Entity
    // Properties panel picks up the migration in the same frame instead of showing a stale/dangling
    // handle. Returns the new entity handle (invalid if SceneGuid/Id didn't resolve).
    inline xecs::component::entity MigrateEntityComponents(xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id, std::span<const xecs::component::type::info* const> Add, std::span<const xecs::component::type::info* const> Sub) noexcept
    {
        if (!e29::g_pGameMgr) return {};
        auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
        if (!pScene) return {};
        auto It = pScene->m_LocalToRuntime.find(Id);
        if (It == pScene->m_LocalToRuntime.end()) return {};
        const auto OldEntity = It->second;

        const auto NewEntity = e29::g_pGameMgr->AddOrRemoveComponents(OldEntity, Add, Sub);

        pScene->m_RuntimeToLocal.erase(OldEntity.m_Value);
        pScene->m_LocalToRuntime[Id]                = NewEntity;
        pScene->m_RuntimeToLocal[NewEntity.m_Value] = Id;
        e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, Id);

        if (e29::g_pState && e29::g_pState->m_SelectedEntityId == Id && e29::g_pState->m_SelectedEntityScene == SceneGuid)
        {
            e29::g_pState->m_SelectedEntity        = NewEntity;
            e29::g_pState->m_bEntityInspectorDirty = true;
        }

        return NewEntity;
    }

    // Snapshots every property of Entity's Info component into {Path, TypeGuid, ValueStr} triples,
    // written length-prefixed to File - same string-based shape set_property_cmd already uses. Called
    // by remove_component_cmd::BackupCurrenState, BEFORE the component is actually removed.
    inline void SnapshotComponentProperties(xundo::undo_file& File, xecs::component::entity Entity, const xecs::component::type::info& Info) noexcept
    {
        if (!e29::g_pGameMgr) { File.Write(std::uint32_t{ 0 }); return; }
        auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
        if (!Details.m_pPool) { File.Write(std::uint32_t{ 0 }); return; }
        const auto iType = Details.m_pPool->findIndexComponentFromInfo(Info);
        if (iType < 0) { File.Write(std::uint32_t{ 0 }); return; }
        auto* pInstance = &Details.m_pPool->m_pComponent[iType][Details.m_PoolIndex.m_Value * Info.m_Size];

        struct snapshot_row { std::string m_Path; std::uint32_t m_TypeGuid; std::string m_ValueStr; };
        std::vector<snapshot_row> Rows;

        xproperty::settings::context Context;
        xproperty::sprop::collector(pInstance, *Info.m_pPropertyTable, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
        {
            std::array<char, 256> Buffer{};
            const auto Len = xproperty::settings::AnyToString(Buffer, Data);
            Rows.push_back({ pPropertyName, Data.m_pType ? Data.m_pType->m_GUID : 0u, std::string(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0) });
        });

        File.Write(static_cast<std::uint32_t>(Rows.size()));
        for (auto& Row : Rows)
        {
            WriteString(File, Row.m_Path);
            File.Write(Row.m_TypeGuid);
            WriteString(File, Row.m_ValueStr);
        }
    }

    // Counterpart to SnapshotComponentProperties - reads the {Path, TypeGuid, ValueStr} list back and
    // replays every value onto Target via SetLivePropertyValue (E29_Commands_PropertyEdit.h). Called
    // by remove_component_cmd::Undo AFTER re-adding the component (fresh, default-constructed).
    inline void RestoreComponentProperties(xundo::undo_file& File, const resolved_property_target& Target) noexcept
    {
        std::uint32_t Count = 0; File.Read(Count);
        for (std::uint32_t i = 0; i < Count; ++i)
        {
            const std::string Path = ReadString(File);
            std::uint32_t     TypeGuid = 0; File.Read(TypeGuid);
            const std::string ValueStr = ReadString(File);
            if (Target.m_pInfo) SetLivePropertyValue(Target, Path, TypeGuid, ValueStr);
        }
    }

    //================================================================================================
    // AddComponent - Redo adds Component to the entity named by Scene/Id; Undo removes it again (see
    // this file's own top comment for why Undo needs no snapshot here).
    //================================================================================================
    struct add_component_cmd : xundo::command_base
    {
        add_component_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "AddComponent", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Adds a component to an entity (undoable - removes it again on Undo). Usage: AddComponent -Scene hexguid -Id hexid -Component hex64";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene     = m_Parser.addOption("Scene",     "Scene guid, 16 hex digits",          true, 1);
            m_hId        = m_Parser.addOption("Id",        "Entity permanent_id, 8 hex digits",  true, 1);
            m_hComponent = m_Parser.addOption("Component", "Component type guid, 16 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(CompArg))
                return "AddComponent: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));
            const auto CompGuid  = std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);

            if (!e29::g_pGameMgr) return "AddComponent: no game world";
            auto* pInfo = e29::g_pGameMgr->m_ComponentMgr.findComponentTypeInfo(xecs::component::type::guid{ CompGuid });
            if (!pInfo) return "AddComponent: unknown component";

            std::array<const xecs::component::type::info*, 1> Add{ pInfo };
            if (!MigrateEntityComponents(SceneGuid, Id, Add, {}).isValid()) return "AddComponent: target not found";
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);

            const std::uint64_t Scene    = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id       = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint64_t Component = std::holds_alternative<xerr>(CompArg) ? 0 : std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);

            File.Write(Scene);
            File.Write(Id);
            File.Write(Component);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;     File.Read(Scene);
            std::uint32_t Id = 0;        File.Read(Id);
            std::uint64_t Component = 0; File.Read(Component);

            if (!e29::g_pGameMgr) return;
            auto* pInfo = e29::g_pGameMgr->m_ComponentMgr.findComponentTypeInfo(xecs::component::type::guid{ Component });
            if (!pInfo) return;

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            std::array<const xecs::component::type::info*, 1> Sub{ pInfo };
            MigrateEntityComponents(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), {}, Sub);
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hComponent;
    };

    //================================================================================================
    // RemoveComponent - Redo removes Component from the entity named by Scene/Id (after
    // BackupCurrenState snapshots its current values); Undo re-adds it and replays the snapshot.
    //================================================================================================
    struct remove_component_cmd : xundo::command_base
    {
        remove_component_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RemoveComponent", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Removes a component from an entity (undoable - restores it with its prior values on Undo). Usage: RemoveComponent -Scene hexguid -Id hexid -Component hex64";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene     = m_Parser.addOption("Scene",     "Scene guid, 16 hex digits",          true, 1);
            m_hId        = m_Parser.addOption("Id",        "Entity permanent_id, 8 hex digits",  true, 1);
            m_hComponent = m_Parser.addOption("Component", "Component type guid, 16 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(CompArg))
                return "RemoveComponent: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));
            const auto CompGuid  = std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);

            if (!e29::g_pGameMgr) return "RemoveComponent: no game world";
            auto* pInfo = e29::g_pGameMgr->m_ComponentMgr.findComponentTypeInfo(xecs::component::type::guid{ CompGuid });
            if (!pInfo) return "RemoveComponent: unknown component";

            std::array<const xecs::component::type::info*, 1> Sub{ pInfo };
            if (!MigrateEntityComponents(SceneGuid, Id, {}, Sub).isValid()) return "RemoveComponent: target not found";
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);

            const std::uint64_t Scene    = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id       = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint64_t Component = std::holds_alternative<xerr>(CompArg) ? 0 : std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);

            File.Write(Scene);
            File.Write(Id);
            File.Write(Component);

            // Snapshot the CURRENT (pre-removal) property values - this runs before Redo() actually
            // removes the component (see set_property_cmd's own comment for the confirmed ordering).
            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto Entity    = ResolveEntityHandle(SceneGuid, static_cast<xecs::scene::permanent_id>(Id));
            auto* pInfo = e29::g_pGameMgr ? e29::g_pGameMgr->m_ComponentMgr.findComponentTypeInfo(xecs::component::type::guid{ Component }) : nullptr;
            if (pInfo && Entity.isValid())
                SnapshotComponentProperties(File, Entity, *pInfo);
            else
                File.Write(std::uint32_t{ 0 });
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;     File.Read(Scene);
            std::uint32_t Id = 0;        File.Read(Id);
            std::uint64_t Component = 0; File.Read(Component);

            if (!e29::g_pGameMgr) return;
            auto* pInfo = e29::g_pGameMgr->m_ComponentMgr.findComponentTypeInfo(xecs::component::type::guid{ Component });
            if (!pInfo)
            {
                // Still have to drain File's own snapshot bytes (count == 0 if nothing was written)
                // so a later Read in the same undo_file record doesn't desync - matches
                // RestoreComponentProperties's own unconditional read of Count below.
                std::uint32_t Count = 0; File.Read(Count);
                return;
            }

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            std::array<const xecs::component::type::info*, 1> Add{ pInfo };
            MigrateEntityComponents(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Add, {});

            const auto Target = ResolvePropertyTarget(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Component);
            RestoreComponentProperties(File, Target);
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hComponent;
    };
}

#endif // E29_COMMANDS_COMPONENT_EDIT_H
