#!/usr/bin/env python3

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "src/language/language_catalogue.inc"
CATALOGUE_IMPLEMENTATION = ROOT / "src/language/language_catalogue.cpp"
OUTPUTS = {
    ("Dbc", False): ROOT / "docs/reference/darkbasic_reference.md",
    ("DbPro", False): ROOT / "docs/reference/dbpro_reference.md",
    ("DbPro", True): ROOT / "docs/reference/dbpro_extensions_reference.md",
}
ENTRY = re.compile(
    r'DBC_CATALOGUE\(([^,]+),\s*([^,]+),\s*"([^"]+)",\s*'
    r'(\d+),\s*(-?\d+),\s*(-?\d+),\s*UINT64_C\((0x[0-9A-Fa-f]+|0)\),\s*'
    r'(-?\d+),\s*(-?\d+),\s*UINT64_C\((0x[0-9A-Fa-f]+|0)\),\s*'
    r'(\d+),\s*(\d+)\)'
)
SIGNATURE = re.compile(
    r'DBC_SIGNATURE\(([^,]+),\s*(Dbc|DbPro),\s*(-?\d+),\s*(-?\d+),\s*'
    r'UINT64_C\((0x[0-9A-Fa-f]+|0)\),\s*(Void|Integer|Real|String),\s*"([^"]+)"\)'
)
TYPE_NAMES = {"A": "any", "N": "number", "I": "integer", "R": "real", "S": "string"}
RETURN_NAMES = {"Void": "void", "Integer": "integer", "Real": "real", "String": "string"}
EDITORIAL = ROOT / "scripts/data/language_catalogue_editorial.json"
OPENDBC_REFERENCE = ROOT / "docs/reference/open-dbc_reference.md"
ROW_FIELDS = (
    "symbol", "kind", "profiles", "surface", "arity", "typed_overloads", "runtime_status",
    "project_status", "disposition", "relationship", "provenance", "description", "notes", "source",
)
SURFACES = {
    16: "ODE",
    32: "3D Cloth & Particles",
    64: "DarkPHYSICS",
    128: "Dark Dynamix",
}
PUBLIC_NOTES = {
    ("APPEND OBJECT", "DBC"): "APPEND OBJECT Filename$, ObjectId, StartFrame loads animation data relative to the execution asset root; both slash styles are accepted, StartFrame must be non-negative and animation channels must match the destination object.",
    ("LOAD OBJECT", "DBC"): "Loads relative to the execution asset root; both forward slashes and historical Windows backslashes are accepted.",
    ("LOAD MUSIC", "DBC + DBPro"): "OpenDBC does not ship a general MIDI bank. A failed .mid/.midi load tries a same-stem .mp3 and then .wav; project MIDI music ships with deterministic MP3 fallbacks.",
    ("LOAD SOUND", "DBC"): "Accepts Filename$, SoundId and an optional historical priority hint. The priority value is retained for source compatibility and has no portable backend effect.",
    ("SET DISPLAY MODE", "DBC"): "Accepts Width, Height and optional Depth; omitting Depth selects the logical 32-bit mode.",
    ("SET OBJECT COLLISION TO POLYGONS", "DBC"): "Uses current world-space mesh geometry for narrow-phase collision. Sliding feedback remains available to an explicit non-rotating collision box colliding with that polygonal object.",
    ("GET OBJECT COLLISION X", "DBC"): "Accepts zero or one argument. The result is attempted position minus corrected position for the selected or most recently queried object.",
    ("GET OBJECT COLLISION Y", "DBC"): "Accepts zero or one argument. The result is attempted position minus corrected position for the selected or most recently queried object.",
    ("GET OBJECT COLLISION Z", "DBC"): "Accepts zero or one argument. The result is attempted position minus corrected position for the selected or most recently queried object.",
    ("FORCE ANGLE", "DBC"): "Uses a constant polar haptic effect when supported. Duration is 50 + floor(ms / 1000) * 1000; zero means infinite. Unsupported devices remain a portable no-op.",
    ("FORCE WATER EFFECT", "DBC"): "Uses a two-axis damper haptic effect for ms + 50 and may coexist with FORCE ANGLE. Unsupported devices remain a portable no-op.",
    ("ALPHABLENDING AVAILABLE", "DBC"): "Reports the renderer's actual transparent-pipeline capability instead of a compatibility constant.",
    ("SCROLL BACKDROP", "DBC"): "Sets the backdrop offset as absolute percentages of the current texture dimensions.",
    ("SCROLL OBJECT TEXTURE", "DBC"): "Applies a cumulative stage-zero UV offset to the selected object.",
    ("TEXTURE BACKDROP", "DBC"): "Requires an existing image and tiles it across the current camera backdrop.",
    ("MAKE OBJECT PLAIN", "DBC"): "The three-argument DBC form defaults to orientation 0, producing the historical face toward -Z with the current winding. DBPro retains its explicit fourth argument forms.",
    ("PREPARE MATRIX TEXTURE", "DBC + DBPro"): "DBC snapshots the prepared source image, so deleting or replacing that image does not alter the matrix. DBPro retains the live image identifier.",
    ("HIDE LIMB", "DBC + DBPro"): "DBC changes only the selected limb. DBPro propagates the hidden state through that limb's hierarchy children.",
    ("SHOW LIMB", "DBC + DBPro"): "DBC changes only the selected limb. DBPro propagates the visible state through that limb's hierarchy children.",
}


