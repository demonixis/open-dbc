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
#include <imgui_internal.h>

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
    CHECK(panels::EditorPanel::CanOpen(Root / "game.dbpro"));
    CHECK(panels::EditorPanel::CanOpen(Root / ".opendbc-project.json"));
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

    // A descriptor is a text like an effect: never part of a BASIC run, nor saved under another kind's name.
    std::ofstream(Root / "game.dbpro") << "[Project]\n";
    REQUIRE(Editor.Open(Root / "game.dbpro"));
    CHECK_FALSE(Editor.CurrentIsBasic());
    CHECK(Editor.CurrentEditor()->GetText().find("[Project]") != std::string::npos);
    CHECK_FALSE(Editor.SaveCurrentAs(Root / "game.dba"));
    CHECK_FALSE(Editor.SaveCurrentAs(Root / "game.fx"));
    CHECK(Editor.SaveCurrentAs(Root / "copy.dbpro"));
    CHECK(Editor.SaveCurrentAs(Root / "copy.json"));

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

namespace
{
    // Runs the real editor and popup through ImGui without a window: only what is
    // typed and pressed goes in, only the text and the cursor come out.
    class CompletionHarness
    {
      public:
        // An untitled BASIC tab unless a file is given.
        explicit CompletionHarness(const std::filesystem::path& File = {})
        {
            ImGui::CreateContext();
            ImGuiIO& Io = ImGui::GetIO();
            Io.DisplaySize = ImVec2(800.0f, 600.0f);
            Io.IniFilename = nullptr;
            Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            // As in the Studio: it competes for Escape, Tab, Enter and the arrows.
            Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            Commands_.Build(Reference_);
            Editor_.Initialize(&Commands_);
            if (File.empty())
                Editor_.NewFile();
            else
                REQUIRE(Editor_.Open(File));
            Frame();
            Editor_.CurrentEditor()->SetFocus();
            Frame();
            Frame();
        }

        ~CompletionHarness()
        {
            ImGui::DestroyContext();
        }

        void Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
            ImGui::Begin("Editor");
            Editor_.Draw();
            ImGui::End();
            ImGui::Render();
        }

        void Type(std::string_view Text)
        {
            for (const char Character : Text)
            {
                ImGui::GetIO().AddInputCharacter(static_cast<unsigned char>(Character));
                Frame();
            }
        }

        void Press(ImGuiKey Key)
        {
            ImGui::GetIO().AddKeyEvent(Key, true);
            Frame();
            ImGui::GetIO().AddKeyEvent(Key, false);
            Frame();
        }

        void Click(ImVec2 Position)
        {
            ImGui::GetIO().AddMousePosEvent(Position.x, Position.y);
            Frame();
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            Frame();
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Frame();
            Frame();
        }

        void PressManualTrigger()
        {
            // On macOS ImGui swaps Ctrl and Cmd, so this is the trigger everywhere.
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, true);
            Frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, false);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
            Frame();
        }

        std::string Text()
        {
            return Widget().GetText();
        }

        TextEditor& Widget()
        {
            return *Editor_.CurrentEditor();
        }

        const ImGuiWindow* EditorWindow() const
        {
            for (const ImGuiWindow* Window : ImGui::GetCurrentContext()->Windows)
            {
                if (std::string_view(Window->Name).find("##editor") != std::string_view::npos)
                    return Window;
            }
            return nullptr;
        }

        const ImGuiWindow* Popup() const
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName("##completion");
            return Window != nullptr && Window->Active ? Window : nullptr;
        }

        const ImGuiWindow* Hint() const
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName("##parameter-hint");
            return Window != nullptr && Window->Active ? Window : nullptr;
        }

        void PressHintTrigger()
        {
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, true);
            Frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, false);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
            Frame();
        }

      private:
        dba::Reference Reference_;
        dba::CommandDatabase Commands_;
        panels::EditorPanel Editor_;
    };
} // namespace

