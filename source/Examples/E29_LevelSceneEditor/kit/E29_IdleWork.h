#ifndef E29_IDLE_WORK_H
#define E29_IDLE_WORK_H
#pragma once

// Idle Work - a place for maintenance that only matters when nothing else is happening. Direct user
// request/design, arrived at after two rounds of correction on the scene load/save hardening pass
// ([[xecs_scene_save_future_hardening]] memory): the orphan/dangling consistency check started out
// inline on the Load/Play/Stop path, got moved to a detached background thread once "Load/Save should
// stay fast" was raised, then got moved AGAIN - all the way out here - once the real scaling concern
// surfaced: "scenes could be about 1,000,000 entities... what is ok now with 10 may not scale so
// gracefully later." A single scan can genuinely be a long-running disk walk at that size, so even a
// background thread is the wrong trigger point if it fires on every Play/Stop cycle (thread pileup,
// I/O contention with whatever real work is happening). The fix: only run this kind of thing once the
// editor - neither the user NOR any AI/CLI driver - has done anything for a while ("screensaver
// territory"). This file is that dispatch point; the scene sanity scan is its first task, not the
// whole system - a future maintenance task registers here the same way.
//
// Included from the .cpp after E29_CommandContext.h (needs query_command_base for RunSanityCheck,
// below) and after E29_LevelSceneEditorKit.h (needs editor_state).
//
// Tasks run through xscheduler::g_System (dependencies/xscheduler) rather than a raw detached
// std::thread - direct user follow-up after the panel landed: "we should be centralizing all this
// background jobs into submissions in xscheduler somewhere... we need to find the right composition
// so that any part of the system can know how to dump jobs." xscheduler already exists in this repo,
// is already globally Init()'d once in Examples/main.cpp, and is already the compiler's own job
// engine (E10_AssetMgr.h's compilation_job/process_info_job) - this is the first non-compiler consumer,
// not a new dependency. priority::LOW keeps it from competing with real (compiler/render) work for a
// worker slot; complexity::HEAVY matches what it actually is (a real disk walk, not a quick calc).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include <chrono>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <optional>

namespace e29
{
    // How long neither the user nor the CLI/pipe has to be quiet before Idle Work is allowed to run.
    // A "screensaver territory" heuristic, not a precise measurement - picked so it matters during a
    // real editing session's natural pauses without firing between two clicks.
    inline constexpr double idle_threshold_seconds_v = 30.0;

    struct idle_work_state
    {
        std::chrono::steady_clock::time_point m_LastActivityTime      = std::chrono::steady_clock::now();
        // True once this idle period has already triggered a run - reset by NotifyActivity, so Idle
        // Work fires once per transition into idle, never repeatedly while it stays idle.
        bool                                   m_bTriggeredThisIdlePeriod = false;
    };

    // Shared by every in-flight idle task - set the instant real user activity resumes, so any task
    // currently running can notice and stop within roughly one unit of its own work (see
    // ScanSceneConsistency's own use of it) rather than running to completion regardless. Direct user
    // request: "when the user starts using the editor, the idle work should stop ASAP" - not just
    // "won't start a NEW one." Reset to false right before a fresh batch of tasks launches (PumpIdleWork/
    // RunSanityCheck/the panel's own "Run Now"), never left permanently true.
    //
    // Deliberately NOT set from CLI/pipe activity, only from real user input (RequestIdleWorkCancel,
    // called separately - see the main loop's own two call sites for NotifyActivity vs this) - a real
    // bug caught by screenshot-verifying the panel: RunSanityCheck itself logs to the console, which
    // the CLI-activity detector (comparing ConsoleLog size) then saw as "activity," instantly
    // cancelling the very scan the command had just launched. The user's own two requests, read
    // together, actually draw this exact distinction: idleNESS considers BOTH the user and any AI/CLI
    // driver being quiet (so idle work doesn't start while a script is mid-flight), but cancellation on
    // resume is specifically about the USER - an AI/script issuing a command (including one that
    // starts idle work on purpose) has no reason to instantly kill it.
    inline std::atomic<bool>& IdleWorkCancelRequested() noexcept { static std::atomic<bool> Flag{ false }; return Flag; }
    inline void RequestIdleWorkCancel() noexcept { IdleWorkCancelRequested().store(true, std::memory_order_relaxed); }