def permanent_stub_symbols() -> set[str]:
    source = CATALOGUE_IMPLEMENTATION.read_text(encoding="utf-8")
    body = source.split("constexpr bool IsPermanentCompatibilityStub", 1)[1]
    body = body.split("constexpr std::uint8_t CompatibilityStubProfiles", 1)[0]
    return set(re.findall(r"case SymbolId::(\w+):", body))


PERMANENT_STUB_SYMBOLS = permanent_stub_symbols()


def runtime_status(symbol_id: str, available: int, implemented: int, bit: int) -> str:
    if implemented & bit:
        return "Implemented"
    if bit == 1 and available & bit:
        policy = "permanent" if symbol_id in PERMANENT_STUB_SYMBOLS else "temporary"
        return f"Compatibility stub ({policy})"
    return "Unsupported (DBC2043)"


def mask_counts(minimum: int, maximum: int, mask: int) -> str:
    if mask:
        counts = [count for count in range(64) if mask & (1 << count)]
        if counts == list(range(minimum, maximum + 1)):
            return str(minimum) if minimum == maximum else f"{minimum}-{maximum}"
        return ", ".join(map(str, counts))
    return str(minimum) if minimum == maximum else f"{minimum}-{maximum}"


def parameter_text(spec: str) -> str:
    if spec == "-":
        return "none"
    repeated = spec.endswith("*")
    if repeated:
        spec = spec[:-1]
    values = []
    for raw in spec.split(","):
        mode = ""
        if raw.endswith("[]"):
            raw = raw[:-2]
            mode = " array"
        elif raw.endswith("!"):
            raw = raw[:-1]
            mode = " output"
        values.append(TYPE_NAMES[raw] + mode)
    result = ", ".join(values)
    return result + ("..." if repeated else "")


def escape(value: str) -> str:
    return value.replace("|", "&#124;")


