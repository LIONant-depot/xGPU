#ifndef E29_PREFAB_AUTHORING_H
#define E29_PREFAB_AUTHORING_H
#pragma once

// Extracted from E29_LevelSceneEditorKit.h (mechanical move, phase 2 of the kit split - see the
// umbrella file's own top comment). Prefab creation/instancing/deletion (RegisterInstantiatedSubtree
// through CreatePrefabVariantFromInstance), plus the drag-payload + drop registration that turns a
// Level-tree entity into a Prefab asset (entity_to_prefab_drop) - kept together rather than split
// further since the drop handler directly calls the authoring functions above it and shares their
// two globals (g_pGameMgr/g_pState), not a separately-reusable concern on its own. Meant to be
// included via the umbrella only, after E29_PrefabOverrides.h (AttachPrefabInstanceComponent).

namespace e29
{
    // Recursively registers every entity in a freshly-instantiated prefab subtree (Entity itself,
    // plus - if it has children - every descendant) into Scene's bookkeeping under a freshly minted
    // permanent_id each, marking each new. Shared by InstantiatePrefabIntoScene (the whole returned
    // group needs registering) and CreatePrefabFromGroupRoot (only the NEW group's children need fresh
    // ids - its root keeps a preserved one, registered separately by the caller).
    void RegisterInstantiatedSubtree(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, xecs::component::entity Entity) noexcept
    {
        const auto Id = NextFreeEntityId(Scene);
        Scene.m_LocalToRuntime[Id]              = Entity;
        Scene.m_RuntimeToLocal[Entity.m_Value]  = Id;
        GameMgr.m_SceneMgr.MarkEntityNew(SceneGuid, Id);

        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if (Details.m_pPool == nullptr) return;
        if (Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) == false) return;

