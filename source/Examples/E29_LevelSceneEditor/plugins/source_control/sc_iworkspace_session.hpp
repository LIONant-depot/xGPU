// sc_iworkspace_session.hpp
//
// Provider-agnostic source-control abstraction: identity/policy vocabulary,
// request/result value types, and the pure-virtual `iworkspace_session`
// interface. See source_control_abstraction_spec_v1_3.md (same directory)
// for the full design rationale -- this file is that spec's Part I/II made
// real, kept in sync with it.
//
// Git+LFS (sc_git_lfs_provider.hpp) is the only concrete implementation
// today. A future Perforce/SVN/Lore provider derives from
// `iworkspace_session` the same way, reusing these same request/result
// types -- nothing here is git-specific.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>

namespace sc
{
    // -------------------------------------------------------------
    // Identity & policy vocabulary
    // -------------------------------------------------------------

    struct ProviderId    { std::string value; };
    struct RepositoryId  { std::string value; };
    struct WorkspaceId   { std::string value; };
    struct EndpointId    { std::string value; };
    struct BranchId      { std::string value; };
    struct StreamId      { std::string value; };
    struct OperationId   { std::uint64_t value = 0; };

    struct ChangeId
    {
        std::string value;
        [[nodiscard]] bool IsValid() const noexcept { return !value.empty(); }
    };

    struct RevisionId { std::string value; };
    struct LockId      { std::string opaqueValue; };

    // PathRules must precede WorkspaceInfo -- WorkspaceInfo holds it by value.
    struct PathRules
    {
        bool repositoryCaseSensitive       = true;
        bool workspaceCaseSensitive        = true;
        bool supportsUnicodeNormalization  = true;
        bool followsSymlinks               = false;
        std::uint32_t maxPathBytes         = 0; // 0 = provider-defined/unknown
    };

    struct WorkspaceInfo
    {
        WorkspaceId   id;
        ProviderId    provider;
        RepositoryId  repository;
        std::filesystem::path root;
        std::string   displayName;
        PathRules     pathRules;
    };

    struct RepoPath { std::filesystem::path relative; };
    struct WorkspacePath { std::filesystem::path relative; };

    // -------------------------------------------------------------
    // Lock vocabulary (reconciled -- see spec Part I for the two drafts
    // this merges: v1.2's rich LockInfo and the provider's own simpler
    // LockInfo + CoordinationState/EnforcementAuthority)
    // -------------------------------------------------------------

    enum class LockScope : std::uint8_t
    { Unknown, RepositoryPath, BranchPath, StreamPath, ChangePath, WorkspacePath };

    enum class LockOwnership : std::uint8_t
    { Unknown, CurrentUser, OtherUser, Administrator };

    // Canonical authority enum -- replaces the provider draft's separate
    // `EnforcementAuthority`. `PublishEndpoint` is kept for future
    // providers (a submit server distinct from the repository server)
    // even though Git never produces it.
    enum class LockAuthority : std::uint8_t
    { Unknown, LocalConvention, RepositoryServer, PublishEndpoint };

    enum class LockReleaseReason : std::uint8_t
    { Unknown, ManualUnlock, Submit, LeaseExpiry, AdministrativeBreak, ProviderPolicy };

    // Was this fact read straight from local working-copy state, or
    // confirmed by a round trip to the server? Git's `git lfs locks` is
    // always a server round trip (RemoteConfirmed).
    enum class ObservationFreshness : std::uint8_t
    { Unknown, LocalWorkingCopy, RemoteConfirmed };

