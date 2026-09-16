# Source Control Abstraction Layer — Design Specification (v1.3)

## Status & scope

This supersedes `source_control_abstraction_spec_v1_2.md`. v1.2 was itself only a diff against a
v1.1 document that no longer exists — this revision is written whole, not as a diff, so it stands
on its own.

**Git + Git-LFS is the only real implementation built now.** Every type and interface below is
shaped so that Perforce, SVN, and an in-house system ("Lore") can implement the same
`sc::iworkspace_session` interface later — but they are not stubbed, scaffolded, or speculated
about beyond that. The abstraction earns its genericity by being extracted from one real, working
provider (`GitLfsWorkspaceSession`, already drafted and reviewed — see `sc_git_lfs_provider.hpp`
in this directory), not designed top-down before any provider existed. If a second provider is
ever added and it needs a concept this interface doesn't have, extend the interface then, against
real requirements — don't pre-guess Perforce's or Lore's needs today.

## Why v1.3, not v1.2 verbatim

v1.2 was written in isolation from this codebase's real conventions. Since then, a concrete,
CLI-driven Git+LFS provider was actually built against it, and E29's own architecture (command
bus, Idle Work system, Asset Tree UI) was surveyed in detail. Three things fell out of that:

1. **The provider and the v1.2 spec disagree on some type shapes** — most notably `LockInfo` is
   defined twice, differently, and `LockAuthority` (spec) vs `EnforcementAuthority` (provider) are
   near-duplicate enums from two separate drafting passes. Part I below reconciles these instead
   of carrying the collision forward silently.
2. **The provider introduced real concepts the spec never had** — `ObservationFreshness` (was
   this fact locally inferred, or server-confirmed?) turned out to matter in practice and is
   folded into the canonical vocabulary here.
3. **v1.2's "frozen kernel"** (`Task<T>`, `CancellationToken`, `FakeWorkspaceSession` coroutine
   test harness) **was never implemented anywhere in this repo**, and E29 turns out to already
   have a working answer to the problem that kernel was solving. Part III explains why it's
   dropped.

---

## Part I — Identity & policy vocabulary

Reproduced in full (not by diff) since v1.1 no longer exists to diff against. Carried forward from
v1.2 essentially unchanged except where noted.

```cpp
namespace sc
{
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

    // PathRules must precede WorkspaceInfo — WorkspaceInfo holds it by value.
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
}
```

### Lock vocabulary (reconciled)

v1.2 defined a rich `LockInfo` for a generic multi-provider lock record. The concrete Git
provider, built independently, defined its own much simpler `LockInfo` (just `lfsPath`,
`ownerName`, `ownedByCurrentUser`) plus a separate `CoordinationState` wrapper with its own
`EnforcementAuthority`/`ObservationFreshness` enums that v1.2 never had. **These do not merge
cleanly — v1.3 picks one canonical shape and retires the rest:**

