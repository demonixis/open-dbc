#!/usr/bin/env python3

import argparse
from pathlib import Path

from audit_language_catalogue import ENTRY


ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "src/language/language_catalogue.inc"
DEFAULT_OUTPUT = ROOT / "tests/unit/unsupported_compatibility_manifest.tsv"


def is_video(name: str) -> bool:
    return ("ANIMATION" in name and "OBJECT ANIMATION" not in name) or "DVD" in name


def family(name: str) -> str:
    if name.startswith(("ODE ", "PHY ", "DYN ")):
        return "physics"
    if "FTP" in name:
        return "ftp"
    if is_video(name):
        return "video"
    if "JOYSTICK" in name:
        return "joystick"
    if any(token in name for token in ("KEY", "MOUSE", "ENTRY$", "ENTRY BUFFER")):
        return "input"
    if any(token in name for token in ("FILE", "DIR", "PATH", "CHECKLIST")):
        return "file"
    if any(token in name for token in ("DISPLAY", "SCREEN", "WINDOW", "BITMAP")):
        return "display"
    if any(token in name for token in ("TEXT", "FONT", "STRING", "STR$")):
        return "text"
    if any(token in name for token in ("OBJECT", "LIMB", "MESH", "CAMERA", "LIGHT", "BSP")):
        return "3d"
    if any(token in name for token in ("SPRITE", "DOT", "LINE", "BOX", "CIRCLE", "ELLIPSE")):
        return "2d"
    if any(token in name for token in ("SOUND", "MUSIC", "CD")):
        return "audio"
    if any(token in name for token in ("MEMBLOCK", "MEMORY")):
        return "memory"
    return "core"


def disposition(name: str) -> tuple[str, str]:
    if "FTP" in name:
        return "Never", "none"
    if is_video(name):
        return "Deferred", "media-backend"
    if "FILEBLOCK" in name or name.startswith(("READ FILE", "WRITE FILE")):
        return "Study", "exact-file-stream-semantics"
    if "DISPLAY" in name or name in {"SCREEN DEPTH", "SCREEN TYPE"}:
        return "Study", "platform-display-mode-matrix"
    if "ENTRY$" in name or "ENTRY BUFFER" in name:
        return "Study", "text-input-lifecycle"
    if any(token in name for token in ("SYSTEMKEY", "CONTROLKEY", "SHIFTKEY", "ESCAPEKEY", "KEYSTATE")):
        return "Study", "cross-platform-key-map"
    if "JOYSTICK" in name:
        return "Study", "historical-controller-model"
    if name in {"DOT", "LINE", "BOX", "CIRCLE", "ELLIPSE"}:
        return "Study", "pixel-render-oracle"
    if name.startswith("LOAD "):
        return "Study", "typed-load-errors"
    return "Target", "engine-subsystem-and-oracle"


def evidence(mask: int) -> str:
    names = []
    if mask & 1:
        names.append("dbc-help")
    if mask & 2:
        names.append("dbpro-reference")
    if mask & 4:
        names.append("opendbc-extension")
    if mask & 8:
        names.append("legacy-unverified")
    if mask & 16:
        names.append("ode-extension")
    if mask & 32:
        names.append("cloth-particles-extension")
    if mask & 64:
        names.append("darkphysics-extension")
    if mask & 128:
        names.append("dark-dynamix-extension")
    return ",".join(names) or "none"


def generate() -> str:
    rows = ["profile\tsymbol_id\tkind\tname\tdisposition\tfamily\tproof\tprerequisite"]
    seen = set()
    for match in ENTRY.finditer(CATALOGUE.read_text(encoding="utf-8")):
        symbol_id, kind, name = match[1], match[2], match[3]
        available = int(match[4])
        implemented = int(match[11])
        proof = evidence(int(match[12]))
        extension = bool(int(match[12]) & 0xF0)
        for profile, bit in (("dbc", 1), ("dbpro", 2)):
            if not available & bit or implemented & bit:
                continue
            if profile == "dbc":
                continue
            key = (profile, symbol_id)
            if key in seen:
                raise SystemExit(f"duplicate unsupported compatibility key: {profile}/{symbol_id}")
            seen.add(key)
            status, prerequisite = (("Target", "jolt-adapter-and-oracle") if extension else disposition(name))
            symbol_family = "physics" if extension else family(name)
            if status == "Never" and "FTP" not in name:
                raise SystemExit(f"only FTP may be classified Never: {name}")
            if status == "Deferred" and not is_video(name):
                raise SystemExit(f"only video may be classified Deferred: {name}")
            rows.append("\t".join((profile, symbol_id, kind, name, status, symbol_family, proof, prerequisite)))
    return "\n".join(rows) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Regenerate the unsupported BASIC compatibility inventory")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated = generate()
    count = generated.count("\n") - 1
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != generated:
            raise SystemExit(f"unsupported compatibility manifest is stale: regenerate {args.output}")
        print(f"Unsupported compatibility manifest is current: {count} profile/symbol pairs")
        return
    args.output.write_text(generated, encoding="utf-8")
    print(f"Wrote {count} unsupported profile/symbol classifications to {args.output}")


if __name__ == "__main__":
    main()
