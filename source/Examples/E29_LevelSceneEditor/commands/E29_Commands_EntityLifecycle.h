#ifndef E29_COMMANDS_ENTITY_LIFECYCLE_H
#define E29_COMMANDS_ENTITY_LIFECYCLE_H
#pragma once

// Create/Delete Entity - phase 4 of [[e29_command_undo_system_plan]] (memory). Replaces
// kit/E29_Panel_LevelTree.h's direct calls (ShowCreateMenuItems' "New Entity", DoDeleteEntity) with
// real xundo commands.
//
// CREATE is cheap either direction: Redo makes a bare entity (no components at all, matching
// ShowCreateMenuItems' own existing behavior) and Undo just deletes it again - by xundo's own
// documented stepping contract (xundo_system.h's RewindTo/FastForwardTo always step Undo/Redo ONE AT
// A TIME, even jumping several entries), any later command that added components/children to this
// entity already had ITS OWN Undo run first, in order, before create_entity_cmd's own Undo is ever
// reached - so by then the entity is guaranteed back to bare. Shares delete_entity_cmd's own cascade
// (DeleteSubtreeByPermanentId, below) rather than assuming "always a leaf" for the same reason
// add_component_cmd::Undo (E29_Commands_ComponentEdit.h) doesn't special-case anything either: it
// costs nothing to be correct for the untracked edge case (e.g. a child dragged onto it via the
// tree's own drag-drop, which isn't itself undo-routed yet) instead of merely the common one.
//
// DELETE is the hard direction, and the reason this phase needed a dedicated research pass before any
// code was written (matching phase 2/3's own precedent for anything touching prefab-override or
// cross-entity-reference bookkeeping). A deleted entity's children (xecs::component::children),
// parent (xecs::component::parent), and any xecs::component::entity_reference all hold RAW RUNTIME
// HANDLES - meaningless the instant the entity is destroyed and a new one is created in its place, so
// Phase 2/3's "walk properties as strings" snapshot is NOT safe to reuse verbatim here. Reusing it
// naively would either corrupt these fields or require reinventing xECS's own already-correct
// reference-encoding scheme by hand - a real risk of introducing exactly the kind of subtle bug this
// whole undo system exists to make it easy to avoid.
//
// Instead, this reuses the EXACT machinery real Save/Load already trusts for this:
//   xecs::scene::mgr::SaveEntity / xecs::scene::details::LoadEntity - per-entity read/write, already
//   correctly reference-encoding parent/children/entity_reference fields as permanent-id-relative
//   values (or an external-ref-table index for a cross-scene target) instead of raw handles, and
//   already correctly handling a prefab-instance entity's own overlay (only the OVERRIDDEN data gets
//   written/read, the rest reconstructed from the prefab itself).
//   xecs::persist::details::RemapLoadedEntityReferences - resolves those encoded values back into
//   real handles, per entity, once every entity that might be referenced is registered.
//   xecs::persist::details::ApplyPrefabInstancePropertyOverrides - reapplies a prefab instance's own
//   recorded property overrides, which needs to run AFTER every reference is resolved (a non-empty
//   m_MemberPath override needs a real children.m_List to walk - see LoadEntity's own comment for why
//   doing this any earlier crashed, confirmed already hit once this session).
//
// The one wrinkle: SaveEntity/LoadEntity always read/write a FIXED path derived from
// (Mgr, SceneGuid, Id) - EntityPath, xecs_scene_inline.h - with no in-memory or alternate-path mode.
// Using the entity's REAL Id for this snapshot would be actively wrong, not just inelegant: SaveScene
// (the real "File > Save") DELETES that exact file the moment it sees this Id marked Deleted
// (xecs_scene_inline.h ~line 429) - so a real Save between Delete and Undo would silently destroy the
// very snapshot Undo needs, and two independent deletes of the same (recreated) Id within one live
// undo history would clobber each other's snapshot at the same shared path. Both are avoided by
// writing to a throwaway SHADOW id (freshly minted via NextFreeEntityId, unique per subtree member,
// per history entry) instead of the entity's real one, then remapping the scene's own maps from
// shadow back to real immediately after loading - see SnapshotSubtree/RestoreSubtree below.
//
// m_PendingChanges (xecs_scene.h) already anticipates exactly this need, by its own doc comment:
// "each concern gets its own delta so undo/redo (whenever it lands) can just apply the exact inverse
// of whatever action it's undoing (a create's undo does m_New -= 1, ... a delete's undo does
// m_Deleted -= 1)" - this file follows that documented contract literally, not a workaround invented
// here. Without it, a real Save after a Delete+Undo round trip would still see the stale Deleted>0
// counter and incorrectly erase the (now very much alive again) entity's real on-disk file.
//
// A second, sharper wrinkle found live (a real crash, not anticipated by the reasoning above):
// SaveEntity itself unconditionally re-registers the scene's OWN identity maps at the end of its own
// body (xecs_scene_inline.h ~line 660: `Scene.m_LocalToRuntime[Id] = Entity; Scene.m_RuntimeToLocal
// [Entity.m_Value] = Id;`), using WHATEVER Id it was called with - correct behavior for a REAL save
// (defensively keeps the maps in sync with whatever's actually live), but calling it with a throwaway
// ShadowId re-points Scene.m_RuntimeToLocal[Entity.m_Value] AWAY from the entity's real permanent_id
// and onto the shadow one, corrupting the very entity BackupCurrenState is about to hand off to
// Redo()'s own delete - which then can't find it under its real id, silently skips erasing
// m_LocalToRuntime[RealId], and leaves it dangling at a now-destroyed (zombied) entity the moment
// GameMgr.DeleteEntity runs anyway. Symptom, confirmed via a live debugger call stack: an assert
// (`Entry.m_Validation == Entity.m_Validation`) the NEXT time anything (here,
// RenderEntityPropertiesPanel) resolved that dangling id - not inside this file's own code at all,
// which is what made it worth recording this precisely. Fixed by having Walk() (below) immediately
// undo SaveEntity's own side effect for the shadow id right after each call.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_ComponentEdit.h"

