#!/usr/bin/env python3
"""Fuzz each input format for a time under the sanitizers, filing each distinct crash.

The fuzz_inputs binary stops at its first crash; this restarts it from its corpus until the time is up.
Build it with: cmake --preset sanitize && cmake --build --preset sanitize --target fuzz_inputs
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ("frontend", "image", "dds", "wav", "dbo", "memblock-classic", "memblock-pro")
SEEDS = ("examples", "tests/fixtures")
# Give up on a target that keeps crashing at once: a seed crashes, or the same bug hides everything else.
MAX_RESTARTS = 40


def crash_signature(report):
    """The kind of report and the first frames of OpenDBC's own code, which survive unrelated mutations."""
    kind = re.search(r"(?:ERROR: \w+Sanitizer: |SUMMARY: \w+Sanitizer: |runtime error: )([\w -]{3,60})", report)
    frames = re.findall(r"#\d+ 0x[0-9a-f]+ in (\S.*?) (?:\(|/)\S*?/src/\S*", report)
    if not frames:
        frames = re.findall(r"#\d+ 0x[0-9a-f]+ in (\S+)", report)
    return (kind.group(1) if kind else "unknown") + " | " + " < ".join(frames[:3])


def fuzz_target(binary, target, seconds, work):
    directory = work / target
    crashes = directory / "crashes"
    crashes.mkdir(parents=True, exist_ok=True)
    environment = dict(
        os.environ,
        ASAN_OPTIONS="abort_on_error=0:detect_leaks=0:handle_abort=1:allocator_may_return_null=1:"
        "max_allocation_size_mb=4096",
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
    )
    deadline = time.monotonic() + seconds
    seen = {}
    restarts = 0
    while (remaining := int(deadline - time.monotonic())) > 0 and restarts < MAX_RESTARTS:
        run = subprocess.run(
            [binary, target, str(remaining), directory, *(ROOT / seed for seed in SEEDS)],
            env=environment,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            errors="replace",
            cwd=directory,
        )
        lines = run.stdout.strip().splitlines()
        if lines:
            print(lines[-1], flush=True)
        if run.returncode == 0:
            break
        current = next(iter(sorted(directory.glob("current.*"))), None)
        # A stack protector in an uninstrumented dependency dies without a sanitizer report.
        signature = crash_signature(run.stderr) if run.stderr else f"exit {run.returncode}, no report"
        name = hashlib.sha256(signature.encode()).hexdigest()[:12]
        if name not in seen:
            seen[name] = signature
            print(f"{target}: new crash {name}: {signature}", flush=True)
            (crashes / f"{name}.log").write_text(run.stderr)
            if current:
                shutil.copy(current, crashes / f"{name}{current.suffix}")
        restarts += 1
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/sanitize/tests/fuzz/fuzz_inputs")
    parser.add_argument("--seconds", type=int, default=300, help="time for each target")
    parser.add_argument("--work", type=Path, required=True, help="directory for corpora and crashes")
    parser.add_argument("targets", nargs="*", default=TARGETS)
    arguments = parser.parse_args()

    found = {target: fuzz_target(arguments.binary, target, arguments.seconds, arguments.work.resolve())
             for target in arguments.targets}
    for target, crashes in found.items():
        print(f"{target}: {len(crashes)} distinct crash(es)")
    return 1 if any(found.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
