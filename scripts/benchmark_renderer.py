#!/usr/bin/env python3
"""Measure the renderer on the stress scenes and the demos, and compare two builds.

Each row of tests/benchmarks/benchmarks.tsv names a program, its profile, how many
frames to run and its isolation: read-only programs run in place, copy-tree ones in
a temporary copy of their folder, so files they write never reach the source tree.

    scripts/benchmark_renderer.py run --opendbc build/release/.../opendbc --output DIR
    scripts/benchmark_renderer.py compare BEFORE_DIR AFTER_DIR

run writes DIR/results.json, the median of each measurement over --runs runs, and
DIR/<name>.bmp, the last frame of the last run. It fails when a program does not
complete. compare prints both builds' frame and step times and how many pixels
differ between their captures. The stress scenes draw the same frames on every run;
demos that depend on time may differ between two runs of one build. Measure
Release builds.
"""

import argparse
import csv
import json
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/benchmarks/benchmarks.tsv"
COLUMNS = ("name", "source", "profile", "frames", "isolation")
ISOLATION_MODES = {"read-only", "copy-tree"}
STEPS = ("build", "order", "convert", "upload", "record")


def read_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    for row in rows:
        if tuple(row) != COLUMNS or row["isolation"] not in ISOLATION_MODES:
            raise SystemExit(f"{path}: malformed row {row}")
    return rows


def run_once(opendbc: Path, row: dict[str, str], frames: int, folder: Path, capture: Path | None) -> dict:
    source = ROOT / row["source"]
    if row["isolation"] == "copy-tree":
        copy = folder / source.parent.name
        shutil.copytree(source.parent, copy, ignore=shutil.ignore_patterns("*.log"))
        source = copy / source.name
    evidence = folder / "benchmark.json"
    command = [str(opendbc), "run", str(source), "--dialect", row["profile"], "--headless",
               "--max-frames", str(frames), "--benchmark", "--benchmark-output", str(evidence),
               "--log-directory", str(folder)]
    if capture:
        command += ["--capture-output", str(capture)]
    result = subprocess.run(command, cwd=source.parent, capture_output=True, text=True, timeout=600)
    if result.returncode != 0 or not evidence.exists():
        lines = [line for line in result.stderr.splitlines() if line.strip()]
        return {"status": f"exit {result.returncode}: {lines[-1] if lines else 'no benchmark output'}"}
    return json.loads(evidence.read_text(encoding="utf-8"))


def measure(opendbc: Path, row: dict[str, str], runs: int, frames: int, output: Path) -> dict:
    samples = []
    for run in range(runs):
        with tempfile.TemporaryDirectory(prefix="opendbc-benchmark-") as temporary:
            capture = output / f"{row['name']}.bmp" if run == runs - 1 else None
            sample = run_once(opendbc, row, frames, Path(temporary), capture)
        if sample.get("status") != "completed":
            return {"status": sample.get("status", "failed")}
        samples.append(sample)
    return {
        "status": "completed",
        "frame_ms": statistics.median(sample["frame_ms"]["average"] for sample in samples),
        "p95_ms": statistics.median(sample["frame_ms"]["p95"] for sample in samples),
        "step_ms": {step: statistics.median(sample["renderer_step_ms"][step] for sample in samples)
                    for step in STEPS},
        "renderer": samples[-1]["renderer"],
    }


def read_bitmap(path: Path) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    bits, = struct.unpack_from("<H", data, 28)
    step = bits // 8
    row = (width * step + 3) // 4 * 4
    pixels = [b"".join(data[offset + y * row + x * step:offset + y * row + x * step + 3] for x in range(width))
              for y in range(abs(height))]
    return width, abs(height), pixels


def differing_pixels(before: Path, after: Path) -> str:
    if not before.exists() or not after.exists():
        return "no capture"
    width, height, first = read_bitmap(before)
    other_width, other_height, second = read_bitmap(after)
    if (width, height) != (other_width, other_height):
        return "size differs"
    count = 0
    for line, other in zip(first, second):
        if line != other:
            count += sum(1 for x in range(0, len(line), 3) if line[x:x + 3] != other[x:x + 3])
    return str(count)


def run(arguments: argparse.Namespace) -> int:
    rows = read_manifest(arguments.manifest)
    if arguments.only:
        rows = [row for row in rows if row["name"] in arguments.only]
    arguments.output.mkdir(parents=True, exist_ok=True)
    results = {}
    failed = False
    for row in rows:
        frames = arguments.frames or int(row["frames"])
        result = measure(arguments.opendbc.resolve(), row, arguments.runs, frames, arguments.output.resolve())
        results[row["name"]] = result
        if result["status"] != "completed":
            failed = True
            print(f"{row['name']:16} {result['status']}")
            continue
        steps = " ".join(f"{step} {result['step_ms'][step]:.2f}" for step in STEPS)
        print(f"{row['name']:16} {result['frame_ms']:8.2f} ms  {steps}  "
              f"triangles {result['renderer']['triangles']}  draw calls {result['renderer']['draw_calls']}")
    (arguments.output / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 1 if failed else 0


def compare(arguments: argparse.Namespace) -> int:
    before = json.loads((arguments.before / "results.json").read_text(encoding="utf-8"))
    after = json.loads((arguments.after / "results.json").read_text(encoding="utf-8"))
    print(f"{'program':16} {'before':>8} {'after':>8} {'change':>7}  {'draw calls':>12}  pixels differing")
    for name, result in after.items():
        previous = before.get(name)
        if not previous or previous["status"] != "completed" or result["status"] != "completed":
            print(f"{name:16} not comparable")
            continue
        change = (result["frame_ms"] - previous["frame_ms"]) / previous["frame_ms"] * 100.0
        calls = f"{previous['renderer']['draw_calls']} -> {result['renderer']['draw_calls']}"
        pixels = differing_pixels(arguments.before / f"{name}.bmp", arguments.after / f"{name}.bmp")
        print(f"{name:16} {previous['frame_ms']:8.2f} {result['frame_ms']:8.2f} {change:+6.1f}%  {calls:>12}  {pixels}")
        for step in STEPS:
            print(f"{'':16}   {step:8} {previous['step_ms'][step]:7.2f} -> {result['step_ms'][step]:7.2f}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run_parser = commands.add_parser("run", help="measure every program of the manifest")
    run_parser.add_argument("--opendbc", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    run_parser.add_argument("--manifest", type=Path, default=MANIFEST)
    run_parser.add_argument("--runs", type=int, default=3)
    run_parser.add_argument("--frames", type=int, help="override every program's frame count")
    run_parser.add_argument("--only", nargs="+", help="names of the programs to run")
    compare_parser = commands.add_parser("compare", help="compare two run outputs")
    compare_parser.add_argument("before", type=Path)
    compare_parser.add_argument("after", type=Path)
    arguments = parser.parse_args()
    return run(arguments) if arguments.command == "run" else compare(arguments)


if __name__ == "__main__":
    sys.exit(main())
