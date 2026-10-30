#include <doctest/doctest.h>

#include "dba/code_outline.hpp"
#include "dba/command_database.hpp"
#include "dba/completion.hpp"
#include "dba/program_info.hpp"
#include "dba/reference.hpp"
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
                           [&](const dba::Command* Entry) { return Entry->Classic == Name; });
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
    CHECK(std::is_sorted(Set.Items.begin(), Set.Items.end(), [](const dba::Command* Left, const dba::Command* Right)
                         { return Left->Classic < Right->Classic; }));

    const dba::Completion Window = Complete("set window ");
    CHECK(Window.ReplaceStart == 0);
    CHECK(Contains(Window, "SET WINDOW TITLE"));
    for (const dba::Command* Entry : Window.Items)
    {
        CHECK(Entry->Classic.starts_with("SET WINDOW "));
    }

    const dba::Completion Title = Complete("    set window ti");
    REQUIRE(Title.Items.size() == 1);
    CHECK(Title.Items[0]->Classic == "SET WINDOW TITLE");
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
    CHECK(Complete("set").Items[0]->Words[0] == "set");
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
