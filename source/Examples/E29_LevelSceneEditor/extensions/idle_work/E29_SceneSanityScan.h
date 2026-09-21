#ifndef E29_SCENE_SANITY_SCAN_H
#define E29_SCENE_SANITY_SCAN_H
#pragma once

// The scene consistency check, run as an idle task (xeditor/idle_work.h): compares each open scene's descriptor with
// the entity files on disk and reports orphans (a file no descriptor lists) and dangling references (a listed entity
// whose file is missing). At a million entities this is a long disk walk, so it only starts once the editor has been
// quiet for a while, on xscheduler at LOW priority, and stops as soon as the user is back.
//
// Included after E29_CommandContext.h (query_command_base for RunSanityCheck) and E29_EditorState.h (level_context).
#include "plugins/xlevel.plugin/source/Editor/xlevel_command_context.h"
#include "dependencies/xeditor/include/xeditor/idle_work.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

namespace e29
{
    // A scene is never scanned twice at once: a scan slower than the gap between two idle periods would only waste I/O.
    inline std::mutex& SanityScanMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_set<std::uint64_t>& ScenesBeingScanned() noexcept { static std::unordered_set<std::uint64_t> S; return S; }

    // The idle trigger scans each scene once per session: an unchanged scene cannot produce new findings, so re-scanning it
    // every quiet period would repeat the same answer. Run Now and RunSanityCheck always scan.
    inline std::mutex&                        AutoScannedScenesMutex()       noexcept { static std::mutex M; return M; }
    inline std::unordered_set<std::uint64_t>& AutoScannedScenesThisSession() noexcept { static std::unordered_set<std::uint64_t> S; return S; }

    // Takes plain data only, never a manager: a background scan must not hold a reference into a world that a Stop or
    // a Game.dll reload can destroy under it. Returns the summary ("clean", "1 orphan", ...), or nullopt when the user's
    // return cancelled it - a half-finished listing would make present files look missing, so a cancelled scan reports
    // nothing and simply runs again next idle period.
    inline std::optional<std::string> ScanSceneConsistency(std::wstring ProjectPath, xecs::scene::guid SceneGuid, std::vector<xecs::scene::permanent_id> ActiveEntities) noexcept
    {
        const auto OnDisk = xecs::scene::details::DiscoverEntityIds(ProjectPath, SceneGuid, &xeditor::IdleWorkCancelRequested());
        if (xeditor::IdleWorkCancelRequested().load(std::memory_order_relaxed)) return std::nullopt;

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

    // Starts a background scan of one scene, unless it is already being scanned. The active entities come from the live
    // scene (what is active right now), and the panel's description is resolved once here so the panel stays generic.
    inline void LaunchSceneSanityScan(xecs::game_mgr::instance& GameMgr, xecs::scene::guid SceneGuid) noexcept
    {
        auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid);
        if (!pScene) return;

        {
            std::lock_guard<std::mutex> Lock(SanityScanMutex());
            if (!ScenesBeingScanned().insert(SceneGuid.m_Instance.m_Value).second) return;
        }

        std::vector<xecs::scene::permanent_id> ActiveEntities;
        ActiveEntities.reserve(pScene->m_LocalToRuntime.size());
        for (auto& Pair : pScene->m_LocalToRuntime) ActiveEntities.push_back(Pair.first);

        std::string Description = std::format("{:016X}", SceneGuid.m_Instance.m_Value);
        if (auto Names = xlevel::commands::BuildAssetNameMap(xecs::scene::type_guid_v); true)
            if (auto It = Names.find(SceneGuid.m_Instance.m_Value); It != Names.end())
                Description = std::format("{} ({:016X})", It->second, SceneGuid.m_Instance.m_Value);

        const auto TaskId = xeditor::BeginIdleTask(std::format("Scene Sanity Scan ({} entities)", ActiveEntities.size()), Description);

        xscheduler::g_System.SubmitLambda( xscheduler::str_v<"IdleWork_SceneSanityScan">
        , [ProjectPath = GameMgr.m_SceneMgr.m_ProjectPath, SceneGuid, ActiveEntities = std::move(ActiveEntities), TaskId]() mutable noexcept
        {
            auto Result = ScanSceneConsistency(std::move(ProjectPath), SceneGuid, std::move(ActiveEntities));
            xeditor::EndIdleTask(TaskId, Result.has_value() ? xeditor::idle_task_status::Done : xeditor::idle_task_status::Cancelled, Result.value_or(std::string{}));

            std::lock_guard<std::mutex> Lock(SanityScanMutex());
            ScenesBeingScanned().erase(SceneGuid.m_Instance.m_Value);
        }
        , xscheduler::complexity::HEAVY
        , xscheduler::priority::LOW
        );
    }

    // This editor's subscription to xeditor::idle_work::m_OnRun.
    struct scene_sanity_scanner
    {
        xlevel::level_context& m_Ed;

        void Run(bool bManual) noexcept
        {
            if (!m_Ed.m_pWorld) return;
            for (auto& SceneGuid : m_Ed.m_State.m_OpenScenes)
            {
                if (!bManual)
                {
                    std::lock_guard<std::mutex> Lock(AutoScannedScenesMutex());
                    if (!AutoScannedScenesThisSession().insert(SceneGuid.m_Instance.m_Value).second) continue;
                }
                LaunchSceneSanityScan(m_Ed.World(), SceneGuid);
            }
        }
    };
}

namespace e29::commands
{
    // RunSanityCheck - the same scan, on demand: for testing it without waiting out the idle threshold, and for an AI/script.
    struct run_sanity_check_query_cmd : xlevel::commands::level_query_command
    {
        run_sanity_check_query_cmd(xundo::system& System, void* pDataBase) noexcept : xlevel::commands::level_query_command(System, "RunSanityCheck", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Manually runs the scene orphan/dangling sanity scan (normally idle-triggered) on every open scene, right now. Usage: RunSanityCheck"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            auto& OpenScenes = State().m_OpenScenes;
            if (OpenScenes.empty()) return "RunSanityCheck: no open scenes";

            xeditor::IdleWorkCancelRequested().store(false, std::memory_order_relaxed);
            for (auto& SceneGuid : OpenScenes)
                e29::LaunchSceneSanityScan(World(), SceneGuid);
            return std::format("RunSanityCheck: scanning {} scene(s) in the background - check the log shortly", OpenScenes.size());
        }
    };

    // GetIdleTasks - the Idle Work panel's table as text, newest first.
    struct get_idle_tasks_query_cmd : xlevel::commands::level_query_command
    {
        get_idle_tasks_query_cmd(xundo::system& System, void* pDataBase) noexcept : xlevel::commands::level_query_command(System, "GetIdleTasks", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the idle-work tasks (running and finished), newest first. Usage: GetIdleTasks"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            constexpr const char* Status[] = { "Running", "Done", "Cancelled" };
            std::lock_guard<std::mutex> Lock(xeditor::IdleTaskRegistryMutex());
            std::string Out;
            for (auto It = xeditor::IdleTaskRegistry().rbegin(); It != xeditor::IdleTaskRegistry().rend(); ++It)
                Out += std::format("{:<9} {} | {} | {}\n", Status[static_cast<int>(It->m_Status)], It->m_Name, It->m_Description, It->m_Info);
            return Out.empty() ? "no idle tasks" : Out;
        }
    };
}

#endif // E29_SCENE_SANITY_SCAN_H
