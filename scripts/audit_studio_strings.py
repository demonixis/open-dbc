#!/usr/bin/env python3
"""Find Studio interface text that a language cannot reach.

Reports, for the C++ sources of tools/studio/src:
- a literal shown by ImGui, or given to a helper that translates its label,
  without going through Tr, TrC, TrId or TrFmt;
- a literal that reads as a sentence and is neither translated nor listed in
  scripts/data/studio_verbatim_strings.txt, wherever it is: messages travel
  through variables on their way to the screen;
- a printf-style format that is translated, which a translation could break;
- a Tr* key absent from tools/studio/locales/fr.json, and a catalogue key that
  no source mentions any more.

Without --check this is a report; with it, any finding fails.
"""

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "tools/studio/src"
CATALOGUE = ROOT / "tools/studio/locales/fr.json"
VERBATIM = ROOT / "scripts/data/studio_verbatim_strings.txt"

TRANSLATORS = {"Tr", "TrC", "TrId", "TrFmt"}

# ImGui functions and the positions of the arguments they display.
LABELS = {
    "Begin": (0,),
    "BeginCombo": (0, 1),
    "BeginMenu": (0,),
    "BeginPopupModal": (0,),
    "BeginTabItem": (0,),
    "Button": (0,),
    "Checkbox": (0,),
    "CollapsingHeader": (0,),
    "ColorEdit3": (0,),
    "ColorEdit4": (0,),
    "Combo": (0, 2),
    "DragFloat": (0,),
    "DragFloat2": (0,),
    "DragFloat3": (0,),
    "DragInt": (0,),
    "DragInt2": (0,),
    "DragIntRange2": (0,),
    "InputInt": (0,),
    "InputInt2": (0,),
    "InputInt3": (0,),
    "InputText": (0,),
    "InputTextMultiline": (0,),
    "InputTextWithHint": (0, 1),
    "MenuItem": (0,),
    "ProgressBar": (2,),
    "RadioButton": (0,),
    "Selectable": (0,),
    "SeparatorText": (0,),
    "SliderInt": (0,),
    "SmallButton": (0,),
    "TableSetupColumn": (0,),
    "TextUnformatted": (0,),
    "TreeNode": (0,),
    "TreeNodeEx": (0,),
}

# ImGui functions with a printf format: every other argument is displayed too.
FORMATS = {"BulletText": 0, "SetTooltip": 0, "Text": 0, "TextColored": 1, "TextDisabled": 0, "TextWrapped": 0}

# Studio helpers that translate these arguments themselves. Their literals
# must be in the catalogue, since no Tr* call around them says so.
HELPERS = {
    "CheckboxList": (0,),
    "LabelledValue": (0,),
    "ModeButton": (0, 2, 3),
    "TextField": (0,),
    "ToolbarButton": (3,),
}

# Reaches the screen under a name a sentence heuristic cannot see.
SHORT_MESSAGE_SINKS = {"Status_"}

# These hold BASIC, HLSL or messages that only reach the log, never the interface.
NO_INTERFACE_TEXT = {
    "core/builtin_snippets.cpp",
    "core/locales.cpp",
    "core/settings.cpp",
    "scene/scene_codegen.cpp",
    "toolchain/shader_compiler.cpp",
}

# Zones still to wrap. Each must be emptied, not extended.
PENDING = {
    "app.cpp",
    "app.hpp",
    "core/gpu_texture.cpp",
    "core/new_project.cpp",
    "core/project_settings.cpp",
    "core/recovery.cpp",
    "core/snippet_library.cpp",
    "core/workspace.cpp",
    "dba/code_outline.cpp",
    "dba/completion.cpp",
    "dba/program_info.cpp",
    "panels/asset_preview.cpp",
    "panels/build_window.cpp",
    "panels/code_panel.cpp",
    "panels/completion_popup.cpp",
    "panels/editor_panel.cpp",
    "panels/launcher_window.cpp",
    "panels/media_panel.cpp",
    "panels/output_panel.cpp",
    "panels/problems_panel.cpp",
    "panels/project_panel.cpp",
    "panels/project_settings_window.cpp",
    "panels/scene_editor.cpp",
    "panels/scene_inspector.cpp",
    "panels/scene_paint.cpp",
    "panels/scene_viewport.cpp",
    "panels/snippet_window.cpp",
    "scene/heightmap.cpp",
    "scene/scene_capture.cpp",
    "scene/scene_geometry.cpp",
    "scene/scene_integration.cpp",
    "scene/scene_patch.cpp",
    "scene/scene_renderer.cpp",
    "toolchain/active_run_planner.cpp",
    "toolchain/android_keystore.cpp",
    "toolchain/artifact_export_service.cpp",
    "toolchain/execution_adapter.cpp",
    "toolchain/export_android.cpp",
    "toolchain/export_common.cpp",
    "toolchain/export_desktop.cpp",
    "toolchain/export_ios.cpp",
    "toolchain/export_readiness.cpp",
    "toolchain/icon_generator.cpp",
    "toolchain/process_runner.cpp",
    "toolchain/windows_resources.cpp",
}