def build(profile: str, extensions: bool) -> str:
    source = CATALOGUE.read_text(encoding="utf-8")
    bit = 1 if profile == "Dbc" else 2
    entries = []
    by_symbol = defaultdict(list)
    for match in SIGNATURE.finditer(source):
        if match.group(2) == profile:
            by_symbol[match.group(1)].append(match.groups())
    for match in ENTRY.finditer(source):
        if int(match.group(4)) & bit == 0:
            continue
        symbol, kind, name = match.group(1), match.group(2), match.group(3)
        minimum_index, maximum_index, counts_index = ((5, 6, 7) if profile == "Dbc" else (8, 9, 10))
        arity = mask_counts(int(match.group(minimum_index)), int(match.group(maximum_index)),
                            int(match.group(counts_index), 16))
        implemented = runtime_status(symbol, int(match.group(4)), int(match.group(11)), bit)
        evidence = int(match.group(12))
        is_extension = bool(evidence & 0xF0)
        if is_extension != extensions:
            continue
        provenance = []
        if evidence & 1:
            provenance.append("Classic help")
        if evidence & 2:
            provenance.append("DBPro snapshot")
        if evidence & 4:
            provenance.append("OpenDBC extension")
        if evidence & 8:
            provenance.append("legacy unverified")
        if evidence & 16:
            provenance.append("ODE extension")
        if evidence & 32:
            provenance.append("3D Cloth & Particles extension")
        if evidence & 64:
            provenance.append("DarkPHYSICS extension")
        if evidence & 128:
            provenance.append("Dark Dynamix extension")
        signatures = []
        for signature in by_symbol[symbol]:
            signatures.append(f"{parameter_text(signature[6])} -> {RETURN_NAMES[signature[5]]}")
        signature_text = "; ".join(signatures) if signatures else "grammar form"
        entries.append((kind, name, arity, signature_text, implemented, ", ".join(provenance)))

    product = "DarkBASIC Classic" if profile == "Dbc" else "DarkBASIC Professional"
    title = f"{product} extension compatibility reference" if extensions else f"{product} compatibility reference"
    lines = [
        f"# {title}",
        "",
        "This file is generated from `src/language/language_catalogue.inc` by",
        "`scripts/generate_language_references.py`. It describes OpenDBC support; edit the catalogue, not this file.",
        "",
        "`.dba` is the source extension for both profiles. The project, detector or `--dialect` selects one",
        "profile before lexing; includes inherit it and OpenDBC never mixes profiles.",
        "A recognized `Unsupported` form fails binding with `DBC2043`. A `Compatibility stub` executes its",
        "documented neutral behavior and is never presented as implemented.",
        "Inside a function, ordinary scalar names are local; arrays retain their historical shared namespace.",
        "Function return types come from `ENDFUNCTION` and `EXITFUNCTION` values.",
        "",
        "Type legend: `number` accepts historical numeric coercion; `output` is assigned by the command;",
        "`array` requires a whole-array marker. Arity is the number of source arguments.",
        "",
        "| Symbol | Kind | Arity | Typed overloads | OpenDBC status | Provenance | Description |",
        "|---|---|---:|---|---|---|---|",
    ]
    if extensions:
        lines[4:4] = [
            "The extension surfaces are always available in the `DbPro` profile and share one portable physics architecture.",
            "The implemented ODE rigid-body core uses the pinned Jolt backend; remaining forms retain explicit diagnostics.",
            "Retired catalogue identifiers remain reserved; active extension forms use newly appended stable identifiers.",
        ]
    for kind, name, arity, signatures, status, provenance in sorted(entries, key=lambda row: (row[0], row[1])):
        if provenance == "OpenDBC extension" or (profile == "Dbc" and "OpenDBC extension" in provenance):
            description = "Shared OpenDBC VR addition; see docs/vr.md for its observable contract."
        elif status == "Implemented":
            description = f"{product} {kind.lower()} implemented with an observable contract."
        elif status.startswith("Compatibility stub"):
            description = f"Historical {product} {kind.lower()} callable through a documented neutral compatibility stub."
        else:
            description = f"Historical {product} {kind.lower()} recognized for a stable unsupported diagnostic."
        lines.append(f"| `{escape(name)}` | {kind} | {arity} | {escape(signatures)} | {status} | "
                     f"{provenance} | {description} |")
    return "\n".join(lines) + "\n"


def provenance_text(evidence: int) -> str:
    values = []
    for bit, label in (
        (1, "Classic help"),
        (2, "DBPro snapshot"),
        (4, "OpenDBC extension"),
        (8, "legacy unverified"),
        (16, "ODE extension"),
        (32, "3D Cloth & Particles extension"),
        (64, "DarkPHYSICS extension"),
        (128, "Dark Dynamix extension"),
    ):
        if evidence & bit:
            values.append(label)
    return ", ".join(values)


