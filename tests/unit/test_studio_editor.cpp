#include <doctest/doctest.h>

#include "dba/command_database.hpp"
#include "dba/reference.hpp"
#include "panels/editor_panel.hpp"

#include <filesystem>
#include <fstream>

#include <imgui.h>

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

TEST_CASE("Studio editor tabs take generated text as an undoable edit")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-replace";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    std::ofstream(Root / "main.dba") << "print 1\n";

    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    panels::EditorPanel Editor;
    Editor.Initialize(&Commands);

    CHECK_FALSE(Editor.OpenText(Root / "main.dba").has_value());
    CHECK_FALSE(Editor.ReplaceText(Root / "main.dba", "print 2\n", false));
    REQUIRE(Editor.Open(Root / "main.dba"));
    CHECK(Editor.OpenText(Root / "main.dba") == "print 1\n");

    REQUIRE(Editor.ReplaceText(Root / "main.dba", "LoadLevel_A()\nprint 1\n", false));
    CHECK(Editor.OpenText(Root / "main.dba") == "LoadLevel_A()\nprint 1\n");
    CHECK(Editor.AnyDirty());
    REQUIRE(Editor.CurrentEditor()->CanUndo());
    Editor.CurrentEditor()->Undo();
    CHECK(Editor.OpenText(Root / "main.dba") == "print 1\n");
    CHECK_FALSE(Editor.AnyDirty());

    REQUIRE(Editor.ReplaceText(Root / "main.dba", "print 3\n", true));
    CHECK_FALSE(Editor.AnyDirty());

    std::filesystem::remove_all(Root, Error);
}

TEST_CASE("Studio editor brings an already open tab to the front when it is opened again")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-focus";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    std::ofstream(Root / "a.dba") << "print 1\n";
    std::ofstream(Root / "b.dba") << "print 2\n";

    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.DisplaySize = ImVec2(800.0f, 600.0f);
    Io.IniFilename = nullptr;
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    const auto Frame = [](panels::EditorPanel& Editor)
    {
        ImGui::NewFrame();
        ImGui::Begin("Editor");
        Editor.Draw();
        ImGui::End();
        ImGui::Render();
    };

    {
        dba::Reference Reference;
        dba::CommandDatabase Commands;
        Commands.Build(Reference);
        panels::EditorPanel Editor;
        Editor.Initialize(&Commands);
        REQUIRE(Editor.Open(Root / "a.dba"));
        Frame(Editor);
        REQUIRE(Editor.Open(Root / "b.dba"));
        for (int Index = 0; Index < 3; ++Index)
            Frame(Editor);
        CHECK(Editor.CurrentPath().filename() == "b.dba");

        REQUIRE(Editor.Open(Root / "a.dba"));
        for (int Index = 0; Index < 3; ++Index)
            Frame(Editor);
        CHECK(Editor.CurrentPath().filename() == "a.dba");
    }
    ImGui::DestroyContext();
    std::filesystem::remove_all(Root, Error);
}