namespace e29::commands
{
    // Shared by delete_entity_cmd::Redo and create_entity_cmd::Undo (see this file's own top comment
    // for why undo-of-create reuses the real cascade instead of a bespoke "just this one" delete).
    // Mirrors kit/E29_Panel_LevelTree.h's own DoDeleteEntity exactly, including the selection/
    // multi-select survival cleanup - this command can run from Undo/Redo just as easily as from the
    // context menu that used to be the only caller.
    inline void DeleteSubtreeByPermanentId(xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id) noexcept
    {
        if (!e29::g_pGameMgr) return;
        auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
        if (!pScene) return;
        auto It = pScene->m_LocalToRuntime.find(Id);
        if (It == pScene->m_LocalToRuntime.end()) return;

        e29::DeleteEntitySubtree(*e29::g_pGameMgr, *pScene, SceneGuid, It->second);
        std::printf("[DeleteSubtreeByPermanentId] Id=%u deleted\n", Id); std::fflush(stdout);

        if (e29::g_pState)
        {
            auto& State = *e29::g_pState;
            if (State.m_SelectedEntityScene == SceneGuid)
            {
                // Hardening beyond "does the id still resolve to SOMETHING": also clear if it resolves
                // to a DIFFERENT live handle than State.m_SelectedEntity itself currently holds (e.g.
                // id reuse, or any future bookkeeping bug of the same shape as the SaveEntity/shadow-id
                // map-corruption case this phase already hit once) - a stale cached handle is exactly
                // what crashed RenderEntityPropertiesPanel before that root cause was found, so this
                // check is deliberately stricter than a bare `.contains()`.
                auto SelIt = pScene->m_LocalToRuntime.find(State.m_SelectedEntityId);
                const bool bStillLive = (SelIt != pScene->m_LocalToRuntime.end()) && (SelIt->second.m_Value == State.m_SelectedEntity.m_Value);
                if (!bStillLive)
                {
                    State.m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
                    State.m_SelectedEntity      = {};
                    State.m_SelectedEntityScene = {};
                }
            }
            if (State.m_MultiSelectScene == SceneGuid)
            {
                std::erase_if(State.m_MultiSelectedEntityIds, [&](auto Id2) noexcept { return !pScene->m_LocalToRuntime.contains(Id2); });
                std::erase_if(State.m_MultiSelectOrder, [&](auto Id2) noexcept { return !pScene->m_LocalToRuntime.contains(Id2); });
            }
            State.m_bEntityInspectorDirty = true;
        }
    }

