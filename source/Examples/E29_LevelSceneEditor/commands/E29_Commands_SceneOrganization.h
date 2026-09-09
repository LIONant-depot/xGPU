#ifndef E29_COMMANDS_SCENE_ORGANIZATION_H
#define E29_COMMANDS_SCENE_ORGANIZATION_H
#pragma once

// InstantiatePrefab / MoveToFolder - gap #5 of [[e29_command_undo_known_gaps]] ("half the editor
// isn't undo-routed"): dragging a Prefab asset onto the Level tree, and dragging an entity between
// folders (or in/out of one), both mutated scene state directly - not undoable, not reachable from a
// CLI/AI caller. "Make Prefab" and "Duplicate" are this gap's other two named items - deliberately
// NOT addressed here: Duplicate doesn't exist anywhere in E29 today (nothing to wrap), and Make
// Prefab is disproportionately larger than everything else this whole command system has ever had to
// undo - it creates a persistent ASSET FILE on disk (a brand-new Prefab, via AssetMgr.NewAsset +
// PrefabMgr.Save) and, for a multi-select group, synthesizes an entirely new root entity
// (DetermineGroupRoot) before converting the live group into an instance - no command in this system
// has ever had to reverse an asset-library creation, and getting that wrong risks corrupting the
// asset database, not just scene state. Flagged rather than rushed, matching this project's own
// standing rule against scope-creeping into a gap disproportionate to what was asked.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_EntityLifecycle.h"

namespace e29::commands
{
    //================================================================================================
    // InstantiatePrefab - mirrors e29::InstantiatePrefabIntoScene (E29_PrefabAuthoring.h) exactly,
    // except the group's ROOT is registered under an EXPLICIT, caller-minted id rather than an
    // auto-minted one - same "-Id is pre-minted by the caller" convention create_entity_cmd already
    // established (E29_Commands_EntityLifecycle.h), needed so Redo stays deterministic/re-runnable
    // across an Undo/Redo cycle. The group's DESCENDANTS still get freshly-minted ids on every Redo
    // call (via the existing RegisterInstantiatedSubtree) - safe, because Undo discovers them
    // dynamically by walking the root's own LIVE children (DeleteSubtreeByPermanentId, reused
    // verbatim from Phase 4), never by remembering descendant ids from a prior Redo.
    //================================================================================================
    inline bool InstantiatePrefabIntoSceneWithId(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::prefab::guid PrefabGuid, xecs::scene::permanent_id ExplicitRootId, xecs::scene::folder_id TargetFolder) noexcept
    {
        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err) return false;
        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return false;
        if (Scene.m_LocalToRuntime.contains(ExplicitRootId)) return false;

