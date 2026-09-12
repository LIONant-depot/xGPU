"""removed_child_undo — delete child under PI, Undo, assert child back.

Best-effort stub wired to real Route() command names. Depends on DeleteEntity Undo
restoring the child (and, after the HierarchyDiffs patch, clearing PI diffs).
"""
from __future__ import annotations

from . import _constants as C

NAME = "removed_child_undo"
DESCRIPTION = (
    "Delete child under prefab instance, Undo, assert child returns "
    "(HierarchyDiffs cleared if DescribeEntity dumps them)."
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
        "name": "list entities (before)",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        "expect": "{CHILD_ID}",
    },
    {
        "name": "delete child",
        "cmd": "E29/Edit/DeleteEntity -Scene {SCENE} -Id {CHILD_ID}",
        "expect_ok": True,
    },
    {
        "name": "assert child gone (pre-undo)",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        "expect_absent": "{CHILD_ID}",
    },
    {
        "name": "undo",
        "cmd": "E29/Query/Undo",
        "expect_ok": True,
        "expect_re": r"Undone|Nothing to undo",
    },
    {
        "name": "assert child back",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        "expect": "{CHILD_ID}",
    },
    {
        "name": "describe PI after undo",
        "cmd": "E29/Query/DescribeEntity -Scene {SCENE} -Id {PI_ID}",
        "expect_ok": True,
        "expect_re": r".+",
    },
]
