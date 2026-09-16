// sc_git_lfs_provider.hpp
//
// A real, CLI-driven Git + Git-LFS provider implementing sc::iworkspace_session
// (see sc_iworkspace_session.hpp and source_control_abstraction_spec_v1_3.md,
// same directory). Ported from the original standalone `sc::git_lfs` draft:
// its own local type definitions (WorkspacePath, Error, LockInfo,
// CoordinationState, SessionCapabilities, etc.) are gone -- this file now
// uses the canonical `sc::` types the interface itself defines, so a future
// Perforce/SVN/Lore provider can be reviewed against exactly the same
// request/result shapes.
//
// Fix history carried over from the original draft (all still applied):
//   1. Submit never risks committing unrelated staged files -- `git commit
//      -m <desc> -- <paths>` restricts the commit to only the requested
//      paths regardless of what else is staged.
//   2. GetStatus parses `git status --porcelain=v1 -z --untracked-files=all`,
//      consuming the extra "from" path record for renames/copies, instead
//      of assuming a fixed column in human-oriented output.
//   3. LFS lock failures are classified by ClassifyLfsLockFailure() instead
//      of being uniformly reported as LockedByOther.
//   4. PrepareEdit never claims MadeWritable without verifying/setting the
//      permission, and never reports LocalIntentRecorded unless something
//      was actually recorded in session state.
//   5. Lock release only attempts locks THIS session acquired
//      (acquiredLocks_); unlock failures are warnings and correctly
//      prevent SubmitPhaseFlags::LocksReleased without touching Published.
//   6. Workspace paths are validated (relative, no `..` segments) before
//      being used in any git invocation.
//   7. PrepareEditPolicy is a real tri-state (Ignore/Try/Require).
//
// Still-deferred limitations (unchanged from the original draft):
//   - No cancellation/progress (see sc_process_runner.hpp's own header).
//   - Revert is `git checkout --` only; does not undo a newly-added
//     (untracked-before-add) file and is not the right operation for a
//     staged deletion or rename. Documented, not fixed here.

#pragma once

#include "sc_process_runner.hpp"
#include "sc_iworkspace_session.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sc::git_lfs
{

using sc::process::ProcessResult;
using sc::process::ProcessRunner;
using namespace sc; // request/result/identity types all live in sc:: now

// ---------------------------------------------------------------------
// String/parsing helpers (genuinely git/git-lfs-specific -- stay local)
// ---------------------------------------------------------------------

namespace detail
{
    inline std::string Trim(std::string s)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    inline std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    inline bool Contains(std::string_view haystack, std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }

    inline std::string ExtractLockOwner(const std::string& lfsOutput)
    {
        const std::string marker = "locked by ";
        const std::string lower = ToLower(lfsOutput);
        const auto pos = lower.find(marker);
        if (pos == std::string::npos) return {};

        const auto start = pos + marker.size();
        auto end = lfsOutput.find_first_of("\r\n", start);
        if (end == std::string::npos) end = lfsOutput.size();
        return Trim(lfsOutput.substr(start, end - start));
    }

    // Splits on NUL bytes, matching git's `-z`/`--stdin`-style machine
    // output. A trailing NUL does not produce an extra empty element.
    inline std::vector<std::string> SplitByNul(const std::string& data)
    {
        std::vector<std::string> result;
        std::size_t start = 0;
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            if (data[i] == '\0')
            {
                result.push_back(data.substr(start, i - start));
                start = i + 1;
            }
        }
        if (start < data.size()) result.push_back(data.substr(start));
        return result;
    }

    // ---------------------------------------------------------------------
    // Minimal, purpose-built JSON field extraction for `git lfs locks --json`
    // output specifically -- NOT a general JSON parser. git-lfs's own --json
    // output is a stable, flat shape (an array of {id,path,owner:{name},
    // locked_at} objects, or {"ours":[...],"theirs":[...]} under --verify) with
    // no nested arrays/objects beyond one level and no exotic escaping in the
    // fields this provider reads - a real JSON library would be the right call
    // for arbitrary JSON, but is unjustified machinery for one CLI tool's own
    // fixed, known output.
    // ---------------------------------------------------------------------
    inline std::optional<std::string> ExtractJsonStringField(const std::string& Object, const std::string& Key)
    {
        const std::string Needle = "\"" + Key + "\"";
        auto Pos = Object.find(Needle);
        if (Pos == std::string::npos) return std::nullopt;
        Pos = Object.find(':', Pos + Needle.size());
        if (Pos == std::string::npos) return std::nullopt;
        Pos = Object.find('"', Pos);
        if (Pos == std::string::npos) return std::nullopt;
        ++Pos;
        std::string Value;
        for (; Pos < Object.size() && Object[Pos] != '"'; ++Pos)
        {
            if (Object[Pos] == '\\' && Pos + 1 < Object.size()) Value += Object[++Pos];
            else Value += Object[Pos];
        }
        return Value;
    }

    // Finds `"Key": [ ... ]` and returns the bracket-matched contents (the `[`
    // and `]` themselves included) - used to pull "ours"/"theirs" out of the
    // --verify object before splitting each into its own lock objects.
    inline std::optional<std::string> ExtractJsonArrayField(const std::string& Object, const std::string& Key)
    {
        const std::string Needle = "\"" + Key + "\"";
        auto Pos = Object.find(Needle);
        if (Pos == std::string::npos) return std::nullopt;
        Pos = Object.find('[', Pos + Needle.size());
        if (Pos == std::string::npos) return std::nullopt;

        int Depth = 0;
        const std::size_t Start = Pos;
        for (std::size_t i = Pos; i < Object.size(); ++i)
        {
            if (Object[i] == '[') ++Depth;
            else if (Object[i] == ']')
            {
                --Depth;
                if (Depth == 0) return Object.substr(Start, i - Start + 1);
            }
        }
        return std::nullopt;
    }

    // Splits a JSON array of flat objects ("[{...},{...}]") into its individual
    // "{...}" fragments by brace depth - correct for this provider's own
    // objects, which never contain a literal '{'/'}' inside a string value.
    inline std::vector<std::string> SplitJsonObjects(const std::string& ArrayText)
    {
        std::vector<std::string> Result;
        int Depth = 0;
        std::size_t Start = std::string::npos;
        for (std::size_t i = 0; i < ArrayText.size(); ++i)
        {
            if (ArrayText[i] == '{')
            {
                if (Depth == 0) Start = i;
                ++Depth;
            }
            else if (ArrayText[i] == '}')
            {
                --Depth;
                if (Depth == 0 && Start != std::string::npos)
                {
                    Result.push_back(ArrayText.substr(Start, i - Start + 1));
                    Start = std::string::npos;
                }
            }
        }
        return Result;
    }
}

