#!/usr/bin/env python3
"""Fail when a tracked file contains a home-directory path.

Such paths leak a user name and a machine layout, typically from a generated
report, a project file or a build log committed by mistake. Binary files are
scanned too, since exported models and media can embed source paths.
"""

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOME_PATH = re.compile(rb"/Users/[A-Za-z0-9._-]+/|/home/[A-Za-z0-9._-]+/|[A-Za-z]:[\\/]+Users[\\/]+[A-Za-z0-9._-]+")
# Placeholder names used in documentation are not personal.
PLACEHOLDERS = {b"Shared", b"user", b"username", b"runner"}


def main() -> None:
    Files = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout.split(b"\0")
    Failures = []
    for Name in filter(None, Files):
        PathValue = ROOT / Name.decode("utf-8")
        if not PathValue.is_file():
            continue
        for Match in HOME_PATH.finditer(PathValue.read_bytes()):
            Owner = re.split(rb"[\\/]+", Match.group(0).rstrip(b"/\\"))[-1]
            if Owner not in PLACEHOLDERS:
                Failures.append(f"{Name.decode('utf-8')}: {Match.group(0).decode('utf-8', 'replace')}")
    if Failures:
        print("tracked files contain personal home-directory paths:", file=sys.stderr)
        print("\n".join(Failures), file=sys.stderr)
        raise SystemExit(1)
    print("No personal paths in tracked files")


if __name__ == "__main__":
    main()
