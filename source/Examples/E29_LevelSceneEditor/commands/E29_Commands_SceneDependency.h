#ifndef E29_COMMANDS_SCENE_DEPENDENCY_H
#define E29_COMMANDS_SCENE_DEPENDENCY_H
#pragma once

// AddSceneDependency / RemoveSceneDependency - explicit scene ParentScenes authoring.
// Dependencies are NOT derived from entity references: the Level Tree "Dependencies" folder is the
// only place that adds/removes edges. Entity-reference assignment requires the edge to already exist
// (see m_OnEntityReferenceRender). Cycle check walks transitive parents (live or Descriptor.txt).
// Remove refuses when any live entity reference in the owning scene targets a scene that would become
// unreachable (the removed parent plus anything only reachable through it).

#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    //================================================================================================
    // AddSceneDependency - Owner gains Parent as a direct ParentScenes entry (undoable).
    // Usage: AddSceneDependency -Scene hexguid -Parent hexguid
    //================================================================================================
    struct add_scene_dependency_cmd : xundo::command_base
    {
        add_scene_dependency_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "AddSceneDependency", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Adds an explicit scene dependency (ParentScenes). Refuses cycles. Does not save. Usage: AddSceneDependency -Scene hexguid -Parent hexguid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene  = m_Parser.addOption("Scene",  "Owning scene guid, 16 hex digits", true, 1);
            m_hParent = m_Parser.addOption("Parent", "Dependency scene guid, 16 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(ParentArg))
                return "AddSceneDependency: bad arguments";
            if (!e29::g_pGameMgr) return "AddSceneDependency: no game world";

            const auto SceneGuid  = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto ParentGuid = ParseSceneGuid(std::get<std::string>(ParentArg));
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return "AddSceneDependency: owning scene is not loaded";

            if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid) != pScene->m_ParentScenes.end())
                return {};

            if (e29::WouldCreateDependencyCycle(*e29::g_pGameMgr, SceneGuid, ParentGuid))
                return "AddSceneDependency: would create a circular scene dependency";

            pScene->m_ParentScenes.push_back(ParentGuid);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            const std::uint64_t Scene  = std::holds_alternative<xerr>(SceneArg)  ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint64_t Parent = std::holds_alternative<xerr>(ParentArg) ? 0 : std::strtoull(std::get<std::string>(ParentArg).c_str(), nullptr, 16);
            std::uint32_t bWasPresent = 0;
            if (e29::g_pGameMgr && Scene && Parent)
            {
                const auto SceneGuid  = xecs::scene::guid{ .m_Instance = { Scene } };
                const auto ParentGuid = xecs::scene::guid{ .m_Instance = { Parent } };
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                    bWasPresent = (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid) != pScene->m_ParentScenes.end()) ? 1u : 0u;
            }
            File.Write(Scene);
            File.Write(Parent);
            File.Write(bWasPresent);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0; File.Read(Scene);
            std::uint64_t Parent = 0; File.Read(Parent);
            std::uint32_t bWasPresent = 0; File.Read(bWasPresent);
            if (bWasPresent || !e29::g_pGameMgr) return;
            const auto SceneGuid  = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto ParentGuid = xecs::scene::guid{ .m_Instance = { Parent } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return;
            if (auto It = std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid); It != pScene->m_ParentScenes.end())
                pScene->m_ParentScenes.erase(It);
        }

        xcmdline::parser::handle m_hScene, m_hParent;
    };

    //================================================================================================
    // RemoveSceneDependency - erases Parent from Owner.m_ParentScenes (undoable). Refuses when any
    // live entity ref in Owner would target a scene that becomes unreachable, unless -ClearRefs 1
    // is passed (nulls those refs first, then removes - one undo step restores refs + the edge).
    // Usage: RemoveSceneDependency -Scene hexguid -Parent hexguid [-ClearRefs 1]
    //================================================================================================
    struct remove_scene_dependency_cmd : xundo::command_base
    {
        remove_scene_dependency_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RemoveSceneDependency", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Removes an explicit scene dependency. Refuses if entity refs would break unless -ClearRefs 1 (null those refs, then remove). Usage: RemoveSceneDependency -Scene hexguid -Parent hexguid [-ClearRefs 1]";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene     = m_Parser.addOption("Scene",     "Owning scene guid, 16 hex digits", true, 1);
            m_hParent    = m_Parser.addOption("Parent",    "Dependency scene guid, 16 hex digits", true, 1);
            m_hClearRefs = m_Parser.addOption("ClearRefs", "Pass 1 to null entity refs targeting the lost parent tree, then remove (UI Yes / AI)", false, 1);
        }

        static bool WantsClearRefs(const xcmdline::parser& Parser, xcmdline::parser::handle h) noexcept
        {
            auto Arg = Parser.getOptionArgAs<std::string>(h, 0);
            return !std::holds_alternative<xerr>(Arg) && std::get<std::string>(Arg) == "1";
        }

        std::string Redo() noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(ParentArg))
                return "RemoveSceneDependency: bad arguments";
            if (!e29::g_pGameMgr) return "RemoveSceneDependency: no game world";

            const auto SceneGuid  = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto ParentGuid = ParseSceneGuid(std::get<std::string>(ParentArg));
            const bool bClearRefs = WantsClearRefs(m_Parser, m_hClearRefs);
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return "RemoveSceneDependency: owning scene is not loaded";

            if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid) == pScene->m_ParentScenes.end())
                return "RemoveSceneDependency: parent is not a dependency";

            std::vector<xecs::scene::guid> Lost;
            e29::CollectLostParentsOnRemove(*e29::g_pGameMgr, *pScene, ParentGuid, Lost);

            if (!bClearRefs)
            {
                if (auto Why = e29::WhyCannotRemoveSceneDependency(*e29::g_pGameMgr, SceneGuid, ParentGuid); !Why.empty())
                    return Why;
            }
            else
            {
                std::vector<e29::clearable_cross_scene_ref> Hits;
                e29::CollectClearableRefsToLostParents(*e29::g_pGameMgr, *pScene, Lost, Hits);
                for (auto& Hit : Hits)
                {
                    const auto Target = ResolvePropertyTarget(SceneGuid, Hit.m_HolderId, Hit.m_ComponentGuid);
                    if (!Target.m_pInfo) continue;
                    SetLiveEntityReferenceValue(Target, Hit.m_Path, {});
                    xproperty::any AnyVal; AnyVal.set<xecs::component::entity>({});
                    std::array<char, 256> Buffer{};
                    const auto Len = FormatPropertyValue(Buffer, AnyVal);
                    const std::string ValueStr(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
                    RecordPropertyOverride(Target, SceneGuid, Hit.m_HolderId, Hit.m_Path, ValueStr);
                }
            }

            pScene->m_ParentScenes.erase(std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid));
            e29::PruneStaleExternalRefsAfterDependencyRemove(*pScene, Lost);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg  = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            const std::uint64_t Scene  = std::holds_alternative<xerr>(SceneArg)  ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint64_t Parent = std::holds_alternative<xerr>(ParentArg) ? 0 : std::strtoull(std::get<std::string>(ParentArg).c_str(), nullptr, 16);
            const bool bClearRefs = WantsClearRefs(m_Parser, m_hClearRefs);
            std::uint32_t Index = 0;
            std::vector<e29::clearable_cross_scene_ref> Hits;
            if (e29::g_pGameMgr && Scene && Parent)
            {
                const auto SceneGuid  = xecs::scene::guid{ .m_Instance = { Scene } };
                const auto ParentGuid = xecs::scene::guid{ .m_Instance = { Parent } };
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                {
                    auto It = std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid);
                    if (It != pScene->m_ParentScenes.end())
                        Index = static_cast<std::uint32_t>(std::distance(pScene->m_ParentScenes.begin(), It));
                    if (bClearRefs)
                    {
                        std::vector<xecs::scene::guid> Lost;
                        e29::CollectLostParentsOnRemove(*e29::g_pGameMgr, *pScene, ParentGuid, Lost);
                        e29::CollectClearableRefsToLostParents(*e29::g_pGameMgr, *pScene, Lost, Hits);
                    }
                }
            }
            File.Write(Scene);
            File.Write(Parent);
            File.Write(Index);
            const std::uint32_t bHadClear = bClearRefs ? 1u : 0u;
            File.Write(bHadClear);
            const std::uint32_t nHits = static_cast<std::uint32_t>(Hits.size());
            File.Write(nHits);
            for (auto& Hit : Hits)
            {
                File.Write(static_cast<std::uint32_t>(Hit.m_HolderId));
                File.Write(Hit.m_ComponentGuid);
                WriteString(File, Hit.m_Path);
                File.Write(Hit.m_BeforeScene.m_Instance.m_Value);
                File.Write(static_cast<std::uint32_t>(Hit.m_BeforeId));
            }
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0; File.Read(Scene);
            std::uint64_t Parent = 0; File.Read(Parent);
            std::uint32_t Index = 0; File.Read(Index);
            std::uint32_t bHadClear = 0; File.Read(bHadClear);
            std::uint32_t nHits = 0; File.Read(nHits);
            struct hit_rec { std::uint32_t HolderId; std::uint64_t Comp; std::string Path; std::uint64_t BeforeScene; std::uint32_t BeforeId; };
            std::vector<hit_rec> Hits;
            Hits.reserve(nHits);
            for (std::uint32_t i = 0; i < nHits; ++i)
            {
                hit_rec H{};
                File.Read(H.HolderId);
                File.Read(H.Comp);
                H.Path = ReadString(File);
                File.Read(H.BeforeScene);
                File.Read(H.BeforeId);
                Hits.push_back(std::move(H));
            }

            if (!e29::g_pGameMgr) return;
            const auto SceneGuid  = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto ParentGuid = xecs::scene::guid{ .m_Instance = { Parent } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return;

            // Restore the ParentScenes edge first so ResolveReference / owning-scene walks see it.
            if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid) == pScene->m_ParentScenes.end())
            {
                const auto Idx = std::min<std::size_t>(Index, pScene->m_ParentScenes.size());
                pScene->m_ParentScenes.insert(pScene->m_ParentScenes.begin() + static_cast<std::ptrdiff_t>(Idx), ParentGuid);
            }

            if (bHadClear)
            {
                for (auto& H : Hits)
                {
                    const auto BeforeSceneGuid = xecs::scene::guid{ .m_Instance = { H.BeforeScene } };
                    const auto BeforeEntity = ResolveEntityReferenceTarget(BeforeSceneGuid, static_cast<xecs::scene::permanent_id>(H.BeforeId));
                    const auto Target = ResolvePropertyTarget(SceneGuid, static_cast<xecs::scene::permanent_id>(H.HolderId), H.Comp);
                    if (!Target.m_pInfo) continue;
                    SetLiveEntityReferenceValue(Target, H.Path, BeforeEntity);
                    xproperty::any AnyVal; AnyVal.set<xecs::component::entity>(BeforeEntity);
                    std::array<char, 256> Buffer{};
                    const auto Len = FormatPropertyValue(Buffer, AnyVal);
                    const std::string ValueStr(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
                    RecordPropertyOverride(Target, SceneGuid, static_cast<xecs::scene::permanent_id>(H.HolderId), H.Path, ValueStr);
                }
            }
        }

        xcmdline::parser::handle m_hScene, m_hParent, m_hClearRefs;
    };
}

