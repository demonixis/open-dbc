#!/usr/bin/env python3

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "src/language/language_catalogue.inc"
DBPRO_EXTENSION_EVIDENCE = ROOT / "scripts/data/dbpro_extension_evidence.tsv"
BEGIN = "// BEGIN GENERATED OVERLOAD SIGNATURES"
END = "// END GENERATED OVERLOAD SIGNATURES"
ENTRY = re.compile(
    r'DBC_CATALOGUE\(([^,]+),\s*([^,]+),\s*"([^"]+)",\s*'
    r'(\d+),\s*(-?\d+),\s*(-?\d+),\s*UINT64_C\((0x[0-9A-Fa-f]+|0)\),\s*'
    r'(-?\d+),\s*(-?\d+),\s*UINT64_C\((0x[0-9A-Fa-f]+|0)\),\s*'
    r'(\d+),\s*(\d+)\)'
)

STRING_PARAMETERS = {
    "APPEND OBJECT ANIMATION": {0, 1},
    "CD": {0},
    "EXECUTE FILE": {0, 1, 2},
    "EXIT PROMPT": {0, 1},
    "FTP CONNECT": {0, 1, 2},
    "FTP DELETE FILE": {0},
    "FTP GET FILE": {0, 1},
    "FTP PUT FILE": {0},
    "FTP SET DIR": {0},
    "LOAD BITMAP": {0},
    "LOAD EFFECT": {0},
    "LOAD IMAGE": {0},
    "LOAD MESH": {0},
    "LOAD MUSIC": {0},
    "LOAD OBJECT": {0},
    "LOAD SOUND": {0},
    "LOAD STATIC OBJECTS": {0},
    "LOAD 3DSOUND": {0},
    "MAKE TERRAIN": {1},
    "SAVE SOUND": {0},
    "COPY FILE": {0, 1},
    "DELETE FILE": {0},
    "SAVE BITMAP": {0},
    "SAVE IMAGE": {0},
    "SAVE MESH": {0},
    "SAVE OBJECT": {0},
    "SAVE OBJECT ANIMATION": {0},
    "SAVE STATIC OBJECTS": {0},
    "PERFORM CHECKLIST FOR FILES": {0},
    "TEXT": {2},
    "CENTER TEXT": {2},
    "SET TEXT FONT": {0},
    "OPEN TO READ": {1},
    "OPEN TO WRITE": {1},
    "READ FILEBLOCK": {1},
    "READ DIRBLOCK": {1},
    "WRITE FILEBLOCK": {1},
    "WRITE DIRBLOCK": {1},
    "DELETE DIRECTORY": {0},
    "MAKE DIRECTORY": {0},
    "MAKE FILE": {0},
    "MOVE FILE": {0, 1},
    "RENAME FILE": {0, 1},
    "SET DIR": {0},
    "FILE SIZE": {0},
    "PATH EXIST": {0},
    "READ BYTE FROM FILE": {0},
    "WRITE BYTE TO FILE": {0},
    "WRITE TO CLIPBOARD": {0},
    "WRITE STRING": {1},
    "TEXT WIDTH": {0},
    "TEXT HEIGHT": {0},
    "ASC": {0},
    "LEN": {0},
    "UPPER$": {0},
    "LOWER$": {0},
    "VAL": {0},
    "FILE EXIST": {0},
    "IMAGE FILE EXIST": {0},
    "LEFT$": {0},
    "RIGHT$": {0},
    "MID$": {0},
    "INSTR": {0, 1},
    "APPEND$": {0, 1},
    "COMPARE CASE$": {0, 1},
    "FIND FIRST CHAR$": {0, 1},
    "FIND LAST CHAR$": {0, 1},
    "FIND SUB STRING$": {0, 1},
    "FIRST TOKEN$": {0, 1},
    "NEXT TOKEN$": {0},
    "REVERSE$": {0},
    "SET WINDOW TITLE": {0},
    "WINDOW EXIST": {0},
    "WINDOW TO FRONT": {0},
    "CREATE ANIMATED SPRITE": {1},
    "SET CONTROL DEVICE": {0},
    "SET EFFECT ON": {1},
    "SET EFFECT TECHNIQUE": {1},
    "SET EFFECT CONSTANT BOOLEAN": {1},
    "SET EFFECT CONSTANT FLOAT": {1},
    "SET EFFECT CONSTANT INTEGER": {1},
    "SET EFFECT CONSTANT MATRIX": {1},
    "SET EFFECT CONSTANT VECTOR": {1},
}

