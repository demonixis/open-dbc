#include <doctest/doctest.h>

#include "dba/code_outline.hpp"
#include "dba/command_database.hpp"
#include "dba/program_info.hpp"
#include "dba/reference.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using dbc::language::LanguageProfile;

using namespace dbc::studio;

TEST_CASE("Studio completion is filtered by the immutable profile")
{
    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);

    CHECK(Commands.Find(LanguageProfile::Dbc, "ADD VECTOR4") == nullptr);
    CHECK(Commands.Find(LanguageProfile::DbPro, "ADD VECTOR4") != nullptr);
    const std::vector<std::string> Results = Commands.Complete(LanguageProfile::DbPro, "addv", 10);
    REQUIRE_FALSE(Results.empty());
    CHECK(std::find(Results.begin(), Results.end(), "ADD VECTOR4") != Results.end());
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
