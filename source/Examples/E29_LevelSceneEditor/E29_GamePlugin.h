#ifndef E29_GAME_PLUGIN_H
#define E29_GAME_PLUGIN_H
#pragma once

// Phase 8 of the xECSV2 type-registration architecture plan: E29's own host-side half of the
// hot-reloadable "Game.dll" - loading/unloading the plugin and the full destroy-and-recreate-world
// reload sequence (Phase 8A - "full-world reconstruction", the smallest unload-safety surface;
// see the plan's own Phase 8 section for the entity-preserving 8B milestone this deliberately does
// NOT attempt). Mirrors E27_NodeOS's own ReloadPlugin as closely as this project's much simpler
// registration model allows - see xecs_plugin_api.h's own comment for why no virtual node/factory
// interfaces are needed here at all.
//
// Must be included after both xecs.h and xecs_plugin_api.h.
#include <Windows.h>
#include <filesystem>
#include <future>
#include <mutex>

namespace e29
{
    // Mirrors E27_NodeOS's own GetRuntimeLog()/DrawRuntimeLogPanel() pattern exactly
    // (Editor/NodeOS_Types.h / Editor/NodeOS_UI_Panels.h) - the one on-screen surface for Game.dll
    // build/load activity, since printf's own output has no visible console in this GUI-only app
    // (confirmed live: the user had no way to tell whether an auto-rebuild had even been attempted).
    //
    // Mutex-guarded (unlike NodeOS's own version) because BuildGamePluginIfStale now runs on a
    // background thread (see StartGameReload) so the editor's own render loop never blocks on a
    // slow compile - LogGamePlugin() is called from that thread, GetGamePluginLog()'s CONTENTS are
    // iterated from the main thread every frame in RenderGamePluginLogPanel(), and std::vector has
    // no thread-safety of its own for a concurrent push_back/iterate pair.
    inline std::mutex& GetGamePluginLogMutex() noexcept
    {
        static std::mutex s_Mutex;
        return s_Mutex;
    }

    inline std::vector<std::string>& GetGamePluginLog() noexcept
    {
        static std::vector<std::string> s_Log;
        return s_Log;
    }

    inline void LogGamePlugin( std::string_view Msg ) noexcept
    {
        std::printf("%.*s\n", static_cast<int>(Msg.size()), Msg.data());
        std::fflush(stdout);
        std::lock_guard Lock(GetGamePluginLogMutex());
        GetGamePluginLog().emplace_back(Msg);
    }