// ---------------------------------------------------------------------
// GitLfsWorkspaceSession
// ---------------------------------------------------------------------

class GitLfsWorkspaceSession final : public sc::iworkspace_session
{
public:
    explicit GitLfsWorkspaceSession(std::filesystem::path repositoryRoot)
        : repoRoot_(std::move(repositoryRoot)) {}

    [[nodiscard]] std::optional<Error> Connect() override
    {
        const auto gitCheck = RunGit({"rev-parse", "--is-inside-work-tree"});
        if (gitCheck.launchFailed)
        {
            return MakeError(ErrorCode::ProviderInternalError,
                "git executable not found or failed to launch.", gitCheck);
        }
        if (gitCheck.exitCode != 0 || detail::Trim(gitCheck.stdOut) != "true")
        {
            return MakeError(ErrorCode::WorkspaceNotFound,
                "Path is not inside a Git working tree.", gitCheck);
        }
        capabilities_.sourceControlAvailable = true;

        const auto lfsCheck = RunGitLfs({"version"});
        if (!lfsCheck.launchFailed && lfsCheck.exitCode == 0)
        {
            capabilities_.lockingBackendAvailable = true;
            // We have not distinguished "LFS client installed" from "LFS
            // server actually supports the locking API" -- a stricter
            // implementation would probe `git lfs locks` once here.
            // Conservative default: assume available if the client is.
            capabilities_.lockingAvailable = true;
            return std::nullopt;
        }

        return MakeError(ErrorCode::Unsupported,
            "git-lfs is not installed or not on PATH. LFS locking "
            "features will be unavailable; plain Git operations still work.", lfsCheck);
    }

    [[nodiscard]] const SessionCapabilities& Capabilities() const noexcept override { return capabilities_; }

    // -------------------------------------------------------------
    // GetStatus
    // -------------------------------------------------------------
    [[nodiscard]] StatusResult GetStatus(const StatusRequest& request) override
    {
        StatusResult result;

        for (const auto& p : request.paths)
        {
            if (auto err = ValidateWorkspacePath(p))
            {
                result.error = *err;
                return result;
            }
        }

        std::vector<std::string> args = {"status", "--porcelain=v1", "-z", "--untracked-files=all", "--"};
        for (const auto& p : request.paths) args.push_back(ToGitPath(p));

        const auto res = RunGit(args);
        if (res.launchFailed || res.exitCode != 0)
        {
            result.error = MakeError(ErrorCode::ProviderProtocolError, "git status failed.", res);
            return result;
        }

        const std::vector<std::string> tokens = detail::SplitByNul(res.stdOut);
        std::unordered_set<std::string> seen;

        std::size_t i = 0;
        while (i < tokens.size())
        {
            const std::string& record = tokens[i];
            if (record.size() < 3)
            {
                ++i;
                continue;
            }

            const char x = record[0];
            const char y = record[1];
            const std::string path = record.substr(3);

            FileStatus fs;
            fs.path = WorkspacePath{ std::filesystem::path(path) };
            fs.staged = (x != ' ' && x != '?');
            fs.modified = (y == 'M');
            fs.untracked = (x == '?' && y == '?');
            fs.conflicted = (x == 'U' || y == 'U');
            result.files.push_back(fs);
            seen.insert(path);

            const bool isRenameOrCopy = (x == 'R' || x == 'C');
            ++i;
            if (isRenameOrCopy && i < tokens.size())
            {
                ++i; // consume the "from" path record that follows a rename/copy entry
            }
        }

        // Explicitly requested paths git reported no change for are
        // filled in as clean. NOTE: this cannot yet distinguish
        // "tracked and clean" from "ignored" or "does not exist" --
        // that would need an additional `git ls-files`/`check-ignore`
        // query, deferred per the review's status_tests note.
        for (const auto& p : request.paths)
        {
            const std::string key = ToGitPath(p);
            if (seen.count(key) == 0)
            {
                FileStatus fs;
                fs.path = p;
                result.files.push_back(fs);
            }
        }

        if (capabilities_.lockingBackendAvailable && !result.files.empty())
        {
            std::vector<WorkspacePath> toCheck;
            toCheck.reserve(result.files.size());
            for (const auto& f : result.files) toCheck.push_back(f.path);

            const auto tracked = BatchIsLfsTracked(toCheck);
            for (auto& f : result.files)
            {
                const auto it = tracked.find(ToGitPath(f.path));
                f.lfsTracked = (it != tracked.end()) && it->second;
            }
        }

        return result;
    }

