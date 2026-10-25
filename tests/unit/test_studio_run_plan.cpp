#include <doctest/doctest.h>

#include "toolchain/active_run_planner.hpp"

using namespace dbc::studio;

namespace
{
    dbc::interpreter::ResolvedProject SourceProject(const std::filesystem::path& Entry)
    {
        dbc::interpreter::ResolvedProject Project;
        Project.Profile = dbc::language::LanguageProfile::Dbc;
        Project.InputKind = dbc::interpreter::ProjectInputKind::Source;
        Project.InputPath = Entry;
        Project.ProjectRoot = Entry.parent_path();
        Project.EntrySource = Entry;
        Project.OrderedSources = {Entry};
        Project.AssetRoot = Entry.parent_path();
        return Project;
    }
} // namespace

TEST_CASE("Studio active run never falls back from an unsaved document")
{
    const auto Project = SourceProject("/project/a.dba");
    const toolchain::ActiveRunPlan Plan = toolchain::PlanActiveRun({}, Project, {});

    CHECK_FALSE(Plan);
    CHECK(Plan.Error == "Save the active document as a .dba file before running.");
}

TEST_CASE("Studio active run accepts only a DBA editor document")
{
    const auto Project = SourceProject("/project/a.dba");
    const toolchain::ActiveRunPlan Plan = toolchain::PlanActiveRun("/project/project.dbpro", Project, {});

    CHECK_FALSE(Plan);
    CHECK(Plan.Error == "The active document is not a .dba BASIC source.");
}

TEST_CASE("Studio active run replaces the project source graph but preserves DBPro startup context")
{
    dbc::interpreter::ResolvedProject Project;
    Project.Profile = dbc::language::LanguageProfile::DbPro;
    Project.InputKind = dbc::interpreter::ProjectInputKind::DbProProject;
    Project.InputPath = "/project/game.dbpro";
    Project.ProjectRoot = "/project";
    Project.DescriptorPath = "/project/game.dbpro";
    Project.EntrySource = "/project/a.dba";
    Project.OrderedSources = {"/project/a.dba", "/project/extra.dba"};
    Project.AssetRoot = "/project/media";
    Project.Startup.Depth = 16;
    Project.Startup.Title = "Configured title";
    Project.RunBlockers = {"blocked startup option"};

    std::vector<toolchain::MemorySource> Sources = {
        {"/project/b.dba", "print 2"},
        {"/project/include.dba", "print 3"},
    };
    const toolchain::ActiveRunPlan Plan = toolchain::PlanActiveRun("/project/b.dba", Project, std::move(Sources));

    REQUIRE(Plan);
    REQUIRE(Plan.Document);
    CHECK(Plan.Document->LogicalPath == "/project/b.dba");
    CHECK(Plan.Document->Profile == dbc::language::LanguageProfile::DbPro);
    REQUIRE(Plan.Document->Project);
    CHECK(Plan.Document->Project->EntrySource == "/project/b.dba");
    CHECK(Plan.Document->Project->OrderedSources == std::vector<std::filesystem::path>{"/project/b.dba"});
    CHECK(Plan.Document->Project->DescriptorPath == Project.DescriptorPath);
    CHECK(Plan.Document->Project->Startup.Depth == 16);
    CHECK(Plan.Document->Project->Startup.Title == "Configured title");
    CHECK(Plan.Document->Sources.size() == 2);
    CHECK(Plan.AssetRoot == "/project/media");
    CHECK(Plan.Blockers == Project.RunBlockers);
}

TEST_CASE("Standalone active run derives its root from the active source")
{
    const auto Project = SourceProject("/project/a.dba");
    const toolchain::ActiveRunPlan Plan = toolchain::PlanActiveRun("/other/b.dba", Project, {});

    REQUIRE(Plan);
    REQUIRE(Plan.Document->Project);
    CHECK(Plan.Document->Project->ProjectRoot == "/other");
    CHECK(Plan.Document->Project->AssetRoot == "/other");
    CHECK(Plan.Document->Project->OrderedSources == std::vector<std::filesystem::path>{"/other/b.dba"});
    CHECK(Plan.AssetRoot == "/other");
}
