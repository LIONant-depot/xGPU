#ifndef E29_COMMAND_CONTEXT_H
#define E29_COMMAND_CONTEXT_H
#pragma once

// e29_command_context - the "database" every E29 xundo command mutates, retrieved via
// command_base::get<e29_command_context>(). Direct port of E27_NodeOS's own
// node_os_command_context/BackupSelection/RestoreSelection (Editor/NodeOS_CommandContext.h) - see
// documentation/E29_LevelSceneEditor/command_undo_system_plan.md for the full phased plan this is step 1 of.
//
// Deliberately holds ONLY editor_state& - NOT a xecs::game_mgr::instance& the way E27_NodeOS's own
// context holds its node/link vectors directly. E29's pGameMgr is a unique_ptr that gets destroyed
// and reconstructed on every hot-reload (RebuildWorld, E29_GamePlugin.h) - a reference captured once
// at construction would dangle the moment the first reload happened, exactly the class of bug this
// whole session's earlier work (tree preservation, the m_ComponentInfoMap stale-pointer fix) was
// about. editor_state itself is never reconstructed this way (RebuildWorld mutates its fields, never
// replaces the object), so a reference to it is safe to hold - GameMgr access instead goes through
// e29::g_pGameMgr, the existing global E29_GamePlugin.h's own RebuildWorld already keeps correctly
// rebound after every reload (`g_pGameMgr = pGameMgr.get();`), rather than this context inventing a
// second, separately-maintained pointer to keep in sync.
//
// Meant to be included after editor_state (and e29::g_pGameMgr/xeditor::NotifyError) are already defined -
// via the kit umbrella (E29_LevelSceneEditorKit.h), or a caller that already includes it - same
// convention every other extracted kit/plugin module in this project already follows, rather than
// self-including the umbrella here (this header is itself reached FROM WITHIN the umbrella, via
// level/E29_Panel_LevelTree.h - a self-include would just bounce off E29_LevelSceneEditorKit.h's own
// include guard at that point, working by accident rather than by design).
#include "dependencies/xundo/source/xundo_system.h"
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "dependencies/xeditor/include/xeditor/serialize.h"

namespace e29::commands
{
    // One line of the Say/GetLog conversation (extensions/command_console/E29_Commands_Chat.h) - lets multiple AI/CLI
    // clients talking to the same running E29 session leave messages for each other over the Command
    // Console pipe. In-memory only, current session (matches E29Undo's own bAutoLoadSave=false choice
    // - a fresh conversation each run), deliberately separate from ConsoleLog
    // (extensions/command_console/E29_CommandConsolePipe.h, phase 5) - that log is command-dispatch echo/result text,
    // this one is purely a conversation transcript, so GetLog doesn't have to filter dispatch noise
    // out of what it returns.
    struct chat_message
    {
        std::string m_From;
        std::string m_Text;
    };

    struct e29_command_context
    {
        editor_state&             m_State;
        std::vector<chat_message> m_ChatLog;
    };

    // Shared by select_cmd/toggle_multi_select_cmd/clear_selection_cmd - all three snapshot/restore
    // the exact same fields. m_SelectedEntity (the live runtime handle) is deliberately NOT part of
    // the snapshot - it's a cache, always re-resolved fresh from {m_SelectedEntityScene,
    // m_SelectedEntityId} via the scene's own m_LocalToRuntime map on restore (through
    // e29::g_pGameMgr - see this file's own top comment for why), matching this project's own
    // standing rule of never carrying a raw runtime handle across a boundary where the world could
    // have changed underneath it - an Undo/Redo step is exactly such a boundary, potentially long
    // after the entity in question was last touched.
    inline void BackupSelection(e29_command_context& Ctx, xundo::undo_file& File) noexcept
    {
        auto& S = Ctx.m_State;
        File.Write(S.m_SelectedEntityId);
        File.Write(S.m_SelectedEntityScene);
        File.Write(static_cast<std::uint32_t>(S.m_MultiSelectedEntityIds.size()));
        for (auto Id : S.m_MultiSelectedEntityIds) File.Write(Id);
        File.Write(static_cast<std::uint32_t>(S.m_MultiSelectOrder.size()));
        for (auto Id : S.m_MultiSelectOrder) File.Write(Id);
        File.Write(S.m_MultiSelectScene);
    }

    inline void RestoreSelection(e29_command_context& Ctx, xundo::undo_file& File) noexcept
    {
        auto& S = Ctx.m_State;

        File.Read(S.m_SelectedEntityId);
        File.Read(S.m_SelectedEntityScene);

        S.m_SelectedEntity = {};
        if (S.m_SelectedEntityId != xecs::scene::invalid_permanent_id_v && e29::g_pGameMgr)
        {
            if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(S.m_SelectedEntityScene))
            {
                if (auto It = pScene->m_LocalToRuntime.find(S.m_SelectedEntityId); It != pScene->m_LocalToRuntime.end())
                    S.m_SelectedEntity = It->second;
            }
        }

        std::uint32_t Count = 0;
        File.Read(Count);
        S.m_MultiSelectedEntityIds.clear();
        for (std::uint32_t i = 0; i < Count; ++i)
        {
            xecs::scene::permanent_id Id{};
            File.Read(Id);
            S.m_MultiSelectedEntityIds.insert(Id);
        }

