#!/usr/bin/env python3

import argparse
from pathlib import Path

from basic_profiles import load_test_profiles


ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README.md"
BEGIN = "<!-- BEGIN GENERATED COMPATIBILITY METRICS -->"
END = "<!-- END GENERATED COMPATIBILITY METRICS -->"


def basic_golden_count() -> int:
    root = ROOT / "tests/dba"
    profiles = load_test_profiles(ROOT)
    sources = sorted(root.glob("test_*.dba"))
    source_stems = [path.stem for path in sources]
    expected_stems = sorted(path.stem for path in root.glob("test_*.expected"))
    if len(source_stems) != len(set(source_stems)):
        raise SystemExit("BASIC golden source stems must be unique")
    if sorted(source_stems) != expected_stems:
        raise SystemExit("BASIC golden sources and .expected files are not a bijection")
    if len(profiles) != len(sources):
        raise SystemExit("BASIC golden profile manifest is incomplete")
    return len(sources)


def corpus_rows() -> list[list[str]]:
    lines = (ROOT / "tests/unit/corpus_expectations.txt").read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "# path|profile|expected_stage|diagnostic_codes":
        raise SystemExit("corpus expectation header is invalid")
    rows = [line.split("|") for line in lines[1:] if line]
    if any(len(row) != 4 or row[1] not in {"dbc", "dbpro"} or
           row[2] not in {"lex", "parse", "bind", "lower", "verify", "verified"} for row in rows):
        raise SystemExit("corpus expectation row is invalid")
    paths = [row[0] for row in rows]
    if len(paths) != len(set(paths)):
        raise SystemExit("corpus expectation paths must be unique")
    return rows


def behavior_rows() -> list[list[str]]:
    lines = (ROOT / "tests/unit/builtin_behavior_manifest.tsv").read_text(encoding="utf-8").splitlines()
    header = "profile\tsymbol_id\toverload_id\tkind\tname\toracle\ttest\toracle_sha256"
    if not lines or lines[0] != header:
        raise SystemExit("behavior manifest header is invalid")
    rows = [line.split("\t") for line in lines[1:] if line]
    if any(len(row) != 8 or row[0] not in {"dbc", "dbpro"} or
           not row[1].isdigit() or not row[2].isdigit() for row in rows):
        raise SystemExit("behavior manifest row is invalid")
    keys = [(row[0], row[1], row[2]) for row in rows]
    if len(keys) != len(set(keys)):
        raise SystemExit("behavior manifest overload keys must be unique")
    return rows


def generated_block() -> str:
    basic_tests = basic_golden_count()
    corpus = corpus_rows()
    verified = sum(row[2] == "verified" for row in corpus)
    behavior = behavior_rows()
    return "\n".join((
        BEGIN,
        f"- BASIC execution goldens: **{basic_tests}**.",
        f"- Full corpus expectations: **{len(corpus)}** sources, including **{verified}** that reach verified bytecode.",
        f"- Observable implemented-overload contracts: **{len(behavior)}**.",
        END,
    ))


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate compatibility metrics in README.md")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    current = README.read_text(encoding="utf-8")
    start = current.find(BEGIN)
    finish = current.find(END)
    if start < 0 or finish < start:
        raise SystemExit("README compatibility metric markers are missing or inverted")
    finish += len(END)
    expected = current[:start] + generated_block() + current[finish:]
    if args.check:
        if current != expected:
            raise SystemExit("README compatibility metrics are stale")
        print("README compatibility metrics are current")
        return
    README.write_text(expected, encoding="utf-8")
    print("Updated README compatibility metrics")


if __name__ == "__main__":
    main()