```cpp
namespace sc
{
    enum class LockScope : std::uint8_t
    { Unknown, RepositoryPath, BranchPath, StreamPath, ChangePath, WorkspacePath };

    enum class LockOwnership : std::uint8_t
    { Unknown, CurrentUser, OtherUser, Administrator };

    // Canonical authority enum. Replaces the provider file's separate
    // `EnforcementAuthority` (None/LocalConvention/RepositoryServer) outright —
    // that was a near-duplicate from a separate drafting pass. `PublishEndpoint`
    // is kept because it is a real, distinct case for future providers (a submit
    // server distinct from the repository server, e.g. some Perforce/streaming
    // setups) even though Git never produces it.
    enum class LockAuthority : std::uint8_t
    { Unknown, LocalConvention, RepositoryServer, PublishEndpoint };

    enum class LockReleaseReason : std::uint8_t
    { Unknown, ManualUnlock, Submit, LeaseExpiry, AdministrativeBreak, ProviderPolicy };

    // New in v1.3 — promoted from the provider's own `CoordinationState`, which
    // proved genuinely useful and has no v1.2 equivalent: was this fact read
    // straight from local working-copy state, or confirmed by a round trip to
    // the server? Git's own `git lfs locks` is always a server round trip
    // (RemoteConfirmed); a hypothetical local-only status read is
    // LocalWorkingCopy. Callers use this to decide whether a stale answer is
    // acceptable or a fresh round trip is needed before acting on it.
    enum class ObservationFreshness : std::uint8_t
    { Unknown, LocalWorkingCopy, RemoteConfirmed };

    enum class CoordinationEffectFlags : std::uint32_t
    {
        None                   = 0,
        VisibleToOtherUsers    = 1u << 0,
        PreventsOtherPublish   = 1u << 1,
        PreventsConcurrentEdit = 1u << 2 // never set by the Git provider — see its own file header
    };
    [[nodiscard]] constexpr CoordinationEffectFlags operator|(CoordinationEffectFlags a, CoordinationEffectFlags b) noexcept
    { return static_cast<CoordinationEffectFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); }
    constexpr CoordinationEffectFlags& operator|=(CoordinationEffectFlags& a, CoordinationEffectFlags b) noexcept
    { a = a | b; return a; }
    [[nodiscard]] constexpr bool HasFlag(CoordinationEffectFlags v, CoordinationEffectFlags f) noexcept
    { return (static_cast<std::uint32_t>(v) & static_cast<std::uint32_t>(f)) == static_cast<std::uint32_t>(f); }

    // The one canonical LockInfo. A provider populates whichever fields it can
    // actually answer and leaves the rest at these documented, honest defaults
    // — it must never fabricate a value it cannot observe.
    struct LockInfo
    {
        LockId        id;
        RepoPath      path;
        LockScope     scope     = LockScope::Unknown;
        LockOwnership ownership = LockOwnership::Unknown;
        LockAuthority authority = LockAuthority::Unknown;
        ObservationFreshness freshness = ObservationFreshness::Unknown;

        // Renamed from v1.2's `guarantees`: this is an OBSERVED FACT reported
        // by the provider, not a promise. A host-side policy evaluator (not
        // part of this layer) decides whether these effects satisfy some
        // requested guarantee — the provider only reports what it saw.
        CoordinationEffectFlags effects = CoordinationEffectFlags::None;

        std::string ownerIdentity;
        std::string ownerDisplayName;
        std::optional<WorkspaceId> ownerWorkspace;
        std::optional<ChangeId>    ownerChange;
        std::string comment;
        std::optional<std::chrono::system_clock::time_point> acquiredAt;  // Git+LFS CLI cannot report this — left empty
        std::optional<std::chrono::system_clock::time_point> expiresAt;   // Git+LFS locks don't expire — left empty
        bool canUnlock = false;
        bool canSteal  = false; // Git+LFS CLI has no steal concept — left false
        bool canBreak  = false; // Git+LFS CLI has no admin-break concept — left false
        LockReleaseReason expectedRelease = LockReleaseReason::Unknown;
    };

    // Per-file result of a coordination attempt (PrepareEdit). Replaces the
    // provider's own `CoordinationState` name with the same shape, now using
    // the reconciled `LockAuthority`/`ObservationFreshness` above instead of
    // the retired `EnforcementAuthority`.
    struct CoordinationState
    {
        CoordinationEffectFlags effects   = CoordinationEffectFlags::None;
        LockAuthority           authority = LockAuthority::Unknown;
        ObservationFreshness    freshness = ObservationFreshness::Unknown;
        std::optional<LockInfo> lock;
    };
}
```

---

## Part II — `sc::iworkspace_session` abstract interface

A pure-virtual abstract class, matching this codebase's own established convention for "one
shape, many concrete implementations" (`xundo::command_base` / `xundo::query_command_base`,
overridden by every command in `commands/E29_Commands_*.h`). `GitLfsWorkspaceSession` derives from
this directly — no adapter layer — because its nine methods already match 1:1 in name and shape.
Every future provider (Perforce/SVN/Lore) derives from it the same way.

The request/result types below are unchanged in *shape* from the working Git provider draft — this
is a relocation from `sc::git_lfs` into provider-agnostic `sc::`, not a redesign, because they are
already proven against a real CLI provider and nothing in them is git-specific.

