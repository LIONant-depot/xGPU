// sc_process_runner.hpp
//
// Minimal cross-platform subprocess execution: run a command, optionally
// feed it stdin, capture stdout/stderr separately, get the exit code,
// and set a working directory. No external dependencies.
//
// Revision notes (fixes applied after review):
//   - Added optional stdin input, needed for batched `git check-attr
//     --stdin` queries (avoids one subprocess per file).
//   - POSIX: pipes are cleaned up on every setup-failure path (no more
//     leaked descriptors if the second/third pipe() call fails or if
//     fork() fails after some pipes were already created).
//   - POSIX: EINTR is now retried around poll/read/write/waitpid instead
//     of being treated as an error or a spurious EOF.
//   - Windows: argument quoting replaced with the standard algorithm
//     that correctly handles embedded quotes and runs of backslashes
//     before a quote or at the end of an argument (the previous version
//     could corrupt paths ending in backslashes or messages containing
//     quotes).
//   - FIXED (found via a real hang, not a review note): both platforms
//     used to write all of stdin to completion BEFORE reading any
//     output. That deadlocks once the payload is large enough -- the
//     child fills its stdout pipe buffer and blocks on its own write()
//     before it has drained enough of stdin for the parent to finish
//     writing, while the parent is still blocked writing stdin waiting
//     for the child to read more. Hit for real running GetStatus's
//     BatchIsLfsTracked against ~170 changed paths in a real project.
//     POSIX now multiplexes the stdin-write fd into the same poll()
//     loop as the two read fds; Windows now runs the stdin write and
//     both reads concurrently on their own threads. Neither platform
//     writes stdin to completion up front anymore.
//
// Still-deferred limitations (documented, not fixed in this pass -- see
// "must fix before production" in the review this revision responds to):
//   - No cancellation or timeout: a hung `git` process (e.g. waiting on
//     a credential prompt) blocks the calling thread indefinitely.
//     Fixing this properly requires a process handle/PID exposed to the
//     caller plus platform-specific termination (posix: kill process
//     group; Windows: TerminateProcess + job objects) -- planned as a
//     follow-up, not attempted here to keep this change reviewable.

#pragma once

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace sc::process
{

struct ProcessResult
{
    int exitCode = -1;
    std::string stdOut;
    std::string stdErr;
    bool launchFailed = false;
    std::string launchError;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return !launchFailed && exitCode == 0;
    }
};