        // Snapshot - registering a child only ever touches Scene's own maps, never this entity's OWN
        // children list, so a plain copy is enough (no in-place-mutation hazard to guard against here).
        auto ChildEntities = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
        for (auto Child : ChildEntities)
            RegisterInstantiatedSubtree(GameMgr, Scene, SceneGuid, Child);
    }

    // Loads PrefabGuid (if not already resident) and instantiates it into Scene under a fresh
    // permanent_id - the shared tail of both the drag-a-prefab-onto-the-scene-tree flow and (until it
    // existed) the old "+ Instantiate Prefab" button. TargetFolder (invalid = loose, rendered directly
    // at scene root) lets a drop directly onto a specific folder row land the new instance there
    // instead of always landing loose regardless of where the user actually dropped it.
    void InstantiatePrefabIntoScene(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::prefab::guid PrefabGuid, xecs::scene::folder_id TargetFolder = xecs::scene::invalid_folder_id_v) noexcept
    {
        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to load Prefab: {}", Err.getMessage()));
            return;
        }

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return;

        // bRemoveRoot=false - a multi-entity ("Scene-Prefab") root must survive instancing so it comes
        // back as a real, independent entity here; bRemoveRoot=true (the default) is for splicing a
        // prefab's CHILDREN directly onto a caller-supplied existing entity, discarding the prefab's
        // own root - not what this wants (this needs one standalone instantiated group, root included).
        auto NewRoot = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, RootIt->second, xecs::tools::empty_lambda{}, /*bRemoveRoot=*/false);

        // Registers the whole group (root + every descendant, each under a freshly minted id).
        RegisterInstantiatedSubtree(GameMgr, Scene, Scene.m_Guid, NewRoot);

        const auto RootId = Scene.m_RuntimeToLocal.at(NewRoot.m_Value);
        if (TargetFolder != xecs::scene::invalid_folder_id_v)
            ReparentEntityIntoFolder(Scene, RootId, TargetFolder);
        AttachPrefabInstanceComponent(GameMgr, Scene, RootId, NewRoot, PrefabGuid, nullptr); // brand-new entity, can't already be selected
    }

    // Recursively deletes Entity and (if it has children) its whole live descendant subtree, scrubbing
    // scene bookkeeping/folder membership for each - the "whole group" analog of a single-entity
    // delete action.
    void DeleteEntitySubtree(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, xecs::component::entity Entity) noexcept
    {
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);

        // Scrub Entity out of its own parent's children list, if it has one - otherwise the parent
        // keeps holding a dangling handle to an entity that's about to stop existing, rendering as a
        // broken/empty expandable row. Only meaningful on the TOP-LEVEL call (the row actually
        // clicked) - a recursive call's own parent is itself being deleted this same pass, so
        // scrubbing it is harmless but moot; doing it unconditionally here is simpler than threading a
        // "is this the top call" flag through the recursion.
        if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
        {
            const auto ParentEntity = Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value;
            if (ParentEntity.isValid())
            {
                auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(ParentEntity);
                if (PDetails.m_pPool && PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                {
                    auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                    std::erase_if(List, [&](auto& E) noexcept { return E.m_Value == Entity.m_Value; });
                    if (auto ParentIt = Scene.m_RuntimeToLocal.find(ParentEntity.m_Value); ParentIt != Scene.m_RuntimeToLocal.end())
                        GameMgr.m_SceneMgr.MarkEntityDirty(SceneGuid, ParentIt->second);
                }
            }
        }

        if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
        {
            auto ChildEntities = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
            for (auto Child : ChildEntities)
                DeleteEntitySubtree(GameMgr, Scene, SceneGuid, Child);
        }

        if (auto It = Scene.m_RuntimeToLocal.find(Entity.m_Value); It != Scene.m_RuntimeToLocal.end())
        {
            const auto Id = It->second;
            Scene.m_RuntimeToLocal.erase(It);
            Scene.m_LocalToRuntime.erase(Id);
            GameMgr.m_SceneMgr.MarkEntityDeleted(SceneGuid, Id);
            ReparentEntityIntoFolder(Scene, Id, xecs::scene::invalid_folder_id_v);
        }

        auto E = Entity;
        GameMgr.DeleteEntity(E);
    }

    // The Level tree's "Make Prefab" action - the multi-entity-aware counterpart of dragging a single
    // entity onto the asset browser (entity_to_prefab_drop, below). ClickedId is the row the context
    // menu/drag was started on; if it's part of a live multi-selection (2+ entities, all in this same
    // scene), the WHOLE selection becomes the group, otherwise just ClickedId alone.
    //
    // Root selection: a single selected entity becomes the root directly (covers "no children" and
    // "already has children" alike - CreatePrefabFromEntity/CloneEntityIntoPrefabGroup pulls in
    // children automatically). Multiple selected entities compute their "top-level" subset (those
    // whose parent, if any, isn't ALSO selected): exactly one top-level entity means the user
    // multi-selected an existing subtree - use it as the real root directly; otherwise (multiple
    // disjoint top-level entities) a synthetic root (Name + Children only) is created and every
    // top-level entity is reparented under it.
    xecs::component::entity DetermineGroupRoot(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, editor_state& State, xecs::scene::permanent_id ClickedId) noexcept
    {
        std::vector<xecs::scene::permanent_id> SelectedIds;
        if (State.m_MultiSelectScene == SceneGuid && State.m_MultiSelectedEntityIds.size() > 1 && State.m_MultiSelectedEntityIds.contains(ClickedId))
            SelectedIds = State.m_MultiSelectOrder; // click order, not m_MultiSelectedEntityIds' own unordered iteration
        else
            SelectedIds.push_back(ClickedId);

        std::vector<xecs::component::entity> SelectedEntities;
        for (auto Id : SelectedIds)
            if (auto It = Scene.m_LocalToRuntime.find(Id); It != Scene.m_LocalToRuntime.end())
                SelectedEntities.push_back(It->second);
        std::printf("[MakePrefab] DetermineGroupRoot: %zu selected id(s), %zu resolved live entity(ies)\n", SelectedIds.size(), SelectedEntities.size());
        std::fflush(stdout);
        if (SelectedEntities.empty()) return {};

        if (SelectedEntities.size() == 1)
            return SelectedEntities.front();

        auto IsSelected = [&](xecs::component::entity E) noexcept
        {
            return std::find_if(SelectedEntities.begin(), SelectedEntities.end(), [&](auto& S) noexcept { return S.m_Value == E.m_Value; }) != SelectedEntities.end();
        };

        std::vector<xecs::component::entity> TopLevel;
        for (auto E : SelectedEntities)
        {
            auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(E);
            bool bParentSelected = false;
            if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
                bParentSelected = IsSelected(Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value);
            if (!bParentSelected) TopLevel.push_back(E);
        }
        std::printf("[MakePrefab] DetermineGroupRoot: %zu top-level entity(ies) among the selection\n", TopLevel.size());
        std::fflush(stdout);

        if (TopLevel.size() == 1)
            return TopLevel.front();

        // The synthetic root is brand new, so it has no history of its own to fall back on - without
        // this, it always starts loose at scene root, even when the entities it's about to wrap all
        // came from the SAME real folder or the SAME real
        // scene-hierarchy parent. A real PARENT wins over folder membership - matching how "an entity
        // with a parent is never ALSO in a folder" already works everywhere else in this tree - falling
        // back to whichever folder (if any) the FIRST top-level entity was in when there's no external
        // parent, and using ITS choice when several top-level entities disagree.
        xecs::component::entity InheritedParent;
        if (auto& FirstDetails = GameMgr.m_ComponentMgr.getEntityDetails(TopLevel.front()); FirstDetails.m_pPool && FirstDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
            InheritedParent = FirstDetails.m_pPool->getComponent<xecs::component::parent>(FirstDetails.m_PoolIndex).m_Value;
        const auto InheritedFolderId = InheritedParent.isValid() ? xecs::scene::invalid_folder_id_v : FindFolderContaining(Scene, Scene.m_RuntimeToLocal.at(TopLevel.front().m_Value));

        auto& RootArchetype = GameMgr.getOrCreateArchetype<e29::name, xecs::component::children>();
        auto  Root          = RootArchetype.CreateEntity([&](e29::name& Name) noexcept { Name.m_Value = "Prefab Root"; });

        if (InheritedParent.isValid())
        {
            std::array Add{ &xecs::component::type::info_v<xecs::component::parent> };
            Root = GameMgr.AddOrRemoveComponents(Root, Add, {});
            auto& RootPDetails = GameMgr.m_ComponentMgr.getEntityDetails(Root);
            RootPDetails.m_pPool->getComponent<xecs::component::parent>(RootPDetails.m_PoolIndex).m_Value = InheritedParent;

            // Splice Root into whatever position the FIRST top-level entity held in ITS parent's own
            // children list, replacing it - the parent's list otherwise keeps pointing at that entity's
            // stale handle (about to be swapped for a fresh one below) instead of the new wrapper root.
            // Every OTHER top-level entity that ALSO happened to share this same external parent
            // (multi-selecting 2+ disjoint entities that are siblings under one real parent) must be
            // ERASED from this list entirely, not merely left alone - Root already represents the
            // whole group in the one spliced slot, and each of those other entities is ALSO about to
            // be migrated to a new handle by the reparent loop below, so leaving its OLD entry here
            // would be both a duplicate membership (appears under InheritedParent AND under Root) and
            // a dangling one (pointing at a handle the migration is about to invalidate).
            auto& ExtParentDetails = GameMgr.m_ComponentMgr.getEntityDetails(InheritedParent);
            if (ExtParentDetails.m_pPool && ExtParentDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
            {
                auto& ExtChildren = ExtParentDetails.m_pPool->getComponent<xecs::component::children>(ExtParentDetails.m_PoolIndex).m_List;
                bool bSplicedRoot = false;
                std::erase_if(ExtChildren, [&](auto& C) noexcept
                {
                    const bool bIsTopLevelMember = std::find_if(TopLevel.begin(), TopLevel.end(), [&](auto& T) noexcept { return T.m_Value == C.m_Value; }) != TopLevel.end();
                    if (!bIsTopLevelMember) return false;
                    if (!bSplicedRoot) { C = Root; bSplicedRoot = true; return false; }
                    return true;
                });
            }
        }

        const auto RootId = NextFreeEntityId(Scene);
        Scene.m_LocalToRuntime[RootId]        = Root;
        Scene.m_RuntimeToLocal[Root.m_Value]  = RootId;
        GameMgr.m_SceneMgr.MarkEntityNew(SceneGuid, RootId);
        if (InheritedFolderId != xecs::scene::invalid_folder_id_v)
            ReparentEntityIntoFolder(Scene, RootId, InheritedFolderId);

        for (auto E : TopLevel)
        {
            const auto OldId = Scene.m_RuntimeToLocal.at(E.m_Value);

            std::array Add{ &xecs::component::type::info_v<xecs::component::parent> };
            auto NewE = GameMgr.AddOrRemoveComponents(E, Add, {});
            auto& NewDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewE);
            NewDetails.m_pPool->getComponent<xecs::component::parent>(NewDetails.m_PoolIndex).m_Value = Root;

            Scene.m_RuntimeToLocal.erase(E.m_Value);
            Scene.m_LocalToRuntime[OldId]         = NewE;
            Scene.m_RuntimeToLocal[NewE.m_Value]  = OldId;
            GameMgr.m_SceneMgr.MarkEntityDirty(SceneGuid, OldId);

            auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(Root);
            RootDetails.m_pPool->getComponent<xecs::component::children>(RootDetails.m_PoolIndex).m_List.push_back(NewE);

            if (State.m_SelectedEntityId == OldId)
            {
                State.m_SelectedEntity        = NewE;
                State.m_bEntityInspectorDirty = true;
            }

            // Entities with a parent are excluded from folder membership entirely (rendered via their
            // parent's own row instead) - scrub whatever folder this entity was in.
            ReparentEntityIntoFolder(Scene, OldId, xecs::scene::invalid_folder_id_v);
        }

        return Root;
    }

    // Step 2: given a resolved group root (a real, live entity - either the single dragged/clicked
    // entity, an existing subtree's own root, or DetermineGroupRoot's synthetic one), creates the
    // Prefab asset (at LibraryGUID/ParentGUID - the caller's own drop target) and converts the
    // original live group into an instance of it, generalizing the single-entity "drag out becomes an
    // instance" behavior.
    xresource::full_guid CreatePrefabFromGroupRoot(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, editor_state* pState, e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, xecs::component::entity Root) noexcept
    {
        // If Root already had a parent in the live scene (e.g. a single child entity that's part of
        // some OTHER, unrelated hierarchy, or a whole existing subtree being grouped), that positional
        // link is NOT part of what gets persisted (a prefab root never carries its own parent) -
        // captured here so the freshly-instantiated root can be spliced back into the exact same
        // position afterward, rather than unexpectedly falling out to scene-root.
        xecs::component::entity OriginalParent;
        if (auto& RD = GameMgr.m_ComponentMgr.getEntityDetails(Root); RD.m_pPool && RD.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
            OriginalParent = RD.m_pPool->getComponent<xecs::component::parent>(RD.m_PoolIndex).m_Value;

        const auto RootId           = Scene.m_RuntimeToLocal.at(Root.m_Value);
        const bool bRootWasSelected = pState && (pState->m_SelectedEntityId == RootId);
        const auto StaleRootValue   = Root.m_Value; // Root's own OLD live handle - about to be deleted; only ever compared, never dereferenced, below

        // Folder membership is keyed by RootId (a permanent_id, preserved across this whole
        // conversion) rather than by live entity handle, so in principle it wouldn't need capturing -
        // except DeleteEntitySubtree (below) explicitly scrubs it as part of deleting the OLD live
        // root (ReparentEntityIntoFolder(..., invalid_folder_id_v)), since from ITS point of view the
        // entity is simply being removed. Without capturing and restoring it here, RootId would render
        // loose at scene root after re-registration instead of back in its original folder.
        const auto OriginalFolderId = FindFolderContaining(Scene, RootId);

        std::string Name = "Prefab";
        if (auto& D = GameMgr.m_ComponentMgr.getEntityDetails(Root); D.m_pPool && D.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<e29::name>.m_BitID))
            Name = D.m_pPool->getComponent<e29::name>(D.m_PoolIndex).m_Value;

        const xresource::full_guid NewGuid   = AssetMgr.NewAsset(LibraryGUID, xresource::full_guid{ {}, xecs::prefab::type_guid_v }, ParentGUID, Name);
        const xecs::prefab::guid   PrefabGuid = NewGuid;

        std::printf("[MakePrefab] CreatePrefabFromGroupRoot: RootId=%u Name='%s' - cloning into prefab\n", RootId, Name.c_str());
        std::fflush(stdout);

        GameMgr.m_PrefabMgr.CreatePrefabFromEntity(Root, PrefabGuid);
        if (auto Err = GameMgr.m_PrefabMgr.Save(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to save new Prefab: {}", Err.getMessage()));
            return {};
        }

        // Convert the original live group into an instance of the new prefab: delete the original
        // root+descendants, instantiate a fresh copy, splice it back into whatever OriginalParent
        // held, then register it under RootId's preserved permanent_id (so scene bookkeeping/
        // selection keep referencing "the same" entity) - every child gets a freshly minted id
        // instead (they're new scene entities, never existed as "an instance" before).
        DeleteEntitySubtree(GameMgr, Scene, SceneGuid, Root);

        auto NewRoot = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, GameMgr.m_PrefabMgr.m_PrefabList.at(PrefabGuid.m_Instance.m_Value), xecs::tools::empty_lambda{}, /*bRemoveRoot=*/false);
        std::printf("[MakePrefab] CreatePrefabFromGroupRoot: instantiated fresh copy, NewRoot.isValid=%d NewRoot.isZombie=%d\n", NewRoot.isValid(), NewRoot.isZombie());
        std::fflush(stdout);

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
            std::printf("[MakePrefab] CreatePrefabFromGroupRoot: NewRoot has %zu child(ren) to register\n", ChildEntities.size());
            std::fflush(stdout);
            for (auto Child : ChildEntities)
                RegisterInstantiatedSubtree(GameMgr, Scene, SceneGuid, Child);
        }

        Scene.m_LocalToRuntime[RootId]           = NewRoot;
        Scene.m_RuntimeToLocal[NewRoot.m_Value]  = RootId;

        // Restore RootId's folder membership, scrubbed by DeleteEntitySubtree above - but only when
        // the root did NOT get a parent restored (an entity with a parent is never ALSO placed via
        // folder membership - the parent becomes the folder, per this whole tree's own convention).
        if (false == OriginalParent.isValid())
            ReparentEntityIntoFolder(Scene, RootId, OriginalFolderId);

        AttachPrefabInstanceComponent(GameMgr, Scene, RootId, NewRoot, PrefabGuid, pState);
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
                // The previously-selected entity might have been one of the OTHER group members (a
                // non-root one). Non-root members get deleted and replaced with a FRESH entity under
                // a FRESH id (RegisterInstantiatedSubtree), so there's no principled "same identity"
                // to preserve for them the way the root's own preserved RootId gives one - if the
                // id/handle pairing no longer matches what's actually live, the safe move is to clear
                // the selection rather than leave pState->m_SelectedEntity holding a stale handle into
                // an entity that DeleteEntitySubtree already destroyed (a stale handle used later
                // trips xECS's own generation/validation assert).
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

        std::printf("[MakePrefab] CreatePrefabFromGroupRoot: done, RootId=%u still resident=%d\n", RootId, Scene.m_LocalToRuntime.contains(RootId));
        std::fflush(stdout);

        return NewGuid;
    }

    // Payload for dragging a scene entity onto an asset-browser folder to create a Prefab from it -
    // registered against e10::external_drop_registration_base (see E10_AssetBrowser.h) so the browser
    // can accept it without knowing anything about xECS/scenes. Carries the scene guid + the entity's
    // scene-local permanent_id rather than a live xecs::component::entity handle, since the handle
    // itself is only guaranteed valid for the frame it was captured in - re-resolving it through the
    // scene's own maps at drop time is what makes this safe across the drag's lifetime.
    struct entity_drag_payload_t
    {
        xecs::scene::guid          m_SceneGuid;
        xecs::scene::permanent_id  m_Id;
    };

    // Set once, near the top of the owning example's setup, so entity_to_prefab_drop::OnDrop (a
    // static, globally-registered object constructed long before GameMgr/State exist) can reach the
    // live editor state at drop time. Matches this codebase's existing convention for singleton editor
    // state (e10::g_LibMgr, xresource::g_Mgr, e29::g_AssetBrowserPopup) - an example built on this kit
    // only ever runs one instance of itself, so this isn't introducing a new kind of assumption.
    inline xecs::game_mgr::instance* g_pGameMgr = nullptr;
    inline editor_state*             g_pState   = nullptr;

    // Unity's own "Prefab Variant" fast path: dragging a SINGLE existing prefab instance (no other
    // entity in the active selection) into the asset browser creates a variant WITHOUT touching the
    // scene object's own live identity - Unity re-points that same GameObject's prefab connection at
    // the new variant rather than deleting and recreating it. Deliberately narrower than
    // CreatePrefabFromGroupRoot (which always deletes+recreates): a multi-select group has no single
    // existing identity to preserve in the first place (a brand-new synthetic root is minted either
    // way), and a PLAIN entity (never instanced) has no existing prefab connection to re-point - both
    // of those keep going through the general path unchanged.
    xresource::full_guid CreatePrefabVariantFromInstance(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::component::entity Entity, e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID) noexcept
    {
        std::string Name = "Prefab";
        if (auto& D = GameMgr.m_ComponentMgr.getEntityDetails(Entity); D.m_pPool && D.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<e29::name>) >= 0)
            Name = D.m_pPool->getComponent<e29::name>(D.m_PoolIndex).m_Value;

        const xresource::full_guid NewGuid   = AssetMgr.NewAsset(LibraryGUID, xresource::full_guid{ {}, xecs::prefab::type_guid_v }, ParentGUID, Name);
        const xecs::prefab::guid   PrefabGuid = NewGuid;

        std::printf("[MakePrefab] CreatePrefabVariantFromInstance: Id=%u Name='%s' - capturing into a variant, live entity untouched\n", Id, Name.c_str());
        std::fflush(stdout);

        GameMgr.m_PrefabMgr.CreatePrefabFromEntity(Entity, PrefabGuid);
        if (auto Err = GameMgr.m_PrefabMgr.Save(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to save new Prefab: {}", Err.getMessage()));
            return {};
        }

        // Re-point the SAME live entity's own bookkeeping at the new variant - no deletion, no fresh
        // instantiation needed: this entity's current data IS already exactly what a fresh instance of
        // the new variant looks like, since it's what the variant was just captured FROM. Overrides are
        // cleared (matching AttachPrefabInstanceComponent's own reasoning) since they were computed
        // relative to whatever this entity pointed at BEFORE - a save recomputes them fresh regardless.
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& PI = Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
        PI.m_PrefabInstance = PrefabGuid;
        PI.m_lComponents.clear();
        PI.m_ComponentDiffs.clear();
        GameMgr.m_SceneMgr.MarkEntityDirty(Scene.m_Guid, Id);

        return NewGuid;
    }

    struct entity_to_prefab_drop final : e10::external_drop_registration_base
    {
        entity_to_prefab_drop() noexcept : e10::external_drop_registration_base{ "E29_ENTITY_DRAG" } {}

        xresource::full_guid OnDrop(e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, const void* pData, std::size_t Size) const noexcept override
        {
            if (Size != sizeof(entity_drag_payload_t) || g_pGameMgr == nullptr) return {};
            auto& Payload = *reinterpret_cast<const entity_drag_payload_t*>(pData);

            auto* pScene = g_pGameMgr->m_SceneMgr.Find(Payload.m_SceneGuid);
            if (pScene == nullptr) return {};

            auto SourceIt = pScene->m_LocalToRuntime.find(Payload.m_Id);
            if (SourceIt == pScene->m_LocalToRuntime.end()) return {};

            // Single-instance Prefab Variant fast path - see CreatePrefabVariantFromInstance's own
            // comment. Only when NOT part of a real (2+) active multi-selection, and only when the
            // dragged entity already carries editor::prefab_instance.
            const bool bIsMultiSelect = g_pState && g_pState->m_MultiSelectScene == Payload.m_SceneGuid && g_pState->m_MultiSelectedEntityIds.size() > 1 && g_pState->m_MultiSelectedEntityIds.contains(Payload.m_Id);
            if (!bIsMultiSelect)
            {
                auto& SourceDetails = g_pGameMgr->m_ComponentMgr.getEntityDetails(SourceIt->second);
                if (SourceDetails.m_pPool && SourceDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0)
                    return CreatePrefabVariantFromInstance(*g_pGameMgr, *pScene, Payload.m_Id, SourceIt->second, AssetMgr, LibraryGUID, ParentGUID);
            }

            // If the dragged entity is part of an active multi-selection (2+, ctrl-clicked in the
            // Level tree, this same scene), the WHOLE selection becomes the prefab's group - this is
            // the primary way to make a multi-entity prefab (drag-and-drop, exactly like the existing
            // single-entity flow, just generalized): ctrl-click to build a selection, then drag any
            // one of the selected rows onto the asset browser, same as before. A single dragged entity
            // with no active multi-selection behaves exactly as it always has.
            auto Root = g_pState ? DetermineGroupRoot(*g_pGameMgr, *pScene, Payload.m_SceneGuid, *g_pState, Payload.m_Id)
                                 : pScene->m_LocalToRuntime.find(Payload.m_Id)->second;
            if (Root.isValid() == false) return {};

            return CreatePrefabFromGroupRoot(*g_pGameMgr, *pScene, Payload.m_SceneGuid, g_pState, AssetMgr, LibraryGUID, ParentGUID, Root);
        }
    };
    inline static entity_to_prefab_drop g_EntityToPrefabDrop{};

} // namespace e29

#endif // E29_PREFAB_AUTHORING_H
