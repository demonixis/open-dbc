#!/usr/bin/env python3

import argparse
import csv
import hashlib
import html
import re
import subprocess
from pathlib import Path

import generate_dbpro_extension_evidence as extension_evidence


ROOT = Path(__file__).resolve().parents[1]
# Facts (names, kinds, accepted argument counts) extracted once from the DarkBASIC
# Classic help index; the help itself is not redistributed. --dbc-help-root
# re-verifies the snapshot against a local copy of that help.
DBC_EVIDENCE = ROOT / "scripts/data/dbc_catalogue_evidence.tsv"
CATALOGUE = ROOT / "src/language/language_catalogue.inc"
DBPRO_EVIDENCE = ROOT / "scripts/data/dbpro_catalogue_evidence.tsv"
DBPRO_EXTENSION_EVIDENCE = ROOT / "scripts/data/dbpro_extension_evidence.tsv"
CLASSIC_INDEX_SHA256 = "078263201bdaab13c24145e769dcdaabb40cb720bcc72915e4cd793f26ffc52a"
DBPRO_COMMIT = "af6f19d7fc2919c356c683f7f5b8ec0c37a7d7d9"
DBPRO_BASE_KEYWORDS_SHA256 = "e42a81f2bf3437193d5c8e78afe7857db8d7d2eabae62b5f068f10da5f330135"
DBPRO_KEYWORDS_SHA256 = "26f0146bb687795a613c9780b7cbe84701375e43ee418ebd9381c5e221666568"
DBPRO_TERRAIN_KEYWORDS_SHA256 = "161719945555adb463c0445fb188bb92c407bb2457b58859d47b4a9e7e2ec99d"
DBPRO_CORE_RESOURCE_EVIDENCE = (
    ("CHANGE MOUSE", "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Input/Input.rc",
     "Cursor Number, Touch Friendly"),
    ("CLS", "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Basic2D/Basic2D.rc", "Color Value"),
    ("SAVE BITMAP", "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Bitmap/Bitmap.rc", "Filename"),
    ("SAVE BITMAP", "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Bitmap/Bitmap.rc",
     "Filename,Bitmap Number"),
    ("SET OBJECT", "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Objects/Objects.rc",
     "Object Number, Wire, Transparent, Cull, Filter, Light, Fog, Ambient"),
    ("SET SPRITE RESIZE", "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Sprites/Sprites.rc",
     "Mode, Global X Offset, Global X Scale"),
)

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

