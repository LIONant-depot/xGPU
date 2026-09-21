"""E29 smoke-test harness: launches the editor, talks to its Command Console pipe, detects crashes.

The editor exposes one named pipe (byte mode, one request per connection): write "<command>\\n", read the
response until the server disconnects. Commands are dispatched on the editor's main thread, one per frame.

Command grammar (xeditor::host::dispatch):
    <Command> ...                   workspace command      (OpenLevel, Play, Stop, Save, ListLevels, ...)
    <Session name>\\<Command> ...    command on a session   (Main Level\\CreateEntity ...)
    help | list                     command names / open sessions
Edit commands return an EMPTY string on success and an error text on failure; query commands return text.
"""
from __future__ import annotations

import base64
import ctypes
import re
import subprocess
import threading
import time
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

PIPE = r"\\.\pipe\xEditor_Console"
SMOKE_DIR = Path(__file__).resolve().parent
GOLDEN_DIR = SMOKE_DIR / "golden"
REPO = SMOKE_DIR.parents[3]
DEFAULT_EXE = REPO / "Build" / "xGPUExamples.vs2022" / "Release" / "xGPU_unit_test.exe"


_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_k32.CreateFileW.restype = wintypes.HANDLE
_k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
_k32.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
_k32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
_k32.CancelIoEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
_k32.CloseHandle.argtypes = [wintypes.HANDLE]
_INVALID_HANDLE = wintypes.HANDLE(-1).value
_GENERIC_RW = 0xC0000000
_OPEN_EXISTING = 3
_EOF_ERRORS = (109, 233)      # ERROR_BROKEN_PIPE / ERROR_PIPE_NOT_CONNECTED: the server hung up after its reply


# Commands that write the developer's own project data. The suite runs against the real example project, so a
# test must opt in (allow_disk=True) instead of touching disk by accident.
DISK_WRITERS = frozenset({
    "Save", "SaveAssets", "CreateAsset", "CreateLibrary", "RenameAsset", "MoveAsset", "DeleteAsset", "RestoreAsset",
    "RenameAssetFile", "MoveAssetFile", "DeleteAssetFileToTrash", "RestoreAssetFileFromTrash", "CopyAssetFile",
    "AddProjectModuleReference", "RemoveProjectModuleReference", "RegenerateProjectModuleSources",
    "AddScriptSourceFile", "RemoveScriptSourceFile", "SetScriptSourceFileContent", "RenameScriptSourceFile",
    "SourceControlCommit", "SourceControlPull", "SourceControlPush", "SourceControlRevert", "SourceControlStage",
    "SourceControlLock", "SourceControlUnlock",
})
# Play (and Step from Stopped) saves the open level to disk first, so it is only safe on a clean document,
# where that save rewrites identical content.
SAVES_ON_START = frozenset({"Play", "Step"})


class EditorCrashed(RuntimeError):
    pass


class CommandError(AssertionError):
    """An edit command answered with an error text instead of an empty success reply."""


def b64(text: str) -> str:
    """Property paths and values travel as base64 of their TEXT form (not raw bytes)."""
    return base64.b64encode(text.encode()).decode()


@dataclass
class Session:
    name: str
    type_guid: str
    guid: str
    dirty: bool

    def __str__(self) -> str:
        return self.name