    //================================================================================================
    // CreateEntity - Redo makes a brand-new, bare entity (no components, matching
    // ShowCreateMenuItems' own existing "New Entity" behavior exactly); Undo removes it again.
    //================================================================================================
    struct create_entity_cmd : xundo::command_base
    {
        create_entity_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateEntity", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Creates a brand-new, bare entity (undoable - deletes it again on Undo). Usage: CreateEntity -Scene hexguid -Id hexid -Folder hexfolder (0 = loose) [-Parent hexid]";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene  = m_Parser.addOption("Scene",  "Scene guid, 16 hex digits",                                 true,  1);
            m_hId     = m_Parser.addOption("Id",     "Entity permanent_id, 8 hex digits, pre-minted by the caller", true,  1);
            m_hFolder = m_Parser.addOption("Folder", "Target folder id, 8 hex digits (0 = loose/none)",           true,  1);
            // NOT required - ShowCreateMenuItems' Scene/Folder-row call sites never pass this at all
            // (only the entity-row "New Entity" does, to create a child). Marking it required broke
            // BOTH of those pre-existing call sites outright: xcmdline::parser::Parse fails the whole
            // command the moment ANY required option has zero args (xcmdline_parser.h ~line 119) -
            // confirmed via direct external review, then verified against the parser's own source
            // before fixing (rather than taking the report at face value).
            m_hParent = m_Parser.addOption("Parent", "Parent entity permanent_id, 8 hex digits, if creating a child", false, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto FolderArg = m_Parser.getOptionArgAs<std::string>(m_hFolder, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(FolderArg))
                return "CreateEntity: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));
            const auto FolderVal = static_cast<xecs::scene::folder_id>(std::strtoul(std::get<std::string>(FolderArg).c_str(), nullptr, 16));

            // -Parent is genuinely optional (see RegisterArguments' own comment) - absent means "no
            // parent, use Folder instead", not a parse failure.
            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            const auto ParentId = std::holds_alternative<xerr>(ParentArg)
                ? xecs::scene::invalid_permanent_id_v
                : ParseEntityId(std::get<std::string>(ParentArg));

            if (!e29::g_pGameMgr) return "CreateEntity: no game world";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return "CreateEntity: scene not found";
            if (pScene->m_LocalToRuntime.contains(Id)) return "CreateEntity: id already in use";
            if (ParentId != xecs::scene::invalid_permanent_id_v && !pScene->m_LocalToRuntime.contains(ParentId))
                return "CreateEntity: parent not found";

            auto& Archetype = e29::g_pGameMgr->getOrCreateArchetype<>();
            auto  Entity    = Archetype.CreateEntity(xecs::tools::empty_lambda{});
            pScene->m_LocalToRuntime[Id]              = Entity;
            pScene->m_RuntimeToLocal[Entity.m_Value]  = Id;
            e29::g_pGameMgr->m_SceneMgr.MarkEntityNew(SceneGuid, Id);

            if (ParentId != xecs::scene::invalid_permanent_id_v)
            {
                // Give the new entity a `parent` component pointing at ParentId - AddOrRemoveComponents
                // always migrates to a new handle (Phase 3's own MigrateEntityComponents,
                // E29_Commands_ComponentEdit.h, does the caller-responsibility scene-map remap this
                // already needs), so re-resolve everything through it rather than trusting the local
                // `Entity` above afterward.
                std::array<const xecs::component::type::info*, 1> AddParent{ &xecs::component::type::info_v<xecs::component::parent> };
                const auto NewChildEntity = MigrateEntityComponents(SceneGuid, Id, AddParent, {});
                if (!NewChildEntity.isValid()) return "CreateEntity: failed to attach parent";

                // Ensure the PARENT also has a `children` component - a freshly-authored entity usually
                // doesn't have one yet the first time it gains a child.
                auto ParentEntity = pScene->m_LocalToRuntime.at(ParentId);
                auto& PDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(ParentEntity);
                if (!PDetails.m_pPool || !PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                {
                    std::array<const xecs::component::type::info*, 1> AddChildren{ &xecs::component::type::info_v<xecs::component::children> };
                    ParentEntity = MigrateEntityComponents(SceneGuid, ParentId, AddChildren, {});
                    if (!ParentEntity.isValid()) return "CreateEntity: failed to attach children to parent";
                }

                // Wire up both sides directly - these two fields are structural ECS bookkeeping, not
                // user-editable data (see DeleteEntitySubtree's own identical direct-pool-access pattern
                // for parent/children, E29_PrefabAuthoring.h), so no xproperty round trip needed here.
                auto& ChildDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(NewChildEntity);
                ChildDetails.m_pPool->getComponent<xecs::component::parent>(ChildDetails.m_PoolIndex).m_Value = ParentEntity;

                auto& ParentDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(ParentEntity);
                ParentDetails.m_pPool->getComponent<xecs::component::children>(ParentDetails.m_PoolIndex).m_List.push_back(NewChildEntity);

                e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, ParentId);
            }
            else if (FolderVal != xecs::scene::invalid_folder_id_v)
            {
                e29::ReparentEntityIntoFolder(*pScene, Id, FolderVal);
            }

            if (e29::g_pState) e29::g_pState->m_bEntityInspectorDirty = true;
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            // Almost nothing to snapshot - see this file's own top comment (undo of create is a pure
            // inverse, no data to preserve) - EXCEPT one real side effect Redo() has when -Parent is
            // given: it gives the PARENT a `children` component if it didn't already have one
            // ([[e29_command_undo_known_gaps]]'s own gap #2). Recorded here, BEFORE Redo runs, since
            // that's the only point "did the parent already have one" is still answerable.
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);

