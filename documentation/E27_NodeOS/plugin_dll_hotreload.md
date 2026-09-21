# Plugin DLL hot-reload sequence

> E27_NodeOS's safe plugin-DLL hot-reload sequence (Save/ClearGraph/UnloadPlugin/recompile+load/reload-graph) and why the codebase never FreeLibrary'd a plugin module before this - a user-designed sequence, confirmed correct and necessary by reading the actual crash hazard in the code
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-08-24).

E27_NodeOS compiles plugin `.cpp` files into DLLs live while the app runs (`CompilePluginWorker`,
`E27_NodeOS_Editor.cpp`), and until 2026-08-25 NEVER called `FreeLibrary` on any of them - explicitly,
per that function's own comment: "A recompile's own DLL is never FreeLibrary'd... so any
already-placed node instance's m_pFactory keeps working." Old DLLs just accumulated in memory for
the process's whole life, on purpose.

**Why this was the safe default:** `node_instance::m_pNode` is an `xnode_os_node*` whose vtable lives
inside the plugin's DLL, and destroying one calls a *virtual* function
(`xnode_os_node_factory::DestroyNodeInstance`) that dispatches into that same DLL. `FreeLibrary`ing a
module while any `node_instance` still references its factory/vtable is a guaranteed dangling-pointer
crash the instant anything touches it (rendering the node, deleting it, an undo snapshot, anything).

**The user proposed the exact right fix, unprompted**, as a 6-step sequence for reloading a plugin
after editing its source: Save -> Clear the editor -> Unload the DLL -> Recompile -> Reload the DLL ->
Reload the saved graph. Verifying this against the actual code confirmed it's not just cautious, it's
necessary - each step exists for a real reason:
1. **Save** - the graph's about to be torn down; this is what step 6 reconstructs from.
2. **Clear** - MUST destroy every node instance via `DestroyNodeInstance` (which calls the node's OWN,
   still-loaded, factory) BEFORE the module is freed - not just drop the pointers and let something
   else clean up later. This is the step that makes the rest safe.
3. **Unload** - only safe now that nothing references the module. Implemented as `UnloadPlugin` with a
   refusal check (count any live node whose `m_pNode->m_pFactory` is still in that module's
   `AvailableTypes` entries) that returns an error instead of calling `FreeLibrary` if the check fails -
   confirmed via testing that this refusal fires correctly when 3 live nodes still used the target
   plugin.
4+5. **Recompile + load** are one atomic operation in this codebase already
   (`CompileAndLoadPlugin`/`CompilePluginWorker` always compiles a fresh, never-reused `.dll` filename
   and immediately `LoadLibrary`s it) - there's no "compiled but not yet loaded" state to split into two
   separate steps.
6. **Reload the saved graph** - `LoadGraph`'s own `EnsureLoadedAndGetType` per node handles resolving
   against whatever's loaded now; since step 3 set `Source.m_bLoaded=false`, this would even trigger a
   fresh compile on its own if step 4/5 were skipped, but doing 4/5 explicitly first surfaces a compile
   error immediately rather than deep inside a Load call.

**Implementation**: `ClearGraph`/`UnloadPlugin`/`ReloadPlugin` query commands
(`nodeos::commands::clear_graph_query_cmd` etc.) in `E27_NodeOS_Editor.cpp`, following Load/Save's own
precedent of being `xundo::query_command_base` despite being drastic (see
`xundo_query_command_router` (note pending migration)) - this codebase's convention for "not part of undo history" rather than
strictly read-only. `ReloadPlugin -DirName X` runs the whole sequence in one call and is
best-effort-continue past the Save step (once Clear has run the canvas is already empty, so getting
back to a WORKING graph matters more than aborting halfway).

**Verified working end-to-end** (2026-08-25): `UnloadPlugin -DirName Print` correctly refused while 3
live Print nodes existed; `ReloadPlugin -DirName Print` ran clean through all 6 steps and the graph came
back with all 14 nodes resolving to real types (not `?`), no crash.

**How to apply**: any future "how do I safely swap a live plugin's code" question in this project should
reuse this exact sequence/these exact commands, not re-derive it. The same DestroyNodeInstance-before-
FreeLibrary constraint would apply to ANY future feature that wants to actually unload a DLL in this
codebase, not just this reload flow.
