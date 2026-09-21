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
// Mostly QUERY commands (xundo::query_command_base): OpenLevel/CloseScene/List* are workspace or
// read-only discovery, same category as Play/Stop - not undo-routed. AddScene/RemoveScene below ARE
// undoable edits of Level.m_Scenes membership (no LevelMgr.Save; Add does not OpenScene) - same
// xeditor::Run pattern as ApplyOverrides / CreateEntity.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29
{
    // Forward-only, same reasoning as the identical declaration in E29_Commands_SourceControl.h -
    // extensions/game_module/E29_ComponentCompatibility.h is already fully defined earlier in this same translation unit
    // via the umbrella, this just makes it visible here too without re-including anything (that header
    // assumes a specific inclusion position and breaks badly pulled in directly this deep in the
    // commands chain - confirmed live).
    bool IsComponentInLiveRegistry( xecs::component::type::guid Guid ) noexcept;
}

namespace e29::commands
{
    //================================================================================================
    // OpenLevel - loads a Level (by its instance guid, same 16-hex-digit convention every other
    // command already uses for a Scene guid) and activates it, which - per e29::OpenLevel's own
    // existing behavior (E29_LevelSceneEditorKit.h) - loads every Scene the Level owns automatically
    // (direct user request from earlier in this project: "Scenes should always be loaded if they are
    // part of the level"). Reports success/failure by checking State.m_CurrentLevel afterward rather
    // than trusting e29::OpenLevel's own return type (void - it only reports failure via a blocking
    // xeditor::NotifyError() modal popup, the same existing failure-UX every other command's own error path
    // already inherits, not something new introduced here).
    //================================================================================================
    struct open_level_cmd : scene_query_command
    {
        open_level_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "OpenLevel", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Loads a Level and activates every Scene it owns. If another Level is open and dirty, pass -Save 1 or -Save 0 first. Usage: OpenLevel -Level hexguid [-Save 0|1]";
        }
        void RegisterArguments() noexcept override
        {
            m_hLevel = m_Parser.addOption("Level", "Level instance guid, 16 hex digits", true, 1);
            m_hSave  = m_Parser.addOption("Save", "When the current Level has unsaved changes: 1/true = save then switch, 0/false = discard then switch. Ignored when clean or nothing open.", false, 1);
        }

        std::string Query() noexcept override
        {
            auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
            if (std::holds_alternative<xerr>(LevelArg))
                return "OpenLevel: bad arguments";


            auto& State = get<editor_context>().m_State;
            if (State.isPlaying()) return "OpenLevel: blocked while Play/Paused";

            const std::uint64_t Value = std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16);
            const xresource::full_guid LevelGuid{
                .m_Instance = { Value },
                .m_Type     = xecs::level::type_guid_v
            };
            const xecs::level::guid AsLevel{ .m_Instance = LevelGuid.m_Instance };

            if (!State.m_CurrentLevel.empty() && State.m_CurrentLevel.m_Instance == AsLevel.m_Instance)
                return std::format("OpenLevel: {:016X} is already open", Value);

            std::optional<bool> SaveOverride;
            if (auto SaveArg = m_Parser.getOptionArgAs<std::string>(m_hSave, 0); !std::holds_alternative<xerr>(SaveArg))
            {
                const auto& S = std::get<std::string>(SaveArg);
                SaveOverride = (S == "true" || S == "1");
            }

            const bool bHaveDoc = !State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty();
            const bool bDirty   = e29::HasUnsavedDocumentChanges(State, EditorContext().m_Undo);
            if (bHaveDoc && bDirty && !SaveOverride.has_value())
                return "OpenLevel: current Level has unsaved changes; pass -Save 1 (save) or -Save 0 (discard)";

            if (bHaveDoc)
            {
                if (bDirty && SaveOverride.value())
                {
                    e29::SaveEverything(World(), State);
                    e29::MarkDocumentClean(State, EditorContext().m_Undo);
                }
                e29::CloseLevel(World(), State, EditorContext().m_Undo);
            }

            e29::OpenLevel(World(), State, LevelGuid);
            e29::MarkDocumentClean(State, EditorContext().m_Undo);

            if (State.m_CurrentLevel.m_Instance.m_Value != Value)
                return std::format("OpenLevel: failed to open {:016X} (unknown Level guid or load error)", Value);

