#!/usr/bin/env python3

import argparse
import hashlib
import json
import subprocess
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "scripts/data/dbpro_projects.json"


def canonical_key(value: str) -> str:
    return " ".join(value.strip().lower().split())


def parse_descriptor(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8-sig")
    values: dict[str, str] = {}
    for line_number, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if not stripped or stripped.startswith(";"):
            continue
        if "=" not in raw:
            raise SystemExit(f"{path}:{line_number}: expected key=value")
        key, value = raw.split("=", 1)
        key = canonical_key(key)
        if key in values:
            raise SystemExit(f"{path}:{line_number}: duplicate key {key}")
        values[key] = value.strip()
    if values.get("version", "").lower() != "dbp1.00":
        raise SystemExit(f"{path}: missing or unsupported version")
    if not values.get("main"):
        raise SystemExit(f"{path}: missing main")
    return values


def resolve_case_insensitive(root: Path, raw: str) -> Path | None:
    current = root
    for part in raw.replace("\\", "/").split("/"):
        if not part or part == ".":
            continue
        if part == "..":
            current = current.parent
            continue
        if not current.is_dir():
            return None
        matches = [entry for entry in current.iterdir() if entry.name.lower() == part.lower()]
        if len(matches) != 1:
            return None
        current = matches[0]
    return current


def snapshot(source_root: Path) -> dict:
    commit = subprocess.run(
        ["git", "-C", str(source_root), "rev-parse", "HEAD"],
        check=True, text=True, capture_output=True,
    ).stdout.strip()
    descriptors = sorted(
        (path for path in source_root.rglob("*") if path.is_file() and path.suffix.lower() == ".dbpro"),
        key=lambda path: path.relative_to(source_root).as_posix().lower(),
    )
    keys: Counter[str] = Counter()
    newlines: Counter[str] = Counter()
    final_sources: Counter[str] = Counter()
    main_resolved = 0
    digest = hashlib.sha256()
    for path in descriptors:
        raw = path.read_bytes()
        relative = path.relative_to(source_root).as_posix()
        values = parse_descriptor(path)
        keys.update(values.keys())
        if b"\r\n" in raw:
            newlines["crlf"] += 1
        else:
            newlines["lf"] += 1
        final = values.get("final source")
        if final is None:
            final_sources["missing"] += 1
        else:
            resolved_final = resolve_case_insensitive(path.parent, final)
            if resolved_final is None:
                final_sources["missing_file"] += 1
            else:
                final_sources["resolved"] += 1
        main = resolve_case_insensitive(path.parent, values["main"])
        if main is not None and main.is_file() and main.suffix.lower() == ".dba":
            main_resolved += 1
        digest.update(relative.encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(raw).digest())
        digest.update(b"\0")
        digest.update(values["main"].encode())
        digest.update(b"\0")
    return {
        "source_commit": commit,
        "descriptor_count": len(descriptors),
        "descriptor_inventory_sha256": digest.hexdigest(),
        "main_resolved": main_resolved,
        "newline_counts": dict(sorted(newlines.items())),
        "final_source_counts": dict(sorted(final_sources.items())),
        "key_counts": dict(sorted(keys.items())),
    }


def validate_evidence(value: dict) -> None:
    required = {
        "source_commit", "descriptor_count", "descriptor_inventory_sha256", "main_resolved",
        "newline_counts", "final_source_counts", "key_counts",
    }
    if set(value) != required:
        raise SystemExit("DBPro project evidence schema is invalid")
    if value["descriptor_count"] != 64 or value["main_resolved"] != 64:
        raise SystemExit("DBPro project evidence must resolve all 64 historical projects")


def main() -> None:
    parser = argparse.ArgumentParser(description="Audit historical DBPro project descriptors")
    parser.add_argument("--dbpro-root", type=Path)
    parser.add_argument("--print", action="store_true", dest="print_snapshot")
    args = parser.parse_args()

    if args.dbpro_root:
        actual = snapshot(args.dbpro_root.resolve())
        validate_evidence(actual)
        if args.print_snapshot:
            print(json.dumps(actual, indent=2, sort_keys=True))
            return
        expected = json.loads(EVIDENCE.read_text(encoding="utf-8"))
        if actual != expected:
            raise SystemExit("DBPro project evidence is stale; inspect and refresh the snapshot")
        print("DBPro project evidence matches the pinned checkout")
        return

    evidence = json.loads(EVIDENCE.read_text(encoding="utf-8"))
    validate_evidence(evidence)
    print("DBPro project evidence schema is valid")


if __name__ == "__main__":
    main()
