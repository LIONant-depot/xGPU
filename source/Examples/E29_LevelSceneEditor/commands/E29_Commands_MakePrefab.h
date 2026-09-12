#ifndef E29_COMMANDS_MAKE_PREFAB_H
#define E29_COMMANDS_MAKE_PREFAB_H
#pragma once

// MakePrefab / MakePrefabVariant - the gap deliberately deferred from both the prior gap-closing
// session ([[e29_command_undo_known_gaps]]) and the Asset Browser command-layer session
// ([[e29_asset_browser_command_layer]]): "Make Prefab" creates a real Prefab ASSET on disk
// (AssetMgr.NewAsset) AND converts a live entity/group into an instance of it - a genuine composition
// of what CreateAsset (E29_Commands_AssetBrowser.h) and the entity-subtree snapshot/restore machinery
// (SnapshotSubtreeForRestore/RestoreSubtreeFromSnapshot, E29_Commands_EntityLifecycle.h) each already
// solve on their own. This file is that composition, not a third reimplementation.
//
// Two distinct existing functions in kit/E29_PrefabAuthoring.h, two distinct commands here, matching
// that file's own split:
//   CreatePrefabFromGroupRoot - the general path: creates the asset, then DELETES the original live
//   root+descendants and INSTANTIATES a fresh copy under the prefab, splicing back into the original
//   parent/folder position. Wrapped below as MakePrefab.
//   CreatePrefabVariantFromInstance - the fast path for a SINGLE entity that's already a prefab
//   instance (no multi-select): creates the asset and just RE-POINTS that same live entity's own
//   prefab_instance component at it (clearing overrides) - no delete/recreate at all. Wrapped below
//   as MakePrefabVariant.
//
// DELIBERATELY NOT WRAPPED, same "flag rather than rush" precedent as EmptyTrashcan/Duplicate before
// it: DetermineGroupRoot's own synthetic-root-creation path (kit/E29_PrefabAuthoring.h) - multi-
// selecting 2+ DISJOINT top-level entities (no single existing subtree already covers the whole
// selection) synthesizes a brand-new "Prefab Root" entity and reparents each selected entity under
// it, BEFORE either MakePrefab/MakePrefabVariant below ever runs. That synthesis step is a real,
// separate mutation of its own (creates an entity, moves several others) that neither command here
// captures for undo - both commands below assume the ROOT they're given is already fully resolved
// (a single existing entity, root of a real subtree already, or from that synthesis step run
// separately/manually) exactly as DetermineGroupRoot itself already hands back today. Making THAT
// step undo-routed too is a distinct, smaller follow-up, not folded in here.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetBrowser.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_EntityLifecycle.h"

namespace e29::commands
{
    // Identical to e29::CreatePrefabFromGroupRoot (kit/E29_PrefabAuthoring.h) except the new Prefab
    // asset is created under an EXPLICIT, caller-pre-minted guid instead of an auto-generated one -
    // same "-Asset is pre-minted by the caller" convention CreateAsset/InstantiatePrefab already
    // established, needed so make_prefab_cmd::Redo stays deterministic/re-runnable across an
    // Undo/Redo cycle (the original function's own auto-generation would mint a DIFFERENT asset guid
    // on every call, leaking an abandoned, never-cleaned-up asset in the Trash on every Redo after an
    // Undo). Body is otherwise a verbatim copy - see that function's own comments for the full
    // reasoning behind each step, not repeated here.
    inline xresource::full_guid CreatePrefabFromGroupRootWithAssetGuid(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, editor_state* pState, e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, xecs::component::entity Root, xresource::full_guid ExplicitPrefabAssetGuid) noexcept
    {
        xecs::component::entity OriginalParent;
        if (auto& RD = GameMgr.m_ComponentMgr.getEntityDetails(Root); RD.m_pPool && RD.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
            OriginalParent = RD.m_pPool->getComponent<xecs::component::parent>(RD.m_PoolIndex).m_Value;

        const auto RootId           = Scene.m_RuntimeToLocal.at(Root.m_Value);
        const bool bRootWasSelected = pState && (pState->m_SelectedEntityId == RootId);
        const auto StaleRootValue   = Root.m_Value;

        const auto OriginalFolderId = e29::FindFolderContaining(Scene, RootId);

        std::string Name = "Prefab";
        if (auto& D = GameMgr.m_ComponentMgr.getEntityDetails(Root); D.m_pPool && D.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<e29::name>.m_BitID))
            Name = D.m_pPool->getComponent<e29::name>(D.m_PoolIndex).m_Value;

        // CreateOrRestoreAsset (E29_Commands_AssetBrowser.h), not a plain NewAsset call - a re-Redo
        // (after an Undo trashed this exact prefab asset guid) must restore-from-trash instead of
        // calling NewAsset again, same reasoning/bug CreateAsset's own Redo already had to solve -
        // confirmed live this hits the identical failure mode when reused verbatim here.
        CreateOrRestoreAsset(LibraryGUID, ExplicitPrefabAssetGuid, ParentGUID, Name);
        const xecs::prefab::guid PrefabGuid = ExplicitPrefabAssetGuid;

        GameMgr.m_PrefabMgr.CreatePrefabFromEntity(Root, PrefabGuid);
        if (auto Err = GameMgr.m_PrefabMgr.Save(PrefabGuid); Err)
        {
            e29::Debugger(std::format("Failed to save new Prefab: {}", Err.getMessage()));
            return {};
        }

        e29::DeleteEntitySubtree(GameMgr, Scene, SceneGuid, Root);

        auto NewRoot = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, GameMgr.m_PrefabMgr.m_PrefabList.at(PrefabGuid.m_Instance.m_Value), xecs::tools::empty_lambda{}, /*bRemoveRoot=*/false);

