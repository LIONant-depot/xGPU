#ifndef E29_PREFAB_OVERRIDES_H
#define E29_PREFAB_OVERRIDES_H
#pragma once

// Extracted from E29_LevelSceneEditorKit.h (mechanical move, phase 2 of the kit split - see the
// umbrella file's own top comment). Prefab-instance lookup, override-entry bookkeeping, and the
// entity-reference-component resolver the Entity Properties inspector uses for both (adjacent,
// small, and used by the same override-rendering code path - kept here rather than split out
// further, per "don't explode into too many files"). Meant to be included via the umbrella only.

namespace e29
{
    // Returns the entity's live prefab_instance component, or nullptr if it isn't a prefab instance
    // (a plain entity never has this component).
    xecs::editor::prefab_instance* FindPrefabInstance(xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity) noexcept
    {
        if (Entity.isValid() == false) return nullptr;
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if (Details.m_pPool == nullptr) return nullptr;
        // findIndexComponentFromInfo, not getComponentBits().getBit() - see
        // [[xecs_getbit_vs_findindexcomponentfrominfo]] (a runtime-assigned component bit checked this
        // way can read as absent/invalid even when the component is genuinely present).
        if (Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) < 0)
            return nullptr;
        return &Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
    }

    // Result of walking UP from some entity to find the prefab instance it's structurally part of -
    // itself if it carries prefab_instance directly, else the nearest ancestor (via parent) that
    // does, recording the child-index path down from that ancestor to the original entity along the
    // way (see xecs::editor::prefab_component_override::m_MemberPath's own comment for why a path,
    // not a stored id). Stops at the first prefab_instance found walking up - never crosses further
    // out past a nested instance's own root, matching this session's existing nested-override scope.
    struct prefab_instance_context
    {
        xecs::editor::prefab_instance* m_pPI = nullptr;
        xecs::component::entity        m_RootEntity{};
        std::vector<std::uint32_t>     m_MemberPath;
    };

    prefab_instance_context FindContainingPrefabInstance(xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity) noexcept
    {
        prefab_instance_context Ctx;
        if (Entity.isValid() == false) return Ctx;

        std::vector<std::uint32_t> ReversePath;
        auto Cur = Entity;
        for(;;)
        {
            if (auto* pPI = FindPrefabInstance(GameMgr, Cur))
            {
                Ctx.m_pPI       = pPI;
                Ctx.m_RootEntity = Cur;
                Ctx.m_MemberPath.assign(ReversePath.rbegin(), ReversePath.rend());
                return Ctx;
            }

            auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Cur);
            if (Details.m_pPool == nullptr) return {};
            const auto iParentType = Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::parent>);
            if (iParentType < 0) return {};   // no parent, and not a PI itself - not part of any instance

            const auto ParentEntity = Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value;
            if (ParentEntity.isValid() == false) return {};

            auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(ParentEntity);
            if (PDetails.m_pPool == nullptr) return {};
            const auto iChildrenType = PDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::children>);
            if (iChildrenType < 0) return {};

            auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
            auto  It    = std::find_if(List.begin(), List.end(), [&](auto& E) noexcept { return E.m_Value == Cur.m_Value; });
            if (It == List.end()) return {};

            ReversePath.push_back(static_cast<std::uint32_t>(std::distance(List.begin(), It)));
            Cur = ParentEntity;
        }
    }

    // Resolves a live entity handle of UNKNOWN owning scene (all the inspector ever has for an
    // xecs::component::entity_reference field) into a friendly label + which open scene owns it, by
    // scanning every currently-open scene's m_RuntimeToLocal - same label logic the Level tree's own
    // entity rows already use (Name component if present, else "Entity #Id"), just without a SceneGuid
    // known up front the way a tree row already has one. Only scenes the user has actually opened are
    // searched - a reference into a scene nobody opened this session simply can't be resolved to a
    // live handle yet (matches how the reference itself only round-trips through save/load, not
    // through any live lookup that would need every scene loaded just to inspect one entity).
    bool ResolveEntityReference(xecs::game_mgr::instance& GameMgr, editor_state& State, xecs::component::entity Entity, std::string& OutLabel, xecs::scene::guid& OutSceneGuid) noexcept
    {
        if (Entity.isValid() == false) return false;

        for (auto& SceneGuid : State.m_OpenScenes)
        {
            auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid);
            if (pScene == nullptr) continue;

            auto It = pScene->m_RuntimeToLocal.find(Entity.m_Value);
            if (It == pScene->m_RuntimeToLocal.end()) continue;

            const auto Id = It->second;
            OutLabel = std::format("Entity #{}", Id);
            if (auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool)
            {
                auto Bits = Details.m_pPool->m_pArchetype->getComponentBits();
                if (Bits.getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                    OutLabel = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;
            }
            std::string SceneLabel;
            RemapGUIDToString(SceneLabel, xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type });
            OutLabel += std::format(" ({})", SceneLabel);
            OutSceneGuid = SceneGuid;
            return true;
        }
        return false;
    }

    // Components that xECS itself attaches/manages internally (never meaningful to add/remove/edit
    // by hand): entity is the identity itself; parent/children carry raw xecs::component::entity
    // references, which the shared xproperty inspector has no rendering style for at all (asserts -
    // "UNHANDLED ATOMIC STYLE: TypeName='entity'" - the moment one is appended); ref_count/
    // share_filter/share_as_data_exclusive_tag are share-component bookkeeping; prefab::tag/root only
    // ever live on a prefab's own root entity (in mgr::m_PrefabList), never on a scene entity; and
    // editor::prefab_instance is this editor's own override-tracking bookkeeping, edited only through
    // the dedicated prefab-override UI. Centralized here so Add/Remove Component and the inspector
    // rebuild loop can't independently drift out of sync on this list.
    bool IsInternalComponent(const xecs::component::type::info* pInfo) noexcept
    {
        return pInfo == &xecs::component::type::info_v<xecs::component::entity>
            || pInfo == &xecs::component::type::info_v<xecs::component::parent>
            || pInfo == &xecs::component::type::info_v<xecs::component::children>
            || pInfo == &xecs::component::type::info_v<xecs::component::ref_count>
            || pInfo == &xecs::component::type::info_v<xecs::component::share_filter>
            || pInfo == &xecs::component::type::info_v<xecs::component::share_as_data_exclusive_tag>
            || pInfo == &xecs::component::type::info_v<xecs::prefab::tag>
            || pInfo == &xecs::component::type::info_v<xecs::prefab::root>
            || pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance>;
    }

    // Finds the override-tracking entry for a given (component type, group member) pair on a prefab
    // instance, creating one (as OVERRIDES) if none exists yet - fixes the old, never-finished
    // design's bug of always appending a new entry even when one already exists. MemberPath empty
    // means the prefab_instance-carrying entity itself (the only case that existed before
    // multi-entity groups); non-empty addresses a plain child/nested-instance-root member instead -
    // see prefab_component_override::m_MemberPath's own comment.
    xecs::editor::prefab_component_override& FindOrCreateOverrideEntry(xecs::editor::prefab_instance& PI, std::uint64_t ComponentTypeGuidValue, std::span<const std::uint32_t> MemberPath) noexcept
    {
        for (auto& C : PI.m_lComponents)
            if (C.m_ComponentTypeGuid == ComponentTypeGuidValue && std::ranges::equal(C.m_MemberPath, MemberPath)) return C;

        PI.m_lComponents.push_back(xecs::editor::prefab_component_override
        { .m_ComponentTypeGuid = ComponentTypeGuidValue
        , .m_MemberPath        = std::vector<std::uint32_t>(MemberPath.begin(), MemberPath.end())
        , .m_PropertyOverrides = {}
        });
        return PI.m_lComponents.back();
    }

    // Attaches a fresh (no overrides yet) prefab_instance component pointed at PrefabGuid onto
    // Entity, and re-registers the (possibly archetype-migrated - AddOrRemoveComponents returns a new
    // entity handle) result into Scene's local/runtime maps under Id. The common tail end of both
    // "instantiate a prefab into a scene" (Entity is brand new, Id not yet in the maps - the erase
    // below is just a harmless no-op) and "the entity just dragged out becomes an instance of the
    // prefab created from it" (Entity/Id already exist in the maps under the same Id).
    //
    // pState (nullable - InstantiatePrefabIntoScene's brand-new entity can never already be selected,
    // so it passes nullptr) matters for the OTHER caller, entity_to_prefab_drop::OnDrop: if the
    // dragged-out entity happened to be the one currently shown in the Entity Properties panel, this
    // migration invalidates State.m_SelectedEntity (a stale handle) AND every pool-memory address the
    // xproperty inspector cached for it (entity_inspector_bridge::m_ComponentMap, populated by the
    // m_bEntityInspectorDirty rebuild block in RenderEntityPropertiesPanel) - exactly like the entity
    // handle "Add Component"/"Remove Component" migrate, except NEITHER of those refreshed State nor
    // set the dirty flag afterward for THIS migration, since this function used to have no idea a
    // selection even existed. Without this fix, editing a property afterward through the still-
    // displayed, now-stale inspector hands OnPropertyChanged a dangling/reused pool address via
    // Cmd.m_pClassObject - a plausible root cause for "override a property, then Save -> invalidated
    // vector iterator" style corruption that only manifests through real UI interaction, never
    // through headless, data-only testing (which never drives State/the component map at all).
    void AttachPrefabInstanceComponent(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::component::entity Entity, xecs::prefab::guid PrefabGuid, editor_state* pState) noexcept
    {
        const bool bWasSelected = pState != nullptr && pState->m_SelectedEntity.m_Value == Entity.m_Value;

        xecs::component::entity NewEntity;

        // If Entity already carries editor::prefab_instance, it's the root of a NESTED prefab
        // instance (this prefab's own root wraps a DIFFERENT prefab - the "variant" case): the
        // engine's own instantiation already gave it correct live DATA (re-derived from the INNER
        // prefab's current state, inner-relative overrides applied), but its PI still identifies as
        // an instance of the INNER prefab, not the OUTER one the user actually just placed - without
        // stamping over it here, the scene entity is silently tracked under the wrong prefab guid
        // (asset-browser "reveal", future re-instantiation-of-this-prefab bookkeeping, etc. would all
        // point at the inner prefab instead of what was dragged in). No AddOrRemoveComponents needed
        // (the bit is already set, no archetype migration) - just overwrite the existing component's
        // fields directly. m_lComponents/m_ComponentDiffs are reset to empty rather than left as-is:
        // they were computed relative to the INNER prefab and would misleadingly describe "overrides"
        // relative to the wrong base; a save recomputes them fresh anyway
        // (RefreshPrefabInstanceOverlayRecord), so this loses no data - it only avoids a stale,
        // wrongly-labeled "differs from prefab" indicator in the Properties panel between placement
        // and the next save. Checked via findIndexComponentFromInfo (matches the per-component lookup
        // SaveGroupMember/LoadGroupMember already use), not getComponentBits().getBit() - see
        // [[xecs_getbit_vs_findindexcomponentfrominfo]].
        auto& ExistingDetails = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if( ExistingDetails.m_pPool && ExistingDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0 )
        {
            auto& PI = ExistingDetails.m_pPool->getComponent<xecs::editor::prefab_instance>(ExistingDetails.m_PoolIndex);
            PI.m_PrefabInstance = PrefabGuid;
            PI.m_lComponents.clear();
            PI.m_ComponentDiffs.clear();
            NewEntity = Entity;
        }
        else
        {
            std::array Add{ &xecs::component::type::info_v<xecs::editor::prefab_instance> };
            NewEntity = GameMgr.AddOrRemoveComponents(Entity, Add, {});
            auto& NewDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            NewDetails.m_pPool->getComponent<xecs::editor::prefab_instance>(NewDetails.m_PoolIndex).m_PrefabInstance = PrefabGuid;
        }

        Scene.m_RuntimeToLocal.erase(Entity.m_Value);
        Scene.m_LocalToRuntime[Id]               = NewEntity;
        Scene.m_RuntimeToLocal[NewEntity.m_Value] = Id;

        if (bWasSelected)
        {
            pState->m_SelectedEntity        = NewEntity;
            pState->m_bEntityInspectorDirty = true;
        }
    }

} // namespace e29

#endif // E29_PREFAB_OVERRIDES_H
