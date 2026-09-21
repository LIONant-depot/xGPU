# E29 command / undo - known gaps (closed)

> Known gaps in E29's xundo command system, surfaced by external AI review 2026-09-07 - ALL CLOSED, including Make Prefab (landed 2026-09-10, see [[e29_asset_browser_command_layer]]); Duplicate doesn't exist as a feature
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-09).

Surfaced by a second round of external-AI review of [E29 command / undo system - phased plan and status](command_undo_system_plan.md). Direct user
instruction 2026-09-07: "let make sure those items are recorded for later." Direct user instruction
2026-09-09, after this list plus "half the editor isn't undo-routed" were reported: "OK lets close
all the gaps... You have the wheel... and you should be able to test everything... go ahead."

**All items below are now CLOSED, verified live via E29CLI (not just build-verified)** - see each
item's own note for the exact test performed.

1. **Play-state gate on Undo/Redo/Save - FIXED.** `E29_LevelScene_Editor.cpp`'s Ctrl+Z/Y handler and
   `undo_query_cmd`/`redo_query_cmd` (`E29_Commands_Workspace.h`) now check `!State.isPlaying()`,
   matching the existing Save gate exactly. Verified: Play (via the new `Play` command, see below) →
   Undo/Redo/Save all correctly return "blocked while Play/Paused" → Stop → all three work again.

2. **`create_entity_cmd -Parent`'s Undo leaving an empty `children` component - FIXED.**
   `BackupCurrenState` now records whether the parent already had a `children` component BEFORE
   Redo; Undo strips it if Redo had to add it AND the list is empty after the child is removed.
   Verified via a real Save/Undo/Save disk round-trip: `nComponents` on the parent's `.entity` file
   dropped from 3 back to 2 and the `[Children]`/`[AllChildren]` blocks disappeared entirely after
   Undo - not just checked via DescribeEntity, which filters `children` as an internal component and
   couldn't have caught this on its own.

3. **Entity-reference assign/clear bypassing the command system - FIXED.** New file
   `commands/E29_Commands_EntityReference.h` - `SetEntityReference` command. Real design decision,
   not a mechanical wrap: Before/After are encoded as `{SceneGuid, permanent_id}` pairs, NEVER a raw
   `xecs::component::entity` runtime handle - same reasoning delete_entity_cmd's own top comment
   already established for parent/children/entity_reference fields (a raw handle is meaningless the
   instant its target is destroyed/recreated). `entity_inspector_bridge::m_OnEntityReferenceRender`
   (E29_LevelSceneEditorKit.h) now calls `e29::commands::Run(Undo, "SetEntityReference ...")` instead
   of `BeginEdit`/`setProperty`/`CommitEdit` directly, for both the drag-drop assign and the "X" clear
   button. Verified live: assign → Undo (reverts to the pre-assign target, including a genuinely odd
   pre-existing `runtime-entity 0000000000000000` baseline value, not just "cleared") → Redo → Clear
   → Undo-of-clear, all round-tripped correctly via DescribeEntity.

4. **RemoveComponent orphaning prefab property overrides - FIXED.** `ScrubComponentOverrideEntry`/
   `SnapshotComponentOverrideEntry`/`RestoreComponentOverrideEntry` (`E29_Commands_ComponentEdit.h`).
   Redo now scrubs the matching `prefab_component_override` entry from the prefab instance's
   `m_lComponents`; Undo restores it exactly (MemberPath + PropertyOverrides). **Real bug found and
   fixed during verification, not anticipated**: the first version scrubbed/restored `m_lComponents`
   correctly in LIVE memory but never called `MarkEntityDirty` on the prefab instance's ROOT entity
   (which owns `m_lComponents` - not necessarily the same entity the removed component itself lived
   on) - `SaveScene` only re-writes entities `m_PendingChanges` marks dirty, so the fix silently never
   persisted to disk. Confirmed via a real Save/RemoveComponent/Save/cat cycle showing the override's
   own file section frozen at its pre-fix content; fixed by adding the same
   `Ctx.m_RootEntity != Entity` → `MarkEntityDirty(SceneGuid, RootId)` check
   `RecordPropertyOverride`/`RemovePropertyOverride` (E29_Commands_PropertyEdit.h) already use for the
   identical reason. Verified afterward with a distinct marker value ("Entity-MARKER-XYZ") through a
   full Remove→Save→Undo→Save round trip, confirmed via `cat` on the raw `.entity` file both times.