```cpp
namespace sc
{
    struct WorkspacePath { std::filesystem::path relative; };

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

    struct FileStatus
    {
        WorkspacePath path;
        bool staged      = false;
        bool modified    = false;
        bool untracked   = false;
        bool conflicted  = false;
        bool lfsTracked  = false; // == "binary/non-mergeable/lock-required" — see Part IV
    };

    struct StatusRequest { std::vector<WorkspacePath> paths; };
    struct StatusResult  { std::vector<FileStatus> files; std::optional<Error> error; };

    struct AddRequest    { std::vector<WorkspacePath> paths; };  using AddResult    = BatchResult<WorkspacePath>;
    struct RemoveRequest { std::vector<WorkspacePath> paths; };  using RemoveResult = BatchResult<WorkspacePath>;
    struct RevertRequest { std::vector<WorkspacePath> paths; };  using RevertResult = BatchResult<WorkspacePath>;

    struct SyncRequest  { bool fastForwardOnly = true; };
    struct SyncResult   { bool succeeded = false; std::optional<Error> error; std::string summary; };

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

    struct SessionCapabilities
    {
        bool sourceControlAvailable = false; // renamed from the provider's git-specific `gitAvailable`
        bool lockingBackendAvailable = false; // renamed from `lfsAvailable`
        bool lockingAvailable        = false; // renamed from `lfsLockingAvailable`
    };

    // -------------------------------------------------------------
    // The abstract interface itself.
    // -------------------------------------------------------------
    class iworkspace_session
    {
    public:
        virtual ~iworkspace_session() = default;

        [[nodiscard]] virtual std::optional<Error> Connect() = 0;
        [[nodiscard]] virtual const SessionCapabilities& Capabilities() const noexcept = 0;

        [[nodiscard]] virtual StatusResult      GetStatus  (const StatusRequest&)      = 0;
        [[nodiscard]] virtual PrepareEditResult PrepareEdit(const PrepareEditRequest&) = 0;
        [[nodiscard]] virtual AddResult         Add        (const AddRequest&)         = 0;
        [[nodiscard]] virtual RemoveResult      Remove     (const RemoveRequest&)      = 0;
        [[nodiscard]] virtual RevertResult      Revert     (const RevertRequest&)      = 0;
        [[nodiscard]] virtual UnlockResult      Unlock     (const UnlockRequest&)      = 0;
        [[nodiscard]] virtual SyncResult        Sync       (const SyncRequest&)        = 0;
        [[nodiscard]] virtual SubmitResult      Submit     (const SubmitRequest&)      = 0;
        [[nodiscard]] virtual PushResult        Push       (const PushRequest&)        = 0;
    };
}
```

`GitLfsWorkspaceSession` (Phase 1) adds `: public sc::iworkspace_session`, marks the original nine
methods `override`, and its three `SessionCapabilities` fields (`gitAvailable`/`lfsAvailable`/
`lfsLockingAvailable`) are renamed to the provider-neutral names above — the rename is mechanical,
not a behavior change.

**Amendment (found during Phase 2, not anticipated when this spec was first written):** the
original draft only ever released a lock as a side effect of `Submit`'s own `keepLocks=false`
cleanup, and only ever pushed as the last step of `Submit`'s commit. Wiring the real
`unlock_query_cmd`/`push_query_cmd` CLI commands surfaced that both are genuinely needed standalone
— a user releasing a lock without committing, or retrying a push after `Sync` resolved what blocked
it, with nothing new to commit. Two methods were added to close this gap: `Unlock(UnlockRequest)`
(paths + a `force` flag, mirrors `git lfs unlock --force`) and `Push(PushRequest)` (`PushRequest` is
empty — push always means "whatever is already committed locally"; `PushResult` carries
`succeeded`/`error`/`remoteRevision`/`summary`). `GitLfsWorkspaceSession::Push` and `Submit`'s own
push step now share one `ClassifyPushFailure` helper (returns a recovery action, an `ErrorCode`, a
message, and whether remote state is ambiguous, from one string match) so the two can never
disagree about what a given `git push` failure means.

---

## Part III — What v1.3 drops, and why

v1.2 referenced a "frozen kernel" with `Task<T>`/`CancellationToken`-based async operations and a
`FakeWorkspaceSession`/`FakeScenario` coroutine test harness with barrier-based pause/resume
semantics for deterministic cancellation testing. **None of that kernel exists anywhere in this
repository.** It was aspirational — designed but never built, in a conversation that predates this
one and whose full text (v1.1) is lost.