    // -------------------------------------------------------------
    // PrepareEdit
    // -------------------------------------------------------------
    [[nodiscard]] PrepareEditResult PrepareEdit(const PrepareEditRequest& request) override
    {
        PrepareEditResult result;
        result.files.reserve(request.paths.size());

        const auto trackedMap = BatchIsLfsTracked(request.paths);

        for (const auto& path : request.paths)
        {
            PrepareEditFileResult fileResult;
            fileResult.path = path;

            if (auto err = ValidateWorkspacePath(path))
            {
                fileResult.error = *err;
                result.files.push_back(std::move(fileResult));
                continue;
            }

            const auto it = trackedMap.find(ToGitPath(path));
            const bool lfsTracked = (it != trackedMap.end()) && it->second;

            const bool wantsLock =
                lfsTracked && request.policy.lockRequirement != LockRequirement::Ignore;

            if (!wantsLock)
            {
                EnsureWritableAndReport(path, fileResult);
                result.files.push_back(std::move(fileResult));
                continue;
            }

            if (!capabilities_.lockingAvailable)
            {
                if (request.policy.lockRequirement == LockRequirement::Require)
                {
                    Error e;
                    e.code = ErrorCode::Unsupported;
                    e.message = "LFS locking is unavailable in this session; cannot satisfy a required lock.";
                    fileResult.error = e;
                }
                else
                {
                    fileResult.warnings.push_back(
                        "LFS locking unavailable; proceeding without a lock.");
                    EnsureWritableAndReport(path, fileResult);
                }
                result.files.push_back(std::move(fileResult));
                continue;
            }

            const auto lockRes = RunGitLfs({"lock", ToGitPath(path)});

            if (!lockRes.launchFailed && lockRes.exitCode == 0)
            {
                LockInfo lock;
                lock.path.relative = path.relative;
                lock.ownership      = LockOwnership::CurrentUser;
                lock.authority      = LockAuthority::RepositoryServer;
                lock.freshness      = ObservationFreshness::RemoteConfirmed;
                lock.effects        = CoordinationEffectFlags::VisibleToOtherUsers |
                                       CoordinationEffectFlags::PreventsOtherPublish;
                lock.canUnlock      = true;
                acquiredLocks_[ToGitPath(path)] = lock; // session now owns this lock

                fileResult.coordination.lock      = lock;
                fileResult.coordination.effects   = lock.effects;
                fileResult.coordination.authority = lock.authority;
                fileResult.coordination.freshness = lock.freshness;
                fileResult.actions |= EditActionFlags::LockAcquired | EditActionFlags::LocalIntentRecorded;

                EnsureWritableAndReport(path, fileResult);
            }
            else
            {
                // REAL BUG FOUND LIVE (2026-09-17): `git lfs lock` on a path you ALREADY hold the lock
                // on fails - confirmed directly against the real repo: exit code 2, stderr "Lock
                // exists" - not "already locked"/"locked by" (the only phrases
                // ClassifyLfsLockFailure's own text-matching recognized), so this fell all the way
                // through to its generic "unrecognized reason" fallback and PrepareEdit reported it as
                // a hard failure - opening a file you yourself already had locked incorrectly showed
                // the "File Locked" (locked by someone else) dialog. Text-matching git-lfs's own error
                // wording is inherently fragile (different server implementations phrase this
                // differently) - the only actually-correct fix is to ask who really holds the lock
                // (ListLocks --verify, server-authoritative, the same mechanism ListLocks already uses
                // rather than a local heuristic) and decide from THAT, not from error-message text.
                const auto Verify = ListLocks(ListLocksRequest{ path });
                const std::string RequestedGitPath = ToGitPath(path);
                const auto ExistingIt = std::find_if(Verify.locks.begin(), Verify.locks.end()
                    , [&](const LockInfo& L) { return L.path.relative.generic_string() == RequestedGitPath; });

                // TEMP diagnostic (2026-09-17) - the fix above still reports LockedByOther for a lock
                // that's genuinely ours; this prints exactly what the verify parse produced so we can
                // tell a parsing bug apart from a real identity mismatch.
                std::printf("[SC] PrepareEdit verify: requested='%s' initial-lock-exit=%d launchFailed=%d locks-found=%zu\n"
                    , RequestedGitPath.c_str(), lockRes.exitCode, lockRes.launchFailed ? 1 : 0, Verify.locks.size());
                for (auto& L : Verify.locks)
                    std::printf("[SC]   lock path='%s' ownership=%d ownerDisplayName='%s'\n"
                        , L.path.relative.generic_string().c_str(), static_cast<int>(L.ownership), L.ownerDisplayName.c_str());
                std::fflush(stdout);

                if (ExistingIt != Verify.locks.end() && ExistingIt->ownership == LockOwnership::CurrentUser)
                {
                    // Already locked by us - this is success, not a failure. Report the EXISTING lock
                    // rather than fabricating a fresh "just acquired" one.
                    acquiredLocks_[ToGitPath(path)] = *ExistingIt;

                    fileResult.coordination.lock      = *ExistingIt;
                    fileResult.coordination.effects   = ExistingIt->effects;
                    fileResult.coordination.authority = ExistingIt->authority;
                    fileResult.coordination.freshness = ExistingIt->freshness;
                    fileResult.actions |= EditActionFlags::LocalIntentRecorded; // NOT LockAcquired - it pre-existed, nothing was newly acquired

                    EnsureWritableAndReport(path, fileResult);
                }
                else
                {
                    const Error classified = (ExistingIt != Verify.locks.end())
                        ? [&] { Error e; e.code = ErrorCode::LockedByOther;
                                e.message = "File is locked by " + (ExistingIt->ownerDisplayName.empty() ? std::string("another user") : ExistingIt->ownerDisplayName) + ": " + ToGitPath(path);
                                return e; }()
                        : ClassifyLfsLockFailure(lockRes, path);

                    if (request.policy.lockRequirement == LockRequirement::Require)
                    {
                        fileResult.error = classified;
                    }
                    else
                    {
                        fileResult.warnings.push_back(
                            "Could not acquire lock (" + classified.message + "); proceeding without one.");
                        EnsureWritableAndReport(path, fileResult);
                    }
                }
            }

            result.files.push_back(std::move(fileResult));
        }

        return result;
    }

