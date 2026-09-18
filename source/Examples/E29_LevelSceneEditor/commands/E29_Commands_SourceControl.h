#ifndef E29_COMMANDS_SOURCE_CONTROL_H
#define E29_COMMANDS_SOURCE_CONTROL_H
#pragma once

// Source Control command/undo layer - Phase 2 of the plugins/source_control/ plan (see
// source_control_abstraction_spec_v1_3.md, Part IV, "Mutating operations -> E29's command bus").
// Every command here is xundo::query_command_base, never xundo::command_base: Commit/Pull/Push are
// real round trips to a server that already has its own history (git's own commit graph/revert/
// reflog), and Lock/Unlock are server round trips too, not local-state edits - the exact same
// reasoning that kept EmptyTrashcan out of the undo system entirely (E29_Commands_AssetBrowser.h's
// own top comment). None of these belong in the local Undo/Redo history.
//
// Path arguments reuse the EXACT convention E29_Commands_AssetFiles.h already established for real
// (non-descriptor) paths: Base64-encoded, relative to the library root (EncodeAssetPath/
// DecodeAssetPath, same file) - real paths contain backslashes/spaces that would otherwise collide
// with the CLI's own token splitting.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetFiles.h" // EncodeAssetPath/DecodeAssetPath
#include "source/Examples/E29_LevelSceneEditor/plugins/source_control/E29_SourceControlStatus.h"

namespace e29::commands
{
    // Resolves a Library guid to its real on-disk root path - the one piece every command below
    // needs before it can reach a GitLfsWorkspaceSession. Empty return means the library isn't
    // currently open (same "not found" shape ListAssets/DescribeAsset already report for a bad
    // Library guid).
    inline std::wstring ResolveLibraryRootPath(e10::library::guid LibraryGuid) noexcept
    {
        std::wstring RootPath;
        e10::g_LibMgr.m_mLibraryDB.FindAsReadOnly(LibraryGuid, [&](const std::unique_ptr<e10::library_db>& Library)
        {
            RootPath = Library->m_Library.m_Path;
        });
        return RootPath;
    }

    // Issues ONE "SC Revert" for every currently-pending file under a library-relative folder -
    // shared tail end for every "revert this whole resource" call site (Level Tree's Level/Scene
    // rows, the Resources tab's per-tile "Resource Menu", the Assets tab's folder rows). A no-op if
    // nothing under FolderPath is actually pending (e.g. the confirm dialog was somehow reached on an
    // already-clean resource).
    inline void RunRevertUnderFolder(xundo::system& Undo, e10::library::guid LibraryGuid, const std::wstring& RootPath, const std::wstring& FolderPath) noexcept
    {
        const auto Paths = e10::source_control::GetPendingPathsUnderFolder(RootPath, FolderPath);
        if (Paths.empty()) return;
        std::wstring Joined;
        for (auto& P : Paths) { if (!Joined.empty()) Joined += L'\n'; Joined += P; }
        RunQuery(Undo, std::format("SourceControlRevert -Library {} -Paths {}", FormatLibraryGuid(LibraryGuid), EncodeAssetPath(Joined)));
    }