    inline void RenderGamePluginLogPanel() noexcept
    {
        // Matches E27_NodeOS's own DrawRuntimeLogPanel exactly (no autoscroll - an earlier version
        // of this function added a GetScrollY()/GetScrollMaxY()/SetScrollHereY() check here; pulled
        // back out after a live crash while docking this window. Root cause turned out to be
        // unrelated to this function entirely - E10_AssetBrowser.h's own MainWindow() was calling
        // ImGui::End() INSIDE its `if (ImGui::Begin(...))` block, skipping it whenever Begin()
        // returned false (a docked-but-not-the-active-tab window) - permanently unbalancing
        // ImGui's window stack from that frame on. Fixed there; this function was never the
        // problem, so it's kept in its simpler, proven-stable form regardless.
        ImGui::SetNextWindowPos(ImVec2(506, 530), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Game.dll Log"))
        {
            std::lock_guard Lock(GetGamePluginLogMutex());
            if (ImGui::SmallButton("Clear")) GetGamePluginLog().clear();
            ImGui::Separator();
            // A child window of its own, rather than relying on the outer window's own scrollbars -
            // ImGuiWindowFlags_HorizontalScrollbar only kicks in when content actually overflows the
            // region, and build/compile output routinely has lines far wider than this panel's default
            // size (long paths, full compiler command lines) that would otherwise just get clipped.
            if (ImGui::BeginChild("GameLogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
            {
                for (auto& Line : GetGamePluginLog())
                    ImGui::TextUnformatted(Line.c_str());
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

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

    //---------------------------------------------------------------------------
    // Copies Plugin.m_CompiledDllPath (+ its matching .pdb, if present - direct user requirement:
    // "the job of the editor is to copy the new version of the dll with any symbols it may need
    // for debugging") to a fresh, generation-suffixed filename in the same directory - the file
    // that actually gets LoadLibrary'd. Never overwrites a previous generation's copy (each
    // Generation value names a distinct file) - by the time this runs, UnloadGamePlugin has already
    // deleted the previous one anyway (safe then: nothing still has it mapped). Returns the new
    // copy's path, or empty on failure (nothing compiled yet, or the copy itself failed).
    //---------------------------------------------------------------------------
    inline std::wstring CopyGamePluginForLoad( const std::wstring& CompiledDllPath, std::uint32_t Generation ) noexcept
    {
        std::error_code Ec;
        const std::filesystem::path Compiled = CompiledDllPath;
        if (!std::filesystem::exists(Compiled, Ec))
        {
            LogGamePlugin(std::format("Game.dll: nothing compiled yet at {}", Compiled.string()));
            return {};
        }

        const auto Dir     = Compiled.parent_path();
        const auto Stem    = Compiled.stem().wstring();
        const auto NewDll  = Dir / std::format(L"{}_loaded_{}.dll", Stem, Generation);
        // The COMPILED pdb lives in its own separate "GamePdb/<Config>" directory now (sibling to
        // Dir) AND is linked with /PDBALTPATH set to just its own bare filename ("E29_Game.pdb", no
        // directory) instead of the real compile-time absolute path - see CMakeLists.txt's own
        // comment on the E29_Game target for the full story of why (relocating the directory ALONE
        // was tried first and empirically disproven - the debugger resolves symbols via the
        // EMBEDDED path regardless of where the file physically sits, confirmed live via Restart
        // Manager). Because the embedded path is just a bare filename, NewPdb below must be that
        // SAME bare name ("E29_Game.pdb", not generation-suffixed like NewDll) for the debugger's
        // own resolution (starting with the loaded module's own directory) to actually find it.
        const auto SrcPdb  = Dir.parent_path() / L"GamePdb" / Dir.filename() / (Stem + L".pdb");
        const auto NewPdb  = Dir / (Stem + L".pdb");

        std::filesystem::copy_file(Compiled, NewDll, std::filesystem::copy_options::overwrite_existing, Ec);
        if (Ec)
        {
            LogGamePlugin(std::format("Game.dll: failed to copy {} -> {}", Compiled.string(), NewDll.string()));
            return {};
        }

        // Best-effort - a missing/failed .pdb copy only degrades debugging, it's not a load failure.
        if (std::filesystem::exists(SrcPdb, Ec))
        {
            std::error_code PdbEc;
            std::filesystem::copy_file(SrcPdb, NewPdb, std::filesystem::copy_options::overwrite_existing, PdbEc);
        }

        return NewDll.wstring();
    }

    //---------------------------------------------------------------------------
    // Copies the (already known-good - see StartGameReload/PollGameReload, which call
    // BuildGamePluginIfStale BEFORE this) compiled DLL into a new generation-suffixed shadow file
    // (CopyGamePluginForLoad), loads THAT, and calls its XecsPlugin_RegisterComponents - the FIRST
    // of the two-call sequence xecs_plugin_api.h's own comment requires (every RegisterComponents
    // call, host's and the plugin's, must happen before ANY RegisterSystems call). Returns false
    // (Plugin left untouched, exactly as before the call) on any failure - missing file, missing
    // export, nothing else to do differently: the caller keeps running without a game loaded, same
    // as if this was never called.
    //---------------------------------------------------------------------------
    inline bool LoadGamePluginComponents( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin, std::uint32_t Generation ) noexcept
    {
        assert( Plugin.isLoaded() == false );

        const std::wstring LoadedPath = CopyGamePluginForLoad(Plugin.m_CompiledDllPath, Generation);
        if (LoadedPath.empty())
        {
            Plugin.m_LastStatus += " | nothing to load";
            LogGamePlugin("Game.dll: nothing to load");
            return false;
        }

        // Plain printf, deliberately NOT e29::Debugger() - Debugger() also arms the modal error-
        // popup (RenderErrorPopup, checked once per frame from the main loop's own top-level
        // scope). This function's very first caller (E29_LevelScene_Editor.cpp's own startup code)
        // calls it BEFORE the main loop has rendered even one frame - confirmed empirically (a
        // deterministic, 100%-reproducible "Missing EndChild()" ImGui assertion on frame 1,
        // isolated by bisecting against the pre-Phase-8 file and adding per-call-site printf
        // checkpoints) that arming the popup flag that early breaks ImGui's window-stack
        // bookkeeping - every OTHER Debugger() call site in this codebase fires from inside an
        // already-running frame, which this startup call path is not. A missing/failed-to-load
        // Game.dll is an expected, benign condition anyway (nothing has been built yet on a fresh
        // checkout) - a plain log line is the right amount of ceremony for it, not a modal.
        HMODULE hModule = LoadLibraryW(LoadedPath.c_str());
        if (hModule == nullptr)
        {
            Plugin.m_LastStatus = std::format("Game.dll: LoadLibrary failed for {}", std::filesystem::path(LoadedPath).filename().string());
            LogGamePlugin(Plugin.m_LastStatus);
            return false;
        }

        auto* pRegisterComponents = reinterpret_cast<xecs_plugin_pfn_register_components*>(GetProcAddress(hModule, XECS_PLUGIN_REGISTER_COMPONENTS_NAME));
        if (pRegisterComponents == nullptr)
        {
            Plugin.m_LastStatus = std::format("Game.dll: missing export {}", XECS_PLUGIN_REGISTER_COMPONENTS_NAME);
            LogGamePlugin(Plugin.m_LastStatus);
            FreeLibrary(hModule);
            return false;
        }

        Plugin.m_hModule       = hModule;
        Plugin.m_LoadedDllPath = LoadedPath;
        Plugin.m_Token         = { .m_Slot = 1, .m_Generation = Generation };
        pRegisterComponents(GameMgr, Plugin.m_Token);

        Plugin.m_LastStatus = std::format("Game.dll: loaded generation {} ({})", Generation, std::filesystem::path(LoadedPath).filename().string());
        return true;
    }

    // The SECOND call of the two-call sequence - only meaningful once every RegisterComponents
    // call (host's own, done by the caller, and the plugin's, done by
    // LoadGamePluginComponents above) has already happened. A no-op if no plugin is loaded.
    inline void RegisterGamePluginSystems( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pRegisterSystems = reinterpret_cast<xecs_plugin_pfn_register_systems*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_REGISTER_SYSTEMS_NAME)))
            pRegisterSystems(GameMgr);
    }

    //---------------------------------------------------------------------------
    // Detach/quiesce + unload - called with the OLD world already destroyed (see PollGameReload
    // below), so xecs::component::mgr::UnregisterPlugin's own full-reset is exactly the correct,
    // sufficient operation (see its own comment for why). A no-op if nothing is loaded.
    //---------------------------------------------------------------------------
    inline void UnloadGamePlugin( game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pUnregister = reinterpret_cast<xecs_plugin_pfn_unregister*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_UNREGISTER_NAME)))
            pUnregister(Plugin.m_Token);

        xecs::component::mgr::UnregisterPlugin(Plugin.m_Token);

        FreeLibrary(Plugin.m_hModule);
        Plugin.m_hModule = nullptr;
        Plugin.m_Token   = {};

        // The shadow copy (see game_plugin_state's own comment for why it exists) is safe to
        // delete now that nothing has it mapped - best-effort; a leftover file here would be
        // cosmetic, never a correctness problem. The .pdb is NOT
        // Plugin.m_LoadedDllPath-with-a-different-extension anymore - CopyGamePluginForLoad's own
        // comment explains why it's always the fixed bare name "E29_Game.pdb" (matching this
        // target's compiled PDB_NAME), never generation-suffixed like the .dll itself.
        if (!Plugin.m_LoadedDllPath.empty())
        {
            std::error_code Ec;
            const std::filesystem::path LoadedDll = Plugin.m_LoadedDllPath;
            std::filesystem::remove(LoadedDll, Ec);
            std::filesystem::remove(LoadedDll.parent_path() / L"E29_Game.pdb", Ec);
            Plugin.m_LoadedDllPath.clear();
        }
    }