        File.Read(Count);
        S.m_MultiSelectOrder.clear();
        S.m_MultiSelectOrder.reserve(Count);
        for (std::uint32_t i = 0; i < Count; ++i)
        {
            xecs::scene::permanent_id Id{};
            File.Read(Id);
            S.m_MultiSelectOrder.push_back(Id);
        }

        File.Read(S.m_MultiSelectScene);
        S.m_bEntityInspectorDirty = true;
    }

    // Parses a scene guid formatted as 16 hex digits (see FormatSceneGuid, below) back into a
    // xecs::scene::guid - the two are always used as a pair, never guid <-> guid elsewhere in the
    // codebase, since this hex text form only exists for command-line argument round-tripping. Shared
    // by every command that needs to name a scene (selection, property edits, and whatever future
    // phases need it too) - not scoped to one command file.
    inline xecs::scene::guid ParseSceneGuid(std::string_view Text) noexcept
    {
        return xecs::scene::guid{ .m_Instance = { std::strtoull(std::string(Text).c_str(), nullptr, 16) } };
    }

    inline std::string FormatSceneGuid(xecs::scene::guid Guid) noexcept
    {
        return std::format("{:016X}", Guid.m_Instance.m_Value);
    }

    // Same shape as ParseSceneGuid/FormatSceneGuid - e10::library::guid is also a
    // xresource::def_guid<> (a single instance value, implicit compile-time type), same as
    // xecs::scene::guid/xecs::level::guid. Named separately (not just reused via a template) so a
    // command's own signature stays self-documenting about which guid space an argument names.
    inline e10::library::guid ParseLibraryGuid(std::string_view Text) noexcept
    {
        return e10::library::guid{ .m_Instance = { std::strtoull(std::string(Text).c_str(), nullptr, 16) } };
    }

    inline std::string FormatLibraryGuid(e10::library::guid Guid) noexcept
    {
        return std::format("{:016X}", Guid.m_Instance.m_Value);
    }

    // An asset guid (xresource::full_guid) is genuinely instance+type - unlike Scene/Level/Library's
    // def_guid<> (one fixed, compile-time-known type), an asset can be a Prefab, Scene, Level,
    // Texture, Material, ... resource, so the type half has to travel too. 32 hex digits (16 instance
    // + 16 type, concatenated with no separator) keeps this a single command-line token, same
    // "one guid, one argument" shape every other guid convention here already has.
    inline xresource::full_guid ParseAssetGuid(std::string_view Text) noexcept
    {
        if (Text.size() != 32) return {};
        const auto Instance = std::strtoull(std::string(Text.substr(0, 16)).c_str(), nullptr, 16);
        const auto Type     = std::strtoull(std::string(Text.substr(16, 16)).c_str(), nullptr, 16);
        return xresource::full_guid{ .m_Instance = { Instance }, .m_Type = { Type } };
    }

    inline std::string FormatAssetGuid(xresource::full_guid Guid) noexcept
    {
        return std::format("{:016X}{:016X}", Guid.m_Instance.m_Value, Guid.m_Type.m_Value);
    }

    // Parses/formats an entity permanent_id as 8 hex digits - standardizes it to match every other
    // id/guid a command ever takes (Scene/Component/TypeGuid/Level/Folder are all hex already).
    // permanent_id used to be the one remaining decimal field (parsed via plain std::stoul) - direct
    // user report: "I think we need to standardize the way we do GUIDs.... I think they should always
    // be in hex." xecs::scene::permanent_id is a plain std::uint32_t (xecs_scene.h), so 8 hex digits
    // matches Folder/TypeGuid's own existing width exactly, not an arbitrary new choice.
    inline xecs::scene::permanent_id ParseEntityId(std::string_view Text) noexcept
    {
        return static_cast<xecs::scene::permanent_id>(std::strtoul(std::string(Text).c_str(), nullptr, 16));
    }

    inline std::string FormatEntityId(xecs::scene::permanent_id Id) noexcept
    {
        return std::format("{:08X}", Id);
    }

    // xproperty::settings::AnyToString (my_properties.h, shared xproperty lib) only knows the
    // project-wide atomic_types_tuple - it has no case for xecs::component::entity, which is
    // registered as ITS OWN var_type<> specialization inside xECS instead
    // (xecs_entity_xproperty_bridge.h), specifically because my_properties.h can't see xecs types
    // without a circular include. That's fine for every existing caller (SetLivePropertyValue etc.
    // never had to print one back out as text) - but any generic property-value-to-string walk
    // (DescribeEntity, SnapshotComponentProperties for Add/Remove Component undo) hits a live
    // component with an entity_reference (or any other entity-handle-valued property) and asserts
    // in AnyToString's `default: assert(false)` - confirmed live via a real crash: DescribeEntity on
    // an entity carrying an EntityReference component. Fixed by special-casing entity's own guid
    // here, in E29 (the xECS consumer layer), matching the same layering the var_type<> bridge
    // itself already established, rather than teaching the shared xproperty lib about a type it's
    // architecturally not allowed to know about.
    inline int FormatPropertyValue(std::span<char> Buffer, const xproperty::any& Data) noexcept
    {
        if (Data.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v)
        {
            const auto E = Data.get<xecs::component::entity>();
            if (!E.isValid()) return sprintf_s(Buffer.data(), Buffer.size(), "invalid");
            return sprintf_s(Buffer.data(), Buffer.size(), "runtime-entity %016llX", (unsigned long long)E.m_Value);
        }
        return xproperty::settings::AnyToString(Buffer, Data);
    }

}

#endif // E29_COMMAND_CONTEXT_H
