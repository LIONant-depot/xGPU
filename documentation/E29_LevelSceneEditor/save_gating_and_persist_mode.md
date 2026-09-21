# E29 Save gating and persist_mode unification

> E29 fixes landed 2026-09-07 from an external review's two data-loss-risk findings - Save is now gated during Play/Pause, and edit-mode code reloads no longer silently write to disk
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-07).

Landed same day as `e29_gamedll_pdb_embedded_path_lock` (note pending migration) and [Scene save hardening (orphan check + atomic descriptor write)](../../dependencies/xECSV2/doc/scene_save_hardening.md),
in response to an external AI review of E29 that correctly flagged two real, verified (not assumed)
gaps - direct user framing: "none of them seem like a rework/reframe... seems like edge issues," and
asked to fix them if necessary rather than just document.

1. **Save was never gated during Play/Pause.** `E29_LevelScene_Editor.cpp`'s File>Save menu item and
   Ctrl+S shortcut both called `SaveEverything` unconditionally. Since V1 (the disk save Stop reverts
   to) IS that same file, saving mid-play-session silently overwrote the revert point with in-flight
   play-mode mutations - one Save + Stop would "revert" to the mutated state, not the pre-Play one.
   Fixed: both gated behind `!State.isPlaying()` (`ImGui::BeginDisabled` for the menu item, a plain
   `&&` guard for the Ctrl+S check) - same disabling pattern already used for Pause/Stop buttons
   elsewhere in this file.

2. **Edit-mode code-triggered reloads silently wrote to disk.** `persist_mode::DiskSaveAndReload` was
   used whenever a Game.dll rebuild happened while NOT playing (e.g. tabbing back into the editor
   after an unrelated header/game-code edit) - it called `SaveEverything` unconditionally as part of
   the reload, with no explicit user Save action. Neither Unity's domain reload nor Unreal's Live
   Coding persists anything to the real project this way. Fixed by REMOVING `DiskSaveAndReload`
   entirely (not gating it - it had exactly one caller and was structurally unnecessary once fixed) -
   `persist_mode` is now just `{RawSnapshotBridge, RestoreFromV1}`. Every code-triggered reload
   (Playing, Paused, or plain edit-mode) now goes through the SAME `RawSnapshotBridge` path already
   proven this session for Play/Pause reloads (in-memory Vn snapshot + `CaptureOpenScenes`/
   `ReattachOpenScenes` for full tree fidelity, never touching disk) - a straight subtraction, not a
   new code path. The one place that used to get V1 "for free" as `DiskSaveAndReload`'s side effect
   (`PollGameReload`'s `Rebuilt` branch, when a Play was ALSO requested) now writes it explicitly,
   matching the pattern the `UpToDate` branch already used.

Both fixes verified: clean rebuild of `xGPU_unit_test`/`xECSV2`/`E29_Game` (all three, since the
registry fix from `e29_gamedll_pdb_embedded_path_lock` (note pending migration)'s session also needed a fresh `xECSV2.dll`),
and a live launch showing both `Game: Spin System` and the user's own `Game: Spin2 System` (added
during their own live testing of the add-a-system-then-reload flow) registered correctly - confirms
the `RawSnapshotBridge`-only reload path still works end to end. UI-level confirmation of the actual
Save-menu greyed-out state was not obtained (synthetic click on the File menu didn't land reliably -
matches `xgpu_ui_automation_friction` (note pending migration)'s standing note) - confidence instead comes from this being
the exact same `ImGui::BeginDisabled` pattern already proven working for the Pause/Stop buttons in
the same file, not a novel mechanism.

Other findings from the same review, assessed but NOT acted on:
- `info_v`/BitID/pointer-identity concerns - valid as a recurring bug *pattern* (this session fixed
  two separate instances of it - see `e29_gamedll_pdb_embedded_path_lock` (note pending migration) and the earlier
  `m_ComponentInfoMap` stale-pointer fix), but not a single open item to close.
- Stale-check is `E29_Game.cpp` mtime only (header edits don't trigger a rebuild) - real, low-stakes
  while the sample is one file, not addressed.
- Fixed (non-generation-suffixed) Vn bridge temp path - real, narrow edge case (two concurrent editor
  instances), not addressed.
- Prefab claims ("no Prefab Mode," "make-prefab is delete+recreate") - could not verify against code
  this session, not assessed either way.