    // Convenience overload for "the resource's own guid, library already known" - resolves the
    // resource's containing ".desc" folder (info.txt/Descriptor.txt/dependencies.txt all live there)
    // via the SAME single-library getNodeInfo lookup idiom used throughout this codebase, then
    // forwards to RunRevertUnderFolder above. Unlike the Level Tree's own resolver (which has to
    // search every open library because it only ever has a bare full_guid), this overload is for
    // callers that already know which library owns the resource (e.g. the Asset Browser's own
    // per-library tabs) - no search needed.
    inline void RevertResourceWholeFolder(xundo::system& Undo, e10::library::guid LibraryGuid, xresource::full_guid ResourceGuid) noexcept
    {
        std::wstring FolderPath;
        const bool bFound = e10::g_LibMgr.getNodeInfo(LibraryGuid, ResourceGuid, [&](const e10::library_db::info_node& Node)
        {
            const auto SlashPos = Node.m_Path.find_last_of(L'\\');
            FolderPath = (SlashPos == std::wstring::npos) ? Node.m_Path : Node.m_Path.substr(0, SlashPos);
        });
        if (!bFound) return;

        const auto RootPath = ResolveLibraryRootPath(LibraryGuid);
        if (RootPath.empty()) return;

        e10::g_LibMgr.m_mLibraryDB.FindAsReadOnly(LibraryGuid, [&](const std::unique_ptr<e10::library_db>& Lib)
        {
            const auto& LibRoot = Lib->m_Library.m_Path;
            if (FolderPath.size() > LibRoot.size() && FolderPath.compare(0, LibRoot.size(), LibRoot) == 0)
            {
                FolderPath = FolderPath.substr(LibRoot.size());
                while (!FolderPath.empty() && (FolderPath.front() == L'\\' || FolderPath.front() == L'/'))
                    FolderPath.erase(FolderPath.begin());
            }
        });

        RunRevertUnderFolder(Undo, LibraryGuid, RootPath, FolderPath);
    }

    // Decodes a "-Paths" argument: Base64 of the real paths joined by '\n', same free-text-encoding
    // convention -Message already uses. Added in Phase 4A so a changelist with several files can
    // land as ONE real commit (SourceControlCommit) instead of one commit per file - the previous
    // -Path (singular) argument stays for simple single-file scripting/CLI use.
    inline std::vector<std::wstring> DecodeAssetPathList(const std::string& EncodedJoined) noexcept
    {
        const auto Joined = Base64Decode(EncodedJoined);
        std::vector<std::wstring> Paths;
        std::size_t Start = 0;
        while (Start <= Joined.size())
        {
            const auto End  = Joined.find('\n', Start);
            const auto Line = Joined.substr(Start, End == std::string::npos ? std::string::npos : End - Start);
            if (!Line.empty()) Paths.push_back(xstrtool::To(Line));
            if (End == std::string::npos) break;
            Start = End + 1;
        }
        return Paths;
    }

    // Shared by Stage/Commit/Revert: resolves whichever of -Path (singular) / -Paths (Base64,
    // '\n'-joined, Phase 4A) was actually supplied into one path list. Returns empty if neither
    // argument was given at all - the caller decides what that means (bad arguments).
    inline std::vector<sc::WorkspacePath> ResolveRequestPaths(xcmdline::parser& Parser, xcmdline::parser::handle hPath, xcmdline::parser::handle hPaths) noexcept
    {
        std::vector<sc::WorkspacePath> Result;
        if (auto PathsArg = Parser.getOptionArgAs<std::string>(hPaths, 0); !std::holds_alternative<xerr>(PathsArg))
        {
            for (auto& P : DecodeAssetPathList(std::get<std::string>(PathsArg)))
                Result.push_back(sc::WorkspacePath{ P });
        }
        else if (auto PathArg = Parser.getOptionArgAs<std::string>(hPath, 0); !std::holds_alternative<xerr>(PathArg))
        {
            Result.push_back(sc::WorkspacePath{ DecodeAssetPath(std::get<std::string>(PathArg)) });
        }
        return Result;
    }

    inline const char* ToString(sc::ErrorCode Code) noexcept
    {
        switch (Code)
        {
        case sc::ErrorCode::None:                   return "None";
        case sc::ErrorCode::InvalidArgument:        return "InvalidArgument";
        case sc::ErrorCode::WorkspaceNotFound:       return "WorkspaceNotFound";
        case sc::ErrorCode::PathNotFound:            return "PathNotFound";
        case sc::ErrorCode::LockedByOther:           return "LockedByOther";
        case sc::ErrorCode::AuthenticationRequired:  return "AuthenticationRequired";
        case sc::ErrorCode::Offline:                 return "Offline";
        case sc::ErrorCode::OutOfDate:                return "OutOfDate";
        case sc::ErrorCode::Conflict:                return "Conflict";
        case sc::ErrorCode::Unsupported:              return "Unsupported";
        case sc::ErrorCode::ProviderProtocolError:   return "ProviderProtocolError";
        case sc::ErrorCode::ProviderInternalError:   return "ProviderInternalError";
        }
        return "Unknown";
    }

