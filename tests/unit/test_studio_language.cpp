#include <doctest/doctest.h>

#include "dba/code_outline.hpp"
#include "dba/command_database.hpp"
#include "dba/completion.hpp"
#include "dba/program_info.hpp"
#include "dba/reference.hpp"
#include "dba/source_symbols.hpp"
#include "language/language_catalogue.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using dbc::language::LanguageProfile;

using namespace dbc::studio;

namespace
{
    bool Contains(const dba::Completion& Completion, std::string_view Name)
    {
        return std::any_of(Completion.Items.begin(), Completion.Items.end(),
                           [&](const dba::CompletionItem& Entry) { return Entry.Name == Name; });
    }
} // namespace

TEST_CASE("Studio completion is filtered by the immutable profile")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);

    CHECK(Commands.Find(LanguageProfile::Dbc, "ADD VECTOR4") == nullptr);
    CHECK(Commands.Find(LanguageProfile::DbPro, "ADD VECTOR4") != nullptr);
    const auto Offers = [&](LanguageProfile Profile, std::string_view Name)
    { return Contains(dba::Complete(Commands, Profile, "addv", false), Name); };
    CHECK(Offers(LanguageProfile::DbPro, "ADD VECTOR4"));
    CHECK_FALSE(Offers(LanguageProfile::Dbc, "ADD VECTOR4"));
}

TEST_CASE("Studio completion follows a command name across its words")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    const auto Complete = [&](std::string_view Line)
    { return dba::Complete(Commands, LanguageProfile::DbPro, Line, false); };

    // Nothing caps the list: every command starting with SET is offered.
    const size_t SetCommands =
        std::count_if(Commands.All().begin(), Commands.All().end(),
                      [](const dba::Command& Entry)
                      {
                          return Entry.Words.size() > 1 && Entry.Words[0] == "set" &&
                                 dbc::language::AvailableIn(*Entry.Descriptor, LanguageProfile::DbPro);
                      });
    const dba::Completion Set = Complete("set ");
    CHECK(Set.Items.size() == SetCommands);
    CHECK(Set.Items.size() > 40);
    CHECK(Set.Selection == dba::CompletionSelection::None);
    CHECK(std::is_sorted(Set.Items.begin(), Set.Items.end(),
                         [](const dba::CompletionItem& Left, const dba::CompletionItem& Right)
                         { return Left.Name < Right.Name; }));

    const dba::Completion Window = Complete("set window ");
    CHECK(Window.ReplaceStart == 0);
    CHECK(Contains(Window, "SET WINDOW TITLE"));
    for (const dba::CompletionItem& Entry : Window.Items)
    {
        CHECK(Entry.Name.starts_with("SET WINDOW "));
    }

    const dba::Completion Title = Complete("    set window ti");
    REQUIRE(Title.Items.size() == 1);
    CHECK(Title.Items[0].Name == "SET WINDOW TITLE");
    CHECK(Title.ReplaceStart == 4);
    CHECK(Title.Typed == "set window ti");
    CHECK(Title.Selection == dba::CompletionSelection::Hard);

    // Abbreviated or skipped words still replace all that was typed.
    CHECK(Complete("set win ti").ReplaceStart == 0);
    CHECK(Contains(Complete("set win ti"), "SET WINDOW TITLE"));
    const dba::Completion Skipped = Complete("set title");
    CHECK(Skipped.ReplaceStart == 0);
    CHECK(Contains(Skipped, "SET WINDOW TITLE"));

    // A command starts at the longest run of words that fits a name.
    const dba::Completion Function = Complete("x = object position ");
    CHECK(Function.ReplaceStart == 4);
    CHECK(Contains(Function, "OBJECT POSITION X"));
    CHECK(Complete("print set w").ReplaceStart == 6);

    // Prefixes outrank loose matches, which Enter does not accept.
    const dba::Completion Loose = Complete("title");
    CHECK(Contains(Loose, "SET WINDOW TITLE"));
    CHECK(Loose.Selection == dba::CompletionSelection::Soft);
    CHECK(Complete("set").Items[0].Builtin->Words[0] == "set");
    CHECK(Complete("set").Selection == dba::CompletionSelection::Hard);
    CHECK(Contains(Complete("swt"), "SET WINDOW TITLE"));
    CHECK(Complete("s").Selection == dba::CompletionSelection::Soft);

    // Nothing is offered where no name can continue.
    CHECK(Complete("").Items.empty());
    CHECK(Complete("    ").Items.empty());
    CHECK(Complete("x = ").Items.empty());
    CHECK(Complete("print ").Items.empty());
    CHECK(Complete("set camera range ").Items.empty());
    CHECK(Complete("sync rate 60").Items.empty());
    CHECK(Complete("sync rate 60 ").Items.empty());
    CHECK(Complete("player.x").Items.empty());
    CHECK(Complete("x = caf\xC3\xA9").Items.empty());

    // Asking explicitly always shows something.
    const dba::Completion Everything = dba::Complete(Commands, LanguageProfile::DbPro, "x = ", true);
    CHECK(Everything.ReplaceStart == 4);
    CHECK(Everything.Items.size() > SetCommands);
    CHECK(Everything.Selection == dba::CompletionSelection::None);
    CHECK(dba::Complete(Commands, LanguageProfile::DbPro, "x = qqqq", true).ReplaceStart == 4);
}

