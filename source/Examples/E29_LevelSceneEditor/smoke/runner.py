"""E29CLI smoke runner — one command per process via named-pipe client.

E29CLI.exe does NOT host the editor. It connects to
\\\\.\\pipe\\E29_LevelSceneEditor_Console on a live xGPU_unit_test (E29 example),
sends one routed command line, prints the response, and exits.

Route shape (xundo::history::Route):
  E29/Edit/<Command>  ...   undoable mutations
  E29/Query/<Command> ...   read-only / session actions
"""
from __future__ import annotations

import re
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, MutableMapping, Optional


DEFAULT_CLI_REL = Path("Build") / "xGPUExamples.vs2022" / "Debug" / "E29CLI.exe"
DEFAULT_PROJECT_REL = Path("example.lionprj")


@dataclass
class CmdResult:
    cmd: str
    returncode: int
    stdout: str
    stderr: str
    elapsed_s: float

    @property
    def combined(self) -> str:
        return (self.stdout or "") + (("\n" + self.stderr) if self.stderr else "")


@dataclass
class StepFailure(Exception):
    step_index: int
    step: Mapping[str, Any]
    result: Optional[CmdResult]
    reason: str

    def __str__(self) -> str:  # pragma: no cover - formatting
        return f"step {self.step_index}: {self.reason}"


# Heuristic: command-name error replies look like "OpenLevel: bad arguments".
_ERROR_LINE = re.compile(
    r"(?im)^(?:Could not connect|Malformed command|"
    r"(?:OpenLevel|Close|CloseScene|Save|Undo|Redo|DeleteEntity|CreateEntity|"
    r"InstantiatePrefab|ListEntities|ListScenes|ListLevels|DescribeEntity|"
    r"ListFolders|AddScene|RemoveScene)\s*:\s*(?!.*already open))"
)


def looks_like_error(text: str) -> bool:
    if not text:
        return False
    if "Could not connect to E29" in text:
        return True
    if "Malformed command" in text:
        return True
    return bool(_ERROR_LINE.search(text))


class SmokeRunner:
    def __init__(
        self,
        cli: Path,
        *,
        project: Optional[Path] = None,
        timeout: float = 30.0,
        repo_root: Optional[Path] = None,
        dry_run: bool = False,
    ) -> None:
        self.cli = Path(cli)
        self.project = Path(project) if project else None
        self.timeout = float(timeout)
        self.repo_root = Path(repo_root) if repo_root else None
        self.dry_run = dry_run
        self.vars: MutableMapping[str, str] = {}
        self.history: list[CmdResult] = []

    def format_cmd(self, template: str) -> str:
        try:
            return template.format_map(self.vars)
        except KeyError as e:
            raise StepFailure(-1, {"cmd": template}, None, f"missing scenario var {{{e.args[0]}}}") from e

    def run_cmd(self, cmd: str) -> CmdResult:
        cmd = self.format_cmd(cmd)
        if self.dry_run:
            print(f"  [dry-run] {cmd}")
            return CmdResult(cmd=cmd, returncode=0, stdout="(dry-run)", stderr="", elapsed_s=0.0)

        if not self.cli.is_file():
            raise StepFailure(
                -1,
                {"cmd": cmd},
                None,
                f"E29CLI not found: {self.cli} (build E29CLI or pass --cli)",
            )

        t0 = time.perf_counter()
        try:
            proc = subprocess.run(
                [str(self.cli), cmd],
                capture_output=True,
                text=True,
                timeout=self.timeout,
                cwd=str(self.repo_root) if self.repo_root else None,
            )
        except subprocess.TimeoutExpired as e:
            elapsed = time.perf_counter() - t0
            out = (e.stdout or "") if isinstance(e.stdout, str) else ""
            err = (e.stderr or "") if isinstance(e.stderr, str) else f"timeout after {self.timeout}s"
            result = CmdResult(cmd=cmd, returncode=124, stdout=out, stderr=err, elapsed_s=elapsed)
            self.history.append(result)
            raise StepFailure(-1, {"cmd": cmd}, result, err) from e

        elapsed = time.perf_counter() - t0
        result = CmdResult(
            cmd=cmd,
            returncode=proc.returncode,
            stdout=proc.stdout or "",
            stderr=proc.stderr or "",
            elapsed_s=elapsed,
        )
        self.history.append(result)
        return result

    def check_step(self, step: Mapping[str, Any], result: CmdResult, index: int) -> None:
        expect_ok = bool(step.get("expect_ok", True))
        expect = step.get("expect")
        expect_re = step.get("expect_re")
        expect_absent = step.get("expect_absent")
        text = result.combined

        if expect_ok:
            if result.returncode != 0:
                raise StepFailure(
                    index,
                    step,
                    result,
                    f"E29CLI exit {result.returncode}; stderr={result.stderr!r}; stdout={result.stdout!r}",
                )
            if looks_like_error(text) and not expect and not expect_re:
                # Allow callers to assert specific error text via expect*.
                raise StepFailure(index, step, result, f"response looks like an error:\n{text}")

        if expect is not None:
            needle = self.format_cmd(str(expect))
            if needle not in text:
                raise StepFailure(index, step, result, f"expected substring {needle!r} not in:\n{text}")

        if expect_re is not None:
            pat = self.format_cmd(str(expect_re))
            if not re.search(pat, text, re.MULTILINE | re.DOTALL):
                raise StepFailure(index, step, result, f"expected regex {pat!r} not in:\n{text}")

        if expect_absent is not None:
            bad = self.format_cmd(str(expect_absent))
            if bad in text:
                raise StepFailure(index, step, result, f"unexpected substring {bad!r} found in:\n{text}")

        assert_fn: Optional[Callable[[CmdResult, "SmokeRunner"], None]] = step.get("assert")
        if callable(assert_fn):
            assert_fn(result, self)

    def run_steps(self, steps: Iterable[Mapping[str, Any]], *, fail_fast: bool = True) -> list[CmdResult]:
        results: list[CmdResult] = []
        for i, step in enumerate(steps):
            name = step.get("name") or step.get("cmd") or f"step-{i}"
            print(f"[{i}] {name}")
            if "set" in step and isinstance(step["set"], Mapping):
                for k, v in step["set"].items():
                    self.vars[str(k)] = self.format_cmd(str(v))
                continue
            if "cmd" not in step:
                raise StepFailure(i, step, None, "step missing 'cmd'")
            try:
                result = self.run_cmd(str(step["cmd"]))
                print(f"    exit={result.returncode} ({result.elapsed_s:.2f}s)")
                if result.stdout.strip():
                    preview = result.stdout.strip().splitlines()
                    for line in preview[:8]:
                        print(f"    | {line}")
                    if len(preview) > 8:
                        print(f"    | ... ({len(preview) - 8} more lines)")
                if not self.dry_run:
                    self.check_step(step, result, i)
                results.append(result)
            except StepFailure:
                if fail_fast:
                    raise
                raise
        return results


def default_cli_path(repo_root: Path) -> Path:
    return repo_root / DEFAULT_CLI_REL


def default_project_path(repo_root: Path) -> Path:
    return repo_root / DEFAULT_PROJECT_REL
