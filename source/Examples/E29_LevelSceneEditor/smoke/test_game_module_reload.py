"""Game.dll hot reload: a changed game module is rebuilt and every world is destroyed and recreated around the swap.

Play is what triggers it. The recompile-check finds the DLL stale (its inputs are newer), rebuilds the script project (tens of seconds), swaps the DLL and
restores the open level in a fresh world before entering Play. The level must come back exactly as it was.
"""
import os
from pathlib import Path

from harness import REPO

GAME_SOURCE = REPO / "plugins" / "xscript_module.plugin" / "source" / "Runtime" / "xscript_game_entry.cpp"


def test_play_after_a_game_source_change_reloads_the_module_and_keeps_the_level(editor, level):
    before = {scene: level.entities(scene) for scene, _ in level.scenes}
    reloads_before = editor.log_text().count("[Vn restore]")

    os.utime(GAME_SOURCE)                               # looks edited: the module is now older than its source
    assert editor.cmd("Play").startswith("Play requested")
    editor.wait_play_state("Playing", timeout=240)      # includes the module rebuild

    log = editor.log_text()
    assert "Game.dll: rebuild succeeded" in log
    assert log.count("[Vn restore]") == reloads_before + 1, "the world should have been rebuilt exactly once"
    assert {scene: level.entities(scene) for scene, _ in level.scenes} == before
    assert level.ed.sessions()[0].name == level.name

    assert editor.cmd("Stop") == "Stop requested"
    editor.wait_play_state("Stopped")
    assert {scene: level.entities(scene) for scene, _ in level.scenes} == before
