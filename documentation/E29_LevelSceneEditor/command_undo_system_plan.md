# E29 command / undo system - phased plan and status

> Phased plan to bring E27_NodeOS's real xundo command pattern to E29 - ALL 6 phases DONE and verified live (Select, property edit, add/remove component, create/delete entity, CLI/pipe + Say/GetLog chat, Command Console panel), plus a follow-on Level/Scene discovery command set and hex-id standardization
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-09).

Agreed 2026-09-07, right after finishing the `e29_kit_split_phase1_panels` (note pending migration) 3-phase split. Direct
user motivation: E29's "real actions" (property edits, component add/remove, entity create/delete,
selection) currently mutate state directly from the UI with no undo and no external entry point at
all - E27_NodeOS already solved exactly this with a real, working, already-proven pattern
(`xundo::command_base` + `xundo::history::Route()`), not something to invent fresh. Also gives an
AI/CLI agent a real way to drive E29 without UI automation - directly useful for THIS session's own
testing, which has repeatedly hit synthetic-mouse-click flakiness (see `xgpu_ui_automation_friction` (note pending migration)).

**Important scoping note, not obvious from the outside**: E29's property inspector already has SOME
undo today (`xproperty::inspector::BeginEdit`/`CommitEdit`, see
`xproperty_begin_commit_edit_undo` (note pending migration)) - but that's explicitly the SIMPLIFIED, non-`xundo` version.
Direct user quote from when it was built: "the one that comes with xproperty inspector is just a
simple example... it shows how a real undo system really should work [is E27's]." So the property-
editing phase below is a REPLACEMENT of that simple bracket with a real routed command, not adding
undo where there was none - the snapshot-taking BeginEdit/CommitEdit already does is largely reusable
as the Before/After capture, it just needs to feed a command instead of a local bracket.

**Reference pattern, already read in detail this session** -
`source/Examples/E27_NodeOS/Editor/NodeOS_Commands_Edit.h`:
- `select_cmd`/`clear_selection_cmd` (~line 1515/1597) - one Select command covers ALL selection
  fields at once (matches every existing interaction site already setting them together); a
  dedicated ClearSelection command rather than a degenerate empty Select, so a reader (human or
  agent) doesn't have to infer "nothing after Select" means deselect.
- `set_properties_cmd` (~line 1455) - NodeId + Before/After base64 property snapshots as CLI args;
  Redo applies After, Undo restores Before.
- `delete_nodes_cmd` (~line 523) - Redo does the actual removal via existing helpers;
  `BackupCurrenState` serializes the doomed subtree's properties before removing it; Undo recreates
  from that snapshot.
- `create_node_cmd` (~line 25) - mint id, call existing creation helpers; Undo = delete it.
Also: `node_os_command_context` (`NodeOS_CommandContext.h`) - plain references to the live state, zero
ImGui/xgpu dependency; `NodeOS_UI_CommandConsole.h` - the in-app panel + a Win32 named pipe
(`\\.\pipe\E27_NodeOS_Console`) served on a background thread, drained once/frame on the main thread;
`NodeOSCLI.cpp` - a standalone Win32+iostream CLI with zero xGPU/Vulkan/ImGui deps that talks to that
pipe - this already lets a script or AI drive E27 today with no UI automation.

**Agreed phase order** (start with the simplest, each its own verified/committed step, matching how
the kit split was done):
1. **Select / ClearSelection** - smallest, and a real dependency for later commands ("add a
   component to the selected entity" needs a resolved selection). Retarget E27's exact two commands
   at E29's own `editor_state::m_SelectedEntityId`/`m_SelectedEntityScene`/
   `m_MultiSelectedEntityIds`/`m_MultiSelectOrder`/`m_MultiSelectScene`.
2. **Property editing** - reuse/redirect the BeginEdit/CommitEdit snapshot mechanism into a real
   command's Before/After, mirroring `set_properties_cmd`.
3. **Add/Remove Component** - same snapshot shape as property editing, at the component-set level.
4. **Create/Delete Entity** - needs the most new plumbing (subtree serialization for delete-undo,
   matching `delete_nodes_cmd`'s pattern but using E29's own entity/component serialization, the
   same one `SaveEntity` already uses).
5. **CLI/pipe** - near-direct port of E27's plumbing (`NodeOSCLI.cpp` + the named-pipe thread), cheap
   once step 1-4's commands exist to route to.
6. **Command Console panel** - last, once the router actually has something to route; adapt
   `NodeOS_UI_CommandConsole.h`.

**Phase 1 (Select/ToggleMultiSelect/ClearSelection) - DONE, verified live by direct user testing
(both Ctrl+Z and Ctrl+Y confirmed working), 2026-09-07.**

New files: `source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h`
(`e29_command_context{ editor_state& m_State }`, `BackupSelection`/`RestoreSelection`, the `Run()`
logging wrapper) and `scene/commands/E29_Commands_Selection.h` (`select_cmd`, `toggle_multi_select_cmd`,
`clear_selection_cmd` - two edit commands rather than E27's one `Select`, since E29's selection model
splits primary vs. multi-select in a way NodeOS's flatter model doesn't). Wired into
`E29_LevelScene_Editor.cpp`: `xundo::system E29Undo` + `xundo::history E29History` (namespace "E29",
ready for the CLI phase), the 3 command instances constructed as locals, Ctrl+Z/Ctrl+Y (same
convention as E27), and `RenderLevelTreePanel` gained a `xundo::system&` parameter so its real
plain-click/ctrl-click handlers call `e29::commands::Run(Undo, "Select -Scene ... -Id ...")` instead
of mutating `State` directly.

**Real correctness catch made BEFORE wiring, not found by luck**: `e29_command_context` deliberately
holds only `editor_state&`, NOT a `xecs::game_mgr::instance&` the way E27's own context holds its
node/link vectors directly - `pGameMgr` is a `unique_ptr` destroyed and reconstructed on every
hot-reload (`RebuildWorld`), so a reference captured once at construction would dangle after the
first reload. GameMgr access instead goes through `e29::g_pGameMgr`, the existing global
`E29_GamePlugin.h` already keeps correctly rebound after every reload - reused rather than inventing
a second pointer to keep in sync.

**Real, pre-existing library bug found and fixed along the way, not anticipated**: wiring xundo into
a SECOND .cpp in the same executable (E29, alongside E27_NodeOS) surfaced a genuine ODR violation -
several out-of-class function definitions in `dependencies/xundo/source/xundo_system.h`
(`command_base`/`query_command_base` constructors, all 4 `job::*::Execute()` overrides,
`getCommandName`) and 4 explicit template specializations in `dependencies/xcmdline/source/
xcmdline_parser.h` (`parser::convertValue<T>` for string/string_view/int64_t/double) were missing
`inline` - harmless while only one .cpp in a given link ever included these headers, a real
multiply-defined-symbol LNK2005 the moment a second one did. Fixed both (separate dependency repos,
each their own commit).

**Automation note**: my own synthetic-mouse verification of the click→Select flow worked reliably
(confirmed via screenshot - Entity Properties panel updated correctly); synthetic KEYBOARD input
(Ctrl+Z via `keybd_event`) did not visibly register even with confirmed window foreground focus -
consistent with `xgpu_ui_automation_friction` (note pending migration)'s existing note, this time specifically isolating
keyboard vs. mouse synthetic input as the less reliable half. Handed off to the user, who confirmed
both Ctrl+Z and Ctrl+Y work correctly live.

**Phase 2 (property editing) - DONE, verified live by direct user testing, 2026-09-07.** Direct user
caution going in: "careful with resetting the overrides" - this shaped the whole design.

New file: `source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_PropertyEdit.h`
(`set_property_cmd` - Scene/Id/Component/Path/TypeGuid/Before/After as CLI args, self-contained via
`BackupCurrenState`/`undo_file`, not relying on `m_Parser` during Undo). Deliberately does NOT go
through `xproperty::inspector::BeginEdit`/`CommitEdit`'s whole-component snapshot bracket - that
bracket's `m_OnChangeEvent` carries a bracket label + multi-line blob, not a real property path/scalar
value (see `xproperty_begin_commit_edit_undo` (note pending migration)); this command hooks the ORDINARY per-row commit path
instead (`Cmd.m_Original`/`Cmd.m_NewValue`, real scalar values), which is also why no
`m_bSuppressOverrideTracking` guard is needed here - this path never re-fires `m_OnChangeEvent`.
`entity_inspector_bridge::RegisterCallbacks` gained a `xundo::system&` parameter, and its
`m_OnPropertyChanged` was rewritten to build/`Run()` a `SetProperty ...` command string instead of
mutating override state directly. `xundo::system& Undo` threaded through `RebuildWorld`/
`StopPlaySession`/`PollGameReload` (all 3 call `RegisterCallbacks` again after a hot-reload) so the
command keeps working across reloads too.

**Real bugs found via live user testing, not caught by the build** (build success only proves it
compiles, not that undo semantics are correct - matches this session's own established pattern of
methodical live verification over declaring victory early):
1. **`set_property_cmd` was never instantiated.** Wrote the whole class, wired the callback to
   `Run()` a `SetProperty` command string, but forgot the one line every other command needs
   (`e29::commands::select_cmd CmdSelect(E29Undo, &CmdContext);`-style) to actually construct an
   instance and self-register it with `E29Undo`. Symptom: "Unable find the command: SetProperty" in
   the error popup the moment a property was edited. Fixed by adding
   `e29::commands::set_property_cmd CmdSetProperty(E29Undo, &CmdContext);` alongside the other 3
   command instances in `E29_LevelScene_Editor.cpp`.
2. **The actual "careful with resetting the overrides" bug, caught by the user manually testing
   exactly that.** The original `Undo()` always called the same find-or-create-and-set-value logic
   Redo() used, with the Before value - so undoing the FIRST edit to a previously non-overridden
   property left behind a bogus override entry (holding the pre-edit value) instead of removing it
   entirely. Direct user report: "when I override a property then I undo it... it needs to go back
   to a non-overwritten property." Root cause: there is no "does this value match the prefab base"
   comparison anywhere in this override system (a known, already-documented design property - see
   `xecs_prefab_override_design` (note pending migration)) - a NEW override and an UPDATE to an existing override are
   indistinguishable from the value alone, so undo has to remember which one it was. Fixed by having
   `BackupCurrenState` (which `xundo::system::Execute` always calls BEFORE `Redo()`) check whether an
   override for this exact Path already existed BEFORE the edit (`HasPropertyOverride`, a
   non-creating read-only counterpart to `FindOrCreateOverrideEntry`) and persist that as a
   `bHadOverride` bool in the undo_file. `Undo()` now branches on it: if an override already existed,
   restore it to the Before value (`RecordPropertyOverride`, same as before); if not, apply the Before
   value to the live property AND remove the override entry entirely
   (`RemovePropertyOverride` - erase the matching property entry, and if the owning component's
   override list is now empty, erase that whole component-override entry too), mirroring
   `entity_inspector_bridge`'s own pre-existing "Revert Override" action
   (`m_OnOverrideReset`, `E29_LevelSceneEditorKit.h`) exactly rather than inventing new bookkeeping
   semantics.

**Other real bug, non-semantic, caught by the compiler**: the `#include` for this new command file in
`E29_LevelSceneEditorKit.h` was initially placed while `namespace e29 { ... }` was still open - since
the new file declares its own `namespace e29::commands { ... }` at file scope, this nested into
`e29::e29::commands`, breaking lookup of unrelated symbols elsewhere in the same TU (`RemapGUIDToString`
etc., via a confusing "is not a member of 'e29::e29'" error). Fixed by closing/reopening
`namespace e29 {}` around the include, matching the umbrella's own established convention for every
other kit/plugin include.

**Phase 3 (Add/Remove Component) - DONE, verified live by direct user testing, 2026-09-07.**

New file: `source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_ComponentEdit.h`
(`add_component_cmd`, `remove_component_cmd`). `AddOrRemoveComponents` always migrates the entity to
a NEW handle (real archetype change, not in-place) - `MigrateEntityComponents` factors out the
caller-responsibility remap every pre-existing call site did by hand (erase old
`m_RuntimeToLocal`/insert both maps under the new handle/`MarkEntityDirty`), plus - new - only
refreshing `State.m_SelectedEntity`/`m_bEntityInspectorDirty` when the affected entity IS the one
currently selected (a command could in principle target a non-selected entity once the CLI phase
exists; the old inline UI code never had to check this since it only ever operated on the selection).
`add_component_cmd::Undo` is simply "remove it again" - no snapshot needed, since `xundo::system`
always steps Undo/Redo ONE AT A TIME even when jumping several entries (confirmed by reading
`xundo_system.h`'s own RewindTo/FastForwardTo), so any property edits made to the component after
adding it are guaranteed already undone, in order, before this Undo runs.
`remove_component_cmd::BackupCurrenState` (called BEFORE `Redo()` - same confirmed xundo ordering
[[phase 2 relied on]]) snapshots every property via `xproperty::sprop::collector` +
`AnyToString`/`SetLivePropertyValue` (reused from `E29_Commands_PropertyEdit.h`) - a string-value
list, not a raw memcpy, since components can hold non-trivial members (`std::string`/`std::vector`)
a memcpy would corrupt (see [Scratch buffers: construct before copy](../../dependencies/xECSV2/doc/scratch_buffer_construct_before_copy.md) for a related real bug of
that exact class). Undo re-adds the component (fresh/default) then replays the snapshot onto it.

**Dedicated research pass before writing code** (same discipline as phase 2, since this also touches
prefab-override bookkeeping) confirmed: `AddOrRemoveComponents` itself
(`dependencies/xECSV2/src/details/xecs_game_mgr_inline.h`/`xecs_archetype_mgr_inline.h`/
`xecs_pool_inline.h`) does zero prefab-bookkeeping - pure ECS layer, components leaving the archetype
just get destructed, no snapshot anywhere. `xecs::editor::prefab_instance::m_ComponentDiffs`
(`xecs_editor.h`) is NOT incrementally maintained anywhere - it's fully recomputed from scratch only
at SAVE time (`RefreshPrefabInstanceOverlayRecord`, `xecs_reference_remap_inline.h`), so unlike
`m_PropertyOverrides` (phase 2's own bug class) it can never go stale from Add/Remove Component and
needed no touching here.

**Known, pre-existing gap - NOT fixed, deliberately, flagged to the user instead**: removing a
component that currently has recorded `m_PropertyOverrides` entries (in
`prefab_instance::m_lComponents`) leaves those entries orphaned - confirmed this is how the
EXISTING (pre-command) UI code already behaved, not a regression introduced by wrapping it in a
command. Fixing it would be new behavior beyond "wrap the existing action," so it was left alone and
called out explicitly rather than silently changed - matches this project's standing rule of not
scope-creeping into adjacent bugs the user didn't ask about, especially ones touching the same
override system phase 2 already had to get right.

**Phase 4 (Create/Delete Entity) - DONE, verified live by direct user testing, 2026-09-07. By far the
hardest phase - two dedicated research passes (one via a delegated Explore agent, one via direct
source reading) plus a live debugger call stack were needed before it was actually correct.**

New file: `source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_EntityLifecycle.h`
(`create_entity_cmd`, `delete_entity_cmd`, `DeleteSubtreeByPermanentId` shared helper). Key design
decision: a deleted entity's `parent`/`children`/`entity_reference` fields hold RAW RUNTIME HANDLES,
meaningless the instant the entity is destroyed - phase 2/3's "snapshot properties as strings" isn't
safe here. Instead of reinventing reference-encoding, `delete_entity_cmd::BackupCurrenState` reuses
the EXACT machinery real Save/Load already trusts: `xecs::scene::mgr::SaveEntity`/
`xecs::scene::details::LoadEntity` (already correctly permanent-id-encode/decode parent/children/
entity_reference and prefab-instance overlays), `xecs::persist::details::RemapLoadedEntityReferences`
(resolves encoded refs back to real handles once every entity is registered), and
`ApplyPrefabInstancePropertyOverrides` (must run last - a non-empty `m_MemberPath` override needs a
real `children.m_List` to walk).

**The wrinkle**: SaveEntity/LoadEntity always read/write a FIXED path derived from (Mgr, SceneGuid,
Id) - using the entity's REAL id would let a real "File > Save" between Delete and Undo silently
delete the very snapshot Undo needs (`SaveScene` deletes a Deleted-marked id's file). Fixed by
snapshotting under a throwaway SHADOW id (minted via `NextFreeEntityId`) instead, remapped back to
the real id immediately after loading.

**Three real, live-debugged bugs, not anticipated by the design above**:
1. **`create_entity_cmd` never instantiated** (same class of miss as phase 2's `set_property_cmd`,
   caught before user testing this time since I remembered to check).
2. **`SaveEntity` itself unconditionally re-registers the scene's OWN identity maps**
   (`Scene.m_LocalToRuntime[Id]`/`m_RuntimeToLocal[Entity.m_Value]`, `xecs_scene_inline.h` ~line 660)
   using WHATEVER `Id` it's called with - correct for a real save, but calling it with a throwaway
   shadow id repoints `m_RuntimeToLocal[Entity.m_Value]` AT the shadow id, corrupting the real
   entity's own registration. Symptom: no crash at delete time, but the NEXT frame's completely
   unrelated `RenderEntityPropertiesPanel` call asserted (`Entry.m_Validation == Entity.m_Validation`)
   on a now-dangling `State.m_SelectedEntity` - found via a live Visual Studio call stack after two
   rounds of printf-tracing failed to explain it (the corruption happened one level removed from
   where the crash surfaced). Fixed by having the snapshot Walk() immediately undo SaveEntity's own
   side effect (`erase(ShadowId)` + restore `m_RuntimeToLocal[Entity.m_Value] = RealId`) right after
   each call.
3. **`DeleteEntitySubtree`'s parent-scrub is a side effect Undo must reverse too.** It correctly
   removes the deleted entity from its PARENT's `children.m_List` - but the parent is, by definition,
   OUTSIDE the deleted subtree, so its `children` component is never part of what gets
   snapshotted/restored. Without an explicit fix, Undo brought the entity back with a correct `Parent`
   field (fixed generically by `RemapLoadedEntityReferences`) but it never re-appeared in the tree,
   since nothing re-inserted it into the parent's own list. Fixed by capturing `RootParentId` (and,
   per direct user follow-up request, its exact INDEX within both the parent's `children.m_List` and
   a folder's `m_Entities`) in `BackupCurrenState`, and re-inserting at that same id/index in `Undo`
   rather than just appending.

**UI gap found and fixed along the way, not a regression**: an entity row's own context menu only
ever offered "Delete Entity" - there was NO way to create an entity as a child of another entity at
all (only Scene/Folder rows could call `ShowCreateMenuItems`). Direct user report: "otherwise I can
not test your command." Added "New Entity" to the entity row's own context menu, extended
`create_entity_cmd` with an optional `-Parent` arg (wires up both `parent` and `children` via Phase
3's own `MigrateEntityComponents` helper) - Undo needed zero changes for this case, since
`DeleteEntitySubtree`'s existing parent-scrub logic already generically handles it.

**Process note**: temporary printf tracing added mid-investigation was removed from PRE-EXISTING,
widely-shared code (`DeleteEntitySubtree`, `RenderEntityPropertiesPanel`) once root-caused (it was
innocent in both bugs), but KEPT as permanent one-shot milestone logs in the new phase-4-only file
itself, per the project's standing working rule - the distinction being hot-path/shared code
vs. a new command's own lifecycle events.

**Phase 5 (CLI/pipe) - DONE, verified live via a real end-to-end round trip, 2026-09-09.** Near-direct
port of E27_NodeOS's own named-pipe server (NOT the full Command Console UI panel - that part stays
deferred to phase 6): `extensions/command_console/E29_CommandConsolePipe.h` (`command_console_pipe_bridge`,
`CommandConsolePipeThreadMain`, `PumpCommandConsolePipe`, `ProcessConsoleCommand`) + a standalone
`E29CLI.cpp` client (zero Vulkan/ImGui deps, its own `add_executable` CMake target, mirrors
`NodeOSCLI.cpp` exactly). Listens on `\\.\pipe\E29_LevelSceneEditor_Console`; commands route as
`E29/Edit/<Command> -args...` through `E29History.Route()` (registered since phase 1, unused until
now). One real deviation from E27: the per-frame pump runs BEFORE `BeginRendering` (grouped with
`PollGameReload`), not after like E27's own placement - E29's own established convention (heavy state
mutation inside an active ImGui frame corrupts its window-stack, confirmed multiple times this
project). Verified via `E29CLI.exe "help"` (correctly listed all 8 phase 1-4 commands),
per-command `-h`, and a real dispatch reaching `select_cmd::Redo()` and returning its actual error
text through the pipe.

**Say/GetLog chat extension - DONE, verified live, 2026-09-09.** Direct user request, riding on phase
5's own pipe: "add the ability [to] personalize commands... so if there are multiple AIs you guys can
have a conversation." New file `extensions/command_console/E29_Commands_Chat.h` - two `xundo::query_command_base`
commands (not `command_base` - a chat message isn't an undo-able scene mutation, matches
`query_command_base`'s own documented purpose): `Say -From name -Text base64` (appends to a NEW
`e29_command_context::m_ChatLog`, deliberately separate from the phase-5 `ConsoleLog` - a pure
conversation transcript, not command-dispatch echo/result noise) and `GetLog [-Count n]` (default 10,
returns the last N as `[From] Text` lines, oldest-of-the-shown-N first). In-memory only, current
session (matches `E29Undo`'s own `bAutoLoadSave=false` choice from phase 1). Design settled via direct
user answers to 3 clarifying questions: generic `-From`/`-Text` flags (not one hardcoded flag per
named AI - keeps it open to any future participant with no code change), a dedicated `GetLog` query
rather than relying on phase 6's still-unbuilt UI, in-memory-only persistence. `-From` is left as a
plain (non-Base64) argument since it's an identifier, never expected to contain a space - `-Text` is
Base64-encoded, since it's genuinely free-form content - see [E29 command / undo - known gaps (closed)](command_undo_known_gaps.md)'s own
"standing rule" entry (added the same day, prompted by this exact feature) for the full reasoning.
Verified live: a real two-message Claude<->GPT exchange sent via `E29CLI.exe`, read back correctly via
`GetLog`, `-Count` limiting and both commands' own `-h` all confirmed working.

Next: phase 6 (Command Console panel - the in-app ImGui text box + autocomplete + colored log,
`source/Examples/E27_NodeOS/Editor/NodeOS_UI_CommandConsole.h`'s `DrawCommandConsolePanel` is the
direct port target; `ConsoleLog`/`command_console_pipe_bridge` already exist from phase 5, so this is
purely the UI layer on top of what's already built and working).

**Known, recorded-for-later gaps**: see [E29 command / undo - known gaps (closed)](command_undo_known_gaps.md) - a second external review
pass (2026-09-07) found a few real issues in phases 1-4 (one already fixed: `create_entity_cmd`'s
`-Parent` was wrongly required, silently breaking "New Entity" from Scene/Folder rows). Direct user
instruction: fix these later, not as part of finishing the phase list.

**Phase 6 (Command Console panel) - DONE, verified live, 2026-09-09.** Direct user request: "you can
bring over the command window from example 27." `extensions/command_console/E29_Panel_CommandConsole.h` - near-direct port of
`DrawCommandConsolePanel` (autocomplete via `xstrtool::SubstringDamerauLevenshteinDistanceI`, shell-
style Up/Down history via `ImGuiInputTextFlags_CallbackHistory`, a persistent `TextEditor` for the
colored log). Renders the SAME `ConsoleLog`/`command_console_pipe_bridge` phase 5 already built - no
new state, purely the UI layer on top. Real bug found live: the copied-verbatim E27 default window
position `(1265, 18)` spawned almost entirely off the right edge of E29's own 1288-wide window (Level
Tree already occupies that whole x-range) - user found it themselves, docked it manually before I
even confirmed the fix; repositioned to `(990, 530)` (completing the existing bottom-row panel grid)
for future launches.

**Real gap found live and fixed in the same pass**: `e29::commands::Run()` (E29_CommandContext.h,
phase 1) - the ONE function every UI-driven command routes through - never actually logged anything
into the Command Console's log, only pipe-driven and console-typed commands did (`PumpCommandConsole
Pipe`/`DrawCommandConsolePanel` each had their own separate echo logic `Run()` never shared). Direct
user report: "now you have to route the users commands there as well... nothing showing up there
yet." Fixed by moving `console_log_entry`/`console_log_source` from `extensions/command_console/E29_CommandConsolePipe.h`
into `E29_CommandContext.h` itself (a phase-1 foundational file, included far earlier - the ordering
problem this move solves is the same shape as several earlier phases' own g_pGameMgr/xundo_history
availability issues) and adding a `g_pConsoleLog` global pointer (same "bound once at startup" pattern
as `g_pGameMgr`/`g_pState`) that `Run()` now pushes into, matching the SAME echo-then-result shape the
pipe/console paths already used.

**Level/Scene discovery + workspace commands - DONE, verified live, 2026-09-09.** Direct user reports,
each building on the last: "clearly one of the commands should be load a level...", "yes I think you
are missing Level/Scene commands", "ListFolders -Scene ... -From root...". New file
`level/commands/E29_Commands_Level.h` - six Query commands (not Edit - workspace/discovery actions, not
scene-content mutations, same reasoning as Say/GetLog): `OpenLevel -Level hexguid` (loads + activates
a Level's scenes; reports success by checking `State.m_CurrentLevel` afterward, since
`e29::OpenLevel`'s own return type is void), `CloseScene -Scene hexguid` (wraps the existing
`e29::CloseScene` helper), `ListLevels`/`ListScenes [-Level hexguid]` (walk `e10::g_LibMgr`'s own
type-indexed asset map via a shared `BuildAssetNameMap` helper - the same map the Asset Browser itself
walks), `ListEntities -Scene hexguid` (flat, id+name, same name-resolution fallback the Level Tree
panel's own row rendering already uses), `ListFolders -Scene hexguid [-From folderid-or-root]`
(recursive folder tree with each folder's own entities, indented by depth - fixed a real bug where an
explicit `-From <folder>` never showed that folder's OWN entities, only its children, inconsistent
with the root-default case). Found and fixed a real MSVC template-trait bug along the way - see
[FindAsReadOnly and noexcept callbacks](../../dependencies/xcontainer/documentation/noexcept_lambda_trait_trap.md) (new memory, cross-linked with the existing xproperty
one - a second, independent instance of the same general "noexcept callback silently fails a
library's own trait matching" class of bug).

**Entity-id hex standardization - DONE, verified live, 2026-09-09.** Direct user observation: "I think
we need to standardize the way we do GUIDs.... I think they should always be in hex." Confirmed real:
`Scene`/`Component`/`TypeGuid`/`Level`/`Folder` were already all hex, but entity `permanent_id`
(`-Id`/`-Parent`) was the one remaining DECIMAL field, parsed via plain `std::stoul` everywhere.
Added shared `ParseEntityId`/`FormatEntityId` (`E29_CommandContext.h`, 8 hex digits - `permanent_id`
is a plain `std::uint32_t`, matching Folder/TypeGuid's own existing width) and swept every parse site
(13, across 5 command files), every UI call site that builds a command string (8, across 3 files), and
every query-command output site that prints an id (`ListEntities`/`ListFolders`, 4 sites) to use them
- entity ids are now hex everywhere, matching the on-disk `.entity` filename convention too, so a
listing's output is directly pasteable into another command's `-Id`/`-Parent` with no conversion.
`GetLog -Count` deliberately stayed decimal (a plain magnitude, not an id/guid).

**All 6 phases plus this follow-on set are now DONE.** The command/undo system - Select, property
editing, add/remove component, create/delete entity, CLI/pipe + chat, the Command Console panel, plus
Level/Scene discovery/workspace commands and consistent hex ids throughout - is a complete, load-
bearing feature: a real undo-routed editing pipeline usable identically from the UI, a script, or an
AI, with no remaining "how do I even get started" gap. Remaining known gaps are tracked separately in
[E29 command / undo - known gaps (closed)](command_undo_known_gaps.md).
