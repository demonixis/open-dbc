#!/usr/bin/env python3

import argparse
import re
from pathlib import Path

from audit_language_catalogue import ENTRY


ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "src/language/language_catalogue.inc"
CATALOGUE_IMPLEMENTATION = ROOT / "src/language/language_catalogue.cpp"
DEFAULT_OUTPUT = ROOT / "tests/unit/compatibility_stub_manifest.tsv"

INERT_RESOURCES = {"Command_LoadCdmusic", "Command_LoadDll"}
EMPTY_STRINGS = {
    "Function_GetFtpDirString",
    "Function_GetFtpErrorString",
    "Function_GetFtpFileNameString",
    "Function_WindirString",
}


def permanent_symbols() -> set[str]:
    source = CATALOGUE_IMPLEMENTATION.read_text(encoding="utf-8")
    body = source.split("constexpr bool IsPermanentCompatibilityStub", 1)[1]
    body = body.split("constexpr std::uint8_t CompatibilityStubProfiles", 1)[0]
    return set(re.findall(r"case SymbolId::(\w+):", body))


def neutral_behavior(symbol_id: str, kind: str, _name: str) -> str:
    if symbol_id in INERT_RESOURCES:
        return "accept-inert-resource-id"
    if kind == "Command":
        return "no-op"
    if symbol_id == "Function_GetFtpFailure":
        return "numeric:1"
    if symbol_id in {"Function_GetFtpProgress", "Function_GetFtpFileType"}:
        return "numeric:-1"
    if symbol_id in EMPTY_STRINGS:
        return "string:empty"
    return "numeric:0"


def generate() -> str:
    permanent = permanent_symbols()
    if len(permanent) != 49:
        raise SystemExit(f"permanent Classic compatibility allowlist must contain exactly 49 symbols, got {len(permanent)}")
    rows = ["profile\tsymbol_id\tkind\tname\tpolicy\tneutral_behavior"]
    seen = set()
    for match in ENTRY.finditer(CATALOGUE.read_text(encoding="utf-8")):
        symbol_id, kind, name = match[1], match[2], match[3]
        available = int(match[4])
        implemented = int(match[11])
        if not available & 1 or implemented & 1:
            continue
        if symbol_id in seen:
            raise SystemExit(f"duplicate DBC compatibility stub: {symbol_id}")
        seen.add(symbol_id)
        policy = "Permanent" if symbol_id in permanent else "Temporary"
        rows.append("\t".join(("dbc", symbol_id, kind, name, policy,
                               neutral_behavior(symbol_id, kind, name))))
    missing = permanent - seen
    if missing:
        raise SystemExit("permanent compatibility allowlist contains non-stub symbols: " + ", ".join(sorted(missing)))
    return "\n".join(rows) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate the DBC neutral compatibility-stub inventory")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--require-no-temporary", action="store_true")
    args = parser.parse_args()
    generated = generate()
    if args.require_no_temporary and "\tTemporary\t" in generated:
        temporary = [line.split("\t")[3] for line in generated.splitlines()[1:]
                     if "\tTemporary\t" in line]
        raise SystemExit("temporary DBC compatibility stubs remain: " + ", ".join(temporary))
    count = generated.count("\n") - 1
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != generated:
            raise SystemExit(f"compatibility stub manifest is stale: regenerate {args.output}")
        print(f"Compatibility stub manifest is current: {count} DBC symbols")
        return
    args.output.write_text(generated, encoding="utf-8")
    print(f"Wrote {count} DBC compatibility stubs to {args.output}")


if __name__ == "__main__":
    main()
