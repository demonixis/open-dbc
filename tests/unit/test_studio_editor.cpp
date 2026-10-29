#include <doctest/doctest.h>

#include "core/recovery.hpp"
#include "core/text_encoding.hpp"
#include "dba/command_database.hpp"
#include "dba/reference.hpp"
#include "panels/editor_panel.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

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

TEST_CASE("Studio text codec keeps Windows-1252, CRLF and BOM files byte for byte")
{
    std::string Every;
    for (int Byte = 1; Byte < 256; ++Byte)
        Every.push_back(static_cast<char>(Byte));
    for (const std::string& Bytes :
         {Every, std::string("print \"caf\xE9\"\r\nend\r\n"), std::string("\xEF\xBB\xBFprint \"caf\xC3\xA9\"\nend\n"),
          std::string("print 1\r\nprint 2\r\n")})
    {
        core::TextFormat Format;
        std::string Text = core::DecodeText(Bytes, Format);
        // The editor drops carriage returns; saving restores them.
        std::erase(Text, '\r');
        const std::optional<std::string> Saved = core::EncodeText(Text, Format);
        REQUIRE(Saved);
        // A lone CR in the 255-byte sample is the one change a line-based editor makes.
        if (Bytes == Every)
            CHECK(*Saved == std::string(Every).erase(12, 1));
        else
            CHECK(*Saved == Bytes);
    }

    core::TextFormat Format;
    CHECK(core::DecodeText("caf\xE9\r\n", Format) == "caf\xC3\xA9\r\n");
    CHECK(Format.Windows1252);
    CHECK(Format.Crlf);
    CHECK_FALSE(Format.Bom);
    CHECK(core::DecodeText("\x80", Format) == "\xE2\x82\xAC");

    CHECK(core::DecodeText("plain\n", Format) == "plain\n");
    CHECK_FALSE(Format.Windows1252);
    CHECK_FALSE(Format.Crlf);

    Format.Windows1252 = true;
    CHECK_FALSE(core::EncodeText("\xE2\x86\x92", Format));
}

TEST_CASE("Studio editor saves a file in the format it was opened in")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-encoding";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    const std::string Original = "print \"caf\xE9\"\r\nend\r\n";
    std::ofstream(Root / "main.dba", std::ios::binary) << Original;
    const auto Read = [&]
    {
        std::ifstream Input(Root / "main.dba", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(Input), {});
    };

    panels::EditorPanel Editor;
    Editor.Initialize(nullptr);
    REQUIRE(Editor.Open(Root / "main.dba"));
    CHECK(Editor.CurrentEditor()->GetText() == "print \"caf\xC3\xA9\"\nend\n");
    REQUIRE(Editor.SaveCurrent());
    CHECK(Read() == Original);

    // An unsaved run compiles the same bytes as the saved file.
    REQUIRE(Editor.ReplaceText(Root / "main.dba", "print \"\xC3\xA9t\xC3\xA9\"\nend", false));
    const std::vector<panels::BufferedSource> Dirty = Editor.DirtySources(dbc::language::LanguageProfile::Dbc);
    REQUIRE(Dirty.size() == 1);
    CHECK(Dirty.front().Text == "print \"\xE9t\xE9\"\r\nend");

    // A character Windows-1252 cannot store turns the file into UTF-8, with a notice.
    REQUIRE(Editor.ReplaceText(Root / "main.dba", "print \"\xE2\x86\x92\"\nend", false));
    REQUIRE(Editor.SaveCurrent());
    CHECK(Read() == "print \"\xE2\x86\x92\"\r\nend");
    CHECK(Editor.TakeNotices().size() == 1);
    CHECK(Editor.TakeNotices().empty());
}

TEST_CASE("Recovery copies follow the unsaved tabs and survive for the next Studio")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-recovery";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root / "project", Error);
    REQUIRE_FALSE(Error);
    std::ofstream(Root / "project" / "main.dba", std::ios::binary) << "print 1\n";
    const std::filesystem::path Copies = Root / "recovery";
    const auto CountCopies = [&]
    {
        std::error_code ListError;
        return std::distance(std::filesystem::directory_iterator(Copies, ListError),
                             std::filesystem::directory_iterator());
    };

    panels::EditorPanel Editor;
    Editor.Initialize(nullptr);
    REQUIRE(Editor.Open(Root / "project" / "main.dba"));
    CHECK(Editor.UnsavedTexts().empty());
    REQUIRE(Editor.ReplaceText(Root / "project" / "main.dba", "print \"caf\xC3\xA9\"\n", false));
    Editor.NewFile();
    Editor.CurrentEditor()->ReplaceTextInCurrentCursor("x = 1\n");
    const std::vector<core::UnsavedText> Unsaved = Editor.UnsavedTexts();
    REQUIRE(Unsaved.size() == 2);

    core::Recovery Writer(Copies);
    std::string WriteError;
    REQUIRE(Writer.Update(Unsaved, WriteError));
    CHECK(CountCopies() == 2);
    CHECK(Writer.Pending().empty());

    // A Studio started after a crash finds both copies.
    const std::vector<core::RecoveryCopy> Found = core::Recovery(Copies).Pending();
    REQUIRE(Found.size() == 2);
    const auto Saved =
        std::find_if(Found.begin(), Found.end(), [](const auto& Copy) { return !Copy.Content.Path.empty(); });
    REQUIRE(Saved != Found.end());
    CHECK(Saved->Content.Path == Unsaved.front().Path);
    CHECK(Saved->Content.Text == "print \"caf\xC3\xA9\"\n");

    panels::EditorPanel Restored;
    Restored.Initialize(nullptr);
    for (const core::RecoveryCopy& Copy : Found)
        Restored.OpenRecovered(Copy.Content);
    const std::vector<core::UnsavedText> Reopened = Restored.UnsavedTexts();
    REQUIRE(Reopened.size() == 2);
    CHECK(std::any_of(Reopened.begin(), Reopened.end(), [&](const auto& Value)
                      { return Value.Path == Unsaved.front().Path && Value.Text == Unsaved.front().Text; }));
    CHECK(std::any_of(Reopened.begin(), Reopened.end(),
                      [&](const auto& Value) { return Value.Path.empty() && Value.Text == Unsaved.back().Text; }));

    // Saved or closed tabs lose their copy, and a normal quit removes the rest.
    REQUIRE(Writer.Update({Unsaved.front()}, WriteError));
    CHECK(CountCopies() == 1);
    Writer.Clear();
    CHECK(CountCopies() == 0);
}