TEST_CASE("Studio completion replaces every word of a command")
{
    CompletionHarness Harness;
    Harness.Type("set window ti");
    Harness.Press(ImGuiKey_Enter);
    CHECK(Harness.Text() == "set window title");

    // One undoable edit brings back what was typed, and typing resumes after the name.
    Harness.Widget().Undo();
    CHECK(Harness.Text() == "set window ti");
    Harness.Widget().Redo();
    Harness.Type(" ");
    CHECK(Harness.Text() == "set window title ");
}

TEST_CASE("Studio completion lists the next words after a space and takes the arrows")
{
    CompletionHarness Harness;
    Harness.Type("print 1");
    Harness.Press(ImGuiKey_Enter);
    Harness.Type("  set window ");
    const size_t Line = Harness.Widget().GetMainCursorPosition().line;

    Harness.Press(ImGuiKey_DownArrow);
    Harness.Press(ImGuiKey_DownArrow);
    Harness.Press(ImGuiKey_UpArrow);
    CHECK(Harness.Widget().GetMainCursorPosition().line == Line);
    Harness.Press(ImGuiKey_Tab);
    CHECK(Harness.Text() == "print 1\n  set window layout");
}

TEST_CASE("Studio completion opens under the words it completes and only when a name can continue")
{
    CompletionHarness Harness;
    CHECK(Harness.Popup() == nullptr);

    // Eleven lines widen the line numbers, which moves the text.
    for (int Line = 0; Line < 10; ++Line)
    {
        Harness.Type("end");
        Harness.Press(ImGuiKey_Escape);
        Harness.Press(ImGuiKey_Enter);
    }
    Harness.Type("    set window ");
    const ImGuiWindow* Popup = Harness.Popup();
    REQUIRE(Popup != nullptr);
    TextEditor& Widget = Harness.Widget();
    const ImVec2 Corner(Popup->Pos.x + Widget.GetGlyphWidth() * 0.5f, Popup->Pos.y - Widget.GetLineHeight() * 0.5f);
    CHECK(Widget.GetWordAtMousePos(Corner) == "set");
    CHECK(ImGui::GetCurrentContext()->NavWindow != Popup);

    // Deleting never opens the list: only typing does.
    Harness.Type("zz");
    CHECK(Harness.Popup() == nullptr);
    Harness.Press(ImGuiKey_Backspace);
    Harness.Press(ImGuiKey_Backspace);
    CHECK(Harness.Popup() == nullptr);
    Harness.Type("t");
    CHECK(Harness.Popup() != nullptr);
    Harness.Press(ImGuiKey_Escape);
    CHECK(Harness.Popup() == nullptr);

    Harness.Press(ImGuiKey_Enter);
    Harness.Type("print ");
    CHECK(Harness.Popup() == nullptr);
    Harness.Type("set ");
    CHECK(Harness.Popup() != nullptr);
    Harness.Widget().SetCursor(TextEditor::DocPos(0, 0));
    Harness.Frame();
    CHECK(Harness.Popup() == nullptr);
}

TEST_CASE("Studio completion leaves Enter and Tab alone without a selection")
{
    SUBCASE("After a space")
    {
        CompletionHarness Harness;
        Harness.Type("set window ");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "set window \n");
    }
    SUBCASE("A single letter is a variable until Tab says otherwise")
    {
        CompletionHarness Harness;
        Harness.Type("x");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "x\n");
    }
    SUBCASE("A loose match")
    {
        CompletionHarness Harness;
        Harness.Type("title");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "title\n");
    }
    SUBCASE("Tab accepts a loose match")
    {
        CompletionHarness Harness;
        Harness.Type("swt");
        Harness.Press(ImGuiKey_Tab);
        CHECK(Harness.Text() == "set window title");
    }
    SUBCASE("A name no command starts with")
    {
        CompletionHarness Harness;
        Harness.Type("qqqq");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "qqqq\n");
    }
}