    //---------------------------------------------------------------------------
    // V1 vs Vn, direct user model: "when you hit play you snapshot the current state and save it
    // (V1); when you hit pause you may recompile etc - we call these ones Vn (n>1); when you hit stop
    // you only care about reloading V1, all other ones are 100% irrelevant, because the point is get
    // back to normal editing" - and, critically, "like Unity the tree represents the current truth
    // of the scenes": entities can die or get created (dumped into the default folder) while playing,
    // so Stop must put the Level tree back exactly as it was before Play, not just restore raw
    // component values. V1 is therefore the REAL Scene/Level/Prefab disk save (SaveEverything/
    // OpenLevel - already Scene-aware, already the proven mechanism that reconstructs the tree
    // correctly) taken once at Play-entry - not a raw binary dump. Vn does NOT need any of that
    // (direct user confirmation: "V1 is the only one that needs to serialize [the tree]... Vn does
    // not need that") - it stays the fast, ephemeral, scene-unaware xecs::game_mgr::instance::
    // SerializeGameState bridge below, purely to keep gameplay itself continuous across a mid-play
    // Game.dll reload, never touching the real saved assets and never read back by Stop.
    inline std::wstring GetReloadBridgeSnapshotPath() noexcept
    {
        static const std::wstring s_Path = (std::filesystem::temp_directory_path() / L"xGPU_E29_ReloadBridge.bin").wstring();
        return s_Path;
    }

    inline bool SaveSnapshot( xecs::game_mgr::instance& GameMgr, const std::wstring& Path ) noexcept
    {
        const std::string PathA{ std::filesystem::path(Path).string() };
        if (auto Err = GameMgr.SerializeGameState(PathA.c_str(), /*isRead*/false, /*isBinary*/true); Err)
        {
            LogGamePlugin(std::format("Game.dll: snapshot save failed: {}", Err.getMessage()));
            return false;
        }
        return true;
    }

    inline bool LoadSnapshot( xecs::game_mgr::instance& GameMgr, const std::wstring& Path ) noexcept
    {
        const std::string PathA{ std::filesystem::path(Path).string() };
        if (auto Err = GameMgr.SerializeGameState(PathA.c_str(), /*isRead*/true, /*isBinary*/true); Err)
        {
            LogGamePlugin(std::format("Game.dll: snapshot restore failed: {}", Err.getMessage()));
            return false;
        }
        return true;
    }