def canonical_variants() -> list[dict]:
    source = CATALOGUE.read_text(encoding="utf-8")
    signatures = defaultdict(list)
    for match in SIGNATURE.finditer(source):
        signatures[(match.group(1), match.group(2))].append(match.groups())
    rows = []
    for match in ENTRY.finditer(source):
        symbol, kind, name = match.group(1), match.group(2), match.group(3)
        if kind not in ("Command", "Function"):
            continue
        available = int(match.group(4))
        implemented = int(match.group(11))
        evidence = int(match.group(12))
        surface = next((label for bit, label in SURFACES.items() if evidence & bit), "Core")
        for profile, bit, fields in (("DBC", 1, (5, 6, 7)), ("DBPro", 2, (8, 9, 10))):
            if available & bit == 0:
                continue
            minimum, maximum, mask_index = fields
            arity = mask_counts(int(match.group(minimum)), int(match.group(maximum)),
                                int(match.group(mask_index), 16))
            overloads = []
            profile_name = "Dbc" if profile == "DBC" else "DbPro"
            for signature in signatures[(symbol, profile_name)]:
                overloads.append(f"{parameter_text(signature[6])} -> {RETURN_NAMES[signature[5]]}")
            rows.append({
                "symbol_id": symbol,
                "symbol": name,
                "kind": kind,
                "profiles": profile,
                "surface": surface,
                "arity": arity,
                "typed_overloads": "; ".join(overloads) if overloads else "grammar form",
                "runtime_status": runtime_status(symbol, available, implemented, bit),
                "provenance": provenance_text(evidence),
            })
    return rows


def reference_locations(references: dict[tuple[str, bool], str]) -> dict[tuple[str, str, str], str]:
    result = {}
    names = {
        ("Dbc", False): "darkbasic_reference.md",
        ("DbPro", False): "dbpro_reference.md",
        ("DbPro", True): "dbpro_extensions_reference.md",
    }
    for key, text in references.items():
        profile = "DBC" if key[0] == "Dbc" else "DBPro"
        for line_number, line in enumerate(text.splitlines(), 1):
            match = re.match(r"\| `([^`]+)` \| ([^|]+) \|", line)
            if match:
                result[(match.group(1), match.group(2).strip(), profile)] = f"{names[key]}:{line_number}"
    return result


def relationship_description(row: dict, relationship: str) -> str:
    product = "DarkBASIC Classic" if row["profiles"] == "DBC" else "DarkBASIC Professional"
    kind = row["kind"].lower()
    if relationship == "Shared identical":
        return ("Shared DarkBASIC Classic and Professional callable with the same signature and "
                "OpenDBC support state.")
    if row["runtime_status"] == "Implemented":
        return f"{product} {kind} implemented with an observable contract."
    if row["runtime_status"].startswith("Compatibility stub"):
        return f"Historical {product} {kind} callable through a documented neutral compatibility stub."
    return f"Historical {product} {kind} recognized for a stable unsupported diagnostic."


def row_metadata(row: dict, relationship: str, locations: dict) -> dict:
    profiles = row["profiles"]
    extension = row["surface"] != "Core"
    is_stub = row["runtime_status"].startswith("Compatibility stub")
    project_status = ("Implemented" if row["runtime_status"] == "Implemented" else
                      "Compatibility stub" if is_stub else "Not implemented")
    disposition = ("Delivered" if project_status == "Implemented" else
                   "Permanent compatibility" if row["runtime_status"] == "Compatibility stub (permanent)" else
                   "Temporary compatibility" if is_stub else
                   "Future extension work" if extension else "To classify")
    note = PUBLIC_NOTES.get((row["symbol"], profiles))
    if note is None:
        if project_status == "Implemented":
            note = "Implemented with an observable behavioral contract."
        elif is_stub:
            note = ("Executes a documented neutral compatibility behavior; it is not an implementation of the "
                    "historical facility.")
        elif extension:
            note = "Recognized extension surface; no handler until a coherent, tested backend lot is delivered."
        else:
            note = ("Recognized and rejected with DBC2043; exact Target/Study/Deferred disposition is not "
                    "present in the supplied files.")
    source_profiles = ("DBC", "DBPro") if profiles == "DBC + DBPro" else (profiles,)
    sources = [locations.get((row["symbol"], row["kind"], profile), "") for profile in source_profiles]
    sources = [value for value in sources if value]
    return {
        "symbol": row["symbol"],
        "kind": row["kind"],
        "profiles": profiles,
        "surface": row["surface"],
        "arity": row["arity"],
        "typed_overloads": row["typed_overloads"],
        "runtime_status": row["runtime_status"],
        "project_status": project_status,
        "disposition": disposition,
        "relationship": relationship,
        "provenance": row["provenance"],
        "description": relationship_description(row, relationship),
        "notes": note,
        "source": "; ".join(sources) or "src/language/language_catalogue.inc",
    }