TOKEN = re.compile(
    r"""
    (?P<comment>//[^\n]*|/\*.*?\*/)
    |(?P<directive>^[ \t]*\#(?:[^\n\\]|\\.)*)
    |R"(?P<delimiter>[^(\s]*)\((?P<raw>.*?)\)(?P=delimiter)"
    |(?:u8|u|U|L)?"(?P<string>(?:[^"\\\n]|\\.)*)"
    |'(?:[^'\\\n]|\\.)+'
    |(?P<word>[A-Za-z_][A-Za-z0-9_]*)
    |(?P<number>\.?[0-9](?:[A-Za-z0-9_.']|[eEpP][+-])*)
    |(?P<symbol>::|.)
    """,
    re.DOTALL | re.MULTILINE | re.VERBOSE,
)
FORMAT_SPECIFIER = re.compile(r"%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|z|j|t|L)?[a-zA-Z%]")
PLACEHOLDER = re.compile(r"\{[0-9]+\}")
ESCAPE = re.compile(r"\\(x[0-9a-fA-F]+|[0-7]{1,3}|.)", re.DOTALL)
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}
WORD = re.compile(r"[A-Za-z]{2,}")


@dataclass
class Token:
    Kind: str
    Text: str
    Line: int


def Unescape(Text: str) -> str:
    def Replace(Match: re.Match) -> str:
        Value = Match.group(1)
        if Value in SIMPLE_ESCAPES:
            return SIMPLE_ESCAPES[Value]
        if Value[0] == "x":
            return chr(int(Value[1:], 16))
        if Value[0].isdigit():
            return chr(int(Value, 8))
        return Value

    return ESCAPE.sub(Replace, Text)


def Tokenize(Text: str) -> list[Token]:
    """C++ tokens without comments and directives; adjacent strings become one."""
    Tokens: list[Token] = []
    Line = 1
    At = 0
    for Match in TOKEN.finditer(Text):
        Line += Text.count("\n", At, Match.start())
        At = Match.start()
        Kind = Match.lastgroup
        if Kind in ("comment", "directive") or Match.group().isspace():
            continue
        if Kind in ("string", "raw"):
            Value = Unescape(Match.group("string")) if Kind == "string" else Match.group("raw")
            if Tokens and Tokens[-1].Kind == "string":
                Tokens[-1].Text += Value
            else:
                Tokens.append(Token("string", Value, Line))
        elif Kind == "word":
            Tokens.append(Token("word", Match.group(), Line))
        else:
            Tokens.append(Token("symbol", Match.group(), Line))
    return Tokens


def Close(Tokens: list[Token], Open: int) -> int:
    """The index of the token closing the bracket at Open."""
    Depth = 0
    for Index in range(Open, len(Tokens)):
        if Tokens[Index].Kind != "symbol":
            continue
        if Tokens[Index].Text in "([{":
            Depth += 1
        elif Tokens[Index].Text in ")]}":
            Depth -= 1
            if Depth == 0:
                return Index
    return len(Tokens) - 1


def SplitArguments(Tokens: list[Token], Open: int) -> list[tuple[int, int]]:
    """The [begin, end) token ranges of the call's arguments."""
    End = Close(Tokens, Open)
    Result = []
    Begin = Open + 1
    Index = Begin
    while Index < End:
        Token_ = Tokens[Index]
        if Token_.Kind == "symbol" and Token_.Text in "([{":
            Index = Close(Tokens, Index) + 1
            continue
        if Token_.Kind == "symbol" and Token_.Text == ",":
            Result.append((Begin, Index))
            Begin = Index + 1
        Index += 1
    if Begin < End:
        Result.append((Begin, End))
    return Result


