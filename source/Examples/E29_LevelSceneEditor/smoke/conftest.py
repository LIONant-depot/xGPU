"""pytest fixtures for the E29 smoke suite.

    python -m pytest smoke -q                       # launches the Release editor itself
    python -m pytest smoke -q --exe <path>          # a different build
    python -m pytest smoke -q --update-golden       # accept the current command surface

One editor process serves the whole run. It is restarted if a test kills it, so a crash fails exactly the
test that caused it (the exit code is in the failure) instead of every test after it.
"""
from __future__ import annotations

import itertools
import re
from dataclasses import dataclass
from pathlib import Path

import pytest

from harness import DEFAULT_EXE, GOLDEN_DIR, Editor


def pytest_addoption(parser):
    parser.addoption("--exe", default=str(DEFAULT_EXE), help="xGPU_unit_test.exe to launch")
    parser.addoption("--update-golden", action="store_true", help="rewrite golden files from the running editor")


@pytest.fixture(scope="session")
def editor(request):
    ed = Editor(Path(request.config.getoption("--exe")))
    ed.start()
    yield ed
    ed.stop()


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    if call.when == "call":
        item.call_failed = outcome.get_result().failed


@pytest.fixture(autouse=True)
def _editor_alive(request, editor):
    """Restart a dead editor before each test; fail the test that killed it (once)."""
    editor.ensure_running()
    yield
    if not editor.alive() and not getattr(request.node, "call_failed", False):
        pytest.fail(f"editor crashed during this test (exit {editor.describe_exit()}); log: {editor.log_dir}", pytrace=False)


@dataclass
class Level:
    """The example project's first level, opened clean. Everything a test creates lives only in memory."""
    ed: Editor
    guid: str
    name: str
    scenes: list                      # [(guid, name)]
    _ids: itertools.count

    @property
    def scene(self) -> str:
        return self.scenes[0][0]

    def cmd(self, line: str, **kw) -> str:
        return self.ed.cmd(f"{self.name}\\{line}", **kw)

    def ok(self, line: str, **kw) -> None:
        self.ed.ok(f"{self.name}\\{line}", **kw)

    def new_entity(self, scene: str | None = None) -> str:
        """Creates an empty entity with a fresh id and returns the id."""
        entity = f"7E57{next(self._ids):04X}"
        self.ok(f"CreateEntity -Scene {scene or self.scene} -Id {entity} -Folder 0")
        return entity

    def entities(self, scene: str | None = None) -> dict:
        return self.ed.entities(self.name, scene or self.scene)

    def describe(self, entity: str, scene: str | None = None) -> str:
        return self.ed.describe(self.name, scene or self.scene, entity)

    def dirty(self) -> bool:
        return next(s.dirty for s in self.ed.sessions() if s.name == self.name)

    def find_with_component(self, component: str):
        """(scene, entity, component guid) of the first existing entity that has the named component."""
        for scene, _ in self.scenes:
            for entity in self.entities(scene):
                m = re.search(rf"\[(\w{{16}})\] {re.escape(component)}", self.describe(entity, scene))
                if m:
                    return scene, entity, m[1]
        pytest.skip(f"the example project has no entity with a {component} component")

    def property_type(self, entity: str, path: str, scene: str | None = None) -> str:
        """The TypeGuid DescribeEntity prints next to a property."""
        m = re.search(rf"{re.escape(path)} = \S+\s+\(TypeGuid (\w+)\)", self.describe(entity, scene))
        assert m, f"{path} not on {entity}"
        return m[1]


@pytest.fixture
def level(editor):
    guid, name = editor.levels()[0]
    editor.cmd("Close -Save 0")
    editor.cmd(f"OpenLevel -Level {guid} -Save 0")
    editor.wait_for("GetPlayState", r"Building=false", timeout=120)   # the startup Game.dll check must settle
    scenes = [(m[1], m[2].strip()) for l in editor.cmd(f"{name}\\ListScenes").splitlines()
              if (m := re.match(r"(\w{16})\s+(.*)", l))]
    lv = Level(editor, guid, name, scenes, itertools.count(1))
    yield lv
    if editor.alive():
        if editor.play_state() != "Stopped":
            editor.cmd("Stop -Keep false")
            editor.wait_play_state("Stopped")
        editor.cmd("Close -Save 0")