    // Call whenever real activity happens - a mouse/keyboard event this frame (see
    // DetectUserInputActivity), or a CLI/pipe command actually got processed. Resets the idle clock and
    // re-arms the trigger for the NEXT time things go quiet. Does NOT cancel in-flight idle work by
    // itself - see RequestIdleWorkCancel's own comment for why that's kept separate.
    inline void NotifyActivity(idle_work_state& State) noexcept
    {
        State.m_LastActivityTime       = std::chrono::steady_clock::now();
        State.m_bTriggeredThisIdlePeriod = false;
    }

    // Approximate, not exhaustive (e.g. holding a navigation key with no character output isn't
    // caught) - good enough for a "has anyone touched this app recently" heuristic. Deliberately
    // app-local (ImGui's own io, not a system-wide Win32 GetLastInputInfo) - what matters here is
    // whether THIS editor is being used, not the whole machine.
    inline bool DetectUserInputActivity() noexcept
    {
        const auto& IO = ImGui::GetIO();
        return IO.MouseDelta.x != 0.0f || IO.MouseDelta.y != 0.0f
            || IO.MouseWheel  != 0.0f || IO.MouseWheelH != 0.0f
            || ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)
            || IO.InputQueueCharacters.Size > 0;
    }

    //---------------------------------------------------------------------------
    // Task registry - backs the dockable "Idle Work" panel (RenderIdleWorkPanel, below): what ran,
    // what's running now, how long it took, what it found. Direct user follow-up: "we should have an
    // idle-task window which shows all the idle work in progress, done, priorities, etc." Identified
    // by a monotonic m_TaskId rather than a vector index (a LaunchSceneSanityScan on the main thread
    // can grow the vector - possibly reallocating it - while another task's OWN background thread is
    // concurrently about to update ITS record; an index taken before that growth would then point at
    // the wrong slot, or a stale one after the cap-trim below discards old entries). Capped at
    // idle_task_history_cap_v entries so a long-running session doesn't grow this unbounded.
    //---------------------------------------------------------------------------
    enum class idle_task_status : std::uint8_t { Running, Done, Cancelled };

    // Deliberately generic - direct user framing: "this way stays nice and generic," the whole point
    // of Idle Work being a general dispatch point rather than "the scene scanner with a window bolted
    // on." m_Description/m_Info are plain, pre-formatted strings the TASK itself fills in (a scene
    // scan writes "Scene 1 (guid)" / "1 orphan"; a future non-scene task writes whatever makes sense
    // for it) - the panel never needs to know or special-case what kind of task it's showing.
    struct idle_task_record
    {
        std::uint64_t                         m_TaskId      = 0;
        std::string                           m_Name;
        std::string                           m_Description; // empty = nothing to show
        idle_task_status                      m_Status      = idle_task_status::Running;
        std::chrono::steady_clock::time_point m_StartTime   = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point m_EndTime     = {}; // meaningful once m_Status != Running
        std::string                           m_Info;           // e.g. "1 orphan" - meaningful once m_Status == Done
    };

    inline constexpr std::size_t idle_task_history_cap_v = 100;

    inline std::mutex&                     IdleTaskRegistryMutex() noexcept { static std::mutex M; return M; }
    inline std::vector<idle_task_record>&  IdleTaskRegistry()      noexcept { static std::vector<idle_task_record> R; return R; }

    inline std::uint64_t BeginIdleTask(std::string Name, std::string Description) noexcept
    {
        static std::atomic<std::uint64_t> s_NextId{ 1 };
        const auto TaskId = s_NextId.fetch_add(1, std::memory_order_relaxed);

        std::lock_guard<std::mutex> Lock(IdleTaskRegistryMutex());
        auto& Registry = IdleTaskRegistry();
        if (Registry.size() >= idle_task_history_cap_v)
            Registry.erase(Registry.begin(), Registry.begin() + (Registry.size() - idle_task_history_cap_v + 1));
        Registry.push_back({ TaskId, std::move(Name), std::move(Description) });
        return TaskId;
    }

    inline void EndIdleTask(std::uint64_t TaskId, idle_task_status FinalStatus, std::string Info) noexcept
    {
        std::lock_guard<std::mutex> Lock(IdleTaskRegistryMutex());
        for (auto& Rec : IdleTaskRegistry())
        {
            if (Rec.m_TaskId != TaskId) continue;
            Rec.m_Status  = FinalStatus;
            Rec.m_EndTime = std::chrono::steady_clock::now();
            Rec.m_Info    = std::move(Info);
            break;
        }
    }

    //---------------------------------------------------------------------------
    // Task 1: scene sanity scan (the check that used to live inline in EnsureLoaded - see
    // xecs_scene_inline.h's own comment at the old call site). Guarded against overlapping scans of
    // the SAME scene (a real risk at large entity counts: if a scan takes longer than the gap between
    // two idle periods, launching a second one on top of the first wastes disk I/O for no benefit) via
    // a small, file-local "currently scanning" set - inserted synchronously before the thread starts,
    // erased by the thread itself right before it exits.
    //---------------------------------------------------------------------------
    inline std::mutex& SanityScanMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_set<std::uint64_t>& ScenesBeingScanned() noexcept { static std::unordered_set<std::uint64_t> S; return S; }

    // Shared by the idle-triggered path and the manual CLI command below - takes plain data only
    // (never mgr&/GameMgr&), matching the exact lifetime reasoning DiscoverEntityIds' own comment
    // gives (xecs_scene_inline.h): a detached thread must never hold a reference into something a
    // concurrent Stop/hot-reload could destroy while it's still running. Returns the formatted Info
    // string for a completed scan (e.g. "1 orphan", "2 orphans, 1 dangling reference", or "clean" if
    // nothing was found), or std::nullopt if IdleWorkCancelRequested() fired mid-scan - a cancelled
    // walk's partial on-disk listing would make plenty of real, present files look falsely "missing"
    // from the dangling-reference check, so a cancelled run reports nothing rather than something
    // misleading; it'll simply run again next idle period.
    inline std::optional<std::string> ScanSceneConsistency(std::wstring ProjectPath, xecs::scene::guid SceneGuid, std::vector<xecs::scene::permanent_id> ActiveEntities) noexcept
    {
        const auto OnDisk = xecs::scene::details::DiscoverEntityIds(ProjectPath, SceneGuid, &IdleWorkCancelRequested());
        if (IdleWorkCancelRequested().load(std::memory_order_relaxed)) return std::nullopt;

        std::unordered_set<xecs::scene::permanent_id> ActiveSet(ActiveEntities.begin(), ActiveEntities.end());
        std::unordered_set<xecs::scene::permanent_id> OnDiskSet(OnDisk.begin(), OnDisk.end());

        int OrphanCount = 0, DanglingCount = 0;
        for (auto Id : OnDisk)
        {
            if (ActiveSet.contains(Id)) continue;
            std::printf("[IdleWork] WARNING: Scene=%llX orphaned entity file Id=%u exists on disk but is not listed in the scene descriptor - inert, safe to clean up manually\n", SceneGuid.m_Instance.m_Value, Id);
            ++OrphanCount;
        }
        for (auto Id : ActiveEntities)
        {
            if (OnDiskSet.contains(Id)) continue;
            std::printf("[IdleWork] WARNING: Scene=%llX dangling reference - descriptor lists Id=%u as active but its entity file is missing on disk\n", SceneGuid.m_Instance.m_Value, Id);
            ++DanglingCount;
        }
        if (OrphanCount || DanglingCount) std::fflush(stdout);

        if (OrphanCount == 0 && DanglingCount == 0) return std::string("clean");

        std::string Info;
        if (OrphanCount)
            Info = std::format("{} orphan{}", OrphanCount, OrphanCount == 1 ? "" : "s");
        if (DanglingCount)
        {
            if (!Info.empty()) Info += ", ";
            Info += std::format("{} dangling reference{}", DanglingCount, DanglingCount == 1 ? "" : "s");
        }
        return Info;
    }

    // Launches (or skips, if one's already running for this scene) a detached background scan.
    // Snapshots ActiveEntities from the scene's own live m_LocalToRuntime - the authoritative "what's
    // actually active right now" data, same thing the descriptor's own m_ActiveEntities would have
    // held at load time, just read from the live scene instead since Idle Work runs long after load.
    // Description is resolved once, HERE, via the same {guid -> name} lookup ListScenes/ListLevels
    // already use (BuildAssetNameMap, E29_Commands_Level.h) - the panel itself stays entirely generic,
    // no scene-specific logic left in it at all (direct user framing: "this way stays nice and
    // generic").
    inline void LaunchSceneSanityScan(xecs::game_mgr::instance& GameMgr, xecs::scene::guid SceneGuid) noexcept
    {
        auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid);
        if (!pScene) return;

        {
            std::lock_guard<std::mutex> Lock(SanityScanMutex());
            if (!ScenesBeingScanned().insert(SceneGuid.m_Instance.m_Value).second) return; // already scanning this one
        }

        std::vector<xecs::scene::permanent_id> ActiveEntities;
        ActiveEntities.reserve(pScene->m_LocalToRuntime.size());
        for (auto& Pair : pScene->m_LocalToRuntime) ActiveEntities.push_back(Pair.first);

        std::string Description = std::format("{:016X}", SceneGuid.m_Instance.m_Value);
        if (auto Names = e29::commands::BuildAssetNameMap(xecs::scene::type_guid_v); true)
            if (auto It = Names.find(SceneGuid.m_Instance.m_Value); It != Names.end())
                Description = std::format("{} ({:016X})", It->second, SceneGuid.m_Instance.m_Value);

        const auto TaskId = BeginIdleTask(std::format("Scene Sanity Scan ({} entities)", ActiveEntities.size()), Description);

        // Submitted to the shared worker pool, not a raw detached thread - see this file's own top
        // comment. LOW priority/HEAVY complexity: a real disk walk that must never compete with actual
        // editor work (compiles, renders) for a worker slot.
        xscheduler::g_System.SubmitLambda( xscheduler::str_v<"IdleWork_SceneSanityScan">
        , [ProjectPath = GameMgr.m_SceneMgr.m_ProjectPath, SceneGuid, ActiveEntities = std::move(ActiveEntities), TaskId]() mutable noexcept
        {
            auto Result = ScanSceneConsistency(std::move(ProjectPath), SceneGuid, std::move(ActiveEntities));
            EndIdleTask(TaskId, Result.has_value() ? idle_task_status::Done : idle_task_status::Cancelled, Result.value_or(std::string{}));

            std::lock_guard<std::mutex> Lock(SanityScanMutex());
            ScenesBeingScanned().erase(SceneGuid.m_Instance.m_Value);
        }
        , xscheduler::complexity::HEAVY
        , xscheduler::priority::LOW
        );
    }

    // Scenes the AUTOMATIC idle trigger has already scanned this session - direct user correction: "the
    // job should not get rescheduled often... if we scan one time [why] do we need to keep rescanning?"
    // Right call - nothing about an unchanged scene's on-disk state can produce new findings between
    // one idle period and the next, so re-scanning it every single time the editor happens to go quiet
    // again just burns disk I/O for the same answer. Scoped to the AUTOMATIC path only (PumpIdleWork) -
    // "Run Now" and the CLI RunSanityCheck are explicit, deliberate requests and always run regardless,
    // same as clicking any other "do it now" button would.
    inline std::mutex&                       AutoScannedScenesMutex()       noexcept { static std::mutex M; return M; }
    inline std::unordered_set<std::uint64_t>& AutoScannedScenesThisSession() noexcept { static std::unordered_set<std::uint64_t> S; return S; }

    //---------------------------------------------------------------------------
    // Called once per frame (E29_LevelScene_Editor.cpp, grouped with the other per-frame pumps). A
    // no-op almost every frame - only does anything the ONE frame idle_threshold_seconds_v is first
    // crossed since the last real activity, then stays quiet again until NotifyActivity resets it.
    // Every open scene not yet auto-scanned this session gets its own scan - each independently guarded
    // against overlap too, so a scene still mid-scan from a previous idle period just gets skipped this
    // time, not queued twice.
    //---------------------------------------------------------------------------
    inline void PumpIdleWork(idle_work_state& IdleState, xecs::game_mgr::instance& GameMgr, editor_state& State) noexcept
    {
        if (IdleState.m_bTriggeredThisIdlePeriod) return;

        const double SecondsIdle = std::chrono::duration<double>(std::chrono::steady_clock::now() - IdleState.m_LastActivityTime).count();
        if (SecondsIdle < idle_threshold_seconds_v) return;

        IdleState.m_bTriggeredThisIdlePeriod = true;
        IdleWorkCancelRequested().store(false, std::memory_order_relaxed);
        for (auto& SceneGuid : State.m_OpenScenes)
        {
            {
                std::lock_guard<std::mutex> Lock(AutoScannedScenesMutex());
                if (!AutoScannedScenesThisSession().insert(SceneGuid.m_Instance.m_Value).second) continue;
            }
            LaunchSceneSanityScan(GameMgr, SceneGuid);
        }
    }

    //---------------------------------------------------------------------------
    // Dockable status panel - direct user request ("an idle-task window which shows all the idle work
    // in progress, done, priorities, etc... the window is a dockable window"). Same
    // SetNextWindowPos/Size(ImGuiCond_FirstUseEver) convention every other panel here uses - freely
    // dockable/rearrangeable afterward, this is purely a first-launch default. "Run Now" reuses the
    // exact same LaunchSceneSanityScan the idle trigger and the CLI command both call - one code path,
    // three ways to reach it.
    //---------------------------------------------------------------------------
    inline void RenderIdleWorkPanel(idle_work_state& IdleState, xecs::game_mgr::instance* pGameMgr, editor_state& State) noexcept
    {
        ImGui::SetNextWindowPos(ImVec2(505, 530), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Idle Work"))
        {
            const double SecondsIdle = std::chrono::duration<double>(std::chrono::steady_clock::now() - IdleState.m_LastActivityTime).count();
            if (SecondsIdle >= idle_threshold_seconds_v)
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Idle for %.0fs - background maintenance may run", SecondsIdle);
            else
                ImGui::TextDisabled("Active (%.0fs since last activity, idles at %.0fs)", SecondsIdle, idle_threshold_seconds_v);

            ImGui::SameLine(ImGui::GetWindowWidth() - 100.0f);
            ImGui::BeginDisabled(pGameMgr == nullptr || State.m_OpenScenes.empty());
            if (ImGui::Button("Run Now"))
            {
                IdleWorkCancelRequested().store(false, std::memory_order_relaxed);
                for (auto& SceneGuid : State.m_OpenScenes)
                    LaunchSceneSanityScan(*pGameMgr, SceneGuid);
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            // Fully generic - the panel just prints whatever each task recorded, with no scene (or
            // any other task-specific) knowledge baked in here at all. See idle_task_record's own
            // comment.
            if (ImGui::BeginTable("##IdleTasks", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Task");
                ImGui::TableSetupColumn("Description");
                ImGui::TableSetupColumn("Status");
                ImGui::TableSetupColumn("Duration");
                ImGui::TableSetupColumn("Info");
                ImGui::TableHeadersRow();

                std::lock_guard<std::mutex> Lock(IdleTaskRegistryMutex());
                auto& Registry = IdleTaskRegistry();
                for (auto It = Registry.rbegin(); It != Registry.rend(); ++It) // newest first
                {
                    auto& Rec = *It;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Rec.m_Name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    if (!Rec.m_Description.empty()) ImGui::TextUnformatted(Rec.m_Description.c_str());
                    else ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(2);
                    switch (Rec.m_Status)
                    {
                    case idle_task_status::Running:   ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "Running");   break;
                    case idle_task_status::Done:      ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Done");     break;
                    case idle_task_status::Cancelled: ImGui::TextColored(ImVec4(0.85f, 0.4f, 0.4f, 1.0f), "Cancelled"); break;
                    }
                    ImGui::TableSetColumnIndex(3);
                    const auto EndPoint = (Rec.m_Status == idle_task_status::Running) ? std::chrono::steady_clock::now() : Rec.m_EndTime;
                    ImGui::Text("%.1fs", std::chrono::duration<double>(EndPoint - Rec.m_StartTime).count());
                    ImGui::TableSetColumnIndex(4);
                    if (Rec.m_Status == idle_task_status::Done && !Rec.m_Info.empty()) ImGui::TextUnformatted(Rec.m_Info.c_str());
                    else ImGui::TextDisabled("-");
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
}

namespace e29::commands
{
    //================================================================================================
    // RunSanityCheck - manually kicks the same scene sanity scan Idle Work runs automatically,
    // bypassing the idle gate. For testing/verification (confirming the check itself still works
    // without waiting out idle_threshold_seconds_v) and for an AI/script that wants to ask for one on
    // demand rather than wait. Query, not Edit - purely diagnostic, never mutates scene content.
    //================================================================================================
    struct run_sanity_check_query_cmd : xundo::query_command_base
    {
        run_sanity_check_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "RunSanityCheck", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Manually runs the scene orphan/dangling sanity scan (normally idle-triggered) on every open scene, right now. Usage: RunSanityCheck"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            if (!e29::g_pGameMgr) return "RunSanityCheck: no game world";
            auto& State = get<e29_command_context>().m_State;
            if (State.m_OpenScenes.empty()) return "RunSanityCheck: no open scenes";

            e29::IdleWorkCancelRequested().store(false, std::memory_order_relaxed);
            for (auto& SceneGuid : State.m_OpenScenes)
                e29::LaunchSceneSanityScan(*e29::g_pGameMgr, SceneGuid);
            return std::format("RunSanityCheck: scanning {} scene(s) in the background - check the log shortly", State.m_OpenScenes.size());
        }
    };
}

#endif // E29_IDLE_WORK_H
