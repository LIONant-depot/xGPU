# Task: grow the E29 smoke suite

You are adding tests to a pytest suite that drives the real editor through its command console. Read `README.md` in this
folder first, then `harness.py`, `conftest.py` and the four `test_*.py` files (171 lines in all). Copy their style.

You only write Python tests (and the small harness changes listed in step 0). **Do not edit any C++.** When a test shows a
bug, mark it `@pytest.mark.xfail(reason="...", strict=False)` and add it to `FINDINGS.md` (see the end). When you need a new
query command to test something, add it to the requests list in `FINDINGS.md` instead of writing it.

## The most important rule: do not damage the project data

The suite runs against the developer's real project, `D:\LIONant\xGPU\example.lionprj`. Its `Descriptors\Scene`, `Level` and
`Prefab` folders are **not under git**: there is no way to get them back. An earlier run of this suite already lost an entity
because `Play` saves the open level first.

1. **Before every session of test runs**, copy the three folders somewhere outside the repo:
   `robocopy example.lionprj\Descriptors\Scene <backup>\Scene /E` (and `Level`, `Prefab`).
2. After the run, `diff -r` them against the backup. The only difference allowed is trailing whitespace in
   `ComponentDeps.txt`. If content differs, restore from the backup and tell the maintainer which test did it.
3. The suite leaves empty orphan files (`entity_db\..\*.entity`, 116 bytes, "nComponents 0"). Delete the ones that are
   **not** in your backup. Never delete anything else.
4. Never pass `allow_disk=True` unless a test is impossible without it, and then ask the maintainer first.
5. Never run `Save`, `MakePrefab`, `MakePrefabVariant`, `ApplyOverrides`, `Create*`, `Rename*`, `Delete*Asset*`, source
   control writes or script-source edits. `Play` and `Step` save the level: only use the `level` fixture (opened clean) for them,
   and create test entities **after** Play has started or not at all.
