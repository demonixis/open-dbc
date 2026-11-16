#include "unique_suffix.hpp"
#include <doctest/doctest.h>

#include "core/new_project.hpp"
#include "interpreter/project_resolver.hpp"
#include "language/frontend.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace dbc::studio;
using dbc::language::LanguageProfile;

namespace
{
    struct TemporaryLocation
    {
        TemporaryLocation()
        {
            Root = std::filesystem::temp_directory_path() /
                   ("opendbc-new-project-" + std::to_string(dbc::testing::UniqueSuffix()));
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
    for (const char* Name : {"", " Game", "Game ", "Game.", ".", "..", "a/b", "a\\b", "Say \"hi\"", "a:b", "a*b", "con",
                             "NUL.txt", "com1", "LPT9"})
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
    const core::NewProjectResult Created =
        core::CreateProject({Location.Root, "Classic Game", LanguageProfile::Dbc, core::ProjectTemplate::BasicScene});
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

TEST_CASE("a new project is DBPro unless the request says otherwise")
{
    TemporaryLocation Location;
    core::NewProjectRequest Request;
    Request.Location = Location.Root;
    Request.Name = "Default Game";
    CHECK(Request.Profile == LanguageProfile::DbPro);
    const core::NewProjectResult Created = core::CreateProject(Request);
    REQUIRE_MESSAGE(Created, Created.Error);
    CHECK(std::filesystem::is_regular_file(Created.Root / "Default Game.dbpro"));

    const auto Resolved = Resolve(Created.Root);
    REQUIRE_MESSAGE(Resolved, Resolved.Error);
    CHECK(Resolved.Project->Profile == LanguageProfile::DbPro);
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

TEST_CASE("a location lists its project folders by name and skips hidden entries")
{
    TemporaryLocation Location;
    std::filesystem::create_directories(Location.Root / "beta");
    std::filesystem::create_directories(Location.Root / "Alpha");
    std::filesystem::create_directories(Location.Root / ".hidden");
    std::ofstream(Location.Root / "notes.txt") << "x";

    const std::vector<std::filesystem::path> Folders = core::ProjectFolders(Location.Root);
    REQUIRE(Folders.size() == 2);
    CHECK(Folders[0].filename() == "Alpha");
    CHECK(Folders[1].filename() == "beta");
    CHECK(core::ProjectFolders(Location.Root / "missing").empty());
}

TEST_CASE("a sample opens as it was, beside the copy the user edited")
{
    TemporaryLocation Location;
    const std::filesystem::path Sample = Location.Root / "samples" / "demo";
    std::filesystem::create_directories(Sample / "textures");
    std::ofstream(Sample / "demo.dba") << "print 1\n";
    std::ofstream(Sample / "textures" / "wall.png") << "png";
    const std::filesystem::path Projects = Location.Root / "projects";

    const core::NewProjectResult Copy = core::CopySampleProject(Sample, Projects);
    REQUIRE_MESSAGE(Copy, Copy.Error);
    CHECK(Copy.Root == Projects / "demo");
    CHECK(std::filesystem::is_regular_file(Copy.Root / "textures" / "wall.png"));

    // A copy that only ran is opened again; its log is not an edit.
    std::ofstream(Copy.Root / "open-dbc.log") << "ran";
    const core::NewProjectResult Same = core::CopySampleProject(Sample, Projects);
    REQUIRE_MESSAGE(Same, Same.Error);
    CHECK(Same.Root == Copy.Root);

    // An edited copy is the user's: the sample opens as it was, beside it.
    std::ofstream(Copy.Root / "demo.dba") << "print 2\n";
    const core::NewProjectResult Again = core::CopySampleProject(Sample, Projects);
    REQUIRE_MESSAGE(Again, Again.Error);
    CHECK(Again.Root == Projects / "demo-2");
    std::ifstream Edited(Copy.Root / "demo.dba");
    std::string Text;
    std::getline(Edited, Text);
    CHECK(Text == "print 2");
    CHECK(std::filesystem::file_size(Again.Root / "demo.dba") == std::filesystem::file_size(Sample / "demo.dba"));
    CHECK(core::CopySampleProject(Sample, Projects).Root == Projects / "demo-2");

    CHECK(!core::CopySampleProject(Location.Root / "samples" / "absent", Projects));
}

TEST_CASE("a project is duplicated, renamed and deleted inside its location")
{
    TemporaryLocation Location;
    const std::filesystem::path Projects = Location.Root / "projects";
    std::filesystem::create_directories(Projects / "game" / "media");
    std::ofstream(Projects / "game" / "main.dba") << "print 1\n";
    std::ofstream(Projects / "game" / "media" / "a.png") << "png";

    const core::NewProjectResult Copy = core::DuplicateProject(Projects / "game");
    REQUIRE_MESSAGE(Copy, Copy.Error);
    CHECK(Copy.Root == Projects / "game-2");
    CHECK(std::filesystem::is_regular_file(Copy.Root / "media" / "a.png"));
    CHECK(core::DuplicateProject(Projects / "game").Root == Projects / "game-3");
    CHECK(!core::DuplicateProject(Projects / "absent"));

    const core::NewProjectResult Renamed = core::RenameProject(Projects / "game-2", "level test");
    REQUIRE_MESSAGE(Renamed, Renamed.Error);
    CHECK(Renamed.Root == Projects / "level test");
    CHECK(!std::filesystem::exists(Projects / "game-2"));
    // A taken or an invalid name leaves the project where it is.
    CHECK(!core::RenameProject(Projects / "game-3", "game"));
    CHECK(!core::RenameProject(Projects / "game-3", "a/b"));
    CHECK(std::filesystem::is_directory(Projects / "game-3"));

    CHECK(core::DeleteProject(Projects / "game-3", Projects).empty());
    CHECK(!std::filesystem::exists(Projects / "game-3"));
    // Only a folder of the location itself is deleted.
    CHECK(!core::DeleteProject(Projects / "game" / "media", Projects).empty());
    CHECK(!core::DeleteProject(Location.Root / "projects", Projects).empty());
    CHECK(!core::DeleteProject(Projects / "absent", Projects).empty());
    CHECK(std::filesystem::is_regular_file(Projects / "game" / "media" / "a.png"));
}

TEST_CASE("a saved path follows its folder when the app's container has moved")
{
    TemporaryLocation Location;
    const std::filesystem::path Root = Location.Root / "new-container" / "Documents" / "projects";
    std::filesystem::create_directories(Root / "demo");
    const std::filesystem::path Old = Location.Root / "old-container" / "Documents" / "projects";

    CHECK(core::RelocateUnder(Old / "demo", Root) == Root / "demo");
    CHECK(core::RelocateUnder(Old / "demo" / "main.dba", Root) == Root / "demo" / "main.dba");
    // A path that still exists, or was never under such a folder, is kept.
    CHECK(core::RelocateUnder(Root / "demo", Root) == Root / "demo");
    CHECK(core::RelocateUnder(Location.Root / "elsewhere" / "demo", Root) == Location.Root / "elsewhere" / "demo");
    CHECK(core::RelocateUnder({}, Root).empty());
}

