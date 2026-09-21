"""The command surface is the AI-facing API: removing or renaming a command must be a deliberate act."""
import pytest

from harness import GOLDEN_DIR

GOLDEN = GOLDEN_DIR / "commands.txt"


def test_workspace_commands_match_golden(editor, request):
    current = sorted(editor.commands())
    if request.config.getoption("--update-golden"):
        GOLDEN.parent.mkdir(exist_ok=True)
        GOLDEN.write_text("\n".join(current) + "\n")
        pytest.skip(f"golden updated ({len(current)} commands)")
    expected = GOLDEN.read_text().split()
    removed, added = sorted(set(expected) - set(current)), sorted(set(current) - set(expected))
    assert not removed and not added, (
        f"command surface changed - removed: {removed or '-'}, added: {added or '-'}. "
        "If intended, run: python -m pytest smoke --update-golden")