6. Do not edit `example.lionprj`, `Cache\`, or anything under `plugins\` or `dependencies\`.

## Setup

- Build: `cmake --build Build\xGPUExamples.vs2022 --config Release --target xGPU_unit_test` (and `--config Debug` for the
  Debug run). Close any running editor first: the pipe admits one server.
- Run: `python -m pytest source\Examples\E29_LevelSceneEditor\smoke -q` (Release) and add
  `--exe Build\xGPUExamples.vs2022\Debug\xGPU_unit_test.exe` for Debug. Debug has real asserts, so it finds bugs Release hides.
- The first launch after switching configuration rebuilds Game.dll (about a minute). That is normal.
- Do not put `sleep` in tests. Poll with `editor.wait_for(query, regex)` or `editor.wait_play_state(...)`.

## Step 0: harden the harness (small, do it first)

In `harness.py`, add to `DISK_WRITERS`: `MakePrefab`, `MakePrefabVariant`, `ApplyOverrides`, `AddLibraryDependency`,
`RemoveLibraryDependency`. Check the list against `help`: any other command that writes files must be there too.

## How to find out what a command does

There is no per-command help over the pipe. Read the source: `grep -rn "Usage:" plugins/xscene.plugin/source/Editor
plugins/xlevel.plugin/source/Editor dependencies/xresource_pipeline_v2/source/editor`. Each command's `getCommandHelp()` gives its
arguments. Each plugin has `documentation/editor.md` listing its commands. Facts you need:

- Edit commands answer `""` on success and a message on failure. Query commands answer text.
- A command on an open level is `Main Level\<Command>` (`level.cmd(...)` does the prefix).
- Guids and entity ids are hex; property paths and values are base64 of their text (`b64("5.000000")`).
- `help` only lists workspace commands (`golden/commands.txt`). Session commands (CreateEntity, SetProperty, ...) are not in it.

## Tests to write

Each item is a test file or a group. Every test must: use the `level` fixture, be independent of the others, leave no state
behind (restore anything global, e.g. `CompileAuto`), and assert on **specific** replies, not just "no crash". A test that
finds the editor dead must fail with the exit code (the existing `_editor_alive` fixture does that).

1. **`test_components.py`**: `AddComponent` then `RemoveComponent`, each undo/redo; removing restores the values the component
   had (set a property, remove, undo, read it back with `DescribeEntity`); adding a component the entity already has is refused;
   removing one it does not have is refused; `ListComponentTypes` lists `Transform` and `Name`; `DescribeEntity` output format
   (each component `[guid] Name`, each property `path = value (TypeGuid xxxxxxxx)`).
2. **`test_entities.py`**: `CreateEntity` with an existing id is refused; into a folder; with `-Parent` (a hierarchy);
   `DeleteEntity` of a parent removes its children and one `Undo` brings all back; ids are hex and case-insensitive; the dirty
   flag is set after an edit and clear after undoing back to the start.
3. **`test_folders.py`**: `CreateFolder` (nested with `-Parent`), `DeleteFolder` promotes its entities and child folders
   to the parent, `MoveToFolder` (into a folder, out to `0`), each with undo/redo; `ListFolders` reflects every step.
4. **`test_scenes_and_levels.py`**: reply formats of `ListLevels`, `ListScenes`, `ListEntities`, `ListFolders`;
   `OpenLevel` on an open level says so; `OpenLevel` over unsaved edits is refused until `-Save 0`; `CloseScene` and reopening;
   `AddScene`/`RemoveScene` with undo (discover a scene from `ListLevels`/`ListScenes`, never hard-code one);
   `AddSceneDependency`/`RemoveSceneDependency` including a **cycle being refused** and removing a dependency that
   still has references being refused (read the reply text in the source first).
5. **`test_undo_redo.py`**: several edits, undo them all, redo them all, state identical at each step; a new edit after an undo
   discards the redo branch (`Redo` then answers a refusal); `Undo` with nothing to undo answers a message, not a crash;
   the dirty flag follows the undo position.
6. **`test_prefabs.py`**: only with prefabs already in the project. Find one with `ListAssets` (see its reply format) and
   `InstantiatePrefab` it (in memory), then `DescribeEntity` shows the instance component; `SetProperty` on the instance and
   `RevertOverride`; `RevertAllOverrides` and `RevertHierarchyOverrides`; delete a child of an instance and undo. If the project
   has no prefab, `pytest.skip` with the reason. **Never** call `ApplyOverrides`.
7. **`test_play_more.py`**: `Pause`/`Resume` (Play again while Paused), `Step` from Playing is refused, `Stop` twice, `Play`
   while Playing, `GetPlayState` fields after each transition; editing commands during Play are refused or gated (read the
   source for which); `Stop -Keep true` keeps a `SetProperty` made during Play and one `Undo` reverts it, `-Keep false` discards it.
   Keep these in one file and few in number: every Play saves the level and can rebuild Game.dll.
8. **`test_console_and_chat.py`**: `Say`/`GetLog` round trip with base64 text (unicode, an emoji, a very long line, text with
   `\n`), `-Count`; `GetLog` on an empty log; bad base64 is refused.
9. **`test_read_only_queries.py`**: `ListAssets`, `DescribeAsset` (real and bogus guids), `CompileStatus`,
   `ListProjectModuleReferences`, `ListScriptSourceFiles`, `SourceControlStatus`, `SourceControlDepotStatus`,
   `SourceControlListLocks`, `GetIdleTasks`: reply is non-empty, well formed, and stable when asked twice. `CompilePause` and
   `CompileAuto` toggle and report through `CompileStatus`; restore the original state in a `finally`.
10. **`test_robustness.py`**: table-driven with `pytest.mark.parametrize`. For every command you know (workspace ones from
    `editor.commands()`, session ones from a list you keep in the file): send it with no arguments, with each required option
    missing, with a malformed guid (`ZZZ`, empty, 40 hex digits), with a huge value (100 KB) and with a bad base64 value.
    Assert a **message** comes back and the editor is still alive. Any crash is a real bug: `xfail` it and record the exact
    command line in `FINDINGS.md` (this is the most valuable test in the suite). Known crash: `SetProperty` with an unparseable
    `-Before`/`-After`.
11. **`test_session_command_surface.py`**: a golden list for the session commands like `golden/commands.txt`, so removing or
    renaming one is a deliberate act. Find a way to enumerate them from the running editor (try `Main Level\help`); if none works,
    keep the list in the test file and say so in `FINDINGS.md`.
12. **`test_game_module_reload_more.py`**: after the reload in `test_game_module_reload.py`, `ListComponentTypes` is the same as
    before; `Stop` after a reload restores the level; reload while Paused. The DLL rebuild makes these slow: at most two tests.

Do not test: asset create/rename/move/delete, library commands, script source edits, source control writes, anything that
needs `allow_disk=True`. List what you skipped and why in `FINDINGS.md`.

## Style

- One `test_*.py` per area, plain functions and `assert`, a one-line docstring per file and only where a test's intent is not
  obvious from its name. No classes, no mocks, no helper layers: put a helper in `conftest.py` only when three tests use it.
- New helpers on `Editor`/`Level` go in `harness.py` / `conftest.py` and follow the existing ones.
- A test is 3 to 15 lines. If it needs more, split it.
- Ids you create come from `level.new_entity()`; folder ids you make up must be unique (`0xF0..` range) and hex.
- Keep the whole suite under 3 minutes on Release.

## Definition of done

1. Every test file above exists (or the item is listed as skipped with a reason).
2. The full suite passes on Release **and** on Debug, three runs in a row (no flaky test: a test that fails once is a bug in the
   test or the editor, find out which).
3. The project data check from "The most important rule" is clean after each of those runs.
4. `git status` shows only the files you meant to change. Stage them **by name**; never `git add -A`, `git add .` or a folder:
   the repo has untracked user files that must not be committed. Do not push. Commit message style: a short imperative first
   line, then a paragraph on why.
5. `FINDINGS.md` in this folder contains: crashes and wrong behaviour found (exact command lines, replies, exit codes, Debug or
   Release), requests for new query commands (e.g. a `GetSelection` query: there is none, so `Select`, `ToggleMultiSelect` and
   `ClearSelection` cannot be tested yet), tests skipped, and anything in this file that was wrong or unclear.
6. Update the "Known gaps" list in `README.md` to match.

If something here contradicts what you see in the code, trust the code and say so in `FINDINGS.md`. If you are about to do
anything that could write project data and you are not sure, stop and ask.
