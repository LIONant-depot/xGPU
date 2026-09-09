#ifndef E29_COMMAND_CONSOLE_PIPE_H
#define E29_COMMAND_CONSOLE_PIPE_H
#pragma once

// Named-pipe server - phase 5 of [[e29_command_undo_system_plan]] (memory). Near-direct port of
// E27_NodeOS's own command_console_pipe_bridge/CommandConsolePipeThreadMain/PumpCommandConsolePipe/
// ProcessConsoleCommand (source/Examples/E27_NodeOS/Editor/NodeOS_UI_CommandConsole.h) - lets an
// external process (a script, an AI, the E29CLI.cpp client below) drive E29 through the SAME
// xundo::history::Route() dispatch every phase 1-4 command is already registered with
// ("E29/Edit/SetProperty -Scene ... -Id ... ", "E29/Query/..."), with zero UI automation.
//
// Deliberately does NOT port E27's own DrawCommandConsolePanel (the in-app ImGui text box +
// autocomplete + colored TextEditor log) - that's phase 6's job, once there's an actual UI worth
// building around this. console_log_entry/console_log_source are ported now anyway (not deferred)
// because PumpCommandConsolePipe already needs somewhere to append what a pipe-driven command did -
// phase 6 just needs to RENDER the same log this phase already produces, not invent it.
//
// Threading model, same as E27's own (see CommandConsolePipeThreadMain's own comment for the full
// reasoning): the pipe thread only ever reads request text and hands it to the main thread via
// command_console_pipe_bridge (mutex + condition_variable) - Route() ultimately calls into command
// Redo()/Query() implementations that read/mutate the SAME GameMgr/Scene state the main thread draws
// and edits every frame, so actually dispatching a command must happen on the main thread, once per
// frame, via PumpCommandConsolePipe - never on this background thread.
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "dependencies/xundo/source/xundo_history.h"

namespace e29
{
    enum class console_log_source { System, User, Pipe };
    struct console_log_entry
    {
        std::string         m_Text;
        console_log_source  m_Source;
    };

    // Shared by the named-pipe server (below) and, once it exists, phase 6's own Command Console
    // panel - one place implementing "help"/"<cmd> -h"/plain routing, so a pipe-driven command
    // behaves identically to one typed into a future UI, not a second, silently-drifting copy of the
    // same dispatch logic. Direct port of E27_NodeOS's own ProcessConsoleCommand.
    inline std::string ProcessConsoleCommand(std::string_view Cmd, xundo::history& History, const std::vector<xundo::history::routable_command>& Routable)
    {
        std::string Out;
        if (Cmd == "help" || Cmd == "Help" || Cmd == "?")
        {
            for (auto& C : Routable)
                Out += (C.m_Help.empty() ? C.m_FullName : std::format("{} - {}", C.m_FullName, C.m_Help)) + "\n";
        }
        else if (Cmd.size() > 2 && Cmd.substr(Cmd.size() - 2) == "-h")
        {
            // Bypasses Route() here on purpose: the underlying command's own "-h" handling
            // (xundo::system::Execute/Query -> xcmdline::parser::printHelp()) writes straight to
            // std::cout - nothing this GUI process's console window or a pipe client ever sees - so
            // look the help text up directly instead of dispatching.
            std::string_view FullName = Cmd.substr(0, Cmd.find(' '));
            Out = History.GetCommandHelpFor(FullName) + "\n";
        }
        else
        {
            // Route()'s empty-string return is ambiguous on purpose for Edit commands ("ran fine,
            // nothing worth reporting" - xundo::system::Execute's own convention) - matches E29's
            // own set_property_cmd/etc, which all return {} on success. Only Query commands (none
            // registered for E29 yet) get an explicit "(empty result)" placeholder, same as E27.
            std::string Result = History.Route(Cmd);
            if (!Result.empty())
                Out = Result + "\n";
            else if (Cmd.find("/Query/") != std::string_view::npos)
                Out = "(empty result)\n";
        }
        return Out;
    }

