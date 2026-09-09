#ifndef E29_GAME_PLUGIN_BUILD_H
#define E29_GAME_PLUGIN_BUILD_H
#pragma once

// Extracted from E29_GamePlugin.h (mechanical move, phase 3 of the kit split - see the umbrella
// file's own top comment). game_plugin_state (the one loaded-generation record threaded through
// build/load/play-session alike) and the staleness-check + cmake-invoking rebuild itself
// (BuildGamePluginIfStale) - see CMakeLists.txt's own E29_Game target comments for the
// /PDBALTPATH + PDB_OUTPUT_DIRECTORY + /nodeReuse:false story this function's own cmake invocation
// relies on. Meant to be included via the umbrella (E29_GamePlugin.h) only, after
// E29_GamePluginLog.h (LogGamePlugin).

namespace e29
{
    // Outcome of a staleness check (see BuildGamePluginIfStale). Deliberately three-valued, not a
    // bool: once recompile-checks fire automatically (on window focus regained, or on pressing Play -
    // see ConsumeWindowFocusGained's own comment), most checks find nothing to do at all, and the
    // caller (PollGameReload) must be able to tell "nothing changed, don't touch the world" apart
    // from "a new generation was actually built, do the full destroy/recreate reload" - collapsing
    // both into one "succeeded" bool (as the original, button-only version of this did, when every
    // call was a deliberate user click that always meant "yes, reload") would destroy and rebuild the
    // entire runtime world on every single alt-tab back into the editor, whether or not anything
    // actually changed.
    enum class build_result : std::uint8_t { UpToDate, Rebuilt, Failed };

    // One loaded Game.dll generation. Slot is fixed at 1 (0 is xecs::plugin::host_v, reserved for
    // the host's own registrations) - E29 only ever hosts one game plugin at a time, so there is
    // only ever one non-host slot to assign.
    //
    // m_CompiledDllPath (fixed - the E29_Game CMake target's own build output) and m_LoadedDllPath
    // (a generation-suffixed COPY of it, made fresh every load/reload, that's the one actually
    // LoadLibrary'd) are deliberately different files/paths - direct user requirement: "the dll
    // that we are loading should not be the same one that the compiler is compiling... the job of
    // the editor is to copy the new version of the dll with any symbols it may need for debugging."
    // This means a rebuild (whether the user's own external build or E29's own auto-build below)
    // never has to fight a file this process still has mapped - the compiler always writes to
    // m_CompiledDllPath, which is NEVER the currently-loaded file.
    struct game_plugin_state
    {
        HMODULE                m_hModule          = nullptr;
        xecs::plugin::token     m_Token            = {};
        std::wstring            m_CompiledDllPath;
        std::wstring            m_LoadedDllPath;

        // Human-readable outcome of the most recent build/load attempt - printf's own log lines are
        // invisible in a GUI app with no attached console, so RenderSystemRegistryPanel (or wherever
        // the Reload Game button lives) renders this directly instead of leaving the user with no
        // feedback at all about whether a reload actually did anything.
        std::string             m_LastStatus       = "Game.dll: not loaded yet";

        // Async build state - direct user requirement: compiling must never freeze the editor, the
        // Reload Game button must disable itself while a build is in flight, and a FAILED build
        // must leave the currently loaded generation completely untouched (never unloaded in the
        // first place) rather than tearing the world down first and hoping the build succeeds.
        // StartGameReload() kicks off BuildGamePluginIfStale() on a background thread
        // (std::async) and returns immediately; PollGameReload() (called once per frame, at the
        // same safe top-of-frame point the old synchronous reload used to run from) checks for
        // completion without blocking, and only THEN - and only on success - performs the actual,
        // destructive reload steps (destroy world, unload old generation, load new one). This is
        // exactly why the compiled/loaded DLL split exists at all: the build always targets
        // m_CompiledDllPath, which is never the loaded file, so it can safely run in the background
        // while the OLD generation keeps running completely undisturbed, whether the build
        // eventually succeeds or fails.
        std::future<build_result> m_BuildFuture;
        bool                    m_bBuilding        = false;

        bool isLoaded(void) const noexcept { return m_hModule != nullptr; }
    };

    // Set once, near g_pGameMgr/g_pState (E29_PrefabAuthoring.h), so a CLI/Console-driven command
    // (E29_Commands_PlaySession.h) can trigger the same Play/Pause/Stop transitions the menu-bar
    // buttons do without needing synthetic mouse input - same "one instance per process" assumption
    // those two globals already make.
    inline game_plugin_state* g_pGamePlugin = nullptr;