        if (OriginalParent.isValid())
        {
            std::array Add{ &xecs::component::type::info_v<xecs::component::parent> };
            NewRoot = GameMgr.AddOrRemoveComponents(NewRoot, Add, {});
            auto& NRDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewRoot);
            NRDetails.m_pPool->getComponent<xecs::component::parent>(NRDetails.m_PoolIndex).m_Value = OriginalParent;

            auto& OPDetails = GameMgr.m_ComponentMgr.getEntityDetails(OriginalParent);
            if (OPDetails.m_pPool && OPDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
            {
                auto& OPChildren = OPDetails.m_pPool->getComponent<xecs::component::children>(OPDetails.m_PoolIndex).m_List;
                for (auto& C : OPChildren)
                    if (C.m_Value == StaleRootValue) { C = NewRoot; break; }
            }
        }

        auto& NewChildDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewRoot);
        if (NewChildDetails.m_pPool && NewChildDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
        {
            auto ChildEntities = NewChildDetails.m_pPool->getComponent<xecs::component::children>(NewChildDetails.m_PoolIndex).m_List;
            for (auto Child : ChildEntities)
                e29::RegisterInstantiatedSubtree(GameMgr, Scene, SceneGuid, Child);
        }

        Scene.m_LocalToRuntime[RootId]           = NewRoot;
        Scene.m_RuntimeToLocal[NewRoot.m_Value]  = RootId;

        if (false == OriginalParent.isValid())
            e29::ReparentEntityIntoFolder(Scene, RootId, OriginalFolderId);

        e29::AttachPrefabInstanceComponent(GameMgr, Scene, RootId, NewRoot, PrefabGuid, pState);
        GameMgr.m_SceneMgr.MarkEntityDirty(SceneGuid, RootId);

        if (pState)
        {
            pState->m_MultiSelectedEntityIds.clear();
            pState->m_MultiSelectOrder.clear();
            if (bRootWasSelected)
            {
                if (auto It = Scene.m_LocalToRuntime.find(RootId); It != Scene.m_LocalToRuntime.end())
                {
                    pState->m_SelectedEntity        = It->second;
                    pState->m_SelectedEntityScene   = SceneGuid;
                    pState->m_bEntityInspectorDirty = true;
                }
            }
            else if (pState->m_SelectedEntityScene == SceneGuid && pState->m_SelectedEntityId != xecs::scene::invalid_permanent_id_v)
            {
                auto It = Scene.m_LocalToRuntime.find(pState->m_SelectedEntityId);
                if (It == Scene.m_LocalToRuntime.end() || It->second.m_Value != pState->m_SelectedEntity.m_Value)
                {
                    pState->m_SelectedEntityId      = xecs::scene::invalid_permanent_id_v;
                    pState->m_SelectedEntity        = {};
                    pState->m_SelectedEntityScene   = {};
                    pState->m_bEntityInspectorDirty = true;
                }
            }
        }

        return ExplicitPrefabAssetGuid;
    }


    // MoveToTrash alone is in-memory until a library Save - and its return value used to be ignored
    // here, so a silent miss (bad guid/library) left the Prefab visible in Resources forever after
    // Ctrl+Z. Persist the trashed info.txt immediately so the hide sticks across any reload, and
    // surface failures through Debugger.
    inline void TrashCreatedPrefabAsset(e10::library::guid LibraryGuid, xresource::full_guid AssetGuid) noexcept
    {
        if (AssetGuid.empty())
        {
            e29::Debugger("MakePrefab Undo: refusing to trash an empty asset guid");
            return;
        }
        if (auto Err = e10::g_LibMgr.MoveToTrash(LibraryGuid, AssetGuid); !Err.empty())
        {
            e29::Debugger(std::format("MakePrefab Undo: MoveToTrash failed: {}", Err));
            return;
        }

        xproperty::settings::context Context;
        const bool bFound = e10::g_LibMgr.getNodeInfo(LibraryGuid, AssetGuid, [&](e10::library_db::info_node& Node)
        {
            if (Node.m_Path.empty()) return;
            if (auto SerErr = Node.m_Info.Serialize(false, Node.m_Path.c_str(), Context); SerErr)
            {
                e29::Debugger(std::format("MakePrefab Undo: failed to persist trashed info.txt: {}", SerErr.getMessage()));
                return;
            }
            Node.m_InfoChangeCount = 0;
        });
        if (!bFound)
            e29::Debugger("MakePrefab Undo: MoveToTrash succeeded but getNodeInfo missed the asset");
    }

    //================================================================================================
    // MakePrefab - the general path. BackupCurrenState snapshots the ORIGINAL group (same shadow-id
    // machinery delete_entity_cmd's own Undo already relies on) BEFORE Redo converts it. Undo deletes
    // whatever instance is currently registered under Id, restores the original group from that
    // snapshot, and trashes the created asset - same DOCUMENTED ASYMMETRY as CreateAsset's own Undo
    // (MoveToTrash/MoveFromTrashTo is the only reversal primitive this asset system has; the created
    // info.txt is not deleted from disk, only trashed).
    //================================================================================================
    struct make_prefab_cmd : xundo::command_base
    {
        make_prefab_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MakePrefab", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Converts an entity (and its whole subtree) into a Prefab instance, creating the Prefab asset (undoable - restores the original group and trashes the created asset on Undo). Usage: MakePrefab -Scene hexguid -Id hexid -Library hexguid -Asset assetguid -Parent assetguid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene   = m_Parser.addOption("Scene",   "Scene guid, 16 hex digits",                                   true, 1);
            m_hId      = m_Parser.addOption("Id",      "Root entity permanent_id, 8 hex digits",                      true, 1);
            m_hLibrary = m_Parser.addOption("Library", "Asset library guid, 16 hex digits",                           true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "New Prefab asset's guid, 32 hex digits, pre-minted by the caller", true, 1);
            m_hParent  = m_Parser.addOption("Parent",  "Parent asset guid (where the new Prefab is filed), 32 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg   = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg      = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto ParentArg  = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(ParentArg))
                return "MakePrefab: bad arguments";

            const auto SceneGuid   = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id          = ParseEntityId(std::get<std::string>(IdArg));
            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto ParentGuid  = ParseAssetGuid(std::get<std::string>(ParentArg));

            if (!e29::g_pGameMgr) return "MakePrefab: no game world";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(Id)) return "MakePrefab: target not found";

            const auto Root = pScene->m_LocalToRuntime.at(Id);
            const auto Result = CreatePrefabFromGroupRootWithAssetGuid(*e29::g_pGameMgr, *pScene, SceneGuid, e29::g_pState, e10::g_LibMgr, LibraryGuid, ParentGuid, Root, AssetGuid);
            if (Result.empty()) return "MakePrefab: failed";
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg   = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg      = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);

            const std::uint64_t Scene   = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id      = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);

            File.Write(Scene);
            File.Write(Id);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));

            // BEFORE Redo runs anything - captures the ORIGINAL, pre-conversion group exactly, same
            // machinery delete_entity_cmd's own Undo relies on (E29_Commands_EntityLifecycle.h).
            SnapshotSubtreeForRestore(File, xecs::scene::guid{ .m_Instance = { Scene } }, static_cast<xecs::scene::permanent_id>(Id));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;   File.Read(Scene);
            std::uint32_t Id = 0;      File.Read(Id);
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset = ReadString(File);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto RootId    = static_cast<xecs::scene::permanent_id>(Id);

            // Remove whatever instance is currently registered under RootId (the fresh copy Redo
            // created), then restore the original group from the snapshot taken before Redo ever ran.
            DeleteSubtreeByPermanentId(SceneGuid, RootId);
            RestoreSubtreeFromSnapshot(File, SceneGuid, RootId);

            // Same documented asymmetry as CreateAsset's own Undo - trash, don't attempt to make the
            // on-disk info.txt vanish (MoveToTrash/MoveFromTrashTo is the only reversal primitive this
            // asset system has). Persist the trash tag to info.txt immediately - see
            // TrashCreatedPrefabAsset's own comment (silent MoveToTrash misses left "Entity" Prefabs
            // visible in Resources after Ctrl+Z).
            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            TrashCreatedPrefabAsset(LibraryGuid, ParseAssetGuid(Asset));
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hLibrary, m_hAsset, m_hParent;
    };

    //================================================================================================
    // MakePrefabVariant - the fast path for a single entity that's already a prefab instance: creates
    // the asset and re-points that SAME live entity's own prefab_instance component at it (no delete/
    // recreate at all - see CreatePrefabVariantFromInstance's own comment, kit/E29_PrefabAuthoring.h,
    // for why this is safe: the entity's current data already IS what a fresh instance of the new
    // variant looks like). Undo restores the old m_PrefabInstance/m_lComponents/m_ComponentDiffs and
    // trashes the created asset.
    //================================================================================================
    struct make_prefab_variant_cmd : xundo::command_base
    {
        make_prefab_variant_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MakePrefabVariant", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Captures a prefab instance's current overrides into a new Prefab Variant asset, in place (undoable). Usage: MakePrefabVariant -Scene hexguid -Id hexid -Library hexguid -Asset assetguid -Parent assetguid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene   = m_Parser.addOption("Scene",   "Scene guid, 16 hex digits",                                   true, 1);
            m_hId      = m_Parser.addOption("Id",      "Entity permanent_id, 8 hex digits",                           true, 1);
            m_hLibrary = m_Parser.addOption("Library", "Asset library guid, 16 hex digits",                           true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "New Prefab asset's guid, 32 hex digits, pre-minted by the caller", true, 1);
            m_hParent  = m_Parser.addOption("Parent",  "Parent asset guid (where the new Prefab is filed), 32 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg   = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg      = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto ParentArg  = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(ParentArg))
                return "MakePrefabVariant: bad arguments";

            const auto SceneGuid   = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id          = ParseEntityId(std::get<std::string>(IdArg));
            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto ParentGuid  = ParseAssetGuid(std::get<std::string>(ParentArg));

            if (!e29::g_pGameMgr) return "MakePrefabVariant: no game world";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(Id)) return "MakePrefabVariant: target not found";
            auto Entity = pScene->m_LocalToRuntime.at(Id);

            auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
            const auto iType = Details.m_pPool ? Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) : -1;
            if (iType < 0) return "MakePrefabVariant: entity is not a prefab instance";

            std::string Name = "Prefab";
            if (Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                Name = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;

            CreateOrRestoreAsset(LibraryGuid, AssetGuid, ParentGuid, Name);
            const xecs::prefab::guid PrefabGuid = AssetGuid;

            e29::g_pGameMgr->m_PrefabMgr.CreatePrefabFromEntity(Entity, PrefabGuid);
            if (auto Err = e29::g_pGameMgr->m_PrefabMgr.Save(PrefabGuid); Err)
                return std::format("MakePrefabVariant: {}", Err.getMessage());

            auto& PI = Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
            PI.m_PrefabInstance = PrefabGuid;
            PI.m_lComponents.clear();
            PI.m_ComponentDiffs.clear();
            e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, Id);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg   = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg      = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);

            const std::uint64_t Scene   = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id      = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);

            File.Write(Scene);
            File.Write(Id);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            xecs::editor::prefab_instance* pOldPI = nullptr;
            if (e29::g_pGameMgr)
            {
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid); pScene && pScene->m_LocalToRuntime.contains(static_cast<xecs::scene::permanent_id>(Id)))
                {
                    auto Entity = pScene->m_LocalToRuntime.at(static_cast<xecs::scene::permanent_id>(Id));
                    auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
                    const auto iType = Details.m_pPool ? Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) : -1;
                    if (iType >= 0) pOldPI = &Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
                }
            }

            File.Write(pOldPI != nullptr);
            if (!pOldPI) return;

            File.Write(pOldPI->m_PrefabInstance.m_Instance.m_Value);
            File.Write(pOldPI->m_PrefabInstance.m_Type.m_Value);

            File.Write(static_cast<std::uint32_t>(pOldPI->m_lComponents.size()));
            for (auto& C : pOldPI->m_lComponents)
            {
                File.Write(C.m_ComponentTypeGuid);
                File.Write(static_cast<std::uint32_t>(C.m_MemberPath.size()));
                for (auto P : C.m_MemberPath) File.Write(P);
                File.Write(static_cast<std::uint32_t>(C.m_PropertyOverrides.size()));
                for (auto& O : C.m_PropertyOverrides)
                {
                    WriteString(File, O.m_PropertyName);
                    WriteString(File, O.m_PropertyValueAsString);
                }
            }

            File.Write(static_cast<std::uint32_t>(pOldPI->m_ComponentDiffs.size()));
            for (auto& D : pOldPI->m_ComponentDiffs)
            {
                File.Write(D.m_ComponentTypeGuid);
                File.Write(D.m_bAdded);
            }

            File.Write(static_cast<std::uint32_t>(pOldPI->m_HierarchyDiffs.size()));
            for (auto& H : pOldPI->m_HierarchyDiffs)
            {
                File.Write(static_cast<std::uint32_t>(H.m_MemberPath.size()));
                for (auto P : H.m_MemberPath) File.Write(P);
                File.Write(H.m_bAdded);
            }
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;   File.Read(Scene);
            std::uint32_t Id = 0;      File.Read(Id);
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset = ReadString(File);

            bool bHadPI = false; File.Read(bHadPI);

            std::uint64_t OldInstance = 0, OldType = 0;
            std::vector<xecs::editor::prefab_component_override> OldComponents;
            std::vector<xecs::editor::prefab_component_diff>     OldDiffs;
            std::vector<xecs::editor::prefab_hierarchy_diff>     OldHierarchy;

            if (bHadPI)
            {
                File.Read(OldInstance);
                File.Read(OldType);

                std::uint32_t CompCount = 0; File.Read(CompCount);
                OldComponents.resize(CompCount);
                for (auto& C : OldComponents)
                {
                    File.Read(C.m_ComponentTypeGuid);
                    std::uint32_t PathCount = 0; File.Read(PathCount);
                    C.m_MemberPath.resize(PathCount);
                    for (auto& P : C.m_MemberPath) File.Read(P);
                    std::uint32_t OverrideCount = 0; File.Read(OverrideCount);
                    C.m_PropertyOverrides.resize(OverrideCount);
                    for (auto& O : C.m_PropertyOverrides)
                    {
                        O.m_PropertyName          = ReadString(File);
                        O.m_PropertyValueAsString = ReadString(File);
                    }
                }

                std::uint32_t DiffCount = 0; File.Read(DiffCount);
                OldDiffs.resize(DiffCount);
                for (auto& D : OldDiffs)
                {
                    File.Read(D.m_ComponentTypeGuid);
                    File.Read(D.m_bAdded);
                }

                std::uint32_t HierCount = 0; File.Read(HierCount);
                OldHierarchy.resize(HierCount);
                for (auto& H : OldHierarchy)
                {
                    std::uint32_t PathCount = 0; File.Read(PathCount);
                    H.m_MemberPath.resize(PathCount);
                    for (auto& P : H.m_MemberPath) File.Read(P);
                    File.Read(H.m_bAdded);
                }
            }

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));

            // Trash the created asset first (same asymmetric-Undo shape as CreateAsset/MakePrefab).
            TrashCreatedPrefabAsset(LibraryGuid, ParseAssetGuid(Asset));

            if (!bHadPI || !e29::g_pGameMgr) return;
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(static_cast<xecs::scene::permanent_id>(Id))) return;
            auto Entity = pScene->m_LocalToRuntime.at(static_cast<xecs::scene::permanent_id>(Id));
            auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
            const auto iType = Details.m_pPool ? Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) : -1;
            if (iType < 0) return;

            auto& PI = Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
            PI.m_PrefabInstance = xecs::prefab::guid{ .m_Instance = { OldInstance }, .m_Type = { OldType } };
            PI.m_lComponents    = std::move(OldComponents);
            PI.m_ComponentDiffs = std::move(OldDiffs);
            PI.m_HierarchyDiffs = std::move(OldHierarchy);
            e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, static_cast<xecs::scene::permanent_id>(Id));
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hLibrary, m_hAsset, m_hParent;
    };
}

