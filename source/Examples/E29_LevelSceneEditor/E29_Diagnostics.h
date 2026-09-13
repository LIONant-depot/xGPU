#ifndef E29_DIAGNOSTICS_H
#define E29_DIAGNOSTICS_H
#pragma once

#include <cstdarg>
#include <cstdlib>
#include <exception>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <typeinfo>

#if defined(_MSC_VER)
#include <share.h>
#endif

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <dbghelp.h>
#include <windows.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

namespace e29::diagnostics
{
    inline std::mutex g_TraceMutex;
    inline FILE*      g_pTraceFile = nullptr;
    inline std::string g_TracePath;
    inline std::terminate_handler g_PreviousTerminateHandler = nullptr;

    inline FILE* OpenTraceFile(const char* pPath) noexcept
    {
#if defined(_MSC_VER)
        return _fsopen(pPath, "w", _SH_DENYNO);
#else
        return std::fopen(pPath, "w");
#endif
    }

    inline void LogUnlocked(const char* pFormat, va_list Args) noexcept
    {
        if (g_pTraceFile == nullptr) return;
        std::vfprintf(g_pTraceFile, pFormat, Args);
        std::fputc('\n', g_pTraceFile);
        std::fflush(g_pTraceFile);
    }

    inline void Log(const char* pFormat, ...) noexcept
    {
        std::lock_guard Lock(g_TraceMutex);
        if (g_pTraceFile == nullptr) return;

        va_list Args;
        va_start(Args, pFormat);
        LogUnlocked(pFormat, Args);
        va_end(Args);
    }

    inline const char* Path() noexcept
    {
        return g_TracePath.c_str();
    }

    inline void Start() noexcept
    {
        std::lock_guard Lock(g_TraceMutex);
        if (g_pTraceFile != nullptr) return;

        constexpr const char* pFileName = "E29_LevelSceneEditor.trace.log";
        std::error_code Ec;
        const auto CurrentPath = std::filesystem::current_path(Ec);
        g_TracePath = Ec ? pFileName : (CurrentPath / pFileName).string();

        g_pTraceFile = OpenTraceFile(g_TracePath.c_str());
        if (g_pTraceFile == nullptr)
        {
            g_TracePath = pFileName;
            g_pTraceFile = OpenTraceFile(pFileName);
        }

        if (g_pTraceFile != nullptr)
        {
            std::fprintf(g_pTraceFile, "E29 trace start\n");
            std::fprintf(g_pTraceFile, "trace_path=%s\n", g_TracePath.c_str());
            std::fflush(g_pTraceFile);
        }
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    inline void LogCrtStack() noexcept
    {
        static std::once_flag SymbolsOnce;
        std::call_once(SymbolsOnce, []
        {
            ::SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            (void)::SymInitialize(::GetCurrentProcess(), nullptr, TRUE);
        });

        void* Frames[32]{};
        const USHORT Count = ::CaptureStackBackTrace(1, static_cast<DWORD>(std::size(Frames)), Frames, nullptr);
        HANDLE Process = ::GetCurrentProcess();
        alignas(SYMBOL_INFO) char SymbolStorage[sizeof(SYMBOL_INFO) + 512]{};
        auto* Symbol = reinterpret_cast<SYMBOL_INFO*>(SymbolStorage);
        Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        Symbol->MaxNameLen = 511;

        for (USHORT Index = 0; Index < Count; ++Index)
        {
            const DWORD64 Address = reinterpret_cast<DWORD64>(Frames[Index]);
            DWORD64 Displacement = 0;
            IMAGEHLP_LINE64 Line{};
            Line.SizeOfStruct = sizeof(Line);
            if (::SymFromAddr(Process, Address, &Displacement, Symbol))
            {
                DWORD LineDisplacement = 0;
                if (::SymGetLineFromAddr64(Process, Address, &LineDisplacement, &Line))
                    Log("CRT stack[%u] %s+0x%llx (%s:%lu)", Index, Symbol->Name
                        , static_cast<unsigned long long>(Displacement)
                        , Line.FileName, static_cast<unsigned long>(Line.LineNumber));
                else
                    Log("CRT stack[%u] %s+0x%llx", Index, Symbol->Name
                        , static_cast<unsigned long long>(Displacement));
            }
            else
            {
                Log("CRT stack[%u] address=0x%llx", Index, static_cast<unsigned long long>(Address));
            }
        }
    }

    inline int __cdecl CrtReportHook(int ReportType, wchar_t* pMessage, int* pReturnValue) noexcept
    {
        static thread_local bool InHook = false;
        if (InHook) return 0;
        InHook = true;

        char Message[4096]{};
        if (pMessage != nullptr)
        {
            const int Count = ::WideCharToMultiByte
            ( CP_UTF8, 0, pMessage, -1, Message, static_cast<int>(sizeof(Message)), nullptr, nullptr );
            if (Count == 0) std::snprintf(Message, sizeof(Message), "<CRT message conversion failed>");
        }
        else
        {
            std::snprintf(Message, sizeof(Message), "<null CRT message>");
        }

        Log("CRT report type=%d message=%s", ReportType, Message);
        LogCrtStack();
        if (pReturnValue != nullptr) *pReturnValue = 0;
        InHook = false;
        return 0;
    }

    inline void InstallCrtReportHook() noexcept
    {
        const int Result = _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &CrtReportHook);
        Log("CRT assertion hook installed result=%d", Result);
    }

    inline void RemoveCrtReportHook() noexcept
    {
        const int Result = _CrtSetReportHookW2(_CRT_RPTHOOK_REMOVE, &CrtReportHook);
        Log("CRT assertion hook removed result=%d", Result);
    }
#else
    inline void InstallCrtReportHook() noexcept {}
    inline void RemoveCrtReportHook() noexcept {}
#endif

    inline void TerminateHandler() noexcept
    {
        try
        {
            if (const auto Exception = std::current_exception())
            {
                std::rethrow_exception(Exception);
            }
        }
        catch (const std::filesystem::filesystem_error& Error)
        {
            Log("terminate: filesystem_error what=%s code=%d category=%s path1=%s path2=%s"
                , Error.what(), Error.code().value(), Error.code().category().name()
                , Error.path1().string().c_str(), Error.path2().string().c_str());
        }
        catch (const std::exception& Error)
        {
            Log("terminate: exception what=%s type=%s", Error.what(), typeid(Error).name());
        }
        catch (...)
        {
            Log("terminate: unknown exception");
        }

        std::abort();
    }

    inline void InstallTerminateHandler() noexcept
    {
        g_PreviousTerminateHandler = std::set_terminate(&TerminateHandler);
        Log("terminate handler installed");
    }

    inline void RemoveTerminateHandler() noexcept
    {
        std::set_terminate(g_PreviousTerminateHandler);
        Log("terminate handler removed");
        g_PreviousTerminateHandler = nullptr;
    }

    inline void Stop() noexcept
    {
        std::lock_guard Lock(g_TraceMutex);
        if (g_pTraceFile == nullptr) return;
        std::fprintf(g_pTraceFile, "E29 trace stop\n");
        std::fflush(g_pTraceFile);
        std::fclose(g_pTraceFile);
        g_pTraceFile = nullptr;
    }
}

#endif // E29_DIAGNOSTICS_H
