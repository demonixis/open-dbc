#!/usr/bin/env python3

import argparse
import csv
import json
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tests/qualification/dbpro_demo_expectations.tsv"
TUTORIALS = ROOT / "examples/dbpro/tutorials"
CODE = re.compile(r"\b(DBC\d{4})\b")
STAGES = ("lex", "parse", "bind", "lower", "verify", "verified")
DISPOSITIONS = {"Target", "Partial", "Deferred", "Never", "Study"}
RUN_OUTCOMES = {"compile-blocked", "startup-failure", "runtime-failure", "bounded"}
FIXED_DISPOSITIONS = {
    "3dmaths1": "Never",
    "animation1": "Deferred",
    "basic2d1": "Never",
    "basic3d2": "Never",
    "basic3d5": "Never",
    "basic3d8": "Never",
    "display1": "Never",
    "ftp1": "Never",
    "multiplayer1": "Deferred",
}
REQUIRED_COLUMNS = (
    "demo", "descriptor", "entry", "disposition", "expected_stage", "expected_diagnostics",
    "expected_descriptor_run", "expected_source_run", "max_frames", "interactions", "oracle", "blockers",
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


def safe_repo_path(raw: str, field: str) -> Path:
    relative = Path(raw)
    if relative.is_absolute() or ".." in relative.parts:
        raise SystemExit(f"{field} must be a safe repository-relative path: {raw}")
    return ROOT / relative


def descriptor_main(path: Path) -> str:
    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith(";") or "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        if " ".join(key.strip().lower().split()) == "main":
            return value.strip().replace("\\", "/")
    raise SystemExit(f"descriptor has no main source: {path}")


def load_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if tuple(reader.fieldnames or ()) != REQUIRED_COLUMNS:
            raise SystemExit(f"qualification manifest schema is invalid: {path}")
        rows = list(reader)
    if len(rows) != 32:
        raise SystemExit(f"qualification manifest must contain exactly 32 tutorial projects, found {len(rows)}")
    return rows


def validate_manifest(rows: list[dict[str, str]]) -> None:
    discovered = {path.resolve() for path in TUTORIALS.rglob("*.dba.dbpro") if path.is_file()}
    described: set[Path] = set()
    demos: set[str] = set()
    entries: set[Path] = set()
    for row in rows:
        demo = row["demo"]
        if not demo or demo in demos:
            raise SystemExit(f"duplicate or empty demo identifier: {demo}")
        demos.add(demo)
        descriptor = safe_repo_path(row["descriptor"], "descriptor")
        entry = safe_repo_path(row["entry"], "entry")
        if not descriptor.is_file() or not entry.is_file():
            raise SystemExit(f"qualification path does not exist: {descriptor} / {entry}")
        if descriptor.resolve() in described or entry.resolve() in entries:
            raise SystemExit(f"duplicate qualification path for {demo}")
        described.add(descriptor.resolve())
        entries.add(entry.resolve())
        declared_main = (descriptor.parent / descriptor_main(descriptor)).resolve()
        if declared_main != entry.resolve():
            raise SystemExit(f"{descriptor}: main source does not match manifest entry {entry}")
        if row["disposition"] not in DISPOSITIONS:
            raise SystemExit(f"invalid disposition for {demo}: {row['disposition']}")
        if demo in FIXED_DISPOSITIONS and row["disposition"] != FIXED_DISPOSITIONS[demo]:
            raise SystemExit(
                f"fixed disposition for {demo} must remain {FIXED_DISPOSITIONS[demo]}, found {row['disposition']}"
            )
        if row["expected_stage"] not in STAGES:
            raise SystemExit(f"invalid compiler stage for {demo}: {row['expected_stage']}")
        if row["expected_stage"] == "verified" and row["expected_diagnostics"] != "-":
            raise SystemExit(f"verified demo must not expect diagnostics: {demo}")
        if row["expected_stage"] != "verified" and row["expected_diagnostics"] == "-":
            raise SystemExit(f"blocked demo must declare diagnostics: {demo}")
        for field in ("expected_descriptor_run", "expected_source_run"):
            if row[field] not in RUN_OUTCOMES:
                raise SystemExit(f"invalid {field} for {demo}: {row[field]}")
            if row["expected_stage"] != "verified" and row[field] != "compile-blocked":
                raise SystemExit(f"blocked demo must use compile-blocked for {field}: {demo}")
        try:
            if int(row["max_frames"]) <= 0:
                raise ValueError
        except ValueError as error:
            raise SystemExit(f"invalid max_frames for {demo}: {row['max_frames']}") from error
        if not row["oracle"] or not row["blockers"]:
            raise SystemExit(f"qualification oracle and blockers are required: {demo}")
    if discovered != described:
        missing = sorted(path.relative_to(ROOT).as_posix() for path in discovered - described)
        stale = sorted(path.relative_to(ROOT).as_posix() for path in described - discovered)
        raise SystemExit(f"qualification inventory mismatch; missing={missing}, stale={stale}")


def run_process(command: list[str], timeout: float) -> dict:
    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                timeout=timeout, check=False)
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
    if result["returncode"] == 0:
        return "verified", []
    codes = sorted(set(CODE.findall(result["stdout"] + result["stderr"])))
    if not codes:
        return "unknown", []
    stage = min((diagnostic_stage(code) for code in codes), key=STAGES.index)
    return stage, [code for code in codes if diagnostic_stage(code) == stage]