    // -------------------------------------------------------------
    // Add / Remove / Revert
    // -------------------------------------------------------------
    [[nodiscard]] AddResult Add(const AddRequest& request) override
    {
        return RunBatchGitCommand(request.paths, {"add", "--"});
    }

    [[nodiscard]] RemoveResult Remove(const RemoveRequest& request) override
    {
        return RunBatchGitCommand(request.paths, {"rm", "--"});
    }

    // Added during Phase 2: standalone lock release, independent of Submit. Unlike
    // ReleaseSessionLocksFor (Submit's own auto-cleanup, which only ever touches
    // locks THIS session acquired, so a commit never surprise-unlocks something it
    // didn't just lock), this is a deliberate, explicit user action -- it attempts
    // to release whatever lock exists on the path, including one acquired in an
    // earlier session, same as `git lfs unlock` itself does. `force` maps to
    // `git lfs unlock --force` (administrator break of someone else's lock).
    [[nodiscard]] UnlockResult Unlock(const UnlockRequest& request) override
    {
        BatchResult<WorkspacePath> result;
        for (const auto& path : request.paths)
        {
            if (auto err = ValidateWorkspacePath(path))
            {
                result.items.push_back(ItemResult<WorkspacePath>{ path, err });
                continue;
            }

            const std::string key = ToGitPath(path);
            std::vector<std::string> args = {"unlock", key};
            if (request.force) args.push_back("--force");

            const auto res = RunGitLfs(args);
            if (res.Succeeded())
            {
                acquiredLocks_.erase(key);
                result.items.push_back(ItemResult<WorkspacePath>{ path, std::nullopt });
            }
            else
            {
                result.items.push_back(ItemResult<WorkspacePath>{ path, ClassifyLfsLockFailure(res, path) });
            }
        }
        return result;
    }

    // Added while wiring Phase 3: a non-destructive "who currently holds this
    // lock" query, backed by `git lfs locks --verify --json`. --verify asks the
    // SERVER to split locks into "ours"/"theirs" based on the authenticated
    // caller's own identity - not a local heuristic comparing display-name
    // strings, which would produce real false positives (two users sharing a
    // display name, or a local git config that doesn't match how someone
    // authenticated). If the server doesn't implement --verify (not every
    // self-hosted LFS server does), falls back to plain `--json` with honest
    // LockOwnership::Unknown rather than guessing.
    [[nodiscard]] ListLocksResult ListLocks(const ListLocksRequest& request) override
    {
        ListLocksResult result;

        if (request.path)
        {
            if (auto err = ValidateWorkspacePath(*request.path))
            {
                result.error = *err;
                return result;
            }
        }

        // REAL BUG FOUND LIVE (2026-09-17): `git-lfs` itself refuses `--verify` combined with
        // `--path` - confirmed directly: "--verify option can't be combined with filters", exit code
        // 2. That meant EVERY scoped (single-path) verify call here silently fell through to the
        // unverified plain listing below, reporting LockOwnership::Unknown for a file you actually
        // hold the lock on - which is exactly what made PrepareEdit misreport a self-held lock as
        // belonging to someone else (surfaced as the "File Locked" dialog on a file the user already
        // had locked). Fix: --verify is NEVER combined with --path - always fetch the full,
        // unscoped ours/theirs split, then filter to the requested path locally in C++ afterward
        // (ListLocksResult::locks already gets filtered by the request.path check further below).
        std::vector<std::string> verifyArgs = {"locks", "--verify", "--json"};

        const auto verifyRes = RunGitLfs(verifyArgs);
        if (!verifyRes.launchFailed && verifyRes.exitCode == 0)
        {
            if (auto Ours = detail::ExtractJsonArrayField(verifyRes.stdOut, "ours"))
                for (auto& Obj : detail::SplitJsonObjects(*Ours))
                    result.locks.push_back(ParseLockObject(Obj, LockOwnership::CurrentUser));
            if (auto Theirs = detail::ExtractJsonArrayField(verifyRes.stdOut, "theirs"))
                for (auto& Obj : detail::SplitJsonObjects(*Theirs))
                    result.locks.push_back(ParseLockObject(Obj, LockOwnership::OtherUser));

            if (request.path)
            {
                const std::string RequestedGitPath = ToGitPath(*request.path);
                result.locks.erase(std::remove_if(result.locks.begin(), result.locks.end()
                    , [&](const LockInfo& L) { return L.path.relative.generic_string() != RequestedGitPath; })
                    , result.locks.end());
            }
            return result;
        }

        std::vector<std::string> plainArgs = {"locks", "--json"};
        if (request.path) { plainArgs.push_back("--path"); plainArgs.push_back(ToGitPath(*request.path)); }

        const auto plainRes = RunGitLfs(plainArgs);
        if (plainRes.launchFailed || plainRes.exitCode != 0)
        {
            result.error = MakeError(ErrorCode::ProviderProtocolError, "git-lfs locks failed.", plainRes);
            return result;
        }
        for (auto& Obj : detail::SplitJsonObjects(plainRes.stdOut))
            result.locks.push_back(ParseLockObject(Obj, LockOwnership::Unknown));
        return result;
    }