    //---------------------------------------------------------------------------
    // "The editor should try to recompile automatically; if it's already compiled (newer than the
    // one we're using) just load, no need to try to recompile" (direct user direction). CompiledDllPath
    // is the E29_Game CMake target's own, fixed build output (NEVER the file actually loaded - see
    // game_plugin_state's own comment on the compiled/loaded split), from which the build directory
    // and config are derived structurally (`<ProjectRoot>\Build\<BuildDirName>\<Config>\` is this
    // project's own fixed layout - see xgpu_build_quirks). Rebuilds via the E29_Game CMake target
    // (Phase 7/8's own CMakeLists.txt addition) rather than a raw cl.exe shellout the way
    // E27_NodeOS's compiler does - that pattern is scoped for many small single-.cpp node plugins
    // recompiled individually; Game.dll is one larger, normally-built target that already has a
    // real CMake target, so reusing cmake's own (fine-grained, header-dependency-aware) staleness
    // tracking for the ACTUAL rebuild decision is both simpler and more correct than re-deriving
    // it from a single source file's timestamp by hand.
    //
    // The timestamp check here exists purely to decide whether to invoke cmake AT ALL, not to
    // second-guess what it decides once invoked - per the user's explicit ask, an up-to-date DLL
    // should never even see a rebuild attempted (skips the cmake generate/build overhead, which is
    // otherwise incurred on every single load/reload regardless of whether anything changed).
    //
    // Blocking (WaitForSingleObject(..., INFINITE) below), but that's no longer a UI-freeze concern -
    // the ONLY caller is StartGameReload's own std::async background thread (see game_plugin_state's
    // own comment on why), never the main/render thread directly. Only touches Plugin.m_LastStatus/
    // LogGamePlugin (thread-safe - see GetGamePluginLogMutex) and local filesystem/process state -
    // never GameMgr, State, or anything ImGui-related, so it's safe to run concurrently with the
    // editor's own main loop.
    inline build_result BuildGamePluginIfStale( game_plugin_state& Plugin ) noexcept
    {
        std::error_code Ec;
        const std::filesystem::path Dll        = Plugin.m_CompiledDllPath;
        const std::filesystem::path ExeDir     = Dll.parent_path();               // .../Build/<BuildDirName>/<Config>
        const std::filesystem::path BuildDir   = ExeDir.parent_path();            // .../Build/<BuildDirName>
        const std::filesystem::path ProjectRoot= BuildDir.parent_path().parent_path();
        const std::wstring          Config     = ExeDir.filename().wstring();     // "Debug" or "Release"
        const std::filesystem::path SourcePath = ProjectRoot / L"source" / L"Examples" / L"E29_LevelSceneEditor" / L"GameProject" / L"E29_Game.cpp";

        const bool bDllMissing = !std::filesystem::exists(Dll, Ec);
        bool bStale = bDllMissing;
        if (!bStale)
        {
            const auto SourceTime = std::filesystem::last_write_time(SourcePath, Ec);
            if (Ec)
            {
                Plugin.m_LastStatus = std::format("Game.dll: can't stat source {} - skipping rebuild attempt", SourcePath.string());
                LogGamePlugin(Plugin.m_LastStatus);
                return build_result::UpToDate;
            }
            const auto DllTime = std::filesystem::last_write_time(Dll, Ec);
            bStale = Ec || SourceTime > DllTime;
        }

        if (!bStale)
        {
            Plugin.m_LastStatus = "Game.dll: up to date, no rebuild needed";
            LogGamePlugin(Plugin.m_LastStatus);
            return build_result::UpToDate; // load it directly, no rebuild attempted
        }

        Plugin.m_LastStatus = std::format("Game.dll: {} - rebuilding via cmake...", bDllMissing ? "DLL missing" : "source newer than DLL");
        LogGamePlugin(Plugin.m_LastStatus);

        // /nodeReuse:false (passed through to MSBuild via cmake's own "-- <native tool args>"
        // convention) - direct fix for a live LNK1201 ("error writing to program database ...pdb")
        // reproduced by the user. NOT a Visual Studio conflict - only this function ever builds
        // E29_Game, VS never does. The race is this function against ITSELF, across separate
        // reload triggers (focus-regain/Play/Level-open) over one editing session: `cmake --build`
        // spawns MSBuild, which by default (/nodeReuse:true) leaves a worker process alive AFTER
        // this call returns specifically so a LATER build can reuse it for speed (confirmed live via
        // a lingering MSBuild.exe "...\<random>.proj" node process still running well after its own
        // triggering build had finished) - that worker holds a PDB-write lock via mspdbsrv.exe. If
        // the editor's whole process tree ever gets killed uncleanly mid-build (e.g. a forced
        // taskkill, or a crash) the worker can be left in a bad state and corrupt/contend with the
        // NEXT auto-build's own attempt to write the same E29_Game.pdb, later in the same or a
        // future session. E29_Game.cpp is one small file - there's no meaningful incremental-build
        // speed to lose by asking THIS invocation not to spawn/reuse a persistent worker at all.
        const std::wstring CmdLine = std::format(L"cmake --build \"{}\" --target E29_Game --config {} -- /nodeReuse:false", BuildDir.wstring(), Config);

        // Belt-and-suspenders on top of the /nodeReuse:false switch above - confirmed live that the
        // command-line switch alone does NOT reliably stop every nested MSBuild worker node it spawns
        // for a multi-project (solution-level) build from defaulting back to node reuse (a worker
        // process was still observed running with an explicit /nodeReuse:true on its own command
        // line despite the outer invocation's /nodeReuse:false). MSBUILDDISABLENODEREUSE is the
        // environment-variable form of the same setting and is Microsoft's own documented, more
        // reliable way to force it onto every node a build spawns, nested workers included - exactly
        // the mechanism CI systems use for this. Set on this (the caller's) process rather than built
        // into a custom lpEnvironment block for CreateProcessW below - simpler, and lpEnvironment
        // nullptr already means "inherit the caller's current environment," so this is picked up
        // automatically. Idempotent (safe to set every call).
        SetEnvironmentVariableW(L"MSBUILDDISABLENODEREUSE", L"1");

        // Redirected to a pipe and logged line-by-line below rather than left to inherit this
        // process's own (nonexistent - GUI subsystem, no console) stdout - the compiler's own
        // error output is exactly what a student needs to see when their game code fails to build,
        // and until this, it was going nowhere anyone could read it.
        SECURITY_ATTRIBUTES PipeSa{ .nLength = sizeof(PipeSa), .bInheritHandle = TRUE };
        HANDLE ReadPipe = nullptr, WritePipe = nullptr;
        CreatePipe(&ReadPipe, &WritePipe, &PipeSa, 0);
        SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0); // this process's own read end must NOT be inherited by the child

