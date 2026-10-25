#include <doctest/doctest.h>

#include "language/dbpro_project.hpp"
#include "language/dialect_detector.hpp"
#include "language/language_profile.hpp"

#include <filesystem>
#include <fstream>

using dbc::language::LanguageProfile;

TEST_CASE("DBA sources and DBPro project descriptors have distinct extensions")
{
    CHECK(dbc::language::IsLanguageSource("game.DBA"));
    CHECK_FALSE(dbc::language::IsLanguageSource("game.dbpro"));
    CHECK(dbc::language::IsDbProProject("game.DBPRO"));
}

TEST_CASE("Language profile names are strict and case insensitive")
{
    CHECK(dbc::language::ParseProfile("dbc") == LanguageProfile::Dbc);
    CHECK(dbc::language::ParseProfile("DBPRO") == LanguageProfile::DbPro);
    CHECK_FALSE(dbc::language::ParseProfile("classic"));
    CHECK_FALSE(dbc::language::ParseProfile("hybrid"));
}

TEST_CASE("Dialect detection uses profile-specific grammar symbols and arities")
{
    using dbc::language::DialectDetectionKind;
    CHECK(dbc::language::DetectDialect({"shared.dba", "print 1\n"}).Kind == DialectDetectionKind::Ambiguous);
    CHECK(dbc::language::DetectDialect({"professional.dba", "global value as integer\n"}).Kind ==
          DialectDetectionKind::DbPro);
    CHECK(dbc::language::DetectDialect({"classic.dba", "bob 1,2,3,4\n"}).Kind == DialectDetectionKind::Dbc);
    CHECK(dbc::language::DetectDialect({"conflict.dba", "global value as integer\nbob 1,2,3,4\n"}).Kind ==
          DialectDetectionKind::Conflict);
}

TEST_CASE("DBPro project documents round trip without reserializing unrelated text")
{
    const std::string Source = "\xef\xbb\xbf; Header\r\nVersion = DBP1.00\r\nMAIN = Main.DBA\r\n"
                               "graphics mode=window\r\nwindow resolution=640x480\r\n"
                               "mystery = keep me\r\n";
    dbc::language::DbProProjectDocument Document = dbc::language::ParseDbProProject("Game.dbpro", Source);
    CHECK(Document.Valid());
    CHECK(Document.HasUtf8Bom);
    CHECK(Document.NewLine == "\r\n");
    CHECK(Document.Value("main") == "Main.DBA");
    REQUIRE(dbc::language::SetDbProProjectValue(Document, "graphics mode", "fullscreen"));
    CHECK(Document.Text.find("MAIN = Main.DBA") != std::string::npos);
    CHECK(Document.Text.find("mystery = keep me") != std::string::npos);
    CHECK(Document.Text.find("graphics mode=fullscreen") != std::string::npos);
}

TEST_CASE("DBPro project resolution is ordered case insensitive and confined by default")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-dbpro-project-resolution";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    {
        std::ofstream Main(Root / "Main.DBA");
        Main << "print 1\n";
        std::ofstream Include(Root / "Helpers.dba");
        Include << "function helper()\nendfunction 1\n";
    }
    const std::string Source = "version=DBP1.00\nmain=main.dba\ninclude1=helpers.DBA\n"
                               "media root path=\n";
    const dbc::language::DbProProjectDocument Document = dbc::language::ParseDbProProject(Root / "Game.dbpro", Source);
    const dbc::language::DbProProjectResolution Resolution = dbc::language::ResolveDbProProject(Document);
    const std::string Details = Resolution.Diagnostics.empty() ? std::string{} : Resolution.Diagnostics.front().Message;
    INFO(Details);
    CHECK(Resolution.Valid());
    REQUIRE(Resolution.OrderedSources.size() == 2);
    CHECK(Resolution.EntrySource.filename() == "Main.DBA");
    CHECK(Resolution.OrderedSources[1].filename() == "Helpers.dba");

    const dbc::language::DbProProjectDocument Escaping =
        dbc::language::ParseDbProProject(Root / "Escape.dbpro", "version=DBP1.00\nmain=../outside.dba\n");
    CHECK_FALSE(dbc::language::ResolveDbProProject(Escaping).Valid());
    std::filesystem::remove_all(Root, Error);
}
