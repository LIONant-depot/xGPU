"""Shared guids / defaults for E29 smoke scenarios.

Discovered from E29CLI.cpp comments + manual smoke notes:
  Scene 08C298C9F6668005 appears in E29CLI usage examples (Select -Scene ...).
  Prefab FFFF000200000001 is the fixture used in the removed-child persist smoke.

LEVEL_GUID may equal the scene guid in some fixtures, or a distinct Level that owns
that scene — override via env / run_smoke --var LEVEL=... / scenario setup.
OpenLevel takes -Level (not -Scene). ListLevels / ListScenes can refine at runtime.
"""
from __future__ import annotations

import os

SCENE_GUID = os.environ.get("E29_SMOKE_SCENE", "08C298C9F6668005")
# Default: same hex as the known scene fixture; override if ListLevels shows otherwise.
LEVEL_GUID = os.environ.get("E29_SMOKE_LEVEL", SCENE_GUID)
PREFAB_GUID = os.environ.get("E29_SMOKE_PREFAB", "FFFF000200000001")
# Root permanent_id for a fresh InstantiatePrefab (8 hex digits). Override if colliding.
INSTANCE_ROOT_ID = os.environ.get("E29_SMOKE_INSTANCE_ROOT", "A1000001")
# Child permanent_id under the PI to delete — set after ListEntities / known fixture.
CHILD_ID = os.environ.get("E29_SMOKE_CHILD", "")
# PI root id (entity carrying prefab_instance) — optional, for DescribeEntity asserts.
PI_ID = os.environ.get("E29_SMOKE_PI", "")