def load_editorial() -> dict:
    try:
        editorial = json.loads(EDITORIAL.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"invalid language editorial source {EDITORIAL}: {error}") from error
    required = {"schema_version", "overrides", "additional_rows"}
    if set(editorial) != required or editorial["schema_version"] != 1:
        raise SystemExit("language editorial source must use schema version 1 and the documented top-level fields")
    if not isinstance(editorial["overrides"], list) or not isinstance(editorial["additional_rows"], list):
        raise SystemExit("language editorial overrides and additional_rows must be arrays")
    return editorial


def matches_editorial(row: dict, match: dict) -> bool:
    allowed = {"symbols", "symbol_prefix", "surface", "profiles", "kind"}
    if not match or set(match) - allowed:
        raise SystemExit(f"invalid language editorial matcher: {match}")
    if "symbols" in match and row["symbol"] not in match["symbols"]:
        return False
    if "symbol_prefix" in match and not row["symbol"].startswith(match["symbol_prefix"]):
        return False
    return all(row[field] == value for field, value in match.items()
               if field in {"surface", "profiles", "kind"})


def apply_editorial(rows: list[dict], editorial: dict) -> list[dict]:
    allowed_updates = {"project_status", "disposition", "description", "notes"}
    for override in editorial["overrides"]:
        if set(override) != {"match", "set", "expected_rows"} or not override["set"] or set(override["set"]) - allowed_updates:
            raise SystemExit(f"invalid language editorial override: {override}")
        matched = [row for row in rows if matches_editorial(row, override["match"])]
        if len(matched) != override["expected_rows"]:
            raise SystemExit(f"language editorial override expected {override['expected_rows']} rows but matched "
                             f"{len(matched)}: {override['match']}")
        if "symbols" in override["match"]:
            missing = sorted(set(override["match"]["symbols"]) - {row["symbol"] for row in matched})
            if missing:
                raise SystemExit("language editorial symbols no longer exist: " + ", ".join(missing))
        for row in matched:
            row.update(override["set"])
    seen = {(row["symbol"], row["kind"], row["profiles"], row["surface"]) for row in rows}
    if len(editorial["additional_rows"]) != 10:
        raise SystemExit("language editorial source must retain the ten documented New-profile rows")
    for additional in editorial["additional_rows"]:
        if set(additional) != set(ROW_FIELDS):
            raise SystemExit(f"language editorial additional row has an unexpected schema: {additional.get('symbol')}")
        if not all(isinstance(additional[field], str) and additional[field] for field in ROW_FIELDS):
            raise SystemExit(f"language editorial additional row contains an empty or non-text field: {additional.get('symbol')}")
        if additional["profiles"] != "DbcNew + DbProNew" or additional["surface"] != "OpenDBC New / Mobile":
            raise SystemExit(f"language editorial row would contaminate a historical profile: {additional['symbol']}")
        key = (additional["symbol"], additional["kind"], additional["profiles"], additional["surface"])
        if key in seen:
            raise SystemExit(f"language editorial row duplicates the canonical catalogue: {key}")
        seen.add(key)
        rows.append(dict(additional))
    for row in rows:
        if row["project_status"] == "Implemented" and row["runtime_status"] != "Implemented":
            raise SystemExit(f"implemented project status contradicts runtime status: {row['symbol']}")
        if row["project_status"] == "Implemented" and row["disposition"] != "Delivered":
            raise SystemExit(f"implemented rows must be delivered: {row['symbol']}")
    return rows


def build_unified_rows(editorial: dict, locations: dict) -> list[dict]:
    grouped = defaultdict(dict)
    for row in canonical_variants():
        grouped[(row["symbol"], row["kind"], row["surface"])][row["profiles"]] = row
    output = []
    for _, profiles in sorted(grouped.items(), key=lambda item: item[0]):
        dbc = profiles.get("DBC")
        dbpro = profiles.get("DBPro")
        comparable = (dbc and dbpro and dbc["surface"] == "Core" and
                      all(dbc[field] == dbpro[field]
                          for field in ("arity", "typed_overloads", "runtime_status")))
        if comparable:
            merged = dict(dbc)
            merged["profiles"] = "DBC + DBPro"
            output.append(row_metadata(merged, "Shared identical", locations))
            continue
        for profile in ("DBC", "DBPro"):
            row = profiles.get(profile)
            if not row:
                continue
            if row["surface"] != "Core":
                relationship = "DBPro extension"
            elif dbc and dbpro:
                relationship = "Profile-specific variant"
            else:
                relationship = f"{profile}-only"
            output.append(row_metadata(row, relationship, locations))
    missing_notes = sorted(set(PUBLIC_NOTES) - {(row["symbol"], row["profiles"]) for row in output})
    if missing_notes:
        raise SystemExit("public behavior notes no longer match canonical rows: " +
                         ", ".join(f"{symbol} ({profiles})" for symbol, profiles in missing_notes))
    apply_editorial(output, editorial)
    output.sort(key=lambda row: (row["symbol"], row["kind"],
                                 {"DBC": 0, "DBPro": 1, "DBC + DBPro": 2}.get(row["profiles"], 3),
                                 row["surface"]))
    return output