v1.3 drops it in full, for a concrete reason: **E29 already has a working answer to the problem
that kernel was solving.** The Idle Work system (`kit/E29_IdleWork.h`) dispatches long-running
background work via `xscheduler::g_System.SubmitLambda(..., priority::LOW, complexity::HEAVY)` —
its first real consumer, `LaunchSceneSanityScan`, is a full disk walk, the same shape of work as a
`git status`/`git lfs locks` call. This already keeps the UI thread unblocked without any
cooperative-cancellation machinery. A synchronous, blocking `iworkspace_session` call issued from
inside an Idle Work lambda gets exactly the same non-blocking-UI property the async kernel was
built to provide, using infrastructure that already exists and is already proven in this app.

The one thing the async kernel would add that E29's scheduler doesn't — mid-operation
cancellation of a hung subprocess (e.g. `git` blocked on a credential prompt) — is a real, known
gap, but it is `sc_process_runner.hpp`'s own documented limitation already, not something the
async kernel actually solved either (the kernel spec describes cancelling the *awaiting caller*,
not killing the underlying OS process, which still needs the platform-specific process-handle work
`sc_process_runner.hpp`'s header already flags as deferred). Building a coroutine-cancellation
runtime today would add real complexity to solve a problem it doesn't actually solve, in service
of a caller that doesn't exist yet. If a future provider genuinely needs true async (e.g. a
network API with its own async SDK), revisit this then, against that provider's real constraints.

---

## Part IV — Mapping onto E29's real conventions

This is the actual point of doing a v1.3 pass: grounding the abstraction in how E29 already works,
not leaving it as a floating generic layer.

### Status refresh → Idle Work

A new periodic task, registered the same way `LaunchSceneSanityScan` is: bracketed by
`BeginIdleTask`/`EndIdleTask`, submitted via `xscheduler::g_System.SubmitLambda(...,
priority::LOW, complexity::HEAVY)`, gated by the same 30-second `idle_threshold_seconds_v` trigger
inside `PumpIdleWork`. A manual CLI bypass mirrors the existing `run_sanity_check_query_cmd`
pattern, so a script or an AI can force an immediate refresh without waiting for the idle gate.

### Mutating operations → E29's command bus, `query_command_base`-shaped, never `xundo::command_base`

E29's undo system (`xundo::command_base`, with `Redo()`/`BackupCurrenState()`/`Undo()`) models
*local, reversible state edits* — its whole contract is "capture enough state before Redo to
restore it exactly on Undo." Commit, Pull, and Push are none of that: they are real round trips to
a server that already has its own history (git's own commit graph, revert, reflog). Lock and
Unlock are server round trips too, not local-state edits. This project already has a precedent for
exactly this distinction: `EmptyTrashcan` (real, irreversible disk deletion) was deliberately
excluded from the undo system entirely rather than forced into a shape that doesn't fit it. Source
control's mutating operations follow the same reasoning and all subclass
`xundo::query_command_base` — invokable from the UI, from a script, or from an AI via the CLI, but
never entered into the local Undo/Redo history.

### Asset Tree status icons → optional callback hooks (Phase 3, landed)

`WrappedButton2` (`E10_asset_browser_virtual_tree_tab.h`) already had exactly this shape for its
pre-existing "unsaved in editor" tick: a plain `bModified` bool threaded through the function
signature. Two new, independent optional hooks were added following the same established
convention (already used for `entity_inspector_bridge`'s callbacks and the Asset Browser's own
command hooks) — default-empty, so the other 7 consumers (E10, E19–21, E23–25, E28) render
identically to before; only E29 wires them, to the Phase 2 status/lock caches:
`assert_browser::m_OnGetAssetStatusBadge` and `m_OnGetAssetLockBadge` (`E10_AssetBrowser.h`).

Two *independent* hooks, not one combined value — a file can be both modified and locked by you at
once, and the lock signal must never be hidden by whichever status also happens to be true.

**Tile decoration zones** (direct user design, converged after several rounds): the LEFT edge is
reserved for source-control icons, the TOP edge for runtime/editor info (the pre-existing unsaved-
in-editor tick, moved from the corner to centered top), and the CORNERS stay reserved/unused for a
future signal that's genuinely about *both* at once. Fixed positions, not a collapsing layout —
users expect a given signal to always live in the same spot.

**Icon table** (all drawn with plain `ImDrawList` primitives — never a font glyph. A first attempt
used an unverified Segoe MDL2 codepoint for the lock shape and it rendered as a huge, wrong
tofu/fallback glyph engulfing the whole tile, confirmed live via an A/B screenshot rebuild —
primitives sidestep the "which codepoint actually exists in this font" gamble entirely):

| Concept | Symbol | Color | Position |
|---|---|---|---|
| Locked by you | 🔒 padlock | Red | Left edge, upper (primary — "an important feature") |
| Locked by someone else | 🔒 padlock | Grey | Left edge, upper |
| Not tracked / never scanned | *(nothing)* | — | — |
| Untracked (scanned, git doesn't know this file) | **+** | Green | Left edge, lower |
| Tracked, clean | ✓ checkmark | Green | Left edge, lower |
| Tracked, modified | ● dot | Gold/orange | Left edge, lower |
| Unsaved in editor (pre-existing, unrelated to git) | tick glyph | Green | Top edge, centered |

Untracked/Clean/Modified share ONE slot (left edge, upper) rather than each claiming their own —
direct user reasoning: they're mutually exclusive by construction (a file has exactly one git
status at a time), so "no information yet" (blank) is meaningfully different from "checked, and
it's new" (+), which would otherwise look identical. Colors follow Visual Studio's own Git palette
for familiarity, but polarity is deliberately inverted from VS's own convention (which draws
nothing for a clean file) — this is an asset-heavy project where plenty of files may never be
meant for source control at all, so a positive "this was actually checked" signal was judged more
useful than VS's own "silence means fine" default.

