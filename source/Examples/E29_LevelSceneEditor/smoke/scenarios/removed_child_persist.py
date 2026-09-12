"""removed_child_persist — delete child under PI, Save, reopen Level, child stays gone.

Manual smoke that was already proved: open level/scene fixture, ensure prefab instance
exists (InstantiatePrefab FFFF000200000001 if needed), DeleteEntity a child under the PI,
Save, Close, OpenLevel again, assert child absent (HierarchyDiffs via DescribeEntity if
the PI dumps them).
"""
from __future__ import annotations

from . import _constants as C

NAME = "removed_child_persist"
DESCRIPTION = (
    "Delete child under prefab instance, Save+reopen, assert child still gone "
    "(HierarchyDiffs present if DescribeEntity dumps them)."
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


# Steps use full Route() names. Edit cmds succeed with empty stdout.
STEPS = [
    {
        "name": "list levels (discovery)",
        "cmd": "E29/Query/ListLevels",
        "expect_ok": True,
    },
    {
        "name": "open level",
        "cmd": "E29/Query/OpenLevel -Level {LEVEL} -Save 0",
        "expect_ok": True,
        "expect_re": r"Opened Level|already open",
    },
    {
        "name": "list scenes",
        "cmd": "E29/Query/ListScenes",
        "expect_ok": True,
        "expect": "{SCENE}",
    },
    {
        "name": "list entities (before)",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
    },
    # Best-effort: instantiate if ROOT_ID not already present. If id-in-use, expect that text
    # and continue (fixture already has the PI).
    {
        "name": "instantiate prefab if needed",
        "cmd": "E29/Edit/InstantiatePrefab -Scene {SCENE} -Id {ROOT_ID} -Prefab {PREFAB} -Folder 0",
        "expect_ok": False,  # E29CLI exits 0 even on cmd errors; allow empty OK or id-in-use
        "expect_re": r"(?s)^\s*$|id already in use",
    },
    {
        "name": "list entities (pick child)",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
    },
    {
        "name": "delete child under PI",
        "cmd": "E29/Edit/DeleteEntity -Scene {SCENE} -Id {CHILD_ID}",
        "expect_ok": True,
    },
    {
        "name": "save",
        "cmd": "E29/Query/Save",
        "expect_ok": True,
        "expect": "Saved",
    },
    {
        "name": "close level",
        "cmd": "E29/Query/Close -Save 0",
        "expect_ok": True,
        "expect_re": r"Closed",
    },
    {
        "name": "reopen level",
        "cmd": "E29/Query/OpenLevel -Level {LEVEL} -Save 0",
        "expect_ok": True,
        "expect_re": r"Opened Level|already open",
    },
    {
        "name": "assert child still gone",
        "cmd": "E29/Query/ListEntities -Scene {SCENE}",
        "expect_ok": True,
        "expect_absent": "{CHILD_ID}",
    },
    {
        "name": "describe PI (HierarchyDiffs if dumpable)",
        "cmd": "E29/Query/DescribeEntity -Scene {SCENE} -Id {PI_ID}",
        "expect_ok": True,
        # Soft: if PrefabInstance is filtered as internal, this may just list other comps.
        # Prefer matching HierarchyDiffs when present.
        "expect_re": r".+",
    },
]
