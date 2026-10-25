#!/usr/bin/env python3
"""Run the OpenDBC examples headless and check that their last frame shows a picture.

Each row of tests/qualification/example_frames.tsv names a program, its profile,
how many frames to run and the least a healthy frame must show: the share of
lit pixels and the number of distinct colours. A black or nearly empty frame,
a failed run or a missing capture fails the check. Every program runs in a
temporary copy of its folder, so files it writes never reach the source tree.

    scripts/check_example_frames.py --opendbc build/.../opendbc [--report]

--report prints the measured values without failing, to set new thresholds.
"""

import argparse
import csv
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/qualification/example_frames.tsv"
COLUMNS = ("source", "profile", "frames", "min_lit", "min_colours")


def frame_statistics(path: Path) -> tuple[float, int]:
    """Share of pixels brighter than near-black and the number of 5-bit colours."""
    data = path.read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    bits, = struct.unpack_from("<H", data, 28)
    step = bits // 8
    row = (width * step + 3) // 4 * 4
    lit = total = 0
    colours = set()
    for y in range(0, abs(height), 3):
        start = offset + y * row
        for x in range(0, width, 3):
            blue, green, red = data[start + x * step:start + x * step + 3]
            total += 1
            if red + green + blue > 24:
                lit += 1
            colours.add((red >> 3, green >> 3, blue >> 3))
    return lit / max(total, 1), len(colours)


def check(opendbc: Path, row: dict[str, str]) -> tuple[bool, str]:
    source = ROOT / row["source"]
    with tempfile.TemporaryDirectory(prefix="opendbc-frame-") as temporary:
        copy = Path(temporary) / source.parent.name
        shutil.copytree(source.parent, copy, ignore=shutil.ignore_patterns("*.log"))
        capture = Path(temporary) / "frame.bmp"
        command = [str(opendbc), "run", str(copy / source.name), "--dialect", row["profile"], "--headless",
                   "--max-frames", row["frames"], "--capture-output", str(capture)]
        try:
            result = subprocess.run(command, cwd=copy, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            return False, "timed out"
        if result.returncode != 0 or not capture.exists():
            lines = [line for line in result.stderr.splitlines() if line.strip()]
            return False, f"exit {result.returncode}: {lines[-1] if lines else 'no capture'}"
        lit, colours = frame_statistics(capture)
    summary = f"lit {lit:.2f}, {colours} colours"
    if lit < float(row["min_lit"]) or colours < int(row["min_colours"]):
        return False, f"{summary} (needs lit {row['min_lit']}, {row['min_colours']} colours)"
    return True, summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--opendbc", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--report", action="store_true", help="print measurements without failing")
    parser.add_argument("--jobs", type=int, default=4)
    arguments = parser.parse_args()

    with arguments.manifest.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader((line for line in stream if not line.startswith("#")), delimiter="\t"))
    if not rows or tuple(rows[0].keys()) != COLUMNS:
        print(f"{arguments.manifest}: expected columns {', '.join(COLUMNS)}", file=sys.stderr)
        return 1
    missing = [row["source"] for row in rows if not (ROOT / row["source"]).is_file()]
    if missing:
        print("Missing example sources: " + ", ".join(missing), file=sys.stderr)
        return 1

    with ThreadPoolExecutor(arguments.jobs) as pool:
        opendbc = arguments.opendbc.resolve()
        results = list(pool.map(lambda row: check(opendbc, row), rows))
    failures = 0
    for row, (passed, detail) in zip(rows, results):
        if not passed:
            failures += 1
        print(f"{'ok  ' if passed else 'FAIL'} {row['source']}: {detail}")
    if arguments.report:
        return 0
    print(f"{len(rows) - failures}/{len(rows)} example frames passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