    //---------------------------------------------------------------------------
    // Diagnostic only - the Level tree can't show anything meaningful right after a raw snapshot
    // restore (no Scene ever gets reopened - see persist_mode's own comment), so this is the one way
    // to actually confirm real entity/component data survived the round trip rather than just
    // guessing from an empty-looking tree. Left in permanently (not added-then-reverted) per this
    // project's own persistent-diagnostic-logging convention - logged to the Game.dll Log panel,
    // which is already visible, after every single reload regardless of which persist_mode ran.
    //---------------------------------------------------------------------------
    inline void LogWorldEntityCount( xecs::game_mgr::instance& GameMgr, const char* pLabel ) noexcept
    {
        int nArchetypes = 0;
        int nEntities    = 0;
        for (auto& pArchetype : GameMgr.m_ArchetypeMgr.m_lArchetype)
        {
            ++nArchetypes;
            for (auto pF = pArchetype->getFamilyHead(); pF; pF = pF->m_Next.get())
                for (auto pP = &pF->m_DefaultPool; pP; pP = pP->m_Next.get())
                    nEntities += pP->Size();
        }
        LogGamePlugin(std::format("Game.dll: [{}] world now has {} archetype(s), {} live entit(y/ies)", pLabel, nArchetypes, nEntities));
    }

    //---------------------------------------------------------------------------
    // The Vn (RawSnapshotBridge) tree-preservation trick - direct user insight: "the raw
    // serialization just needs to make sure entities are restored with the same exact ID" (confirmed
    // empirically: xecs::component::entity's own m_Value round-trips bit-for-bit identical through
    // SerializeGameState - the write path's own "GlobalEntities" record restores each entity's
    // Validation flag at its EXACT original global-info slot index, not a freshly reallocated one -
    // see xecs_game_mgr.cpp's own comment on that record). Since the entity VALUES a Scene's
    // m_LocalToRuntime/m_RuntimeToLocal maps reference never change, NO translation is needed at all
    // - moving the whole xecs::scene::instance object out before the destroy and back in after the
    // restore is sufficient; its maps are still valid, unmodified, pointing at the exact same entity
    // values that come back. Folders/parent-scene edges/pending-changes/everything else about the
    // scene comes along for free in the same move, for the same reason RestoreFromV1 doesn't need
    // any of this at all (it goes through OpenLevel instead).
    //---------------------------------------------------------------------------
    inline std::vector<std::unique_ptr<xecs::scene::instance>> CaptureOpenScenes
    ( xecs::game_mgr::instance& GameMgr
    , const editor_state&       State
    ) noexcept
    {
        std::vector<std::unique_ptr<xecs::scene::instance>> Captured;
        for (auto& SceneGuid : State.m_OpenScenes)
        {
            if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
            {
                LogGamePlugin(std::format("Game.dll: [Vn capture] scene {:016X} - {} entit(y/ies), {} folder(s)",
                    SceneGuid.m_Instance.m_Value, pScene->m_LocalToRuntime.size(), pScene->m_Folders.size()));
                Captured.push_back(std::make_unique<xecs::scene::instance>(std::move(*pScene)));
            }
            else
            {
                LogGamePlugin(std::format("Game.dll: [Vn capture] scene {:016X} NOT FOUND in SceneMgr", SceneGuid.m_Instance.m_Value));
            }
        }
        LogGamePlugin(std::format("Game.dll: [Vn capture] {} of {} open scene(s) captured", Captured.size(), State.m_OpenScenes.size()));
        return Captured;
    }

    inline void ReattachOpenScenes
    ( xecs::game_mgr::instance&                              GameMgr
    , std::vector<std::unique_ptr<xecs::scene::instance>>&&  Captured
    ) noexcept
    {
        for (auto& pScene : Captured)
        {
            const auto SceneGuid = pScene->m_Guid;
            auto& NewScene = GameMgr.m_SceneMgr.FindOrCreate(SceneGuid);
            NewScene = std::move(*pScene);
            LogGamePlugin(std::format("Game.dll: [Vn reattach] scene {:016X} - {} entit(y/ies), {} folder(s), state={}",
                SceneGuid.m_Instance.m_Value, NewScene.m_LocalToRuntime.size(), NewScene.m_Folders.size(), (int)NewScene.m_State));
        }
    }

