#ifndef E29_COMMANDS_APPLY_OVERRIDES_H
#define E29_COMMANDS_APPLY_OVERRIDES_H
#pragma once

// ApplyOverrides - Unity-style "Apply Overrides to Prefab": pushes every recorded override on one
// prefab_instance up into the source Prefab asset, saves the prefab, and clears the instance's
// m_lComponents bookkeeping. The previous UI path (kit/E29_Panel_EntityProperties.h) called
// ApplyInstanceOverridesToPrefab directly with no undo. BackupCurrenState snapshots (1) the full
// override bookkeeping that will be cleared and (2) each affected Prefab property's BEFORE value so
// Undo can put the Prefab asset AND the instance's override list back.

#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_PropertyEdit.h"

namespace e29::commands
{
    inline void WriteMemberPath(xundo::undo_file& File, const std::vector<std::uint32_t>& Path) noexcept
    {
        File.Write(static_cast<std::uint32_t>(Path.size()));
        for (auto P : Path) File.Write(P);
    }

    inline std::vector<std::uint32_t> ReadMemberPath(xundo::undo_file& File) noexcept
    {
        std::uint32_t Count = 0; File.Read(Count);
        std::vector<std::uint32_t> Path(Count);
        for (auto& P : Path) File.Read(P);
        return Path;
    }

    // Full PI.m_lComponents dump - same length-prefixed shape SnapshotComponentOverrideEntry uses,
    // but for EVERY component-override entry on the instance (Apply clears the whole list).
    inline void SnapshotAllOverrideBookkeeping(xundo::undo_file& File, const xecs::editor::prefab_instance& PI) noexcept
    {
        File.Write(static_cast<std::uint32_t>(PI.m_lComponents.size()));
        for (auto& C : PI.m_lComponents)
        {
            File.Write(C.m_ComponentTypeGuid);
            WriteMemberPath(File, C.m_MemberPath);
            File.Write(static_cast<std::uint32_t>(C.m_PropertyOverrides.size()));
            for (auto& O : C.m_PropertyOverrides)
            {
                WriteString(File, O.m_PropertyName);
                WriteString(File, O.m_PropertyValueAsString);
            }
        }
        File.Write(static_cast<std::uint32_t>(PI.m_HierarchyDiffs.size()));
        for (auto& H : PI.m_HierarchyDiffs)
        {
            WriteMemberPath(File, H.m_MemberPath);
            File.Write(H.m_bAdded);
        }
    }

    inline void RestoreAllOverrideBookkeeping(xundo::undo_file& File, xecs::editor::prefab_instance& PI) noexcept
    {
        PI.m_lComponents.clear();
        std::uint32_t CompCount = 0; File.Read(CompCount);
        PI.m_lComponents.reserve(CompCount);
        for (std::uint32_t i = 0; i < CompCount; ++i)
        {
            xecs::editor::prefab_component_override Comp{};
            File.Read(Comp.m_ComponentTypeGuid);
            Comp.m_MemberPath = ReadMemberPath(File);
            std::uint32_t OverrideCount = 0; File.Read(OverrideCount);
            Comp.m_PropertyOverrides.reserve(OverrideCount);
            for (std::uint32_t j = 0; j < OverrideCount; ++j)
            {
                xecs::editor::prefab_property_override Prop{};
                Prop.m_PropertyName          = ReadString(File);
                Prop.m_PropertyValueAsString = ReadString(File);
                Comp.m_PropertyOverrides.push_back(std::move(Prop));
            }
            PI.m_lComponents.push_back(std::move(Comp));
        }
        PI.m_HierarchyDiffs.clear();
        std::uint32_t HierCount = 0; File.Read(HierCount);
        PI.m_HierarchyDiffs.reserve(HierCount);
        for (std::uint32_t i = 0; i < HierCount; ++i)
        {
            xecs::editor::prefab_hierarchy_diff H{};
            H.m_MemberPath = ReadMemberPath(File);
            File.Read(H.m_bAdded);
            PI.m_HierarchyDiffs.push_back(std::move(H));
        }
    }