        // bRemoveRoot=false - same reasoning as InstantiatePrefabIntoScene's own comment: this needs
        // one standalone instantiated group, root included, not the root spliced away.
        auto NewRoot = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, RootIt->second, xecs::tools::empty_lambda{}, /*bRemoveRoot=*/false);

        Scene.m_LocalToRuntime[ExplicitRootId]  = NewRoot;
        Scene.m_RuntimeToLocal[NewRoot.m_Value] = ExplicitRootId;
        GameMgr.m_SceneMgr.MarkEntityNew(Scene.m_Guid, ExplicitRootId);

        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(NewRoot);
        if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
        {
            auto ChildEntities = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
            for (auto Child : ChildEntities)
                e29::RegisterInstantiatedSubtree(GameMgr, Scene, Scene.m_Guid, Child);
        }

        if (TargetFolder != xecs::scene::invalid_folder_id_v)
            e29::ReparentEntityIntoFolder(Scene, ExplicitRootId, TargetFolder);
        e29::AttachPrefabInstanceComponent(GameMgr, Scene, ExplicitRootId, NewRoot, PrefabGuid, nullptr); // brand-new, can't already be selected
        return true;
    }

    struct instantiate_prefab_cmd : xundo::command_base
    {
        instantiate_prefab_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "InstantiatePrefab", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Instantiates a Prefab asset into a scene (undoable - deletes the whole group again on Undo). Usage: InstantiatePrefab -Scene hexguid -Id hexid -Prefab hexguid -Folder hexfolder (0 = loose)";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene  = m_Parser.addOption("Scene",  "Scene guid, 16 hex digits",                                       true, 1);
            m_hId     = m_Parser.addOption("Id",     "Root entity permanent_id, 8 hex digits, pre-minted by the caller", true, 1);
            m_hPrefab = m_Parser.addOption("Prefab",  "Prefab asset's instance guid, 16 hex digits",                     true, 1);
            m_hFolder = m_Parser.addOption("Folder", "Target folder id, 8 hex digits (0 = loose/none)",                 true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto PrefabArg = m_Parser.getOptionArgAs<std::string>(m_hPrefab, 0);
            auto FolderArg = m_Parser.getOptionArgAs<std::string>(m_hFolder, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(PrefabArg) || std::holds_alternative<xerr>(FolderArg))
                return "InstantiatePrefab: bad arguments";

            const auto SceneGuid  = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id         = ParseEntityId(std::get<std::string>(IdArg));
            // xecs::prefab::guid is a full_guid (instance+type), but the type half is always
            // xecs::prefab::type_guid_v for anything reaching this command - same assumption the
            // existing drag-drop call site itself makes (E29_Panel_LevelTree.h checks
            // Dropped.m_Source.m_Type == xecs::prefab::type_guid_v before ever calling
            // InstantiatePrefabIntoScene) - so only the instance half needs to travel as an argument.
            const auto PrefabGuid = xecs::prefab::guid{ .m_Instance = { std::strtoull(std::get<std::string>(PrefabArg).c_str(), nullptr, 16) }, .m_Type = xecs::prefab::type_guid_v };
            const auto FolderVal  = static_cast<xecs::scene::folder_id>(std::strtoul(std::get<std::string>(FolderArg).c_str(), nullptr, 16));

            if (!e29::g_pGameMgr) return "InstantiatePrefab: no game world";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return "InstantiatePrefab: scene not found";
            if (pScene->m_LocalToRuntime.contains(Id)) return "InstantiatePrefab: id already in use";

            if (!InstantiatePrefabIntoSceneWithId(*e29::g_pGameMgr, *pScene, PrefabGuid, Id, FolderVal))
                return "InstantiatePrefab: failed to load/instantiate prefab";

            if (e29::g_pState) e29::g_pState->m_bEntityInspectorDirty = true;
            return {};
        }

        // Nothing to snapshot beyond Scene/Id - same reasoning as create_entity_cmd's own
        // BackupCurrenState (undo of "create/instantiate" is a pure inverse).
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);

            const std::uint64_t Scene = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id    = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            File.Write(Scene);
            File.Write(Id);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0; File.Read(Scene);
            std::uint32_t Id = 0;    File.Read(Id);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto PermId    = static_cast<xecs::scene::permanent_id>(Id);

            DeleteSubtreeByPermanentId(SceneGuid, PermId);

            if (e29::g_pGameMgr)
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                    pScene->m_PendingChanges[PermId].m_New -= 1;
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hPrefab, m_hFolder;
    };

    //================================================================================================
    // MoveToFolder - mirrors every existing e29::ReparentEntityIntoFolder drag-drop call site
    // (E29_Panel_LevelTree.h: entity dropped onto a folder row, dragged back out to loose, or dropped
    // onto the tree root). ReparentEntityIntoFolder has a real side effect beyond the entity's own
    // membership: PruneEmptyFolderChain deletes the OLD folder (and, cascading upward, any now-
    // childless ancestor) if the move leaves it completely empty - Undo must be able to reverse THAT
    // too, not just move the entity back, or an ancestor folder pruned by Redo would simply not exist
    // to move back into. BackupCurrenState snapshots every folder along the old ancestor chain
    // (id/parent/name only - membership is Id's own concern, not re-derived here) so Undo can
    // recreate any that Redo's own prune step removed, in root-to-leaf order, before re-inserting.
    //================================================================================================
    struct move_to_folder_cmd : xundo::command_base
    {
        move_to_folder_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MoveToFolder", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Moves an entity into a folder, or out to loose (undoable - restores the old folder, its position, and any folder emptied+pruned by the move). Usage: MoveToFolder -Scene hexguid -Id hexid -Folder hexfolder (0 = loose)";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene  = m_Parser.addOption("Scene",  "Scene guid, 16 hex digits",                    true, 1);
            m_hId     = m_Parser.addOption("Id",     "Entity permanent_id, 8 hex digits",             true, 1);
            m_hFolder = m_Parser.addOption("Folder", "Target folder id, 8 hex digits (0 = loose)",   true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto FolderArg = m_Parser.getOptionArgAs<std::string>(m_hFolder, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(FolderArg))
                return "MoveToFolder: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));
            const auto FolderVal = static_cast<xecs::scene::folder_id>(std::strtoul(std::get<std::string>(FolderArg).c_str(), nullptr, 16));

            if (!e29::g_pGameMgr) return "MoveToFolder: no game world";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(Id)) return "MoveToFolder: target not found";

            // A parented entity is never a folder member (rendered nested under its parent's own row
            // instead) - matches every existing drag-drop call site's own bHasParent check.
            auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(pScene->m_LocalToRuntime.at(Id));
            if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
                return "MoveToFolder: entity has a parent, not a folder member";

            e29::ReparentEntityIntoFolder(*pScene, Id, FolderVal);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto FolderArg = m_Parser.getOptionArgAs<std::string>(m_hFolder, 0);

            const std::uint64_t Scene    = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id       = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint32_t FolderVal = std::holds_alternative<xerr>(FolderArg) ? 0 : static_cast<std::uint32_t>(std::strtoul(std::get<std::string>(FolderArg).c_str(), nullptr, 16));

            File.Write(Scene);
            File.Write(Id);
            File.Write(FolderVal);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            auto* pScene = e29::g_pGameMgr ? e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid) : nullptr;
            if (!pScene)
            {
                File.Write(static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v));
                File.Write(std::uint32_t{ 0 });
                File.Write(std::uint32_t{ 0 });
                return;
            }

            const auto OldFolder = e29::FindFolderContaining(*pScene, static_cast<xecs::scene::permanent_id>(Id));
            std::uint32_t OldIndex = 0;
            if (OldFolder != xecs::scene::invalid_folder_id_v)
            {
                if (auto FolderIt = std::ranges::find(pScene->m_Folders, OldFolder, &xecs::scene::folder::m_Id); FolderIt != pScene->m_Folders.end())
                    if (auto EntIt = std::ranges::find(FolderIt->m_Entities, static_cast<xecs::scene::permanent_id>(Id)); EntIt != FolderIt->m_Entities.end())
                        OldIndex = static_cast<std::uint32_t>(EntIt - FolderIt->m_Entities.begin());
            }
            File.Write(static_cast<std::uint32_t>(OldFolder));
            File.Write(OldIndex);

            // Snapshot every folder along OldFolder's own ancestor chain (leaf to root) - the exact
            // set PruneEmptyFolderChain could remove as a side effect of this move. Written root-first
            // so Undo can recreate them in a sensible order (not structurally required - m_Parent is
            // just a stored id, not a container the recreation needs to already exist - but keeps the
            // written order readable).
            std::vector<xecs::scene::folder> Ancestors;
            for (auto Cur = OldFolder; Cur != xecs::scene::invalid_folder_id_v; )
            {
                auto FolderIt = std::ranges::find(pScene->m_Folders, Cur, &xecs::scene::folder::m_Id);
                if (FolderIt == pScene->m_Folders.end()) break;
                Ancestors.push_back({ .m_Id = FolderIt->m_Id, .m_Parent = FolderIt->m_Parent, .m_Name = FolderIt->m_Name });
                Cur = FolderIt->m_Parent;
            }
            std::ranges::reverse(Ancestors);

            File.Write(static_cast<std::uint32_t>(Ancestors.size()));
            for (auto& F : Ancestors)
            {
                File.Write(static_cast<std::uint32_t>(F.m_Id));
                File.Write(static_cast<std::uint32_t>(F.m_Parent));
                WriteString(File, F.m_Name);
            }
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;        File.Read(Scene);
            std::uint32_t Id = 0;           File.Read(Id);
            std::uint32_t FolderVal = 0;    File.Read(FolderVal);
            std::uint32_t OldFolder = 0;    File.Read(OldFolder);
            std::uint32_t OldIndex = 0;     File.Read(OldIndex);
            std::uint32_t AncestorCount = 0; File.Read(AncestorCount);

            struct ancestor_row { std::uint32_t m_Id, m_Parent; std::string m_Name; };
            std::vector<ancestor_row> Ancestors(AncestorCount);
            for (auto& A : Ancestors)
            {
                File.Read(A.m_Id);
                File.Read(A.m_Parent);
                A.m_Name = ReadString(File);
            }

            if (!e29::g_pGameMgr) return;
            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return;

            // Recreate any ancestor folder Redo's own prune step removed, BEFORE reparenting back into
            // it - ReparentEntityIntoFolder silently no-ops the re-add if the target folder doesn't
            // exist (its own find_if just fails), so this must run first.
            for (auto& A : Ancestors)
            {
                if (std::ranges::find(pScene->m_Folders, static_cast<xecs::scene::folder_id>(A.m_Id), &xecs::scene::folder::m_Id) == pScene->m_Folders.end())
                {
                    pScene->m_Folders.push_back(xecs::scene::folder
                    { .m_Id       = static_cast<xecs::scene::folder_id>(A.m_Id)
                    , .m_Parent   = static_cast<xecs::scene::folder_id>(A.m_Parent)
                    , .m_Name     = A.m_Name
                    , .m_Entities = {}
                    });
                }
            }

            e29::ReparentEntityIntoFolder(*pScene, static_cast<xecs::scene::permanent_id>(Id), static_cast<xecs::scene::folder_id>(OldFolder));

            if (OldFolder != static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v))
            {
                if (auto FolderIt = std::ranges::find(pScene->m_Folders, static_cast<xecs::scene::folder_id>(OldFolder), &xecs::scene::folder::m_Id); FolderIt != pScene->m_Folders.end())
                {
                    if (auto EntIt = std::ranges::find(FolderIt->m_Entities, static_cast<xecs::scene::permanent_id>(Id)); EntIt != FolderIt->m_Entities.end())
                    {
                        FolderIt->m_Entities.erase(EntIt);
                        const auto InsertAt = std::min(static_cast<std::size_t>(OldIndex), FolderIt->m_Entities.size());
                        FolderIt->m_Entities.insert(FolderIt->m_Entities.begin() + InsertAt, static_cast<xecs::scene::permanent_id>(Id));
                    }
                }
            }

            if (e29::g_pState) e29::g_pState->m_bEntityInspectorDirty = true;
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hFolder;
    };
}

#endif // E29_COMMANDS_SCENE_ORGANIZATION_H