TEST_CASE("Studio completion takes the mouse and gives the keyboard back")
{
    SUBCASE("Clicking a row accepts it")
    {
        CompletionHarness Harness;
        Harness.Type("set window ");
        const ImGuiWindow* Popup = Harness.Popup();
        REQUIRE(Popup != nullptr);
        Harness.Click(ImVec2(Popup->Pos.x + 20.0f, Popup->Pos.y + ImGui::GetFrameHeight() * 1.5f));
        CHECK(Harness.Text() == "set window off");
        CHECK(Harness.Popup() == nullptr);
        Harness.Type(" ");
        CHECK(Harness.Text() == "set window off ");
    }
    SUBCASE("Clicking the help leaves the list open and typing goes to the editor")
    {
        CompletionHarness Harness;
        Harness.Type("set w");
        const ImGuiWindow* Popup = Harness.Popup();
        REQUIRE(Popup != nullptr);
        Harness.Click(ImVec2(Popup->Pos.x + Popup->Size.x - 20.0f, Popup->Pos.y + Popup->Size.y - 10.0f));
        CHECK(Harness.Popup() != nullptr);
        Harness.Type("indow ti");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "set window title");
    }
}

TEST_CASE("Studio completion closes on Escape and keeps the focus")
{
    CompletionHarness Harness;
    Harness.Type("set w");
    Harness.Press(ImGuiKey_Escape);
    Harness.Press(ImGuiKey_Enter);
    CHECK(Harness.Text() == "set w\n");
    Harness.Type("x");
    CHECK(Harness.Text() == "set w\nx");
}

TEST_CASE("Studio completion stays out of comments, strings and effect tabs")
{
    SUBCASE("Comment")
    {
        CompletionHarness Harness;
        Harness.Type("rem set w");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "rem set w\n");
    }
    SUBCASE("Block comment")
    {
        CompletionHarness Harness;
        Harness.Type("remstart");
        Harness.Press(ImGuiKey_Enter);
        Harness.Type("set w");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "remstart\nset w\n");
    }
    SUBCASE("String")
    {
        CompletionHarness Harness;
        Harness.Type("print \"set w");
        Harness.Press(ImGuiKey_Enter);
        // The editor closes the quote by itself, so Enter splits the string.
        CHECK(Harness.Text() == "print \"set w\n\"");
    }
    SUBCASE("Effect")
    {
        const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-completion";
        std::error_code Error;
        std::filesystem::remove_all(Root, Error);
        std::filesystem::create_directories(Root, Error);
        REQUIRE_FALSE(Error);
        std::ofstream{Root / "main.fx"};
        {
            CompletionHarness Harness(Root / "main.fx");
            Harness.Type("set w");
            Harness.Press(ImGuiKey_Enter);
            CHECK(Harness.Text() == "set w\n");
        }
        std::filesystem::remove_all(Root, Error);
    }
}

TEST_CASE("Studio completion follows the cursor and opens on demand")
{
    SUBCASE("Backspace narrows the words again")
    {
        CompletionHarness Harness;
        Harness.Type("set windoz");
        Harness.Press(ImGuiKey_Backspace);
        Harness.Type("w ti");
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "set window title");
    }
    SUBCASE("Accepting replaces the rest of the word under the cursor")
    {
        CompletionHarness Harness;
        Harness.Type("set window tixx");
        Harness.Press(ImGuiKey_LeftArrow);
        Harness.Press(ImGuiKey_LeftArrow);
        Harness.PressManualTrigger();
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "set window title");
    }
    SUBCASE("Ctrl+Space lists everything on an empty line")
    {
        CompletionHarness Harness;
        Harness.PressManualTrigger();
        Harness.Press(ImGuiKey_DownArrow);
        Harness.Press(ImGuiKey_Enter);
        CHECK_FALSE(Harness.Text().empty());
        CHECK(Harness.Text().find('\n') == std::string::npos);
    }
    SUBCASE("Leaving the line closes the list")
    {
        CompletionHarness Harness;
        Harness.Type("print 1");
        Harness.Press(ImGuiKey_Enter);
        Harness.Type("set w");
        Harness.Widget().SetCursor(TextEditor::DocPos(0, 0));
        Harness.Frame();
        Harness.Press(ImGuiKey_Enter);
        CHECK(Harness.Text() == "\nprint 1\nset w");
    }
}