            // Component-registry compatibility plan, Phase 4: informational, not blocking - EnsureLoaded
            // already soft-fails a per-entity missing-component-type case on its own (skips that one
            // entity, logs a warning, the rest of the scene loads fine - xecs_scene_inline.h's own
            // established behavior, unchanged here). This just surfaces the SAME class of problem more
            // visibly, at the command's own return value, right after the scenes just opened, instead
            // of only a console log line buried in the editor's own scrollback.
            std::vector<xecs::scene::component_dependency> Missing;
            for (auto& SceneGuid : State.m_OpenScenes)
                for (auto& Dep : xecs::scene::LoadSceneComponentDependencies(e10::g_LibMgr.m_ProjectPath, SceneGuid))
                    if (!IsComponentInLiveRegistry(Dep.m_Guid) && std::find_if(Missing.begin(), Missing.end(), [&](auto& M) noexcept { return M.m_Guid == Dep.m_Guid; }) == Missing.end())
                        Missing.push_back(Dep);

            std::string Result = std::format("Opened Level {:016X}, {} scene(s) now open", Value, State.m_OpenScenes.size());
            if (!Missing.empty())
            {
                std::string Names;
                for (auto& Dep : Missing) Names += (Names.empty() ? "" : ", ") + Dep.m_Name;
                Result += std::format(" - WARNING: {} component type(s) used by these scenes are not currently registered: {}", Missing.size(), Names);
            }
            return Result;
        }

        xcmdline::parser::handle m_hLevel;
        xcmdline::parser::handle m_hSave;
    };

    //================================================================================================
    // Component-registry compatibility plan, Phase 6 (lean form - see this session's own plan/memory
    // for why the full Idle-Work background-task version was scoped down under time pressure): an
    // on-demand, project-wide sweep of EVERY scene's own ComponentDeps.txt (open or not) against the
    // live registry - the one thing the synchronous gates (hot-reload/Open/module-removal, all scoped
    // to OPEN scenes only) can't see: "this change didn't break what's open, but it may break some
    // OTHER scene, not loaded right now" - direct user framing from this session's own design
    // conversation. Read-only, safe to call any time, no game world required.
    struct audit_component_usage_query_cmd : scene_query_command
    {
        audit_component_usage_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "AuditComponentUsage", pDataBase) {}
        const char* getCommandHelp() const noexcept override
        {
            return "Scans every scene in the project (open or not) for component types no longer registered. Usage: AuditComponentUsage";
        }
        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override
        {
            const auto ScenesRoot = std::filesystem::path(e10::g_LibMgr.m_ProjectPath) / L"Descriptors" / L"Scene";
            std::error_code Ec;
            if (!std::filesystem::exists(ScenesRoot, Ec)) return "AuditComponentUsage: no scenes found";

            std::string Result;
            int ScenesScanned = 0, ScenesWithIssues = 0;
            for (auto It = std::filesystem::recursive_directory_iterator(ScenesRoot, std::filesystem::directory_options::skip_permission_denied, Ec); It != std::filesystem::recursive_directory_iterator(); It.increment(Ec))
            {
                if (Ec) break;
                if (!It->is_regular_file(Ec) || It->path().filename() != L"ComponentDeps.txt") continue;

                // Folder name IS the scene's own instance guid, hex - same convention every other
                // resource type's Descriptors/<Type>/<b0>/<b1>/<guid>.desc path already uses.
                const auto GuidHex = xstrtool::To(It->path().parent_path().stem().wstring());
                const auto SceneGuid = ParseSceneGuid(GuidHex);
                ++ScenesScanned;

                std::vector<xecs::scene::component_dependency> Missing;
                for (auto& Dep : xecs::scene::LoadSceneComponentDependencies(e10::g_LibMgr.m_ProjectPath, SceneGuid))
                    if (!IsComponentInLiveRegistry(Dep.m_Guid))
                        Missing.push_back(Dep);

                if (!Missing.empty())
                {
                    ++ScenesWithIssues;
                    std::string Names;
                    for (auto& Dep : Missing) Names += (Names.empty() ? "" : ", ") + Dep.m_Name;
                    Result += std::format("Scene {}: missing {}\n", GuidHex, Names);
                }
            }

            return std::format("AuditComponentUsage: {} scene(s) scanned, {} with issues\n{}", ScenesScanned, ScenesWithIssues, Result);
        }
    };

    //================================================================================================
    // CloseScene - wraps the existing e29::CloseScene helper (E29_LevelSceneEditorKit.h): releases one
    // scene's residency and removes it from State.m_OpenScenes. Query, not Edit, matching OpenLevel's
    // own reasoning above - a workspace/session action, not a scene-content mutation.
    //================================================================================================
    struct close_scene_cmd : scene_query_command
    {
        close_scene_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "CloseScene", pDataBase) { RegisterArguments(); }
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

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            auto& State = get<editor_context>().m_State;
            const bool bWasOpen = std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) != State.m_OpenScenes.end();

            e29::CloseScene(World(), State, SceneGuid);

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
    struct list_levels_query_cmd : scene_query_command
    {
        list_levels_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "ListLevels", pDataBase) { RegisterArguments(); }
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
    struct list_scenes_query_cmd : scene_query_command
    {
        list_scenes_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "ListScenes", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the scenes owned by a Level (default: the currently open one). Usage: ListScenes [-Level hexguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hLevel = m_Parser.addOption("Level", "Level instance guid, 16 hex digits (default: the currently open level)", false, 1);
        }

        std::string Query() noexcept override
        {
            auto& State = get<editor_context>().m_State;

            auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
            const auto LevelGuid = std::holds_alternative<xerr>(LevelArg)
                ? State.m_CurrentLevel
                : xecs::level::guid{ .m_Instance = { std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16) } };

            auto* pLevel = World().m_LevelMgr.Find(LevelGuid);
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
    // uses (level/E29_Panel_LevelTree.h).
    //================================================================================================
    struct list_entities_query_cmd : scene_query_command
    {
        list_entities_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "ListEntities", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every entity in a scene (hex id + name). Usage: ListEntities -Scene hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            if (std::holds_alternative<xerr>(SceneArg)) return "ListEntities: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            auto* pScene = World().m_SceneMgr.Find(SceneGuid);
            if (!pScene) return std::format("ListEntities: Scene {} is not open", FormatSceneGuid(SceneGuid));

            std::string Out;
            for (auto& [Id, Entity] : pScene->m_LocalToRuntime)
            {
                std::string Label = std::format("Entity #{:08X}", Id);
                if (auto& Details = World().m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool)
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
    struct list_folders_query_cmd : scene_query_command
    {
        list_folders_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "ListFolders", pDataBase) { RegisterArguments(); }
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

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            auto* pScene = World().m_SceneMgr.Find(SceneGuid);
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

//================================================================================================
// AddScene - Redo appends Scene to Level.m_Scenes (no OpenScene, no LevelMgr.Save). Undo erases
// it again if we were the ones who added it. Already-present is a no-op both ways (UI already
// skips Run when present; Redo still guards so a CLI re-add cannot duplicate).
//
// Usage: AddScene -Level hexguid -Scene hexguid
//================================================================================================
struct add_scene_cmd : scene_command
{
    add_scene_cmd(xundo::system& System, void* pDataBase) noexcept : scene_command(System, "AddScene", pDataBase) { RegisterArguments(); }
    const char* getCommandHelp() const noexcept override
    {
        return "Adds a Scene to a Level's membership list (undoable). Does not open the scene or save the level. Usage: AddScene -Level hexguid -Scene hexguid";
    }
    void RegisterArguments() noexcept override
    {
        m_hLevel = m_Parser.addOption("Level", "Level instance guid, 16 hex digits", true, 1);
        m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits",          true, 1);
    }

    std::string Redo() noexcept override
    {
        auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
        auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
        if (std::holds_alternative<xerr>(LevelArg) || std::holds_alternative<xerr>(SceneArg))
            return "AddScene: bad arguments";

        const auto LevelGuid = xecs::level::guid{ .m_Instance = { std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16) } };
        const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));

        auto* pLevel = World().m_LevelMgr.Find(LevelGuid);
        if (!pLevel) return "AddScene: level not found";

        // Already a member: empty success, no mutation. Backup wrote bWasPresent=1 so Undo is a no-op.
        if (std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), SceneGuid) != pLevel->m_Scenes.end())
            return {};

        pLevel->m_Scenes.push_back(SceneGuid);
        return {};
    }

    void BackupCurrenState(xundo::undo_file& File) noexcept override
    {
        // Runs BEFORE Redo. Record whether Scene is already in the level so Undo can no-op on a
        // re-add, and erase only if we are about to be the ones who add it.
        auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
        auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);

        const std::uint64_t Level = std::holds_alternative<xerr>(LevelArg) ? 0 : std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16);
        const std::uint64_t Scene = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
        std::uint32_t       bWasPresent = 0;

        if (!std::holds_alternative<xerr>(LevelArg) && !std::holds_alternative<xerr>(SceneArg))
        {
            const auto LevelGuid = xecs::level::guid{ .m_Instance = { Level } };
            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            if (auto* pLevel = World().m_LevelMgr.Find(LevelGuid))
                bWasPresent = (std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), SceneGuid) != pLevel->m_Scenes.end()) ? 1u : 0u;
        }

        File.Write(Level);
        File.Write(Scene);
        File.Write(bWasPresent);
    }

    void Undo(xundo::undo_file& File) noexcept override
    {
        std::uint64_t Level = 0; File.Read(Level);
        std::uint64_t Scene = 0; File.Read(Scene);
        std::uint32_t bWasPresent = 0; File.Read(bWasPresent);

        if (bWasPresent) return;

        const auto LevelGuid = xecs::level::guid{ .m_Instance = { Level } };
        const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
        auto* pLevel = World().m_LevelMgr.Find(LevelGuid);
        if (!pLevel) return;

        if (auto It = std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), SceneGuid); It != pLevel->m_Scenes.end())
            pLevel->m_Scenes.erase(It);

        // If the user opened the scene after we added it (row expand), drop residency too.
        // CloseScene is a no-op when the guid is not in State.m_OpenScenes.
        auto& State = get<editor_context>().m_State;
        e29::CloseScene(World(), State, SceneGuid);
    }

    xcmdline::parser::handle m_hLevel, m_hScene;
};