CLASSIC_ALIASES = {
    "ENTYR$": "ENTRY$",
    "LIMB TEXTURE NAME": "LIMB TEXTURE NAME$",
    "MUSIC LOOPED": "MUSIC LOOPING",
    "NET MESSAGE STRING": "NET MESSAGE STRING$",
    "SET TEXT TO BOLD ITALIC": "SET TEXT TO BOLDITALIC",
}
DBPRO_EVIDENCE_ALIASES = {}
CLASSIC_LINK_NAME_OVERRIDES = {
    "system/maw.htm": "MAXIMIZE WINDOW",
    "system/miw.htm": "MINIMIZE WINDOW",
}
CLASSIC_STATEMENTS = {
    "CASE", "CASE DEFAULT", "DEC", "DIM", "DO", "ELSE", "END", "ENDCASE", "ENDFUNCTION",
    "ENDIF", "ENDSELECT", "ENDWHILE", "EXIT", "EXITFUNCTION", "FOR", "FUNCTION", "GOSUB",
    "GOTO", "IF", "INC", "LOOP", "NEXT", "REM", "REMEND", "REMSTART", "REPEAT", "RETURN",
    "SELECT", "STEP", "THEN", "UNTIL", "WHILE",
}
CLASSIC_ARITY_OVERRIDES = {
    "BOB COLLISION": {2}, "CHECK DISPLAY MODE": {3}, "CLEAR CAMERA VIEW": {1},
    "COLOR LIGHT": {2, 4}, "COPY BITMAP": {2, 10}, "DIR": {0}, "EXECUTE FILE": {3, 4},
    "FIND FIRST": {0}, "FORCE AUTO CENTER OFF": {0}, "FTP CONNECT": {3},
    "FTP DISCONNECT": {0, 1}, "FTP GET FILE": {2, 3}, "FTP PUT FILE": {1},
    "GET OBJECT COLLISION X": {0, 1}, "GET OBJECT COLLISION Y": {0, 1}, "GET OBJECT COLLISION Z": {0, 1},
    "GET STATIC COLLISION X": {0}, "GET STATIC COLLISION Y": {0}, "GET STATIC COLLISION Z": {0},
    "LISTENER ANGLE X": {0}, "LISTENER ANGLE Y": {0}, "LISTENER ANGLE Z": {0},
    "LISTENER POSITION X": {0}, "LISTENER POSITION Y": {0}, "LISTENER POSITION Z": {0},
    "LOAD CDMUSIC": {2}, "LOAD SOUND": {2, 3}, "LOOP SOUND": {1, 2, 3, 4},
    "MAKE OBJECT CONE": {2}, "MAKE OBJECT CYLINDER": {2}, "MAKE OBJECT SPHERE": {2},
    "MID$": {2}, "PERFORM CHECKLIST FOR FILES": {0},
    "PERFORM CHECKLIST FOR OBJECT LIMBS": {1}, "PLAY SOUND": {1, 2},
    "SAVE SOUND": {2}, "SET BOB": {3}, "SET DISPLAY MODE": {2, 3}, "SET MATRIX TEXTURE": {3}, "SET SPRITE": {3},
    "SET WINDOW LAYOUT": {3}, "SPRITE COLLISION": {2}, "SYNC": {0}, "TEXT SIZE": {0},
    "WRITE TO REGISTRY": {3},
}
CLASSIC_VARIADIC_ARITIES = {
    "CALL DLL": (2, 11), "DATA": (1, 99), "PRINT": (0, 99), "READ": (1, 99),
}
CLASSIC_PERMANENT_STUB_TYPED_SIGNATURES = {
    "Command_CallDll": {(2, 11, 0, "Void", "N,S,A,A,A,A,A,A,A,A,A")},
    "Function_CallDll": {(2, 11, 0, "Real", "N,S,A,A,A,A,A,A,A,A,A")},
    "Command_CreateNetGame": {(3, 4, 0x18, "Void", "S,S,N,N")},
    "Command_CreateNetPlayer": {(1, 1, 0x2, "Void", "S")},
    "Function_CreateNetPlayer": {(1, 1, 0x2, "Real", "S")},
    "Function_DllCallExist": {(2, 2, 0x4, "Real", "N,S")},
    "Function_GetRegistry": {(2, 2, 0x4, "Real", "S,S")},
    "Command_JoinNetGame": {(2, 2, 0x4, "Void", "N,S")},
    "Command_LoadAnimation": {(2, 2, 0x4, "Void", "S,N")},
    "Command_LoadDll": {(2, 2, 0x4, "Void", "S,N")},
    "Command_SendNetMessageString": {(2, 2, 0x4, "Void", "N,S")},
    "Command_SetNetConnection": {(1, 2, 0x6, "Void", "N,S")},
    "Command_WriteToRegistry": {(3, 3, 0, "Void", "S,S,N")},
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def accepted_counts(signature: str) -> tuple[int, ...]:
    signature = signature.strip()
    if signature.upper() in {"DATA STATEMENTS", "PRINT STATEMENTS", "VARIABLE"}:
        return ()
    if not signature:
        return ()
    forms = re.findall(r"\[([^]]*)\]", signature) or [signature]
    result = set()
    for form in forms:
        form = form.strip()
        if form.startswith("(") and form.endswith(")"):
            form = form[1:-1].strip()
        if not form or form.lower() == "*no parameters*":
            result.add(0)
        else:
            result.add(form.count(",") + 1)
    return tuple(sorted(result))


def normalized_name(value: str) -> str:
    return " ".join(html.unescape(value).upper().replace("()", "").split())


def classic_help_inventory(help_root: Path) -> dict[tuple[str, str], tuple[set[int] | tuple[int, int], set[str]]]:
    index_path = help_root / "index.htm"
    require(sha256(index_path) == CLASSIC_INDEX_SHA256,
            "DarkBASIC Classic index changed without a catalogue provenance update")
    index = index_path.read_text(errors="replace")
    inventory: dict[tuple[str, str], tuple[set[int] | tuple[int, int], set[str]]] = {}
    for href, raw_name in re.findall(r'<a\s+href="([^"]+\.htm)">([^<]+)</a>', index, re.IGNORECASE):
        if href.lower() == "menu.htm":
            continue
        raw_name = normalized_name(raw_name)
        name = CLASSIC_LINK_NAME_OVERRIDES.get(href.lower(), CLASSIC_ALIASES.get(raw_name, raw_name))
        path = help_root / Path(href.replace("\\", "/"))
        require(path.is_file(), f"Classic help index points to a missing page: {href}")
        discovered: list[tuple[str, set[int] | tuple[int, int]]]
        if name == "#INCLUDE":
            discovered = [("Directive", {1})]
        elif name in CLASSIC_STATEMENTS:
            discovered = [("Statement", set())]
        else:
            document = path.read_text(errors="replace")
            sections = re.split(r">SYNTAX\s*<", document, flags=re.IGNORECASE)
            require(len(sections) >= 2, f"Classic help page has no syntax section: {path}")
            syntax = re.split(r">RELATED INFO<", sections[1], flags=re.IGNORECASE)[0]
            lines = [
                re.sub(r"^BR>\s*", "", " ".join(html.unescape(re.sub(r"<[^>]+>", " ", line)).split()))
                for line in re.split(r"<BR\s*/?>|\r?\n", syntax, flags=re.IGNORECASE)
            ]
            candidates = [line for line in lines if name in line.upper() or raw_name in line.upper()]
            groups: dict[str, set[int]] = {}
            for line in candidates:
                upper = line.upper()
                spelling = name if name in upper else raw_name
                tail = line[upper.rfind(spelling) + len(spelling):].strip()
                kind = "Function" if ("RETURN" in upper or "=" in line) and "(" in line else "Command"
                if kind == "Function":
                    match = re.match(r"\s*\((.*)\)", tail)
                    if not match:
                        continue
                    parameters = match.group(1).strip()
                    count = 0 if not parameters else parameters.count(",") + 1
                else:
                    count = 0 if not tail else tail.count(",") + 1
                groups.setdefault(kind, set()).add(count)
            require(groups, f"Could not derive Classic help signature for {name}: {path}")
            if name in CLASSIC_VARIADIC_ARITIES:
                discovered = [(kind, CLASSIC_VARIADIC_ARITIES[name]) for kind in groups]
            elif name in CLASSIC_ARITY_OVERRIDES:
                discovered = [(kind, CLASSIC_ARITY_OVERRIDES[name]) for kind in groups]
            else:
                discovered = list(groups.items())
        for kind, counts in discovered:
            key = (kind, name)
            existing = inventory.get(key)
            if existing:
                previous_counts, paths = existing
                require(isinstance(previous_counts, set) and isinstance(counts, set),
                        f"Duplicate variadic Classic help entry: {name}")
                previous_counts.update(counts)
                paths.add(path.relative_to(help_root).as_posix())
            else:
                inventory[key] = (counts, {path.relative_to(help_root).as_posix()})
    return inventory


def evidence_kind(name: str, signatures: list[str], known_kinds: set[str]) -> str:
    if name in {"AS", "BOOLEAN", "BYTE", "DOUBLE FLOAT", "DOUBLE INTEGER", "DWORD", "ENDTYPE",
                "FLOAT", "GLOBAL", "INTEGER", "LOCAL", "STRING", "TYPE", "WORD"}:
        return "Statement"
    if name in {"AND", "NOT", "OR", "XOR"}:
        return "Operator"
    if name == "GET REGISTRY":
        return "Function"
    if len(known_kinds) == 1:
        return next(iter(known_kinds))
    if any(signature.strip().startswith("(") or re.search(r"\[\s*\(", signature)
           for signature in signatures):
        return "Function"
    if name.endswith("$") or name.endswith(" AVAILABLE") or name.startswith("GET "):
        return "Function"
    if name in {"AMBIENT LIGHT", "CONTROL DEVICE INDEX", "DESKTOP HEIGHT", "DESKTOP WIDTH",
                "NET BUFFER SIZE", "PERFTIMER"}:
        return "Function"
    return "Command"


def read_dbpro_sources(root: Path) -> dict[str, dict[str, object]]:
    keyword_root = root / "Install/Editor/Keywords"
    result: dict[str, dict[str, object]] = {}
    for filename in ("Keywords100.ini", "keywords107.ini", "keywordsAT.ini"):
        path = keyword_root / filename
        for raw in path.read_text(errors="replace").splitlines():
            if not raw or raw.startswith("[") or raw.startswith(";") or "=" not in raw:
                continue
            raw_name, _, remainder = raw.partition("=")
            name = " ".join(raw_name.upper().split())
            if not name:
                continue
            signature = remainder.split("=", 1)[1].strip() if "=" in remainder else ""
            row = result.setdefault(name, {"sources": set(), "signatures": []})
            row["sources"].add(filename)
            if signature not in row["signatures"]:
                row["signatures"].append(signature)
    for name, relative_path, signature in DBPRO_CORE_RESOURCE_EVIDENCE:
        path = root / relative_path
        document = path.read_text(errors="replace")
        require(any(f'"{name}%' in line and f'%{signature}"' in line for line in document.splitlines()),
                f"DBPro core resource evidence changed for {name}: {relative_path}")
        row = result.setdefault(name, {"sources": set(), "signatures": []})
        row["sources"].add(relative_path)
        if signature not in row["signatures"]:
            row["signatures"].append(signature)
    return result


def refresh_dbpro_evidence(root: Path) -> None:
    rows = read_dbpro_sources(root)
    known: dict[str, set[str]] = {}
    for match in ENTRY.finditer(CATALOGUE.read_text()):
        known.setdefault(match.group(3), set()).add(match.group(2))
    with DBPRO_EVIDENCE.open("w", newline="") as output:
        output.write(f"# DBPro commit\t{DBPRO_COMMIT}\n")
        output.write(f"# Keywords100.ini sha256\t{DBPRO_BASE_KEYWORDS_SHA256}\n")
        output.write(f"# keywords107.ini sha256\t{DBPRO_KEYWORDS_SHA256}\n")
        output.write(f"# keywordsAT.ini sha256\t{DBPRO_TERRAIN_KEYWORDS_SHA256}\n")
        writer = csv.writer(output, delimiter="\t", lineterminator="\n")
        writer.writerow(("name", "kind", "accepted_counts", "sources", "signatures"))
        for name, value in sorted(rows.items()):
            signatures = value["signatures"]
            counts = sorted({count for signature in signatures for count in accepted_counts(signature)})
            writer.writerow((name, evidence_kind(name, signatures, known.get(name, set())),
                             ",".join(map(str, counts)),
                             ",".join(sorted(value["sources"])), " | ".join(signatures) or "-"))


def audit_catalogue() -> tuple[list[tuple], dict[tuple[str, str], tuple]]:
    entries = []
    signatures = []
    for number, line in enumerate(CATALOGUE.read_text().splitlines(), 1):
        if line.startswith("//") or not line.strip():
            continue
        match = ENTRY.fullmatch(line)
        if match is None:
            signature = SIGNATURE.fullmatch(line)
            require(signature is not None, f"Malformed catalogue entry at line {number}")
            signatures.append((signature.group(1), signature.group(2), int(signature.group(3)),
                               int(signature.group(4)), int(signature.group(5), 16),
                               signature.group(6), signature.group(7)))
            continue
        symbol_id, kind, name = match.group(1), match.group(2), match.group(3)
        profiles = int(match.group(4))
        dbc_min, dbc_max = int(match.group(5)), int(match.group(6))
        dbpro_min, dbpro_max = int(match.group(8)), int(match.group(9))
        implemented, evidence = int(match.group(11)), int(match.group(12))
        require(profiles & ~3 == 0, f"Invalid profile mask for {name}")
        require((kind == "Reserved") == (profiles == 0), f"Reserved/profile mismatch for {name}")
        require(implemented & ~profiles == 0, f"Implemented profile is unavailable for {name}")
        require(profiles == 0 or evidence != 0, f"Available symbol has no evidence: {name}")
        require(dbc_min <= dbc_max and dbpro_min <= dbpro_max, f"Invalid arity for {name}")
        dbc_counts = int(match.group(7), 16)
        dbpro_counts = int(match.group(10), 16)
        entries.append((symbol_id, kind, name, profiles, dbc_min, dbc_max, dbc_counts,
                        dbpro_min, dbpro_max, dbpro_counts, implemented, evidence))

    require(len({entry[0] for entry in entries}) == len(entries),
            "Duplicate SymbolId in language catalogue")

    expected_counts = {}
    symbols = {entry[0]: entry for entry in entries}
    for entry in entries:
        symbol_id, kind, _, profiles = entry[:4]
        if kind not in {"Command", "Function"}:
            continue
        for profile, bit, minimum, maximum, mask in (
                ("Dbc", 1, entry[4], entry[5], entry[6]),
                ("DbPro", 2, entry[7], entry[8], entry[9])):
            if profiles & bit == 0:
                continue
            expected_counts[(symbol_id, profile)] = (
                {count for count in range(64) if mask & (1 << count)} if mask else
                set(range(minimum, maximum + 1))
            )

    actual_signatures = set()
    actual_counts = {key: set() for key in expected_counts}
    for symbol_id, profile, minimum, maximum, mask, result, parameters in signatures:
        key = (symbol_id, profile)
        exact_key = (symbol_id, profile, minimum, maximum, mask, result, parameters)
        require(symbol_id in symbols, f"Signature refers to an unknown SymbolId: {symbol_id}")
        require(key in expected_counts, f"Signature refers to an unavailable profile: {key}")
        require(exact_key not in actual_signatures, f"Duplicate overload signature: {exact_key}")
        actual_signatures.add(exact_key)
        accepted = ({count for count in range(64) if mask & (1 << count)} if mask else
                    set(range(minimum, maximum + 1)))
        require(accepted <= expected_counts[key], f"Overload signature exceeds catalogue arity: {exact_key}")
        actual_counts[key].update(accepted)
        entry = symbols[symbol_id]
        require((entry[1] == "Command") == (result == "Void"),
                f"Command/function return mismatch for {symbol_id}")
        if parameters == "-":
            parameter_count = 0
        elif parameters.endswith("*"):
            require(re.fullmatch(r"[ANIRS](?:!|\[\])?\*", parameters) is not None,
                    f"Malformed repeated parameter signature for {symbol_id}")
            parameter_count = maximum
        else:
            tokens = parameters.split(",")
            require(all(re.fullmatch(r"[ANIRS](?:!|\[\])?", token) for token in tokens),
                    f"Malformed parameter signature for {symbol_id}")
            parameter_count = len(tokens)
        require(parameter_count == maximum,
                f"Parameter count differs from source arity for {symbol_id} {profile}")
    require(actual_counts == expected_counts,
            "Explicit overload signatures do not exhaust the callable catalogue")

    for symbol_id, expected in CLASSIC_PERMANENT_STUB_TYPED_SIGNATURES.items():
        actual = {(minimum, maximum, mask, result, parameters)
                  for current_id, profile, minimum, maximum, mask, result, parameters in signatures
                  if current_id == symbol_id and profile == "Dbc"}
        require(actual == expected,
                f"Classic permanent-stub types differ from help for {symbol_id}: "
                f"catalogue={sorted(actual)}, expected={sorted(expected)}")

    typed_spellings = set()
    for _, kind, name, profiles, *_ in entries:
        require(name == name.upper(), f"Canonical spelling is not uppercase: {name}")
        for profile in (1, 2):
            if profiles & profile:
                key = (profile, kind, name)
                require(key not in typed_spellings, f"Duplicate typed spelling: {key}")
                typed_spellings.add(key)
    return entries, {(entry[1], entry[2]): entry for entry in entries}


def dbc_evidence_text(inventory: dict[tuple[str, str], tuple[set[int] | tuple[int, int], set[str]]]) -> str:
    lines = [f"# DarkBASIC Classic help index sha256\t{CLASSIC_INDEX_SHA256}",
             "kind\tname\taccepted_counts\tpages"]
    for (kind, name), (counts, pages) in sorted(inventory.items(), key=lambda item: (item[0][1], item[0][0])):
        accepted = (f"{counts[0]}..{counts[1]}" if isinstance(counts, tuple) else
                    ",".join(map(str, sorted(counts))))
        lines.append(f"{kind}\t{name}\t{accepted}\t{','.join(sorted(pages))}")
    return "\n".join(lines) + "\n"


def load_dbc_evidence() -> dict[tuple[str, str], tuple[set[int] | tuple[int, int], set[str]]]:
    require(DBC_EVIDENCE.is_file(), "Missing DarkBASIC Classic catalogue evidence snapshot")
    inventory = {}
    with DBC_EVIDENCE.open(newline="", encoding="utf-8") as source:
        rows = csv.DictReader((line for line in source if not line.startswith("#")), delimiter="\t")
        for row in rows:
            accepted = row["accepted_counts"]
            if ".." in accepted:
                low, high = accepted.split("..")
                counts = (int(low), int(high))
            else:
                counts = {int(value) for value in accepted.split(",") if value}
            inventory[(row["kind"], row["name"])] = (counts, set(row["pages"].split(",")))
    require(inventory, "DarkBASIC Classic catalogue evidence snapshot is empty")
    return inventory


def audit_classic_help(catalogue: dict[tuple[str, str], tuple]) -> None:
    inventory = load_dbc_evidence()
    for raw_alias, canonical in CLASSIC_ALIASES.items():
        require(not any(name == raw_alias and entry[3] & 1 for (_, name), entry in catalogue.items()),
                f"Reviewed Classic alias leaked into the canonical profile: {raw_alias}")
        require(any(name == canonical and entry[3] & 1 for (_, name), entry in catalogue.items()),
                f"Reviewed Classic alias target is absent: {raw_alias} -> {canonical}")
    for key, (expected, paths) in inventory.items():
        require(key in catalogue, f"Classic help is absent from catalogue: {key[0]} {key[1]}")
        entry = catalogue[key]
        require(entry[3] & 1, f"Classic help symbol is unavailable in DBC: {key[1]}")
        require(entry[11] & 1, f"Classic help provenance is missing for {key[1]}")
        if key[0] not in {"Command", "Function"}:
            continue
        if isinstance(expected, tuple):
            require((entry[4], entry[5], entry[6]) == (expected[0], expected[1], 0),
                    f"Classic variadic signature differs for {key[1]}: catalogue={entry[4:7]}, help={expected}")
            continue
        actual = ({count for count in range(64) if entry[6] & (1 << count)} if entry[6] else
                  set(range(entry[4], entry[5] + 1)))
        require(actual == expected,
                f"Classic arity differs for {key[1]}: catalogue={sorted(actual)}, help={sorted(expected)}, "
                f"pages={sorted(paths)}")


def audit_dbpro_evidence(catalogue: dict[tuple[str, str], tuple]) -> None:
    require(DBPRO_EVIDENCE.is_file(), "Missing reproducible DBPro catalogue evidence snapshot")
    with DBPRO_EVIDENCE.open(newline="") as source:
        rows = [row for row in csv.DictReader((line for line in source if not line.startswith("#")), delimiter="\t")]
    require(rows, "DBPro catalogue evidence snapshot is empty")
    for row in rows:
        name = DBPRO_EVIDENCE_ALIASES.get(row["name"], row["name"])
        key = (row["kind"], name)
        require(key in catalogue, f"DBPro evidence is absent from catalogue: {row['kind']} {row['name']}")
        entry = catalogue[key]
        profiles = entry[3]
        require(profiles & 2, f"DBPro evidence is unavailable in DBPro profile: {name}")
        counts = {int(value) for value in row["accepted_counts"].split(",") if value}
        if row["kind"] in {"Command", "Function"} and counts:
            accepted_mask = entry[9]
            accepted = ({count for count in range(64) if accepted_mask & (1 << count)} if accepted_mask else
                        set(range(entry[7], entry[8] + 1)))
            require(counts == accepted,
                    f"DBPro arity differs from evidence for {row['name']}: catalogue={sorted(accepted)}, evidence={sorted(counts)}")


def audit_dbpro_extension_evidence(catalogue: dict[tuple[str, str], tuple]) -> None:
    require(DBPRO_EXTENSION_EVIDENCE.is_file(), "Missing reproducible DBPro extension evidence snapshot")
    with DBPRO_EXTENSION_EVIDENCE.open(newline="", encoding="utf-8") as source:
        rows = [row for row in csv.DictReader((line for line in source if not line.startswith("#")), delimiter="\t")]
    require(rows, "DBPro extension evidence snapshot is empty")
    surface_bits = extension_evidence.SURFACE_BITS
    spellings = set()
    for row in rows:
        key = (row["kind"], row["name"])
        require(key in catalogue, f"DBPro extension evidence is absent from catalogue: {row['surface']} {key}")
        entry = catalogue[key]
        require(entry[0] == row["symbol_id"], f"DBPro extension SymbolId differs for {row['name']}")
        require(entry[3] == 2, f"DBPro extension leaked outside the DbPro profile: {row['name']}")
        require(entry[11] == surface_bits[row["surface"]], f"DBPro extension provenance differs: {row['name']}")
        count = int(row["accepted_count"])
        accepted_mask = entry[9]
        accepted = ({value for value in range(64) if accepted_mask & (1 << value)} if accepted_mask else
                    set(range(entry[7], entry[8] + 1)))
        require(count in accepted, f"DBPro extension arity differs from evidence for {row['name']}")
        spellings.add((row["surface"], row["name"]))
    require(len(spellings) == 1204, "DBPro extension public spelling total drifted")


def audit_dbpro(root: Path) -> None:
    commit = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    require(commit == DBPRO_COMMIT, f"Expected DBPro {DBPRO_COMMIT}, found {commit}")
    keywords = root / "Install/Editor/Keywords/keywords107.ini"
    base = root / "Install/Editor/Keywords/Keywords100.ini"
    terrain = root / "Install/Editor/Keywords/keywordsAT.ini"
    require(sha256(base) == DBPRO_BASE_KEYWORDS_SHA256, "DBPro Keywords100.ini hash mismatch")
    require(sha256(keywords) == DBPRO_KEYWORDS_SHA256, "DBPro keywords107.ini hash mismatch")
    require(sha256(terrain) == DBPRO_TERRAIN_KEYWORDS_SHA256, "DBPro keywordsAT.ini hash mismatch")
    generated_extensions = extension_evidence.EvidenceText(extension_evidence.BuildEvidence(root))
    require(DBPRO_EXTENSION_EVIDENCE.read_text(encoding="utf-8") == generated_extensions,
            "DBPro extension evidence differs from pinned source")


def main() -> None:
    parser = argparse.ArgumentParser(description="Audit the immutable OpenDBC language catalogue")
    parser.add_argument("--dbpro-root", type=Path,
                        help="optional pinned Dark-Basic-Pro checkout to verify")
    parser.add_argument("--dbc-help-root", type=Path,
                        help="optional local DarkBASIC Classic help folder to verify the Classic evidence")
    parser.add_argument("--refresh-dbc-evidence", action="store_true",
                        help="rewrite the tracked Classic evidence snapshot from --dbc-help-root")
    parser.add_argument("--refresh-dbpro-evidence", action="store_true",
                        help="rewrite the tracked DBPro evidence snapshot from --dbpro-root")
    args = parser.parse_args()
    entries, catalogue = audit_catalogue()
    if args.dbc_help_root:
        generated = dbc_evidence_text(classic_help_inventory(args.dbc_help_root.resolve()))
        if args.refresh_dbc_evidence:
            DBC_EVIDENCE.write_text(generated, encoding="utf-8")
        require(DBC_EVIDENCE.read_text(encoding="utf-8") == generated,
                "DarkBASIC Classic evidence differs from the supplied help")
    elif args.refresh_dbc_evidence:
        raise SystemExit("--refresh-dbc-evidence requires --dbc-help-root")
    audit_classic_help(catalogue)
    if args.dbpro_root:
        audit_dbpro(args.dbpro_root.resolve())
        if args.refresh_dbpro_evidence:
            refresh_dbpro_evidence(args.dbpro_root.resolve())
    elif args.refresh_dbpro_evidence:
        raise SystemExit("--refresh-dbpro-evidence requires --dbpro-root")
    audit_dbpro_evidence(catalogue)
    audit_dbpro_extension_evidence(catalogue)
    callables = sum(entry[1] in {"Command", "Function"} for entry in entries)
    print(f"Language catalogue audit passed: {len(entries)} symbols, {callables} callables")


if __name__ == "__main__":
    main()
