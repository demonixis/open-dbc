#include <doctest/doctest.h>

#include "core/new_project.hpp"
#include "interpreter/project_resolver.hpp"
#include "language/frontend.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

using namespace dbc::studio;
using dbc::language::LanguageProfile;

namespace
{
    struct TemporaryLocation
    {
        TemporaryLocation()
        {
            Root = std::filesystem::temp_directory_path() /
                   ("opendbc-new-project-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(Root);
        }

        ~TemporaryLocation()
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Root, Ignored);
        }

        std::filesystem::path Root{};
    };

    std::string CompileErrors(const std::string& Text, LanguageProfile Profile)
    {
        auto Source = std::make_shared<dbc::language::SourceDocument>();
        Source->LogicalPath = "main.dba";
        Source->Text = Text;
        Source->Profile = Profile;
        const dbc::language::Program Program = dbc::language::Compile(Source);
        std::string Errors;
        for (const auto& Diagnostic : Program.Diagnostics)
            if (Diagnostic.Severity == dbc::language::DiagnosticSeverity::Error)
                Errors += dbc::language::FormatDiagnostic(*Source, Diagnostic) + "\n";
        return Errors;
    }

    dbc::interpreter::ProjectResolveResult Resolve(const std::filesystem::path& Root)
    {
        dbc::interpreter::ProjectResolveRequest Request;
        Request.Target = Root;
        return dbc::interpreter::ResolveProject(Request);
    }
} // namespace

TEST_CASE("project names must be valid folder names on every desktop host")
{
    CHECK(core::ValidateProjectName("My Game 2").empty());
    CHECK(core::ValidateProjectName("Space.Invaders").empty());
    CHECK(core::ValidateProjectName("Console").empty());
    for (const char* Name : {"", " Game", "Game ", "Game.", ".", "..", "a/b", "a\\b", "Say \"hi\"", "a:b", "a*b",
                             "con", "NUL.txt", "com1", "LPT9"})
        CHECK_MESSAGE(!core::ValidateProjectName(Name).empty(), Name);
    CHECK(!core::ValidateProjectName(std::string(65, 'a')).empty());
}

TEST_CASE("project templates compile under both profiles")
{
    for (const core::ProjectTemplate Template : {core::ProjectTemplate::Empty, core::ProjectTemplate::BasicScene})
    {
        const std::string Source = core::ProjectTemplateSource(Template, "My Game");
        CHECK(CompileErrors(Source, LanguageProfile::Dbc) == "");
        CHECK(CompileErrors(Source, LanguageProfile::DbPro) == "");
    }
    CHECK(core::ProjectTemplateSource(core::ProjectTemplate::BasicScene, "My Game")
              .find("set window title \"My Game\"") != std::string::npos);
}

TEST_CASE("a new DBC project resolves through its project configuration")
{
    TemporaryLocation Location;
    const core::NewProjectResult Created = core::CreateProject(
        {Location.Root, "Classic Game", LanguageProfile::Dbc, core::ProjectTemplate::BasicScene});
    REQUIRE_MESSAGE(Created, Created.Error);
    CHECK(Created.Root == Location.Root / "Classic Game");
    CHECK(Created.MainSource == Created.Root / "main.dba");
    CHECK(std::filesystem::is_regular_file(Created.Root / ".opendbc-project.json"));

    const auto Resolved = Resolve(Created.Root);
    REQUIRE_MESSAGE(Resolved, Resolved.Error);
    CHECK(Resolved.Project->Profile == LanguageProfile::Dbc);
    CHECK(Resolved.Project->ProfileOrigin == dbc::interpreter::DialectOrigin::Configuration);
    CHECK(Resolved.Project->InputKind == dbc::interpreter::ProjectInputKind::Folder);
    CHECK(Resolved.Project->EntrySource.filename() == "main.dba");
    CHECK(!Resolved.Project->DescriptorPath);
}

TEST_CASE("a new DBPro project writes a valid historical descriptor")
{
    TemporaryLocation Location;
    const core::NewProjectResult Created =
        core::CreateProject({Location.Root, "Pro Game", LanguageProfile::DbPro, core::ProjectTemplate::BasicScene});
    REQUIRE_MESSAGE(Created, Created.Error);
    CHECK(!std::filesystem::exists(Created.Root / ".opendbc-project.json"));

    const auto Resolved = Resolve(Created.Root);
    REQUIRE_MESSAGE(Resolved, Resolved.Error);
    const dbc::interpreter::ResolvedProject& Project = *Resolved.Project;
    CHECK(Project.Profile == LanguageProfile::DbPro);
    CHECK(Project.InputKind == dbc::interpreter::ProjectInputKind::DbProProject);
    REQUIRE(Project.DescriptorPath);
    CHECK(Project.DescriptorPath->filename() == "Pro Game.dbpro");
    CHECK(Project.EntrySource.filename() == "main.dba");
    CHECK(Project.Warnings.empty());
    CHECK(Project.RunBlockers.empty());
    CHECK(Project.Startup.Title == "Pro Game");
    CHECK(Project.Startup.WindowMode == dbc::interpreter::ProjectWindowMode::Window);
    CHECK(Project.Startup.Width == 1280);
    CHECK(Project.Startup.Height == 720);
    CHECK(Project.Metadata.Name == "Pro Game");
    REQUIRE(Project.Descriptor);
    CHECK(Project.Descriptor->Diagnostics.empty());
}

TEST_CASE("project creation never touches an existing folder")
{
    TemporaryLocation Location;
    std::filesystem::create_directories(Location.Root / "Taken");
    const core::NewProjectResult Existing =
        core::CreateProject({Location.Root, "Taken", LanguageProfile::Dbc, core::ProjectTemplate::Empty});
    CHECK(!Existing);
    CHECK(std::filesystem::is_empty(Location.Root / "Taken"));

    const core::NewProjectResult Invalid =
        core::CreateProject({Location.Root, "a/b", LanguageProfile::Dbc, core::ProjectTemplate::Empty});
    CHECK(!Invalid);
    const core::NewProjectResult Missing =
        core::CreateProject({Location.Root / "missing", "Game", LanguageProfile::Dbc, core::ProjectTemplate::Empty});
    CHECK(!Missing);
    CHECK(!std::filesystem::exists(Location.Root / "missing"));
}
