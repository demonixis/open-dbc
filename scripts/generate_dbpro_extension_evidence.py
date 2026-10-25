#!/usr/bin/env python3

import argparse
import csv
import hashlib
import re
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "src/language/language_catalogue.inc"
EVIDENCE = ROOT / "scripts/data/dbpro_extension_evidence.tsv"
BEGIN = "// BEGIN GENERATED DBPRO EXTENSION CATALOGUE"
END = "// END GENERATED DBPRO EXTENSION CATALOGUE"
SIGNATURE_BEGIN = "// BEGIN GENERATED OVERLOAD SIGNATURES"
DBPRO_COMMIT = "af6f19d7fc2919c356c683f7f5b8ec0c37a7d7d9"

SOURCES = {
    "ode-keywords": (
        "Install/Editor/Keywords/keywordsODE.ini",
        "bf74ba8e745258218f446c5d94083b43a5847512b582b3d7ed6e59203f83c1a1",
    ),
    "ode-exports": (
        "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK/Bullet/Script1.RC",
        "0e6eb1563e3f9489b98c77158b520080c3c027a0c42136a7960dac6fa11ca232",
    ),
    "cloth-keywords": (
        "Dark Basic Public Shared/Official Plugins/3D Cloth & Particles/Install/Keywords/Physics.ini",
        "d569e7290f90a68eaf8d177e66fa347d49bee5d2d9f81441851a4526a71c29b8",
    ),
    "cloth-exports": (
        "Dark Basic Public Shared/Official Plugins/3D Cloth & Particles/Code/DBProPhysicsMaster/DBProPhysicsMaster.rc",
        "9de8906f1ddbdc23b60240393775343a472e9d713e9405b04444b6d560219834",
    ),
    "darkphysics-exports": (
        "Dark Basic Public Shared/Official Plugins/DarkPHYSICS/DarkPHYSICS/DLL/DLL2.rc",
        "ba0bd72da090c4dabda6962e9dc5a2a8c56de603d9824489e41d63300711cea8",
    ),
    "dynamix-commands": (
        "Dark Basic Public Shared/Official Plugins/Dark Dynamix/Docs/Commands.ini",
        "4b542cd5d9829a9bb19b9bd5045acf4dadbdfaade84793977ba4bc1edcda5222",
    ),
    "dynamix-exports": (
        "Dark Basic Public Shared/Official Plugins/Dark Dynamix/Project files/Physics Dark Basic/Include/Physics.rc",
        "d6b210e93b715bc484e7b3a4d361fded0d23e10bb45875c57ea0d6f62e9a3852",
    ),
}

SURFACE_BITS = {"Ode": 16, "ClothParticles": 32, "DarkPhysics": 64, "DarkDynamix": 128}
SURFACE_PREFIXES = {"Ode": "ODE ", "DarkPhysics": "PHY ", "DarkDynamix": "DYN "}
TYPE_CODES = {"D": "I", "L": "I", "F": "R", "S": "S"}
DYNAMIX_ALIASES = {
    "DYN CLOTH ATTACH TO CORE": "DYN CLOTH MESH ATTACH TO CORE",
    "DYN EMITTER GET VELOCITY MAGNITUDE": "DYN EMITTER GET VELOCITY MAGNOTUDE",
    "DYN FLUID GET EXTERNAL ACCELERATION": "DYN FLUID GET EXTRNAL ACCELERATION",
    "DYN FLUID GET RANDOM ANGLE": "DYN EMITTER GET RANDOM ANGLE",
    "DYN FLUID SET RANDOM ANGLE": "DYN EMITTER SET RANDOM ANGLE",
    "DYN SOFT BODY GET FLAGS": "DYN SODT BODY GET FLAGS",
}
DYNAMIX_DOCUMENTED_ONLY = {
    "DYN ADD SHAPES": (("Function", "Integer", "I"),),
    "DYN GET PARAMETER": (("Function", "Real", "I"),),
    "DYN SET LIMB": (("Command", "Void", "I,I,I"),),
}


@dataclass(frozen=True, order=True)
class Overload:
    Surface: str
    SymbolId: str
    Kind: str
    Name: str
    AcceptedCount: int
    ReturnType: str
    Parameters: str
    CanonicalName: str
    Sources: str