TEST_CASE("Studio editor finds and replaces without touching what does not match")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-find";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    const std::filesystem::path Path = Root / "main.dba";
    std::ofstream(Path) << "alpha beta\nAlpha gamma\n";

    dba::Reference Reference;
    dba::CommandDatabase Commands;
    Commands.Build(Reference);
    panels::EditorPanel Editor;
    Editor.Initialize(&Commands);
    REQUIRE(Editor.Open(Path));
    TextEditor& Text = *Editor.CurrentEditor();
    const auto Selected = [&] { return Text.GetSectionText(Text.GetMainCursorSelection()); };
    const auto Line = [&] { return Text.GetMainCursorSelection().start.line; };

    // Each letter still matches where the last did, and the search goes round past the end.
    Editor.SetFindText("al");
    CHECK(Selected() == "al");
    Editor.SetFindText("alpha");
    CHECK(Selected() == "alpha");
    CHECK(Line() == 0);
    CHECK(Editor.FindNext());
    CHECK(Selected() == "Alpha");
    CHECK(Line() == 1);
    CHECK(Editor.FindNext());
    CHECK(Line() == 0);

    // A word in the text is taken up, not one that goes past the cursor's line.
    Text.SetCursor(TextEditor::DocPos(1, 8));
    Editor.OpenFind(false);
    Editor.FindNext();
    CHECK(Selected() == "gamma");
    Text.SelectAll();
    Editor.OpenFind(false);
    Text.SetCursor(TextEditor::DocPos(0, 0));
    CHECK(Editor.FindNext());
    CHECK(Selected() == "gamma");

    // No match leaves no selection for a replacement to take.
    Text.SelectAll();
    Editor.SetFindText("missing");
    CHECK(Editor.FindFailed());
    CHECK_FALSE(Text.MainCursorHasSelection());
    Text.SelectAll();
    Editor.SetReplaceText("oops");
    Editor.ReplaceAll();
    CHECK(Editor.FindFailed());
    CHECK(Editor.OpenText(Path) == "alpha beta\nAlpha gamma\n");
    Text.SelectAll();
    Editor.ReplaceCurrent();
    CHECK(Editor.OpenText(Path) == "alpha beta\nAlpha gamma\n");

    // A selection other than a match is kept: Replace first goes to a match.
    Editor.SetFindText("alpha");
    Text.SetCursor(TextEditor::DocPos(0, 6));
    Editor.SetReplaceText("delta");
    Editor.ReplaceCurrent();
    CHECK(Editor.OpenText(Path) == "alpha beta\nAlpha gamma\n");
    CHECK(Line() == 1);
    Editor.ReplaceCurrent();
    CHECK(Editor.OpenText(Path) == "alpha beta\ndelta gamma\n");
    CHECK(Line() == 0);
    CHECK(Selected() == "alpha");
    Text.Undo();
    CHECK(Editor.OpenText(Path) == "alpha beta\nAlpha gamma\n");

    // One undo step, and an empty replacement deletes.
    Editor.SetReplaceText("");
    Editor.ReplaceAll();
    CHECK(Editor.OpenText(Path) == " beta\n gamma\n");
    Text.Undo();
    CHECK(Editor.OpenText(Path) == "alpha beta\nAlpha gamma\n");

    std::filesystem::remove_all(Root, Error);
}