TEST_CASE("Studio completion describes a name from the catalogue, per profile")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    const auto Describe = [&](std::string_view Name, LanguageProfile Profile = LanguageProfile::DbPro)
    { return dba::DescribeCommand(Commands, Profile, Name); };
    using Lines = std::vector<std::string>;

    CHECK(Describe("SET WINDOW TITLE").Signatures == Lines{"SET WINDOW TITLE string"});
    CHECK(Describe("SET WINDOW OFF").Signatures == Lines{"SET WINDOW OFF"});
    CHECK(Describe("OBJECT POSITION X").Signatures == Lines{"OBJECT POSITION X(number) -> real"});
    CHECK(Describe("TIMER").Signatures == Lines{"TIMER() -> integer"});
    CHECK(Describe("PRINT").Signatures == Lines{"PRINT any..."});
    CHECK(Describe("BOX").Signatures == Lines{"BOX number, number, number, number",
                                              "BOX number, number, number, number, number, number, number, number"});
    CHECK(Describe("SET WINDOW TITLE").Warning.empty());

    // A name both a command and a function lists both forms.
    const dba::CommandHelp Both = Describe("MAKE VECTOR3");
    CHECK(std::any_of(Both.Signatures.begin(), Both.Signatures.end(),
                      [](const std::string& Line) { return Line.starts_with("MAKE VECTOR3("); }));
    CHECK(std::any_of(Both.Signatures.begin(), Both.Signatures.end(),
                      [](const std::string& Line) { return Line.starts_with("MAKE VECTOR3 "); }));

    CHECK(Describe("ADD LIGHT MAP LIGHT").Warning.starts_with("Not supported"));
    CHECK(Describe("DISABLE TNL", LanguageProfile::Dbc).Warning.starts_with("Compatibility stub"));
    CHECK(Describe("ADD VECTOR4", LanguageProfile::Dbc).Signatures.empty());
    CHECK(Describe("NOT A COMMAND").Signatures.empty());
}

