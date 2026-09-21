# E29 Asset Browser command layer

> E29's Asset Browser command/undo layer (CreateAsset/RenameAsset/MoveAsset/DeleteAsset/RestoreAsset/ListAssets/DescribeAsset) plus opt-in UI hooks on the shared browser panel - landed 2026-09-10; MakePrefab/MakePrefabVariant (E29_Commands_MakePrefab.h) built on top of it the same day
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-09).

Direct user framing that started this: "I think make prefab or create prefab instance may depend on
the asset browser... The next big task is going to be the asset browser... We need to make it work
with commands as well so it can be added into a global undo system." Make Prefab itself
(kit/E29_PrefabAuthoring.h) was deliberately deferred from the prior gap-closing session (see
[E29 command / undo - known gaps (closed)](command_undo_known_gaps.md)) because it calls `AssetMgr.NewAsset` to create a real asset on disk,
and nothing in E29's command system had ever had to reverse an asset-library creation. This work
builds that foundation - Make Prefab itself is still not implemented, deliberately, and is the next
natural follow-up now that CreateAsset/etc exist to compose with.

**Two commits, `e56af26` (command layer) and `96c12ba` (UI wiring)** - see their own messages for full
detail. High-level shape:

- New file `commands/E29_Commands_AssetBrowser.h`: `ListAssets`/`DescribeAsset` (discovery, walk
  `e10::library_mgr`'s tree via the existing `getInfo`/`getNodeInfo` accessors), `RenameAsset`,
  `MoveAsset`, `DeleteAsset` (soft-delete via Trash, fully reversible), `RestoreAsset` (a genuine
  forward action distinct from DeleteAsset's Undo - the Trash UI's "Restore to X" can target a
  DIFFERENT parent than the one deleted from), `CreateAsset` (undo-routed, pre-minted instance guid
  same "-Id pre-minted by the caller" convention `create_entity_cmd`/`instantiate_prefab_cmd` use),
  `SaveAssets`. An asset guid is a genuine `xresource::full_guid` (instance+type, unlike Scene/Level's
  single-type `def_guid`) - new `FormatAssetGuid`/`ParseAssetGuid` (32 hex: 16 instance + 16 type,
  E29_CommandContext.h), plus `FormatLibraryGuid`/`ParseLibraryGuid` for `e10::library::guid`.
- Scoped entirely to E29's own `commands/` directory - `E10_AssetMgr.h`'s `library_mgr` itself is
  untouched, since it's shared by 8 examples (E10, E19-E21, E23-E25, E28, plus E29).
- UI wiring (2nd commit): optional callback hooks added to `e10::assert_browser`
  (`E10_AssetBrowser.h`) - `m_OnRenameAsset`/`m_OnMoveAsset`/`m_OnDeleteAsset`/`m_OnRestoreAsset`/
  `m_OnCreateAsset`, default-empty. `E10_asset_browser_virtual_tree_tab.h`'s own ~13 real mutation
  call sites now check the hook first, call it INSTEAD of `library_mgr` directly when set. E29
  registers handlers (`RegisterAssetBrowserCallbacks`, E29_LevelSceneEditorKit.h) that `Run()` the
  matching command. Confirmed additive/safe by the build itself succeeding across all 8 consumers.

**Two real bugs found via live E29CLI testing (see `xgpu_ui_automation_friction` (note pending migration) for why CLI, not
UI clicks, was the verification path):**
1. `ListAssets` initially showed a trashed asset under BOTH its old folder and Trash -
   `MoveToTrash` only tags the asset's own `m_RscLinks`, it never unlinks the old parent's own
   `m_lChildLinks` entry. Fixed by filtering on the trash tag (same signal `MoveToTrash`'s own source
   checks internally: `m_RscLinks.front() == trash_guid_v`).
2. `CreateAsset`'s Redo-after-Undo hit a real assert: `NewAsset` writes its `info.txt` to disk
   IMMEDIATELY and unconditionally (unlike Rename/Move/Delete, in-memory only until a real
   `SaveAssets`), so "this asset already exists" (found via `getInfo`) does NOT necessarily mean
   "this asset is currently trashed" - an asset created by an EARLIER process/session (its trashing
   only ever in-memory, never saved) can be found already-existing-but-not-trashed on a fresh
   restart. Calling `MoveFromTrashTo` on a non-trashed node hits its own
   `m_RscLinks[0] == trash_guid_v` assert. Fixed by checking the node's current trashed-or-not state
   before deciding whether Redo needs `MoveFromTrashTo`, `NewAsset`, or is already a no-op.

**Known limitation, not resolved**: real UI click-through for the new hooks could not be verified
interactively - this ImGui app's keyboard/text-field input isn't reachable via synthetic Win32 input
in this environment (confirmed live: neither `keybd_event`-style key events nor `WM_CHAR` reach
ImGui's own polled input), so the Asset Browser's Search tab couldn't be filtered down to click a
specific item. Confidence rests on a clean build across all 8 consumers + the wiring being a trivial
conditional swap calling the exact same functions the direct path already called + every invoked
command already CLI-verified correct.

**Follow-up, landed the same day**: `MakePrefab`/`MakePrefabVariant` (new file
`commands/E29_Commands_MakePrefab.h`) compose this layer's `CreateOrRestoreAsset` (made a free
function, shared rather than duplicated) with `kit/E29_PrefabAuthoring.h`'s existing
`CreatePrefabFromGroupRoot`/`CreatePrefabVariantFromInstance` - bracketed as ONE undo step each, not
two adjacent history entries (the open question this memory used to flag). `MakePrefab`'s Undo reuses
`SnapshotSubtreeForRestore`/`RestoreSubtreeFromSnapshot` (factored out of `delete_entity_cmd`,
E29_Commands_EntityLifecycle.h) to restore the original entity subtree, then trashes the created
asset - `MakePrefabVariant`'s Undo instead fully serializes/restores the old `prefab_instance`
component state (no delete/recreate, matching its own fast-path design). Verified live via E29CLI:
full Redo/Undo/Redo cycles for both a childless entity and a root+child (Scene-Prefab) group, plus
MakePrefabVariant, each checked via `DescribeAsset`/`ListEntities` at every step.

**Real engine bug found and fixed while verifying this - see `xecs_copyentity_oob_childless_prefab` (note pending migration)
for full detail**: converting a childless entity crashed inside the xECSV2 dependency itself
(`xecs::pool::instance::CopyEntity`), not in any E29 code. Fixed upstream and pushed to the `xECSV2`
repo separately from this `xGPU` commit.
