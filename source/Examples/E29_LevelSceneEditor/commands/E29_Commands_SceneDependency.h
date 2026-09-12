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
    // live entity ref in Owner would target a scene that becomes unreachable.
    // Usage: RemoveSceneDependency -Scene hexguid -Parent hexguid
    //================================================================================================
    struct remove_scene_dependency_cmd : xundo::command_base
    {
        remove_scene_dependency_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RemoveSceneDependency", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Removes an explicit scene dependency. Refuses if entity refs would break. Usage: RemoveSceneDependency -Scene hexguid -Parent hexguid";
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
                return "RemoveSceneDependency: bad arguments";
            if (!e29::g_pGameMgr) return "RemoveSceneDependency: no game world";

            const auto SceneGuid  = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto ParentGuid = ParseSceneGuid(std::get<std::string>(ParentArg));
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return "RemoveSceneDependency: owning scene is not loaded";

            if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid) == pScene->m_ParentScenes.end())
                return "RemoveSceneDependency: parent is not a dependency";

            if (auto Why = e29::WhyCannotRemoveSceneDependency(*e29::g_pGameMgr, SceneGuid, ParentGuid); !Why.empty())
                return Why;

            std::vector<xecs::scene::guid> Lost;
            e29::CollectLostParentsOnRemove(*e29::g_pGameMgr, *pScene, ParentGuid, Lost);
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
            std::uint32_t Index = 0;
            if (e29::g_pGameMgr && Scene && Parent)
            {
                const auto SceneGuid  = xecs::scene::guid{ .m_Instance = { Scene } };
                const auto ParentGuid = xecs::scene::guid{ .m_Instance = { Parent } };
                if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
                {
                    auto It = std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid);
                    if (It != pScene->m_ParentScenes.end())
                        Index = static_cast<std::uint32_t>(std::distance(pScene->m_ParentScenes.begin(), It));
                }
            }
            File.Write(Scene);
            File.Write(Parent);
            File.Write(Index);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0; File.Read(Scene);
            std::uint64_t Parent = 0; File.Read(Parent);
            std::uint32_t Index = 0; File.Read(Index);
            if (!e29::g_pGameMgr) return;
            const auto SceneGuid  = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto ParentGuid = xecs::scene::guid{ .m_Instance = { Parent } };
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return;
            if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), ParentGuid) != pScene->m_ParentScenes.end())
                return;
            const auto Idx = std::min<std::size_t>(Index, pScene->m_ParentScenes.size());
            pScene->m_ParentScenes.insert(pScene->m_ParentScenes.begin() + static_cast<std::ptrdiff_t>(Idx), ParentGuid);
        }

        xcmdline::parser::handle m_hScene, m_hParent;
    };
}

#endif // E29_COMMANDS_SCENE_DEPENDENCY_H
