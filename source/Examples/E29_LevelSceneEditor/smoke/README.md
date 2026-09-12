# E29 CLI smoke harness

Thin Python (stdlib-only) driver for `E29CLI.exe`, which is a **named-pipe client** for a
live E29 Level Scene Editor session:

```text
\\.\pipe\E29_LevelSceneEditor_Console
```

`E29CLI` does **not** launch the editor. Start `xGPU_unit_test` with the E29 example open
(project auto-loads `example.lionprj` from the repo root), then run this harness.

## Defaults

| Item | Path |
|------|------|
| CLI | `<repo>\Build\xGPUExamples.vs2022\Debug\E29CLI.exe` |
| Project | `<repo>\example.lionprj` |
| Commands | `E29/Edit/...` and `E29/Query/...` (xundo `history::Route`) |

## How to run

```bat
cd source\Examples\E29_LevelSceneEditor\smoke
python run_smoke.py --list
python run_smoke.py --dry-run
python run_smoke.py removed_child
python run_smoke.py removed_child_undo --var CHILD_ID=00000042 --var PI_ID=A1000001
python run_smoke.py --cli D:\LIONant\xGPU\Build\xGPUExamples.vs2022\Debug\E29CLI.exe
```

Aliases: `removed_child` → `removed_child_persist`.

### Fixture vars

| Var | Default | Meaning |
|-----|---------|---------|
| `LEVEL` | `08C298C9F6668005` | `OpenLevel -Level` (override if ListLevels differs) |
| `SCENE` | `08C298C9F6668005` | Scene guid for List/Delete/Instantiate |
| `PREFAB` | `FFFF000200000001` | Prefab instance guid for InstantiatePrefab |
| `ROOT_ID` | `A1000001` | Pre-minted root id for InstantiatePrefab |
| `CHILD_ID` | *(required for real run)* | Child permanent_id under the PI to delete |
| `PI_ID` | `ROOT_ID` | Prefab-instance root for DescribeEntity |

Also overridable via env: `E29_SMOKE_LEVEL`, `E29_SMOKE_SCENE`, `E29_SMOKE_PREFAB`,
`E29_SMOKE_CHILD`, `E29_SMOKE_PI`, `E29_SMOKE_INSTANCE_ROOT`.

## How to add a scenario

1. Create `scenarios/my_case.py` exporting `NAME`, `DESCRIPTION`, `STEPS`, optional `setup(runner)`.
2. Register the module name in `scenarios/__init__.py` (`_MODULES`).
3. Each step is a dict:

```python
{
  "name": "optional label",
  "cmd": "E29/Query/Save",          # required (supports {VAR} formatting)
  "expect_ok": True,                # default True — fail on nonzero exit / error-looking text
  "expect": "Saved",                # optional substring
  "expect_re": r"Opened Level|...", # optional regex
  "expect_absent": "{CHILD_ID}",    # optional forbidden substring
  "assert": callable,               # optional fn(result, runner)
}
```

## Notes

- Edit commands return **empty** stdout on success; Query commands return a message.
- Pipe is single-client; the runner invokes a fresh `E29CLI.exe` per step.
- Do not kill other agents' `E29CLI` / editor processes if the pipe is busy — wait or
  run `--dry-run` / `--list` only.
- `--project` is recorded for future on-disk asserts; the live editor already opened
  `example.lionprj` at startup from the exe path containing `xGPU`.
