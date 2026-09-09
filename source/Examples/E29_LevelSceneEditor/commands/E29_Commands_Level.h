#ifndef E29_COMMANDS_LEVEL_H
#define E29_COMMANDS_LEVEL_H
#pragma once

// Level/Scene workspace + discovery commands - a real gap found live, direct user reports:
// "clearly one of the commands should be load a level...", "yes I think you are missing Level/Scene
// commands", "ListFolders -Scene ... -From root...". Every phase 1-4 command assumes a level/scene is
// ALREADY open via the UI, and every one of THOSE assumes the caller already knows a Scene guid/entity
// id - there was no way for a fresh AI/CLI-driven session to bootstrap itself OR discover what's even
// there at all, which defeats phase 5's own "drive E29 with zero UI automation" purpose. This file is
// the fix: OpenLevel/CloseScene (workspace actions) plus ListLevels/ListScenes/ListEntities/
// ListFolders (pure discovery, read the live state, never mutate anything).
//
// All QUERY commands (xundo::query_command_base), not Edit ones: none of these mutate SCENE CONTENT
// the way every phase 1-4 command does - they're workspace/session operations (which files am I
// looking at) or pure read-only discovery, the same category Play/Stop already sit in without being
// undo-routed. "Undo" of opening a level would really mean "close it again," a distinct, deliberate
// action of its own, not the inverse of this one - matches Say/GetLog's own reasoning for why THEY
// are Query commands too (commands/E29_Commands_Chat.h).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    //================================================================================================
    // OpenLevel - loads a Level (by its instance guid, same 16-hex-digit convention every other
    // command already uses for a Scene guid) and activates it, which - per e29::OpenLevel's own
    // existing behavior (E29_LevelSceneEditorKit.h) - loads every Scene the Level owns automatically
    // (direct user request from earlier in this project: "Scenes should always be loaded if they are
    // part of the level"). Reports success/failure by checking State.m_CurrentLevel afterward rather
    // than trusting e29::OpenLevel's own return type (void - it only reports failure via a blocking
    // Debugger() modal popup, the same existing failure-UX every other command's own error path
    // already inherits, not something new introduced here).
    //================================================================================================
    struct open_level_cmd : xundo::query_command_base
    {
        open_level_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "OpenLevel", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Loads a Level and activates every Scene it owns. Usage: OpenLevel -Level hexguid";
        }
        void RegisterArguments() noexcept override
        {
            m_hLevel = m_Parser.addOption("Level", "Level instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
            if (std::holds_alternative<xerr>(LevelArg))
                return "OpenLevel: bad arguments";

            if (!e29::g_pGameMgr) return "OpenLevel: no game world";

            const std::uint64_t Value = std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16);
            auto& State = get<e29_command_context>().m_State;

            e29::OpenLevel(*e29::g_pGameMgr, State, xresource::full_guid{ .m_Instance = { Value }, .m_Type = {} });

            if (State.m_CurrentLevel.m_Instance.m_Value != Value)
                return std::format("OpenLevel: failed to open {:016X} - see the app's own error popup for details", Value);

            return std::format("Opened Level {:016X}, {} scene(s) now open", Value, State.m_OpenScenes.size());
        }

        xcmdline::parser::handle m_hLevel;
    };

    //================================================================================================
    // CloseScene - wraps the existing e29::CloseScene helper (E29_LevelSceneEditorKit.h): releases one
    // scene's residency and removes it from State.m_OpenScenes. Query, not Edit, matching OpenLevel's
    // own reasoning above - a workspace/session action, not a scene-content mutation.
    //================================================================================================
    struct close_scene_cmd : xundo::query_command_base
    {
        close_scene_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "CloseScene", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Releases a scene's residency and removes it from the open list. Usage: CloseScene -Scene hexguid";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            if (std::holds_alternative<xerr>(SceneArg)) return "CloseScene: bad arguments";
            if (!e29::g_pGameMgr) return "CloseScene: no game world";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            auto& State = get<e29_command_context>().m_State;
            const bool bWasOpen = std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) != State.m_OpenScenes.end();

            e29::CloseScene(*e29::g_pGameMgr, State, SceneGuid);

            return bWasOpen ? std::format("Closed Scene {}", FormatSceneGuid(SceneGuid)) : std::format("CloseScene: {} was not open", FormatSceneGuid(SceneGuid));
        }

        xcmdline::parser::handle m_hScene;
    };

    // Shared by ListLevels/ListScenes - walks e10::g_LibMgr's own type-indexed asset map (the same one
    // the Asset Browser itself walks, E10_AssetBrowser.h) and returns {instance guid value -> display
    // name} for every asset of TypeGuid, across every open library. Not a single-lookup helper (like
    // library_mgr::getInfo, which needs a specific library::guid up front) - ListScenes needs to
    // resolve names for a whole SET of scene guids (a Level's own m_Scenes) at once, so building one
    // full map and looking each up in memory is simpler than N separate library-scoped lookups.
    inline std::unordered_map<std::uint64_t, std::string> BuildAssetNameMap(xresource::type_guid TypeGuid) noexcept
    {
        std::unordered_map<std::uint64_t, std::string> Out;
        for (auto& L : e10::g_LibMgr.m_mLibraryDB)
        {
            L.second->m_InfoByTypeDataBase.FindAsReadOnly(TypeGuid, [&](const std::unique_ptr<e10::library_db::info_db>& TypeDB)
            {
                for (auto& I : TypeDB->m_InfoDataBase)
                    Out[I.second.m_Info.m_Guid.m_Instance.m_Value] = I.second.m_Info.m_Name;
            });
        }
        return Out;
    }

    //================================================================================================
    // ListLevels - every Level asset in the open project(s), {guid, name} one per line. The
    // discovery command a fresh AI/CLI session needs before it can even call OpenLevel at all - direct
    // user report: "yes I think you are missing Level/Scene commands."
    //================================================================================================
    struct list_levels_query_cmd : xundo::query_command_base
    {
        list_levels_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListLevels", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every Level asset (guid + name). Usage: ListLevels"; }
        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override
        {
            std::string Out;
            for (auto& [Guid, Name] : BuildAssetNameMap(xecs::level::type_guid_v))
                Out += std::format("{:016X}  {}\n", Guid, Name);
            return Out;
        }
    };

    //================================================================================================
    // ListScenes - the scenes owned by a Level (default: the currently open one), {guid, name} one per
    // line - a Level's own m_Scenes only holds guids, so names are cross-referenced from the same
    // asset-name map ListLevels itself uses.
    //================================================================================================
    struct list_scenes_query_cmd : xundo::query_command_base
    {
        list_scenes_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListScenes", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the scenes owned by a Level (default: the currently open one). Usage: ListScenes [-Level hexguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hLevel = m_Parser.addOption("Level", "Level instance guid, 16 hex digits (default: the currently open level)", false, 1);
        }

        std::string Query() noexcept override
        {
            if (!e29::g_pGameMgr) return "ListScenes: no game world";
            auto& State = get<e29_command_context>().m_State;

            auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
            const auto LevelGuid = std::holds_alternative<xerr>(LevelArg)
                ? State.m_CurrentLevel
                : xecs::level::guid{ .m_Instance = { std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16) } };

            auto* pLevel = e29::g_pGameMgr->m_LevelMgr.Find(LevelGuid);
            if (!pLevel) return std::format("ListScenes: Level {:016X} is not open (try OpenLevel first)", LevelGuid.m_Instance.m_Value);

            const auto Names = BuildAssetNameMap(xecs::scene::type_guid_v);
            std::string Out;
            for (auto& SceneGuid : pLevel->m_Scenes)
            {
                auto It = Names.find(SceneGuid.m_Instance.m_Value);
                Out += std::format("{}  {}\n", FormatSceneGuid(SceneGuid), It != Names.end() ? It->second : "(unnamed)");
            }
            return Out;
        }

        xcmdline::parser::handle m_hLevel;
    };

    //================================================================================================
    // ListEntities - every entity in a scene (flat, not folder-structured - see ListFolders below for
    // the tree view), {id, name-or-"Entity #id"} one per line, same fallback E29's own Level Tree panel
    // uses (kit/E29_Panel_LevelTree.h).
    //================================================================================================
    struct list_entities_query_cmd : xundo::query_command_base
    {
        list_entities_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListEntities", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every entity in a scene (hex id + name). Usage: ListEntities -Scene hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            if (std::holds_alternative<xerr>(SceneArg)) return "ListEntities: bad arguments";
            if (!e29::g_pGameMgr) return "ListEntities: no game world";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return std::format("ListEntities: Scene {} is not open", FormatSceneGuid(SceneGuid));

            std::string Out;
            for (auto& [Id, Entity] : pScene->m_LocalToRuntime)
            {
                std::string Label = std::format("Entity #{:08X}", Id);
                if (auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool)
                    if (Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                        Label = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;
                Out += std::format("{:08X}  {}\n", Id, Label);
            }
            return Out;
        }

        xcmdline::parser::handle m_hScene;
    };

    //================================================================================================
    // ListFolders - a scene's folder tree, recursively, from -From (a folder id, or the literal word
    // "root"/omitted for the top level) - each folder's own directly-contained entities listed under
    // it, indented by depth. Direct user request: "ListFolders -Scene ... -From root...".
    //================================================================================================
    struct list_folders_query_cmd : xundo::query_command_base
    {
        list_folders_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListFolders", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists a scene's folder tree (folders + their entities), recursively. Usage: ListFolders -Scene hexguid [-From folderid-or-root]"; }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits",                             true,  1);
            m_hFrom  = m_Parser.addOption("From",  "Folder id to start from, or \"root\" (default: root)",  false, 1);
        }

        std::string Query() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            if (std::holds_alternative<xerr>(SceneArg)) return "ListFolders: bad arguments";
            if (!e29::g_pGameMgr) return "ListFolders: no game world";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) return std::format("ListFolders: Scene {} is not open", FormatSceneGuid(SceneGuid));

            auto FromArg = m_Parser.getOptionArgAs<std::string>(m_hFrom, 0);
            xecs::scene::folder_id FromId = xecs::scene::invalid_folder_id_v;
            if (!std::holds_alternative<xerr>(FromArg))
            {
                const auto& S = std::get<std::string>(FromArg);
                if (!(S == "root" || S == "Root")) FromId = static_cast<xecs::scene::folder_id>(std::strtoul(S.c_str(), nullptr, 16));
            }

            std::string Out;

            // An explicitly-named -From folder shows its OWN entities too, not just its child
            // folders - the root/default case doesn't need this special-cased, since root itself
            // isn't a real folder entry to print (Walk below already prints every TOP-LEVEL folder,
            // including their entities, as children of the implicit root).
            if (FromId != xecs::scene::invalid_folder_id_v)
            {
                if (auto It = std::ranges::find(pScene->m_Folders, FromId, &xecs::scene::folder::m_Id); It != pScene->m_Folders.end())
                {
                    Out += std::format("[{}] {:08X} ({} entities)\n", It->m_Name, static_cast<std::uint32_t>(It->m_Id), It->m_Entities.size());
                    for (auto Id : It->m_Entities)
                        Out += std::format("  - {:08X}\n", Id);
                }
                else
                {
                    return std::format("ListFolders: folder {:08X} not found in this scene", static_cast<std::uint32_t>(FromId));
                }
            }

            std::function<void(xecs::scene::folder_id, int)> Walk = [&](xecs::scene::folder_id ParentId, int Depth) noexcept
            {
                for (auto& F : pScene->m_Folders)
                {
                    if (F.m_Parent != ParentId) continue;
                    const std::string Indent(static_cast<std::size_t>(Depth) * 2, ' ');
                    Out += std::format("{}[{}] {:08X} ({} entities)\n", Indent, F.m_Name, static_cast<std::uint32_t>(F.m_Id), F.m_Entities.size());
                    for (auto Id : F.m_Entities)
                        Out += std::format("{}  - {:08X}\n", Indent, Id);
                    Walk(F.m_Id, Depth + 1);
                }
            };
            Walk(FromId, FromId == xecs::scene::invalid_folder_id_v ? 0 : 1);
            return Out.empty() ? "(no folders)\n" : Out;
        }

        xcmdline::parser::handle m_hScene, m_hFrom;
    };
}

#endif // E29_COMMANDS_LEVEL_H
