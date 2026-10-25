#!/usr/bin/env python3

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
# The DarkBASIC Classic examples are not redistributed: --corpus-root names a
# local copy laid out like the original examples folder (demos/, tutorials/).
MANIFEST_PREFIX = "examples/dbc/"
DBC_ROOT = Path()
DEFAULT_MANIFEST = ROOT / "tests/qualification/dbc_example_expectations.tsv"
EXPECTED_PROGRAMS = 196
CODE = re.compile(r"\b(DBC\d{4})\b")
DOCTEST_SUMMARY = re.compile(
    r"\[doctest\]\s+test cases:\s*(\d+)\s*\|\s*(\d+)\s+passed\s*\|\s*(\d+)\s+failed"
)
STAGES = ("lex", "parse", "bind", "lower", "verify", "verified")
RUN_OUTCOMES = {"bounded"}
ISOLATION_MODES = {"read-only", "copy-tree"}
INTERACTIONS = {"none", "audio", "filesystem", "joystick", "keyboard", "mouse", "network"}
REQUIRED_COLUMNS = (
    "id",
    "source",
    "category",
    "expected_stage",
    "expected_diagnostics",
    "expected_run",
    "max_frames",
    "timeout_seconds",
    "isolation",
    "interactions",
    "oracle",
)
P0_ORACLES = {
    "demos/iced/iced": "test_engine_builtins.cpp::Loaded screen bitmaps persist and object defaults are isolated by execution profile",
    "demos/jetski/jetski": "test_engine_builtins.cpp::JetSki rider keeps Classic limb numbering transforms and hidden attachment meshes",
    "demos/room/room": "test_engine_builtins.cpp::Room collision defaults leave the centre open and retain polygon walls",
    "demos/swarm/swarm": "test_engine_builtins.cpp::Swarm Classic plains face negative Z while DBPro orientations remain unchanged",
}
WRITE_COMMAND_RE = re.compile(
    r"\b(?:open\s+to\s+write|make\s+file|delete\s+file|copy\s+file|move\s+file|"
    r"rename\s+file|make\s+directory|delete\s+directory|pack\s+dir|"
    r"read\s+(?:fileblock|dirblock)|write\s+(?:fileblock|dirblock)|"
    r"write\s+(?:byte|word|dword|long|float|string|file)|"
    r"save\s+(?:array|bitmap|image|sound|music|object(?:\s+animation)?|mesh|static\s+objects))\b",
    re.IGNORECASE,
)
OPEN_TO_WRITE_LITERAL_RE = re.compile(
    r'\bopen\s+to\s+write\s+[^,\r\n]+,\s*"([^"\r\n]+)"',
    re.IGNORECASE,
)


def diagnostic_stage(code: str) -> str:
    number = int(code[3:])
    if number < 1100:
        return "lex"
    if number < 2000:
        return "parse"
    if number < 3000:
        return "bind"
    if number < 4000:
        return "lower"
    return "verify"


def discovered_sources() -> list[Path]:
    return sorted(
        (path for path in DBC_ROOT.rglob("*") if path.is_file() and path.suffix.lower() == ".dba"),
        key=lambda path: path.relative_to(DBC_ROOT).as_posix().lower(),
    )


def safe_repo_source(raw: str) -> Path:
    relative = Path(raw)
    if relative.is_absolute() or ".." in relative.parts:
        raise SystemExit(f"source must be a safe repository-relative path: {raw}")
    if not raw.startswith(MANIFEST_PREFIX):
        raise SystemExit(f"source must start with {MANIFEST_PREFIX}: {raw}")
    source = DBC_ROOT / raw[len(MANIFEST_PREFIX):]
    try:
        source.resolve().relative_to(DBC_ROOT.resolve())
    except ValueError as error:
        raise SystemExit(f"source must remain under the corpus root: {raw}") from error
    if not source.is_file() or source.suffix.lower() != ".dba":
        raise SystemExit(f"qualification source does not exist or is not DBA: {raw}")
    return source