PROFILE_STRING_PARAMETERS = {
    ("Dbc", "APPEND OBJECT"): {0},
    ("Dbc", "SET GRAPHICS CARD"): {0},
}

PROFILE_PARAMETER_SPECS = {
    ("Dbc", "SET VR ENABLED"): "I",
    ("Dbc", "SET TRACKING SPACE"): "I",
    ("Dbc", "GET VR CONTROLLER POSITION"): "I,I",
    ("Dbc", "GET VR CONTROLLER ROTATION"): "I,I",
    ("Dbc", "GET VR HEAD POSITION"): "I",
    ("Dbc", "GET VR ROTATION"): "I",
    ("Dbc", "IS VR CONTROLLER TRACKED"): "I",
    ("Dbc", "SET VR PREVIEW MODE"): "I",
    ("Dbc", "SET VR CAMERA HEIGHT"): "R",
    ("DbPro", "SET VR ENABLED"): "I",
    ("DbPro", "SET TRACKING SPACE"): "I",
    ("DbPro", "GET VR CONTROLLER POSITION"): "I,I",
    ("DbPro", "GET VR CONTROLLER ROTATION"): "I,I",
    ("DbPro", "GET VR HEAD POSITION"): "I",
    ("DbPro", "GET VR ROTATION"): "I",
    ("DbPro", "IS VR CONTROLLER TRACKED"): "I",
    ("DbPro", "SET VR PREVIEW MODE"): "I",
    ("DbPro", "SET VR CAMERA HEIGHT"): "R",

    ("Dbc", "CALL DLL"): "N,S,A,A,A,A,A,A,A,A,A",
    ("Dbc", "CREATE NET GAME"): "S,S,N,N",
    ("Dbc", "CREATE NET PLAYER"): "S",
    ("Dbc", "DLL CALL EXIST"): "N,S",
    ("Dbc", "GET REGISTRY"): "S,S",
    ("Dbc", "JOIN NET GAME"): "N,S",
    ("Dbc", "LOAD ANIMATION"): "S,N",
    ("Dbc", "LOAD DLL"): "S,N",
    ("Dbc", "SEND NET MESSAGE STRING"): "N,S",
    ("Dbc", "SET NET CONNECTION"): "N,S",
    ("Dbc", "WRITE TO REGISTRY"): "S,S,N",
}

