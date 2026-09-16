#ifndef E29_SOURCE_CONTROL_STATUS_H
#define E29_SOURCE_CONTROL_STATUS_H
#pragma once

// Source Control ACTIVE side - the idle-triggered refresh that actually talks to a real git/LFS
// provider. Mirrors kit/E29_IdleWork.h's own LaunchSceneSanityScan shape exactly: a synchronous scan
// function that takes plain data only (never a live mgr&), dispatched through xscheduler::g_System
// rather than a raw thread, guarded against overlapping scans of the SAME library root. See
// source_control_abstraction_spec_v1_3.md, Part IV, "Status refresh -> Idle Work".
//
// The READ-SIDE cache (status/lock maps, SourceControlRevision, GetCachedFileStatus/GetCachedLockStatus)
// used to live in this file but was relocated to E10_SourceControlCache.h (beside E10_AssetMgr.h) -
// direct user correction (2026-09-17): that cache is pure data with zero E29 dependency, and needs
// to be the ONE centralized place every view (Asset Tree badges, the E29 Source Control tab) queries
// independently, not something owned by one example. This file now only PUBLISHES into it - it's the
// only file that knows about sc::iworkspace_session/GitLfsWorkspaceSession and the Idle Work system,
// so it stays E29-only.
//
// One GitLfsWorkspaceSession per open library, lazily created and Connect()'d on first use, kept
// alive for the life of the process - git/git-lfs subprocess calls are cheap enough per-call that
// there's no real teardown need before the app exits.
#include "source/Examples/E29_LevelSceneEditor/kit/E29_IdleWork.h"
#include "source/Examples/E29_LevelSceneEditor/plugins/source_control/sc_git_lfs_provider.hpp"
#include "source/Examples/E10_TextureResourcePipeline/E10_SourceControlCache.h"
#include <atomic>
#include <cwctype>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace e29::source_control
{
    // Lazily creates + Connects one GitLfsWorkspaceSession per library, keyed by the library's own
    // root path (e10::library_db::m_Library.m_Path) rather than by guid - the session only ever
    // needs the real filesystem root, and this is the same identity RunSanityCheck's own
    // ScanSceneConsistency already keys its own per-project work by (GameMgr.m_SceneMgr.m_ProjectPath).
    // A nullptr entry is a cached NEGATIVE result (not a git working tree) - remembered so a
    // non-git library isn't re-probed on every single idle period.
    inline std::mutex& WorkspaceRegistryMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_map<std::wstring, std::unique_ptr<sc::git_lfs::GitLfsWorkspaceSession>>& WorkspaceRegistry() noexcept
    {
        static std::unordered_map<std::wstring, std::unique_ptr<sc::git_lfs::GitLfsWorkspaceSession>> R;
        return R;
    }

    inline sc::git_lfs::GitLfsWorkspaceSession* GetOrCreateWorkspace(const std::wstring& RootPath) noexcept
    {
        std::lock_guard<std::mutex> Lock(WorkspaceRegistryMutex());
        auto& Registry = WorkspaceRegistry();
        if (auto It = Registry.find(RootPath); It != Registry.end()) return It->second.get();

        auto pSession = std::make_unique<sc::git_lfs::GitLfsWorkspaceSession>(std::filesystem::path(RootPath));
        const auto Err = pSession->Connect();
        if (Err && Err->code == sc::ErrorCode::WorkspaceNotFound)
        {
            Registry.emplace(RootPath, nullptr);
            return nullptr;
        }
        // Unsupported (git-lfs missing) is a soft warning per Connect()'s own comment - plain Git
        // operations still work, so the session is kept either way.
        auto* pRaw = pSession.get();
        Registry.emplace(RootPath, std::move(pSession));
        return pRaw;
    }

    // git-lfs's own lock listing (--verify or not) is a REAL NETWORK ROUND TRIP to the LFS server -
    // measured live against the real repo at ~1.1-1.5 SECONDS, vs. a plain local `git status` at
    // ~40ms for the WHOLE repo. Direct user question: "if you ask git for the pending list that
    // should be really fast... what are you asking git to do?" - this is the honest answer: local
    // status was never the slow part, this network call was, and it was being re-run on every single
    // scan cycle (every idle sweep, every folder navigation) even though lock ownership changes far
    // less often than that. Gated to once per LockRefreshCooldown per root - a caller that wants
    // fresh locks just asks; most calls are now a no-op instead of a guaranteed 1+ second stall.
    inline std::mutex& LockRefreshMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_map<std::wstring, std::chrono::steady_clock::time_point>& LastLockRefresh() noexcept
    {
        static std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> M;
        return M;
    }
    inline constexpr std::chrono::seconds LockRefreshCooldown_v{ 20 };

    inline bool ShouldRefreshLocks(const std::wstring& RootPath) noexcept
    {
        std::lock_guard<std::mutex> Lock(LockRefreshMutex());
        auto& Map = LastLockRefresh();
        const auto Now = std::chrono::steady_clock::now();
        if (auto It = Map.find(RootPath); It != Map.end() && (Now - It->second) < LockRefreshCooldown_v)
            return false;
        Map[RootPath] = Now;
        return true;
    }

    // Scans exactly Pathspecs (git pathspec strings, repo-root-relative; empty = whole repo) and
    // merges the result into the cache scoped to CoveredPrefixes (see PublishLibraryStatusChunk's own
    // comment for why merge-by-prefix is what makes splitting the scan into chunks safe). Returns the
    // number of changed files found, for the caller's own idle-task reporting. bIncludeLocks means
    // "refresh the lock list too, if it's not still fresh" - see ShouldRefreshLocks above; this is
    // the only place the actual network cost is paid, and only when it's actually due.
    inline std::size_t ScanLibraryStatusChunk(const std::wstring& RootPath, std::vector<std::string> Pathspecs
        , std::vector<std::wstring> CoveredPrefixes, bool bIncludeLocks) noexcept
    {
        auto* pWorkspace = GetOrCreateWorkspace(RootPath);
        if (!pWorkspace) return 0; // not a git working tree - nothing to report

        sc::StatusRequest Request;
        for (auto& P : Pathspecs) Request.paths.push_back(sc::WorkspacePath{ P });
        auto StatusResult = pWorkspace->GetStatus(Request);

        // Cull first, before this entry is even considered for the cache - direct user request ("the
        // expensive operations [should be] done after all possible culling is done"). A file git
        // reports with every change-flag false (only possible when Pathspecs names exact files
        // rather than folders, which this function's own two call sites never do) is dropped right
        // here rather than carried forward into a map insert and later read by every UI consumer.
        std::unordered_map<std::wstring, sc::FileStatus> StatusByPath;
        for (auto& File : StatusResult.files)
        {
            if (!(File.staged || File.modified || File.untracked || File.conflicted)) continue;
            StatusByPath[e10::source_control::NormalizeKey(File.path.relative)] = File;
        }
        const std::size_t Count = StatusByPath.size();
        e10::source_control::PublishLibraryStatusChunk(RootPath, CoveredPrefixes, std::move(StatusByPath));

        if (bIncludeLocks && ShouldRefreshLocks(RootPath))
        {
            auto LocksResult = pWorkspace->ListLocks(sc::ListLocksRequest{}); // nullopt path = every lock
            std::unordered_map<std::wstring, sc::LockInfo> LockByPath;
            for (auto& Lock : LocksResult.locks)
                LockByPath[e10::source_control::NormalizeKey(Lock.path.relative)] = Lock;
            e10::source_control::PublishLibraryLocks(RootPath, std::move(LockByPath));
        }

        return Count;
    }

    // Guarded per (root, chunk) rather than just per root - the "views" and "all" chunks for the SAME
    // root are meant to run concurrently (that's the whole point of splitting them), so the old
    // per-root-only guard would have made the second chunk's launch a no-op while the first was still
    // running. Same shape as E29_IdleWork.h's own ScenesBeingScanned otherwise - a real GetStatus call
    // is not instant, so a second idle period firing before a chunk's own scan finished must be
    // skipped, not queued on top of it.
    inline std::mutex& ScanningMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_set<std::wstring>& ChunksBeingScanned() noexcept { static std::unordered_set<std::wstring> S; return S; }

    // Whether ANY chunk, for any library, is currently scanning - lets a view show "still loading"
    // feedback (direct user request: an animated spinner next to "Pending Changes" - "otherwise the
    // user doesn't know what is going on") instead of silently presenting a possibly-incomplete list
    // as if it were final.
    inline bool IsScanInProgress() noexcept
    {
        std::lock_guard<std::mutex> Lock(ScanningMutex());
        return !ChunksBeingScanned().empty();
    }

    inline void LaunchSourceControlStatusScanChunk(std::wstring RootPath, std::wstring ChunkTag
        , std::vector<std::string> Pathspecs, std::vector<std::wstring> CoveredPrefixes, bool bIncludeLocks
        , xscheduler::priority Priority) noexcept
    {
        const std::wstring Key = RootPath + L"|" + ChunkTag;
        {
            std::lock_guard<std::mutex> Lock(ScanningMutex());
            if (!ChunksBeingScanned().insert(Key).second) return; // already scanning this chunk
        }

        // TEMP diagnostic (2026-09-17) - user reports never seeing the panel's loading spinner;
        // timestamps here let us confirm whether a scan is actually taking measurable wall-clock
        // time and whether the in-flight window is wide enough for a frame to observe it.
        const auto T0 = std::chrono::steady_clock::now();
        std::printf("[SC] scan START chunk=%ls\n", Key.c_str()); std::fflush(stdout);

        const auto TaskId = e29::BeginIdleTask("Source Control Status", xstrtool::To(RootPath) + " [" + xstrtool::To(ChunkTag) + "]");

        xscheduler::g_System.SubmitLambda( xscheduler::str_v<"IdleWork_SourceControlStatus">
        , [RootPath, Key, TaskId, T0, Pathspecs = std::move(Pathspecs), CoveredPrefixes = std::move(CoveredPrefixes), bIncludeLocks]() mutable noexcept
        {
            const auto Count = ScanLibraryStatusChunk(RootPath, std::move(Pathspecs), std::move(CoveredPrefixes), bIncludeLocks);
            e29::EndIdleTask(TaskId, e29::idle_task_status::Done, std::format("{} changed file(s)", Count));

            const auto Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
            std::printf("[SC] scan END   chunk=%ls (%.1f ms)\n", Key.c_str(), Ms); std::fflush(stdout);

            std::lock_guard<std::mutex> Lock(ScanningMutex());
            ChunksBeingScanned().erase(Key);
        }
        , xscheduler::complexity::HEAVY
        , Priority
        );
    }

    // Demand-driven: a view calls this (via assert_browser::m_OnFolderNavigated) when it needs a
    // SPECIFIC folder's status right now, rather than waiting for its turn in the idle-triggered
    // background sweep below. HIGH priority, scoped ONLY to the requested folder (recursively) -
    // direct user request (2026-09-17): "the priority should be based on what the views request...
    // the views should get almost instant answers." Also refreshes the lock list, same reasoning as
    // the background sweep's own root-level catch-all chunk (locks matter to whatever's on screen).
    inline void RequestPriorityScan(std::wstring RootPath, std::wstring RelativeFolderPath) noexcept
    {
        const auto Tag = e10::source_control::NormalizeKey(RelativeFolderPath);
        const std::string Pathspec = RelativeFolderPath.empty() ? "." : xstrtool::To(RelativeFolderPath);
        LaunchSourceControlStatusScanChunk(RootPath, L"priority:" + Tag
            , std::vector<std::string>{ Pathspec }, std::vector<std::wstring>{ Tag }, /*bIncludeLocks*/ true
            , xscheduler::priority::HIGH);
    }

    // Background sweep: ONE unscoped, LOW-priority job covering the whole repo. Direct user
    // correction (2026-09-17), backed by live measurement against the real repo: a plain unscoped
    // `git status` took ~40ms total, while an earlier version of this function split the SAME sweep
    // into 5 separate per-folder-scoped calls that each measured ~100-215ms (this file's own
    // diagnostic timestamps) - meaning the fixed per-invocation cost (process spawn + git's own
    // startup) dominates at this repo's size, and splitting into many small folder jobs when nothing
    // is specifically waiting on any one of them made the routine sweep slower, not faster, exactly
    // the concern raised: "instead of all small batches, they could add several folders to resolve
    // at once... no high priority request from certain folders... this is up to the system to be
    // smart about." Fragmenting into a fine-grained, single-folder job is reserved for
    // RequestPriorityScan (above), where a real view is actually waiting on exactly that folder and
    // jumping the queue is worth the extra process-spawn cost - the routine sweep has no such
    // deadline, so it stays the one call the underlying tool is already fast at.
    inline void LaunchSourceControlStatusScan(std::wstring RootPath) noexcept
    {
        LaunchSourceControlStatusScanChunk(RootPath, L"all"
            , std::vector<std::string>{}, std::vector<std::wstring>{ L"" }, /*bIncludeLocks*/ true
            , xscheduler::priority::LOW);
    }

    // Own idle-period gating, independent of PumpIdleWork's own m_bTriggeredThisIdlePeriod flag -
    // this file can't include kit/E29_IdleWork.h's own consumer back into it (E29_IdleWork.h is a
    // shared, no-plugin-dependency file by design), so PumpIdleWork's own trigger state can't be
    // reused directly. Instead this remembers the m_LastActivityTime value it last fired for and
    // compares against the CURRENT one: NotifyActivity only ever changes m_LastActivityTime on real
    // activity, so "the value changed since we last fired" is exactly "a new idle period began" -
    // read-only, no mutation of the shared idle_work_state, so call ordering relative to
    // PumpIdleWork itself doesn't matter.
    inline std::chrono::steady_clock::time_point& LastHandledActivityBaseline() noexcept
    {
        static std::chrono::steady_clock::time_point T{}; // epoch - "never fired yet"
        return T;
    }

    // Scans any library that has NEVER been scanned yet, immediately - independent of the 30-second
    // idle gate below. Direct user report (2026-09-17): "the system seems to wait to begin working...
    // only when I click the asset view and choose a folder is when it starts working. That should
    // not be the case." Before this fix, a freshly opened library got no status data at all until
    // EITHER 30s of true idle passed OR some view happened to request a specific folder - this fires
    // the moment a library is noticed, every frame, cheaply (GetLastRefreshTime is a map lookup, not
    // a scan - LaunchSourceControlStatusScan's own ChunksBeingScanned guard makes repeat calls for an
    // already-in-flight root a no-op). Once a library HAS been scanned once, this stops firing for
    // it and the idle-gated sweep below takes over for keeping it up to date.
    inline void ScanNewlyOpenedLibraries() noexcept
    {
        for (auto& Lib : e10::g_LibMgr.m_mLibraryDB)
        {
            const auto& RootPath = Lib.second->m_Library.m_Path;
            if (!e10::source_control::GetLastRefreshTime(RootPath).has_value())
                LaunchSourceControlStatusScan(RootPath);
        }
    }

    // Called once per frame, alongside PumpIdleWork (E29_LevelScene_Editor.cpp's own per-frame
    // pumps) - one status scan per currently open library, once per idle-period transition. Every
    // entry in e10::g_LibMgr.m_mLibraryDB is, by definition, open - no separate "which libraries are
    // open" tracking needed here, unlike PumpIdleWork's own State.m_OpenScenes (scenes are
    // opened/closed independently of their library).
    inline void PumpSourceControlIdleWork(const e29::idle_work_state& IdleState) noexcept
    {
        ScanNewlyOpenedLibraries();

        const double SecondsIdle = std::chrono::duration<double>(std::chrono::steady_clock::now() - IdleState.m_LastActivityTime).count();
        if (SecondsIdle < e29::idle_threshold_seconds_v) return;
        if (LastHandledActivityBaseline() == IdleState.m_LastActivityTime) return; // already handled this idle period

        LastHandledActivityBaseline() = IdleState.m_LastActivityTime;
        for (auto& Lib : e10::g_LibMgr.m_mLibraryDB)
            LaunchSourceControlStatusScan(Lib.second->m_Library.m_Path);
    }

    // NOTE: the read accessors that used to live here (GetCachedFileStatus/GetCachedLockStatus/
    // GetLastRefreshTime) moved to e10::source_control (E10_SourceControlCache.h) - callers should
    // use those directly; nothing in this file needs to re-export them.
}

#endif // E29_SOURCE_CONTROL_STATUS_H