def active_source_text(source: Path) -> str:
    lines: list[str] = []
    for line in source.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        stripped = line.lstrip()
        if re.match(r"(?i)rem(?:\s|$)", stripped) or stripped.startswith(("'", "`")):
            continue
        lines.append(line)
    return "\n".join(lines)


def requires_copy_isolation(source: Path) -> bool:
    return WRITE_COMMAND_RE.search(active_source_text(source)) is not None


def load_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if tuple(reader.fieldnames or ()) != REQUIRED_COLUMNS:
            raise SystemExit(f"qualification manifest schema is invalid: {path}")
        rows = list(reader)
    if len(rows) != EXPECTED_PROGRAMS:
        raise SystemExit(
            f"qualification manifest must contain exactly {EXPECTED_PROGRAMS} DBC programs, found {len(rows)}"
        )
    return rows


def validate_manifest(rows: list[dict[str, str]]) -> None:
    discovered = discovered_sources()
    if len(discovered) != EXPECTED_PROGRAMS:
        raise SystemExit(
            f"the corpus root must contain exactly {EXPECTED_PROGRAMS} case-insensitive .dba files, "
            f"found {len(discovered)}"
        )
    discovered_paths = {MANIFEST_PREFIX + path.relative_to(DBC_ROOT).as_posix() for path in discovered}
    described_paths: set[str] = set()
    identifiers: set[str] = set()
    folded_paths: set[str] = set()
    for row in rows:
        source = safe_repo_source(row["source"])
        source_path = MANIFEST_PREFIX + source.relative_to(DBC_ROOT).as_posix()
        identifier = source.relative_to(DBC_ROOT).with_suffix("").as_posix().lower()
        category = source.relative_to(DBC_ROOT).parts[0]
        if row["id"] != identifier:
            raise SystemExit(f"qualification id for {source_path} must be {identifier}, found {row['id']}")
        if not identifier or identifier in identifiers:
            raise SystemExit(f"duplicate or empty qualification id: {identifier}")
        identifiers.add(identifier)
        if source_path in described_paths or source_path.lower() in folded_paths:
            raise SystemExit(f"duplicate qualification source: {source_path}")
        described_paths.add(source_path)
        folded_paths.add(source_path.lower())
        if row["category"] != category or category not in {"demos", "tutorials"}:
            raise SystemExit(f"invalid category for {identifier}: {row['category']}")
        if row["expected_stage"] != "verified" or row["expected_diagnostics"] != "-":
            raise SystemExit(f"DBC qualification cannot accept a compiler diagnostic: {identifier}")
        if row["expected_run"] not in RUN_OUTCOMES:
            raise SystemExit(f"invalid expected_run for {identifier}: {row['expected_run']}")
        try:
            frames = int(row["max_frames"])
            timeout = float(row["timeout_seconds"])
            if frames <= 0 or timeout <= 0:
                raise ValueError
        except ValueError as error:
            raise SystemExit(f"invalid frame or timeout budget for {identifier}") from error
        if row["isolation"] not in ISOLATION_MODES:
            raise SystemExit(f"invalid isolation for {identifier}: {row['isolation']}")
        if requires_copy_isolation(source) and row["isolation"] != "copy-tree":
            raise SystemExit(f"writer must use copy-tree isolation: {identifier}")
        interactions = set(row["interactions"].split(","))
        if not interactions or not interactions <= INTERACTIONS or ("none" in interactions and len(interactions) != 1):
            raise SystemExit(f"invalid interactions for {identifier}: {row['interactions']}")
        if not row["oracle"].strip():
            raise SystemExit(f"qualification oracle is required: {identifier}")
    if discovered_paths != described_paths:
        missing = sorted(discovered_paths - described_paths)
        stale = sorted(described_paths - discovered_paths)
        raise SystemExit(f"qualification inventory mismatch; missing={missing}, stale={stale}")