class ProcessRunner
{
public:
    // `args[0]` is the executable name (resolved via PATH), the rest are
    // arguments passed through without shell interpretation. `stdinInput`,
    // if non-empty, is written fully to the child's stdin before its
    // output is read.
    [[nodiscard]] static ProcessResult Run(
        const std::vector<std::string>& args,
        const std::filesystem::path& workingDirectory,
        std::string_view stdinInput = {})
    {
#if defined(_WIN32)
        return RunWindows(args, workingDirectory, stdinInput);
#else
        return RunPosix(args, workingDirectory, stdinInput);
#endif
    }

private:
#if !defined(_WIN32)
    static ProcessResult RunPosix(
        const std::vector<std::string>& args,
        const std::filesystem::path& cwd,
        std::string_view stdinInput)
    {
        ProcessResult result;

        int inPipe[2]  = { -1, -1 };
        int outPipe[2] = { -1, -1 };
        int errPipe[2] = { -1, -1 };

        auto closeAllOpen = [&]
        {
            for (int fd : { inPipe[0], inPipe[1], outPipe[0], outPipe[1], errPipe[0], errPipe[1] })
            {
                if (fd >= 0) close(fd);
            }
        };

        if (pipe(inPipe) != 0)
        {
            result.launchFailed = true;
            result.launchError = "pipe(stdin) failed";
            return result;
        }
        if (pipe(outPipe) != 0)
        {
            closeAllOpen();
            result.launchFailed = true;
            result.launchError = "pipe(stdout) failed";
            return result;
        }
        if (pipe(errPipe) != 0)
        {
            closeAllOpen();
            result.launchFailed = true;
            result.launchError = "pipe(stderr) failed";
            return result;
        }

        const pid_t pid = fork();
        if (pid < 0)
        {
            closeAllOpen();
            result.launchFailed = true;
            result.launchError = "fork() failed";
            return result;
        }

        if (pid == 0)
        {
            // Child process.
            dup2(inPipe[0], STDIN_FILENO);
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);
            closeAllOpen();

            if (!cwd.empty() && chdir(cwd.string().c_str()) != 0)
            {
                _exit(127);
            }

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& a : args)
            {
                argv.push_back(const_cast<char*>(a.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127); // exec only returns on failure
        }

        // Parent process.
        close(inPipe[0]);
        close(outPipe[1]);
        close(errPipe[1]);

        // BUG FIX (found via a real hang against ~170 paths batched into
        // `git check-attr --stdin`): writing all of stdin to completion
        // BEFORE reading any output is a classic bidirectional-pipe
        // deadlock once the payload is large enough -- the child fills its
        // stdout pipe buffer and blocks on write() before it has drained
        // enough of stdin for us to finish writing, while we are still
        // blocked in write(stdin) waiting for the child to read more. Stdin
        // writing must happen CONCURRENTLY with draining stdout/stderr, not
        // before it -- so the write fd joins the same poll loop as the two
        // read fds below, instead of being fully drained up front.
        if (stdinInput.empty())
        {
            close(inPipe[1]);
            inPipe[1] = -1;
        }
        else
        {
            fcntl(inPipe[1], F_SETFL, O_NONBLOCK);
        }

        ReadWritePipesNonBlocking(inPipe[1], outPipe[0], errPipe[0], stdinInput, result);

        close(outPipe[0]);
        close(errPipe[0]);

        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);

        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return result;
    }

    // Drives stdin-writing and stdout/stderr-reading concurrently via a
    // single poll() loop, so a large stdin payload can never deadlock
    // against a child that starts producing output before it has consumed
    // all of it (and vice versa). `inFd` may be -1 (nothing to write, e.g.
    // an already-empty stdinInput) -- the caller closes it up front in
    // that case and passes -1 here.
    static void ReadWritePipesNonBlocking(
        int inFd, int outFd, int errFd, std::string_view stdinInput, ProcessResult& result)
    {
        fcntl(outFd, F_SETFL, O_NONBLOCK);
        fcntl(errFd, F_SETFL, O_NONBLOCK);

        bool outDone = false;
        bool errDone = false;
        bool writeDone = (inFd < 0);
        std::size_t written = 0;
        char buffer[4096];

        while (!outDone || !errDone || !writeDone)
        {
            struct pollfd fds[3];
            int count = 0;
            int outSlot = -1, errSlot = -1, inSlot = -1;

            if (!outDone) { fds[count] = { outFd, POLLIN, 0 };  outSlot = count++; }
            if (!errDone) { fds[count] = { errFd, POLLIN, 0 };  errSlot = count++; }
            if (!writeDone) { fds[count] = { inFd, POLLOUT, 0 }; inSlot = count++; }

            if (count == 0) break;

            const int pollResult = poll(fds, static_cast<nfds_t>(count), -1);
            if (pollResult < 0)
            {
                if (errno == EINTR) continue;
                break;
            }

            if (outSlot >= 0 && (fds[outSlot].revents & (POLLIN | POLLHUP)))
            {
                const ssize_t n = read(outFd, buffer, sizeof(buffer));
                if (n > 0) result.stdOut.append(buffer, static_cast<std::size_t>(n));
                else if (n == 0) outDone = true;
                else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) outDone = true;
            }
            if (errSlot >= 0 && (fds[errSlot].revents & (POLLIN | POLLHUP)))
            {
                const ssize_t n = read(errFd, buffer, sizeof(buffer));
                if (n > 0) result.stdErr.append(buffer, static_cast<std::size_t>(n));
                else if (n == 0) errDone = true;
                else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) errDone = true;
            }
            if (inSlot >= 0 && (fds[inSlot].revents & (POLLOUT | POLLERR | POLLHUP)))
            {
                if (fds[inSlot].revents & (POLLERR | POLLHUP))
                {
                    writeDone = true;
                    close(inFd);
                }
                else
                {
                    const ssize_t n = write(inFd, stdinInput.data() + written, stdinInput.size() - written);
                    if (n > 0)
                    {
                        written += static_cast<std::size_t>(n);
                        if (written >= stdinInput.size()) { writeDone = true; close(inFd); }
                    }
                    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    {
                        writeDone = true; // give up silently; child will just see a short/closed stdin
                        close(inFd);
                    }
                }
            }
        }

        if (!writeDone && inFd >= 0) close(inFd);
    }