def markdown_escape(value: object) -> str:
    return str(value).replace("|", "&#124;").replace("\n", " ")










def canonical_syntax_rows(locations: dict) -> list[dict]:
    grouped = []
    source = CATALOGUE.read_text(encoding="utf-8")
    for match in ENTRY.finditer(source):
        symbol_id, kind, symbol = match.group(1), match.group(2), match.group(3)
        if kind in ("Command", "Function"):
            continue
        available = int(match.group(4))
        implemented = int(match.group(11))
        provenance = provenance_text(int(match.group(12)))
        variants = []
        for profile, bit, fields in (("DBC", 1, (5, 6, 7)), ("DBPro", 2, (8, 9, 10))):
            if available & bit == 0:
                continue
            variants.append({
                "symbol_id": symbol_id,
                "symbol": symbol,
                "kind": kind,
                "profiles": profile,
                "arity": mask_counts(int(match.group(fields[0])), int(match.group(fields[1])),
                                     int(match.group(fields[2]), 16)),
                "typed_form": "grammar form",
                "runtime_status": runtime_status(symbol_id, available, implemented, bit),
                "provenance": provenance,
            })
        if len(variants) == 2 and all(variants[0][field] == variants[1][field]
                                      for field in ("arity", "typed_form", "runtime_status")):
            row = dict(variants[0])
            row["profiles"] = "DBC + DBPro"
            row["relationship"] = "Shared identical"
            row["description"] = "Shared DBC/DBPro syntax form."
            source_profiles = ("DBC", "DBPro")
            grouped.append((row, source_profiles))
        else:
            for row in variants:
                row["relationship"] = f"{row['profiles']}-only"
                product = "DarkBASIC Classic" if row["profiles"] == "DBC" else "DarkBASIC Professional"
                state = ("implemented with an observable contract" if row["runtime_status"] == "Implemented"
                         else "callable through a documented neutral compatibility stub"
                         if row["runtime_status"].startswith("Compatibility stub")
                         else "recognized for a stable unsupported diagnostic")
                row["description"] = f"{product} {kind.lower()} {state}."
                grouped.append((row, (row["profiles"],)))
    output = []
    for row, profiles in grouped:
        sources = [locations.get((row["symbol"], row["kind"], profile), "") for profile in profiles]
        row["source"] = "; ".join(source for source in sources if source) or "src/language/language_catalogue.inc"
        row.pop("symbol_id")
        output.append(row)
    return output