def run_process(command: list[str], timeout: float, cwd: Path = ROOT) -> dict:
    started = time.monotonic()
    environment = os.environ.copy()
    environment.update({"LANG": "C", "LC_ALL": "C"})
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            text=True,
            errors="replace",
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "returncode": result.returncode,
            "timed_out": False,
            "duration_seconds": round(time.monotonic() - started, 3),
            "stdout": result.stdout[-20000:],
            "stderr": result.stderr[-20000:],
        }
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
        return {
            "command": command,
            "returncode": None,
            "timed_out": True,
            "duration_seconds": round(time.monotonic() - started, 3),
            "stdout": stdout[-20000:],
            "stderr": stderr[-20000:],
        }


def compiler_outcome(result: dict) -> tuple[str, list[str]]:
    codes = sorted(set(CODE.findall(result["stdout"] + result["stderr"])))
    if result["timed_out"]:
        return "timeout", codes
    if result["returncode"] == 0:
        return "verified", codes
    if not codes:
        return "failed", []
    stage = min((diagnostic_stage(code) for code in codes), key=STAGES.index)
    return stage, [code for code in codes if diagnostic_stage(code) == stage]


def execution_outcomes(result: dict, log_output: str) -> tuple[str, str, list[str]]:
    output = result["stdout"] + result["stderr"] + log_output
    codes = sorted(set(CODE.findall(output)))
    if result["timed_out"]:
        return "timeout", "timeout", codes
    if result["returncode"] in {3, 4}:
        return "failed", "not-started", codes
    if result["returncode"] == 5 and any(message in output for message in (
        "Failed to create the engine kernel",
        "Failed to initialize the OpenDBC runtime",
        "Runtime startup failed",
    )):
        return "failed", "not-started", codes
    if any(code.startswith("DBC5") for code in codes):
        return "started", "runtime-failure", codes
    if result["returncode"] == 0:
        return "started", "bounded", codes
    if result["returncode"] == 130:
        return "started", "interrupted", codes
    return "started", "runtime-failure", codes


def inspect_outcome(result: dict) -> tuple[dict | None, str]:
    if result["timed_out"]:
        return None, "inspect-timeout"
    if result["returncode"] != 0:
        return None, "inspect-failure"
    try:
        inspection = json.loads(result["stdout"])
    except json.JSONDecodeError:
        return None, "inspect-invalid-json"
    if inspection.get("dialect") != "dbc":
        return None, "inspect-profile-drift"
    return inspection, "inspected"


def isolated_runtime_target(source: Path, isolation: str, temporary: Path) -> Path:
    if isolation == "read-only":
        return source
    copied_root = temporary / "dbc"
    shutil.copytree(DBC_ROOT, copied_root)
    target = copied_root / source.relative_to(DBC_ROOT)
    for raw_path in OPEN_TO_WRITE_LITERAL_RE.findall(active_source_text(source)):
        relative = Path(raw_path.replace("\\", "/"))
        if relative.is_absolute() or ".." in relative.parts:
            continue
        generated = target.parent / relative
        if generated.is_file() or generated.is_symlink():
            generated.unlink()
    return target