    //================================================================================================
    // SourceControlStatus - dumps the CURRENT cache for a library, one line per changed file
    // ("path [staged][modified][untracked][conflicted] [LFS]"). Instant, reads memory only - never
    // triggers a scan itself, matches GetPlayState's own "just report current state" shape. Use
    // SourceControlRefresh first if the cache might be stale.
    //================================================================================================
    struct source_control_status_query_cmd : xundo::query_command_base
    {
        source_control_status_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlStatus", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the last-known changed files for a library, from the idle-refreshed cache (does not trigger a scan). Usage: SourceControlStatus -Library hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "SourceControlStatus: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlStatus: library not open";

            const auto LastRefresh = e10::source_control::GetLastRefreshTime(RootPath);
            if (!LastRefresh) return "SourceControlStatus: never refreshed - run SourceControlRefresh first";

            std::string Out = std::format("(as of {:.0f}s ago)\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - *LastRefresh).count());

            std::lock_guard<std::mutex> Lock(e10::source_control::StatusCacheMutex());
            auto& Cache = e10::source_control::StatusCacheRegistry()[RootPath];
            for (auto& [Key, File] : Cache.m_ByPath)
            {
                Out += std::format("{}  {}{}{}{}{}\n",
                    File.path.relative.string(),
                    File.staged     ? "[staged]"     : "",
                    File.modified   ? "[modified]"   : "",
                    File.untracked  ? "[untracked]"  : "",
                    File.conflicted ? "[conflicted]" : "",
                    File.lfsTracked ? " (LFS)"        : "");
            }
            if (Cache.m_ByPath.empty()) Out += "(clean)\n";
            return Out;
        }

        xcmdline::parser::handle m_hLibrary;
    };