TEST_CASE("Studio parameter hints find the builtin call around the cursor")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    const auto Find = [&](std::string_view Line, bool InBlockComment = false)
    { return dba::FindParameterHint(Commands, LanguageProfile::DbPro, Line, InBlockComment); };
    // The first overload's text, the argument and the name's offset.
    const auto Check = [&](std::string_view Line, std::string_view Text, size_t Argument, size_t NameStart)
    {
        CAPTURE(Line);
        const std::optional<dba::ParameterHint> Hint = Find(Line);
        REQUIRE(Hint.has_value());
        REQUIRE_FALSE(Hint->Overloads.empty());
        CHECK(Hint->Overloads[0].Text() == Text);
        CHECK(Hint->Argument == Argument);
        CHECK(Hint->NameStart == NameStart);
    };

    // Statements.
    Check("set window title ", "SET WINDOW TITLE string", 0, 0);
    Check("  set window title \"a", "SET WINDOW TITLE string", 0, 2);
    Check("position object 1, 2, ", "POSITION OBJECT number, number, number, number", 2, 0);
    Check("POSITION OBJECT 1,", "POSITION OBJECT number, number, number, number", 1, 0);
    Check("sync : set window title ", "SET WINDOW TITLE string", 0, 7);
    Check("if x then set window title ", "SET WINDOW TITLE string", 0, 10);
    Check("if x then sync else set window title ", "SET WINDOW TITLE string", 0, 20);

    // Calls, and what sits between them.
    Check("x = rgb(", "RGB(number, number, number) -> integer", 0, 4);
    Check("x = rgb(255, ", "RGB(number, number, number) -> integer", 1, 4);
    Check("x = object position x(", "OBJECT POSITION X(number) -> real", 0, 4);
    Check("x = timer(", "TIMER() -> integer", 0, 4);
    Check("print rgb(1, 2", "RGB(number, number, number) -> integer", 1, 6);
    Check("position object 1, rgb(1, ", "RGB(number, number, number) -> integer", 1, 19);
    Check("position object 1, rgb(1, 2, 3), ", "POSITION OBJECT number, number, number, number", 2, 0);
    Check("position object 1, (2 + ", "POSITION OBJECT number, number, number, number", 1, 0);
    Check("position object grid(1, 2), ", "POSITION OBJECT number, number, number, number", 1, 0);
    Check("position object Twice(1, ", "POSITION OBJECT number, number, number, number", 0, 0);
    Check("position object(1, ", "POSITION OBJECT number, number, number, number", 1, 0);

    // Commas in strings separate nothing.
    Check("load image \"a,b\", ", "LOAD IMAGE string, number, [number], [number]", 1, 0);
    Check("load image \"a,b", "LOAD IMAGE string, number, [number], [number]", 0, 0);

    // A name shared by a command and a function follows the form used.
    Check("make vector3 ", "MAKE VECTOR3 number", 0, 0);
    Check("x = make vector3(", "MAKE VECTOR3(number) -> integer", 0, 4);

    // Which parameter each overload gives the argument.
    const std::optional<dba::ParameterHint> Box = Find("box 1, 2, 3, 4, ");
    REQUIRE(Box.has_value());
    REQUIRE(Box->Overloads.size() == 2);
    CHECK(Box->Overloads[0].ParameterFor(Box->Argument) == dba::Signature::NoParameter);
    CHECK(Box->Overloads[1].ParameterFor(Box->Argument) == 4);
    const std::optional<dba::ParameterHint> Print = Find("print 1, 2, 3");
    REQUIRE(Print.has_value());
    CHECK(Print->Argument == 2);
    CHECK(Print->Overloads[0].ParameterFor(2) == 0);
    CHECK(Print->Overloads[0].Text() == "PRINT any...");

    // Nothing while the name is typed, for what takes no argument, or in a comment.
    CHECK_FALSE(Find("").has_value());
    CHECK_FALSE(Find("   ").has_value());
    CHECK_FALSE(Find("set window title").has_value());
    CHECK_FALSE(Find("set window ").has_value());
    CHECK_FALSE(Find("set window off ").has_value());
    CHECK_FALSE(Find("sync(").has_value());
    CHECK_FALSE(Find("x = 1, ").has_value());
    CHECK_FALSE(Find("grid(1, ").has_value());
    CHECK_FALSE(Find("player.rgb(").has_value());
    CHECK_FALSE(Find("x = rgb(1, 2, 3)").has_value());
    CHECK_FALSE(Find("rem position object 1, ").has_value());
    CHECK_FALSE(Find("position object 1 ` 2, ").has_value());
    CHECK_FALSE(Find("position object 1, ", true).has_value());
    CHECK(Find("remend : position object 1, ", true).has_value());
    CHECK_FALSE(Find("remstart position object 1, ").has_value());
    CHECK_FALSE(dba::FindParameterHint(Commands, LanguageProfile::Dbc, "add vector4 1, ", false).has_value());
}

TEST_CASE("Studio completion spells names the way they are typed")
{
    CHECK(dba::ApplyTypedCase("SET WINDOW TITLE", "set window ti") == "set window title");
    CHECK(dba::ApplyTypedCase("SET WINDOW TITLE", "s") == "set window title");
    CHECK(dba::ApplyTypedCase("SET WINDOW TITLE", "SET w") == "SET WINDOW TITLE");
    CHECK(dba::ApplyTypedCase("SET WINDOW TITLE", "Set w") == "Set Window Title");
    CHECK(dba::ApplyTypedCase("GET DIR$", "get") == "get dir$");
    CHECK(dba::ApplyTypedCase("SET WINDOW TITLE", "") == "SET WINDOW TITLE");
    CHECK(dba::ApplyTypedCase("SET WINDOW TITLE", "sEt") == "SET WINDOW TITLE");

    CHECK(dba::CompletionWordEnd("set window", 5) == 10);
    CHECK(dba::CompletionWordEnd("set window", 3) == 3);
    CHECK(dba::CompletionWordEnd("set window", 10) == 10);
}

