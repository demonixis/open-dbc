#!/usr/bin/env python3
"""Report lexical mentions of implemented BASIC builtins in golden tests."""

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOGUE = ROOT / "src" / "language" / "language_catalogue.inc"
TEST_DIR = ROOT / "tests" / "dba"


def implemented_symbols() -> tuple[list[str], list[str]]:
    commands: list[str] = []
    functions: list[str] = []
    for line in CATALOGUE.read_text(encoding="utf-8").splitlines():
        if not line.startswith("DBC_CATALOGUE(") or not line.endswith(")"):
            continue
        fields = next(csv.reader([line[len("DBC_CATALOGUE(") : -1]], skipinitialspace=True))
        if len(fields) != 12:
            raise ValueError(f"Malformed catalogue row: {line}")
        if fields[1] not in {"Command", "Function"}:
            continue
        if int(fields[10], 0) == 0:
            continue
        destination = commands if fields[1] == "Command" else functions
        destination.append(fields[2])
    return sorted(commands), sorted(functions)


def test_text() -> str:
    chunks: list[str] = []
    sources = sorted(TEST_DIR.glob("test_*.dba"))
    for source in sources:
        for line in source.read_text(encoding="utf-8").splitlines():
            stripped = line.lstrip()
            if stripped.upper().startswith("REM ") or stripped.startswith(("'", "`")):
                continue
            chunks.append(stripped.upper())
    return "\n".join(chunks)


def mentioned(name: str, source: str) -> bool:
    pattern = rf"(?<![A-Z0-9_$#]){re.escape(name)}(?![A-Z0-9_$#])"
    return re.search(pattern, source) is not None


def section(title: str, values: list[str]) -> None:
    if not values:
        return
    print(f"## {title} ({len(values)})\n")
    for value in values:
        print(f"- `{value}`")
    print()


def main() -> int:
    commands, functions = implemented_symbols()
    source = test_text()
    command_mentions = [name for name in commands if mentioned(name, source)]
    function_mentions = [name for name in functions if mentioned(name, source)]
    missing_commands = [name for name in commands if name not in command_mentions]
    missing_functions = [name for name in functions if name not in function_mentions]
    total = len(commands) + len(functions)
    mentions = len(command_mentions) + len(function_mentions)

    print("# BASIC Golden-Test Mention Report\n")
    print("This is a lexical inventory, not proof of behavioral coverage.\n")
    print("## Summary")
    print(f"- **Commands**: {len(command_mentions)}/{len(commands)} mentioned")
    print(f"- **Functions**: {len(function_mentions)}/{len(functions)} mentioned")
    print(f"- **Total**: {mentions}/{total} mentioned\n")
    section("Unmentioned Commands", missing_commands)
    section("Unmentioned Functions", missing_functions)
    section("Mentioned Commands", command_mentions)
    section("Mentioned Functions", function_mentions)
    return 0


if __name__ == "__main__":
    sys.exit(main())
