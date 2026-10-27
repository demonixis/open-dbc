#include <doctest/doctest.h>

#include "dba/command_database.hpp"
#include "dba/reference.hpp"
#include "panels/editor_panel.hpp"

#include <filesystem>
#include <fstream>

using namespace dbc::studio;

TEST_CASE("Studio editor opens HLSL effects as plain text tabs")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-editor";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    std::ofstream(Root / "bump.FX") << "float4 PSMain() : SV_Target { return 1; }\n";
    std::ofstream(Root / "main.dba") << "print 1\n";
    std::ofstream(Root / "notes.txt") << "notes\n";

    CHECK(panels::EditorPanel::CanOpen(Root / "bump.FX"));
    CHECK(panels::EditorPanel::CanOpen(Root / "shader.hlsl"));
    CHECK(panels::EditorPanel::CanOpen(Root / "main.dba"));
    CHECK_FALSE(panels::EditorPanel::CanOpen(Root / "notes.txt"));

    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    panels::EditorPanel Editor;
    Editor.Initialize(&Commands);

    CHECK_FALSE(Editor.Open(Root / "notes.txt"));

    REQUIRE(Editor.Open(Root / "bump.FX"));
    CHECK_FALSE(Editor.CurrentIsBasic());
    // An effect tab is never part of a BASIC run, nor saved under a BASIC name.
    CHECK_FALSE(Editor.SaveCurrentAs(Root / "bump.dba"));
    CHECK(Editor.SaveCurrentAs(Root / "copy.hlsl"));
    CHECK(Editor.DirtySources(dbc::language::LanguageProfile::Dbc).empty());

    REQUIRE(Editor.Open(Root / "main.dba"));
    CHECK(Editor.CurrentIsBasic());
    CHECK_FALSE(Editor.SaveCurrentAs(Root / "main.fx"));

    std::filesystem::remove_all(Root, Error);
}
