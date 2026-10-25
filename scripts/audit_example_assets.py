#!/usr/bin/env python3
"""Audit the provenance of every media file shipped with the examples.

- Every media file under examples/ (except the MIT DBPro import, covered by
  docs/NOTICE.md, and the Android export template) needs a row in
  examples/ASSETS.tsv with its SHA-256, licence, source and author.
- No tracked file may match a digest of the removed DarkBASIC Classic content
  (scripts/data/dbc_content_sha256.txt), unless the same file is part of the
  MIT DBPro import.

`--refresh` rewrites the digests of the rows already listed; new files must be
added to the manifest by hand with their provenance.
"""

import argparse
import csv
import hashlib
import io
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "examples/ASSETS.tsv"
DBC_DIGESTS = ROOT / "scripts/data/dbc_content_sha256.txt"
COLUMNS = ("path", "sha256", "licence", "source", "author")
LICENCES = {"MIT", "CC0-1.0"}
MEDIA_SUFFIXES = {".bmp", ".jpg", ".jpeg", ".png", ".tga", ".dds", ".x", ".3ds", ".obj", ".fbx", ".glb",
                  ".gltf", ".wav", ".mp3", ".mid", ".midi", ".ogg", ".avi", ".dat"}
EXEMPT_PREFIXES = ("examples/dbpro/", "examples/android/")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tracked_files() -> list[str]:
    listing = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "--cached", "--others",
                              "--exclude-standard"], check=True, capture_output=True).stdout
    return sorted({name for name in listing.decode().split("\0") if name and (ROOT / name).is_file()})


def read_manifest() -> list[dict[str, str]]:
    with MANIFEST.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if tuple(reader.fieldnames or ()) != COLUMNS:
            raise SystemExit(f"{MANIFEST} must have the columns: {', '.join(COLUMNS)}")
        return list(reader)


def write_manifest(rows: list[dict[str, str]]) -> None:
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, COLUMNS, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    writer.writerows(sorted(rows, key=lambda row: row["path"]))
    MANIFEST.write_text(buffer.getvalue(), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Audit example media provenance")
    parser.add_argument("--refresh", action="store_true", help="rewrite the digests of listed files")
    args = parser.parse_args()

    files = tracked_files()
    rows = read_manifest()
    listed = {row["path"]: row for row in rows}
    failures = []

    for path, row in listed.items():
        file = ROOT / path
        if not file.is_file():
            failures.append(f"listed asset does not exist: {path}")
            continue
        if row["licence"] not in LICENCES:
            failures.append(f"licence must be one of {sorted(LICENCES)}: {path}")
        if not row["source"].strip() or not row["author"].strip():
            failures.append(f"source and author are required: {path}")
        if args.refresh:
            row["sha256"] = sha256(file)
        elif row["sha256"] != sha256(file):
            failures.append(f"digest changed, regenerate or refresh the manifest: {path}")

    for path in files:
        if (path.startswith("examples/") and not path.startswith(EXEMPT_PREFIXES) and
                Path(path).suffix.lower() in MEDIA_SUFFIXES and path not in listed):
            failures.append(f"example media has no provenance row in examples/ASSETS.tsv: {path}")

    forbidden = {line.strip() for line in DBC_DIGESTS.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.startswith("#")}
    allowed = {sha256(ROOT / path) for path in files if path.startswith("examples/dbpro/")}
    for path in files:
        if Path(path).suffix.lower() in MEDIA_SUFFIXES | {".dba", ".htm", ".txt"}:
            digest = sha256(ROOT / path)
            if digest in forbidden and digest not in allowed:
                failures.append(f"file matches removed DarkBASIC Classic content: {path}")

    if failures:
        raise SystemExit("\n".join(failures))
    if args.refresh:
        write_manifest(list(listed.values()))
    print(f"Example asset provenance passed: {len(listed)} listed media files")


if __name__ == "__main__":
    main()
