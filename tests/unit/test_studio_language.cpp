#include <doctest/doctest.h>

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