def VisibleText(Text: str) -> str:
    """What a literal shows, as opposed to its ImGui ID and format specifiers."""
    return PLACEHOLDER.sub("", FORMAT_SPECIFIER.sub("", Text.split("##")[0]))


def HasText(Text: str) -> bool:
    return WORD.search(VisibleText(Text)) is not None


def ReadsAsSentence(Text: str) -> bool:
    """Two words or more, opening with a capital; identifiers, paths and code rarely do."""
    Visible = VisibleText(Text).strip()
    Visible = re.sub(r"^(?:\[[A-Z]+\]\s*|[>(]\s*)", "", Visible)
    if not Visible or not Visible[0].isupper():
        return False
    if re.search(r"[/\\_=;<>{}]|\.[a-z]+$|^[A-Z0-9_ ]+$", Visible):
        return False
    return len(WORD.findall(Visible)) >= 2


def SourceKey(Name: str, Text: str) -> str:
    return Text.split("##")[0] if Name == "TrId" else Text


def Strings(Tokens: list[Token], Begin: int, End: int) -> list[Token]:
    return [Value for Value in Tokens[Begin:End] if Value.Kind == "string"]


def UntranslatedStrings(Tokens: list[Token], Begin: int, End: int) -> list[Token]:
    """The literals of a range, skipping whatever a Tr* call already covers."""
    Result = []
    Index = Begin
    while Index < End:
        Value = Tokens[Index]
        if (
            Value.Kind == "word"
            and Value.Text in TRANSLATORS
            and Index + 1 < End
            and Tokens[Index + 1].Text == "("
        ):
            Index = Close(Tokens, Index + 1) + 1
            continue
        if Value.Kind == "string":
            Result.append(Value)
        Index += 1
    return Result


@dataclass
class Findings:
    Unwrapped: list[str]
    Sentences: list[str]
    TranslatedFormats: list[str]
    Missing: list[str]
    Stale: list[str]

    def Total(self) -> int:
        return sum(len(Items) for Items in vars(self).values())


