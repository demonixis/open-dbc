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
}

# The forms the free DBPro release exports from these folders' string tables and its editor keyword files leave
# out: later core forms and the DarkLIGHTS and Enhancement Pack plugins.
SDK_ROOTS = (
    "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDK",
    "Dark Basic Public Shared/Dark Basic Pro SDK/DarkSDKMore",
    "Dark Basic Public Shared/Official Plugins/DarkLIGHTS",
    "Dark Basic Public Shared/Official Plugins/Enhancement Pack",
)
SDK_SOURCE_BASE = "Dark Basic Public Shared"
# The forms of DBPro's editor keyword files, which the hand-written catalogue rows carry.
CORE_EVIDENCE = ROOT / "scripts/data/dbpro_catalogue_evidence.tsv"
# Plugins left out of the catalogue, whose forms stop with DBC2049 (RemovedPluginOf).
REMOVED_PLUGIN_ROOTS = (
    "Dark Basic Public Shared/Official Plugins/3D Cloth & Particles",
    "Dark Basic Public Shared/Official Plugins/Dark Dynamix",
    "Dark Basic Public Shared/Official Plugins/DarkPHYSICS",
)
CLOTH_FORMS = ROOT / "src/language/removed_cloth_forms.inc"

SURFACE_BITS = {"Ode": 16, "Sdk": 2}
SURFACE_PREFIXES = {"Ode": "ODE "}
TYPE_CODES = {"D": "I", "L": "I", "R": "I", "F": "R", "S": "S"}
RETURN_TYPES = {"D": "Integer", "L": "Integer", "R": "Integer", "F": "Real", "S": "String"}
CATALOGUE_ROW = re.compile(r'DBC_CATALOGUE\(([^,]+), ([^,]+), "([^"]+)", ([0-9]+), .*, ([0-9]+), ([0-9]+)\)$',
                           re.MULTILINE)


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
    if Surface == "Sdk":
        return f"{Kind}_{IdentifierPart(Name)}"
    Prefix = SURFACE_PREFIXES.get(Surface, "")
    LocalName = Name[len(Prefix):] if Prefix and Name.startswith(Prefix) else Name
    SurfacePart = {"Ode": "Ode"}[Surface]
    return f"{Kind}_Ext{SurfacePart}{IdentifierPart(LocalName)}"


def ParseKeywords(PathValue: Path) -> set[str]:
    Result = set()
    for Line in PathValue.read_text(errors="replace").splitlines():
        Value = Line.strip()
        if not Value or Value.startswith(("[", ";", "#", "//")) or "==" not in Value:
            continue
        Result.add(NormalizeName(Value.split("==", 1)[0]))
    return Result


def ReadResource(PathValue: Path) -> str:
    Raw = PathValue.read_bytes()
    return Raw.decode("utf-16") if Raw[:2] in (b"\xff\xfe", b"\xfe\xff") else Raw.decode("latin-1")


# Each exported name with its kind and type codes, the first one of a function being its return type.
def ParseExports(PathValue: Path) -> dict[str, set[tuple[str, str]]]:
    Result = defaultdict(set)
    for Line in ReadResource(PathValue).splitlines():
        Value = Line.strip()
        if re.match(r'(?:IDS_STRING\w*|[0-9]+)\s+"', Value) is None:
            continue
        Match = re.search(r'"([^"\\]*(?:\\.[^"\\]*)*)"', Value)
        if Match is None or "%" not in Match.group(1):
            continue
        Payload = Match.group(1)
        RawName, TypeCode, *_ = Payload.split("%")
        IsFunction = RawName.strip().endswith("[")
        Name = NormalizeName(RawName.strip().rstrip("["))
        if Name:
            Result[Name].add(("Function" if IsFunction else "Command", "" if TypeCode == "0" else TypeCode))
    return Result


def TypedSignature(Name: str, Kind: str, TypeCode: str) -> tuple[str, str, str]:
    ReturnType = "Void"
    if Kind == "Function":
        Require(bool(TypeCode) and TypeCode[0] in RETURN_TYPES, f"Missing return type in export: {Name}")
        ReturnType = RETURN_TYPES[TypeCode[0]]
        TypeCode = TypeCode[1:]
    Require(all(Code in TYPE_CODES for Code in TypeCode), f"Unknown DBPro type code in export: {Name} {TypeCode}")
    return Kind, ReturnType, ",".join(TYPE_CODES[Code] for Code in TypeCode) or "-"


# The plugin RemovedPluginOf names for a form, as language_catalogue.cpp decides it.
def RemovedPlugin(Name: str, ClothForms: set[str]) -> str:
    if Name.startswith("PHY ") or Name == "PHYSX":
        return "DarkPHYSICS"
    if Name.startswith("DYN "):
        return "Dark Dynamix"
    Words = Name.split(" ")
    if any(" ".join(Words[:Count]) in ClothForms for Count in range(1, len(Words) + 1)):
        return "3D Cloth & Particles"
    return ""