TEST_CASE("Studio completion stays out of comments and strings")
{
    const auto Ends = [](std::string_view Text)
    {
        bool InBlockComment = false;
        return dba::EndsInCommentOrString(Text, InBlockComment);
    };
    CHECK_FALSE(Ends("set window"));
    CHECK(Ends("rem set window"));
    CHECK(Ends("REM set window"));
    CHECK(Ends("x = 1 ` set window"));
    CHECK(Ends("print \"set window"));
    CHECK_FALSE(Ends("print \"a\" : set window"));
    // No escape in BASIC strings: this one is closed.
    CHECK_FALSE(Ends("load dir \"C:\\\" : set"));
    CHECK_FALSE(Ends("remaining = 1"));

    bool InBlockComment = false;
    CHECK(dba::EndsInCommentOrString("remstart", InBlockComment));
    CHECK(dba::EndsInCommentOrString("print \"never closed", InBlockComment));
    CHECK_FALSE(dba::EndsInCommentOrString("remend set", InBlockComment));
    CHECK_FALSE(InBlockComment);
}

TEST_CASE("Studio scanning applies one project profile to DBA sources")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-language";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);

    {
        std::ofstream Classic(Root / "classic.DBA");
        Classic << "local marker = 1\n";
        std::ofstream Professional(Root / "professional.dba");
        Professional << "print 42\n";
        std::ofstream Descriptor(Root / "project.DBPRO");
        Descriptor << "version=DBP1.00\nmain=professional.dba\n";
    }

    std::vector<dba::ProgramInfo> Programs = dba::ScanPrograms(Root, LanguageProfile::DbPro);
    REQUIRE(Programs.size() == 2);
    CHECK(Programs[0].Profile == LanguageProfile::DbPro);
    CHECK(Programs[1].Profile == LanguageProfile::DbPro);

    dba::AnalyzeProgram(Programs[0]);
    CHECK(Programs[0].Profile == LanguageProfile::DbPro);

    std::filesystem::remove_all(Root, Error);
}

TEST_CASE("Studio code outline lists declarations with their uses")
{
    const std::string Source = "type Vec\n"
                               "  x as float\n"
                               "  y as float\n"
                               "endtype\n"
                               "global Score\n"
                               "dim Grid(10)\n"
                               "Player as Vec\n"
                               "Player.x = 1\n"
                               "for Index = 1 to 3 : Grid(Index) = Twice(Index) : next Index\n"
                               "gosub Setup\n"
                               "local name$ = \"a\"\n"
                               "end\n"
                               "Setup:\n"
                               "  Lives = Twice(2)\n"
                               "return\n"
                               "function Twice(Value)\n"
                               "  Hidden = Value * 2\n"
                               "  global Total\n"
                               "endfunction Hidden\n"
                               "function Unused()\n"
                               "endfunction\n";

    const dba::CodeOutline Outline = dba::BuildCodeOutline("outline.dba", Source, LanguageProfile::DbPro);

    REQUIRE(Outline.Functions.size() == 2);
    CHECK(Outline.Functions[0].Name == "Twice");
    CHECK(Outline.Functions[0].Line == 16);
    CHECK(Outline.Functions[0].Count == 2);
    CHECK(Outline.Functions[1].Name == "Unused");
    CHECK(Outline.Functions[1].Count == 0);

    REQUIRE(Outline.Labels.size() == 1);
    CHECK(Outline.Labels[0].Name == "Setup");
    CHECK(Outline.Labels[0].Line == 13);
    CHECK(Outline.Labels[0].Count == 1);

    REQUIRE(Outline.Types.size() == 1);
    CHECK(Outline.Types[0].Name == "Vec");
    CHECK(Outline.Types[0].Count == 2);

    std::vector<std::string> Variables;
    for (const dba::OutlineEntry& Entry : Outline.Variables)
        Variables.push_back(Entry.Name + (Entry.Array ? "()" : ""));
    // `Hidden` is local to Twice; `Total` is a global declared inside it.
    CHECK(Variables == std::vector<std::string>{"Score", "Grid()", "Player", "Index", "name$", "Lives", "Total"});
    CHECK(Outline.Variables[0].Line == 5);
}