    //================================================================================================
    // SourceControlDepotStatus - reports a library's CACHED depot identity plus the live validation
    // outcome from the last time it was actually checked (Phase B, "Multi-library project model" plan
    // section) - "the proper warning to the User and AI... should be made" (direct user requirement),
    // so a CLI/AI session sees exactly what a human would in the UI, no separate discovery path.
    // Instant, reads memory only - the check itself runs once per library (GetOrCreateWorkspace's own
    // first-connect path); this command never re-triggers it.
    //================================================================================================
    struct source_control_depot_status_query_cmd : xundo::query_command_base
    {
        source_control_depot_status_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlDepotStatus", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Reports a library's cached depot identity and the last live validation outcome (Confirmed/Mismatch/NoProvider/Unknown). Usage: SourceControlDepotStatus -Library hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "SourceControlDepotStatus: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));

            std::string Out;
            const bool bFound = e10::g_LibMgr.m_mLibraryDB.FindAsReadOnly(LibraryGuid, [&](const std::unique_ptr<e10::library_db>& DB)
            {
                const char* StateStr = "Unknown";
                switch (DB->m_DepotLinkState)
                {
                    case e10::library_db::depot_link_state::Confirmed:  StateStr = "Confirmed";  break;
                    case e10::library_db::depot_link_state::Mismatch:   StateStr = "Mismatch";    break;
                    case e10::library_db::depot_link_state::NoProvider: StateStr = "NoProvider";  break;
                    default: break;
                }

                Out = std::format("Provider: {}\nRepositoryId: {}\nState: {}\n"
                    , DB->m_Library.m_DepotProviderId.empty() ? "(never cached)" : DB->m_Library.m_DepotProviderId
                    , DB->m_Library.m_DepotRepositoryId.empty() ? "(none)" : DB->m_Library.m_DepotRepositoryId
                    , StateStr);
                if (!DB->m_DepotLinkDetail.empty())
                    Out += std::format("Detail: {}\n", DB->m_DepotLinkDetail);
            });
            if (!bFound) return "SourceControlDepotStatus: library not open";
            return Out;
        }

        xcmdline::parser::handle m_hLibrary;
    };

    //================================================================================================
    // SourceControlRefresh - manually kicks an immediate status scan, bypassing the idle gate. Same
    // "for testing/verification, and for an AI/script that wants one on demand" reasoning
    // RunSanityCheck was built for (E29_IdleWork.h).
    //================================================================================================
    struct source_control_refresh_query_cmd : xundo::query_command_base
    {
        source_control_refresh_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlRefresh", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Manually refreshes the source-control status cache for a library, right now. Usage: SourceControlRefresh -Library hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "SourceControlRefresh: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlRefresh: library not open";

            e29::source_control::LaunchSourceControlStatusScan(RootPath);
            return "SourceControlRefresh: scanning in the background - check SourceControlStatus shortly";
        }

        xcmdline::parser::handle m_hLibrary;
    };

    //================================================================================================
    // SourceControlListLocks - dumps the CURRENT lock cache for a library, one line per locked path
    // ("path owner=... ownership=..."). Diagnostic/discovery command, added while debugging why the
    // Asset Tree's own lock badge wasn't appearing for a real, confirmed-live lock - reads the exact
    // same cache the badge hook reads, so a mismatch between this and `git lfs locks` output points
    // straight at the scan/cache layer; a mismatch between this and what the UI shows points at the
    // render/path-matching layer instead.
    //================================================================================================
    struct source_control_list_locks_query_cmd : xundo::query_command_base
    {
        source_control_list_locks_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlListLocks", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the last-known locks for a library, from the idle-refreshed cache. Usage: SourceControlListLocks -Library hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "SourceControlListLocks: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlListLocks: library not open";

            std::lock_guard<std::mutex> Lock(e10::source_control::LockCacheMutex());
            auto& Cache = e10::source_control::LockCacheRegistry()[RootPath];
            if (Cache.m_ByPath.empty()) return "(no locks in cache)";

            std::string Out;
            for (auto& [Key, LockInfo] : Cache.m_ByPath)
            {
                Out += std::format("key=\"{}\"  path=\"{}\"  owner={}  ownership={}\n",
                    xstrtool::To(Key), LockInfo.path.relative.string(), LockInfo.ownerDisplayName,
                    LockInfo.ownership == sc::LockOwnership::CurrentUser ? "CurrentUser" :
                    LockInfo.ownership == sc::LockOwnership::OtherUser   ? "OtherUser"   : "Unknown");
            }
            return Out;
        }

        xcmdline::parser::handle m_hLibrary;
    };

    //================================================================================================
    // SourceControlLock - PrepareEdit with LockRequirement::Require (unless -Try 1 is passed, which
    // downgrades to Try - proceed-with-a-warning if the file isn't lockable/no lock is available).
    // Runs synchronously (one `git lfs lock` subprocess call) - short enough to answer inline,
    // matching SaveAssets' own "quick enough to just do it now" shape rather than going through Idle
    // Work, which is reserved for genuinely slow, poll-shaped work.
    //================================================================================================
    struct source_control_lock_query_cmd : xundo::query_command_base
    {
        source_control_lock_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlLock", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Locks a file before editing (required for binary/LFS-tracked assets). Usage: SourceControlLock -Library hexguid -Path base64 [-Try 1]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",                         true,  1);
            m_hPath    = m_Parser.addOption("Path",    "Path relative to the library root, Base64",                    true,  1);
            m_hTry     = m_Parser.addOption("Try",     "Pass 1 to proceed even if the lock can't be acquired",         false, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto PathArg    = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(PathArg))
                return "SourceControlLock: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlLock: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlLock: not a git working tree";

            const auto Path = DecodeAssetPath(std::get<std::string>(PathArg));

            sc::PrepareEditRequest Request;
            Request.paths = { sc::WorkspacePath{ Path } };
            Request.policy.lockRequirement = IsForced(m_Parser, m_hTry) ? sc::LockRequirement::Try : sc::LockRequirement::Require;

            const auto Result = pWorkspace->PrepareEdit(Request);
            if (Result.files.empty()) return "SourceControlLock: no result";
            const auto& File = Result.files.front();
            if (!File.OperationSucceeded())
                return std::format("SourceControlLock: [{}] {}", ToString(File.error->code), File.error->message);

            // "Proceeding without a lock" is specifically the Try-and-couldn't-get-one wording - wrong
            // for the "already locked by you" case (real bug found live, 2026-09-17: PrepareEdit
            // correctly reports success here via LocalIntentRecorded, not LockAcquired, since nothing
            // NEW was acquired - but this message used to collapse both cases into the same misleading
            // text even though the user does, in fact, hold a lock).
            std::string Out = sc::HasFlag(File.actions, sc::EditActionFlags::LockAcquired) ? "Locked"
                : (File.coordination.lock.has_value() ? "Already locked (by you)" : "Proceeding without a lock");
            for (auto& W : File.warnings) Out += std::format(" ({})", W);

            // Real bug found live (2026-09-17): "I right click and selected open for edit... the check
            // mark did not change to the green lock" - the lock badge only ever refreshed on the NEXT
            // full scan. We already know the real new lock state right here - write it into the cache
            // immediately instead of waiting.
            if (File.coordination.lock) e10::source_control::PublishSingleLock(RootPath, Path, File.coordination.lock);

            return Out;
        }

        xcmdline::parser::handle m_hLibrary, m_hPath, m_hTry;
    };

    //================================================================================================
    // SourceControlUnlock - standalone lock release (sc::iworkspace_session::Unlock, added during
    // this phase - see spec's Part II amendment). -Force 1 maps to `git lfs unlock --force`, which
    // covers TWO distinct real cases, not just one - confirmed live against the real repo's GitHub
    // remote: (1) an administrator breaking a lock held by someone else, AND (2) the much more
    // common case of releasing YOUR OWN lock on a file you've edited but not yet committed - plain
    // `git lfs unlock` refuses that outright ("uncommitted changes") unless forced. Without -Force,
    // this command fails with a specific, actionable message for case (2) rather than a generic
    // error - see ClassifyLfsLockFailure's own comment in sc_git_lfs_provider.hpp.
    //================================================================================================
    struct source_control_unlock_query_cmd : xundo::query_command_base
    {
        source_control_unlock_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlUnlock", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Releases a lock. Usage: SourceControlUnlock -Library hexguid -Path base64 [-Force 1]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",  true,  1);
            m_hPath    = m_Parser.addOption("Path",    "Path relative to the library root, Base64", true,  1);
            m_hForce   = m_Parser.addOption("Force",   "Pass 1 to unlock anyway - needed both to break someone else's lock AND to release your own lock on a file with uncommitted changes", false, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto PathArg    = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(PathArg))
                return "SourceControlUnlock: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlUnlock: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlUnlock: not a git working tree";

            const auto Path = DecodeAssetPath(std::get<std::string>(PathArg));

            sc::UnlockRequest Request;
            Request.paths = { sc::WorkspacePath{ Path } };
            Request.force = IsForced(m_Parser, m_hForce);

            const auto Result = pWorkspace->Unlock(Request);
            if (Result.items.empty()) return "SourceControlUnlock: no result";
            const auto& Item = Result.items.front();
            if (!Item.Succeeded())
                return std::format("SourceControlUnlock: [{}] {}", ToString(Item.error->code), Item.error->message);

            // Same immediate-cache-update fix as SourceControlLock - a successful Unlock means the
            // cache's own entry for this path is now stale (still shows locked) until the next scan,
            // which nothing here should have to wait for.
            e10::source_control::PublishSingleLock(RootPath, Path, std::nullopt);

            return "Unlocked";
        }

        xcmdline::parser::handle m_hLibrary, m_hPath, m_hForce;
    };

    //================================================================================================
    // SourceControlRevert - "Undo Changes": discards real local edits via
    // sc::iworkspace_session::Revert (`git checkout --`, see the provider's own top comment - this
    // does NOT undo a newly-added/untracked file, only a modification to a tracked one). Added in
    // Phase 4A - the interface method existed since Phase 1 but no command wrapped it until the
    // Source Control tab's context menu needed a real "Undo Changes" action. query_command_base, not
    // undo-routed: this is real and destructive to local edits, the same reasoning as EmptyTrashcan -
    // callers (UI) are expected to confirm with the user before running this, not rely on Ctrl+Z.
    //================================================================================================
    struct source_control_revert_query_cmd : xundo::query_command_base
    {
        source_control_revert_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlRevert", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Discards local edits to one or more tracked files (git checkout --). Does not remove a new/untracked file. Usage: SourceControlRevert -Library hexguid (-Path base64 | -Paths base64-of-newline-joined-paths)"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",  true,  1);
            m_hPath    = m_Parser.addOption("Path",    "Path relative to the library root, Base64", false, 1);
            m_hPaths   = m_Parser.addOption("Paths",   "Several paths, Base64 of the paths joined by '\\n'", false, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg))
                return "SourceControlRevert: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlRevert: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlRevert: not a git working tree";

            sc::RevertRequest Request;
            Request.paths = ResolveRequestPaths(m_Parser, m_hPath, m_hPaths);
            if (Request.paths.empty()) return "SourceControlRevert: bad arguments - need -Path or -Paths";

            const auto Result = pWorkspace->Revert(Request);
            if (Result.AllSucceeded()) return std::format("Reverted {} file(s)", Result.items.size());

            std::string Out = "SourceControlRevert: one or more files failed:";
            for (auto& Item : Result.items)
                if (!Item.Succeeded())
                    Out += std::format("\n  [{}] {}", ToString(Item.error->code), Item.error->message);
            return Out;
        }

        xcmdline::parser::handle m_hLibrary, m_hPath, m_hPaths;
    };

    //================================================================================================
    // SourceControlStage - git add, no commit. Rarely needed on its own (Commit below stages what
    // it commits) but exposed for parity with the underlying Add() primitive and for a UI that wants
    // to show "staged" state before the user decides on a commit message.
    //================================================================================================
    struct source_control_stage_query_cmd : xundo::query_command_base
    {
        source_control_stage_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlStage", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Stages one or more files (git add), without committing. Usage: SourceControlStage -Library hexguid (-Path base64 | -Paths base64-of-newline-joined-paths)"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",  true, 1);
            m_hPath    = m_Parser.addOption("Path",    "Path relative to the library root, Base64", false, 1);
            m_hPaths   = m_Parser.addOption("Paths",   "Several paths, Base64 of the paths joined by '\\n'", false, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg))
                return "SourceControlStage: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlStage: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlStage: not a git working tree";

            sc::AddRequest Request;
            Request.paths = ResolveRequestPaths(m_Parser, m_hPath, m_hPaths);
            if (Request.paths.empty()) return "SourceControlStage: bad arguments - need -Path or -Paths";

            const auto Result = pWorkspace->Add(Request);
            return Result.AllSucceeded() ? "Staged" : "SourceControlStage: failed";
        }

        xcmdline::parser::handle m_hLibrary, m_hPath, m_hPaths;
    };

    //================================================================================================
    // SourceControlCommit - stage -> commit (pathspec-scoped) -> push -> release session-owned
    // locks, all via sc::iworkspace_session::Submit. This is genuinely "commit AND push" in one step
    // (Submit's own design, see sc_git_lfs_provider.hpp's top comment) - if the push is rejected
    // because the remote has newer commits, the result says so and names SourceControlPull as the
    // next step, matching the task's own "pushing involves pulling first" workflow: the recovery
    // guidance IS the pull-first instruction, surfaced from the one place that actually knows push
    // failed for that reason.
    //================================================================================================
    struct source_control_commit_query_cmd : xundo::query_command_base
    {
        source_control_commit_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlCommit", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Stages, commits (all given paths as ONE commit), and pushes, releasing any lock this session holds on the given paths. Usage: SourceControlCommit -Library hexguid (-Path base64 | -Paths base64-of-newline-joined-paths) -Message base64 [-KeepLocks 1]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary   = m_Parser.addOption("Library",   "Library instance guid, 16 hex digits",         true,  1);
            m_hPath      = m_Parser.addOption("Path",      "Path relative to the library root, Base64",    false, 1);
            m_hPaths     = m_Parser.addOption("Paths",     "Several paths, Base64 of the paths joined by '\\n' - committed together as ONE commit", false, 1);
            m_hMessage   = m_Parser.addOption("Message",   "Commit message, Base64-encoded",                true,  1);
            m_hKeepLocks = m_Parser.addOption("KeepLocks", "Pass 1 to keep the lock after committing",      false, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto MessageArg = m_Parser.getOptionArgAs<std::string>(m_hMessage, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(MessageArg))
                return "SourceControlCommit: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlCommit: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlCommit: not a git working tree";

            const auto Message = Base64Decode(std::get<std::string>(MessageArg));

            sc::SubmitRequest Request;
            Request.paths = ResolveRequestPaths(m_Parser, m_hPath, m_hPaths);
            if (Request.paths.empty()) return "SourceControlCommit: bad arguments - need -Path or -Paths";
            Request.description = Message;
            Request.keepLocks   = IsForced(m_Parser, m_hKeepLocks);

            const auto Result = pWorkspace->Submit(Request);
            std::string Out = std::format("Outcome: {}",
                Result.Summary() == sc::SubmitOutcome::Published        ? "Published"
              : Result.Summary() == sc::SubmitOutcome::CompletedLocally ? "CompletedLocally (not pushed)"
              : Result.Summary() == sc::SubmitOutcome::RemoteStateUnknown ? "RemoteStateUnknown"
              :                                                           "Failed");
            if (Result.localRevision) Out += std::format(", local {}", *Result.localRevision);
            if (Result.remoteRevision) Out += std::format(", remote {}", *Result.remoteRevision);
            for (auto& W : Result.warnings) Out += std::format("\n  {}", W);
            return Out;
        }

        xcmdline::parser::handle m_hLibrary, m_hPath, m_hPaths, m_hMessage, m_hKeepLocks;
    };

    //================================================================================================
    // SourceControlPull - Sync(fastForwardOnly=true). The explicit "pull first" step named by
    // SourceControlCommit's own recovery message when a push is rejected, or a proactive check
    // before starting new work. Merge conflicts are surfaced in the result, not auto-resolved - the
    // task's own "let the user deal with any merges" requirement; this command never runs `git
    // merge`/`git rebase` on its own behalf.
    //================================================================================================
    struct source_control_pull_query_cmd : xundo::query_command_base
    {
        source_control_pull_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlPull", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Pulls from the remote (fast-forward only - conflicts are reported, never auto-resolved). Usage: SourceControlPull -Library hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "SourceControlPull: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlPull: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlPull: not a git working tree";

            const auto Result = pWorkspace->Sync(sc::SyncRequest{});
            if (Result.succeeded) return std::format("Pulled{}", Result.summary.empty() ? "" : (": " + Result.summary));
            return std::format("SourceControlPull: [{}] {}", ToString(Result.error->code), Result.error->message);
        }

        xcmdline::parser::handle m_hLibrary;
    };

    //================================================================================================
    // SourceControlPush - pushes whatever is already committed locally, without staging or
    // committing anything new (sc::iworkspace_session::Push, added during this phase). The explicit
    // "officially push" step once SourceControlPull resolved whatever blocked an earlier
    // SourceControlCommit's own push.
    //================================================================================================
    struct source_control_push_query_cmd : xundo::query_command_base
    {
        source_control_push_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SourceControlPush", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Pushes locally committed changes to the remote. Usage: SourceControlPush -Library hexguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "SourceControlPush: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto RootPath    = ResolveLibraryRootPath(LibraryGuid);
            if (RootPath.empty()) return "SourceControlPush: library not open";

            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);
            if (!pWorkspace) return "SourceControlPush: not a git working tree";

            const auto Result = pWorkspace->Push(sc::PushRequest{});
            if (Result.succeeded) return std::format("Pushed{}", Result.summary.empty() ? "" : (": " + Result.summary));
            return std::format("SourceControlPush: [{}] {}", ToString(Result.error->code), Result.error->message);
        }

        xcmdline::parser::handle m_hLibrary;
    };
}

#endif // E29_COMMANDS_SOURCE_CONTROL_H