def ResourceFiles(DbProRoot: Path, Folder: str) -> list[Path]:
    Root = DbProRoot / Folder
    Require(Root.is_dir(), f"Missing DBPro folder: {Root}")
    return sorted(PathValue for PathValue in Root.rglob("*") if PathValue.suffix.lower() == ".rc")


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
        AddRows(Result, "Ode", Name, {TypedSignature(Name, *Signature) for Signature in OdeExports[Name]}, Name,
                {"ode-exports"} | ({"ode-keywords"} if Name in OdeKeywords else set()))

    with CORE_EVIDENCE.open(newline="", encoding="utf-8") as Input:
        # Its comments start with "# "; #CONSTANT and #INCLUDE are rows.
        CoreNames = {Row["name"] for Row in
                     csv.DictReader((Line for Line in Input if not Line.startswith("# ")), delimiter="\t")}
    SdkExports = defaultdict(lambda: defaultdict(set))
    for Folder in SDK_ROOTS:
        for PathValue in ResourceFiles(DbProRoot, Folder):
            Source = PathValue.relative_to(DbProRoot / SDK_SOURCE_BASE).as_posix()
            for Name, Signatures in ParseExports(PathValue).items():
                # DBDLLCore.rc also exports FREE STRING$ without its $.
                if Name in CoreNames or Name + "$" in CoreNames or Name.startswith("ODE "):
                    continue
                for Signature in Signatures:
                    SdkExports[Name][Signature].add(Source)
    for Name, Signatures in sorted(SdkExports.items()):
        AddRows(Result, "Sdk", Name, {TypedSignature(Name, *Signature) for Signature in Signatures}, Name,
                set().union(*Signatures.values()))

    ClothForms = set(re.findall(r'^"([^"]+)",$', CLOTH_FORMS.read_text(encoding="utf-8"), re.MULTILINE))
    for Folder in REMOVED_PLUGIN_ROOTS:
        for PathValue in ResourceFiles(DbProRoot, Folder):
            for Name in ParseExports(PathValue):
                Require(bool(RemovedPlugin(Name, ClothForms)),
                        f"{Name} of {PathValue.relative_to(DbProRoot)} is neither catalogued nor named by DBC2049")

    Counts = defaultdict(set)
    for Row in Result:
        Counts[Row.Surface].add(Row.Name)
    Require({Surface: len(Names) for Surface, Names in Counts.items()} == {"Ode": 121, "Sdk": 485},
            "DBPro extension public spelling totals drifted")
    Require(len({(Row.SymbolId, Row.AcceptedCount, Row.ReturnType, Row.Parameters) for Row in Result}) == len(Result),
            "Duplicate DBPro extension overload evidence")
    return sorted(Result)


def EvidenceText(Rows: list[Overload]) -> str:
    Lines = [f"# DBPro commit\t{DBPRO_COMMIT}"]
    for Label, (RelativePath, ExpectedHash) in SOURCES.items():
        Lines.append(f"# {Label}\t{RelativePath}\t{ExpectedHash}")
    for Folder in SDK_ROOTS:
        Lines.append(f"# sdk-exports\t{Folder}")
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


# Hand-written rows keep their own entries: a form they already carry in DBPro is not generated again, and one
# carried in Classic only under the same SymbolId has to be widened to DBPro by hand.
def CatalogueRegion(Rows: list[Overload], ImplementedProfiles: dict[str, int], HandWritten: str) -> str:
    WrittenIds = {}
    DbProSpellings = set()
    for Match in CATALOGUE_ROW.finditer(HandWritten):
        WrittenIds[Match.group(1)] = (Match.group(2), Match.group(3), int(Match.group(4)))
        if int(Match.group(4)) & 2:
            DbProSpellings.add((Match.group(2), Match.group(3)))
    Groups = defaultdict(list)
    for Row in Rows:
        Groups[(Row.Surface, Row.SymbolId, Row.Kind, Row.Name)].append(Row)
    Lines = [BEGIN, "// Generated by scripts/generate_dbpro_extension_evidence.py; do not edit by hand."]
    SeenIds = set()
    for (Surface, SymbolId, Kind, Name), Overloads in sorted(Groups.items()):
        Require(SymbolId not in SeenIds, f"Extension SymbolId collision: {SymbolId}")
        SeenIds.add(SymbolId)
        if SymbolId in WrittenIds:
            Require(WrittenIds[SymbolId] == (Kind, Name, 3), f"Widen {SymbolId} to DBPro or rename the new form")
            continue
        if (Kind, Name) in DbProSpellings:
            continue
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
    if BEGIN in Current:
        Prefix, Remainder = Current.split(BEGIN, 1)
        Require(END in Remainder, "Unterminated generated DBPro extension catalogue region")
        Previous, Suffix = Remainder.split(END, 1)
        Suffix = Suffix.lstrip()
    else:
        Require(SIGNATURE_BEGIN in Current, "Missing generated signature marker in language catalogue")
        Prefix, Suffix = Current.split(SIGNATURE_BEGIN, 1)
        Previous, Suffix = "", SIGNATURE_BEGIN + Suffix
    ImplementedProfiles = {Match.group(1): int(Match.group(5)) for Match in CATALOGUE_ROW.finditer(Previous)}
    Region = CatalogueRegion(Rows, ImplementedProfiles, Prefix).rstrip()
    Generated = Prefix.rstrip() + "\n\n" + Region + "\n\n" + Suffix
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
