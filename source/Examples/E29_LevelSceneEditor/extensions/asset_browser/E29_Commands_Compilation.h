#ifndef E29_COMMANDS_COMPILATION_H
#define E29_COMMANDS_COMPILATION_H
#pragma once

// Compilation command layer - direct user request after a long live-debugging session on the
// Compilation view's Pause/Resume/Recompile buttons: "you should add commands from the compilation
// so that you can trigger what you need without going through the UI." Every command here is a thin
// wrapper over e10::library_mgr::m_Compilation (E10_AssetMgr.h) - same "wrap the real primitive,
// don't reinvent it" shape every other command file in this folder already uses. None of these are
// undoable (matching Play/Pause/Stop's own precedent, E29_Commands_PlaySession.h) - pausing/resuming/
// recompiling the asset pipeline has no meaningful "undo" the way an entity edit does.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    //================================================================================================
    // RecompileAll - force every resource, in every open library, back into the compilation queue.
    // Thin wrap of library_mgr::RecompileAllResources() (E10_AssetMgr.h).
    //================================================================================================
    struct recompile_all_query_cmd : xundo::query_command_base
    {
        recompile_all_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "RecompileAll", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Force-requeues every resource in every open library for recompilation. Usage: RecompileAll"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            e10::g_LibMgr.RecompileAllResources();
            return "RecompileAll: requeued";
        }
    };

    //================================================================================================
    // RecompileErrors - requeue exactly the resources currently known to have failed their last
    // compile (compilation::instance::m_Failed). Thin wrap of RecompileFailedResources().
    //================================================================================================
    struct recompile_errors_query_cmd : xundo::query_command_base
    {
        recompile_errors_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "RecompileErrors", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Requeues every resource that failed its last compile. Usage: RecompileErrors"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            e10::g_LibMgr.RecompileFailedResources();
            return "RecompileErrors: requeued";
        }
    };

    //================================================================================================
    // CompileStart - manual trigger, same as the toolbar's own "Compile" button when Auto-Compile is
    // off. StartCompilation() is itself a safe no-op if a compile is already running (compare_exchange
    // guard) - see its own comment in E10_AssetMgr.h.
    //================================================================================================
    struct compile_start_query_cmd : xundo::query_command_base
    {
        compile_start_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "CompileStart", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Manually kicks the compilation queue (safe no-op if already running). Usage: CompileStart"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            e10::g_LibMgr.m_Compilation.StartCompilation();
            return "CompileStart: requested";
        }
    };

    //================================================================================================
    // CompilePause - -State true/false. Mirrors the Pause button's own click handler exactly,
    // including the live-found fix: resuming (false, from a previously-true state) must also call
    // StartCompilation() itself, or the queue just sits there - flipping the flag alone was never
    // enough (see PauseCompilation's own call site in E10_asset_browser_compiler_tab.h).
    //================================================================================================
    struct compile_pause_query_cmd : xundo::query_command_base
    {
        compile_pause_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "CompilePause", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Pauses or resumes the compilation queue. Usage: CompilePause -State true|false"; }
        void RegisterArguments() noexcept override
        {
            m_hState = m_Parser.addOption("State", "true = pause, false = resume", true, 1);
        }
        std::string Query() noexcept override
        {
            auto StateArg = m_Parser.getOptionArgAs<std::string>(m_hState, 0);
            if (std::holds_alternative<xerr>(StateArg)) return "CompilePause: bad arguments";
            const std::string& S = std::get<std::string>(StateArg);
            const bool bPause = (S == "true" || S == "1");

            const bool bWasPaused = e10::g_LibMgr.m_Compilation.m_PauseCompilation.load();
            e10::g_LibMgr.m_Compilation.PauseCompilation(bPause);
            if (bWasPaused && !bPause) e10::g_LibMgr.m_Compilation.StartCompilation();
            return bPause ? "CompilePause: paused" : "CompilePause: resumed";
        }
        xcmdline::parser::handle m_hState;
    };

    //================================================================================================
    // CompileAuto - -State true/false, mirrors the Auto-Compile toggle button. Re-enabling calls
    // StartCompilation() itself, matching that button's own click handler.
    //================================================================================================
    struct compile_auto_query_cmd : xundo::query_command_base
    {
        compile_auto_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "CompileAuto", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Turns automatic compilation on or off. Usage: CompileAuto -State true|false"; }
        void RegisterArguments() noexcept override
        {
            m_hState = m_Parser.addOption("State", "true = automatic, false = manual", true, 1);
        }
        std::string Query() noexcept override
        {
            auto StateArg = m_Parser.getOptionArgAs<std::string>(m_hState, 0);
            if (std::holds_alternative<xerr>(StateArg)) return "CompileAuto: bad arguments";
            const std::string& S = std::get<std::string>(StateArg);
            const bool bAuto = (S == "true" || S == "1");

            const bool bWasAuto = e10::g_LibMgr.m_Compilation.m_AutoCompilation.load();
            e10::g_LibMgr.m_Compilation.m_AutoCompilation.store(bAuto);
            if (!bWasAuto && bAuto) e10::g_LibMgr.m_Compilation.StartCompilation();
            return bAuto ? "CompileAuto: on" : "CompileAuto: off";
        }
        xcmdline::parser::handle m_hState;
    };

    //================================================================================================
    // CompileStatus - a single-shot snapshot of the whole pipeline: counts (compiling/waiting per
    // priority level/failed/historical) plus every flag driving ContinueCompiling(), so a script or
    // AI can poll "is it actually stuck, or just slow" without ever opening the Compilation window.
    // Directly answers the exact question this command file's own history was built to answer live.
    //================================================================================================
    struct compile_status_query_cmd : xundo::query_command_base
    {
        compile_status_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "CompileStatus", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Reports compilation queue depth, in-flight count, and every pause/auto/running flag. Usage: CompileStatus"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            auto& Compilation = e10::g_LibMgr.m_Compilation;

            std::size_t Compiling = 0;
            { std::lock_guard Lk(Compilation.m_Compiling.m_Mutex); Compiling = Compilation.m_Compiling.m_List.size(); }

            std::string WaitingPerLevel;
            std::size_t WaitingTotal = 0;
            std::vector<std::pair<int, e10::compilation::entry>> WaitingEntries; // (level, entry) - names resolved AFTER releasing m_Queue's lock, never nested inside it
            {
                xcontainer::lock::scope Lk(Compilation.m_Queue);
                int iLevel = 0;
                for (auto& Q : Compilation.m_Queue.get())
                {
                    WaitingPerLevel += std::format("{}{}", WaitingPerLevel.empty() ? "" : ",", Q.size());
                    WaitingTotal += Q.size();
                    for (auto& E : Q)
                        WaitingEntries.push_back({ iLevel, E });
                    ++iLevel;
                }
            }
            std::string WaitingNames;
            const auto Now = std::filesystem::file_time_type::clock::now();
            auto AgeSeconds = [&](std::filesystem::file_time_type T) noexcept
            {
                return std::chrono::duration_cast<std::chrono::seconds>(Now - T).count();
            };
            for (auto& [Level, E] : WaitingEntries)
            {
                std::string Name = "(unnamed)";
                e10::g_LibMgr.getNodeInfo(E.m_gLibrary, E.m_FullGuid, [&](const e10::library_db::info_node& Node)
                {
                    Name = Node.m_Info.m_Name;
                    WaitingNames += std::format(" [L{}:{} guid={:016X} ResourceTime=-{}s DescriptorTime=-{}s NewestDependencyTime=-{}s]"
                        , Level, Name, E.m_FullGuid.m_Instance.m_Value
                        , AgeSeconds(Node.m_ResourceTime), AgeSeconds(Node.m_DescriptorTime), AgeSeconds(Node.m_NewestDependencyTime));
                });
            }

            std::size_t Failed = 0;
            { std::scoped_lock Lk(Compilation.m_Failed.m_Mutex); Failed = Compilation.m_Failed.m_Map.size(); }

            std::size_t Historical = 0;
            { std::lock_guard Lk(Compilation.m_Historical.m_Mutex); Historical = Compilation.m_Historical.m_List.size(); }

            return std::format(
                "Compiling={} Waiting={} (per-level=[{}]) Failed={} Historical={} | "
                "IsCompiling={} Auto={} Paused={} Running={} WorkersWorking={} |{}"
                , Compiling, WaitingTotal, WaitingPerLevel, Failed, Historical
                , Compilation.m_isCompiling.load(), Compilation.m_AutoCompilation.load()
                , Compilation.m_PauseCompilation.load(), Compilation.m_bRunning.load()
                , Compilation.m_WorkersWorking.load(), WaitingNames);
        }
    };
}

#endif // E29_COMMANDS_COMPILATION_H