// UI: when Remove Dependency would fail due to live refs, ask Yes|No|Cancel instead of only Debugger().
// Yes -> RemoveSceneDependency -ClearRefs 1. No / Cancel -> leave the graph alone.
namespace e29
{
    struct pending_remove_dependency_confirm
    {
        bool              m_bOpen = false;
        xecs::scene::guid m_Owner{};
        xecs::scene::guid m_Parent{};
        int               m_RefCount = 0;
    };
    inline pending_remove_dependency_confirm g_PendingRemoveDependencyConfirm{};

    // Level Tree X / context menu - runs remove immediately when clear, otherwise opens the confirm modal.
    inline void RequestRemoveSceneDependency(xundo::system& Undo, xecs::scene::guid Owner, xecs::scene::guid Parent) noexcept
    {
        if (!g_pGameMgr)
        {
            commands::Run(Undo, std::format("RemoveSceneDependency -Scene {} -Parent {}"
                , commands::FormatSceneGuid(Owner), commands::FormatSceneGuid(Parent)));
            return;
        }

        auto* pOwner = g_pGameMgr->m_SceneMgr.Find(Owner);
        std::vector<clearable_cross_scene_ref> Hits;
        if (pOwner)
        {
            std::vector<xecs::scene::guid> Lost;
            CollectLostParentsOnRemove(*g_pGameMgr, *pOwner, Parent, Lost);
            CollectClearableRefsToLostParents(*g_pGameMgr, *pOwner, Lost, Hits);
        }

        // Nothing to clear -> no dialog. Plain remove (also covers "WhyCannot was wrong / ExternalRef-only").
        if (Hits.empty())
        {
            commands::Run(Undo, std::format("RemoveSceneDependency -Scene {} -Parent {}"
                , commands::FormatSceneGuid(Owner), commands::FormatSceneGuid(Parent)));
            return;
        }

        g_PendingRemoveDependencyConfirm = { true, Owner, Parent, static_cast<int>(Hits.size()) };
    }

