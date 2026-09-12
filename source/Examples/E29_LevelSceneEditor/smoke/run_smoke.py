#!/usr/bin/env python3
"""E29 Level Scene Editor — CLI smoke harness entry point.

Usage:
  python run_smoke.py                 # run all scenarios
  python run_smoke.py removed_child   # one scenario (alias for removed_child_persist)
  python run_smoke.py --list
  python run_smoke.py --dry-run removed_child_undo
  python run_smoke.py --cli PATH --project PATH --timeout 60 --var CHILD_ID=00000042

Requires a live E29 session (xGPU_unit_test with E29 open) so E29CLI can talk to
\\\\.\\pipe\\E29_LevelSceneEditor_Console. E29CLI itself is only the pipe client.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow `python run_smoke.py` from this directory without installing a package.
_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

from runner import (  # noqa: E402
    SmokeRunner,
    StepFailure,
    default_cli_path,
    default_project_path,
)
from scenarios import all_scenarios, get_scenario  # noqa: E402


def find_repo_root(start: Path) -> Path:
    """Walk up until we see CMakeLists.txt + source/Examples (xGPU root)."""
    cur = start.resolve()
    for p in [cur, *cur.parents]:
        if (p / "CMakeLists.txt").is_file() and (p / "source" / "Examples").is_dir():
            return p
    # Fallback when harness lives at .../E29_LevelSceneEditor/smoke
    # smoke -> E29_LevelSceneEditor -> Examples -> source -> repo
    parents = start.resolve().parents
    if len(parents) >= 4 and parents[0].name == "E29_LevelSceneEditor":
        return parents[3]
    return cur


def parse_vars(items: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for item in items:
        if "=" not in item:
            raise SystemExit(f"--var expects KEY=VALUE, got {item!r}")
        k, v = item.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="E29CLI smoke-test harness")
    p.add_argument(
        "scenarios",
        nargs="*",
        help="Scenario name(s). Default: all. Aliases: removed_child → removed_child_persist",
    )
    p.add_argument("--list", action="store_true", help="List scenarios and exit")
    p.add_argument(
        "--cli",
        type=Path,
        default=None,
        help=r"Path to E29CLI.exe (default: <repo>\Build\xGPUExamples.vs2022\Debug\E29CLI.exe)",
    )
    p.add_argument(
        "--project",
        type=Path,
        default=None,
        help=r"Path to example.lionprj (default: <repo>\example.lionprj); informational for disk asserts",
    )
    p.add_argument("--timeout", type=float, default=30.0, help="Per-command timeout seconds")
    p.add_argument("--dry-run", action="store_true", help="Print commands without invoking E29CLI")
    p.add_argument(
        "--var",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Override scenario vars (LEVEL, SCENE, PREFAB, ROOT_ID, CHILD_ID, PI_ID, ...)",
    )
    p.add_argument(
        "--repo",
        type=Path,
        default=None,
        help="xGPU repo root (auto-detected from this file's location)",
    )
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.list:
        for s in all_scenarios():
            print(f"{s['name']:24}  {s['description']}")
        return 0

    repo = (args.repo or find_repo_root(_HERE)).resolve()
    cli = (args.cli or default_cli_path(repo)).resolve()
    project = (args.project or default_project_path(repo)).resolve()

    names = args.scenarios or [s["name"] for s in all_scenarios()]
    overrides = parse_vars(args.var)

    print(f"repo:    {repo}")
    print(f"cli:     {cli}  ({'exists' if cli.is_file() else 'MISSING'})")
    print(f"project: {project}  ({'exists' if project.is_dir() or project.is_file() else 'MISSING'})")
    print(f"dry-run: {args.dry_run}")
    print()

    failures = 0
    for name in names:
        try:
            scenario = get_scenario(name)
        except KeyError as e:
            print(f"FAIL  {e}")
            failures += 1
            continue

        print(f"=== {scenario['name']} ===")
        if scenario.get("description"):
            print(scenario["description"])
        runner = SmokeRunner(
            cli,
            project=project,
            timeout=args.timeout,
            repo_root=repo,
            dry_run=args.dry_run,
        )
        setup = scenario.get("setup")
        if callable(setup):
            setup(runner)
        runner.vars.update(overrides)

        try:
            runner.run_steps(scenario["steps"])
            print(f"PASS  {scenario['name']}\n")
        except StepFailure as e:
            failures += 1
            print(f"FAIL  {scenario['name']}: {e}")
            if e.result is not None:
                print(f"      cmd={e.result.cmd!r}")
                if e.result.stdout:
                    print("--- stdout ---")
                    print(e.result.stdout.rstrip())
                if e.result.stderr:
                    print("--- stderr ---")
                    print(e.result.stderr.rstrip())
            print()

    if failures:
        print(f"{failures} scenario(s) failed")
        return 1
    print("all scenarios passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