//================================================================================================
// RemoveScene - Redo erases Scene from Level.m_Scenes and CloseScene's it. Undo inserts it back
// at the recorded index (clamped if the list shrank) and re-opens it if it was open.
//
// Usage: RemoveScene -Level hexguid -Scene hexguid
//================================================================================================
struct remove_scene_cmd : scene_command
{
    remove_scene_cmd(xundo::system& System, void* pDataBase) noexcept : scene_command(System, "RemoveScene", pDataBase) { RegisterArguments(); }
    const char* getCommandHelp() const noexcept override
    {
        return "Removes a Scene from a Level's membership list (undoable). Closes the scene if open. Does not save the level. Usage: RemoveScene -Level hexguid -Scene hexguid";
    }
    void RegisterArguments() noexcept override
    {
        m_hLevel = m_Parser.addOption("Level", "Level instance guid, 16 hex digits", true, 1);
        m_hScene = m_Parser.addOption("Scene", "Scene guid, 16 hex digits",          true, 1);
    }

    std::string Redo() noexcept override
    {
        auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
        auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
        if (std::holds_alternative<xerr>(LevelArg) || std::holds_alternative<xerr>(SceneArg))
            return "RemoveScene: bad arguments";

        const auto LevelGuid = xecs::level::guid{ .m_Instance = { std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16) } };
        const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));

        auto* pLevel = World().m_LevelMgr.Find(LevelGuid);
        if (!pLevel) return "RemoveScene: level not found";

        auto It = std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), SceneGuid);
        if (It == pLevel->m_Scenes.end()) return "RemoveScene: scene not in level";

        pLevel->m_Scenes.erase(It);

        auto& State = get<editor_context>().m_State;
        e29::CloseScene(World(), State, SceneGuid);
        return {};
    }

    void BackupCurrenState(xundo::undo_file& File) noexcept override
    {
        // Runs BEFORE Redo. Index is the current slot; bWasOpen is current residency so Undo can
        // restore both membership and OpenScene if the row was live.
        auto LevelArg = m_Parser.getOptionArgAs<std::string>(m_hLevel, 0);
        auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);

        const std::uint64_t Level = std::holds_alternative<xerr>(LevelArg) ? 0 : std::strtoull(std::get<std::string>(LevelArg).c_str(), nullptr, 16);
        const std::uint64_t Scene = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
        std::uint32_t       Index = 0;
        std::uint32_t       bWasOpen = 0;

        const auto LevelGuid = xecs::level::guid{ .m_Instance = { Level } };
        const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };

        if (auto* pLevel = World().m_LevelMgr.Find(LevelGuid))
        {
            auto It = std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), SceneGuid);
            if (It != pLevel->m_Scenes.end())
                Index = static_cast<std::uint32_t>(std::distance(pLevel->m_Scenes.begin(), It));
        }

        auto& State = get<editor_context>().m_State;
        bWasOpen = (std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) != State.m_OpenScenes.end()) ? 1u : 0u;

        File.Write(Level);
        File.Write(Scene);
        File.Write(Index);
        File.Write(bWasOpen);
    }

    void Undo(xundo::undo_file& File) noexcept override
    {
        std::uint64_t Level = 0; File.Read(Level);
        std::uint64_t Scene = 0; File.Read(Scene);
        std::uint32_t Index = 0; File.Read(Index);
        std::uint32_t bWasOpen = 0; File.Read(bWasOpen);


        const auto LevelGuid = xecs::level::guid{ .m_Instance = { Level } };
        const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
        auto* pLevel = World().m_LevelMgr.Find(LevelGuid);
        if (!pLevel) return;

        if (std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), SceneGuid) == pLevel->m_Scenes.end())
        {
            const auto Idx = std::min<std::size_t>(Index, pLevel->m_Scenes.size());
            pLevel->m_Scenes.insert(pLevel->m_Scenes.begin() + static_cast<std::ptrdiff_t>(Idx), SceneGuid);
        }

        if (bWasOpen)
        {
            auto& State = get<editor_context>().m_State;
            e29::OpenScene(World(), State, xresource::full_guid{ SceneGuid.m_Instance, xecs::scene::type_guid_v });
        }
    }

    xcmdline::parser::handle m_hLevel, m_hScene;
};
}

#endif // E29_COMMANDS_LEVEL_H
