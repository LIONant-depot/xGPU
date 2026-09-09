#ifndef E29_COMMAND_CONTEXT_H
#define E29_COMMAND_CONTEXT_H
#pragma once

// e29_command_context - the "database" every E29 xundo command mutates, retrieved via
// command_base::get<e29_command_context>(). Direct port of E27_NodeOS's own
// node_os_command_context/BackupSelection/RestoreSelection (Editor/NodeOS_CommandContext.h) - see
// [[e29_command_undo_system_plan]] (memory) for the full phased plan this is step 1 of.
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
// Meant to be included after editor_state (and e29::g_pGameMgr/e29::Debugger) are already defined -
// via the kit umbrella (E29_LevelSceneEditorKit.h), or a caller that already includes it - same
// convention every other extracted kit/plugin module in this project already follows, rather than
// self-including the umbrella here (this header is itself reached FROM WITHIN the umbrella, via
// kit/E29_Panel_LevelTree.h - a self-include would just bounce off E29_LevelSceneEditorKit.h's own
// include guard at that point, working by accident rather than by design).
#include "dependencies/xundo/source/xundo_system.h"

namespace e29::commands
{
    // Who authored a Command Console log entry - moved here (from commands/E29_CommandConsolePipe.h,
    // phase 5) so Run() below (a phase 1 foundational helper, included far earlier than phase 5's own
    // pipe file) can log every command through the SAME shared log a pipe-driven or console-typed one
    // already uses, without a forward-declaration/ordering problem. User=green (typed into the
    // console OR clicked in the UI - both are "the user did this," see Run()'s own comment for why
    // this reuses User rather than inventing a third category just for UI clicks), Pipe=teal
    // (arrived over E29CLI's named pipe - an AI-facing color on purpose).
    enum class console_log_source { System, User, Pipe };
    struct console_log_entry
    {
        std::string         m_Text;
        console_log_source  m_Source;
    };

    // Set once, right after ConsoleLog itself is constructed in E29_LevelScene_Editor.cpp's main
    // function (same "global pointer bound once at startup" pattern as e29::g_pGameMgr/g_pState,
    // E29_PrefabAuthoring.h) - Run() is called from many files (kit/E29_Panel_LevelTree.h,
    // E29_LevelSceneEditorKit.h's m_OnPropertyChanged, kit/E29_Panel_EntityProperties.h, ...), so a
    // global pointer avoids threading a ConsoleLog& parameter through every one of those call sites
    // just for this.
    inline std::vector<console_log_entry>* g_pConsoleLog = nullptr;

    // One line of the Say/GetLog conversation (commands/E29_Commands_Chat.h) - lets multiple AI/CLI
    // clients talking to the same running E29 session leave messages for each other over the Command
    // Console pipe. In-memory only, current session (matches E29Undo's own bAutoLoadSave=false choice
    // - a fresh conversation each run), deliberately separate from ConsoleLog
    // (commands/E29_CommandConsolePipe.h, phase 5) - that log is command-dispatch echo/result text,
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

    // Wraps xundo::system::Execute with logging - EVERY command, not just failures, so the log is a
    // genuine audit trail of everything that happened (the same log an external CLI-driven agent
    // would see - phase 6 finally gave this comment's own original intent somewhere to log TO,
    // completing what was documented but not yet wired up since phase 1) - direct port of
    // E27_NodeOS's own Run() (Editor/NodeOS_CommandBuilders.h). Pushes the SAME echo-then-result
    // shape ProcessConsoleCommand/DrawCommandConsolePanel already use for a typed/piped command
    // (commands/E29_CommandConsolePipe.h, kit/E29_Panel_CommandConsole.h), tagged User - a UI click
    // and a typed command are both "the user did this" from the log's own point of view. Direct user
    // report this fixes: "now you have to route the users commands there as well... nothing showing
    // up there yet."
    inline void Run(xundo::system& System, const std::string& Cmd) noexcept
    {
        if (g_pConsoleLog) g_pConsoleLog->push_back({ Cmd, console_log_source::User });
        if (auto Err = System.Execute(Cmd); !Err.empty())
        {
            Debugger(std::format("E29: command failed: '{}' ({})", Cmd, Err));
            if (g_pConsoleLog) g_pConsoleLog->push_back({ Err, console_log_source::System });
        }
    }

    // WriteString/ReadString - a length-prefixed string inside a fixed-record undo_file, direct port
    // of E27_NodeOS's own (Editor/NodeOS_CommandBuilders.h). Base64Encode/Decode - direct port too,
    // used ONLY for a property's own serialized value/path text (arbitrary content that could contain
    // characters awkward for a space-delimited command line) - plain ids need no encoding at all.
    // Copied rather than shared across examples on purpose - E10/E27/E29 don't depend on each other,
    // matching this codebase's own per-example kit convention; this is small, self-contained utility
    // code, not state that could ever drift out of sync between two copies.
    inline void WriteString(xundo::undo_file& File, const std::string& S) noexcept
    {
        const std::uint32_t Len = static_cast<std::uint32_t>(S.size());
        File.Write(Len);
        if (Len) File.Write(S.data(), Len);
    }
    inline std::string ReadString(xundo::undo_file& File) noexcept
    {
        std::uint32_t Len = 0; File.Read(Len);
        std::string S; S.resize(Len);
        if (Len) File.Read(S.data(), Len);
        return S;
    }

    inline std::string Base64Encode(const std::string& In) noexcept
    {
        static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string Out;
        Out.reserve(((In.size() + 2) / 3) * 4);
        std::size_t i = 0;
        for (; i + 2 < In.size(); i += 3)
        {
            const std::uint32_t N = (std::uint32_t(std::uint8_t(In[i])) << 16) | (std::uint32_t(std::uint8_t(In[i + 1])) << 8) | std::uint8_t(In[i + 2]);
            Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F];
            Out += Alphabet[(N >> 6) & 0x3F];  Out += Alphabet[N & 0x3F];
        }
        const std::size_t Rem = In.size() - i;
        if (Rem == 1)
        {
            const std::uint32_t N = std::uint32_t(std::uint8_t(In[i])) << 16;
            Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F]; Out += "==";
        }
        else if (Rem == 2)
        {
            const std::uint32_t N = (std::uint32_t(std::uint8_t(In[i])) << 16) | (std::uint32_t(std::uint8_t(In[i + 1])) << 8);
            Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F]; Out += Alphabet[(N >> 6) & 0x3F]; Out += '=';
        }
        return Out;
    }

    inline std::string Base64Decode(const std::string& In) noexcept
    {
        auto DecodeChar = [](char C) -> int
        {
            if (C >= 'A' && C <= 'Z') return C - 'A';
            if (C >= 'a' && C <= 'z') return C - 'a' + 26;
            if (C >= '0' && C <= '9') return C - '0' + 52;
            if (C == '+') return 62;
            if (C == '/') return 63;
            return -1; // padding ('=') or terminator
        };
        std::string Out;
        Out.reserve((In.size() / 4) * 3);
        int Bits = 0, NumBits = 0;
        for (char C : In)
        {
            const int V = DecodeChar(C);
            if (V < 0) break;
            Bits = (Bits << 6) | V;
            NumBits += 6;
            if (NumBits >= 8)
            {
                NumBits -= 8;
                Out += static_cast<char>((Bits >> NumBits) & 0xFF);
            }
        }
        return Out;
    }
}

#endif // E29_COMMAND_CONTEXT_H