    [[nodiscard]] RevertResult Revert(const RevertRequest& request) override
    {
        // LIMITATION (documented, not fixed in this pass): `git checkout
        // --` only restores tracked, previously-committed content. It
        // does not undo a newly-added (untracked-before-add) file, and
        // it is not the right operation for a staged deletion or a
        // rename. A complete implementation must branch on FileStatus
        // per path (staged/unstaged/added/deleted/renamed/conflicted)
        // and must NOT delete an untracked file just because it appears
        // in a RevertRequest -- that should require explicit request
        // policy, since it is destructive and irreversible.
        return RunBatchGitCommand(request.paths, {"checkout", "--"});
    }

    // -------------------------------------------------------------
    // Sync
    // -------------------------------------------------------------
    [[nodiscard]] SyncResult Sync(const SyncRequest& request) override
    {
        SyncResult result;

        std::vector<std::string> args = {"pull"};
        if (request.fastForwardOnly) args.push_back("--ff-only");

        const auto res = RunGit(args);
        if (res.launchFailed)
        {
            result.error = MakeError(ErrorCode::ProviderInternalError, "git pull failed to launch.", res);
            return result;
        }

        if (res.exitCode == 0)
        {
            result.succeeded = true;
            result.summary = detail::Trim(res.stdOut);
            return result;
        }

        const std::string combined = detail::ToLower(res.stdOut + res.stdErr);
        if (detail::Contains(combined, "could not resolve host") || detail::Contains(combined, "connection timed out"))
        {
            result.error = MakeError(ErrorCode::Offline, "Could not reach remote.", res);
        }
        else if (detail::Contains(combined, "would be overwritten") || detail::Contains(combined, "non-fast-forward"))
        {
            result.error = MakeError(ErrorCode::Conflict,
                "Local changes conflict with remote; resolve before syncing.", res);
        }
        else
        {
            result.error = MakeError(ErrorCode::ProviderProtocolError, "git pull failed.", res);
        }
        return result;
    }

    // -------------------------------------------------------------
    // Submit: stage -> commit (pathspec-scoped) -> push -> release
    // session-owned locks. Never touches staged content outside
    // request.paths.
    // -------------------------------------------------------------
    [[nodiscard]] SubmitResult Submit(const SubmitRequest& request) override
    {
        SubmitResult result;

        for (const auto& p : request.paths)
        {
            if (auto err = ValidateWorkspacePath(p))
            {
                result.warnings.push_back("Invalid path in submit request: " + err->message);
                result.recovery = SubmitRecoveryAction::UserInterventionRequired;
                return result;
            }
        }

        std::vector<std::string> addArgs = {"add", "--"};
        for (const auto& p : request.paths) addArgs.push_back(ToGitPath(p));
        const auto addRes = RunGit(addArgs);
        if (addRes.launchFailed || addRes.exitCode != 0)
        {
            result.recovery = SubmitRecoveryAction::UserInterventionRequired;
            result.warnings.push_back("git add failed: " + detail::Trim(addRes.stdErr));
            return result;
        }
        result.completedPhases |= SubmitPhaseFlags::Validated | SubmitPhaseFlags::LocalStateRecorded;

        // Pathspec-scoped commit: this commits ONLY the requested paths.
        // Any other content the user (or another tool) had already
        // staged remains staged, untouched, for a future commit -- it is
        // never silently swept into this one. This is documented git
        // behavior for `git commit <pathspec>`.
        std::vector<std::string> commitArgs = {"commit", "-m", request.description, "--"};
        for (const auto& p : request.paths) commitArgs.push_back(ToGitPath(p));
        const auto commitRes = RunGit(commitArgs);

        if (commitRes.launchFailed || commitRes.exitCode != 0)
        {
            if (detail::Contains(commitRes.stdOut, "nothing to commit"))
            {
                result.warnings.push_back("Nothing to commit for the requested paths.");
            }
            else
            {
                result.warnings.push_back("git commit failed: " + detail::Trim(commitRes.stdErr));
            }
            result.recovery = SubmitRecoveryAction::UserInterventionRequired;
            return result;
        }
        result.completedPhases |= SubmitPhaseFlags::LocalRevisionMade;

        const auto hashRes = RunGit({"rev-parse", "HEAD"});
        if (!hashRes.launchFailed && hashRes.exitCode == 0)
        {
            result.localRevision = detail::Trim(hashRes.stdOut);
        }

        const auto pushRes = RunGit({"push"});
        if (!pushRes.launchFailed && pushRes.exitCode == 0)
        {
            result.completedPhases |=
                SubmitPhaseFlags::Uploaded | SubmitPhaseFlags::RemoteRevisionMade | SubmitPhaseFlags::Published;
            result.remoteRevision = result.localRevision;
            result.retryMayDuplicateRemoteEffect = false; // re-pushing an already-published commit is a no-op

            ReleaseSessionLocksFor(request.paths, request.keepLocks, result);
            return result;
        }

        const auto Failure = ClassifyPushFailure(pushRes);
        result.recovery = Failure.recovery;
        result.remoteStateAmbiguous = Failure.remoteStateAmbiguous;
        result.warnings.push_back(Failure.message);

        return result;
    }