    // Same top-level OpenPopup/BeginPopupModal scope as RenderErrorPopup / RenderKeepTweaksModal.
    inline void RenderRemoveDependencyConfirmModal(xundo::system& Undo) noexcept
    {
        if (g_PendingRemoveDependencyConfirm.m_bOpen)
        {
            ImGui::OpenPopup("Clear dependency refs?##E29");
            g_PendingRemoveDependencyConfirm.m_bOpen = false;
        }

        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Clear dependency refs?##E29", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 440.0f);
            ImGui::TextUnformatted(std::format(
                "This dependency still has {} entity reference(s) targeting scene(s) that would become unreachable.\n\n"
                "Do you want to clear all those references to null, then remove the dependency?",
                g_PendingRemoveDependencyConfirm.m_RefCount).c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();

            const float W = 120.0f;
            if (ImGui::Button("Yes", ImVec2(W, 0.0f)))
            {
                commands::Run(Undo, std::format("RemoveSceneDependency -Scene {} -Parent {} -ClearRefs 1"
                    , commands::FormatSceneGuid(g_PendingRemoveDependencyConfirm.m_Owner)
                    , commands::FormatSceneGuid(g_PendingRemoveDependencyConfirm.m_Parent)));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(W, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(W, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}

#endif // E29_COMMANDS_SCENE_DEPENDENCY_H