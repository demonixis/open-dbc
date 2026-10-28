#include <doctest/doctest.h>

#include "core/snippet_library.hpp"
#include "language/frontend.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <memory>
#include <string>

using namespace dbc::studio;
using dbc::language::LanguageProfile;

namespace
{
    struct TemporaryFolder
    {
        TemporaryFolder()
        {
            Root = std::filesystem::temp_directory_path() /
                   ("opendbc-snippets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(Root);
        }

        ~TemporaryFolder()
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Root, Ignored);
        }

        std::filesystem::path Root{};
    };

    std::string CompileErrors(const std::string& Text, LanguageProfile Profile)
    {
        auto Source = std::make_shared<dbc::language::SourceDocument>();
        Source->LogicalPath = "snippet.dba";
        Source->Text = Text;
        Source->Profile = Profile;
        const dbc::language::Program Program = dbc::language::Compile(Source);
        std::string Errors;
        for (const auto& Diagnostic : Program.Diagnostics)
            if (Diagnostic.Severity == dbc::language::DiagnosticSeverity::Error)
                Errors += dbc::language::FormatDiagnostic(*Source, Diagnostic) + "\n";
        return Errors;
    }
} // namespace

TEST_CASE("snippet headers describe the snippet and stay out of its body")
{
    const core::Snippet Value = core::ParseSnippet("orbit.dba", "REM Snippet: Orbit Camera\r\n"
                                                                "rem profiles: dbpro\r\n"
                                                                "rem Description: Orbits a point.\r\n"
                                                                "\r\n"
                                                                "rem Not a header\r\n"
                                                                "sync\r\n");
    CHECK(Value.Name == "Orbit Camera");
    CHECK(Value.Description == "Orbits a point.");
    CHECK(!Value.Dbc);
    CHECK(Value.DbPro);
    CHECK(Value.Body == "rem Not a header\nsync\n");

    const core::Snippet Plain = core::ParseSnippet("my_loop.dba", "do\n   sync\nloop\n");
    CHECK(Plain.Name == "my_loop");
    CHECK(Plain.Dbc);
    CHECK(Plain.DbPro);
    CHECK(Plain.Body == "do\n   sync\nloop\n");

    const core::Snippet Unknown = core::ParseSnippet("x.dba", "rem Profiles: agk\nsync\n");
    CHECK(Unknown.Dbc);
    CHECK(Unknown.DbPro);
    CHECK(core::ParseSnippet("x.dba", "rem Profiles: dbc, dbpro\n").Dbc);
}

TEST_CASE("user snippets round-trip through their folder without replacing files")
{
    TemporaryFolder Folder;
    core::Snippet Value;
    Value.Name = "Jump Arc!";
    Value.Description = "A jump.";
    Value.DbPro = false;
    Value.Body = "print 1";
    std::string Error;
    const std::filesystem::path Path = core::SaveSnippet(Folder.Root / "snippets", Value, Error);
    REQUIRE_MESSAGE(!Path.empty(), Error);
    CHECK(Path.filename() == "jump_arc.dba");
    CHECK(core::SaveSnippet(Folder.Root / "snippets", Value, Error).empty());
    CHECK(Error.find("already exists") != std::string::npos);

    Value.Name = "!!!";
    CHECK(core::SaveSnippet(Folder.Root / "snippets", Value, Error).empty());
    Value.Name = "Empty";
    Value.Body = "  \n";
    CHECK(core::SaveSnippet(Folder.Root / "snippets", Value, Error).empty());

    std::ofstream(Folder.Root / "snippets" / "notes.txt") << "not a snippet";
    std::ofstream(Folder.Root / "snippets" / "Alpha.DBA") << "sync\n";
    const std::vector<core::Snippet> Loaded = core::LoadSnippets(Folder.Root / "snippets");
    REQUIRE(Loaded.size() == 2);
    CHECK(Loaded[0].Name == "Alpha");
    CHECK(Loaded[1].Name == "Jump Arc!");
    CHECK(Loaded[1].Description == "A jump.");
    CHECK(Loaded[1].Dbc);
    CHECK(!Loaded[1].DbPro);
    CHECK(Loaded[1].Body == "print 1\n");
    CHECK(Loaded[1].Path == Path);
    CHECK(core::LoadSnippets(Folder.Root / "missing").empty());
}

TEST_CASE("built-in snippets are embedded and compile under each profile they offer")
{
    const std::filesystem::path Folder = std::filesystem::path(TEST_SOURCE_DIR) / "tools/studio/snippets";
    std::size_t Files = 0;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Folder))
        Files += Entry.path().extension() == ".dba" ? 1 : 0;

    const std::vector<core::Snippet> Builtins = core::BuiltinSnippets();
    CHECK(Builtins.size() == Files);
    for (const core::Snippet& Value : Builtins)
    {
        INFO(Value.Name);
        CHECK(!Value.Description.empty());
        CHECK(!Value.Body.empty());
        CHECK(Value.Path.empty());
        for (const LanguageProfile Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
        {
            if (Value.Supports(Profile))
                CHECK(CompileErrors(Value.Body, Profile) == "");
        }
    }
    for (const char* Name : {"FPS Camera", "TPS Camera", "Orbit Camera", "Basic Scene"})
        CHECK(std::any_of(Builtins.begin(), Builtins.end(),
                          [&](const core::Snippet& Value) { return Value.Name == Name; }));
}