INTEGER_RETURNS = {
    "IS VR CONTROLLER TRACKED", "IS VR HEAD TRACKED", "IS VR SUPPORTED", "IS VR ENABLED",
    "ASC", "BITMAP EXIST", "BITMAP HEIGHT", "BITMAP WIDTH", "CHECK DISPLAY MODE",
    "CHECK LIMB LINK", "CHECKLIST QUANTITY", "FILE EXIST", "GET MEMBLOCK SIZE",
    "GET SOUND PAN", "GET SOUND SPEED", "GET SOUND VOLUME", "GET STATIC COLLISION HIT",
    "EFFECT EXIST", "IMAGE EXIST", "IMAGE HEIGHT", "IMAGE WIDTH", "INSTR", "INT", "IS EQUAL MATRIX4",
    "IS EQUAL VECTOR3", "IS IDENTITY MATRIX4", "LEN", "LIMB EXIST", "LIMB VISIBLE",
    "GET INDEXDATA", "GET VERTEXDATA DIFFUSE", "GET VERTEXDATA INDEX COUNT",
    "GET VERTEXDATA VERTEX COUNT", "DELETE MATRIX4", "DELETE VECTOR2", "DELETE VECTOR3",
    "DELETE VECTOR4", "MAKE MATRIX4", "MAKE VECTOR2", "MAKE VECTOR3", "MAKE VECTOR4", "MEMBLOCK BYTE",
    "MEMBLOCK DWORD", "MEMBLOCK EXIST", "MESH EXIST",
    "MEMBLOCK WORD", "MEMORY FREE", "MUSIC EXIST", "MUSIC LOOPING", "MUSIC PAUSED",
    "MUSIC PLAYING", "MUSIC SPEED", "MUSIC VOLUME", "OBJECT COLLISION", "OBJECT EXIST",
    "OBJECT LOOPING", "OBJECT PLAYING", "OBJECT VISIBLE", "PICK OBJECT", "POINT",
    "PARTICLES EXIST",
    "READ BYTE", "READ LONG", "READ WORD", "RGB", "RGBB", "RGBG", "RGBR", "RND",
    "ARRAY COUNT", "ARRAY INDEX VALID", "BITMAP DEPTH", "BITMAP FLIPPED", "BITMAP MIRRORED",
    "COMPARE CASE$", "CURRENT BITMAP", "DESKTOP HEIGHT", "DESKTOP WIDTH", "FILE END", "FILE OPEN", "FILE SIZE",
    "FIND FIRST CHAR$", "FIND LAST CHAR$", "FIND SUB STRING$",
    "GET FILE TYPE", "IMAGE FILE EXIST", "PATH EXIST", "READ BYTE FROM FILE",
    "DOWNKEY", "INKEY", "LEFTKEY", "MOUSECLICK", "MOUSEX", "MOUSEY", "RETURNKEY",
    "RIGHTKEY", "SCANCODE", "SCREEN FPS", "SCREEN HEIGHT", "SCREEN WIDTH", "SPACEKEY",
    "UPKEY", "SCREEN DEPTH", "SCREEN INVALID", "SCREEN TYPE", "SOUND EXIST", "SOUND LOOPING",
    "SOUND PAN", "SOUND PAUSED", "SOUND PLAYING", "SOUND SPEED", "SOUND TYPE", "SOUND VOLUME", "SPRITE ALPHA",
    "SPRITE BLUE", "SPRITE EXIST", "SPRITE GREEN", "SPRITE IMAGE", "SPRITE RED",
    "SPRITE X", "SPRITE Y", "TEXT BACKGROUND TYPE", "TEXT HEIGHT", "TEXT SIZE",
    "TERRAIN EXIST", "TEXT STYLE", "TEXT WIDTH", "TIMER", "WINDOW EXIST",
}

STRING_RETURNS = {"LIMB TEXTURE NAME"}

ANY_VALUE = {"PRINT", "DATA", "RESTORE"}
OUTPUT_ALL = {"READ"}
FILE_OUTPUT = {
    "READ FILE": "I!",
    "READ STRING": "S!",
    "READ FLOAT": "R!",
    "READ BYTE": "I!",
    "READ LONG": "I!",
    "READ WORD": "I!",
}

ARRAY_COLLECTIONS = {
    "ADD TO QUEUE", "ADD TO STACK", "ARRAY COUNT", "ARRAY DELETE ELEMENT",
    "ARRAY INDEX TO BOTTOM", "ARRAY INDEX TO QUEUE", "ARRAY INDEX TO STACK",
    "ARRAY INDEX TO TOP", "ARRAY INDEX VALID", "ARRAY INSERT AT BOTTOM",
    "ARRAY INSERT AT ELEMENT", "ARRAY INSERT AT TOP", "EMPTY ARRAY",
    "NEXT ARRAY INDEX", "PREVIOUS ARRAY INDEX", "REMOVE FROM QUEUE", "REMOVE FROM STACK",
}


def arities(minimum: int, maximum: int, mask: int) -> list[tuple[int, int, int]]:
    contiguous = mask == 0 or all(mask & (1 << count) for count in range(minimum, maximum + 1))
    if contiguous:
        return [(minimum, maximum, mask)]
    return [(count, count, 1 << count) for count in range(64) if mask & (1 << count)]