// Drop-path entry used by entity_to_prefab_drop via g_MakePrefabDropHandler. Lives here (not in
// PrefabAuthoring.h) so it can call commands::Run / Format* without an include cycle. Preserves the
// existing Variant-vs-MakePrefab decision and still runs DetermineGroupRoot for multi-select BEFORE
// MakePrefab (that synthesis step remains a known separate undo gap - see this file's top comment).
namespace e29
{
    inline xundo::system* g_pUndo = nullptr;

    inline xresource::full_guid MakePrefabDropViaCommands(e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, const entity_drag_payload_t& Payload) noexcept
    {
        (void)AssetMgr;
        if (g_pUndo == nullptr || g_pGameMgr == nullptr) return {};

        auto* pScene = g_pGameMgr->m_SceneMgr.Find(Payload.m_SceneGuid);
        if (pScene == nullptr) return {};

        auto SourceIt = pScene->m_LocalToRuntime.find(Payload.m_Id);
        if (SourceIt == pScene->m_LocalToRuntime.end()) return {};

        xresource::instance_guid NewInstance{};
        NewInstance.GenerateGUID();
        const xresource::full_guid NewAsset{ .m_Instance = NewInstance, .m_Type = xecs::prefab::type_guid_v };

        const bool bIsMultiSelect = g_pState && g_pState->m_MultiSelectScene == Payload.m_SceneGuid && g_pState->m_MultiSelectedEntityIds.size() > 1 && g_pState->m_MultiSelectedEntityIds.contains(Payload.m_Id);
        if (!bIsMultiSelect)
        {
            auto& SourceDetails = g_pGameMgr->m_ComponentMgr.getEntityDetails(SourceIt->second);
            if (SourceDetails.m_pPool && SourceDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0)
            {
                const auto Cmd = std::format("MakePrefabVariant -Scene {} -Id {} -Library {} -Asset {} -Parent {}"
                    , e29::commands::FormatSceneGuid(Payload.m_SceneGuid)
                    , e29::commands::FormatEntityId(Payload.m_Id)
                    , e29::commands::FormatLibraryGuid(LibraryGUID)
                    , e29::commands::FormatAssetGuid(NewAsset)
                    , e29::commands::FormatAssetGuid(ParentGUID));
                if (!e29::commands::RunGroup(*g_pUndo, "MakePrefabVariant", { Cmd })) return {};
                return NewAsset;
            }
        }

        auto Root = g_pState ? DetermineGroupRoot(*g_pGameMgr, *pScene, Payload.m_SceneGuid, *g_pState, Payload.m_Id)
                             : SourceIt->second;
        if (Root.isValid() == false) return {};

        auto RootIdIt = pScene->m_RuntimeToLocal.find(Root.m_Value);
        if (RootIdIt == pScene->m_RuntimeToLocal.end()) return {};

        const auto Cmd = std::format("MakePrefab -Scene {} -Id {} -Library {} -Asset {} -Parent {}"
            , e29::commands::FormatSceneGuid(Payload.m_SceneGuid)
            , e29::commands::FormatEntityId(RootIdIt->second)
            , e29::commands::FormatLibraryGuid(LibraryGUID)
            , e29::commands::FormatAssetGuid(NewAsset)
            , e29::commands::FormatAssetGuid(ParentGUID));
        if (!e29::commands::RunGroup(*g_pUndo, "MakePrefab", { Cmd })) return {};
        return NewAsset;
    }
}


#endif // E29_COMMANDS_MAKE_PREFAB_H
