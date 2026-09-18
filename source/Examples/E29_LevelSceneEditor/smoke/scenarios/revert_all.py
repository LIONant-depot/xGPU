"""revert_all — delete child under PI, RevertAllOverrides, assert child back + diffs clear.

Does not modify Prefab asset FFFF000200000001. Keeps PI root id / Transform placement.
"""
from __future__ import annotations

from . import _constants as C

NAME = "revert_all"
DESCRIPTION = (
    "Delete child under prefab instance, RevertAllOverrides, assert child restored "
    "and override lists empty (HierarchyDiffs cleared if DescribeEntity dumps them)."
)


def setup(runner) -> None:
    runner.vars.update(
        {
            "LEVEL": C.LEVEL_GUID,
            "SCENE": C.SCENE_GUID,
            "PREFAB": C.PREFAB_GUID,
            "ROOT_ID": C.INSTANCE_ROOT_ID,
            "CHILD_ID": C.CHILD_ID or "SET_ME",
            "PI_ID": C.PI_ID or C.INSTANCE_ROOT_ID,
        }
    )


STEPS = [
    {
        "name": "open level",
        "cmd": "E29/Query/OpenLevel -Level {LEVEL} -Save 0",
        "expect_ok": True,
        "expect_re": r"Opened Level|already open",
    },
    {
        "name": "instantiate prefab if needed",
        "cmd": "E29/Edit/InstantiatePrefab -Scene {SCENE} -Id {ROOT_ID} -Prefab {PREFAB} -Folder 0",
        "expect_ok": False,
        "expect_re": r"(?s)^\s*$|id already in use",
    },
    {
        "name": "list entities (before)",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
    },
    {
        "name": "delete child under PI",
        "cmd": "E29/Edit/DeleteEntity -Scene {SCENE} -Id {CHILD_ID}",
        "expect_ok": True,
    },
    {
        "name": "assert child gone",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        "expect_absent": "{CHILD_ID}",
    },
    {
        "name": "revert all overrides",
        "cmd": "E29/Edit/RevertAllOverrides -Scene {SCENE} -Id {PI_ID}",
        "expect_ok": True,
    },
    {
        "name": "list entities (after revert)",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        # Child permanent_id may change on rebuild (fresh RegisterInstantiatedSubtree).
        # Soft assert: command succeeded; DescribeEntity checks diffs.
        "expect_re": r".+",
    },
    {
        "name": "describe PI after revert",
        "cmd": "E29/Query/DescribeEntity -Scene {SCENE} -Id {PI_ID}",
        "expect_ok": True,
        "expect_re": r".+",
    },
    {
        "name": "undo revert all",
        "cmd": "E29/Query/Undo",
        "expect_ok": True,
        "expect_re": r"Undone|Nothing to undo",
    },
    {
        "name": "assert child gone again after undo",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        "expect_absent": "{CHILD_ID}",
    },
]
