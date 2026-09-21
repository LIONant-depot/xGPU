#ifndef E10_SOURCE_CONTROL_CACHE_H
#define E10_SOURCE_CONTROL_CACHE_H
#pragma once

// Source-control status/lock cache - the CENTRALIZED, read-only-from-outside state that any view
// (Asset Tree badges, the E29 Source Control tab, or any future consumer) queries independently of
// each other. This file owns only DATA (sc::FileStatus/sc::LockInfo per path, refreshed wholesale by
// whoever actually talks to a provider) - it has no ImGui dependency and no knowledge of git, xundo,
// or E29, matching the same "library_mgr never holds a live device/texture, split CPU work from GPU
// upload" split this codebase already uses for the asset-mgr/icon-atlas boundary. It lives beside
// E10_AssetMgr.h specifically so it's reachable from every example, not just E29 - even though only
// E29 populates it today via a real git/LFS provider.
//
// Direct user correction (2026-09-17): an earlier draft let the new E29 Source Control tab reach
// into the Asset Tree's own files_tab code, and kept this cache E29-only. Both were wrong - "the
// asset window has its mission and is completely different to the source control window", and the
// real need was one centralized place every view queries on its own. This file is that place.
//
// sc::FileStatus/sc::LockInfo come from the xsource_control depot (sc_iworkspace_session.hpp): headless types with
// no editor or UI dependency, shared by every editor that shows source-control state.
#include "dependencies/xsource_control/source/sc_iworkspace_session.hpp"
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace e10::source_control
{
    // Lowercase, backslash-normalized - the same relative-path key convention
    // library_db::m_AssetDataBase already uses (E10_AssetMgr.h), not a new one invented here.
    inline std::wstring NormalizeKey(const std::filesystem::path& RelativePath) noexcept
    {
        std::wstring Key = RelativePath.wstring();
        for (auto& Ch : Key) if (Ch == L'/') Ch = L'\\';
        std::transform(Key.begin(), Key.end(), Key.begin(), [](wchar_t C) { return static_cast<wchar_t>(std::towlower(C)); });
        return Key;
    }

    //---------------------------------------------------------------------------
    // Status cache - one entry per library/workspace root path, replaced wholesale per scan.
    // Only ever holds files the provider reports as changed (staged/modified/untracked/conflicted) -
    // "not in the cache" correctly means "clean, as of the last refresh" (see GetCachedFileStatus).
    //---------------------------------------------------------------------------
    struct library_status_cache
    {
        std::unordered_map<std::wstring, sc::FileStatus> m_ByPath; // key: NormalizeKey(relative path)
        std::chrono::steady_clock::time_point             m_LastRefresh{};
    };

    inline std::mutex& StatusCacheMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_map<std::wstring, library_status_cache>& StatusCacheRegistry() noexcept
    {
        static std::unordered_map<std::wstring, library_status_cache> R;
        return R;
    }

    //---------------------------------------------------------------------------
    // Lock cache - same shape as the status cache, populated by the same scan. A "locked by you" vs
    // "locked by someone else" badge needs to know who holds a lock WITHOUT attempting to acquire
    // one (see sc::iworkspace_session::ListLocks's own comment for why PrepareEdit alone can't answer
    // this).
    //---------------------------------------------------------------------------
    struct library_lock_cache
    {
        std::unordered_map<std::wstring, sc::LockInfo> m_ByPath; // key: NormalizeKey(relative path)
        std::chrono::steady_clock::time_point           m_LastRefresh{};
    };

    inline std::mutex& LockCacheMutex() noexcept { static std::mutex M; return M; }
    inline std::unordered_map<std::wstring, library_lock_cache>& LockCacheRegistry() noexcept
    {
        static std::unordered_map<std::wstring, library_lock_cache> R;
        return R;
    }

    // Bumped once per completed publish - lets a UI consumer tell "the background scan produced new
    // data" apart from "nothing changed, don't bother re-reading" without polling cache contents every
    // frame. Direct user correction from Phase 3: "computing badges should be a job in the
    // scheduler... there is no reason to sync the FPS of the editor with the computation of the
    // badges." This is a single, global counter (not per-root) - every consumer already only cares
    // about "did ANY scan complete since I last looked", matching how files_tab's own revision-gated
    // badge refresh already uses it.
    inline std::atomic<std::uint64_t>& SourceControlRevision() noexcept { static std::atomic<std::uint64_t> R{ 0 }; return R; }

    // A key is "under" Prefix when it IS Prefix, or starts with Prefix followed by a path separator -
    // a plain rfind(Prefix,0)==0 would also match "assetsold\foo" against the prefix "assets", which
    // is wrong (two different top-level folders, one just happens to start with the other's name).
    inline bool KeyIsUnderPrefix(const std::wstring& Key, const std::wstring& Prefix) noexcept
    {
        if (Prefix.empty()) return true; // the CoveredPrefixes={L""} "matches everything" special case
        if (Key.size() < Prefix.size() || Key.compare(0, Prefix.size(), Prefix) != 0) return false;
        return Key.size() == Prefix.size() || Key[Prefix.size()] == L'\\';
    }

    // Merges a partial status scan into the cache, touching only the paths under CoveredPrefixes -
    // direct user request (2026-09-17): a large repo's status scan should be split into
    // independently-scheduled chunks (so high-priority folders can be scanned, published, and shown
    // to the user well before a slower full-repo scan finishes), and merging by prefix is what lets
    // one chunk's fresh result land without clobbering another chunk's still-valid one. An empty
    // CoveredPrefixes entry ("") matches every key (see KeyIsUnderPrefix), so a whole-repo scan is
    // just the CoveredPrefixes={L""} special case of this same function - one code path for both a
    // scoped chunk and a full replace. Every existing entry under a covered prefix is dropped first -
    // it's about to be fully replaced by FreshStatusByPath, which (by the same "absence means clean"
    // contract every caller already relies on) only lists files STILL reported as changed within that
    // scope.
    inline void PublishLibraryStatusChunk(const std::wstring& RootPath, const std::vector<std::wstring>& CoveredPrefixes
        , std::unordered_map<std::wstring, sc::FileStatus> FreshStatusByPath) noexcept
    {
        std::lock_guard<std::mutex> Lock(StatusCacheMutex());
        auto& Entry = StatusCacheRegistry()[RootPath];
        for (auto It = Entry.m_ByPath.begin(); It != Entry.m_ByPath.end(); )
        {
            const bool bInThisChunk = std::any_of(CoveredPrefixes.begin(), CoveredPrefixes.end()
                , [&](const std::wstring& Prefix) { return KeyIsUnderPrefix(It->first, Prefix); });
            It = bInThisChunk ? Entry.m_ByPath.erase(It) : std::next(It);
        }
        for (auto& [Key, Status] : FreshStatusByPath) Entry.m_ByPath[Key] = Status;
        Entry.m_LastRefresh = std::chrono::steady_clock::now();
        SourceControlRevision().fetch_add(1, std::memory_order_relaxed);
    }

    // Locks aren't foldered/chunked the way status is - ListLocks is already one cheap call for the
    // whole workspace (a server round trip, not a working-tree walk), so it stays a full replace.
    inline void PublishLibraryLocks(const std::wstring& RootPath, std::unordered_map<std::wstring, sc::LockInfo> LockByPath) noexcept
    {
        std::lock_guard<std::mutex> Lock(LockCacheMutex());
        auto& Entry = LockCacheRegistry()[RootPath];
        Entry.m_ByPath      = std::move(LockByPath);
        Entry.m_LastRefresh = std::chrono::steady_clock::now();
        SourceControlRevision().fetch_add(1, std::memory_order_relaxed);
    }

    // Updates (or clears) exactly ONE path's lock entry, instantly - no subprocess call, no waiting
    // on the 20s lock-refresh cooldown. Direct user report (2026-09-17): "I right click and selected
    // open for edit... the check mark did not change to the green lock" - Lock/Unlock/PrepareEdit
    // already KNOW the new lock state the instant they succeed (it's right there in their own
    // result), but nothing was telling the cache about it - the badge only ever changed after the
    // NEXT full scan, whenever that happened to be. The caller (E29's Lock/Unlock commands and the
    // PrepareEdit gate) already has the real LockInfo in hand; this just writes it in immediately.
    // Lock == std::nullopt means "no longer locked" (a successful Unlock).
    inline void PublishSingleLock(const std::wstring& RootPath, const std::wstring& RelativePath, std::optional<sc::LockInfo> Lock) noexcept
    {
        std::lock_guard<std::mutex> LockGuard(LockCacheMutex());
        auto& Entry = LockCacheRegistry()[RootPath];
        const auto Key = NormalizeKey(RelativePath);
        if (Lock) Entry.m_ByPath[Key] = *Lock;
        else      Entry.m_ByPath.erase(Key);
        SourceControlRevision().fetch_add(1, std::memory_order_relaxed);
    }

    // Read accessor for any consumer. std::nullopt means either "never scanned yet" or "clean as of
    // the last scan" - the caller can't tell those apart from this call alone; GetLastRefreshTime
    // (below) answers "has this root even been scanned" separately.
    inline std::optional<sc::FileStatus> GetCachedFileStatus(const std::wstring& RootPath, const std::filesystem::path& RelativePath) noexcept
    {
        std::lock_guard<std::mutex> Lock(StatusCacheMutex());
        auto It = StatusCacheRegistry().find(RootPath);
        if (It == StatusCacheRegistry().end()) return std::nullopt;
        auto File = It->second.m_ByPath.find(NormalizeKey(RelativePath));
        if (File == It->second.m_ByPath.end()) return std::nullopt;
        return File->second;
    }

    inline std::optional<sc::LockInfo> GetCachedLockStatus(const std::wstring& RootPath, const std::filesystem::path& RelativePath) noexcept
    {
        std::lock_guard<std::mutex> Lock(LockCacheMutex());
        auto It = LockCacheRegistry().find(RootPath);
        if (It == LockCacheRegistry().end()) return std::nullopt;
        auto Entry = It->second.m_ByPath.find(NormalizeKey(RelativePath));
        if (Entry == It->second.m_ByPath.end()) return std::nullopt;
        return Entry->second;
    }

    inline std::optional<std::chrono::steady_clock::time_point> GetLastRefreshTime(const std::wstring& RootPath) noexcept
    {
        std::lock_guard<std::mutex> Lock(StatusCacheMutex());
        auto It = StatusCacheRegistry().find(RootPath);
        if (It == StatusCacheRegistry().end()) return std::nullopt;
        return It->second.m_LastRefresh;
    }

    // One row per file that is actually pending (modified/untracked/conflicted) or locked - a plain
    // "clean and unlocked" file is never included, matching how the underlying caches never carry
    // clean entries either. This is the enumeration a whole-project "Pending Changes" view needs and
    // that, before this file, did not exist anywhere - every previous consumer took a single
    // "-Library" and read one cache entry, nothing aggregated it for UI consumption.
    struct pending_change_entry
    {
        std::wstring              m_RelativePath; // NormalizeKey'd
        sc::FileStatus             m_Status{};
        std::optional<sc::LockInfo> m_Lock;
    };

    inline std::vector<pending_change_entry> GetAllPendingChanges(const std::wstring& RootPath) noexcept
    {
        std::vector<pending_change_entry> Result;

        std::unordered_map<std::wstring, sc::LockInfo> LocksByPath;
        {
            std::lock_guard<std::mutex> Lock(LockCacheMutex());
            if (auto It = LockCacheRegistry().find(RootPath); It != LockCacheRegistry().end())
                LocksByPath = It->second.m_ByPath;
        }

        std::unordered_set<std::wstring> IncludedKeys;
        {
            std::lock_guard<std::mutex> Lock(StatusCacheMutex());
            if (auto It = StatusCacheRegistry().find(RootPath); It != StatusCacheRegistry().end())
            {
                Result.reserve(It->second.m_ByPath.size());
                IncludedKeys.reserve(It->second.m_ByPath.size());
                for (auto& [Key, Status] : It->second.m_ByPath)
                {
                    pending_change_entry Entry;
                    Entry.m_RelativePath = Key;
                    Entry.m_Status       = Status;
                    if (auto LockIt = LocksByPath.find(Key); LockIt != LocksByPath.end())
                        Entry.m_Lock = LockIt->second;
                    Result.push_back(std::move(Entry));
                    IncludedKeys.insert(Key);
                }
            }
        }

        // A file can be LOCKED without being locally modified (e.g. someone else holds the lock, or
        // you locked it but haven't edited yet) - those entries never show up in the status cache
        // above, so they're added here explicitly rather than silently dropped. Membership check via
        // the set built above, not a linear std::any_of scan of Result - this loop is over every lock
        // in the workspace, and re-scanning the whole Result vector per lock would make the total cost
        // scale with locks*pending-files rather than just locks+pending-files.
        for (auto& [Key, Lock] : LocksByPath)
        {
            if (IncludedKeys.contains(Key)) continue;

            pending_change_entry Entry;
            Entry.m_RelativePath   = Key;
            Entry.m_Status.path    = sc::WorkspacePath{ Key };
            Entry.m_Lock            = Lock;
            Result.push_back(std::move(Entry));
        }

        return Result;
    }

    // Every path currently pending (modified/untracked/conflicted/locked) under a given folder -
    // the primitive a whole-resource "SC Revert" needs: sc::RevertRequest only takes an explicit
    // file list (no directory pathspec), so a folder-scoped revert has to enumerate first. Reuses
    // GetAllPendingChanges + the same KeyIsUnderPrefix the chunked-scan publisher already relies on,
    // rather than inventing a second prefix-match rule.
    inline std::vector<std::wstring> GetPendingPathsUnderFolder(const std::wstring& RootPath, const std::wstring& FolderRelativePath) noexcept
    {
        const auto Prefix = NormalizeKey(FolderRelativePath);
        std::vector<std::wstring> Result;
        for (auto& Entry : GetAllPendingChanges(RootPath))
            if (KeyIsUnderPrefix(Entry.m_RelativePath, Prefix))
                Result.push_back(Entry.m_RelativePath);
        return Result;
    }
}

#endif // E10_SOURCE_CONTROL_CACHE_H
