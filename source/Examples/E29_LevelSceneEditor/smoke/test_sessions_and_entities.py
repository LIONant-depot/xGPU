"""Level sessions, entity create/delete, and their undo/redo. Everything here stays in memory: nothing is saved."""
from harness import b64


def test_open_level_creates_a_clean_session_and_close_removes_it(editor, level):
    [session] = editor.sessions()
    assert session.name == level.name and not session.dirty
    assert editor.cmd("Close -Save 0") == "Closed"
    assert editor.sessions() == []


def test_create_entity_undo_redo(level):
    before = level.entities()
    entity = level.new_entity()
    assert entity in level.entities() and level.dirty()

    assert level.cmd("Undo") == "Undone"
    assert level.entities() == before and not level.dirty()

    assert level.cmd("Redo") == "Redone"
    assert entity in level.entities()


def test_delete_entity_then_undo_restores_it(level):
    entity = level.new_entity()
    level.ok(f"DeleteEntity -Scene {level.scene} -Id {entity}")
    assert entity not in level.entities()

    assert level.cmd("Undo") == "Undone"
    assert entity in level.entities()


def test_refused_commands_answer_with_a_message(editor, level):
    assert "scene not found" in level.cmd("CreateEntity -Scene ZZZ -Id 7E57FFFF -Folder 0")
    assert "required option is missing" in level.cmd("CreateEntity")
    assert "Unable find the command" in level.cmd("NoSuchCommand")
    assert "No open session" in editor.cmd("NoSuchSession\\Undo")


def test_property_edit_undo_redo(level):
    entity = level.new_entity()
    _, _, transform = level.find_with_component("Transform")
    level.ok(f"AddComponent -Scene {level.scene} -Id {entity} -Component {transform}")

    path = "Transform/Position/X"
    type_guid = level.property_type(entity, path)
    level.ok(f"SetProperty -Scene {level.scene} -Id {entity} -Component {transform} -Path {b64(path)}"
             f" -TypeGuid {type_guid} -Before {b64('0.000000')} -After {b64('5.000000')}")
    assert level.ed.property_value(level.name, level.scene, entity, path) == "5.000000"

    level.cmd("Undo")
    assert level.ed.property_value(level.name, level.scene, entity, path) == "0.000000"
    level.cmd("Redo")
    assert level.ed.property_value(level.name, level.scene, entity, path) == "5.000000"


def test_sanity_scan_runs_as_an_idle_task(editor, level):
    assert "scanning" in editor.cmd("RunSanityCheck")
    editor.wait_for("GetIdleTasks", r"(Done|Cancelled)\s+Scene Sanity Scan", timeout=30)