        STARTUPINFOW Si
        { .cb         = sizeof(Si)
        , .dwFlags    = STARTF_USESTDHANDLES
        , .hStdOutput = WritePipe
        , .hStdError  = WritePipe
        };
        PROCESS_INFORMATION Pi{};
        // CreateProcessW may write into the command-line buffer - a std::wstring's own data() must
        // stay mutable/writable for that, hence the copy into a plain array rather than passing
        // CmdLine.data() (or a string literal) directly.
        std::vector<wchar_t> CmdLineBuf(CmdLine.begin(), CmdLine.end());
        CmdLineBuf.push_back(L'\0');

        if (!CreateProcessW(nullptr, CmdLineBuf.data(), nullptr, nullptr, TRUE, 0, nullptr, ProjectRoot.c_str(), &Si, &Pi))
        {
            CloseHandle(ReadPipe);
            CloseHandle(WritePipe);
            Plugin.m_LastStatus = std::format("Game.dll: failed to launch cmake (err={}) - trying to load whatever DLL exists", GetLastError());
            LogGamePlugin(Plugin.m_LastStatus);
            return build_result::Failed;
        }

        // This process's own handle to the write end must close BEFORE reading, or ReadFile below
        // blocks forever waiting for a write-end closure that never comes (the child's copy alone
        // isn't enough - ReadFile only sees EOF once EVERY write handle, including this one, is
        // closed).
        CloseHandle(WritePipe);

        // Blocking reads, one line at a time, until the pipe closes (the child exiting closes its
        // own inherited write handle, which is what makes ReadFile finally return 0) - synchronous,
        // matching this whole function's own "runs only on an explicit user action" scope note
        // above.
        {
            std::string LineBuffer;
            char        Chunk[512];
            DWORD       BytesRead = 0;
            while (ReadFile(ReadPipe, Chunk, sizeof(Chunk), &BytesRead, nullptr) && BytesRead > 0)
            {
                for (DWORD i = 0; i < BytesRead; ++i)
                {
                    if (Chunk[i] == '\n')
                    {
                        if (!LineBuffer.empty() && LineBuffer.back() == '\r') LineBuffer.pop_back();
                        LogGamePlugin(LineBuffer);
                        LineBuffer.clear();
                    }
                    else
                    {
                        LineBuffer.push_back(Chunk[i]);
                    }
                }
            }
            if (!LineBuffer.empty()) LogGamePlugin(LineBuffer);
        }
        CloseHandle(ReadPipe);

        WaitForSingleObject(Pi.hProcess, INFINITE);
        DWORD ExitCode = 1;
        GetExitCodeProcess(Pi.hProcess, &ExitCode);
        CloseHandle(Pi.hThread);
        CloseHandle(Pi.hProcess);

        if (ExitCode != 0)
        {
            Plugin.m_LastStatus = std::format("Game.dll: BUILD FAILED (exit={}) - see stdout for the compiler's own error log", ExitCode);
            LogGamePlugin(Plugin.m_LastStatus);
            return build_result::Failed; // the currently loaded generation is left completely untouched
        }

        Plugin.m_LastStatus = "Game.dll: rebuild succeeded";
        LogGamePlugin(Plugin.m_LastStatus);
        return build_result::Rebuilt;
    }

} // namespace e29

#endif // E29_GAME_PLUGIN_BUILD_H