    // -------------------------------------------------------------
    // Push: whatever is already locally committed, without staging or
    // committing anything new. Added during Phase 2 for the task's own
    // pull-then-push workflow -- retrying a push after Sync resolved
    // what blocked it, with nothing new to commit, has no other way to
    // reach the remote through this interface otherwise.
    // -------------------------------------------------------------
    [[nodiscard]] PushResult Push(const PushRequest&) override
    {
        PushResult result;

        const auto pushRes = RunGit({"push"});
        if (!pushRes.launchFailed && pushRes.exitCode == 0)
        {
            result.succeeded = true;
            result.summary = detail::Trim(pushRes.stdOut + pushRes.stdErr);

            const auto hashRes = RunGit({"rev-parse", "HEAD"});
            if (!hashRes.launchFailed && hashRes.exitCode == 0)
                result.remoteRevision = detail::Trim(hashRes.stdOut);

            return result;
        }

        const auto Failure = ClassifyPushFailure(pushRes);
        Error e;
        e.code = Failure.errorCode;
        e.message = Failure.message;
        e.providerMessage = detail::Trim(pushRes.stdOut + pushRes.stdErr);
        e.retryable = (e.code == ErrorCode::Offline);
        result.error = e;
        return result;
    }

private:
    // Builds one canonical sc::LockInfo from a single git-lfs --json lock
    // object ({"id":...,"path":...,"owner":{"name":...},"locked_at":...}).
    // `locked_at` is a real ISO-8601 timestamp git-lfs already gives us, but
    // parsing it isn't needed for anything this provider does yet - left at
    // LockInfo's own documented std::nullopt default rather than adding a
    // date parser for a field nothing reads.
    [[nodiscard]] static LockInfo ParseLockObject(const std::string& Object, LockOwnership Ownership)
    {
        LockInfo Lock;
        if (auto V = detail::ExtractJsonStringField(Object, "id"))   Lock.id = LockId{ *V };
        if (auto V = detail::ExtractJsonStringField(Object, "path")) Lock.path = RepoPath{ std::filesystem::path(*V) };
        if (auto V = detail::ExtractJsonStringField(Object, "name"))
        {
            Lock.ownerIdentity    = *V;
            Lock.ownerDisplayName = *V;
        }
        Lock.ownership = Ownership;
        Lock.scope     = LockScope::RepositoryPath;
        Lock.authority = LockAuthority::RepositoryServer;
        Lock.freshness = ObservationFreshness::RemoteConfirmed;
        Lock.canUnlock = (Ownership == LockOwnership::CurrentUser); // ours are always unlockable by us; theirs need -Force
        return Lock;
    }

    // Shared by Submit and Push -- classifies a failed `git push` invocation's
    // output into a recovery action, an sc::ErrorCode, and a human-readable
    // message, all from the same string match so the two callers can never
    // disagree on what a given failure means. Factored out rather than
    // duplicated once Push needed the exact same classification Submit's own
    // push step already had.
    struct push_failure
    {
        SubmitRecoveryAction recovery = SubmitRecoveryAction::None;
        ErrorCode   errorCode = ErrorCode::ProviderProtocolError;
        std::string message;
        bool        remoteStateAmbiguous = false;
    };

    [[nodiscard]] static push_failure ClassifyPushFailure(const ProcessResult& pushRes)
    {
        const std::string combined = detail::ToLower(pushRes.stdOut + pushRes.stdErr);

        if (detail::Contains(combined, "could not resolve host") ||
            detail::Contains(combined, "connection timed out") ||
            detail::Contains(combined, "connection reset") ||
            detail::Contains(combined, "unexpected disconnect"))
        {
            return { SubmitRecoveryAction::RefreshThenRetry, ErrorCode::Offline,
                "Network failure during push; remote state is unknown. Refresh status before retrying.", true };
        }
        if (detail::Contains(combined, "[rejected]") || detail::Contains(combined, "non-fast-forward"))
        {
            return { SubmitRecoveryAction::RefreshThenRetry, ErrorCode::Conflict,
                "Push rejected: remote has newer commits. Pull/rebase before retrying.", false };
        }
        if (detail::Contains(combined, "authentication failed") ||
            detail::Contains(combined, "permission denied") ||
            detail::Contains(combined, "403"))
        {
            return { SubmitRecoveryAction::UserInterventionRequired, ErrorCode::AuthenticationRequired,
                "Authentication failed during push.", false };
        }
        return { SubmitRecoveryAction::RetrySafe, ErrorCode::ProviderProtocolError,
            "git push failed: " + detail::Trim(pushRes.stdErr), false };
    }

    void ReleaseSessionLocksFor(
        const std::vector<WorkspacePath>& paths, bool keepLocks, SubmitResult& result)
    {
        if (keepLocks) return;

        bool anyLockAttempted = false;
        bool allLocksReleased = true;

        for (const auto& path : paths)
        {
            const std::string key = ToGitPath(path);
            const auto it = acquiredLocks_.find(key);
            if (it == acquiredLocks_.end())
            {
                continue; // this session never locked it; do not touch it
            }

            anyLockAttempted = true;
            const auto unlockResult = RunGitLfs({"unlock", key});

            if (unlockResult.Succeeded())
            {
                acquiredLocks_.erase(it);
            }
            else
            {
                allLocksReleased = false;
                result.warnings.push_back(
                    "Failed to unlock " + key + ": " +
                    detail::Trim(unlockResult.stdOut + unlockResult.stdErr));
            }
        }

        if (anyLockAttempted && allLocksReleased)
        {
            result.completedPhases |= SubmitPhaseFlags::LocksReleased;
        }
        // If unlocking failed, Published stays set (publication genuinely
        // succeeded) but LocksReleased is correctly left unset, and the
        // warnings explain why -- these are independent facts.
    }

    [[nodiscard]] ProcessResult RunGit(std::vector<std::string> args) const
    {
        std::vector<std::string> full = {"git"};
        full.insert(full.end(), args.begin(), args.end());
        return ProcessRunner::Run(full, repoRoot_);
    }