### Binary/text classification → `.gitattributes` + `git check-attr filter`

This is the concrete mechanism behind "the tree/files must know which files are binary vs text":
a file is LFS-tracked (per `.gitattributes`) or it isn't. LFS-tracked = binary, non-mergeable,
locking required before edit. Not LFS-tracked = text, mergeable, no lock needed — git's own
line-based diff/merge already handles it. The provider's `BatchIsLfsTracked` (already implemented,
batches via `git check-attr -z --stdin filter` so it's one subprocess call for N paths, not N) is
the query; `FileStatus::lfsTracked` is where the answer lands. No new classification logic is
needed — only a real `.gitattributes` for `example.lionprj` (Phase 1) and wiring the existing
query into the status cache (Phase 2).

---

## Part V — Open questions

Carried as a living list, not resolved by guessing:

1. **Build wiring** for a new console smoke-test target under `plugins/source_control/smoke/` —
   the exact `Build\xGPUExamples.vs2022\...` project-file mechanics haven't been checked yet.
2. **The exact E29 entry point for "open this binary asset for editing"** that lock-gating
   (Phase 4) must intercept — not yet located.
3. **Icon glyph availability** for the new lock-mine/lock-other/clean-tick overlays — needs a
   screenshot-verified check against the loaded icon font before Phase 3, per this project's own
   established rule that a codepoint is never trusted without a screenshot.

---

## Summary of naming changes from the working Git-provider draft

For anyone diffing this spec against `sc_git_lfs_provider.hpp` as it stood before Phase 1's port:

| Provider draft (namespace `sc::git_lfs`) | v1.3 canonical (namespace `sc`)         | Why |
|---|---|---|
| `LockInfo` (lfsPath/ownerName/ownedByCurrentUser) | folded into canonical `sc::LockInfo` fields (`path`, `ownership`, `ownerIdentity`) | name collision with v1.2's richer `LockInfo` |
| `CoordinationState::authority` : `EnforcementAuthority` | `CoordinationState::authority` : `LockAuthority` | near-duplicate enum retired in favor of the richer one |
| `SessionCapabilities::gitAvailable` | `sourceControlAvailable` | provider-neutral name |
| `SessionCapabilities::lfsAvailable` | `lockingBackendAvailable` | provider-neutral name |
| `SessionCapabilities::lfsLockingAvailable` | `lockingAvailable` | provider-neutral name |

Everything else carries over unchanged in shape.
