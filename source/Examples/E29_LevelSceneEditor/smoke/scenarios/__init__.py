"""Scenario registry for E29 CLI smoke tests.

Add a module under scenarios/ that exports:
  NAME: str
  DESCRIPTION: str
  STEPS: list[dict]   # {cmd, expect_ok?, expect?, expect_re?, expect_absent?, name?, set?, assert?}
  setup(runner) -> None   # optional; fill runner.vars

Then register it in SCENARIOS below (or drop a .py that we auto-import).
"""
from __future__ import annotations

from importlib import import_module
from typing import Any, Callable, Dict, List, Mapping, Optional, Sequence

Scenario = Mapping[str, Any]


def _load(modname: str) -> Scenario:
    mod = import_module(f"{__name__}.{modname}")
    name = getattr(mod, "NAME", modname)
    return {
        "name": name,
        "description": getattr(mod, "DESCRIPTION", ""),
        "steps": getattr(mod, "STEPS", []),
        "setup": getattr(mod, "setup", None),
        "module": modname,
    }


# Keep explicit for clarity / stable --list order.
_MODULES = (
    "removed_child_persist",
    "removed_child_undo",
)


def all_scenarios() -> List[Scenario]:
    return [_load(m) for m in _MODULES]


def get_scenario(name: str) -> Scenario:
    key = name.strip().lower().replace("-", "_")
    aliases = {
        "removed_child": "removed_child_persist",
        "removed_child_persist": "removed_child_persist",
        "persist": "removed_child_persist",
        "removed_child_undo": "removed_child_undo",
        "undo": "removed_child_undo",
    }
    mod = aliases.get(key, key)
    for s in all_scenarios():
        if s["name"] == mod or s["module"] == mod or s["name"].replace("-", "_") == mod:
            return s
    raise KeyError(f"unknown scenario: {name!r} (try --list)")


def list_scenario_names() -> Sequence[str]:
    return [s["name"] for s in all_scenarios()]
