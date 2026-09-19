// xeditorcli - generic CLI client for xeditor::ConsolePipeThreadMain, replacing the
// per-editor-CLI pattern (E29CLI.cpp/NodeOSCLI.cpp) for editors built on the new shared framework.
// Direct port of E29CLI.cpp's own connect/send/read/exit mechanics - zero dependency on the rest of
// xGPU, just Win32 + iostream.
//
// Usage: xeditorcli "<command>" [--pipe \\.\pipe\Name]   (default pipe: \\.\pipe\xEditor_Console)
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kDefaultPipeName = "\\\\.\\pipe\\xEditor_Console";
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: xeditorcli \"<command>\" [--pipe \\\\.\\pipe\\Name]\n";
        return 1;
    }

    std::string PipeName = kDefaultPipeName;
    std::vector<std::string> CommandWords;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--pipe" && i + 1 < argc) { PipeName = argv[++i]; continue; }
        CommandWords.push_back(argv[i]);
    }
    if (CommandWords.empty())
    {
        std::cerr << "Usage: xeditorcli \"<command>\" [--pipe \\\\.\\pipe\\Name]\n";
        return 1;
    }

    std::string Command = CommandWords[0];
    for (std::size_t i = 1; i < CommandWords.size(); ++i) { Command += ' '; Command += CommandWords[i]; }

    // A few short retries: the server side accepts one connection at a time and immediately loops
    // for the next one between requests, so ERROR_PIPE_BUSY just means "mid-turnaround."
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int Attempt = 0; Attempt < 5; ++Attempt)
    {
        hPipe = CreateFileA(PipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hPipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeA(PipeName.c_str(), 2000);
    }
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Could not connect to pipe '" << PipeName << "' (GetLastError=" << GetLastError() << ")\n";
        return 2;
    }

    Command += '\n';
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
