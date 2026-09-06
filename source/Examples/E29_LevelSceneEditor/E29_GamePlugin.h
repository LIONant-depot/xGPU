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
        std::future<bool>       m_BuildFuture;
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
    inline bool BuildGamePluginIfStale( game_plugin_state& Plugin ) noexcept
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
                return true;
            }
            const auto DllTime = std::filesystem::last_write_time(Dll, Ec);
            bStale = Ec || SourceTime > DllTime;
        }

        if (!bStale)
        {
            Plugin.m_LastStatus = "Game.dll: up to date, no rebuild needed";
            LogGamePlugin(Plugin.m_LastStatus);
            return true; // already up to date - load it directly, no rebuild attempted
        }

        Plugin.m_LastStatus = std::format("Game.dll: {} - rebuilding via cmake...", bDllMissing ? "DLL missing" : "source newer than DLL");
        LogGamePlugin(Plugin.m_LastStatus);

        const std::wstring CmdLine = std::format(L"cmake --build \"{}\" --target E29_Game --config {}", BuildDir.wstring(), Config);

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
            return true;
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
            return false; // still attempt to load below - a stale-but-working DLL beats none at all
        }

        Plugin.m_LastStatus = "Game.dll: rebuild succeeded";
        LogGamePlugin(Plugin.m_LastStatus);
        return true;
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
        const auto SrcPdb  = Dir / (Stem + L".pdb");
        const auto NewPdb  = Dir / std::format(L"{}_loaded_{}.pdb", Stem, Generation);

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
        // cosmetic, never a correctness problem.
        if (!Plugin.m_LoadedDllPath.empty())
        {
            std::error_code Ec;
            std::filesystem::remove(Plugin.m_LoadedDllPath, Ec);
            std::filesystem::remove(std::filesystem::path(Plugin.m_LoadedDllPath).replace_extension(L".pdb"), Ec);
            Plugin.m_LoadedDllPath.clear();
        }
    }

    //---------------------------------------------------------------------------
    // Step 1 of 2 - call from the "Reload Game" button click. Kicks off BuildGamePluginIfStale on a
    // background thread and returns immediately; does NOT touch pGameMgr/the world/the currently
    // loaded generation AT ALL - that's the whole point (see game_plugin_state's own comment). A
    // no-op if a build is already in flight (the button should be disabled then anyway via
    // Plugin.m_bBuilding - this is just defense in depth against a stray extra click).
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

    //---------------------------------------------------------------------------
    // Step 2 of 2 - call once per frame, at a clean frame boundary (the same spot the old
    // synchronous reload used to run from - BEFORE BeginRendering, never mid-frame; see the
    // main loop's own comment on why that ordering matters). A no-op unless a build is both
    // in-flight AND finished (checked via a non-blocking wait_for), so safe to call unconditionally
    // every frame regardless of Plugin.m_bBuilding's current value.
    //
    // On a FAILED build: stops here. The currently loaded generation (if any) was never unloaded,
    // never touched - it just keeps running exactly as it was, per the user's own explicit
    // requirement ("if it fails to compile it should reload the old dll" - achieved here by simply
    // never having unloaded it in the first place, which the compiled/loaded DLL split makes safe).
    //
    // On success (build succeeded, or wasn't needed): runs Phase 8A's full reload sequence - Save ->
    // destroy the runtime world -> unregister/unload the old plugin generation -> load the new one
    // -> create a fresh world -> reload the persistent world into it. Same "abort only if Save
    // fails; everything after is best-effort" philosophy E27_NodeOS's own ReloadPlugin uses (see its
    // own comment) - once the world is destroyed there is no partial state worth preserving over
    // just getting back to A working world.
    //
    // pGameMgr, InspectorBridge and EntityInspector are all rebound in place (pGameMgr reset and
    // reconstructed; InspectorBridge.RegisterCallbacks re-run against the new instance - its own
    // callbacks are stored as std::function MEMBERS specifically so they can be rebound like this,
    // see its own declaration comment) - the caller's own references/pointers to these three stay
    // valid across the call; only their CONTENTS change. g_pGameMgr is updated to match.
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

        const bool bBuildOk = Plugin.m_BuildFuture.get();
        Plugin.m_bBuilding = false;

        if (!bBuildOk)
        {
            // Build genuinely failed (real compile error, not just "nothing to do") - the error is
            // already in the log (BuildGamePluginIfStale logged it). The currently loaded generation,
            // if any, was never unloaded - it's still running exactly as it was.
            return false;
        }

        SaveEverything(*pGameMgr, State);

        const bool bHadPlugin = Plugin.isLoaded();
        const std::uint32_t NextGeneration = Plugin.m_Token.m_Generation + 1;

        // Destroy the entire runtime world FIRST - by the time UnloadGamePlugin's own
        // UnregisterPlugin(Token) call resets the shared component registry, nothing still
        // depends on any current BitID assignment (see xecs_component_mgr.h's own comment on
        // UnregisterPlugin for exactly why that ordering is what makes a full reset correct here).
        pGameMgr.reset();

        if (bHadPlugin)
        {
            UnloadGamePlugin(Plugin);
        }
        else
        {
            // No plugin was ever loaded, but a fresh world still needs a fully blank registry to
            // register everything into from scratch (built-ins + the host's own types) - the same
            // "create a fresh world" starting point a plugin-driven reload gets via UnloadGamePlugin.
            xecs::component::mgr::resetRegistrations();
        }

        pGameMgr = std::make_unique<xecs::game_mgr::instance>();

        RegisterHostComponents(*pGameMgr);

        const bool bLoaded = LoadGamePluginComponents(*pGameMgr, Plugin, NextGeneration);

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
        // (permanent_id / scene guid), untouched by the world being destroyed and rebuilt - clearing
        // those too would silently drop the user's selection on every single reload for no reason.
        // Cleared here, then RE-RESOLVED below once OpenLevel has repopulated the scene fresh, via
        // the scene's own m_LocalToRuntime - the exact same permanent_id -> live-handle lookup every
        // other entity-migrating code path in this Kit already relies on (see e.g.
        // CreatePrefabFromGroupRoot's identical Scene.m_LocalToRuntime.find(Id) pattern).
        State.m_SelectedEntity        = {};
        State.m_bEntityInspectorDirty = true;

        if (!State.m_CurrentLevel.empty())
            OpenLevel(*pGameMgr, State, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });

        // Re-resolve the selection against the freshly reloaded scene. The common case - nothing
        // about this specific entity changed, only the runtime world it lives in was rebuilt -
        // picks selection (and the Entity Properties panel) back up right where it was; if the
        // entity is genuinely gone (e.g. deleted on disk since the last save), this falls back to
        // no selection rather than holding a permanent_id that no longer resolves to anything.
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
}

#endif