    [[nodiscard]] ProcessResult RunGitLfs(std::vector<std::string> args) const
    {
        std::vector<std::string> full = {"git", "lfs"};
        full.insert(full.end(), args.begin(), args.end());
        return ProcessRunner::Run(full, repoRoot_);
    }

    [[nodiscard]] static std::string ToGitPath(const WorkspacePath& path)
    {
        return path.relative.generic_string();
    }

    [[nodiscard]] std::optional<Error> ValidateWorkspacePath(const WorkspacePath& path) const
    {
        const auto& p = path.relative;
        if (p.is_absolute())
        {
            Error e;
            e.code = ErrorCode::InvalidArgument;
            e.message = "Path must be workspace-relative, not absolute: " + p.string();
            return e;
        }
        for (const auto& part : p)
        {
            if (part == "..")
            {
                Error e;
                e.code = ErrorCode::InvalidArgument;
                e.message = "Path must not contain '..' segments: " + p.string();
                return e;
            }
        }
        return std::nullopt;
    }

    // Verifies (and if needed, sets) the owner-write permission on the
    // file, and only reports MadeWritable if that was actually confirmed
    // afterward -- never claimed unconditionally.
    void EnsureWritableAndReport(const WorkspacePath& path, PrepareEditFileResult& fileResult) const
    {
        std::error_code ec;
        const auto fullPath = repoRoot_ / path.relative;

        auto status = std::filesystem::status(fullPath, ec);
        if (ec)
        {
            Error e;
            e.code = ErrorCode::PathNotFound;
            e.message = "File not found in workspace: " + path.relative.string();
            fileResult.error = e;
            return;
        }

        const bool alreadyWritable =
            (status.permissions() & std::filesystem::perms::owner_write) != std::filesystem::perms::none;

        if (!alreadyWritable)
        {
            std::filesystem::permissions(
                fullPath, std::filesystem::perms::owner_write,
                std::filesystem::perm_options::add, ec);
        }

        status = std::filesystem::status(fullPath, ec);
        const bool nowWritable =
            !ec && (status.permissions() & std::filesystem::perms::owner_write) != std::filesystem::perms::none;

        if (nowWritable)
        {
            fileResult.actions |= EditActionFlags::MadeWritable;
        }
        else
        {
            Error e;
            e.code = ErrorCode::ProviderInternalError;
            e.message = "Could not make file writable: " + path.relative.string();
            fileResult.error = e;
        }
    }

    // Classifies a failed `git lfs lock` OR `git lfs unlock` invocation (shared by PrepareEdit and
    // Unlock - both call `git lfs {lock,unlock}`, and their failure text overlaps enough that one
    // classifier correctly serves both). Only text matching a genuine lock-conflict pattern becomes
    // LockedByOther; everything else is reported as what it actually looks like.
    //
    // FOUND LIVE, against the real example.lionprj repo's real GitHub remote (not a hypothetical):
    // `git lfs unlock` on a file with uncommitted local changes refuses UNLESS --force is passed -
    // this is the NORMAL case for releasing your own lock on a file you edited but haven't committed
    // yet, not an edge case. Before this branch existed, that message fell through to the generic
    // "unrecognized reason" bucket below, leaving the real lock stuck on the remote until manually
    // force-unlocked from a shell - exactly what happened testing this function for the first time.
    [[nodiscard]] Error ClassifyLfsLockFailure(const ProcessResult& res, const WorkspacePath& path) const
    {
        if (res.launchFailed)
        {
            Error e;
            e.code = ErrorCode::ProviderInternalError;
            e.message = "Failed to launch git-lfs.";
            e.providerMessage = res.launchError;
            return e;
        }

        const std::string combined = res.stdOut + res.stdErr;
        const std::string lower = detail::ToLower(combined);

        if (detail::Contains(lower, "uncommitted changes"))
        {
            Error e;
            e.code = ErrorCode::Conflict;
            e.message = "Cannot unlock " + ToGitPath(path) + ": it has uncommitted local changes. "
                "Commit or discard them first, or retry with force to unlock anyway.";
            e.providerMessage = combined;
            return e;
        }
        if (detail::Contains(lower, "already locked") || detail::Contains(lower, "locked by"))
        {
            const std::string owner = detail::ExtractLockOwner(combined);
            Error e;
            e.code = ErrorCode::LockedByOther;
            e.message = owner.empty()
                ? ("File is locked by another user: " + ToGitPath(path))
                : ("File is locked by " + owner + ": " + ToGitPath(path));
            e.providerMessage = combined;
            return e;
        }
        if (detail::Contains(lower, "authentication") || detail::Contains(lower, "401") ||
            detail::Contains(lower, "permission denied") || detail::Contains(lower, "403"))
        {
            Error e;
            e.code = ErrorCode::AuthenticationRequired;
            e.message = "Authentication failed while locking " + ToGitPath(path) + ".";
            e.providerMessage = combined;
            return e;
        }
        if (detail::Contains(lower, "could not resolve host") ||
            detail::Contains(lower, "timed out") ||
            detail::Contains(lower, "connection"))
        {
            Error e;
            e.code = ErrorCode::Offline;
            e.retryable = true;
            e.message = "Network error while locking " + ToGitPath(path) + ".";
            e.providerMessage = combined;
            return e;
        }
        if (detail::Contains(lower, "not supported") ||
            detail::Contains(lower, "unknown command") ||
            detail::Contains(lower, "no such"))
        {
            Error e;
            e.code = ErrorCode::Unsupported;
            e.message = "The server does not support LFS locking, or the path is not recognized.";
            e.providerMessage = combined;
            return e;
        }

        Error e;
        e.code = ErrorCode::ProviderProtocolError; // NOT LockedByOther -- unrecognized failure
        e.message = "git-lfs lock failed for an unrecognized reason.";
        e.providerMessage = combined;
        return e;
    }