TEST_CASE("Studio find bar takes the keyboard: Enter goes to the next match instead of typing a line break")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-find-bar";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root, Error);
    REQUIRE_FALSE(Error);
    const std::filesystem::path Path = Root / "main.dba";
    const std::string Source = "alpha beta\nAlpha gamma\n";
    std::ofstream(Path) << Source;

    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.DisplaySize = ImVec2(800.0f, 600.0f);
    Io.IniFilename = nullptr;
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    Io.ConfigMacOSXBehaviors = false;
    // As in the Studio: it competes for Enter and Escape.
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    {
        dba::Reference Reference;
        dba::CommandDatabase Commands;
        Commands.Build(Reference);
        panels::EditorPanel Editor;
        Editor.Initialize(&Commands);
        REQUIRE(Editor.Open(Path));
        TextEditor& Text = *Editor.CurrentEditor();

        const auto Frames = [&](int Count)
        {
            for (int Index = 0; Index < Count; ++Index)
            {
                ImGui::NewFrame();
                ImGui::Begin("Editor");
                Editor.Draw();
                ImGui::End();
                ImGui::Render();
            }
        };
        const auto Press = [&](ImGuiKey Key, bool Control = false, bool Shift = false)
        {
            Io.AddKeyEvent(ImGuiMod_Ctrl, Control);
            Io.AddKeyEvent(ImGuiMod_Shift, Shift);
            Frames(1);
            Io.AddKeyEvent(Key, true);
            Frames(1);
            Io.AddKeyEvent(Key, false);
            Io.AddKeyEvent(ImGuiMod_Ctrl, false);
            Io.AddKeyEvent(ImGuiMod_Shift, false);
            Frames(2);
        };
        const auto Type = [&](const char* Characters)
        {
            Io.AddInputCharactersUTF8(Characters);
            Frames(2);
        };
        const auto Selected = [&] { return Text.GetSectionText(Text.GetMainCursorSelection()); };
        const auto Line = [&] { return Text.GetMainCursorSelection().start.line; };

        Text.SetFocus();
        Frames(4);

        // The bar opens with the keyboard in its field, not in the text.
        Press(ImGuiKey_R, true);
        CHECK(Editor.FindVisible());
        CHECK_FALSE(Editor.FindReplacing());
        Type("alp");
        CHECK(Selected() == "alp");
        Type("ha");
        CHECK(Selected() == "alpha");
        CHECK(Line() == 0);
        CHECK(Editor.OpenText(Path) == Source);

        Press(ImGuiKey_Enter);
        CHECK(Editor.OpenText(Path) == Source);
        CHECK(Selected() == "Alpha");
        CHECK(Line() == 1);
        Press(ImGuiKey_Enter);
        CHECK(Editor.OpenText(Path) == Source);
        CHECK(Line() == 0);

        // Enter leaves the keyboard in the field, whose text is selected.
        Type("missing");
        CHECK(Editor.FindFailed());
        CHECK(Editor.OpenText(Path) == Source);
        Press(ImGuiKey_Enter);
        CHECK(Editor.FindFailed());
        CHECK(Editor.OpenText(Path) == Source);

        Press(ImGuiKey_Escape);
        CHECK_FALSE(Editor.FindVisible());
        CHECK_FALSE(Editor.FindFailed());

        Press(ImGuiKey_R, true, true);
        CHECK(Editor.FindReplacing());
        Press(ImGuiKey_Escape);
        Frames(2);

        // Ctrl+F opens this bar as well, and the widget's own find window stays shut.
        Text.SetCursor(TextEditor::DocPos(0, 7));
        Frames(2);
        Press(ImGuiKey_F, true);
        CHECK(Editor.FindVisible());
        CHECK_FALSE(Editor.FindReplacing());
        CHECK_FALSE(Text.HasFindString());
    }
    ImGui::DestroyContext();
    std::filesystem::remove_all(Root, Error);
}