5. **"Half the editor isn't undo-routed" - Instantiate + folder reparent FIXED, Make Prefab
   deliberately deferred, Duplicate doesn't exist.**
   - **Instantiate** - new `InstantiatePrefab` command (`commands/E29_Commands_SceneOrganization.h`).
     Mirrors `InstantiatePrefabIntoScene` (E29_PrefabAuthoring.h) but registers the group's ROOT under
     an explicit, caller-minted id (same `-Id pre-minted by the caller` convention `create_entity_cmd`
     already established) so Redo stays deterministic across an Undo/Redo cycle; descendants still get
     freshly-minted ids every Redo call, which is fine since Undo discovers them dynamically by
     walking the root's own live children (`DeleteSubtreeByPermanentId`, reused verbatim from Phase
     4), never by remembering descendant ids from a prior Redo - verified live: descendant ids
     genuinely differed between two consecutive Redo calls in the same test, Undo worked correctly
     both times regardless. All 3 UI drag-drop call sites (E29_Panel_LevelTree.h) now route through
     `e29::commands::Run`.
   - **Folder reparent** - new `MoveToFolder` command (same file). `ReparentEntityIntoFolder` has a
     real side effect beyond membership: `PruneEmptyFolderChain` deletes the OLD folder (cascading up
     any now-childless ancestor) if the move empties it - `BackupCurrenState` snapshots every folder
     along the old ancestor chain (id/parent/name) so Undo can recreate any Redo's own prune removed,
     BEFORE re-inserting (`ReparentEntityIntoFolder` silently no-ops the re-add into a folder that no
     longer exists). Verified live with the hardest case: moved all 3 members out of a real folder one
     at a time (confirmed the folder itself got pruned/disappeared from `ListFolders` after the 3rd
     move), then undid all 3 moves - the folder was recreated with its exact original id/name on the
     first undo, and all 3 entities ended up back in their exact original order.
   - **Make Prefab - landed 2026-09-10** (`MakePrefab`/`MakePrefabVariant`, see
     [E29 Asset Browser command layer](asset_browser_command_layer.md) for full detail). Composes the Asset Browser command layer's
     `CreateOrRestoreAsset` with `kit/E29_PrefabAuthoring.h`'s existing
     `CreatePrefabFromGroupRoot`/`CreatePrefabVariantFromInstance`; Undo restores the original entity
     subtree (via `SnapshotSubtreeForRestore`/`RestoreSubtreeFromSnapshot`, factored out of
     `delete_entity_cmd`) and trashes the created asset. Found and fixed a real engine crash along the
     way - see `xecs_copyentity_oob_childless_prefab` (note pending migration).
   - **Duplicate - not a real gap.** Grepped the whole E29_LevelSceneEditor tree: no "Duplicate"
     entity feature exists anywhere in the code today (no menu item, no shortcut, no helper function).
     The original gap list's mention of it was inaccurate - nothing to wrap.

**Also added mid-session, direct user request while watching this session drive Play/Pause via
synthetic mouse clicks**: `Play`/`Pause`/`Stop`/`GetPlayState` query commands
(`commands/E29_Commands_PlaySession.h`) - set the exact same flags
(`State.m_PlayState`/`m_bPlayRequested`/`m_bStopRequested`) the menu-bar buttons themselves set, so
the existing per-frame polling (`PollGameReload`, the deferred-Stop consumption) does the real work
identically regardless of mouse or CLI origin. New global `e29::g_pGamePlugin`
(`plugin/E29_GamePluginBuild.h`), same "one instance per process" pattern as `g_pGameMgr`/`g_pState`.
This is what made gap #1's own Play-state-gate verification possible without any synthetic mouse
input at all.

**Standing rule, confirmed 2026-09-09 (phase 5) - unrelated to the gaps above, kept from the prior
version of this memory**: `xcmdline::parser::Parse(std::string_view)` is a naive space/tab tokenizer
with zero quote-handling - any free-form string argument (a path, a name with spaces, arbitrary
content) MUST be Base64-encoded, never passed raw. Every hex/numeric id field (`Scene`, `Id`,
`Component`, `TypeGuid`, `Folder`, `Parent`, `Prefab`) never needs this.