class Editor:
    def __init__(self, exe: Path = DEFAULT_EXE, *, log_dir: Optional[Path] = None, min_gap: float = 0.05) -> None:
        self.exe = Path(exe)
        self.log_dir = Path(log_dir) if log_dir else SMOKE_DIR / ".logs"
        self.min_gap = min_gap
        self.proc: Optional[subprocess.Popen] = None
        self.launches = 0
        self._last_cmd_at = 0.0

    # ------------------------------------------------------------------ process
    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def exit_code(self) -> Optional[int]:
        return None if self.proc is None else self.proc.poll()

    def start(self, ready_timeout: float = 120.0) -> None:
        if not self.exe.is_file():
            raise FileNotFoundError(f"editor exe not found: {self.exe} (build xGPU_unit_test or pass --exe)")
        self.log_dir.mkdir(exist_ok=True)
        self.launches += 1
        log = open(self.log_dir / f"editor_{self.launches}.log", "wb")
        self.proc = subprocess.Popen([str(self.exe)], cwd=str(self.exe.parent), stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + ready_timeout
            while time.monotonic() < deadline:
                if not self.alive():
                    raise EditorCrashed(f"editor exited during startup (exit {self.describe_exit()})")
                try:
                    self._roundtrip("help", timeout=5.0)
                    return
                except (OSError, TimeoutError):
                    time.sleep(0.5)
            raise TimeoutError(f"editor pipe not ready after {ready_timeout}s")
        except BaseException:
            self.stop()                     # never leave a half-started editor holding the pipe
            raise

    def stop(self) -> None:
        if self.alive():
            self.proc.kill()
            self.proc.wait(timeout=10)

    def ensure_running(self) -> None:
        if not self.alive():
            self.start()

    def describe_exit(self) -> str:
        code = self.exit_code()
        return "running" if code is None else f"{code & 0xFFFFFFFF:#010x}"

    # ------------------------------------------------------------------ pipe
    def _roundtrip(self, line: str, timeout: float) -> str:
        """One request/response. Connecting retries while the server re-creates its pipe instance; a reply
        that does not finish within `timeout` is cancelled and raises TimeoutError."""
        deadline = time.monotonic() + timeout
        while True:
            h = _k32.CreateFileW(PIPE, _GENERIC_RW, 0, None, _OPEN_EXISTING, 0, None)
            if h != _INVALID_HANDLE:
                break
            err = ctypes.get_last_error()
            if time.monotonic() >= deadline:
                raise TimeoutError(f"pipe not available for {line!r}: {ctypes.WinError(err)}")
            time.sleep(0.05)

        timer = threading.Timer(max(0.1, deadline - time.monotonic()), lambda: _k32.CancelIoEx(h, None))
        timer.start()
        try:
            data = line.encode() + b"\n"
            written = wintypes.DWORD()
            if not _k32.WriteFile(h, data, len(data), ctypes.byref(written), None):
                raise OSError(f"write to pipe failed: {ctypes.WinError(ctypes.get_last_error())}")
            chunks, buf, got = [], ctypes.create_string_buffer(4096), wintypes.DWORD()
            while True:
                if not _k32.ReadFile(h, buf, len(buf), ctypes.byref(got), None):
                    if ctypes.get_last_error() in _EOF_ERRORS:
                        break
                    raise TimeoutError(f"no complete reply to {line!r} within {timeout}s")
                if got.value == 0:
                    break
                chunks.append(buf.raw[:got.value])
            return b"".join(chunks).decode(errors="replace")
        finally:
            timer.cancel()
            _k32.CloseHandle(h)

    def cmd(self, line: str, *, timeout: float = 30.0, allow_disk: bool = False) -> str:
        """Send one command; returns the reply with trailing whitespace trimmed."""
        name = re.split(r"[\\/]", line.split(" -", 1)[0])[-1].split()[0]   # "<session>\<Cmd> -Opt ..." or "E29/Edit/<Cmd>"
        if name in DISK_WRITERS and not allow_disk:
            raise PermissionError(f"{name} writes project data; pass allow_disk=True if this test really means to")
        if name in SAVES_ON_START and any(s.dirty for s in self.sessions()):
            raise PermissionError(f"{name} saves the open level first; refusing while a session has unsaved edits")
        gap = self.min_gap - (time.monotonic() - self._last_cmd_at)
        if gap > 0:
            time.sleep(gap)
        try:
            reply = self._roundtrip(line, timeout)
        except (TimeoutError, OSError) as e:
            if not self.alive():
                raise EditorCrashed(f"editor died running {line!r} (exit {self.describe_exit()})") from e
            raise
        finally:
            self._last_cmd_at = time.monotonic()
        if not reply:                       # success for an edit command - but also all a dying editor leaves behind
            time.sleep(0.05)
            if not self.alive():
                raise EditorCrashed(f"editor died running {line!r} (exit {self.describe_exit()})")
        return reply.rstrip()

    def ok(self, line: str, **kw) -> None:
        """An edit command: success is an empty reply."""
        reply = self.cmd(line, **kw)
        if reply:
            raise CommandError(f"{line!r} -> {reply!r}")

    def fails(self, line: str, **kw) -> str:
        """A command that must be refused with a message (never crash, never succeed silently)."""
        reply = self.cmd(line, **kw)
        assert reply, f"{line!r} was accepted but should have been refused"
        return reply

    def wait_for(self, line: str, pattern: str, *, timeout: float = 60.0, poll: float = 0.25) -> str:
        """Poll a query until its reply matches the regex."""
        deadline = time.monotonic() + timeout
        reply = ""
        while time.monotonic() < deadline:
            reply = self.cmd(line)
            if re.search(pattern, reply):
                return reply
            time.sleep(poll)
        raise TimeoutError(f"{line!r} never matched /{pattern}/ within {timeout}s (last: {reply!r})")

    # ------------------------------------------------------------------ typed helpers
    def commands(self) -> list[str]:
        """Workspace command names, in the order `help` prints them."""
        lines = self.cmd("help").splitlines()
        return [l.strip() for l in lines[1:lines.index("Sessions:")]] if "Sessions:" in lines else []

    def sessions(self) -> list[Session]:
        out = []
        for line in self.cmd("list").splitlines():
            m = re.match(r"(.+?)\s{2}type=(\w+) inst=(\w+) dirty=(\d)", line)
            if m:
                out.append(Session(m[1], m[2], m[3], m[4] == "1"))
        return out

    def levels(self) -> list[tuple[str, str]]:
        """[(guid, name)] from ListLevels."""
        return [(m[1], m[2].strip()) for l in self.cmd("ListLevels").splitlines() if (m := re.match(r"(\w{16})\s+(.*)", l))]

    def play_state(self) -> str:
        return re.search(r"PlayState=(\w+)", self.cmd("GetPlayState"))[1]

    def wait_play_state(self, state: str, timeout: float = 60.0) -> None:
        self.wait_for("GetPlayState", rf"PlayState={state}\b", timeout=timeout)

    def entities(self, session: str, scene: str) -> dict[str, str]:
        """{entity id: label} for a scene."""
        return {m[1]: m[2] for l in self.cmd(f"{session}\\ListEntities -Scene {scene}").splitlines()
                if (m := re.match(r"(\w{8})\s+(.*)", l))}

    def describe(self, session: str, scene: str, entity: str) -> str:
        return self.cmd(f"{session}\\DescribeEntity -Scene {scene} -Id {entity}")

    def property_value(self, session: str, scene: str, entity: str, path: str) -> str:
        m = re.search(rf"^\s*{re.escape(path)} = (\S+)", self.describe(session, scene, entity), re.M)
        assert m, f"{path} not found in DescribeEntity for {entity}"
        return m[1]