TEST_CASE("Studio editor keeps the cursor in view while typing")
{
    CompletionHarness Harness;
    TextEditor& Widget = Harness.Widget();
    const auto LineInView = [&]
    {
        const ImGuiWindow* Window = Harness.EditorWindow();
        REQUIRE(Window != nullptr);
        const float Top = Window->DC.CursorStartPos.y +
                          static_cast<float>(Widget.GetMainCursorPosition().line) * Widget.GetLineHeight();
        return Top >= Window->Pos.y && Top + Widget.GetLineHeight() <= Window->Pos.y + Window->Size.y;
    };

    // Past the bottom of the view, where the list could not open either.
    for (int Line = 0; Line < 100; ++Line)
        Harness.Press(ImGuiKey_Enter);
    Harness.Frame();
    CHECK(LineInView());
    Harness.Type("sy");
    CHECK(Harness.Popup() != nullptr);
    Harness.Press(ImGuiKey_Escape);

    // Past the right edge, without jumping back to the start of the line on each letter.
    Harness.Type(std::string(300, 'x'));
    Harness.Frame();
    const ImGuiWindow* Window = Harness.EditorWindow();
    const float CursorX = static_cast<float>(Widget.GetMainCursorPosition().index) * Widget.GetGlyphWidth();
    CHECK(Window->Scroll.x > 0.0f);
    CHECK(CursorX - Window->Scroll.x < Window->Size.x);
    CHECK(LineInView());

    // Typing on a line already in view moves nothing.
    Widget.SetCursor(TextEditor::DocPos(90, 0));
    Harness.Frame();
    Harness.Frame();
    const float ScrollY = Window->Scroll.y;
    Harness.Type("a");
    Harness.Frame();
    CHECK(Window->Scroll.y == ScrollY);
}

TEST_CASE("Studio editor types over a closer instead of doubling it")
{
    CompletionHarness Harness;
    TextEditor& Widget = Harness.Widget();
    const auto Typed = [&](std::string_view Keys)
    {
        Widget.SetText("");
        Harness.Frame();
        Harness.Type(Keys);
        return Harness.Text();
    };
    const auto PlaceCursor = [&](std::string_view Line, size_t Index)
    {
        Widget.SetText(std::string(Line));
        Widget.SetCursor(TextEditor::DocPos(0, Index));
        Harness.Frame();
        Harness.Frame();
    };

    // What ends up in the file is what was typed, whatever the editor closed by itself.
    for (const std::string_view Line :
         {"x = rgb(1, 2, 3)", "x = rgb(abs(1), 2)", "a(1) = b(2)", "print \"hello\"", "print \"a\" + \"b\"",
          "print \"(\" + str$(1)", "print \"don't\"", "rem don't", "x = rgb()"})
    {
        CAPTURE(Line);
        CHECK(Typed(Line) == Line);
    }

    // Stepping out of a call ends its hint.
    Typed("x = rgb(1)");
    CHECK(Harness.Hint() == nullptr);

    // A correction on the way changes nothing.
    Typed("x = rgb(12");
    Harness.Press(ImGuiKey_Backspace);
    Harness.Type(")");
    CHECK(Harness.Text() == "x = rgb(1)");

    // A closer already in the file counts too, and stepping over it is no edit.
    PlaceCursor("x = rgb(1)", 9);
    const size_t UndoIndex = Widget.GetUndoIndex();
    Harness.Type(")");
    CHECK(Harness.Text() == "x = rgb(1)");
    CHECK(Widget.GetMainCursorPosition().index == 10);
    CHECK(Widget.GetUndoIndex() == UndoIndex);

    // Brackets in strings do not count, and a closer that is needed still goes in.
    const std::string_view InString = "print \"(\" + str$(1)";
    PlaceCursor(InString, InString.size() - 1);
    Harness.Type(")");
    CHECK(Harness.Text() == InString);
    PlaceCursor("x = rgb((1)", 10);
    Harness.Type(")");
    CHECK(Harness.Text() == "x = rgb((1))");
    PlaceCursor("print \"abc", 10);
    Harness.Type("\"");
    CHECK(Harness.Text() == "print \"abc\"");

    // Outside strings, and around a selection, pairs still complete.
    CHECK(Typed("x = (") == "x = ()");
    Widget.SetText("print \"abc\"");
    Widget.SelectRegion(TextEditor::DocPos(0, 7), TextEditor::DocPos(0, 10));
    Harness.Frame();
    Harness.Type("(");
    CHECK(Harness.Text() == "print \"(abc)\"");
}