def runtime_outcome(result: dict) -> str:
    if result["timed_out"]:
        return "runtime-failure"
    if result["returncode"] == 0:
        return "bounded"
    output = result["stdout"] + result["stderr"]
    if "SET DISPLAY MODE supports only 32-bit colour" in output:
        return "startup-failure"
    return "runtime-failure"


def isolated_runtime_target(row: dict[str, str], route: str, root: Path) -> Path:
    descriptor = safe_repo_path(row["descriptor"], "descriptor")
    entry = safe_repo_path(row["entry"], "entry")
    if row["demo"] not in {"basic3d3", "file1"}:
        return descriptor if route == "descriptor" else entry
    destination = root / descriptor.parent.name
    shutil.copytree(descriptor.parent, destination)
    return destination / (descriptor.name if route == "descriptor" else entry.name)


def main() -> None:
    parser = argparse.ArgumentParser(description="Inventory and qualify the 32 canonical DBPro tutorial projects")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--opendbc", type=Path)
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--run", action="store_true", dest="run_examples")
    parser.add_argument("--route", choices=("descriptor", "source", "both"), default="descriptor")
    parser.add_argument("--demo", action="append", default=[])
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--max-frames", type=int, help="Override each manifest frame budget")
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()

    rows = load_manifest(args.manifest)
    validate_manifest(rows)
    selected = set(args.demo)
    unknown = selected - {row["demo"] for row in rows}
    if unknown:
        raise SystemExit(f"unknown DBPro demos: {', '.join(sorted(unknown))}")
    rows = [row for row in rows if not selected or row["demo"] in selected]
    if args.inventory_only:
        print("DBPro demo qualification manifest covers all 32 tutorial projects")
        return
    if args.opendbc is None or not args.opendbc.is_file():
        raise SystemExit("--opendbc must name the built OpenDBC executable")
    if args.max_frames is not None and args.max_frames <= 0:
        raise SystemExit("--max-frames must be positive")

    executable = str(args.opendbc.resolve())
    failures: list[str] = []
    results: list[dict] = []
    for row in rows:
        descriptor = safe_repo_path(row["descriptor"], "descriptor")
        entry = safe_repo_path(row["entry"], "entry")
        check = run_process([executable, "check", str(descriptor), "--dialect", "dbpro"], args.timeout)
        actual_stage, actual_codes = compiler_outcome(check)
        expected_codes = [] if row["expected_diagnostics"] == "-" else row["expected_diagnostics"].split(",")
        if actual_stage != row["expected_stage"] or actual_codes != expected_codes:
            failures.append(
                f"{row['demo']}: expected {row['expected_stage']} {expected_codes}, "
                f"observed {actual_stage} {actual_codes}"
            )
        item = {
            "demo": row["demo"],
            "disposition": row["disposition"],
            "compiler": {"stage": actual_stage, "diagnostics": actual_codes, "process": check},
            "runtime": {},
        }
        if args.run_examples and actual_stage == "verified":
            routes = ("descriptor", "source") if args.route == "both" else (args.route,)
            frames = args.max_frames or int(row["max_frames"])
            for route in routes:
                with tempfile.TemporaryDirectory(prefix=f"opendbc-{row['demo']}-") as temporary:
                    target = isolated_runtime_target(row, route, Path(temporary))
                    process = run_process(
                        [executable, "run", str(target), "--dialect", "dbpro", "--max-frames", str(frames)],
                        args.timeout,
                    )
                observed = runtime_outcome(process)
                expected = row[f"expected_{route}_run"]
                item["runtime"][route] = {"expected": expected, "observed": observed, "process": process}
                if observed != expected:
                    failures.append(f"{row['demo']}/{route}: expected {expected}, observed {observed}")
        elif args.run_examples:
            item["runtime"]["skipped"] = "compile-blocked"
        results.append(item)
        print(f"{row['demo']}: {actual_stage} ({row['disposition']})")

    report = {
        "schema": 1,
        "manifest": str(args.manifest.resolve()),
        "selected_count": len(rows),
        "failures": failures,
        "results": results,
    }
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        raise SystemExit("DBPro qualification failed:\n" + "\n".join(failures))
    print(f"DBPro qualification matched {len(rows)} manifest rows")


if __name__ == "__main__":
    main()