def parameter_spec(profile: str, name: str, maximum: int) -> str:
    if maximum == 0:
        return "-"
    exact = PROFILE_PARAMETER_SPECS.get((profile, name))
    if exact is not None:
        return exact
    if name in ANY_VALUE:
        return "A*"
    string_parameters = STRING_PARAMETERS.get(name, set()) | PROFILE_STRING_PARAMETERS.get((profile, name), set())
    values = ["S" if index in string_parameters else "N"
              for index in range(maximum)]
    if name in OUTPUT_ALL:
        values = ["A!" for _ in values]
    elif name == "INPUT":
        values = ["A" for _ in values]
        values[-1] = "A!"
    elif name in FILE_OUTPUT:
        values[-1] = FILE_OUTPUT[name]
    elif name in {"LOAD ARRAY", "SAVE ARRAY"}:
        values[0] = "S"
        values[1] = "A[]"
    elif name == "UNDIM":
        values = ["A[]" for _ in values]
    elif name in ARRAY_COLLECTIONS:
        values[0] = "A[]"
    elif name == "STR$":
        values[0] = "A"
    elif name == "PRINTC":
        values[0] = "A"
    return ",".join(values)


def return_type(kind: str, name: str) -> str:
    if kind == "Command":
        return "Void"
    if name in INTEGER_RETURNS:
        return "Integer"
    if name.endswith("$") or name in STRING_RETURNS:
        return "String"
    return "Real"


def generate(prefix: str) -> str:
    lines = [BEGIN, "// Generated by scripts/generate_language_signatures.py; do not edit by hand."]
    extension_signatures = defaultdict(list)
    if DBPRO_EXTENSION_EVIDENCE.is_file():
        with DBPRO_EXTENSION_EVIDENCE.open(newline="", encoding="utf-8") as source:
            for row in csv.DictReader((line for line in source if not line.startswith("#")), delimiter="\t"):
                extension_signatures[row["symbol_id"]].append(
                    (int(row["accepted_count"]), row["return_type"], row["parameters"])
                )
    for match in ENTRY.finditer(prefix):
        symbol, kind, name = match.group(1), match.group(2), match.group(3)
        if kind not in {"Command", "Function"}:
            continue
        if symbol in extension_signatures:
            for count, result, parameters in sorted(set(extension_signatures[symbol])):
                lines.append(
                    f'DBC_SIGNATURE({symbol}, DbPro, {count}, {count}, '
                    f'UINT64_C(0x{1 << count:016X}), {result}, "{parameters}")'
                )
            continue
        profiles = int(match.group(4))
        values = (
            ("Dbc", 1, int(match.group(5)), int(match.group(6)), int(match.group(7), 16)),
            ("DbPro", 2, int(match.group(8)), int(match.group(9)), int(match.group(10), 16)),
        )
        for profile, bit, minimum, maximum, mask in values:
            if profiles & bit == 0:
                continue
            for accepted_min, accepted_max, accepted_mask in arities(minimum, maximum, mask):
                spec = parameter_spec(profile, name, accepted_max)
                lines.append(
                    f'DBC_SIGNATURE({symbol}, {profile}, {accepted_min}, {accepted_max}, '
                    f'UINT64_C(0x{accepted_mask:016X}), {return_type(kind, name)}, "{spec}")'
                )
    lines.extend((END, ""))
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate explicit catalogue overload signatures")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    current = CATALOGUE.read_text(encoding="utf-8")
    prefix = current.split(BEGIN, 1)[0].rstrip() + "\n\n"
    generated = prefix + generate(prefix)
    if args.check:
        if current != generated:
            raise SystemExit("language catalogue overload signatures are stale")
        count = generated.count("DBC_SIGNATURE(")
        print(f"Language overload signatures are current: {count} explicit contracts")
        return
    CATALOGUE.write_text(generated, encoding="utf-8")
    print(f"Wrote {generated.count('DBC_SIGNATURE(')} explicit overload signatures")


if __name__ == "__main__":
    main()