TEST_CASE("Studio parameter hint sits above the call without taking the keyboard")
{
    CompletionHarness Harness;
    CHECK(Harness.Hint() == nullptr);
    Harness.Type("sync");
    Harness.Press(ImGuiKey_Escape);
    Harness.Press(ImGuiKey_Enter);
    Harness.Type("  position object 1, ");
    Harness.Frame();
    const ImGuiWindow* Hint = Harness.Hint();
    REQUIRE(Hint != nullptr);

    // Its bottom left corner touches the name, on the line above the text.
    TextEditor& Widget = Harness.Widget();
    const ImVec2 Below(Hint->Pos.x + Widget.GetGlyphWidth() * 0.5f,
                       Hint->Pos.y + Hint->Size.y + Widget.GetLineHeight() * 0.5f);
    CHECK(Widget.GetWordAtMousePos(Below) == "position");
    CHECK(ImGui::GetCurrentContext()->NavWindow != Hint);

    // The arrows and Enter still belong to the editor, and leaving the line closes it.
    const size_t Line = Widget.GetMainCursorPosition().line;
    Harness.Press(ImGuiKey_UpArrow);
    CHECK(Widget.GetMainCursorPosition().line == Line - 1);
    CHECK(Harness.Hint() == nullptr);
    Harness.Press(ImGuiKey_DownArrow);
    CHECK(Widget.GetMainCursorPosition().line == Line);
    CHECK(Harness.Hint() == nullptr);

    // Asking brings it back on a line left alone.
    Widget.SetCursor(TextEditor::DocPos(Line, 20));
    Harness.Frame();
    Harness.PressHintTrigger();
    CHECK(Harness.Hint() != nullptr);
    CHECK(Harness.Text() == "sync\n  position object 1, ");
    Harness.Press(ImGuiKey_Enter);
    CHECK(Harness.Hint() == nullptr);
}

TEST_CASE("Studio parameter hint follows its call")
{
    CompletionHarness Harness;
    Harness.Type("x = rgb(1");
    CHECK(Harness.Hint() != nullptr);

    // Moving inside the call keeps it; moving out of the name closes it for good.
    Harness.Press(ImGuiKey_LeftArrow);
    CHECK(Harness.Hint() != nullptr);
    Harness.Press(ImGuiKey_LeftArrow);
    CHECK(Harness.Hint() == nullptr);
    Harness.Press(ImGuiKey_RightArrow);
    CHECK(Harness.Hint() == nullptr);

    // Closing the parenthesis ends the call.
    Harness.Press(ImGuiKey_End);
    Harness.Type("x");
    CHECK(Harness.Hint() == nullptr);
}

TEST_CASE("Studio parameter hint shares Escape with the list")
{
    CompletionHarness Harness;
    Harness.Type("position object 1, s");
    REQUIRE(Harness.Popup() != nullptr);
    REQUIRE(Harness.Hint() != nullptr);

    // The list goes first, then the hint, and the focus stays.
    Harness.Press(ImGuiKey_Escape);
    CHECK(Harness.Popup() == nullptr);
    CHECK(Harness.Hint() != nullptr);
    Harness.Press(ImGuiKey_Escape);
    CHECK(Harness.Hint() == nullptr);

    // Typing does not bring it back until the next argument.
    Harness.Type("1");
    CHECK(Harness.Hint() == nullptr);
    Harness.Type(",");
    CHECK(Harness.Hint() != nullptr);
    CHECK(Harness.Text() == "position object 1, s1,");
}

