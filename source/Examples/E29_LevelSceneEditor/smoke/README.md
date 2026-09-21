# E29 smoke tests

Regression tests for the editor's **command surface** - the same commands the AI/CLI uses. They launch the
real editor (`xGPU_unit_test.exe`), drive it through its Command Console pipe, and assert on replies.

```bat
pip install pytest
cd source\Examples\E29_LevelSceneEditor\smoke
python -m pytest -q                     # all tests (~20 s), launches the Release build itself
python -m pytest test_play.py -q        # one file
python -m pytest -q --exe <path>        # a different build
python -m pytest -q --update-golden     # accept a deliberate change to the command list
```

Build first (`cmake --build Build\xGPUExamples.vs2022 --config Release --target xGPU_unit_test`), and close any
running editor - the pipe (`\\.\pipe\xEditor_Console`) admits one server.

## How it works

| File | Role |
|---|---|
| `harness.py` | `Editor`: launches/kills the process, pipe client (stdlib `ctypes`, no CLI executable needed), typed helpers (`sessions()`, `entities()`, `describe()`, `wait_play_state()`, ...) |
| `conftest.py` | fixtures. `editor` = one process for the run, restarted if a test kills it. `level` = the example project's first level opened **clean**, closed without saving afterwards. `_editor_alive` = a crash fails exactly the test that caused it, with the command and exit code |
| `test_*.py` | the tests. Plain functions and `assert` |
| `golden/commands.txt` | the expected workspace command names; a removed/added command fails until you `--update-golden` |

Command grammar: `<Command> ...` (workspace) or `<Session name>\<Command> ...` (`Main Level\CreateEntity ...`).
**Edit** commands reply with an empty string on success; **query** commands reply with text; refusals are text.
Property paths and values are **base64 of their text** (`b64("5.000000")`), not raw bytes.

## Rules that keep the suite safe

The suite runs against the developer's real `example.lionprj`.

- Tests never save. The harness raises `PermissionError` for commands that write project data (`Save`, `Create*`,
  `Rename*`, source control, ...) unless the call passes `allow_disk=True`.
- `Play` saves the open level first, so the harness refuses `Play`/`Step` while a session has unsaved edits. Create
  test entities **after** the `Play` if you need both. Use the `level` fixture (clean) for any Play test.
- Everything a test creates lives only in memory (`level.new_entity()` mints ids `7E57xxxx`) and is discarded by
  `Close -Save 0` in the fixture teardown.
- A test must not depend on fixed entity ids from the project; discover them (`level.find_with_component("Transform")`).

## Writing a test

```python
def test_create_entity_undo_redo(level):
    before = level.entities()
    entity = level.new_entity()                     # Main Level\CreateEntity ... (must reply "")
    assert entity in level.entities() and level.dirty()
    assert level.cmd("Undo") == "Undone"
    assert level.entities() == before
```

## Known gaps (good next tests)

- **Prefab overrides**: `RevertAllOverrides`, `RevertHierarchyOverrides`, `ApplyOverrides`, delete-child-under-instance and
  undo. The previous harness had stubs for these that never ran (they needed a hand-set `CHILD_ID`); they need a prefab
  instance discovered from the project (`ListEntities` / `DescribeEntity`).
- **Components**: `AddComponent`/`RemoveComponent` undo, incompatible-component refusal.
- **Scenes and folders**: `AddScene`/`RemoveScene`, `CreateFolder`/`DeleteFolder`/`MoveToFolder`, scene dependencies.
- **Selection**: `Select`, `ToggleMultiSelect` (there is no query for the selection yet - add one).
- **Robustness**: `SetProperty` with an unparseable `-Before`/`-After` **crashes the editor** (fast-fail inside the
  property parser). Once it returns an error instead, add a test that it is refused with a message.
- **Save gating**: `Save` refused during Play (needs `allow_disk=True` and care).
