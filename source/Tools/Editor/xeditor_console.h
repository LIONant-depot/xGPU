#ifndef XEDITOR_CONSOLE_H
#define XEDITOR_CONSOLE_H
#pragma once

// Shared named-pipe console, generalized from E29_LevelSceneEditor's own
// commands/E29_CommandConsolePipe.h (itself a port of E27_NodeOS's pipe bridge) - same proven
// mechanics (a background thread owns the pipe, hands request text to the main thread via a
// mutex+condition_variable bridge, the main thread pumps it once per frame or once per headless
// loop iteration), generalized so any editor process can stand one up under its own pipe name
// instead of hand-copying this file a third time. Dispatches straight into an xundo::system's own
// Execute()/Query() - xundo::system already does name-based command lookup itself, so no separate
// xundo::history routing layer is needed for a single-document console.
#include "dependencies/xundo/source/xundo_system.h"
#ifndef NOMINMAX
#define NOMINMAX // avoid windows.h's min/max macros clobbering std::min/std::max in whatever else this translation unit includes later
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR // windows.h's wingdi.h macro collides with this codebase's own ERROR enum values (e.g. xresource_pipeline.h's msg_type::ERROR) when this header is included ahead of them in a translation unit
#endif
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <algorithm>
#include <functional>

namespace xeditor
{
    struct console_bridge
    {
        std::mutex              m_Mutex;
        std::condition_variable m_Cond;
        bool                    m_bHasRequest  = false;
        bool                    m_bHasResponse = false;
        std::string             m_Request;
        std::string             m_Response;
    };

    inline void ConsolePipeThreadMain(console_bridge& Bridge, std::string PipeName) noexcept
    {
        for (;;)
        {
            HANDLE hPipe = CreateNamedPipeA(
                PipeName.c_str(),
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1, 65536, 65536, 0, nullptr);
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

    // Called once per frame (interactive host) or once per loop iteration (headless host, §7.7's
    // logic-headless tier - no window, no ImGui, this loop IS the process's only job). "query.*"
    // style verbs vs mutating commands are distinguished the same way E29 already does (command
    // name convention), so this stays a thin pump, not a second dispatch layer.
    inline void PumpConsole(console_bridge& Bridge, xundo::system& System) noexcept
    {
        std::unique_lock<std::mutex> Lock(Bridge.m_Mutex, std::try_to_lock);
        if (!Lock.owns_lock() || !Bridge.m_bHasRequest || Bridge.m_bHasResponse)
            return;

        const std::string Cmd = Bridge.m_Request;
        Lock.unlock();

        // xundo::system keeps Edit and Query commands in two entirely separate maps/methods
        // (Execute() vs Query()) - calling the wrong one just returns a generic "not found"
        // string rather than dispatching, so the name has to be checked against the right map
        // first (mirrors e29::commands::Run/E29_CommandConsolePipe's own dual-registry handling).
        const std::string Name = std::string(Cmd.substr(0, Cmd.find(' ')));
        std::string Result;
        {
            const auto QueryNames = System.GetQueryCommandNames();
            const bool bIsQuery   = std::find(QueryNames.begin(), QueryNames.end(), Name) != QueryNames.end();
            Result = bIsQuery ? System.Query(Cmd) : System.Execute(Cmd);
        }

        Lock.lock();
        Bridge.m_Response     = Result;
        Bridge.m_bHasResponse = true;
        Bridge.m_bHasRequest  = false;
        Lock.unlock();
        Bridge.m_Cond.notify_all();
    }
}

#endif // XEDITOR_CONSOLE_H