TEST_CASE("Studio parameter hint and list share the space around the line")
{
    CompletionHarness Harness;
    TextEditor& Widget = Harness.Widget();

    // Four overloads do not fit above the first line: the list wins while it is open,
    // and the hint then takes its place under the line.
    Harness.Type("play animation 1, s");
    REQUIRE(Harness.Popup() != nullptr);
    CHECK(Harness.Hint() == nullptr);
    Harness.Press(ImGuiKey_Escape);
    const ImGuiWindow* Below = Harness.Hint();
    REQUIRE(Below != nullptr);
    const ImVec2 Over(Below->Pos.x + Widget.GetGlyphWidth() * 0.5f, Below->Pos.y - Widget.GetLineHeight() * 0.5f);
    CHECK(Widget.GetWordAtMousePos(Over) == "play");

    // Far down the window the list flips above the line, and the hint above the list.
    const float Lines = ImGui::GetIO().DisplaySize.y / Widget.GetLineHeight();
    Widget.SetText(std::string(static_cast<size_t>(Lines * 0.7f), '\n'));
    Widget.SetCursor(TextEditor::DocPos(Widget.GetLineCount() - 1, 0));
    Harness.Frame();
    Harness.Frame();
    Harness.Type("position object 1, s");
    const ImGuiWindow* List = Harness.Popup();
    const ImGuiWindow* Above = Harness.Hint();
    REQUIRE(List != nullptr);
    REQUIRE(Above != nullptr);
    CHECK(Above->Pos.y + Above->Size.y <= List->Pos.y);
    CHECK(List->Pos.y + List->Size.y < ImGui::GetIO().DisplaySize.y);
}

TEST_CASE("Studio parameter hint counts glyphs, not bytes, before the name")
{
    CompletionHarness Harness;
    Harness.Type("s$ = \"caf");
    ImGui::GetIO().AddInputCharactersUTF8("\xC3\xA9");
    Harness.Frame();
    Harness.Type("\" : set window title ");
    Harness.Frame();
    const ImGuiWindow* Hint = Harness.Hint();
    REQUIRE(Hint != nullptr);
    TextEditor& Widget = Harness.Widget();
    const ImVec2 Below(Hint->Pos.x + Widget.GetGlyphWidth() * 0.5f,
                       Hint->Pos.y + Hint->Size.y + Widget.GetLineHeight() * 0.5f);
    CHECK(Widget.GetWordAtMousePos(Below) == "set");
}

TEST_CASE("Studio parameter hint stays out of comments and effect tabs")
{
    SUBCASE("Comment")
    {
        CompletionHarness Harness;
        Harness.Type("rem position object 1, ");
        CHECK(Harness.Hint() == nullptr);
    }
    SUBCASE("Block comment")
    {
        CompletionHarness Harness;
        Harness.Type("remstart");
        Harness.Press(ImGuiKey_Enter);
        Harness.Type("position object 1, ");
        CHECK(Harness.Hint() == nullptr);
    }
    SUBCASE("Effect")
    {
        const std::filesystem::path Root =
            std::filesystem::temp_directory_path() / "opendbc-test-studio-parameter-hint";
        std::error_code Error;
        std::filesystem::remove_all(Root, Error);
        std::filesystem::create_directories(Root, Error);
        REQUIRE_FALSE(Error);
        std::ofstream{Root / "main.fx"};
        {
            CompletionHarness Harness(Root / "main.fx");
            Harness.Type("position object 1, ");
            CHECK(Harness.Hint() == nullptr);
        }
        std::filesystem::remove_all(Root, Error);
    }
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