def render_open_dbc_reference(rows: list[dict]) -> str:
    mobile = [row for row in rows if row["surface"] == "OpenDBC New / Mobile"]
    lines = [
        "# OpenDBC Reference", "",
        "> Generated from `scripts/data/language_catalogue_editorial.json` by `scripts/generate_language_references.py`; do not edit this file.", "",
        ("OpenDBC preserves historical Classic and Professional behavior and adds a shared, opt-in desktop VR surface "
         "to both profiles. The 14 VR forms and Classic `X VECTOR3`, `Y VECTOR3`, `Z VECTOR3` readers are documented "
         "in [VR](../vr.md). Other future additions remain reserved for explicit New profiles. "
         "Both existing profiles use `.dba`; `.dbpro` denotes a Professional project descriptor."), "",
        "## Host facilities", "",
        "| Facility | Where | What it does |",
        "|---|---|---|",
        "| Debug overlay | **F3** in the program window; Build → *Debug Overlay on Launch*; `--debug-overlay` | Shows renderer, object, camera, sync and last-error state. |",
        "| Diagnostic dumps | **F4** in the program window; Build → *Dump Scene and Renderer* | Writes scene and renderer diagnostic text files. |",
        "| Log verbosity | `opendbc run ... --log-level trace\\|debug\\|info\\|warn\\|error`; Build → *Log Level* | Selects the spdlog level for the rotating log and console. |",
        "| Frame bound | `opendbc run ... --max-frames N` | Ends after N presented frames. |",
        "| Asset root | `opendbc run ... --asset-root DIR`; Studio project context | Resolves relative media and file paths. |",
        "| Dialect selection | `--dialect dbc\\|dbpro` | Forces one immutable historical profile. |", "",
        "## Reserved New-profile language surface", "",
        ("Touch-zone keyboard emulation remains a host behavior. Direct touch, orientation and accelerometer access "
         "from BASIC is deferred to these opt-in profiles and is not catalogued in DBC or DBPro."), "",
        "| Symbol | Kind | Profiles | Arity | Typed form | Status |",
        "|---|---|---|---|---|---|",
    ]
    for row in mobile:
        lines.append("| " + " | ".join(markdown_escape(value) for value in (
            f"`{row['symbol']}`", row["kind"], row["profiles"], row["arity"],
            row["typed_overloads"], row["disposition"],
        )) + " |")
    lines.extend(["", "Touch devices emulate the historical directional keys and space key by default; the platform "
                  "back action maps to escape. Desktop emulation accepts direct-touch devices only, not trackpads or "
                  "mouse-generated touch identifiers.", "",
                  "## Documented engine choices", "",
                  "- `SET TEXT FONT name$, charset` uses Unicode fonts, so every character-set value selects the same glyphs.",
                  "- `MAKE STATIC OBJECT obj, 1` keeps the historical flag, but portable depth testing already produces the same visible occlusion result.",
                  "- Classic `LOAD IMAGE ..., texture mode` and `GET IMAGE ..., texturing flag` retain their compatibility hints while SDL_GPU uploads a full mip chain on demand.",
                  "- `STATIC LINE OF SIGHT ..., accuracy` uses an exact intersection test, so accuracy cannot change the result.",
                  "- `PERFORM CHECKLIST FOR GRAPHICS CARDS` lists the GPU driver selected by SDL.",
                  "- A requested 16-bit display remains logically RGB565 over the physical RGBA32 render target; project depth `16M` selects the 32-bit path.",
                  "- `CL$()` returns an empty string until the BASIC host exposes program arguments.",
                  ("- Classic `WRITE TO REGISTRY` and `GET REGISTRY` keep their values in `registry.json` in the "
                   "user's OpenDBC storage folder (SDL's preference path) instead of the Windows registry; folder "
                   "and key names ignore ASCII case."),
                  "- `3DBLIT AVAILABLE()` returns 1: every GPU draws 2D during 3D at full speed.", "",
                  "## Portable host contracts", "",
                  ("BASIC paths accept `/`, historical `\\`, or mixed separators. They normalize lexically before "
                   "resolution under the execution asset root, and confinement is checked after normalization. "
                   "POSIX hosts do not emulate drive-letter or UNC paths."), "",
                  ("Historical control-device commands use raw SDL joysticks. Missing rumble or haptic capabilities "
                   "remain portable no-ops; supported effects keep independent lifecycle-managed slots."), ""])
    return "\n".join(lines)






def language_catalogue() -> tuple[list[dict], list[dict]]:
    """Return the merged catalogue rows and the syntax rows, for the HTML reference generator."""
    locations = reference_locations({key: build(*key) for key in OUTPUTS})
    return build_unified_rows(load_editorial(), locations), canonical_syntax_rows(locations)


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate the profile references from the canonical catalogue")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    references = {key: build(*key) for key in OUTPUTS}
    outputs = dict(zip(OUTPUTS.values(), references.values()))
    rows = build_unified_rows(load_editorial(), reference_locations(references))
    outputs[OPENDBC_REFERENCE] = render_open_dbc_reference(rows)
    for output, generated in outputs.items():
        if args.check:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise SystemExit(f"language reference is stale: {output}")
        else:
            output.write_text(generated, encoding="utf-8")
    print("Language references are current" if args.check else "Regenerated language references")


if __name__ == "__main__":
    main()