    //---------------------------------------------------------------------------
    // Re-registers an ALREADY-loaded plugin module's components against a freshly reset registry,
    // without touching the DLL itself at all (no FreeLibrary/LoadLibrary, no new shadow copy, same
    // xecs::plugin::token/generation as before) - the world still has to be destroyed and recreated
    // (the component registry is reset process-wide the moment ANY plugin generation changes owner,
    // and a fresh xecs::game_mgr::instance needs everything re-registered into it from scratch), but
    // the CODE didn't change, so there's no reason to pay for a fresh compile-output copy or a
    // FreeLibrary/LoadLibrary cycle. Used by StopPlaySession, where "as fast as possible" applies just
    // as much as it does to the play-session snapshot above - Stop is not a recompile, it's "throw
    // away the play session's world and rebuild a clean one".
    //---------------------------------------------------------------------------
    inline void ReregisterAlreadyLoadedPlugin( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pRegisterComponents = reinterpret_cast<xecs_plugin_pfn_register_components*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_REGISTER_COMPONENTS_NAME)))
            pRegisterComponents(GameMgr, Plugin.m_Token);
    }

    //---------------------------------------------------------------------------
    // Step 1 of 2 - a recompile-CHECK, not a user-facing "reload" action anymore (there is no more
    // manual "Reload Game" button - matches Unity's own model: recompiling is something the editor
    // just does for you). Called automatically from two places only, per direct user direction: once
    // on the frame the app window regains OS focus (xgpu::tools::imgui::ConsumeWindowFocusGained -
    // "the user tabbed back in after editing code"), and once when the Play button is pressed
    // (Stopped -> Playing only - see editor_state::m_bPlayRequested). Kicks off
    // BuildGamePluginIfStale on a background thread and returns immediately; does NOT touch
    // pGameMgr/the world/the currently loaded generation AT ALL - that's the whole point (see
    // game_plugin_state's own comment). A no-op if a build is already in flight.
    //---------------------------------------------------------------------------
    inline void StartGameReload( game_plugin_state& Plugin ) noexcept
    {
        if (Plugin.m_bBuilding) return;

        Plugin.m_bBuilding  = true;
        Plugin.m_BuildFuture = std::async(std::launch::async, [&Plugin]() noexcept
        {
            return BuildGamePluginIfStale(Plugin);
        });
    }

    // How RebuildWorld persists the world across the destroy/recreate it always does - the ONE thing
    // that genuinely differs between "a normal reload" and "Stop", beyond just which DLL-swap
    // strategy applies. Direct user model: Play writes ONE snapshot ("V1", the REAL Scene/Level/
    // Prefab disk save - see GetReloadBridgeSnapshotPath's own comment for why this must be disk, not
    // the fast binary dump) the moment it starts; every mid-play/paused reload afterward writes its
    // own throwaway "Vn" (n>1, the fast binary bridge) purely to keep gameplay continuous across that
    // one reload - Stop only ever cares about V1, every Vn is 100% irrelevant to it, because the
    // whole point of Stop is getting back to normal editing - Level tree included - exactly as it was
    // before Play, matching Unity's own Play/Stop semantics.
    //
    //   RawSnapshotBridge - EVERY code-triggered reload (Playing, Paused, AND plain edit-mode) lands
    //                       here now - the world must be destroyed anyway (a Game.dll swap), so
    //                       write/read this reload's own throwaway "Vn" (GetReloadBridgeSnapshotPath -
    //                       overwritten every cycle) so whatever the user currently has - gameplay
    //                       state while Playing, or just unsaved edits while Stopped - survives the
    //                       destroy/recreate intact, entirely in memory, without touching the real
    //                       saved project on disk. Scene-unaware, and deliberately so (Vn never needs
    //                       the tree, only V1 does) - CaptureOpenScenes/ReattachOpenScenes carry the
    //                       tree across separately, snapshot-independent.
    //   RestoreFromV1     - Stop. Never saves anything - there's nothing worth saving; whatever the
    //                       play session's raw Vn bridging left the world in is 100% discarded.
    //                       Reloads via OpenLevel - correct precisely because V1 was itself a real
    //                       disk save (written explicitly by Play, or by PollGameReload's own
    //                       UpToDate/Rebuilt branches right before flipping to Playing - never as a
    //                       silent side effect of an edit-mode reload), so "reload from disk" already
    //                       means "reload V1", nothing more needs building.
    //
    // There used to be a third mode, DiskSaveAndReload, used for every edit-mode (not-playing)
    // reload - it saved the real Scene/Level/Prefab assets to disk unconditionally as part of the
    // reload. Removed per direct user request after an external review correctly flagged it: tabbing
    // back into the editor after an unrelated code edit would silently commit whatever was in the
    // scene to disk, with no explicit Save action from the user - surprising, and unlike Unity/Unreal,
    // neither of which persists anything to the real project on a domain reload / Live Coding patch.
    // RawSnapshotBridge already does everything DiskSaveAndReload needed (preserve current state
    // across the destroy/recreate) without the disk write, so switching every reload to it was a
    // straight subtraction, not a new code path - see PollGameReload's own comment for the one place
    // that used to get V1 "for free" as DiskSaveAndReload's side effect and now writes it explicitly.
    enum class persist_mode : std::uint8_t { RawSnapshotBridge, RestoreFromV1 };

    //---------------------------------------------------------------------------
    // The one shared "destroy the world and rebuild it" skeleton - every reload E29 ever does
    // (a genuine Game.dll recompile, or just discarding a play session on Stop) is exactly this same
    // sequence, differing only in two independent axes, both parameterized rather than duplicated:
    //
    //   bSwapDll     - true: the code actually changed (a Rebuilt generation) - unload the old
    //                  module (if any), copy+load the new one, bump the plugin token's generation.
    //                  false: the module already loaded is still perfectly good (Stop, or a
    //                  recompile check that found nothing to do) - just re-run its OWN
    //                  RegisterComponents against the freshly reset registry (see
    //                  ReregisterAlreadyLoadedPlugin), same token/generation, no DLL I/O at all.
    //
    //   PersistMode  - see persist_mode's own comment above.
    //
    // pGameMgr, InspectorBridge and EntityInspector are all rebound in place (pGameMgr reset and
    // reconstructed; InspectorBridge.RegisterCallbacks re-run against the new instance - its own
    // callbacks are stored as std::function MEMBERS specifically so they can be rebound like this,
    // see its own declaration comment) - the caller's own references/pointers to these three stay
    // valid across the call; only their CONTENTS change. g_pGameMgr is updated to match.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    bool RebuildWorld
    ( std::unique_ptr<xecs::game_mgr::instance>&  pGameMgr
    , editor_state&                               State
    , game_plugin_state&                          Plugin
    , xproperty::inspector&                       EntityInspector
    , entity_inspector_bridge&                    InspectorBridge
    , const std::wstring&                         ProjectPath
    , T_REGISTER_HOST_COMPONENTS_FN&&              RegisterHostComponents
    , T_REGISTER_HOST_SYSTEMS_FN&&                 RegisterHostSystems
    , bool                                        bSwapDll
    , persist_mode                                PersistMode
    ) noexcept
    {
        // Captured BEFORE the destroy, only for the Vn bridge - see CaptureOpenScenes/
        // ReattachOpenScenes's own comment for why this is safe with zero translation (entity IDs
        // round-trip identical through the raw snapshot). Empty for every other PersistMode.
        auto CapturedScenes = (PersistMode == persist_mode::RawSnapshotBridge)
            ? CaptureOpenScenes(*pGameMgr, State)
            : std::vector<std::unique_ptr<xecs::scene::instance>>{};

        switch (PersistMode)
        {
        case persist_mode::RawSnapshotBridge: SaveSnapshot(*pGameMgr, GetReloadBridgeSnapshotPath()); break;
        case persist_mode::RestoreFromV1:     /* nothing worth saving */                              break;
        }

        const bool bHadPlugin = Plugin.isLoaded();

        // Destroy the entire runtime world FIRST - by the time UnloadGamePlugin's own
        // UnregisterPlugin(Token) call (or, for a non-DLL-swap reload, the plain
        // xecs::component::mgr::resetRegistrations() below) resets the shared component registry,
        // nothing still depends on any current BitID assignment (see xecs_component_mgr.h's own
        // comment on UnregisterPlugin for exactly why that ordering is what makes a full reset
        // correct here).
        pGameMgr.reset();

        if (bSwapDll)
        {
            if (bHadPlugin) UnloadGamePlugin(Plugin);
            else             xecs::component::mgr::resetRegistrations();
        }
        else
        {
            // No DLL swap - the module (if any) stays loaded exactly as it is; only the registry
            // needs resetting so the fresh instance below has a blank slate to register into.
            xecs::component::mgr::resetRegistrations();
        }

        pGameMgr = std::make_unique<xecs::game_mgr::instance>();

        RegisterHostComponents(*pGameMgr);

        const bool bLoaded = bSwapDll
            ? LoadGamePluginComponents(*pGameMgr, Plugin, Plugin.m_Token.m_Generation + 1)
            : (ReregisterAlreadyLoadedPlugin(*pGameMgr, Plugin), Plugin.isLoaded());

        RegisterHostSystems(*pGameMgr);
        RegisterGamePluginSystems(*pGameMgr, Plugin);

        pGameMgr->m_SceneMgr.m_ProjectPath  = ProjectPath;
        pGameMgr->m_LevelMgr.m_ProjectPath  = ProjectPath;
        pGameMgr->m_PrefabMgr.m_ProjectPath = ProjectPath;
        pGameMgr->m_SystemMgr.m_ProjectPath = ProjectPath;
        if (auto Err = pGameMgr->m_SystemMgr.Load(); Err)
            Debugger(std::format("Failed to load System Registry order: {}", Err.getMessage()));


        g_pGameMgr = pGameMgr.get();
        InspectorBridge.RegisterCallbacks(EntityInspector, *pGameMgr, State);

        // State.m_SelectedEntity is the only RUNTIME handle here (m_GlobalInfoIndex/m_Validation -
        // meaningless once pGameMgr.reset() destroyed the world it indexed into). Everything else
        // E29 tracks selection with (m_SelectedEntityId, m_SelectedEntityScene,
        // m_MultiSelectedEntityIds/Order, m_MultiSelectScene) is already a STABLE identity
        // (permanent_id / scene guid) - cleared here, then RE-RESOLVED below once the scene has been
        // repopulated, via the scene's own m_LocalToRuntime (the exact same permanent_id -> live-
        // handle lookup every other entity-migrating code path in this Kit already relies on).
        State.m_SelectedEntity        = {};
        State.m_bEntityInspectorDirty = true;

        if (PersistMode == persist_mode::RawSnapshotBridge)
        {
            LoadSnapshot(*pGameMgr, GetReloadBridgeSnapshotPath());
            // Reattach the Scenes captured before the destroy - see CaptureOpenScenes/
            // ReattachOpenScenes's own comment for why zero translation is needed (entity IDs
            // round-trip identical through the raw snapshot). Repopulates State.m_OpenScenes'
            // worth of Scene objects (folders, entities, everything) so the Level tree survives a
            // mid-play reload intact, not just once Stop runs.
            ReattachOpenScenes(*pGameMgr, std::move(CapturedScenes));

            // The Level tree itself is gated on GameMgr.m_LevelMgr.Find(State.m_CurrentLevel) - a
            // SEPARATE manager from m_SceneMgr, ALSO destroyed by pGameMgr.reset() above, and the
            // reattached Scenes above don't touch it at all. Unlike a Scene, a Level holds no runtime
            // entity state whatsoever (just its own name + a list of member Scene guids - see
            // xecs_level.h's own instance struct) - a cheap descriptor-only disk read (Load, NOT
            // Activate - Activate would re-run EnsureLoaded on every member Scene, re-loading
            // entities from disk and clobbering the live, just-reattached Scene objects above) is
            // all it needs, and is exactly what was missing: without this, GameMgr.m_LevelMgr.Find()
            // returned nullptr and RenderLevelTreePanel rendered nothing at all - visually identical
            // to a genuinely empty tree, even though the Scenes/entities themselves were fine.
            if (!State.m_CurrentLevel.empty())
                pGameMgr->m_LevelMgr.Load(State.m_CurrentLevel);
        }
        else if (!State.m_CurrentLevel.empty())
        {
            // Only persist_mode::RestoreFromV1 (Stop) ever lands here now - the only other mode,
            // RawSnapshotBridge, is caught by the `if` above. V1 IS a real disk save (see
            // persist_mode's own comment), so "reload from disk" already means "reload V1" for Stop;
            // nothing extra to build. This is what correctly restores the Level tree exactly as it
            // was before Play - entities that died or got created (into the default folder) during
            // the play session are discarded, matching Unity's own Play/Stop semantics.
            OpenLevel(*pGameMgr, State, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });
        }

        LogWorldEntityCount(*pGameMgr, PersistMode == persist_mode::RawSnapshotBridge ? "Vn restore" : "V1/disk restore");

        // Re-resolve the selection against the freshly reloaded scene. The common case - nothing
        // about this specific entity changed, only the runtime world it lives in was rebuilt -
        // picks selection (and the Entity Properties panel) back up right where it was; if the
        // entity is genuinely gone (e.g. deleted on disk since the last save, or no Scene was
        // reopened at all - the raw-snapshot case above), this falls back to no selection rather
        // than holding a permanent_id that no longer resolves to anything.
        if (State.m_SelectedEntityId != xecs::scene::invalid_permanent_id_v)
        {
            if (auto* pScene = pGameMgr->m_SceneMgr.Find(State.m_SelectedEntityScene))
            {
                if (auto It = pScene->m_LocalToRuntime.find(State.m_SelectedEntityId); It != pScene->m_LocalToRuntime.end())
                    State.m_SelectedEntity = It->second;
                else
                    State.m_SelectedEntityId = xecs::scene::invalid_permanent_id_v;
            }
            else
            {
                State.m_SelectedEntityId = xecs::scene::invalid_permanent_id_v;
            }
        }

        return bLoaded;
    }

    //---------------------------------------------------------------------------
    // The "Stop" button's own handler - always discards whatever a play session did (including any
    // mid-play raw-snapshot reloads along the way - see RebuildWorld's own comment) in favor of a
    // proper, fully correct reload from the last real disk save (the one Play itself made on the way
    // in - see the Play button's own handler in E29_LevelScene_Editor.cpp). No DLL swap here: Stop
    // doesn't imply a code change, so the currently loaded generation is re-registered in place
    // (bSwapDll=false) rather than paying for an unload/reload cycle it doesn't need.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    void StopPlaySession
    ( std::unique_ptr<xecs::game_mgr::instance>&  pGameMgr
    , editor_state&                               State
    , game_plugin_state&                          Plugin
    , xproperty::inspector&                       EntityInspector
    , entity_inspector_bridge&                    InspectorBridge
    , const std::wstring&                         ProjectPath
    , T_REGISTER_HOST_COMPONENTS_FN&&              RegisterHostComponents
    , T_REGISTER_HOST_SYSTEMS_FN&&                 RegisterHostSystems
    ) noexcept
    {
        pGameMgr->Stop();
        RebuildWorld
        ( pGameMgr, State, Plugin, EntityInspector, InspectorBridge, ProjectPath
        , RegisterHostComponents, RegisterHostSystems
        , /*bSwapDll*/ false, persist_mode::RestoreFromV1
        );
        State.m_PlayState = editor_state::play_state::Stopped;
    }

    //---------------------------------------------------------------------------
    // Step 2 of 2 - call once per frame, at a clean frame boundary (BEFORE BeginRendering, never
    // mid-frame - confirmed empirically that running the heavy world-rebuild synchronously inside an
    // active ImGui frame corrupts its window-stack bookkeeping). A no-op unless a build is both
    // in-flight AND finished (checked via a non-blocking wait_for), so safe to call unconditionally
    // every frame regardless of Plugin.m_bBuilding's current value.
    //
    // On a FAILED build: stops here, and cancels any pending Play request (m_bPlayRequested) rather
    // than starting a play session against a known-broken build. The currently loaded generation (if
    // any) was never unloaded, never touched - it just keeps running exactly as it was.
    //
    // On UpToDate (checked but nothing needed rebuilding - the common case once this runs on every
    // focus-regain, not just an explicit click): no world-touching reload at all. If a Play was
    // requested, it can proceed directly - SaveEverything below gives Stop a fresh, correct revert
    // point, and Play just keeps ticking the SAME live world (no reason to tear anything down over a
    // check that found nothing to do).
    //
    // On Rebuilt: runs the full destroy/recreate/DLL-swap sequence via RebuildWorld, always via the
    // raw in-memory snapshot bridge (persist_mode::RawSnapshotBridge) regardless of Play state - see
    // persist_mode's own comment for why this reload never touches disk on its own anymore. If a Play
    // was ALSO requested (the user pressed Play while a rebuild happened to be needed), enters play
    // directly afterward - but MUST write V1 explicitly here (see below), since the reload itself no
    // longer does that as a side effect the way the old DiskSaveAndReload mode used to.
    //---------------------------------------------------------------------------
    template< typename T_REGISTER_HOST_COMPONENTS_FN, typename T_REGISTER_HOST_SYSTEMS_FN >
    bool PollGameReload
    ( std::unique_ptr<xecs::game_mgr::instance>&  pGameMgr
    , editor_state&                               State
    , game_plugin_state&                          Plugin
    , xproperty::inspector&                       EntityInspector
    , entity_inspector_bridge&                    InspectorBridge
    , const std::wstring&                         ProjectPath
    , T_REGISTER_HOST_COMPONENTS_FN&&              RegisterHostComponents  // (xecs::game_mgr::instance&) noexcept - e.g. registers e29::name/transform/etc
    , T_REGISTER_HOST_SYSTEMS_FN&&                 RegisterHostSystems     // (xecs::game_mgr::instance&) noexcept - e.g. registers e29::tick_logger_a/b
    ) noexcept
    {
        if (!Plugin.m_bBuilding) return false;
        if (Plugin.m_BuildFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;

        const build_result Result = Plugin.m_BuildFuture.get();
        Plugin.m_bBuilding = false;

        if (Result == build_result::Failed)
        {
            State.m_bPlayRequested = false;
            return false;
        }

        if (Result == build_result::UpToDate)
        {
            if (State.m_bPlayRequested)
            {
                State.m_bPlayRequested = false;
                // Write V1 - the real disk save Stop will restore from (see persist_mode's own
                // comment for why this must be disk, not the fast binary Vn bridge - Stop needs the
                // Level tree back, not just raw component values).
                SaveEverything(*pGameMgr, State);
                State.m_PlayState = editor_state::play_state::Playing;
            }
            return false;
        }

        // Result == build_result::Rebuilt
        const bool bLoaded = RebuildWorld
        ( pGameMgr, State, Plugin, EntityInspector, InspectorBridge, ProjectPath
        , RegisterHostComponents, RegisterHostSystems
        , /*bSwapDll*/ true, persist_mode::RawSnapshotBridge
        );

        if (State.m_bPlayRequested)
        {
            State.m_bPlayRequested = false;
            // Write V1 explicitly, same as the UpToDate branch above - the reload just above used
            // RawSnapshotBridge (never touches disk), so unlike before this removed the
            // DiskSaveAndReload mode, V1 is no longer a free side effect of the reload itself.
            SaveEverything(*pGameMgr, State);
            State.m_PlayState = editor_state::play_state::Playing;
        }

        return bLoaded;
    }
}

#endif
