// E29CLI - a tiny, standalone client for E29_LevelSceneEditor's Command Console named pipe. Direct
// port of E27_NodeOS's own NodeOSCLI.cpp - deliberately has zero dependency on the rest of xGPU (no
// Vulkan/ImGui/xproperty/xECS/etc.) - just Win32 + iostream - so it builds fast and stays a plain,
// independent CLI tool: connect, send one routed command string
// ("E29/Edit/Select -Scene ... -Id ...", "help", "<cmd> -h"), print whatever the running app's own
// xundo::history::Route()/help dispatch returns, exit. This is what lets an external tool (a script,
// an AI) drive a live E29 session without any UI automation at all - the same text protocol phase 6's
// Command Console panel will eventually offer a human too.
//
// Usage: E29CLI "E29/Edit/Select -Scene 08C298C9F6668005 -Id 1266979020"
//        E29CLI E29/Edit/Select -Scene 08C298C9F6668005 -Id 1266979020   (unquoted words rejoined with spaces)
#include <windows.h>
#include <iostream>
#include <string>

namespace
{
    constexpr const char* kPipeName = "\\\\.\\pipe\\E29_LevelSceneEditor_Console";
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: E29CLI \"<command>\"  (e.g. E29CLI \"help\")\n";
        return 1;
    }

    std::string Command = argv[1];
    for (int i = 2; i < argc; ++i) { Command += ' '; Command += argv[i]; }

    // A few short retries: the server side only accepts one connection at a time and immediately
    // loops for the next one between requests, so ERROR_PIPE_BUSY here just means "mid-turnaround
    // with another client," not "not running."
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int Attempt = 0; Attempt < 5; ++Attempt)
    {
        hPipe = CreateFileA(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hPipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeA(kPipeName, 2000);
    }
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Could not connect to E29's Command Console pipe - is xGPU_unit_test running with the E29_LevelSceneEditor example open? (GetLastError=" << GetLastError() << ")\n";
        return 2;
    }

    Command += '\n'; // server reads until the first newline
    DWORD BytesWritten = 0;
    WriteFile(hPipe, Command.data(), static_cast<DWORD>(Command.size()), &BytesWritten, nullptr);

    std::string Response;
    char Buf[4096];
    DWORD BytesRead = 0;
    while (ReadFile(hPipe, Buf, sizeof(Buf), &BytesRead, nullptr) && BytesRead > 0)
        Response.append(Buf, BytesRead);

    CloseHandle(hPipe);
    std::cout << Response;
    return 0;
}