            const std::uint64_t Scene = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id    = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint32_t ParentId = std::holds_alternative<xerr>(ParentArg) ? static_cast<std::uint32_t>(xecs::scene::invalid_permanent_id_v) : ParseEntityId(std::get<std::string>(ParentArg));

            File.Write(Scene);
            File.Write(Id);
            File.Write(ParentId);

            bool bParentAlreadyHadChildren = true; // harmless default - only consulted when ParentId is valid
            if (ParentId != static_cast<std::uint32_t>(xecs::scene::invalid_permanent_id_v) && e29::g_pGameMgr)
            {
                const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                {
                    if (auto It = pScene->m_LocalToRuntime.find(static_cast<xecs::scene::permanent_id>(ParentId)); It != pScene->m_LocalToRuntime.end())
                    {
                        auto& PDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(It->second);
                        bParentAlreadyHadChildren = PDetails.m_pPool && PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID);
                    }
                }
            }
            File.Write(bParentAlreadyHadChildren);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;    File.Read(Scene);
            std::uint32_t Id = 0;       File.Read(Id);
            std::uint32_t ParentId = 0; File.Read(ParentId);
            bool bParentAlreadyHadChildren = true; File.Read(bParentAlreadyHadChildren);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto PermId    = static_cast<xecs::scene::permanent_id>(Id);

            DeleteSubtreeByPermanentId(SceneGuid, PermId);

            // Exact inverse of this command's own Redo() MarkEntityNew call - see this file's own top
            // comment quoting m_PendingChanges' documented undo contract.
            if (e29::g_pGameMgr)
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                    pScene->m_PendingChanges[PermId].m_New -= 1;

