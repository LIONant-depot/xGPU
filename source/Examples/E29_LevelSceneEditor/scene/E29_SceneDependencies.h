#pragma once

// Scene dependency graph helpers: cycle guard, reachability, lost-parent and cross-scene reference handling.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    // Reads ParentScenes for a scene that may not be loaded - prefers the live instance, else the
    // on-disk Descriptor.txt. Used by cycle / remove-reachability walks so an unloaded dependency
    // still participates in the transitive check (authoring must not wait for EnsureLoaded).
    inline void ReadParentScenesList(xecs::game_mgr::instance& GameMgr, xecs::scene::guid SceneGuid, std::vector<xecs::scene::guid>& Out) noexcept
    {
        Out.clear();
        if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
        {
            Out = pScene->m_ParentScenes;
            return;
        }
        const auto Path = xecs::scene::details::DescriptorPath(GameMgr.m_SceneMgr, SceneGuid);
        xecs::scene::descriptor Descriptor;
        xproperty::settings::context Context{};
        if (auto Err = Descriptor.Serialize(true, Path, Context); Err)
            return;
        Out = std::move(Descriptor.m_ParentScenes);
    }

    // Transitive ParentScenes closure starting from Roots (BFS). If Skip is non-empty, that guid is
    // never expanded as a node (used to simulate "remove this direct parent" when computing Lost).
    inline void CollectTransitiveParents(xecs::game_mgr::instance& GameMgr, const std::vector<xecs::scene::guid>& Roots, xecs::scene::guid Skip, std::vector<xecs::scene::guid>& Out) noexcept
    {
        Out.clear();
        std::vector<xecs::scene::guid> Stack = Roots;
        while (!Stack.empty())
        {
            const auto Cur = Stack.back();
            Stack.pop_back();
            if (Cur.m_Instance.m_Value != 0 && Cur == Skip) continue;
            if (std::find(Out.begin(), Out.end(), Cur) != Out.end()) continue;
            Out.push_back(Cur);
            std::vector<xecs::scene::guid> Parents;
            ReadParentScenesList(GameMgr, Cur, Parents);
            for (auto& P : Parents) Stack.push_back(P);
        }
    }

    // Would adding "NewDependency" to Candidate's own m_ParentScenes close a cycle in the scene
    // dependency graph? True iff Candidate is already (transitively) reachable FROM NewDependency by
    // walking m_ParentScenes edges. Walks live ParentScenes when loaded, else Descriptor.txt on disk.
    bool WouldCreateDependencyCycle(xecs::game_mgr::instance& GameMgr, xecs::scene::guid Candidate, xecs::scene::guid NewDependency) noexcept
    {
        if (Candidate == NewDependency) return true;
        std::vector<xecs::scene::guid> Reachable;
        CollectTransitiveParents(GameMgr, std::vector<xecs::scene::guid>{ NewDependency }, xecs::scene::guid{}, Reachable);
        return std::find(Reachable.begin(), Reachable.end(), Candidate) != Reachable.end();
    }

    // Scenes that become unreachable from Owner once DirectParent is removed from Owner.m_ParentScenes
    // (DirectParent itself plus anything only reachable through it).
    inline void CollectLostParentsOnRemove(xecs::game_mgr::instance& GameMgr, const xecs::scene::instance& Owner, xecs::scene::guid DirectParent, std::vector<xecs::scene::guid>& OutLost) noexcept
    {
        std::vector<xecs::scene::guid> Before, After;
        CollectTransitiveParents(GameMgr, Owner.m_ParentScenes, xecs::scene::guid{}, Before);
        CollectTransitiveParents(GameMgr, Owner.m_ParentScenes, DirectParent, After);
        OutLost.clear();
        for (auto& G : Before)
            if (std::find(After.begin(), After.end(), G) == After.end())
                OutLost.push_back(G);
    }

    // Read-only walk of every live entity-typed reference field on one entity (same coverage as
    // RemapLoadedEntityReferences / SaveEntity's reference pass). Uses findIndexComponentFromInfo
    // (not InSequence) so a mismatched DataSpan/pool order cannot OOB into m_pComponent[-1].
    template<typename T_FN>
    inline void ForEachLiveEntityReference(xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity, T_FN&& Fn) noexcept
    {
        auto& IDetails = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if (!IDetails.m_pPool) return;
        auto& Archetype = *IDetails.m_pPool->m_pArchetype;
        auto  DataSpan  = Archetype.getDataComponentInfos();

        std::vector<xecs::component::entity*> References;
        for (auto pInfo : DataSpan)
        {
            if (pInfo->m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES
             || xecs::component::type::IsComponentType<xecs::component::entity>(pInfo))
                continue;

            const auto iType = IDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
            if (iType < 0) continue;
            auto* pData = &IDetails.m_pPool->m_pComponent[iType][IDetails.m_PoolIndex.m_Value * pInfo->m_Size];

            if (pInfo->m_ReferenceMode == xecs::component::type::reference_mode::BY_FUNCTION)
            {
                pInfo->m_pReportReferencesFn(References, pData);
                for (auto pRef : References)
                    if (pRef && pRef->isValid()) Fn(*pRef);
                References.clear();
            }
            else if (pInfo->m_pPropertyTable)
            {
                xproperty::settings::context Context{};
                xproperty::sprop::collector(pData, *pInfo->m_pPropertyTable, Context, [&](const char*, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    if (Data.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v)
                    {
                        auto& E = Data.get<xecs::component::entity>();
                        if (E.isValid()) Fn(E);
                    }
                });
            }
        }
    }
    inline xecs::scene::guid FindOwningSceneGuid(xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity) noexcept
    {
        if (!Entity.isValid()) return {};
        for (auto& pScene : GameMgr.m_SceneMgr.m_SceneInstances)
            if (pScene && pScene->m_RuntimeToLocal.contains(Entity.m_Value))
                return pScene->m_Guid;
        return {};
    }

    // Refuses remove when a LIVE, VALID entity reference in Owner still targets a scene that would
    // drop out of the reachable parent set (SaveEntity xassert path). Count comes ONLY from the live
    // component walk - ExternalToRuntime is the same handles remapped at load, so counting both
    // double-charges one ref and can disagree with CollectClearableRefsToLostParents (dialog showed
    // "0 entity reference(s)" while WhyCannot was non-empty).
    inline std::string WhyCannotRemoveSceneDependency(xecs::game_mgr::instance& GameMgr, xecs::scene::guid OwnerGuid, xecs::scene::guid DirectParent) noexcept
    {
        auto* pOwner = GameMgr.m_SceneMgr.Find(OwnerGuid);
        if (!pOwner) return "RemoveSceneDependency: owning scene is not loaded";

        std::vector<xecs::scene::guid> Lost;
        CollectLostParentsOnRemove(GameMgr, *pOwner, DirectParent, Lost);
        if (Lost.empty()) return {};

        auto IsLost = [&](xecs::scene::guid G) noexcept
        {
            return std::find(Lost.begin(), Lost.end(), G) != Lost.end();
        };

        int HitCount = 0;
        xecs::scene::guid FirstLost{};

        for (auto& [Id, Entity] : pOwner->m_LocalToRuntime)
        {
            ForEachLiveEntityReference(GameMgr, Entity, [&](xecs::component::entity Target) noexcept
            {
                const auto TargetScene = FindOwningSceneGuid(GameMgr, Target);
                if (TargetScene.m_Instance.m_Value == 0) return;
                if (!IsLost(TargetScene)) return;
                if (HitCount == 0) FirstLost = TargetScene;
                ++HitCount;
            });
        }
        if (HitCount == 0) return {};
        return std::format("Can't remove that dependency: {} reference(s) still target scene(s) that would become unreachable (e.g. {:016X})", HitCount, FirstLost.m_Instance.m_Value);
    }

    // After ParentScenes loses DirectParent, drop ExternalRefTable rows that pointed at scenes no
    // longer reachable AND whose ExternalToRuntime slot is already invalid (soft-failed / cleared).
    // Live-resolved slots must not appear here - WhyCannotRemove already refused. Compacts the table
    // and ExternalToRuntime together so SaveSceneDescriptor does not keep ghost ParentScenes edges.
    inline void PruneStaleExternalRefsAfterDependencyRemove(xecs::scene::instance& Owner, const std::vector<xecs::scene::guid>& Lost) noexcept
    {
        if (Lost.empty()) return;
        auto IsLost = [&](xecs::scene::guid G) noexcept
        {
            return std::find(Lost.begin(), Lost.end(), G) != Lost.end();
        };

        std::vector<xecs::scene::external_entity_address> NewTable;
        std::vector<xecs::component::entity>              NewRuntime;
        NewTable.reserve(Owner.m_ExternalRefTable.size());
        NewRuntime.reserve(Owner.m_ExternalToRuntime.size());
        for (std::size_t i = 0; i < Owner.m_ExternalRefTable.size(); ++i)
        {
            const bool bLost = IsLost(Owner.m_ExternalRefTable[i].m_ParentScene);
            const bool bLive = i < Owner.m_ExternalToRuntime.size() && Owner.m_ExternalToRuntime[i].isValid();
            if (bLost && !bLive) continue; // drop stale ghost
            NewTable.push_back(Owner.m_ExternalRefTable[i]);
            if (i < Owner.m_ExternalToRuntime.size()) NewRuntime.push_back(Owner.m_ExternalToRuntime[i]);
            else NewRuntime.push_back({});
        }
        Owner.m_ExternalRefTable  = std::move(NewTable);
        Owner.m_ExternalToRuntime = std::move(NewRuntime);
    }

    // One live entity-typed property in Owner that currently targets a scene in Lost - enough to
    // build SetEntityReference clears / RemoveSceneDependency -ClearRefs undo records.
    struct clearable_cross_scene_ref
    {
        xecs::scene::permanent_id m_HolderId    = xecs::scene::invalid_permanent_id_v;
        std::uint64_t             m_ComponentGuid = 0;
        std::string               m_Path;
        xecs::scene::guid         m_BeforeScene{};
        xecs::scene::permanent_id m_BeforeId    = xecs::scene::invalid_permanent_id_v;
    };

    // Property-table entity refs in Owner whose target lives in Lost (same coverage WhyCannot's
    // live walk uses for inspector EntityReference fields). BY_FUNCTION-only refs are skipped here
    // - EntityReference and other authoring refs go through properties.
    inline void CollectClearableRefsToLostParents(xecs::game_mgr::instance& GameMgr, const xecs::scene::instance& Owner, const std::vector<xecs::scene::guid>& Lost, std::vector<clearable_cross_scene_ref>& Out) noexcept
    {
        Out.clear();
        if (Lost.empty()) return;
        auto IsLost = [&](xecs::scene::guid G) noexcept
        {
            return std::find(Lost.begin(), Lost.end(), G) != Lost.end();
        };

        for (auto& [Id, Entity] : Owner.m_LocalToRuntime)
        {
            auto& IDetails = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
            if (!IDetails.m_pPool) continue;
            auto& Archetype = *IDetails.m_pPool->m_pArchetype;
            auto  DataSpan  = Archetype.getDataComponentInfos();

            for (auto pInfo : DataSpan)
            {
                if (pInfo->m_ReferenceMode == xecs::component::type::reference_mode::NO_REFERENCES
                 || xecs::component::type::IsComponentType<xecs::component::entity>(pInfo)
                 || !pInfo->m_pPropertyTable)
                    continue;

                const auto iType = IDetails.m_pPool->findIndexComponentFromInfo(*pInfo);
                if (iType < 0) continue;
                auto* pData = &IDetails.m_pPool->m_pComponent[iType][IDetails.m_PoolIndex.m_Value * pInfo->m_Size];

                xproperty::settings::context Context{};
                xproperty::sprop::collector(pData, *pInfo->m_pPropertyTable, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    if (Data.getTypeGuid() != xproperty::settings::var_type<xecs::component::entity>::guid_v) return;
                    auto& Target = Data.get<xecs::component::entity>();
                    if (!Target.isValid()) return;
                    const auto TargetScene = FindOwningSceneGuid(GameMgr, Target);
                    if (TargetScene.m_Instance.m_Value == 0 || !IsLost(TargetScene)) return;
                    auto* pTargetScene = GameMgr.m_SceneMgr.Find(TargetScene);
                    if (!pTargetScene) return;
                    auto It = pTargetScene->m_RuntimeToLocal.find(Target.m_Value);
                    if (It == pTargetScene->m_RuntimeToLocal.end()) return;

                    clearable_cross_scene_ref Hit{};
                    Hit.m_HolderId      = Id;
                    Hit.m_ComponentGuid = pInfo->m_Guid.m_Value;
                    Hit.m_Path          = pPropertyName ? pPropertyName : "";
                    Hit.m_BeforeScene   = TargetScene;
                    Hit.m_BeforeId      = It->second;
                    Out.push_back(std::move(Hit));
                });
            }
        }
    }

    void OpenScene(xecs::game_mgr::instance& GameMgr, scene_state& State, xresource::full_guid SceneGuid)
    {
        const xecs::scene::guid Guid{ .m_Instance = SceneGuid.m_Instance };
        if (std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), Guid) != State.m_OpenScenes.end())
            return;

        if (auto Err = GameMgr.m_SceneMgr.RequestLoad(Guid); Err)
        {
            xeditor::NotifyError(std::format("Failed to load Scene: {}", Err.getMessage()));
            return;
        }
        State.m_OpenScenes.push_back(Guid);
    }

    //---------------------------------------------------------------------------
    // Prefab lookup/override bookkeeping and prefab creation/instancing/deletion + the entity->
    // Prefab-asset drag-drop registration moved to standalone files under kit/ - phase 2 of the kit
    // split (same external review, direct user go-ahead to continue phase by phase). Included here,
    // in the same order they used to appear in this file, for the same reason phase 1's panels are:
    // this file remains the one umbrella #include, unchanged from the outside. Mechanical move only
    // - no behavior change; see each file's own top comment.
    //---------------------------------------------------------------------------
}