def is_executable_file(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def resolve_p0_test_executable(explicit: Path | None, opendbc: Path) -> tuple[Path | None, str]:
    if explicit is not None:
        candidate = explicit.expanduser()
        if not candidate.is_absolute():
            candidate = (Path.cwd() / candidate).resolve()
        if not is_executable_file(candidate):
            raise SystemExit("--p0-test-executable must name an executable test_engine_builtins binary")
        return candidate, "explicit"

    candidates = [
        ROOT / "build/tests/unit/test_engine_builtins",
        ROOT / "build/tests/unit/Debug/test_engine_builtins",
    ]
    for ancestor in opendbc.resolve().parents:
        candidates.extend((
            ancestor / "tests/unit/test_engine_builtins",
            ancestor / "tests/unit/Debug/test_engine_builtins",
        ))
    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if is_executable_file(resolved):
            return resolved, "auto-detected"
    return None, "unavailable"


def p0_probe_outcome(result: dict) -> str:
    if result["timed_out"]:
        return "timeout"
    if result["returncode"] != 0:
        return "failed"
    summary = DOCTEST_SUMMARY.search(result["stdout"] + result["stderr"])
    if summary is None:
        return "invalid-output"
    total, passed, failed = (int(value) for value in summary.groups())
    if total != 1:
        return "selection-mismatch"
    return "passed" if passed == 1 and failed == 0 else "failed"


def run_p0_probes(test_executable: Path | None, rows: list[dict[str, str]],
                   timeout_override: float | None) -> dict[str, dict]:
    results: dict[str, dict] = {}
    for row in rows:
        reference = P0_ORACLES.get(row["id"])
        if reference is None:
            continue
        test_case = reference.split("::", 1)[1]
        if test_executable is None:
            results[row["id"]] = {
                "test": reference,
                "test_case": test_case,
                "observed": "not-available",
                "process": None,
            }
            continue
        timeout = timeout_override or float(row["timeout_seconds"])
        process = run_process(
            [
                str(test_executable),
                f"--test-case={test_case}",
                "--case-sensitive=true",
                "--no-colors=true",
            ],
            timeout,
        )
        results[row["id"]] = {
            "test": reference,
            "test_case": test_case,
            "observed": p0_probe_outcome(process),
            "process": process,
        }
    return results


def increment(summary: dict[str, Counter], section: str, outcome: str) -> None:
    summary[section][outcome] += 1


def main() -> None:
    parser = argparse.ArgumentParser(description="Inventory and qualify all 196 canonical DBC programs")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--corpus-root", type=Path, required=True,
                        help="local copy of the DarkBASIC Classic examples (demos/ and tutorials/)")
    parser.add_argument("--opendbc", type=Path)
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--run", action="store_true", dest="run_examples")
    parser.add_argument("--program", action="append", default=[], help="Run only this manifest id; repeatable")
    parser.add_argument("--timeout", type=float, help="Override each manifest process timeout")
    parser.add_argument("--max-frames", type=int, help="Override each manifest runtime frame budget")
    parser.add_argument(
        "--p0-test-executable",
        type=Path,
        help="Optional test_engine_builtins executable; auto-detected from the Debug build when omitted",
    )
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()
    global DBC_ROOT
    DBC_ROOT = args.corpus_root.resolve()
    if not DBC_ROOT.is_dir():
        raise SystemExit(f"--corpus-root is not a directory: {args.corpus_root}")

    rows = load_manifest(args.manifest)
    validate_manifest(rows)
    if args.inventory_only:
        print(f"DBC qualification manifest covers all {EXPECTED_PROGRAMS} programs")
        return
    if args.opendbc is None or not args.opendbc.is_file():
        raise SystemExit("--opendbc must name the built OpenDBC executable")
    if args.timeout is not None and args.timeout <= 0:
        raise SystemExit("--timeout must be positive")
    if args.max_frames is not None and args.max_frames <= 0:
        raise SystemExit("--max-frames must be positive")

    selected = set(args.program)
    unknown = selected - {row["id"] for row in rows}
    if unknown:
        raise SystemExit(f"unknown DBC programs: {', '.join(sorted(unknown))}")
    rows = [row for row in rows if not selected or row["id"] in selected]
    opendbc = args.opendbc.resolve()
    executable = str(opendbc)
    failures: list[str] = []
    results: list[dict] = []
    p0_test_executable: Path | None = None
    p0_detection = "not-requested"
    p0_results: dict[str, dict] = {}
    if args.run_examples:
        p0_test_executable, p0_detection = resolve_p0_test_executable(args.p0_test_executable, opendbc)
        p0_results = run_p0_probes(p0_test_executable, rows, args.timeout)
        for identifier, probe in p0_results.items():
            if probe["observed"] != "passed":
                failures.append(
                    f"{identifier}: P0 behavior probe {probe['test_case']} observed {probe['observed']}"
                )
    summary: dict[str, Counter] = {
        "compiler": Counter(),
        "inspection": Counter(),
        "startup": Counter(),
        "runtime": Counter(),
        "compatibility_stubs": Counter(),
        "rendering": Counter(),
        "audio": Counter(),
        "interaction": Counter(),
    }
    for row in rows:
        source = safe_repo_source(row["source"])
        timeout = args.timeout or float(row["timeout_seconds"])
        check = run_process(
            [executable, "check", str(source), "--dialect", "dbc"],
            timeout,
        )
        stage, diagnostics = compiler_outcome(check)
        increment(summary, "compiler", stage)
        if stage != "verified" or diagnostics:
            failures.append(f"{row['id']}: expected verified with no diagnostics, observed {stage} {diagnostics}")
        inspection_process = run_process(
            [executable, "inspect", str(source), "--json", "--dialect", "dbc"],
            timeout,
        ) if stage == "verified" and not diagnostics else None
        inspection, inspection_status = (
            inspect_outcome(inspection_process) if inspection_process else (None, "compile-blocked")
        )
        increment(summary, "inspection", inspection_status)
        permanent_stubs: list[str] = []
        temporary_stubs: list[str] = []
        if inspection:
            for use in inspection.get("builtin_overloads", []):
                if use.get("support") != "Compatibility stub":
                    continue
                target = permanent_stubs if use.get("stub_policy") == "Permanent" else temporary_stubs
                target.append(str(use.get("name", "")))
            permanent_stubs = sorted(set(permanent_stubs))
            temporary_stubs = sorted(set(temporary_stubs))
            increment(summary, "compatibility_stubs", "permanent-used" if permanent_stubs else "none-used")
            if temporary_stubs:
                increment(summary, "compatibility_stubs", "temporary-used")
                failures.append(f"{row['id']}: temporary compatibility stubs used: {', '.join(temporary_stubs)}")
        else:
            increment(summary, "compatibility_stubs", "not-inspected")
            failures.append(f"{row['id']}: {inspection_status}")
        item = {
            "id": row["id"],
            "source": row["source"],
            "category": row["category"],
            "interactions": row["interactions"].split(","),
            "oracle": row["oracle"],
            "compiler": {"expected": "verified", "observed": stage, "diagnostics": diagnostics, "process": check},
            "inspection": {
                "observed": inspection_status,
                "process": inspection_process,
            },
            "compatibility_stubs": {
                "permanent": permanent_stubs,
                "temporary": temporary_stubs,
            },
            "startup": {"observed": "not-requested"},
            "runtime": {"expected": row["expected_run"], "observed": "not-requested"},
            "rendering": {
                "oracle": row["oracle"],
                "automated_probe": P0_ORACLES.get(row["id"]),
                "probe": p0_results.get(row["id"]),
                "observed": "not-requested",
                "visual_gate": "manual",
            },
            "audio": {
                "requested": "audio" in row["interactions"].split(","),
                "observed": "not-requested",
                "physical_gate": "manual",
            },
            "interaction": {
                "requested": [value for value in row["interactions"].split(",") if value != "none"],
                "observed": "not-requested",
                "physical_gate": "manual",
            },
        }
        if args.run_examples and inspection:
            frames = args.max_frames or int(row["max_frames"])
            with tempfile.TemporaryDirectory(prefix=f"opendbc-dbc-{row['id'].replace('/', '-')}-") as raw:
                temporary = Path(raw)
                target = isolated_runtime_target(source, row["isolation"], temporary)
                log_path = temporary / "program.out"
                process = run_process(
                    [
                        executable,
                        "run",
                        str(target),
                        "--dialect",
                        "dbc",
                        "--asset-root",
                        str(target.parent),
                        "--log-output",
                        str(log_path),
                        "--max-frames",
                        str(frames),
                    ],
                    timeout,
                    cwd=target.parent,
                )
                log_output = log_path.read_text(encoding="utf-8", errors="replace")[-20000:] if log_path.is_file() else ""
            startup, observed, runtime_diagnostics = execution_outcomes(process, log_output)
            increment(summary, "startup", startup)
            increment(summary, "runtime", observed)
            item["startup"] = {"observed": startup}
            item["runtime"] = {
                "expected": row["expected_run"],
                "observed": observed,
                "diagnostics": runtime_diagnostics,
                "max_frames": frames,
                "timeout_seconds": timeout,
                "isolation": row["isolation"],
                "log_output": log_output,
                "process": process,
            }
            probe = p0_results.get(row["id"])
            if observed != "bounded":
                item["rendering"]["observed"] = "runtime-blocked"
            elif probe is None:
                item["rendering"]["observed"] = "runtime-reached"
            elif probe["observed"] == "passed":
                item["rendering"]["observed"] = (
                    "automated-pixel-state-probe-passed"
                    if row["id"] == "demos/swarm/swarm"
                    else "automated-state-probe-passed"
                )
            elif probe["observed"] == "not-available":
                item["rendering"]["observed"] = "automated-state-probe-unavailable"
            else:
                item["rendering"]["observed"] = f"automated-state-probe-{probe['observed']}"
            increment(summary, "rendering", item["rendering"]["observed"])
            item["audio"]["observed"] = (
                "runtime-reached-manual-physical-gate"
                if item["audio"]["requested"] and observed == "bounded"
                else ("not-used" if not item["audio"]["requested"] else "runtime-blocked")
            )
            increment(summary, "audio", item["audio"]["observed"])
            if (row["id"] in {"demos/room/room", "demos/swarm/swarm"}
                    and probe is not None and probe["observed"] == "passed"
                    and observed == "bounded"):
                item["interaction"]["observed"] = "deterministic-sdl-event-probe-passed"
            else:
                item["interaction"]["observed"] = (
                    "bounded-without-physical-qualification"
                    if item["interaction"]["requested"] and observed == "bounded"
                    else ("not-used" if not item["interaction"]["requested"] else "runtime-blocked")
                )
            increment(summary, "interaction", item["interaction"]["observed"])
            if observed != row["expected_run"]:
                failures.append(f"{row['id']}: expected runtime {row['expected_run']}, observed {observed}")
        elif args.run_examples:
            increment(summary, "startup", "compile-blocked")
            increment(summary, "runtime", "compile-blocked")
            increment(summary, "rendering", "runtime-blocked")
            increment(summary, "audio", "runtime-blocked" if item["audio"]["requested"] else "not-used")
            increment(summary, "interaction",
                      "runtime-blocked" if item["interaction"]["requested"] else "not-used")
            item["startup"] = {"observed": "compile-blocked"}
            item["runtime"] = {"expected": row["expected_run"], "observed": "compile-blocked"}
            item["rendering"]["observed"] = "runtime-blocked"
            item["audio"]["observed"] = "runtime-blocked" if item["audio"]["requested"] else "not-used"
            item["interaction"]["observed"] = (
                "runtime-blocked" if item["interaction"]["requested"] else "not-used"
            )
        else:
            increment(summary, "startup", "not-requested")
            increment(summary, "runtime", "not-requested")
            increment(summary, "rendering", "not-requested")
            increment(summary, "audio", "not-requested")
            increment(summary, "interaction", "not-requested")
        results.append(item)
        runtime = item["runtime"]["observed"]
        print(f"{row['id']}: compiler={stage}, runtime={runtime}", flush=True)

    report = {
        "schema": 1,
        "manifest": str(args.manifest.resolve()),
        "inventory_count": EXPECTED_PROGRAMS,
        "selected_count": len(rows),
        "run_enabled": args.run_examples,
        "p0_probes": {
            "test_executable": str(p0_test_executable) if p0_test_executable is not None else None,
            "detection": p0_detection,
            "results": p0_results,
        },
        "summary": {section: dict(sorted(counts.items())) for section, counts in summary.items()},
        "failures": failures,
        "results": results,
    }
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        raise SystemExit("DBC qualification failed:\n" + "\n".join(failures))
    action = "compiled and ran" if args.run_examples else "compiled"
    print(f"DBC qualification {action} {len(rows)} manifest programs successfully")


if __name__ == "__main__":
    main()