            // Gap #2 fix ([[e29_command_undo_known_gaps]]): DeleteSubtreeByPermanentId above already
            // scrubbed this child out of the parent's children.m_List (DeleteEntitySubtree's own
            // existing parent-scrub side effect) - if Redo() had to ADD that children component in the
            // first place (the parent didn't have one before) and the list is now empty, strip the
            // component entirely rather than leaving an empty, untidy one behind.
            if (!bParentAlreadyHadChildren && ParentId != static_cast<std::uint32_t>(xecs::scene::invalid_permanent_id_v) && e29::g_pGameMgr)
            {
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                {
                    if (auto It = pScene->m_LocalToRuntime.find(static_cast<xecs::scene::permanent_id>(ParentId)); It != pScene->m_LocalToRuntime.end())
                    {
                        auto& PDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(It->second);
                        if (PDetails.m_pPool && PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                        {
                            const auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                            if (List.empty())
                            {
                                std::array<const xecs::component::type::info*, 1> RemoveChildren{ &xecs::component::type::info_v<xecs::component::children> };
                                MigrateEntityComponents(SceneGuid, static_cast<xecs::scene::permanent_id>(ParentId), {}, RemoveChildren);
                            }
                        }
                    }
                }
            }
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hFolder, m_hParent;
    };

    //================================================================================================
    // DeleteEntity - Redo deletes Id and its whole child subtree (DeleteEntitySubtree, same as the
    // pre-existing UI action); Undo restores every entity in that subtree exactly, including
    // hierarchy, cross-entity references, and prefab-instance overrides - see this file's own top
    // comment for the full reasoning behind the shadow-id SaveEntity/LoadEntity approach.
    //================================================================================================
    struct delete_entity_cmd : xundo::command_base
    {
        delete_entity_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteEntity", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Deletes an entity and its whole child subtree (undoable - restores hierarchy, references, and prefab overrides on Undo). Usage: DeleteEntity -Scene hexguid -Id hexid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits",             true, 1);
            m_hId    = m_Parser.addOption("Id",    "Root entity permanent_id, 8 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg))
                return "DeleteEntity: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id        = ParseEntityId(std::get<std::string>(IdArg));

            if (!e29::g_pGameMgr) return "DeleteEntity: no game world";
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene || !pScene->m_LocalToRuntime.contains(Id)) return "DeleteEntity: target not found";