    enum class CoordinationEffectFlags : std::uint32_t
    {
        None                   = 0,
        VisibleToOtherUsers    = 1u << 0,
        PreventsOtherPublish   = 1u << 1,
        PreventsConcurrentEdit = 1u << 2 // never set by the Git provider
    };
    [[nodiscard]] constexpr CoordinationEffectFlags operator|(CoordinationEffectFlags a, CoordinationEffectFlags b) noexcept
    { return static_cast<CoordinationEffectFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); }
    constexpr CoordinationEffectFlags& operator|=(CoordinationEffectFlags& a, CoordinationEffectFlags b) noexcept
    { a = a | b; return a; }
    [[nodiscard]] constexpr bool HasFlag(CoordinationEffectFlags v, CoordinationEffectFlags f) noexcept
    { return (static_cast<std::uint32_t>(v) & static_cast<std::uint32_t>(f)) == static_cast<std::uint32_t>(f); }

    // The one canonical LockInfo. A provider populates whichever fields it
    // can actually answer and leaves the rest at these documented, honest
    // defaults -- it must never fabricate a value it cannot observe.
    struct LockInfo
    {
        LockId        id;
        RepoPath      path;
        LockScope     scope     = LockScope::Unknown;
        LockOwnership ownership = LockOwnership::Unknown;
        LockAuthority authority = LockAuthority::Unknown;
        ObservationFreshness freshness = ObservationFreshness::Unknown;

        // Renamed from v1.2's `guarantees`: an OBSERVED FACT reported by
        // the provider, not a promise.
        CoordinationEffectFlags effects = CoordinationEffectFlags::None;

        std::string ownerIdentity;
        std::string ownerDisplayName;
        std::optional<WorkspaceId> ownerWorkspace;
        std::optional<ChangeId>    ownerChange;
        std::string comment;
        std::optional<std::chrono::system_clock::time_point> acquiredAt;
        std::optional<std::chrono::system_clock::time_point> expiresAt;
        bool canUnlock = false;
        bool canSteal  = false;
        bool canBreak  = false;
        LockReleaseReason expectedRelease = LockReleaseReason::Unknown;
    };

    // Per-file result of a coordination attempt (PrepareEdit).
    struct CoordinationState
    {
        CoordinationEffectFlags effects   = CoordinationEffectFlags::None;
        LockAuthority           authority = LockAuthority::Unknown;
        ObservationFreshness    freshness = ObservationFreshness::Unknown;
        std::optional<LockInfo> lock;
    };

    // -------------------------------------------------------------
    // Errors
    // -------------------------------------------------------------

    enum class ErrorCode : std::uint32_t
    {
        None, InvalidArgument, WorkspaceNotFound, PathNotFound, LockedByOther,
        AuthenticationRequired, Offline, OutOfDate, Conflict, Unsupported,
        ProviderProtocolError, ProviderInternalError
    };

    struct Error
    {
        ErrorCode   code = ErrorCode::None;
        std::string message;
        std::string providerMessage; // raw CLI output, for diagnostics
        bool        retryable = false;
    };

    // -------------------------------------------------------------
    // PrepareEdit (the lock-before-edit operation)
    // -------------------------------------------------------------

    enum class EditActionFlags : std::uint32_t
    {
        None                = 0,
        LocalIntentRecorded = 1u << 0, // set only when session state was actually recorded
        MadeWritable        = 1u << 1, // set only after verifying/setting the permission
        LockAcquired        = 1u << 2
    };
    [[nodiscard]] constexpr EditActionFlags operator|(EditActionFlags a, EditActionFlags b) noexcept
    { return static_cast<EditActionFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); }
    constexpr EditActionFlags& operator|=(EditActionFlags& a, EditActionFlags b) noexcept { a = a | b; return a; }
    [[nodiscard]] constexpr bool HasFlag(EditActionFlags v, EditActionFlags f) noexcept
    { return (static_cast<std::uint32_t>(v) & static_cast<std::uint32_t>(f)) == static_cast<std::uint32_t>(f); }

    struct PrepareEditFileResult
    {
        WorkspacePath      path;
        EditActionFlags    actions = EditActionFlags::None;
        CoordinationState  coordination;
        std::vector<std::string> warnings;
        std::optional<Error>     error;
        [[nodiscard]] bool OperationSucceeded() const noexcept { return !error.has_value(); }
    };

    enum class LockRequirement : std::uint8_t
    {
        Ignore,  // never attempt a lock, even for a lockable/binary file
        Try,     // attempt a lock if locking is available; proceed with a warning otherwise
        Require  // fail the item if a lock cannot be obtained
    };

    struct PrepareEditPolicy { LockRequirement lockRequirement = LockRequirement::Try; };

    struct PrepareEditRequest
    {
        std::vector<WorkspacePath> paths;
        PrepareEditPolicy policy;
    };

    struct PrepareEditResult
    {
        std::vector<PrepareEditFileResult> files;
        [[nodiscard]] bool AllOperationsSucceeded() const noexcept
        {
            if (files.empty()) return false;
            return std::all_of(files.begin(), files.end(), [](const auto& f) { return f.OperationSucceeded(); });
        }
    };

    // -------------------------------------------------------------
    // Batch results (Add/Remove/Revert)
    // -------------------------------------------------------------

    template <typename T> struct ItemResult
    {
        T item;
        std::optional<Error> error;
        [[nodiscard]] bool Succeeded() const noexcept { return !error.has_value(); }
    };

    template <typename T> struct BatchResult
    {
        std::vector<ItemResult<T>> items;
        [[nodiscard]] bool AllSucceeded() const noexcept
        {
            if (items.empty()) return false;
            return std::all_of(items.begin(), items.end(), [](const auto& i) { return i.Succeeded(); });
        }
    };

    // -------------------------------------------------------------
    // Status
    // -------------------------------------------------------------

    struct FileStatus
    {
        WorkspacePath path;
        bool staged      = false;
        bool modified    = false;
        bool untracked   = false;
        bool conflicted  = false;
        bool lfsTracked  = false; // == "binary/non-mergeable/lock-required" -- see spec Part IV
    };

    struct StatusRequest { std::vector<WorkspacePath> paths; };
    struct StatusResult  { std::vector<FileStatus> files; std::optional<Error> error; };

    struct AddRequest    { std::vector<WorkspacePath> paths; };  using AddResult    = BatchResult<WorkspacePath>;
    struct RemoveRequest { std::vector<WorkspacePath> paths; };  using RemoveResult = BatchResult<WorkspacePath>;
    struct RevertRequest { std::vector<WorkspacePath> paths; };  using RevertResult = BatchResult<WorkspacePath>;

    // Added during Phase 2 (E29 command-bus mapping): the original draft only ever
    // released a lock as part of Submit's own keepLocks=false cleanup. A user
    // needs to release a lock WITHOUT committing (changed their mind, or is just
    // done looking at a file) -- a real gap found wiring this to
    // E29_Commands_SourceControl.h's `unlock_query_cmd`, not a speculative addition.
    struct UnlockRequest { std::vector<WorkspacePath> paths; bool force = false; };
    using UnlockResult = BatchResult<WorkspacePath>;

    // Added while wiring Phase 3 (Asset Tree lock icons): PrepareEdit only ever
    // reports lock state as a SIDE EFFECT of attempting to acquire one -- there was
    // no way to non-destructively ask "who currently holds the lock on this file",
    // which a "locked by you" vs "locked by someone else" icon fundamentally needs.
    // `path` empty = every lock in the repo (one call backs a whole tree refresh,
    // not one call per visible row).
    struct ListLocksRequest { std::optional<WorkspacePath> path; };
    struct ListLocksResult  { std::vector<LockInfo> locks; std::optional<Error> error; };

    // -------------------------------------------------------------
    // Sync (pull) / Submit (stage + commit + push)
    // -------------------------------------------------------------

    struct SyncRequest  { bool fastForwardOnly = true; };
    struct SyncResult   { bool succeeded = false; std::optional<Error> error; std::string summary; };

    // Added during Phase 2, same reasoning as UnlockRequest above: Submit's push is
    // coupled to its own commit step (there is no "just push what's already
    // committed" primitive), but the task's own workflow -- pull, resolve, THEN
    // explicitly push -- needs exactly that as a standalone step, e.g. retrying a
    // push after Sync resolved what blocked it, with nothing new to commit.
    struct PushRequest { };
    struct PushResult
    {
        bool succeeded = false;
        std::optional<Error> error;
        std::optional<std::string> remoteRevision;
        std::string summary;
    };

    enum class SubmitPhaseFlags : std::uint32_t
    {
        None = 0,
        Validated          = 1u << 0,
        LocalStateRecorded = 1u << 1,
        LocalRevisionMade  = 1u << 2,
        Uploaded           = 1u << 3,
        RemoteRevisionMade = 1u << 4,
        Published          = 1u << 5,
        LocksReleased      = 1u << 6
    };
    [[nodiscard]] constexpr SubmitPhaseFlags operator|(SubmitPhaseFlags a, SubmitPhaseFlags b) noexcept
    { return static_cast<SubmitPhaseFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); }
    constexpr SubmitPhaseFlags& operator|=(SubmitPhaseFlags& a, SubmitPhaseFlags b) noexcept { a = a | b; return a; }
    [[nodiscard]] constexpr bool HasFlag(SubmitPhaseFlags v, SubmitPhaseFlags f) noexcept
    { return (static_cast<std::uint32_t>(v) & static_cast<std::uint32_t>(f)) == static_cast<std::uint32_t>(f); }

    enum class SubmitRecoveryAction : std::uint8_t { None, RetrySafe, RefreshThenRetry, UserInterventionRequired };
    enum class SubmitOutcome         : std::uint8_t { Published, CompletedLocally, RemoteStateUnknown, Failed };

    struct SubmitRequest
    {
        std::vector<WorkspacePath> paths;
        std::string description;
        bool keepLocks = false;
    };

    struct SubmitResult
    {
        SubmitPhaseFlags completedPhases = SubmitPhaseFlags::None;
        std::optional<std::string> localRevision;
        std::optional<std::string> remoteRevision;
        std::vector<std::string> warnings;
        SubmitRecoveryAction recovery = SubmitRecoveryAction::None;
        bool remoteStateAmbiguous          = false;
        bool retryMayDuplicateRemoteEffect = false;

        [[nodiscard]] SubmitOutcome Summary() const noexcept
        {
            if (HasFlag(completedPhases, SubmitPhaseFlags::Published)) return SubmitOutcome::Published;
            if (remoteStateAmbiguous) return SubmitOutcome::RemoteStateUnknown;
            if (HasFlag(completedPhases, SubmitPhaseFlags::LocalRevisionMade)) return SubmitOutcome::CompletedLocally;
            return SubmitOutcome::Failed;
        }
    };

    // -------------------------------------------------------------
    // Session capabilities
    // -------------------------------------------------------------

    struct SessionCapabilities
    {
        bool sourceControlAvailable  = false; // was `gitAvailable` in the provider draft
        bool lockingBackendAvailable = false; // was `lfsAvailable`
        bool lockingAvailable        = false; // was `lfsLockingAvailable`
    };

    // -------------------------------------------------------------
    // The abstract interface. Every future provider (Perforce/SVN/Lore)
    // derives from this directly -- see spec Part II.
    // -------------------------------------------------------------
    class iworkspace_session
    {
    public:
        virtual ~iworkspace_session() = default;

        [[nodiscard]] virtual std::optional<Error> Connect() = 0;
        [[nodiscard]] virtual const SessionCapabilities& Capabilities() const noexcept = 0;

        // Canonical, resolved workspace identity - added for the "Multi-library project model" plan
        // section's Phase B (depot-as-cache validation): Connect() only ever answers "is this path
        // inside SOME work tree", never what that tree's own canonical root or remote actually is, so
        // there was nothing concrete for a depot-link validator to diff a cached identity against.
        // Only valid to call after a successful Connect(). `.root` must be the fully resolved root
        // (for Git: `git rev-parse --show-toplevel`, NOT just the path the session happened to be
        // constructed with - a library given a subdirectory of a repo must still resolve to the same
        // root a library given the repo's own root would). `.repository.value` is the provider's own
        // best identity for the same physical depot regardless of which local clone/subdirectory it's
        // viewed from (for Git: `git remote get-url origin`, empty if there is no remote yet).
        [[nodiscard]] virtual WorkspaceInfo GetWorkspaceInfo() = 0;

        [[nodiscard]] virtual StatusResult      GetStatus  (const StatusRequest&)      = 0;
        [[nodiscard]] virtual PrepareEditResult PrepareEdit(const PrepareEditRequest&) = 0;
        [[nodiscard]] virtual AddResult         Add        (const AddRequest&)         = 0;
        [[nodiscard]] virtual RemoveResult      Remove     (const RemoveRequest&)      = 0;
        [[nodiscard]] virtual RevertResult      Revert     (const RevertRequest&)      = 0;
        [[nodiscard]] virtual UnlockResult      Unlock     (const UnlockRequest&)      = 0;
        [[nodiscard]] virtual ListLocksResult   ListLocks  (const ListLocksRequest&)   = 0;
        [[nodiscard]] virtual SyncResult        Sync       (const SyncRequest&)        = 0;
        [[nodiscard]] virtual SubmitResult      Submit     (const SubmitRequest&)      = 0;
        [[nodiscard]] virtual PushResult        Push       (const PushRequest&)        = 0;
    };
}