#endif // !_WIN32

#if defined(_WIN32)
    static ProcessResult RunWindows(
        const std::vector<std::string>& args,
        const std::filesystem::path& cwd,
        std::string_view stdinInput)
    {
        ProcessResult result;

        std::string commandLine;
        for (const auto& a : args)
        {
            if (!commandLine.empty()) commandLine += ' ';
            commandLine += ArgvQuote(a);
        }

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE inRead = nullptr, inWrite = nullptr;
        HANDLE outRead = nullptr, outWrite = nullptr;
        HANDLE errRead = nullptr, errWrite = nullptr;

        const bool pipesOk =
            CreatePipe(&inRead, &inWrite, &sa, 0) && SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0) &&
            CreatePipe(&outRead, &outWrite, &sa, 0) && SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0) &&
            CreatePipe(&errRead, &errWrite, &sa, 0) && SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

        if (!pipesOk)
        {
            result.launchFailed = true;
            result.launchError = "CreatePipe failed";
            for (HANDLE h : { inRead, inWrite, outRead, outWrite, errRead, errWrite })
            {
                if (h) CloseHandle(h);
            }
            return result;
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = errWrite;

        PROCESS_INFORMATION pi{};
        std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back('\0');

        const BOOL created = CreateProcessA(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            cwd.empty() ? nullptr : cwd.string().c_str(),
            &si,
            &pi);

        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(errWrite);

        if (!created)
        {
            result.launchFailed = true;
            result.launchError = "CreateProcessA failed";
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(errRead);
            return result;
        }

        // BUG FIX (found via a real hang against ~170 paths batched into
        // `git check-attr --stdin`): writing all of stdin to completion
        // BEFORE reading any output is a classic bidirectional-pipe
        // deadlock once the payload is large enough -- the child fills its
        // stdout pipe buffer and blocks on write() before it has drained
        // enough of stdin for us to finish writing, while we are still
        // blocked in WriteFile(stdin) waiting for the child to read more.
        // Stdin-writing and both reads now run concurrently on their own
        // threads instead of sequentially.
        std::thread stdinWriter([&]
        {
            if (!stdinInput.empty())
            {
                std::size_t offset = 0;
                DWORD written = 0;
                while (offset < stdinInput.size())
                {
                    if (!WriteFile(inWrite, stdinInput.data() + offset,
                                    static_cast<DWORD>(stdinInput.size() - offset), &written, nullptr))
                    {
                        break;
                    }
                    offset += written;
                }
            }
            CloseHandle(inWrite); // signal EOF to the child's stdin
        });

        std::thread stderrReader([&]
        {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (ReadFile(errRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                result.stdErr.append(buffer, bytesRead);
            }
        });

        {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (ReadFile(outRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                result.stdOut.append(buffer, bytesRead);
            }
        }

        stdinWriter.join();
        stderrReader.join();

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result.exitCode = static_cast<int>(exitCode);

        CloseHandle(outRead);
        CloseHandle(errRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return result;
    }

    // Standard Windows command-line argument quoting: correctly escapes
    // embedded quotes and runs of backslashes that precede a quote or
    // the end of the argument. (The previous version only escaped quote
    // characters, which corrupts paths ending in backslashes.)
    static std::string ArgvQuote(const std::string& arg)
    {
        if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
        {
            return arg;
        }

        std::string result = "\"";
        for (auto it = arg.begin();; ++it)
        {
            std::size_t backslashCount = 0;
            while (it != arg.end() && *it == '\\')
            {
                ++it;
                ++backslashCount;
            }

            if (it == arg.end())
            {
                result.append(backslashCount * 2, '\\');
                break;
            }
            if (*it == '"')
            {
                result.append(backslashCount * 2 + 1, '\\');
                result.push_back('"');
            }
            else
            {
                result.append(backslashCount, '\\');
                result.push_back(*it);
            }
        }
        result.push_back('"');
        return result;
    }
#endif // _WIN32
};

} // namespace sc::process
