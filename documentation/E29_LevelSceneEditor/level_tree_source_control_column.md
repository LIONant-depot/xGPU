# E29 Level Tree source-control column

> E29 Level Tree got its own leftmost SC status/lock badge column, matching Asset Tree/Source Control tab conventions; propagated to entities/folders/prefab instances per live user feedback
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-17).

Direct user request (2026-09-18): "Lets do the 'level Tree' left source control column. Similar to
the one we have done in all other views." Landed in `kit/E29_Panel_LevelTree.h` (already a real
`ImGui::BeginTable("LevelTree", ...)`, not plain indented TreeNodeEx calls) - added a new leftmost
"##SC" column (narrow, fixed 20px, unlabeled) ahead of the existing "Name"/"Actions" columns, bumping
the table from 2 to 3 columns. Required a mechanical mid-file column-index shift (every
`TableSetColumnIndex(0)`→`(1)`, `(1)`→`(2)`, ~12 call sites across Level/Scene/Dependencies/Folder/
Entity/Runtime row blocks) - done as two sequential `replace_all` passes (1→2 first, THEN 0→1, in
that order) to avoid double-shifting.

Reuses the EXACT SAME `e10::DrawSourceControlBadge`/`GetSourceControlTooltipText`
(`E10_AssetBrowser.h`) and `e10::source_control::GetCachedFileStatus`/`GetCachedLockStatus`/
`GetLastRefreshTime` (`E10_SourceControlCache.h`) + `e29::commands::ResolveLibraryRootPath`
(`E29_Commands_SourceControl.h`) the Asset Tree and Source Control tab already share - all three
views read identically. New helper `RenderLevelTreeSourceControlBadge(full_guid)` in
`E29_Panel_LevelTree.h` itself resolves "which library owns this resource" (no existing helper did
this - `getNodeInfo`'s own global-search overload loops every open library internally but never
surfaces which one matched) via a fresh per-library loop, then derives the real Descriptor.txt path
the same way `E10_asset_browser_virtual_tree_tab.h`'s own tile badges do (info.txt's path → sibling
Descriptor.txt → strip the owning library's root).

**Scope, per live user correction across 3 follow-up messages** (all landed same session):
- Level and Scene rows: their own real Descriptor.txt badge (obvious - initial implementation).
- "You forgot the entities": an Entity has no file of its own (a Scene is one committable file as far
  as git is concerned - the Source Control tab's own explicit design decision, see
  `e29_source_control_depot_tree_redesign` (note pending migration)) - so an Entity row shows its OWNING SCENE's own badge
  instead, making a scene's pending change visible drilled all the way down.
- Folder rows (the in-memory organizational nodes inside a scene) got the SAME scene-inherited badge
  too, for consistency (not explicitly asked, but same reasoning applies - avoided leaving an
  inconsistent gap the user would likely have flagged next).
- "Also the prefab instances": a prefab-instance ROOT entity (`FindPrefabInstance(...)` returns
  non-null) shows the linked PREFAB RESOURCE's own badge instead of the scene's - since a prefab
  instance references a REAL, separately-tracked file (its own git status/lock, independent of the
  scene it's placed in) - only one badge slot exists, so this replaces rather than adds to the
  scene's own badge. A non-root entity nested inside a prefab instance subtree (blue-tinted, but not
  itself `pPI`) still just shows the scene's badge.
- Dependencies rows and the synthetic Runtime row deliberately still show NO badge - no file identity
  of their own, matches the Source Control tab's own granularity decision.

**Real build bug hit**: the lambda passed to `getNodeInfo(LibraryGuid, FullGuid, callback)` was
marked `noexcept` - triggered the SAME known `function_traits` deduction failure documented in
[FindAsReadOnly and noexcept callbacks](../../dependencies/xcontainer/documentation/noexcept_lambda_trait_trap.md) (recurs anywhere a lambda goes into one of these
`FindAsReadOnly`-style template helpers) - fixed by simply dropping `noexcept` from that one lambda,
same established workaround.

**Verified live via screenshot** (PrintWindow(PW_RENDERFULLCONTENT) + a couple of careful synthetic
clicks to expand a scene and switch tabs - both worked reliably this time, contrary to
`xgpu_ui_automation_friction` (note pending migration)'s usual caution): green "untracked" plus-badges rendered correctly on
Main Level/Scene 1/Scene 2/their entities/a new folder; Dependencies and Runtime rows correctly bare;
cross-checked against the Source Control tab's own "Scenes & Levels (134): Main Level (2), Scene 1
(115), Scene 2 (17)" breakdown for the same project - counts matched, confirming the badge data is
real, not coincidental. No ImGui errors, no crash, app kept running throughout.

**Round 2 (live user testing, same session, after the user returned from a walk)**: a screenshot
surfaced 3 real problems, all fixed and re-verified across several rebuild/relaunch cycles:
1. Badges clipped/mispositioned + tree indentation missing entirely - both traced to the SAME real
   Dear ImGui table default (column 0 only gets tree-indent) plus a draw-order bug (SpanFullWidth
   row highlight painting over an earlier-drawn badge) - see [ImGui: table tree-indent applies to column 0 only](../ImGui/table_column0_indent_quirk.md)
   for the full technical finding. Fixed via explicit `IndentDisable`/`IndentEnable` per column and
   moving the badge draw to after the Name column's own TreeNodeEx.
2. Removed the redundant 3rd "Actions" column (a per-row Remove/X button) - every row kind that had
   one (Scene/Entity/Folder/a dependency entry) already offers the identical action via its own
   existing right-click context menu, direct user observation: "you should be able to right click to
   delete any of them."
3. Column/icon sizing went through several live iterations - halving the column also shrank the
   badge (unrequested scope creep, see the project's standing working rule), which then needed
   a scoped `CellPadding` override (theme's own 6px whole-table padding alone exceeded a 10px column),
   then a width bump, then a full revert of the badge size back to 12px (matching every other view)
   once the user explicitly called out the size mismatch. Final shipped state: "##SC" column = 21px
   (30% narrower than a 30px waypoint), badge = 12px (unchanged from the original, matching
   `E10_asset_browser_virtual_tree_tab.h`/`files_tab`), `CellPadding.x` = 0 for this table only
   (`Push/PopStyleVar`, not the shared theme default), `IndentSpacing` = 8 for this table only
   (half of `E29_Theme.h`'s global 16, also scoped via `Push/PopStyleVar`, not a global theme edit).

Committed as `dfc27de` ("E29: Level Tree source-control column"), pushed to `origin/main`.