    //------------------------------------------------------------------------------------------------
    // Named-pipe server for E29CLI.cpp - connect to \\.\pipe\E29_LevelSceneEditor_Console, write one
    // command line, read back the response, disconnect. Runs on its own detached background thread
    // for the app's whole lifetime - a local dev/debug feature, not something that needs a graceful
    // shutdown path (ConnectNamedPipe blocks indefinitely with no cancellation plumbing here, same as
    // E27's own reasoning - not worth building one for this).
    //------------------------------------------------------------------------------------------------
    struct command_console_pipe_bridge
    {
        std::mutex              m_Mutex;
        std::condition_variable m_Cond;
        bool                    m_bHasRequest  = false; // pipe thread -> main thread: a command is waiting
        bool                    m_bHasResponse = false; // main thread -> pipe thread: the answer is ready
        std::string             m_Request;
        std::string             m_Response;
    };

    inline void CommandConsolePipeThreadMain(command_console_pipe_bridge& Bridge) noexcept
    {
        for (;;)
        {
            HANDLE hPipe = CreateNamedPipeA(
                "\\\\.\\pipe\\E29_LevelSceneEditor_Console",
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,      // one client at a time - E29CLI is a one-shot connect/send/read/exit tool
                65536, 65536,
                0, nullptr);
            if (hPipe == INVALID_HANDLE_VALUE)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            const BOOL bConnected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (bConnected)
            {
                std::string Request;
                char        Buf[4096];
                DWORD       BytesRead = 0;
                while (ReadFile(hPipe, Buf, sizeof(Buf) - 1, &BytesRead, nullptr) && BytesRead > 0)
                {
                    Buf[BytesRead] = 0;
                    Request += Buf;
                    if (Request.find('\n') != std::string::npos) break;
                }
                while (!Request.empty() && (Request.back() == '\n' || Request.back() == '\r')) Request.pop_back();

                std::string Response;
                if (!Request.empty())
                {
                    std::unique_lock<std::mutex> Lock(Bridge.m_Mutex);
                    Bridge.m_Request      = Request;
                    Bridge.m_bHasRequest  = true;
                    Bridge.m_bHasResponse = false;
                    Bridge.m_Cond.notify_all();
                    Bridge.m_Cond.wait(Lock, [&] { return Bridge.m_bHasResponse; });
                    Response = Bridge.m_Response;
                }

                DWORD BytesWritten = 0;
                WriteFile(hPipe, Response.data(), static_cast<DWORD>(Response.size()), &BytesWritten, nullptr);
                FlushFileBuffers(hPipe);
            }
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
    }

    // Called once per frame from the main loop (E29_LevelScene_Editor.cpp), grouped with
    // PollGameReload BEFORE BeginRendering - not after, unlike E27's own placement (which runs its
    // pump after BeginRendering): E29's own established convention (PollGameReload's own comment)
    // found that heavy state mutation nested inside an active ImGui frame corrupts its window-stack
    // bookkeeping, and a pipe-driven command can be just as heavy as a button click (e.g.
    // DeleteEntity's subtree walk) - so this follows PollGameReload's placement, not E27's.
    //
    // Appends into LogEntries (owned by the caller, not a local static) so a pipe-driven command is
    // visible in whatever future UI reads the same vector, exactly like one typed there - never a
    // silent side-channel. Tagged console_log_source::Pipe (not User) for the same reason E27 does -
    // an AI-facing distinction worth keeping even before phase 6 gives it a color.
    inline void PumpCommandConsolePipe(command_console_pipe_bridge& Bridge, xundo::history& History, std::vector<console_log_entry>& LogEntries) noexcept
    {
        std::unique_lock<std::mutex> Lock(Bridge.m_Mutex, std::try_to_lock);
        if (!Lock.owns_lock() || !Bridge.m_bHasRequest || Bridge.m_bHasResponse)
            return;

        const std::string Cmd = Bridge.m_Request;
        Lock.unlock();

        const auto Routable = History.GetRoutableCommands();
        const std::string Result = ProcessConsoleCommand(Cmd, History, Routable);

        LogEntries.push_back({ Cmd, console_log_source::Pipe });
        if (!Result.empty())
            LogEntries.push_back({ Result, console_log_source::System });

        Lock.lock();
        Bridge.m_Response     = Result;
        Bridge.m_bHasResponse = true;
        Bridge.m_bHasRequest  = false;
        Lock.unlock();
        Bridge.m_Cond.notify_all();
    }
}

#endif // E29_COMMAND_CONSOLE_PIPE_H
