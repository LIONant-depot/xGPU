"""Play / Pause / Step / Stop, the single-Play lock, and 'keep property tweaks?'.

Play saves the open level first, so the `level` fixture (clean document) is required: the save then rewrites
identical content, and the harness refuses to Play over unsaved edits.
"""
from harness import b64


def test_transport_cycle(editor, level):
    assert editor.cmd("Play").startswith("Play requested")
    editor.wait_play_state("Playing")
    assert "already playing" in editor.cmd("Play")

    assert editor.cmd("Pause") == "Paused"
    assert editor.cmd("Step") == "Step" and editor.play_state() == "Paused"     # one tick, stays paused
    assert editor.cmd("Play") == "Resumed"
    assert editor.play_state() == "Playing"

    assert editor.cmd("Stop") == "Stop requested"
    editor.wait_play_state("Stopped")

    # The single-Play lock must have been released by Stop, or this second Play would be refused.
    assert editor.cmd("Play").startswith("Play requested")
    editor.wait_play_state("Playing")


def test_step_from_stopped_starts_play_and_lands_paused(editor, level):
    assert editor.cmd("Step") == "Step"
    editor.wait_play_state("Paused")


def test_pause_and_step_are_refused_when_they_make_no_sense(editor, level):
    assert "not playing" in editor.cmd("Pause")
    assert "already stopped" in editor.cmd("Stop")


def test_stop_asks_about_tweaks_and_keep_or_discard_is_honoured(editor, level):
    scene, entity, transform = level.find_with_component("Transform")
    path = "Transform/Position/X"
    original = editor.property_value(level.name, scene, entity, path)
    type_guid = level.property_type(entity, path, scene)
    tweak = (f"SetProperty -Scene {scene} -Id {entity} -Component {transform} -Path {b64(path)} -TypeGuid {type_guid}"
             f" -Before {b64(original)} -After {b64('5.000000')}")

    def play_and_tweak():
        editor.cmd("Play")
        editor.wait_play_state("Playing")
        level.ok(tweak)
        assert editor.property_value(level.name, scene, entity, path) == "5.000000"

    play_and_tweak()
    assert "changed during Play" in editor.cmd("Stop")             # no -Keep: it asks instead of guessing
    assert editor.play_state() == "Paused"                         # ...and waits, frozen, for the answer
    editor.cmd("Stop -Keep false")
    editor.wait_play_state("Stopped")
    assert editor.property_value(level.name, scene, entity, path) == original

    play_and_tweak()
    editor.cmd("Stop -Keep true")
    editor.wait_play_state("Stopped")
    assert editor.property_value(level.name, scene, entity, path) == "5.000000"
    assert level.cmd("Undo") == "Undone"                           # the kept tweak is ONE undo step
    assert editor.property_value(level.name, scene, entity, path) == original