def Require(Condition: bool, Message: str) -> None:
    if not Condition:
        raise SystemExit(Message)


def Sha256(PathValue: Path) -> str:
    return hashlib.sha256(PathValue.read_bytes()).hexdigest()


def NormalizeName(Value: str) -> str:
    return " ".join(Value.upper().split())


def IdentifierPart(Value: str) -> str:
    Words = re.findall(r"[A-Za-z0-9]+", Value)
    return "".join(Word[:1].upper() + Word[1:].lower() for Word in Words)


def SymbolIdFor(Surface: str, Kind: str, Name: str) -> str:
    Prefix = SURFACE_PREFIXES.get(Surface, "")
    LocalName = Name[len(Prefix):] if Prefix and Name.startswith(Prefix) else Name
    SurfacePart = {"Ode": "Ode", "ClothParticles": "Cloth", "DarkPhysics": "Phy", "DarkDynamix": "Dyn"}[Surface]
    return f"{Kind}_Ext{SurfacePart}{IdentifierPart(LocalName)}"


def ParseKeywords(PathValue: Path) -> set[str]:
    Result = set()
    for Line in PathValue.read_text(errors="replace").splitlines():
        Value = Line.strip()
        if not Value or Value.startswith(("[", ";", "#", "//")) or "==" not in Value:
            continue
        Result.add(NormalizeName(Value.split("==", 1)[0]))
    return Result


def ParseExports(PathValue: Path) -> dict[str, set[tuple[str, str, str]]]:
    Result = defaultdict(set)
    for Line in PathValue.read_text(errors="replace").splitlines():
        Value = Line.strip()
        if Value.startswith("//") or "IDS_STRING" not in Value:
            continue
        Match = re.search(r'"([^"\\]*(?:\\.[^"\\]*)*)"', Value)
        if Match is None or "%" not in Match.group(1):
            continue
        Payload = Match.group(1)
        RawName, TypeCode, *_ = Payload.split("%")
        IsFunction = RawName.strip().endswith("[")
        Name = NormalizeName(RawName.strip().rstrip("["))
        if TypeCode == "0":
            TypeCode = ""
        if IsFunction:
            Require(bool(TypeCode), f"Missing return type in export: {Payload}")
            ReturnType = {"D": "Integer", "L": "Integer", "F": "Real", "S": "String"}[TypeCode[0]]
            TypeCode = TypeCode[1:]
            Kind = "Function"
        else:
            ReturnType = "Void"
            Kind = "Command"
        Require(all(Code in TYPE_CODES for Code in TypeCode), f"Unknown DBPro type code in export: {Payload}")
        Parameters = ",".join(TYPE_CODES[Code] for Code in TypeCode) or "-"
        Result[Name].add((Kind, ReturnType, Parameters))
    return Result


def AddRows(Result: list[Overload], Surface: str, Name: str, Signatures: set[tuple[str, str, str]],
            CanonicalName: str, Sources: set[str]) -> None:
    for Kind, ReturnType, Parameters in sorted(Signatures):
        Count = 0 if Parameters == "-" else len(Parameters.split(","))
        Result.append(Overload(Surface, SymbolIdFor(Surface, Kind, Name), Kind, Name, Count, ReturnType,
                               Parameters, CanonicalName, ",".join(sorted(Sources))))