    // For each property Apply will overwrite on the Prefab, capture the Prefab's current value so
    // Undo can put it back. Mirrors ApplyInstanceOverridesToPrefab's own walk.
    inline void SnapshotPrefabBeforeValues(xundo::undo_file& File, xecs::game_mgr::instance& GameMgr, xecs::component::entity PIRootEntity, const xecs::editor::prefab_instance& PI) noexcept
    {
        struct row
        {
            std::uint64_t              m_ComponentTypeGuid = 0;
            std::vector<std::uint32_t> m_MemberPath;
            std::string                m_PropertyName;
            std::uint32_t              m_TypeGuid = 0;
            std::string                m_Before;
        };
        std::vector<row> Rows;

        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PI.m_PrefabInstance); !Err)
        {
            auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PI.m_PrefabInstance.m_Instance.m_Value);
            if (RootIt != GameMgr.m_PrefabMgr.m_PrefabList.end())
            {
                for (auto& CompOverride : PI.m_lComponents)
                {
                    auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo(xecs::component::type::guid{ CompOverride.m_ComponentTypeGuid });
                    if (pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr) continue;

                    const auto PrefabEntity = xecs::persist::details::ResolveMemberPath(GameMgr, RootIt->second, CompOverride.m_MemberPath);
                    if (PrefabEntity.isValid() == false) continue;

                    auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(PrefabEntity);
                    if (PDetails.m_pPool == nullptr) continue;
                    const auto iPrefType = PDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
                    if (iPrefType < 0) continue;
                    auto* pPrefData = &PDetails.m_pPool->m_pComponent[iPrefType][PDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size];

                    for (auto& PropOverride : CompOverride.m_PropertyOverrides)
                    {
                        xproperty::settings::context Context{};
                        xproperty::any               BeforeValue;
                        bool                         bFound = false;
                        xproperty::sprop::collector(pPrefData, *pOwnerInfo->m_pPropertyTable, Context, [&](const char* pPropertyName, xproperty::any&& Value, const xproperty::type::members&, bool, const void*) noexcept
                        {
                            if (PropOverride.m_PropertyName == pPropertyName) { BeforeValue = std::move(Value); bFound = true; }
                        });
                        if (!bFound) continue;

                        std::array<char, 256> Buffer{};
                        const auto Len = FormatPropertyValue(Buffer, BeforeValue);
                        row R;
                        R.m_ComponentTypeGuid = CompOverride.m_ComponentTypeGuid;
                        R.m_MemberPath        = CompOverride.m_MemberPath;
                        R.m_PropertyName      = PropOverride.m_PropertyName;
                        R.m_TypeGuid          = BeforeValue.m_pType ? BeforeValue.m_pType->m_GUID : 0;
                        R.m_Before.assign(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
                        Rows.push_back(std::move(R));
                    }
                }
            }
        }

        File.Write(static_cast<std::uint32_t>(Rows.size()));
        for (auto& R : Rows)
        {
            File.Write(R.m_ComponentTypeGuid);
            WriteMemberPath(File, R.m_MemberPath);
            WriteString(File, R.m_PropertyName);
            File.Write(R.m_TypeGuid);
            WriteString(File, R.m_Before);
        }
    }

    inline void RestorePrefabBeforeValues(xundo::undo_file& File, xecs::game_mgr::instance& GameMgr, const xecs::editor::prefab_instance& PI) noexcept
    {
        std::uint32_t Count = 0; File.Read(Count);

        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PI.m_PrefabInstance); Err)
        {
            // Still need to drain the file stream even if restore can't proceed.
            for (std::uint32_t i = 0; i < Count; ++i)
            {
                std::uint64_t Comp = 0; File.Read(Comp);
                (void)ReadMemberPath(File);
                (void)ReadString(File);
                std::uint32_t TypeGuid = 0; File.Read(TypeGuid);
                (void)ReadString(File);
            }
            return;
        }

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PI.m_PrefabInstance.m_Instance.m_Value);
        if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end())
        {
            for (std::uint32_t i = 0; i < Count; ++i)
            {
                std::uint64_t Comp = 0; File.Read(Comp);
                (void)ReadMemberPath(File);
                (void)ReadString(File);
                std::uint32_t TypeGuid = 0; File.Read(TypeGuid);
                (void)ReadString(File);
            }
            return;
        }

        for (std::uint32_t i = 0; i < Count; ++i)
        {
            std::uint64_t CompGuid = 0; File.Read(CompGuid);
            auto MemberPath = ReadMemberPath(File);
            auto PropertyName = ReadString(File);
            std::uint32_t TypeGuid = 0; File.Read(TypeGuid);
            auto Before = ReadString(File);

            auto* pOwnerInfo = xecs::component::mgr::findComponentTypeInfo(xecs::component::type::guid{ CompGuid });
            if (pOwnerInfo == nullptr || pOwnerInfo->m_pPropertyTable == nullptr) continue;

            const auto PrefabEntity = xecs::persist::details::ResolveMemberPath(GameMgr, RootIt->second, MemberPath);
            if (PrefabEntity.isValid() == false) continue;

            auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(PrefabEntity);
            if (PDetails.m_pPool == nullptr) continue;
            const auto iPrefType = PDetails.m_pPool->findIndexComponentFromInfo(*pOwnerInfo);
            if (iPrefType < 0) continue;
            auto* pPrefData = &PDetails.m_pPool->m_pComponent[iPrefType][PDetails.m_PoolIndex.m_Value * pOwnerInfo->m_Size];

            xproperty::any Value;
            std::string    ValueStrMutable = Before;
            xproperty::settings::StringToAny(Value, TypeGuid, std::span<char>(ValueStrMutable.data(), ValueStrMutable.size()));
            xproperty::settings::context Context{};
            std::string SetError;
            xproperty::sprop::setProperty(SetError, pPrefData, *pOwnerInfo->m_pPropertyTable, xproperty::sprop::container::prop{ PropertyName, Value }, Context);
        }
    }

    //================================================================================================
    struct apply_overrides_cmd : xundo::command_base
    {
        apply_overrides_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ApplyOverrides", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Applies every override on a prefab instance up into the Prefab asset (undoable - restores Prefab property values and the instance override list). Usage: ApplyOverrides -Scene hexguid -Id hexid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits", true, 1);
            m_hId    = m_Parser.addOption("Id",    "Prefab-instance root permanent_id, 8 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg))
                return "ApplyOverrides: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));
            if (!e29::g_pGameMgr) return "ApplyOverrides: no game world";

            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(Id)) return "ApplyOverrides: target not found";

            const auto RootEntity = pScene->m_LocalToRuntime.at(Id);
            if (auto Err = xecs::persist::details::ApplyInstanceOverridesToPrefab(*e29::g_pGameMgr, RootEntity); Err)
                return std::format("ApplyOverrides: {}", Err.getMessage());

            e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, Id);
            if (e29::g_pState) e29::g_pState->m_bEntityInspectorDirty = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            const std::uint64_t Scene = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id    = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            File.Write(Scene);
            File.Write(Id);

            if (!e29::g_pGameMgr)
            {
                File.Write(std::uint32_t{ 0 }); // m_lComponents
                File.Write(std::uint32_t{ 0 }); // m_HierarchyDiffs
                File.Write(std::uint32_t{ 0 }); // empty prefab-before list
                return;
            }

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(static_cast<xecs::scene::permanent_id>(Id)))
            {
                File.Write(std::uint32_t{ 0 });
                File.Write(std::uint32_t{ 0 });
                File.Write(std::uint32_t{ 0 });
                return;
            }

            const auto RootEntity = pScene->m_LocalToRuntime.at(static_cast<xecs::scene::permanent_id>(Id));
            auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(RootEntity);
            const auto iPI = Details.m_pPool ? Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) : -1;
            if (iPI < 0)
            {
                File.Write(std::uint32_t{ 0 });
                File.Write(std::uint32_t{ 0 });
                File.Write(std::uint32_t{ 0 });
                return;
            }

            auto& PI = *reinterpret_cast<xecs::editor::prefab_instance*>(&Details.m_pPool->m_pComponent[iPI][Details.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size]);
            SnapshotAllOverrideBookkeeping(File, PI);
            SnapshotPrefabBeforeValues(File, *e29::g_pGameMgr, RootEntity, PI);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0; File.Read(Scene);
            std::uint32_t Id = 0;    File.Read(Id);
            if (!e29::g_pGameMgr) return;

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(static_cast<xecs::scene::permanent_id>(Id))) return;

            const auto RootEntity = pScene->m_LocalToRuntime.at(static_cast<xecs::scene::permanent_id>(Id));
            auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(RootEntity);
            const auto iPI = Details.m_pPool ? Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) : -1;
            if (iPI < 0) return;

            auto& PI = *reinterpret_cast<xecs::editor::prefab_instance*>(&Details.m_pPool->m_pComponent[iPI][Details.m_PoolIndex.m_Value * xecs::component::type::info_v<xecs::editor::prefab_instance>.m_Size]);

            // File order: override bookkeeping, then prefab-before values. Restore Prefab first (needs
            // EnsureLoaded), then put the instance's override list back, then re-save the Prefab.
            // But bookkeeping is written first - so read bookkeeping into a temp, restore prefab
            // befores, then assign bookkeeping.
            xecs::editor::prefab_instance TempPI;
            TempPI.m_PrefabInstance = PI.m_PrefabInstance;
            RestoreAllOverrideBookkeeping(File, TempPI);
            RestorePrefabBeforeValues(File, *e29::g_pGameMgr, PI);

            PI.m_lComponents    = std::move(TempPI.m_lComponents);
            PI.m_HierarchyDiffs = std::move(TempPI.m_HierarchyDiffs);
            if (auto Err = e29::g_pGameMgr->m_PrefabMgr.Save(PI.m_PrefabInstance); Err)
                e29::Debugger(std::format("ApplyOverrides Undo: Prefab Save failed: {}", Err.getMessage()));

            e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, static_cast<xecs::scene::permanent_id>(Id));
            if (e29::g_pState) e29::g_pState->m_bEntityInspectorDirty = true;
        }

        xcmdline::parser::handle m_hScene, m_hId;
    };
}

#endif // E29_COMMANDS_APPLY_OVERRIDES_H