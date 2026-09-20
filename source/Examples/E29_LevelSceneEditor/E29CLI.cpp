// E29CLI - thin client for the live editor's Command Console named pipe.
// Same transport as before; the editor prefers xeditor::host::dispatch for
// modern commands (help/list/bare workspace names/Name\Cmd). Legacy
// "E29/Edit/..." and "E29/Query/..." still work via History.Route so AI/scripts
// never lose access. Prefer short names going forward, e.g.:
//   E29CLI "list"
//   E29CLI "LevelName\AddComponent -Scene ... -Id ..."
//   E29CLI "OpenTextureEditor -Library ... -Asset ..."
//   E29CLI "E29/Edit/Select -Scene ... -Id ..."   (legacy, still supported)
//
#include <windows.h>
#include <iostream>
#include <string>

namespace
{
    constexpr const char* kPipeName = "\\\\.\\pipe\\xEditor_Console";
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