def BuildEvidence(DbProRoot: Path) -> list[Overload]:
    Commit = subprocess.check_output(["git", "-C", str(DbProRoot), "rev-parse", "HEAD"], text=True).strip()
    Require(Commit == DBPRO_COMMIT, f"Expected DBPro {DBPRO_COMMIT}, found {Commit}")
    for Label, (RelativePath, ExpectedHash) in SOURCES.items():
        PathValue = DbProRoot / RelativePath
        Require(PathValue.is_file(), f"Missing DBPro extension evidence: {Label}: {PathValue}")
        Require(Sha256(PathValue) == ExpectedHash, f"DBPro extension evidence hash mismatch: {Label}")

    Result: list[Overload] = []
    OdeKeywords = ParseKeywords(DbProRoot / SOURCES["ode-keywords"][0])
    OdeExports = ParseExports(DbProRoot / SOURCES["ode-exports"][0])
    OdeNames = {Name for Name in OdeExports if Name.startswith("ODE ")}
    Require(OdeKeywords <= OdeNames, "ODE keyword inventory contains names absent from its export table")
    for Name in sorted(OdeNames):
        AddRows(Result, "Ode", Name, OdeExports[Name], Name,
                {"ode-exports"} | ({"ode-keywords"} if Name in OdeKeywords else set()))

    ClothKeywords = ParseKeywords(DbProRoot / SOURCES["cloth-keywords"][0])
    ClothExports = ParseExports(DbProRoot / SOURCES["cloth-exports"][0])
    Require(ClothKeywords <= set(ClothExports), "Cloth keyword inventory contains names absent from its export table")
    for Name in sorted(ClothExports):
        AddRows(Result, "ClothParticles", Name, ClothExports[Name], Name,
                {"cloth-exports"} | ({"cloth-keywords"} if Name in ClothKeywords else set()))

    PhysicsExports = ParseExports(DbProRoot / SOURCES["darkphysics-exports"][0])
    for Name in sorted(PhysicsExports):
        AddRows(Result, "DarkPhysics", Name, PhysicsExports[Name], Name, {"darkphysics-exports"})

    DynamixCommands = ParseKeywords(DbProRoot / SOURCES["dynamix-commands"][0])
    DynamixExports = ParseExports(DbProRoot / SOURCES["dynamix-exports"][0])
    ReverseAliases = {Value: Key for Key, Value in DYNAMIX_ALIASES.items()}
    for Name in sorted(DynamixCommands | set(DynamixExports)):
        SourceName = DYNAMIX_ALIASES.get(Name, Name)
        Signatures = set(DynamixExports.get(SourceName, set()))
        if Name in DYNAMIX_DOCUMENTED_ONLY:
            Signatures.update(DYNAMIX_DOCUMENTED_ONLY[Name])
        Require(bool(Signatures), f"No typed signature evidence for Dark Dynamix command: {Name}")
        CanonicalName = ReverseAliases.get(Name, Name)
        Sources = ({"dynamix-commands"} if Name in DynamixCommands else set()) | \
                  ({"dynamix-exports"} if Name in DynamixExports or SourceName in DynamixExports else set())
        AddRows(Result, "DarkDynamix", Name, Signatures, CanonicalName, Sources)

    Counts = defaultdict(set)
    for Row in Result:
        Counts[Row.Surface].add(Row.Name)
    Require({Surface: len(Names) for Surface, Names in Counts.items()} == {
        "Ode": 121, "ClothParticles": 157, "DarkPhysics": 491, "DarkDynamix": 435,
    }, "DBPro extension public spelling totals drifted")
    Require(len(set().union(*Counts.values())) == 1204, "DBPro extension public spelling union drifted")
    Require(len({(Row.SymbolId, Row.AcceptedCount, Row.ReturnType, Row.Parameters) for Row in Result}) == len(Result),
            "Duplicate DBPro extension overload evidence")
    return sorted(Result)


def EvidenceText(Rows: list[Overload]) -> str:
    Lines = [f"# DBPro commit\t{DBPRO_COMMIT}"]
    for Label, (RelativePath, ExpectedHash) in SOURCES.items():
        Lines.append(f"# {Label}\t{RelativePath}\t{ExpectedHash}")
    Lines.append("surface\tsymbol_id\tkind\tname\taccepted_count\treturn_type\tparameters\tcanonical_name\tsources")
    for Row in Rows:
        Lines.append("\t".join((Row.Surface, Row.SymbolId, Row.Kind, Row.Name, str(Row.AcceptedCount),
                                Row.ReturnType, Row.Parameters, Row.CanonicalName, Row.Sources)))
    return "\n".join(Lines) + "\n"


def ReadEvidence() -> list[Overload]:
    Require(EVIDENCE.is_file(), f"Missing DBPro extension evidence snapshot: {EVIDENCE}")
    with EVIDENCE.open(newline="", encoding="utf-8") as Input:
        Rows = [Overload(Row["surface"], Row["symbol_id"], Row["kind"], Row["name"],
                         int(Row["accepted_count"]), Row["return_type"], Row["parameters"],
                         Row["canonical_name"], Row["sources"])
                for Row in csv.DictReader((Line for Line in Input if not Line.startswith("#")), delimiter="\t")]
    Require(bool(Rows), "DBPro extension evidence snapshot is empty")
    return Rows