    // Batched `git check-ignore --stdin` - direct user request: the plugin must respect .gitignore
    // (and by extension never treat an ignored file as lock-required, since it's never going to be
    // committed at all). Same "-z --stdin, one process for the whole batch" shape as
    // BatchIsLfsTracked, just asymmetric: check-ignore's stdin mode only ECHOES paths that ARE
    // ignored (absence from stdOut means "not ignored"), unlike check-attr which echoes every input
    // path regardless of match - defaulting every requested path to false up front handles that.
    [[nodiscard]] std::unordered_map<std::string, bool> BatchIsIgnored(
        const std::vector<WorkspacePath>& paths) const
    {
        std::unordered_map<std::string, bool> result;
        if (paths.empty()) return result;
        for (const auto& p : paths) result[ToGitPath(p)] = false;

        std::string stdinInput;
        for (const auto& p : paths)
        {
            stdinInput += ToGitPath(p);
            stdinInput += '\0';
        }

        const std::vector<std::string> args = {"git", "check-ignore", "-z", "--stdin"};
        const auto res = ProcessRunner::Run(args, repoRoot_, stdinInput);
        // check-ignore's own exit code means "0 = at least one match, 1 = no matches, 128 = error" -
        // 1 is a normal, expected outcome (nothing in this batch is ignored), not a failure, so only
        // launchFailed actually means "couldn't determine this, assume not ignored".
        if (res.launchFailed) return result;

        for (auto& Path : detail::SplitByNul(res.stdOut))
            if (!Path.empty()) result[Path] = true;

        return result;
    }

    // Fixes the N+1 subprocess problem: one `git check-attr --stdin`
    // call for the whole batch instead of one process per file.
    [[nodiscard]] std::unordered_map<std::string, bool> BatchIsLfsTracked(
        const std::vector<WorkspacePath>& paths) const
    {
        std::unordered_map<std::string, bool> result;
        if (paths.empty()) return result;

        std::string stdinInput;
        for (const auto& p : paths)
        {
            stdinInput += ToGitPath(p);
            stdinInput += '\0';
        }

        const std::vector<std::string> args = {"git", "check-attr", "-z", "--stdin", "filter"};
        const auto res = ProcessRunner::Run(args, repoRoot_, stdinInput);

        if (res.launchFailed || res.exitCode != 0)
        {
            for (const auto& p : paths) result[ToGitPath(p)] = false;
            return result;
        }

        // Output is NUL-delimited (path, attribute-name, attribute-value)
        // triplets. We only requested one attribute, so each path
        // produces exactly one triplet.
        const std::vector<std::string> tokens = detail::SplitByNul(res.stdOut);
        for (std::size_t i = 0; i + 2 < tokens.size(); i += 3)
        {
            const std::string& path = tokens[i];
            const std::string& value = tokens[i + 2];
            result[path] = (value == "lfs");
        }

        // Any requested path check-attr didn't report on (shouldn't
        // normally happen) defaults to "not tracked" rather than being
        // silently absent from the map.
        for (const auto& p : paths)
        {
            result.try_emplace(ToGitPath(p), false);
        }

        // An ignored path is never treated as LFS-tracked/lock-required, regardless of what
        // check-attr's own pattern match says - .gitattributes patterns and .gitignore are
        // independent files, and a path can match an "*.png filter=lfs" rule while ALSO being
        // .gitignore'd (e.g. a generated/cache thumbnail); it will never be committed either way, so
        // there's nothing real to lock. This is the one place both GetStatus's f.lfsTracked and
        // PrepareEdit's wantsLock ultimately read from, so fixing it here covers both call sites.
        for (auto& [Path, Ignored] : BatchIsIgnored(paths))
            if (Ignored) result[Path] = false;

        return result;
    }

    template <typename T>
    [[nodiscard]] BatchResult<T> RunBatchGitCommand(
        const std::vector<T>& items, std::vector<std::string> baseArgs) const
    {
        BatchResult<T> result;
        if (items.empty()) return result;

        for (const auto& item : items)
        {
            if constexpr (std::is_same_v<T, WorkspacePath>)
            {
                if (auto err = ValidateWorkspacePath(item))
                {
                    result.items.push_back(ItemResult<T>{ item, err });
                    return result; // fail closed on an invalid path in the batch
                }
            }
        }

        std::vector<std::string> args = std::move(baseArgs);
        for (const auto& item : items) args.push_back(ToGitPath(item));

        const auto res = RunGit(args);

        if (!res.launchFailed && res.exitCode == 0)
        {
            for (const auto& item : items)
            {
                result.items.push_back(ItemResult<T>{ item, std::nullopt });
            }
            return result;
        }

        // See original rationale: git's batch commands are not reliably
        // per-item on failure, so every item in a failed batch is
        // reported failed rather than guessing which ones landed.
        const Error batchError = MakeError(
            ErrorCode::ProviderProtocolError,
            "Batch git command reported failure for one or more items.", res);

        for (const auto& item : items)
        {
            result.items.push_back(ItemResult<T>{ item, batchError });
        }
        return result;
    }

    [[nodiscard]] static Error MakeError(ErrorCode code, std::string message, const ProcessResult& res)
    {
        Error err;
        err.code = code;
        err.message = std::move(message);
        err.providerMessage = res.launchFailed ? res.launchError : detail::Trim(res.stdErr);
        err.retryable = (code == ErrorCode::Offline);
        return err;
    }

    std::filesystem::path repoRoot_;
    SessionCapabilities capabilities_;
    std::unordered_map<std::string, LockInfo> acquiredLocks_; // locks THIS session owns
};

} // namespace sc::git_lfs