            DeleteSubtreeByPermanentId(SceneGuid, Id);
            return {};
        }

        // Runs BEFORE Redo() actually deletes anything (xundo::system::Execute always calls
        // BackupCurrenState first - see set_property_cmd's own comment, E29_Commands_PropertyEdit.h,
        // for the confirmed ordering) - the whole subtree is still live under its REAL ids here, so
        // SaveEntity's own reference-encoding naturally captures sibling/parent references using their
        // real permanent_ids, not the shadow ones minted just below for the disk path itself.
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);

            const std::uint64_t Scene = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id    = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            File.Write(Scene);
            File.Write(Id);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto PermId    = static_cast<xecs::scene::permanent_id>(Id);

            auto* pScene = e29::g_pGameMgr ? e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid) : nullptr;
            if (!pScene || !pScene->m_LocalToRuntime.contains(PermId))
            {
                File.Write(static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v));
                File.Write(std::uint32_t{ 0 });
                File.Write(static_cast<std::uint32_t>(xecs::scene::invalid_permanent_id_v));
                File.Write(std::uint32_t{ 0 });
                return;
            }

            // Root-only - a child entity is never independently a folder member (folders own top-level
            // membership only; see ReparentEntityIntoFolder/DetermineGroupRoot's own comments). Position
            // WITHIN the folder's own m_Entities is captured too (direct user request: "when the undo
            // happens the Entity should be reinserted into the tree in the same place") -
            // ReparentEntityIntoFolder itself always appends, so Undo re-inserts at this index
            // explicitly afterward rather than relying on it.
            {
                const auto FolderId = e29::FindFolderContaining(*pScene, PermId);
                File.Write(static_cast<std::uint32_t>(FolderId));
                std::uint32_t FolderIndex = 0;
                if (FolderId != xecs::scene::invalid_folder_id_v)
                {
                    if (auto FolderIt = std::ranges::find(pScene->m_Folders, FolderId, &xecs::scene::folder::m_Id); FolderIt != pScene->m_Folders.end())
                        if (auto EntIt = std::ranges::find(FolderIt->m_Entities, PermId); EntIt != FolderIt->m_Entities.end())
                            FolderIndex = static_cast<std::uint32_t>(EntIt - FolderIt->m_Entities.begin());
                }
                File.Write(FolderIndex);
            }

            // The root's own PARENT (if any) is, by definition, OUTSIDE the subtree being deleted (a
            // parent is never deleted along with just one of its children) - so it's never itself one
            // of the Entries snapshotted below, and its OWN `children` component never gets saved/
            // restored as part of this command at all. DeleteEntitySubtree's own top-level step
            // (E29_PrefabAuthoring.h) scrubs the root out of THIS parent's children.m_List as a real,
            // separate side effect - Undo must reverse that explicitly, or the restored root would come
            // back with a correct `Parent` field (fixed generically by RemapLoadedEntityReferences,
            // below) yet never actually appear in the tree again, since nothing re-inserts it into its
            // parent's own list. Recorded here as a permanent_id (not a runtime handle - the parent
            // itself is never touched by this delete, but ITS live handle could still change for
            // unrelated reasons before Undo runs, e.g. a hot reload). Position WITHIN the parent's own
            // children.m_List is captured too, same "restore to the same place" reasoning as the folder
            // case above.
            {
                xecs::scene::permanent_id RootParentId = xecs::scene::invalid_permanent_id_v;
                std::uint32_t             ChildIndex   = 0;
                auto RootEntity = pScene->m_LocalToRuntime.at(PermId);
                auto& RootDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(RootEntity);
                if (RootDetails.m_pPool && RootDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
                {
                    const auto ParentEntity = RootDetails.m_pPool->getComponent<xecs::component::parent>(RootDetails.m_PoolIndex).m_Value;
                    if (ParentEntity.isValid())
                    {
                        if (auto ParentIt = pScene->m_RuntimeToLocal.find(ParentEntity.m_Value); ParentIt != pScene->m_RuntimeToLocal.end())
                        {
                            RootParentId = ParentIt->second;
                            auto& PDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(ParentEntity);
                            if (PDetails.m_pPool && PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                            {
                                auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                                if (auto ChildIt = std::ranges::find(List, RootEntity.m_Value, &xecs::component::entity::m_Value); ChildIt != List.end())
                                    ChildIndex = static_cast<std::uint32_t>(ChildIt - List.begin());
                            }
                        }
                    }
                }
                File.Write(static_cast<std::uint32_t>(RootParentId));
                File.Write(ChildIndex);
            }

            struct snapshot_entry { xecs::scene::permanent_id m_RealId; xecs::scene::permanent_id m_ShadowId; };
            std::vector<snapshot_entry> Entries;

            std::function<void(xecs::component::entity)> Walk = [&](xecs::component::entity Entity) noexcept
            {
                auto RtIt = pScene->m_RuntimeToLocal.find(Entity.m_Value);
                if (RtIt == pScene->m_RuntimeToLocal.end()) return;
                const auto RealId = RtIt->second;

                const auto ShadowId = e29::NextFreeEntityId(*pScene);
                e29::g_pGameMgr->m_SceneMgr.SaveEntity(SceneGuid, ShadowId, Entity);

                // SaveEntity itself unconditionally re-registers Scene.m_LocalToRuntime[Id]/
                // m_RuntimeToLocal[Entity.m_Value] using WHATEVER Id it was called with (xecs_scene_
                // inline.h ~line 660) - correct for a real save, but calling it with a throwaway
                // ShadowId just repointed m_RuntimeToLocal[Entity.m_Value] at ShadowId, corrupting this
                // entity's own real registration (confirmed live: caused a dangling m_LocalToRuntime
                // entry once Redo() actually deleted it, crashing the NEXT frame's unrelated render
                // code). Undo that side effect immediately - m_LocalToRuntime[RealId] itself was never
                // touched (SaveEntity only wrote under ShadowId), so only these two lines are needed.
                pScene->m_LocalToRuntime.erase(ShadowId);
                pScene->m_RuntimeToLocal[Entity.m_Value] = RealId;

                std::printf("[DeleteEntity::BackupCurrenState] snapshotted RealId=%u under ShadowId=%u\n", RealId, ShadowId); std::fflush(stdout);
                Entries.push_back({ RealId, ShadowId });

                auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
                if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                {
                    // Copy, not reference - matches DeleteEntitySubtree's own pattern (E29_PrefabAuthoring.h):
                    // SaveEntity below can (in principle) touch pool memory, and iterating a reference into a
                    // container that might reallocate underneath the loop is exactly the class of bug
                    // [[xecs_pool_reallocation_hazard]] already covers.
                    auto ChildList = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
                    for (auto Child : ChildList) Walk(Child);
                }
            };
            Walk(pScene->m_LocalToRuntime.at(PermId));
            std::printf("[DeleteEntity::BackupCurrenState] Scene=%s Id=%u subtree walk complete, %zu entries\n", std::get<std::string>(SceneArg).c_str(), Id, Entries.size()); std::fflush(stdout);

            File.Write(static_cast<std::uint32_t>(Entries.size()));
            for (auto& E : Entries)
            {
                File.Write(static_cast<std::uint32_t>(E.m_RealId));
                File.Write(static_cast<std::uint32_t>(E.m_ShadowId));
            }
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;         File.Read(Scene);
            std::uint32_t Id = 0;            File.Read(Id);
            std::uint32_t FolderVal = 0;     File.Read(FolderVal);
            std::uint32_t FolderIndex = 0;   File.Read(FolderIndex);
            std::uint32_t RootParentId = 0;  File.Read(RootParentId);
            std::uint32_t ChildIndex = 0;    File.Read(ChildIndex);
            std::uint32_t Count = 0;         File.Read(Count);

            struct snapshot_entry { xecs::scene::permanent_id m_RealId; xecs::scene::permanent_id m_ShadowId; };
            std::vector<snapshot_entry> Entries(Count);
            for (auto& E : Entries)
            {
                std::uint32_t RealId = 0, ShadowId = 0;
                File.Read(RealId);
                File.Read(ShadowId);
                E = { static_cast<xecs::scene::permanent_id>(RealId), static_cast<xecs::scene::permanent_id>(ShadowId) };
            }
            if (!e29::g_pGameMgr || Entries.empty()) return;

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return;

            // Pass 1: load each entity under its own shadow id (the path SnapshotEntry actually wrote
            // to), then immediately remap the scene's own maps from shadow -> real. Must finish for the
            // WHOLE subtree before pass 2 runs for ANY of them - cross-references inside the subtree
            // were captured using REAL ids, so they can only resolve once every sibling is registered
            // under its real id too (see this file's own top comment).
            std::vector<xecs::component::entity> Restored;
            for (auto& E : Entries)
            {
                if (auto Err = xecs::scene::details::LoadEntity(e29::g_pGameMgr->m_SceneMgr, *pScene, E.m_ShadowId); Err)
                    continue; // a single corrupted/missing snapshot must not take the whole subtree down

                auto ShadowIt = pScene->m_LocalToRuntime.find(E.m_ShadowId);
                if (ShadowIt == pScene->m_LocalToRuntime.end()) continue;
                const auto NewEntity = ShadowIt->second;

                pScene->m_LocalToRuntime.erase(ShadowIt);
                pScene->m_RuntimeToLocal.erase(NewEntity.m_Value);
                pScene->m_LocalToRuntime[E.m_RealId]        = NewEntity;
                pScene->m_RuntimeToLocal[NewEntity.m_Value] = E.m_RealId;

                // Exact inverse of DeleteEntitySubtree's own MarkEntityDeleted for this same id - see
                // this file's own top comment quoting m_PendingChanges' documented undo contract.
                pScene->m_PendingChanges[E.m_RealId].m_Deleted -= 1;

                Restored.push_back(NewEntity);
            }

            // Pass 2: resolve every reference field (parent/children/entity_reference) now that every
            // entity in the subtree is registered under its real id - same resolver shape
            // EnsureLoaded's own real scene-load path uses (xecs_scene_inline.h), reusing the scene's
            // already-populated m_ExternalToRuntime for a cross-scene target rather than rebuilding it.
            for (auto Entity : Restored)
            {
                xecs::persist::details::RemapLoadedEntityReferences(*e29::g_pGameMgr, Entity, [&](std::int64_t Encoded) noexcept -> xecs::component::entity
                {
                    if (Encoded == 0) return {};
                    if (Encoded > 0)
                    {
                        auto It = pScene->m_LocalToRuntime.find(static_cast<xecs::scene::permanent_id>(Encoded));
                        return It != pScene->m_LocalToRuntime.end() ? It->second : xecs::component::entity{};
                    }
                    const auto ExtIndex = static_cast<std::size_t>(-Encoded - 1);
                    return ExtIndex < pScene->m_ExternalToRuntime.size() ? pScene->m_ExternalToRuntime[ExtIndex] : xecs::component::entity{};
                });
            }

            // Pass 3: prefab-instance overrides need every reference resolved first - see LoadEntity's
            // own comment for why doing this any earlier crashed (confirmed already hit once this
            // session, at the real scene-load path this mirrors).
            for (auto Entity : Restored)
            {
                auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity);
                if (Details.m_pPool && Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0)
                    xecs::persist::details::ApplyPrefabInstancePropertyOverrides(*e29::g_pGameMgr, Entity);
            }

            // Root-only, matching BackupCurrenState's own root-only capture. ReparentEntityIntoFolder
            // itself always appends at the end, so re-insert at the originally-captured index
            // afterward, matching direct user request: "when the undo happens the Entity should be
            // reinserted into the tree in the same place."
            if (FolderVal != static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v))
            {
                e29::ReparentEntityIntoFolder(*pScene, static_cast<xecs::scene::permanent_id>(Id), static_cast<xecs::scene::folder_id>(FolderVal));
                if (auto FolderIt = std::ranges::find(pScene->m_Folders, static_cast<xecs::scene::folder_id>(FolderVal), &xecs::scene::folder::m_Id); FolderIt != pScene->m_Folders.end())
                {
                    if (auto EntIt = std::ranges::find(FolderIt->m_Entities, static_cast<xecs::scene::permanent_id>(Id)); EntIt != FolderIt->m_Entities.end())
                    {
                        FolderIt->m_Entities.erase(EntIt);
                        const auto InsertAt = std::min(static_cast<std::size_t>(FolderIndex), FolderIt->m_Entities.size());
                        FolderIt->m_Entities.insert(FolderIt->m_Entities.begin() + InsertAt, static_cast<xecs::scene::permanent_id>(Id));
                    }
                }
            }

            // Reverse DeleteEntitySubtree's own top-level side effect (E29_PrefabAuthoring.h): it
            // scrubbed the root out of ITS OWN parent's children.m_List, a component living on a
            // DIFFERENT entity that was never part of this subtree's own snapshot - see
            // BackupCurrenState's own comment on RootParentId for why this can't be handled generically
            // by RemapLoadedEntityReferences (that only fixes the restored root's OWN `Parent` field,
            // pointing up - not the parent's `children` field, pointing back down). Inserted at the
            // originally-captured ChildIndex rather than appended, same "restore to the same place"
            // reasoning as the folder case above.
            if (RootParentId != static_cast<std::uint32_t>(xecs::scene::invalid_permanent_id_v))
            {
                if (auto ParentIt = pScene->m_LocalToRuntime.find(static_cast<xecs::scene::permanent_id>(RootParentId)); ParentIt != pScene->m_LocalToRuntime.end())
                {
                    auto& PDetails = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(ParentIt->second);
                    if (PDetails.m_pPool && PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                    {
                        if (auto RootIt = pScene->m_LocalToRuntime.find(static_cast<xecs::scene::permanent_id>(Id)); RootIt != pScene->m_LocalToRuntime.end())
                        {
                            auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                            if (std::ranges::find(List, RootIt->second.m_Value, &xecs::component::entity::m_Value) == List.end())
                            {
                                const auto InsertAt = std::min(static_cast<std::size_t>(ChildIndex), List.size());
                                List.insert(List.begin() + InsertAt, RootIt->second);
                            }
                            e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, static_cast<xecs::scene::permanent_id>(RootParentId));
                        }
                    }
                }
            }

            if (e29::g_pState) e29::g_pState->m_bEntityInspectorDirty = true;
        }

        xcmdline::parser::handle m_hScene, m_hId;
    };
}

#endif // E29_COMMANDS_ENTITY_LIFECYCLE_H
