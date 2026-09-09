#ifndef E29_COMMANDS_CHAT_H
#define E29_COMMANDS_CHAT_H
#pragma once

// Say/GetLog - a small extension riding on phase 5's own Command Console pipe
// ([[e29_command_undo_system_plan]] memory), direct user request: "add the ability [to] personalize
// commands... so if there are multiple AIs you guys can have a conversation". Two QUERY commands
// (xundo::query_command_base, not command_base) - a conversation message is not an undo-able mutation
// of the scene/entities, so it doesn't belong in the Edit namespace or the undo timeline at all,
// matching query_command_base's own documented purpose ("debugging/introspection/AI-facing
// questions... as opposed to command_base's mutations").
//
// -From is left as a PLAIN argument (not Base64-encoded) on purpose, unlike -Text: an agent name is
// an identifier (like Scene/Id/Component elsewhere in this codebase), not free-form content, and is
// never expected to contain a space - see [[e29_command_undo_known_gaps]]'s own "standing rule" entry
// for the full reasoning on why xcmdline::parser's naive space/tab tokenizer forces free text through
// Base64 (-Text, here) but never single-token identifiers.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    //================================================================================================
    // Say - appends one message to the in-memory chat log (e29_command_context::m_ChatLog). Returns
    // an echo of exactly what got recorded ("[From] Text") rather than an empty success - unlike a
    // mutating Edit command's own silent-success convention, an AI sending a message benefits from
    // seeing its own message land intact (the Base64 round trip is otherwise invisible from the
    // caller's side).
    //================================================================================================
    struct say_query_cmd : xundo::query_command_base
    {
        say_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Say", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Leaves a message in the shared chat log for other AI/CLI clients to read via GetLog. Usage: Say -From name -Text base64";
        }
        void RegisterArguments() noexcept override
        {
            m_hFrom = m_Parser.addOption("From", "Who's speaking (a short name, e.g. Claude)", true, 1);
            m_hText = m_Parser.addOption("Text", "The message, base64-encoded",                true, 1);
        }

        std::string Query() noexcept override
        {
            auto FromArg = m_Parser.getOptionArgAs<std::string>(m_hFrom, 0);
            auto TextArg = m_Parser.getOptionArgAs<std::string>(m_hText, 0);
            if (std::holds_alternative<xerr>(FromArg) || std::holds_alternative<xerr>(TextArg))
                return "Say: bad arguments";

            const auto From = std::get<std::string>(FromArg);
            const auto Text = Base64Decode(std::get<std::string>(TextArg));

            get<e29_command_context>().m_ChatLog.push_back({ From, Text });
            return std::format("[{}] {}", From, Text);
        }

        xcmdline::parser::handle m_hFrom, m_hText;
    };

    //================================================================================================
    // GetLog - returns the last N chat messages (default 10, matching every other command's "sensible
    // default rather than requiring every argument" convention where the value is genuinely optional)
    // as one "[From] Text" line each, oldest-of-the-shown-N first (reads top-to-bottom like a real
    // transcript, same as `tail -n`), so an AI polling this sees a normal conversation, not a
    // reversed one.
    //================================================================================================
    struct get_log_query_cmd : xundo::query_command_base
    {
        get_log_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "GetLog", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Returns the last N chat messages (default 10). Usage: GetLog [-Count n]";
        }
        void RegisterArguments() noexcept override
        {
            m_hCount = m_Parser.addOption("Count", "How many recent messages to return (default 10)", false, 1);
        }

        std::string Query() noexcept override
        {
            auto CountArg = m_Parser.getOptionArgAs<std::string>(m_hCount, 0);
            const std::size_t Count = std::holds_alternative<xerr>(CountArg) ? 10 : static_cast<std::size_t>(std::stoul(std::get<std::string>(CountArg)));

            auto& ChatLog = get<e29_command_context>().m_ChatLog;
            const std::size_t Start = ChatLog.size() > Count ? ChatLog.size() - Count : 0;

            std::string Out;
            for (std::size_t i = Start; i < ChatLog.size(); ++i)
                Out += std::format("[{}] {}\n", ChatLog[i].m_From, ChatLog[i].m_Text);
            return Out;
        }

        xcmdline::parser::handle m_hCount;
    };
}

#endif // E29_COMMANDS_CHAT_H