TEST_CASE("Studio code outline survives code that does not compile")
{
    const dba::CodeOutline Outline =
        dba::BuildCodeOutline("broken.dba", "function Open(\nprint (\nStart:\ngoto Start\n", LanguageProfile::Dbc);
    REQUIRE(Outline.Labels.size() == 1);
    CHECK(Outline.Labels[0].Count == 1);
}

TEST_CASE("Source symbols list what each scope sees, on code that does not compile yet")
{
    const std::string Text = "type Point\n"
                             "  x as float\n"
                             "  y as float\n"
                             "endtype\n"
                             "type Player\n"
                             "  Position as Point\n"
                             "  name$\n"
                             "endtype\n"
                             "#constant MaxSpeed 8\n"
                             "global Score as integer\n"
                             "hero as Player\n"
                             "dim Enemies(10) as Player\n"
                             "lives = 3\n"
                             "for Wave = 1 to 3 : next Wave\n"
                             "gosub _Setup\n"
                             "end\n"
                             "_Setup:\n"
                             "return\n"
                             "function Move(dx#, who as Player)\n"
                             "  local Step as Point\n"
                             "  Score = Score + 1\n"
                             "  distance# = dx# * 2\n"
                             "  print (\n"
                             "endfunction distance#\n";
    const dba::SourceSymbols Symbols = dba::BuildSourceSymbols("game.dba", Text, LanguageProfile::DbPro);
    const auto Names = [](const std::vector<dba::SourceVariable>& Values)
    {
        std::vector<std::string> Result;
        for (const dba::SourceVariable& Value : Values)
            Result.push_back(Value.Name);
        return Result;
    };

    REQUIRE(Symbols.Types.size() == 2);
    CHECK(Symbols.Types[1].Name == "Player");
    CHECK(Names(Symbols.Types[1].Fields) == std::vector<std::string>{"Position", "name$"});
    CHECK(Symbols.Types[1].Fields[0].Type == "Point");
    CHECK(Symbols.Types[1].Fields[0].UserType == "POINT");
    CHECK(Symbols.Types[1].Fields[1].Type == "string");
    CHECK(Symbols.Constants == std::vector<std::string>{"MaxSpeed"});
    CHECK(Symbols.Labels == std::vector<std::string>{"_Setup"});

    // GLOBAL variables and arrays are seen everywhere; the main program keeps its other variables.
    CHECK(Names(Symbols.Shared) == std::vector<std::string>{"Score", "Enemies"});
    CHECK(Symbols.Shared[1].Array);
    CHECK(Symbols.Shared[1].UserType == "PLAYER");
    CHECK(Names(Symbols.Main) == std::vector<std::string>{"hero", "lives", "Wave"});
    CHECK(Symbols.Main[0].Type == "Player");

    REQUIRE(Symbols.Functions.size() == 1);
    const dba::SourceFunction& Move = Symbols.Functions[0];
    CHECK(Move.Name == "Move");
    CHECK(Move.Parameters == std::vector<std::string>{"dx#", "who as Player"});
    // Score is the global, not a local of Move.
    CHECK(Names(Move.Locals) == std::vector<std::string>{"dx#", "who", "Step", "distance#"});
    CHECK(Move.Locals[0].Type == "real");
    CHECK(Symbols.FunctionAt(19) == &Move);
    CHECK(Symbols.FunctionAt(22) == &Move);
    CHECK(Symbols.FunctionAt(24) == &Move);
    CHECK(Symbols.FunctionAt(18) == nullptr);
    CHECK(Symbols.FindType("POINT") == &Symbols.Types[0]);
}

TEST_CASE("Program symbols follow includes from the folder of the file that names them")
{
    const dba::SourceReader Read = [](const std::filesystem::path& Path) -> std::optional<std::string>
    {
        if (Path == std::filesystem::path("game/lib/maths.dba"))
            return std::string("#include \"../ui/hud.dba\"\n#include \"maths.dba\"\n"
                               "function Clamp(v#, low#, high#)\nendfunction v#\n");
        if (Path == std::filesystem::path("game/ui/hud.dba"))
            return std::string("type Widget\n  id\nendtype\nglobal HudScale#\nfunction DrawHud()\nendfunction\n");
        return std::nullopt;
    };
    const dba::SourceSymbols Symbols = dba::BuildProgramSymbols(
        "game/main.dba",
        "#include \"lib\\maths.dba\"\n#include \"missing.dba\"\nfunction Local()\nendfunction\n",
        LanguageProfile::DbPro, Read);
    REQUIRE(Symbols.Functions.size() == 3);
    CHECK(Symbols.Functions[0].Name == "Local");
    CHECK(Symbols.Functions[0].FirstLine == 3);
    CHECK(Symbols.Functions[1].Name == "Clamp");
    // An included function has no lines in the edited file.
    CHECK(Symbols.Functions[1].FirstLine == 0);
    CHECK(Symbols.Functions[2].Name == "DrawHud");
    REQUIRE(Symbols.Types.size() == 1);
    CHECK(Symbols.Types[0].Name == "Widget");
    REQUIRE(Symbols.Shared.size() == 1);
    CHECK(Symbols.Shared[0].Name == "HudScale#");
}