def Audit(Files: dict[str, str], Catalogue: set[str], Verbatim: set[str], Pending: set[str]) -> Findings:
    Result = Findings([], [], [], [], [])
    Keys: dict[str, str] = {}
    Literals: set[str] = set()

    def Report(Items: list[str], Name: str, Value: Token) -> None:
        Items.append(f"{Name}:{Value.Line}: {Value.Text!r}")

    for Name, Text in sorted(Files.items()):
        Tokens = Tokenize(Text)
        Checked = Name not in Pending and Name not in NO_INTERFACE_TEXT
        Skipped: set[int] = set()
        # "const char* Popup = "Title";" names a literal shown further down.
        Named = {}
        for Index, Value in enumerate(Tokens[:-2]):
            if Value.Kind == "word" and Tokens[Index + 1].Text == "=" and Tokens[Index + 2].Kind == "string":
                if Index >= 2 and Tokens[Index - 1].Text == "*" and Tokens[Index - 2].Text == "char":
                    Named[Value.Text] = Tokens[Index + 2]

        def CheckDisplayed(Begin: int, End: int) -> None:
            if not Checked:
                return
            Displayed = UntranslatedStrings(Tokens, Begin, End)
            if End - Begin == 1 and Tokens[Begin].Text in Named:
                Displayed.append(Named[Tokens[Begin].Text])
            for Value in Displayed:
                if HasText(Value.Text) and Value.Text not in Verbatim:
                    Report(Result.Unwrapped, Name, Value)

        for Index, Value in enumerate(Tokens):
            if Value.Kind == "string":
                Literals.add(Value.Text)
                Literals.add(Value.Text.split("##")[0])
                continue
            if Value.Kind != "word" or Index + 1 >= len(Tokens):
                continue
            IsCall = Tokens[Index + 1].Text == "("
            InImGui = Index >= 2 and Tokens[Index - 1].Text == "::" and Tokens[Index - 2].Text == "ImGui"

            if IsCall and Value.Text.startswith("DBC_LOG_"):
                for Logged in Strings(Tokens, Index + 2, Close(Tokens, Index + 1)):
                    Skipped.add(id(Logged))
            elif IsCall and Value.Text in TRANSLATORS:
                Arguments = SplitArguments(Tokens, Index + 1)
                if Arguments:
                    for Source in Strings(Tokens, *Arguments[0]):
                        Skipped.add(id(Source))
                        Keys.setdefault(SourceKey(Value.Text, Source.Text), f"{Name}:{Source.Line}")
            elif IsCall and InImGui and Value.Text in LABELS:
                Arguments = SplitArguments(Tokens, Index + 1)
                for Position in LABELS[Value.Text]:
                    if Position < len(Arguments):
                        CheckDisplayed(*Arguments[Position])
            elif IsCall and InImGui and Value.Text in FORMATS:
                Arguments = SplitArguments(Tokens, Index + 1)
                Format = FORMATS[Value.Text]
                for Position, Range in enumerate(Arguments):
                    if Position >= Format:
                        CheckDisplayed(*Range)
                if Format < len(Arguments) and Checked:
                    Begin, End = Arguments[Format]
                    if any(Tokens[At].Text in TRANSLATORS for At in range(Begin, End)):
                        Result.TranslatedFormats.append(f"{Name}:{Tokens[Begin].Line}: ImGui::{Value.Text}")
            elif IsCall and Value.Text in HELPERS and Checked:
                Arguments = SplitArguments(Tokens, Index + 1)
                for Position in HELPERS[Value.Text]:
                    if Position < len(Arguments):
                        for Label in Strings(Tokens, *Arguments[Position]):
                            Skipped.add(id(Label))
                            if HasText(Label.Text):
                                Keys.setdefault(SourceKey("TrId", Label.Text), f"{Name}:{Label.Line}")
            elif Checked and Value.Text in SHORT_MESSAGE_SINKS and Tokens[Index + 1].Text == "=":
                Statement = Index + 2
                while Statement < len(Tokens) and Tokens[Statement].Text != ";":
                    Statement += 1
                for Message in UntranslatedStrings(Tokens, Index + 2, Statement):
                    if HasText(Message.Text) and Message.Text not in Verbatim:
                        Report(Result.Unwrapped, Name, Message)
                        Skipped.add(id(Message))

        if Checked:
            for Value in Tokens:
                if (
                    Value.Kind == "string"
                    and id(Value) not in Skipped
                    and ReadsAsSentence(Value.Text)
                    and Value.Text not in Verbatim
                    and Value.Text not in Catalogue
                ):
                    Report(Result.Sentences, Name, Value)

    Result.Unwrapped = sorted(set(Result.Unwrapped))
    Result.Missing = [f"{Where}: {Key!r}" for Key, Where in sorted(Keys.items()) if Key not in Catalogue]
    Result.Stale = sorted(repr(Key) for Key in Catalogue if Key not in Literals)
    return Result


def ReadVerbatim(Path_: Path) -> set[str]:
    """One literal per line, escaped as in C++, with # comments."""
    Lines = Path_.read_text(encoding="utf-8").splitlines()
    return {Unescape(Line) for Line in Lines if Line and not Line.startswith("#")}


def ReadCatalogue(Path_: Path) -> set[str]:
    Entries = json.loads(Path_.read_text(encoding="utf-8"))
    return {Key for Key in Entries if not Key.startswith("@")}


def main() -> None:
    Parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    Parser.add_argument("--check", action="store_true", help="fail when anything is found")
    Arguments = Parser.parse_args()

    Files = {
        Path_.relative_to(SOURCES).as_posix(): Path_.read_text(encoding="utf-8")
        for Path_ in SOURCES.rglob("*")
        if Path_.suffix in (".cpp", ".hpp")
    }
    Result = Audit(Files, ReadCatalogue(CATALOGUE), ReadVerbatim(VERBATIM), PENDING)

    Sections = (
        ("Interface text outside Tr*", Result.Unwrapped),
        ("Sentences that no language can reach", Result.Sentences),
        ("printf formats that are translated", Result.TranslatedFormats),
        (f"Keys absent from {CATALOGUE.name}", Result.Missing),
        (f"Keys of {CATALOGUE.name} that no source mentions", Result.Stale),
    )
    Stream = sys.stderr if Arguments.check else sys.stdout
    for Title, Items in Sections:
        if Items:
            print(f"{Title} ({len(Items)}):", file=Stream)
            print("\n".join(f"  {Item}" for Item in Items), file=Stream)
    if Arguments.check and Result.Total():
        raise SystemExit(1)
    print(f"Studio strings: {Result.Total()} finding(s), {len(PENDING)} file(s) still pending")


if __name__ == "__main__":
    main()
