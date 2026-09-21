# E29 Play mode - keeping property tweaks on Stop

> E29 Stop asks to keep property edits made during Play (CLI: -Keep true|false), merges them as ONE undoable group - researched Unity/Unreal/Godot precedent first, landed 2026-09-12, verified live via CLI
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-12).

Follow-up to an external review's "Undo stack isn't cleared on Play/Stop" finding
(`e29_unity_theme_pass` (note pending migration)'s own review chain). Root cause turned out broader than the review's own
framing: `isPlaying()` gating exists ONLY on `Undo`/`Redo`/`Save` (and their Ctrl+Z/Ctrl+S shortcuts) -
none of the real Edit commands (`SetProperty`, `CreateEntity`, etc.) check it at all, so a live
Inspector tweak made while Playing pushes a real xundo entry referencing play-session-only state that
Stop's `RestoreFromSnapshot`/`OpenLevel` then discards - Ctrl+Z after Stop could land on a stale,
now-meaningless entry.

**Researched industry precedent before designing** (direct user request - "does unity, unreal, or
godot support this feature... we are looking for a clean workflow to adopt"):
- **Unreal**: real, first-party, built-in - "Keep Simulation Changes" (K shortcut / right-click,
  while still Simulating/Playing). Per-selected-actor, immediate, only works on actors that already
  existed in the level before simulating. This is the precedent adopted.
- **Unity**: no built-in equivalent - community plugins exist (e.g. inkle's
  Unity-Save-Play-Mode-Changes, JSON-serializes whole GameObject hierarchies) precisely because Unity
  never shipped an answer.
- **Godot**: no support at all: open, unresolved feature request (godot-proposals#9142).

**Design decision, confirmed by user**: automatic on EVERY Stop, not an explicit "Keep" action
("the reality is the user would not make changes often, and when they do it was because it was a
surprise finding" - i.e. never make them remember to opt in). Scoped to property edits only
(`SetProperty`), never structural changes (Create/Delete/AddComponent etc.) - matches Unreal's own
"pre-existing actors only" restriction, and this project's own explicit call ("we would only accept
property tweaks").

**Mechanism** (no new serialization, no `xproperty::collection`, no temp file - all discovered to be
unnecessary once traced through the actual architecture):
- `xundo::system::GetHistoryCommandString(i)` already returns the ORIGINAL command text for any
  history entry - "what changed" is just re-reading text this same codebase already stores, not a new
  diff/snapshot mechanism.
- V1 (the Play-entry disk save Stop reverts to, `e29_playmode_v1_vn_snapshot_design` (note pending migration)) is a REAL
  Scene/Level save, so Stop's restore is an ordinary `OpenLevel` - nothing exotic to reapply against.
- `editor_state::m_PlayHistoryBoundary` = `Undo.GetUndoIndex()` recorded at all 3 real Play-entry call
  sites (`PollGameReload`'s two branches, `E29_LevelScene_Editor.cpp`'s non-shared-build direct path,
  `play_query_cmd`'s CLI path) - NOT touched on Paused->Playing resume (same session, not a new
  boundary).
- `StopPlaySession` (E29_PlaySession.h): `CollectPlayModePropertyTweaks` walks history from that
  boundary, keeps only `SetProperty` entries, dedupes by (Scene,Id,Component,Path) keeping the
  FIRST-seen `-Before` (== the true pre-Play value, since Play always writes V1 before anything can
  change) and the LAST-seen `-After` (final tweaked value) - a property dragged back and forth
  collapses to one entry, not a replay of every intermediate step.
- `Undo.JumpTo(boundary)` unwinds every play-session entry for real (cheap, world's about to be
  replaced anyway) - this also incidentally closes the ORIGINAL hazard for every OTHER command type
  that ran during play, not just SetProperty, since JumpTo runs real Undo() logic on all of them.
- Normal Stop restore runs unchanged, THEN `Undo.TruncateRedoBranch()` (new small public method,
  wraps the already-existing private `PruneHistory()`) discards the stale tail EVEN WHEN NOTHING GOT
  REAPPLIED (the common case - most Play sessions have zero edits) - without this, the empty-edit case
  would leave the play-session entries sitting Redo()-able, reopening the same hazard from the other
  direction.
- Each deduped tweak is replayed as a BRAND-NEW `SetProperty` via the normal `e29::commands::Run`
  against the just-restored scene - same code path as any manual edit, so it's individually undoable,
  gets prefab-override bookkeeping for free, and an entity that didn't survive Stop (created only
  during Play) just fails to resolve (silent no-op, matching Unreal's own restriction) with zero
  special-case code needed.

**Verified live via CLI** (E29CLI, not UI clicks - matches `e29_compilation_cli_commands` (note pending migration)'s own
established verification style): Play -> tweak Transform/Position/X twice (0->5->9) -> Stop -> value
correctly kept at 9 (the LAST value, not 5 or the original 0) -> Undo correctly reverts to the TRUE
pre-Play value (0.0, proving the FIRST-seen Before survived the dedup, not the intermediate 5.0) ->
Redo correctly reapplies 9.0 -> confirmed exactly ONE undo/redo step exists either direction (no stale
play-session tail reachable). Separately verified a zero-edit Play/Stop cycle leaves no phantom
history entries and doesn't disturb earlier real history.

**Follow-up round 2, same day - added a real confirmation instead of silent always-keep**: direct
user request ("You made some changes you want to keep those?... For the AI this could be a parameter
in the stop command"). `RequestStop()` (E29_PlaySession.h) is now the single decision point for BOTH
triggers - the menu-bar Stop button (always `std::nullopt`, opens `RenderKeepTweaksModal`'s real
ImGui popup and freezes the world via Paused until answered) and the CLI `Stop -Keep true|false`
(answers up front, same "-Force" reasoning as the Asset File commands' own dialog-bypass flag -
"there is no dialog to click"). Zero-edit Stop still short-circuits with no prompt at all. Explicitly
scoped OUT of this pass, per direct user confirmation: Unreal-style per-entity/per-property selection
- "we can always add that later, the core system is in place now."

**Follow-up round 3 - grouped into ONE undo step**: direct user correction - "the user says yes I want
to merge... that step should be undoable" (as one step, not one Ctrl+Z per property, since the user's
"keep" answer is itself one decision). Switched from N separate `commands::Run()` calls to ONE
`commands::RunGroup()` call (xundo's existing `Execute(group_name, vector<string>)` - the same
primitive already used by multi-item asset Delete/Paste). Real bug found and fixed by this change:
xundo's group `Execute()` aborts the ENTIRE group (pushes NO history entry at all) the moment ANY one
sub-command's `Redo()` fails - an entity created only during Play won't exist after Stop's restore, so
a single stale target mixed in with otherwise-valid kept properties would silently swallow all of
them. Fixed via a new `FilterSurvivingTargets()` pass (checks `GameMgr.m_SceneMgr.Find(...)` +
`m_LocalToRuntime.contains(Id)` per command) run BEFORE `RunGroup`, so the group only ever contains
commands guaranteed to succeed. `Stop` itself (the Playing->Stopped transport transition) still isn't
undo-routed - same reasoning `stop_query_cmd`'s own comment already gives - only the merged property
VALUES are, as one atomic group.

User's own explicit take on the "is a group really necessary" tradeoff, worth preserving verbatim:
"my initial worry was scale... but something that we can change later is not critical." The choice is
deliberately isolated to one line in `StopPlaySession` (`RunGroup(...)` vs. a loop of individual
`Run()` calls) - cheap to revisit if a long Play session's worth of scattered tweaks ever makes "one
big Ctrl+Z" feel wrong in practice.

Verified live (CLI): zero-edit Stop completes with no prompt; a single tweak with no `-Keep` opens the
modal and holds Stop (screenshot-confirmed: "Keep Play Mode Changes? / You changed 1 property while
Playing... / [Keep] [Discard]"); `-Keep true`/`-Keep false` both resolve headlessly with no prompt;
two properties tweaked together on one entity are kept/undone/redone as ONE atomic step (confirmed via
"Undone" then immediately "Nothing to undo" after a single Undo reverted both).

Landed as xGPU commit `d3ad6bb` (round 1: automatic always-keep) then `50a5d16` (round 2+3:
confirmation modal + grouped undo), plus xundo commit `371f130` (`TruncateRedoBranch()`) - all pushed.

See also `e29_playmode_v1_vn_snapshot_design` (note pending migration) (the V1/Vn snapshot architecture this builds on) and
[E29 command / undo system - phased plan and status](command_undo_system_plan.md) (the command bus/`RunGroup` this reuses for the actual "keep"
reapplication).