TEST_CASE("Completion offers the names the program declares, by scope, beside the builtins")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    const dba::SourceSymbols Symbols = dba::BuildSourceSymbols(
        "game.dba",
        "type Point\n  x as float\n  y as float\nendtype\n"
        "type Player\n  Position as Point\n  Score\n  name$\nendtype\n"
        "#constant ScreenCenter 320\n"
        "global ScoreTotal\n"
        "hero as Player\n"
        "dim Enemies(10) as Player\n"
        "scale# = 1.0\n"
        "_Setup:\n"
        "function Scatter(amount#, who as Player)\n"
        "  local spread as Point\n"
        "endfunction\n",
        LanguageProfile::DbPro);
    const auto Complete = [&](std::string_view Line, std::uint32_t CursorLine = 14)
    { return dba::Complete(Commands, LanguageProfile::DbPro, Line, false, &Symbols, CursorLine); };
    const auto Names = [](const dba::Completion& Completion)
    {
        std::vector<std::string> Result;
        for (const dba::CompletionItem& Item : Completion.Items)
            if (Item.Builtin == nullptr)
                Result.push_back(Item.Name);
        return Result;
    };

    // The program's names come first, in their own spelling, then the builtins.
    const dba::Completion Sc = Complete("x = sc");
    CHECK(Names(Sc) == std::vector<std::string>{"scale#", "Scatter", "ScoreTotal", "ScreenCenter"});
    REQUIRE(Sc.Items.size() > 4);
    CHECK(Sc.Items[0].What == dba::CompletionItem::Kind::Variable);
    CHECK(Sc.Items[0].Detail == "real");
    CHECK(Sc.Items[1].Detail == "Scatter(amount#, who as Player)");
    CHECK(Sc.Items[4].Builtin != nullptr);
    CHECK(Contains(Sc, "SCREEN WIDTH"));
    CHECK(Sc.ReplaceStart == 4);
    CHECK(Sc.Selection == dba::CompletionSelection::Hard);
    CHECK(dba::CompletionText(Sc.Items[1], "sc") == "Scatter");
    CHECK(dba::CompletionText(Sc.Items[4], "sc") == dba::ApplyTypedCase(Sc.Items[4].Name, "sc"));

    // A name typed in full is not replaced on Enter, and one letter never is.
    CHECK(Complete("x = hero").Selection == dba::CompletionSelection::Soft);
    CHECK(Complete("x = h").Selection == dba::CompletionSelection::Soft);

    // Inside a function: its parameters and locals, and what is global; not the main program's variables.
    const dba::Completion Inside = Complete("  x = s", 16);
    CHECK(Names(Inside) == std::vector<std::string>{"Scatter", "ScoreTotal", "ScreenCenter", "spread"});
    CHECK(Names(Complete("  x = am", 16)) == std::vector<std::string>{"amount#"});
    CHECK(Names(Complete("  x = ene", 16)) == std::vector<std::string>{"Enemies"});
    CHECK(Names(Complete("x = am")).empty());

    // Labels follow a jump, types follow AS, and neither shows elsewhere.
    CHECK(Names(Complete("gosub _s")) == std::vector<std::string>{"_Setup"});
    CHECK(Names(Complete("x = _s")).empty());
    CHECK(Names(Complete("foe as p")) == std::vector<std::string>{"Player", "Point"});
    CHECK(Names(Complete("x = po")).empty());

    // A command name over several words keeps the list, and a space or a number suggests no name.
    CHECK(Names(Complete("set sc")).empty());
    CHECK(Names(Complete("x = hero ")).empty());
    CHECK(Names(Complete("x = 12")).empty());

    // Ctrl+Space with nothing typed lists the names in scope, then every builtin.
    const dba::Completion Everything =
        dba::Complete(Commands, LanguageProfile::DbPro, "x = ", true, &Symbols, 14);
    CHECK(Names(Everything).size() == 6);
    CHECK(Everything.Items.size() > 500);
    CHECK(Everything.ReplaceStart == 4);

    const dba::CommandHelp Help = dba::DescribeItem(Commands, LanguageProfile::DbPro, Sc.Items[0]);
    REQUIRE(Help.Signatures.size() == 1);
    CHECK(Help.Signatures[0] == "scale# as real");
    CHECK_FALSE(Help.Description.empty());
}