def CatalogueRegion(Rows: list[Overload], ImplementedProfiles: dict[str, int]) -> str:
    Groups = defaultdict(list)
    for Row in Rows:
        Groups[(Row.Surface, Row.SymbolId, Row.Kind, Row.Name)].append(Row)
    Lines = [BEGIN, "// Generated by scripts/generate_dbpro_extension_evidence.py; do not edit by hand."]
    SeenIds = set()
    for (Surface, SymbolId, Kind, Name), Overloads in sorted(Groups.items()):
        Require(SymbolId not in SeenIds, f"Extension SymbolId collision: {SymbolId}")
        SeenIds.add(SymbolId)
        Counts = sorted({Row.AcceptedCount for Row in Overloads})
        Mask = sum(1 << Count for Count in Counts)
        Implemented = ImplementedProfiles.get(SymbolId, 0)
        Require(Implemented in (0, 2), f"Extension implementation mask must be DbPro-only: {SymbolId}")
        Lines.append(
            f'DBC_CATALOGUE({SymbolId}, {Kind}, "{Name}", 2, 0, 0, UINT64_C(0x0000000000000000), '
            f'{Counts[0]}, {Counts[-1]}, UINT64_C(0x{Mask:016X}), {Implemented}, {SURFACE_BITS[Surface]})'
        )
    Lines.extend((END, ""))
    return "\n".join(Lines)


def UpdateCatalogue(Rows: list[Overload], Check: bool) -> None:
    Current = CATALOGUE.read_text(encoding="utf-8")
    ImplementedProfiles = {}
    for Match in re.finditer(r"DBC_CATALOGUE\(([^,]+),.*?, ([0-9]+), ([0-9]+)\)$", Current, re.MULTILINE):
        if int(Match.group(3)) & 0xF0:
            ImplementedProfiles[Match.group(1)] = int(Match.group(2))
    Region = CatalogueRegion(Rows, ImplementedProfiles).rstrip()
    if BEGIN in Current:
        Prefix, Remainder = Current.split(BEGIN, 1)
        Require(END in Remainder, "Unterminated generated DBPro extension catalogue region")
        _, Suffix = Remainder.split(END, 1)
        Generated = Prefix.rstrip() + "\n\n" + Region + "\n\n" + Suffix.lstrip()
    else:
        Require(SIGNATURE_BEGIN in Current, "Missing generated signature marker in language catalogue")
        Prefix, Suffix = Current.split(SIGNATURE_BEGIN, 1)
        Generated = Prefix.rstrip() + "\n\n" + Region + "\n\n" + SIGNATURE_BEGIN + Suffix
    if Check:
        Require(Current == Generated, "DBPro extension catalogue region is stale")
    else:
        CATALOGUE.write_text(Generated, encoding="utf-8")


def Main() -> None:
    Parser = argparse.ArgumentParser(description="Generate pinned DBPro extension evidence and catalogue entries")
    Parser.add_argument("--dbpro-root", type=Path)
    Parser.add_argument("--refresh-evidence", action="store_true")
    Parser.add_argument("--check", action="store_true")
    Args = Parser.parse_args()
    if Args.refresh_evidence:
        Require(Args.dbpro_root is not None, "--refresh-evidence requires --dbpro-root")
        Generated = EvidenceText(BuildEvidence(Args.dbpro_root.resolve()))
        if Args.check:
            Require(EVIDENCE.is_file() and EVIDENCE.read_text(encoding="utf-8") == Generated,
                    "DBPro extension evidence snapshot is stale")
        else:
            EVIDENCE.write_text(Generated, encoding="utf-8")
    elif Args.dbpro_root is not None:
        Generated = EvidenceText(BuildEvidence(Args.dbpro_root.resolve()))
        Require(EVIDENCE.is_file() and EVIDENCE.read_text(encoding="utf-8") == Generated,
                "DBPro extension evidence snapshot differs from pinned source")
    Rows = ReadEvidence()
    UpdateCatalogue(Rows, Args.check)
    print(f"DBPro extension evidence is current: {len({Row.Name for Row in Rows})} spellings, "
          f"{len(Rows)} typed overloads")


if __name__ == "__main__":
    Main()
