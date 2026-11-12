#!/usr/bin/env python3
"""Snapshots of what the free DBPro release documents and what its examples call.

The help pages and the official examples are not redistributed: only the command names
they hold are kept, in scripts/data. The catalogue audit reads them to tell a referenced
command from one that exists nowhere.
"""

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HELP_EVIDENCE = ROOT / "scripts/data/dbpro_help_evidence.tsv"
EXAMPLE_EVIDENCE = ROOT / "scripts/data/dbpro_example_evidence.tsv"
DBPRO_COMMIT = "af6f19d7fc2919c356c683f7f5b8ec0c37a7d7d9"
HELP_COMMANDS = "Install/Help/commands"
KEYWORDS = "Install/Editor/Keywords"
EXAMPLE_ROOTS = (
    "Install/Help/examples",
    "Install/Projects",
    "Dark Basic Public Shared/Official Plugins",
)
# The help writes the directive pages four ways: `NAME`, `#NAME`, `%23NAME` and `_NAME`.
DIRECTIVE_PAGES = {"CONSTANT", "INCLUDE"}
WORD = re.compile(r"[A-Z0-9_$#%]+")


def Normalise(Name: str) -> str:
    return " ".join(Name.upper().split())


def PageName(Stem: str) -> str:
    Name = Normalise(Stem.replace("%23", "#"))
    if Name.lstrip("_") in DIRECTIVE_PAGES:
        Name = "#" + Name.lstrip("_")
    return Name


def BuildHelp(DbProRoot: Path) -> dict[str, tuple[set[str], list[str]]]:
    """Command name -> (families, pages). A family is a folder of the help; '-' is its top level."""
    Commands = DbProRoot / HELP_COMMANDS
    Families = {Entry.name.lower() for Entry in Commands.iterdir() if Entry.is_dir()}
    Result: dict[str, tuple[set[str], list[str]]] = {}
    for Page in sorted(Commands.rglob("*.htm")):
        Relative = Page.relative_to(Commands)
        TopLevel = len(Relative.parts) == 1
        # A top-level page named after a folder lists a family instead of a command.
        if TopLevel and Page.stem.lower() in Families:
            continue
        Name = PageName(Page.stem)
        Family = "-" if TopLevel else Relative.parts[0].lower()
        Families_, Pages = Result.setdefault(Name, (set(), []))
        Families_.add(Family)
        Pages.append(Relative.as_posix())
    return Result


def KeywordNames(DbProRoot: Path) -> set[str]:
    Names = set()
    for Path_ in sorted((DbProRoot / KEYWORDS).glob("*.ini")):
        for Line in Path_.read_text(errors="replace").splitlines():
            if not Line or Line.startswith(("[", ";")) or "=" not in Line:
                continue
            Name = Normalise(Line.partition("=")[0])
            if Name:
                Names.add(Name)
    return Names


def CodeLines(Text: str) -> list[str]:
    """Upper-case statements of a BASIC source, without strings and comments."""
    Lines = []
    InBlock = False
    for Raw in Text.splitlines():
        Line = re.sub(r'"[^"]*"?', " ", Raw).upper()
        Stripped = Line.strip()
        if InBlock:
            InBlock = not Stripped.startswith("REMEND")
            continue
        if Stripped.startswith("REMSTART"):
            InBlock = True
            continue
        Line = re.split(r"`|//", Line, maxsplit=1)[0]
        for Statement in Line.split(":"):
            Statement = Statement.strip()
            if Statement and not re.match(r"REM(\s|$)", Statement):
                Lines.append(Statement)
    return Lines


def BuildExamples(DbProRoot: Path, Vocabulary: set[str]) -> dict[str, list[str]]:
    """Command name -> the official example sources that call it, by the longest name at each word."""
    Names = {tuple(Name.split()) for Name in Vocabulary}
    Longest = max(len(Name) for Name in Names)
    Result: dict[str, list[str]] = {}
    for ExampleRoot in EXAMPLE_ROOTS:
        Sources = sorted(Path_ for Path_ in (DbProRoot / ExampleRoot).rglob("*")
                         if Path_.is_file() and Path_.suffix.lower() == ".dba")
        for Source in Sources:
            Relative = Source.relative_to(DbProRoot).as_posix()
            Used = set()
            for Statement in CodeLines(Source.read_text(errors="replace")):
                Words = WORD.findall(Statement)
                Index = 0
                while Index < len(Words):
                    for Count in range(min(Longest, len(Words) - Index), 0, -1):
                        Candidate = tuple(Words[Index:Index + Count])
                        if Candidate in Names:
                            Used.add(" ".join(Candidate))
                            Index += Count - 1
                            break
                    Index += 1
            for Name in Used:
                Result.setdefault(Name, []).append(Relative)
    return Result


def HelpText(Help: dict[str, tuple[set[str], list[str]]]) -> str:
    Lines = [f"# DBPro commit\t{DBPRO_COMMIT}", f"# pages\t{HELP_COMMANDS}", "name\tfamilies\tpages"]
    Lines.extend(f"{Name}\t{','.join(sorted(Families))}\t{','.join(Pages)}"
                 for Name, (Families, Pages) in sorted(Help.items()))
    return "\n".join(Lines) + "\n"


def ExampleText(Examples: dict[str, list[str]]) -> str:
    Lines = [f"# DBPro commit\t{DBPRO_COMMIT}"]
    Lines.extend(f"# sources\t{ExampleRoot}" for ExampleRoot in EXAMPLE_ROOTS)
    Lines.append("name\tsources")
    Lines.extend(f"{Name}\t{','.join(Sources)}" for Name, Sources in sorted(Examples.items()))
    return "\n".join(Lines) + "\n"


def Generate(DbProRoot: Path) -> tuple[str, str]:
    Commit = subprocess.check_output(["git", "-C", str(DbProRoot), "rev-parse", "HEAD"], text=True).strip()
    if Commit != DBPRO_COMMIT:
        raise SystemExit(f"Expected DBPro {DBPRO_COMMIT}, found {Commit}")
    Help = BuildHelp(DbProRoot)
    Examples = BuildExamples(DbProRoot, set(Help) | KeywordNames(DbProRoot))
    return HelpText(Help), ExampleText(Examples)


def ReadNames(Path_: Path) -> dict[str, list[str]]:
    """Name -> the rest of its row, for either snapshot."""
    Rows = [Line.split("\t") for Line in Path_.read_text(encoding="utf-8").splitlines()
            if Line and not Line.startswith("#")]
    if not Rows or Rows[0][0] != "name" or len(Rows) == 1:
        raise SystemExit(f"Malformed DBPro reference evidence: {Path_.name}")
    return {Row[0]: Row[1:] for Row in Rows[1:]}


def main() -> None:
    Parser = argparse.ArgumentParser(description="Regenerate the DBPro help and example snapshots")
    Parser.add_argument("--dbpro-root", type=Path, required=True, help="pinned Dark-Basic-Pro checkout")
    Parser.add_argument("--check", action="store_true")
    Args = Parser.parse_args()
    Generated = Generate(Args.dbpro_root.resolve())
    for Path_, Text in zip((HELP_EVIDENCE, EXAMPLE_EVIDENCE), Generated):
        if Args.check:
            if not Path_.is_file() or Path_.read_text(encoding="utf-8") != Text:
                raise SystemExit(f"{Path_.name} differs from the pinned DBPro checkout")
        else:
            Path_.write_text(Text, encoding="utf-8")
    print("DBPro reference evidence is current" if Args.check else "Regenerated DBPro reference evidence")


if __name__ == "__main__":
    main()