TEST_CASE("Completion lists the fields of the user type left of a dot")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    const dba::SourceSymbols Symbols = dba::BuildSourceSymbols(
        "game.dba",
        "type Point\n  x as float\n  y as float\nendtype\n"
        "type Player\n  Position as Point\n  Score\n  name$\nendtype\n"
        "hero as Player\n"
        "dim Enemies(10) as Player\n"
        "count = 3\n"
        "function Reset(who as Player)\n"
        "endfunction\n",
        LanguageProfile::DbPro);
    const auto Complete = [&](std::string_view Line, std::uint32_t CursorLine = 12)
    { return dba::Complete(Commands, LanguageProfile::DbPro, Line, false, &Symbols, CursorLine); };
    const auto Names = [](const dba::Completion& Completion)
    {
        std::vector<std::string> Result;
        for (const dba::CompletionItem& Item : Completion.Items)
            Result.push_back(Item.Name);
        return Result;
    };

    // Right after the dot: every field, and no builtin.
    const dba::Completion All = Complete("hero.");
    CHECK(Names(All) == std::vector<std::string>{"name$", "Position", "Score"});
    CHECK(All.Items[1].What == dba::CompletionItem::Kind::Field);
    CHECK(All.Items[1].Detail == "Point");
    CHECK(All.ReplaceStart == 5);
    CHECK(All.Selection == dba::CompletionSelection::Soft);

    const dba::Completion Typed = Complete("print hero.sc");
    CHECK(Names(Typed) == std::vector<std::string>{"Score"});
    CHECK(Typed.ReplaceStart == 11);
    CHECK(Typed.Selection == dba::CompletionSelection::Hard);

    // Through a nested type, an array element and a parameter.
    CHECK(Names(Complete("hero.position.")) == std::vector<std::string>{"x", "y"});
    CHECK(Names(Complete("x# = Enemies(count + abs(2)).Position.y")) == std::vector<std::string>{"y"});
    CHECK(Names(Complete("  who.", 13)) == std::vector<std::string>{"name$", "Position", "Score"});

    // Nothing for a field of a builtin type, an unknown name, a number or a parameter out of scope.
    CHECK(Complete("hero.score.").Items.empty());
    CHECK(Complete("nobody.").Items.empty());
    CHECK(Complete("x = 1.5").Items.empty());
    CHECK(Complete("who.").Items.empty());
    // Without symbols a dot suggests nothing, as before.
    CHECK(dba::Complete(Commands, LanguageProfile::DbPro, "hero.", false).Items.empty());
}

TEST_CASE("The parameter hint shows a function of the program when no builtin has the name")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    const dba::SourceSymbols Symbols = dba::BuildSourceSymbols(
        "game.dba",
        "type Player\n  id\nendtype\nfunction Scatter(amount#, who as Player)\nendfunction\n"
        "function Idle()\nendfunction\n",
        LanguageProfile::DbPro);
    const auto Hint = [&](std::string_view Line)
    { return dba::FindParameterHint(Commands, LanguageProfile::DbPro, Line, false, &Symbols); };

    const std::optional<dba::ParameterHint> Found = Hint("x = scatter(1.0, ");
    REQUIRE(Found.has_value());
    REQUIRE(Found->Overloads.size() == 1);
    CHECK(Found->Overloads[0].Text() == "Scatter(amount#, who as Player)");
    CHECK(Found->Argument == 1);
    CHECK(Found->NameStart == 4);
    CHECK_FALSE(Hint("idle(").has_value());
    // A builtin keeps its own hint.
    const std::optional<dba::ParameterHint> Builtin = Hint("x = rgb(1, ");
    REQUIRE(Builtin.has_value());
    CHECK(Builtin->Overloads[0].Prefix == "RGB(");
}
