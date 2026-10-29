#!/usr/bin/env python3

import argparse
import re
import subprocess
from pathlib import Path

from basic_profiles import load_corpus_profiles


ROOT = Path(__file__).resolve().parents[1]
CODE = re.compile(r"\b(DBC\d{4})\b")


def stage(code: str) -> str:
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


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate the full BASIC corpus stage expectations")
    parser.add_argument("--opendbc", type=Path, required=True)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "tests/unit/corpus_expectations.txt")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    examples = ROOT / "examples"
    # Path ordering is case-insensitive on Windows; the committed inventory is not.
    sources = sorted((path for folder in ("dbc", "dbpro", "open-dbc")
                      for path in (examples / folder).rglob("*")
                      if path.is_file() and path.suffix.lower() == ".dba"),
                     key=lambda path: path.as_posix())
    profiles = load_corpus_profiles(args.output)
    rows = []
    for source in sources:
        relative = source.relative_to(examples).as_posix()
        profile = profiles.get(relative)
        if profile is None and relative.startswith("dbc/"):
            profile = "dbc"
        elif profile is None and relative.startswith("dbpro/"):
            profile = "dbpro"
        if profile is None:
            raise SystemExit(f"new OpenDBC example needs an explicit corpus profile: {relative}")
        result = subprocess.run([str(args.opendbc), "check", str(source), "--dialect", profile],
                                text=True, capture_output=True, check=False)
        if result.returncode == 0:
            outcome, codes = "verified", "-"
        else:
            found = sorted(set(CODE.findall(result.stdout + result.stderr)))
            if not found:
                raise SystemExit(f"check failed without a coded diagnostic: {source}")
            outcome = min((stage(code) for code in found),
                          key=("lex", "parse", "bind", "lower", "verify").index)
            codes = ",".join(code for code in found if stage(code) == outcome)
        if profile == "dbc" and outcome != "verified":
            raise SystemExit(
                f"DBC corpus failures cannot be regenerated or blessed: {relative} observed {outcome}/{codes}"
            )
        rows.append("|".join((relative, profile, outcome, codes)))

    Generated = "# path|profile|expected_stage|diagnostic_codes\n" + "\n".join(rows) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text() != Generated:
            raise SystemExit(f"corpus expectations are stale: regenerate {args.output}")
        print(f"Corpus expectations are current: {len(rows)} programs")
    else:
        args.output.write_text(Generated)
        print(f"Wrote {len(rows)} corpus expectations to {args.output}")


if __name__ == "__main__":
    main()
