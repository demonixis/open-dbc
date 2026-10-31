#include "editor_panel.hpp"
#include "core/i18n.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#include <imgui.h>

#include "dba/command_database.hpp"
#include "dba/language.hpp"
#include "language/language_profile.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace
    {
        std::string ReadFile(const std::filesystem::path& Path, bool& Ok)
        {
            std::ifstream In(Path, std::ios::binary);
            if (!In)
            {
                Ok = false;
                return std::string();
            }

            std::ostringstream Buffer;
            Buffer << In.rdbuf();
            Ok = true;
            return Buffer.str();
        }

        std::string LowerExtension(const std::filesystem::path& Path)
        {
            std::string Extension = dbc::PathToUtf8(Path.extension());
            std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Extension;
        }

    } // namespace

    namespace panels
    {
        void EditorPanel::Initialize(const dba::CommandDatabase* Commands)
        {
            Commands_ = Commands;
            Completion_.Initialize(Commands);
        }

        void EditorPanel::SetProfile(dbc::language::LanguageProfile Profile)
        {
            ProjectProfile_ = Profile;
            for (const std::unique_ptr<Document>& Document : Documents_)
            {
                Document->Profile = Profile;
                if (Document->Basic)
                    ConfigureEditor(*Document->Editor, Profile);
            }
            ClearErrorMarkers();
        }

        // Maps the studio theme onto the editor palette so code colors match the
        // rest of the window.
        TextEditor::Palette EditorPalette()
        {
            TextEditor::Palette P = Theme::Current == Theme::Appearance::Light ? TextEditor::GetLightPalette()
                                                                               : TextEditor::GetDarkPalette();
            const auto Set = [&P](TextEditor::Color Slot, ImU32 Color) { P[static_cast<size_t>(Slot)] = Color; };

            Set(TextEditor::Color::text, Theme::Text);
            Set(TextEditor::Color::keyword, Theme::Control);
            Set(TextEditor::Color::declaration, Theme::Keyword);
            Set(TextEditor::Color::number, Theme::Number);
            Set(TextEditor::Color::string, Theme::String);
            Set(TextEditor::Color::punctuation, Theme::Text);
            Set(TextEditor::Color::preprocessor, Theme::Control);
            Set(TextEditor::Color::identifier, Theme::Identifier);
            Set(TextEditor::Color::knownIdentifier, Theme::Keyword);
            Set(TextEditor::Color::comment, Theme::Comment);
            Set(TextEditor::Color::background, Theme::Background);
            Set(TextEditor::Color::cursor, Theme::Text);
            Set(TextEditor::Color::selection, Theme::Selection);
            Set(TextEditor::Color::lineNumber, Theme::LineNumber);
            Set(TextEditor::Color::currentLineNumber, Theme::Text);
            return P;
        }

        void EditorPanel::ConfigureEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile) const
        {
            Editor.SetLanguage(dba::Language(Profile));
            Editor.SetPalette(EditorPalette());
            Editor.SetTabSize(4);
            Editor.SetInsertSpacesOnTabs(true);
            Editor.SetShowLineNumbersEnabled(true);
            Editor.SetShowMatchingBrackets(true);
            Editor.SetAutoIndentEnabled(true);
            Editor.SetShowScrollbarMiniMapEnabled(true);
            Editor.SetShowWhitespacesEnabled(false);

            // Hovering a command shows its signature and description.
            const dba::CommandDatabase* Commands = Commands_;
            const CompletionPopup* Completion = &Completion_;
            TextEditor* Self = &Editor;
            Editor.SetTextHoverCallback(
                [Commands, Completion, Self, Profile](TextEditor::PopupData& Data)
                {
                    // The widget opens the popup for any glyph under the mouse, so a
                    // callback that draws nothing leaves an empty box floating around.
                    // Close it explicitly whenever there is no documentation to show.
                    const dba::Command* CommandValue = nullptr;

                    if (Commands != nullptr && !Completion->IsOpen())
                    {
                        const TextEditor::DocPos Start = Self->FindWordStart(Data.pos);
                        const TextEditor::DocPos End = Self->FindWordEnd(Data.pos);
                        const std::string Word = Self->GetSectionText(Start, End);
                        if (!Word.empty())
                        {
                            CommandValue = Commands->Find(Profile, Word);
                        }
                    }

                    if (CommandValue == nullptr)
                    {
                        ImGui::CloseCurrentPopup();
                        return;
                    }

                    DrawCommandHelp(dba::DescribeCommand(*Commands, Profile, CommandValue->Classic),
                                    ImGui::GetFontSize() * 30.0f);
                });
        }

        void EditorPanel::ConfigureTextEditor(TextEditor& Editor, const std::filesystem::path& Path) const
        {
            if (IsEffectSource(Path))
                Editor.SetLanguage(TextEditor::Language::Hlsl());
            else if (LowerExtension(Path) == ".json")
                Editor.SetLanguage(TextEditor::Language::Json());
            Editor.SetPalette(EditorPalette());
            Editor.SetTabSize(4);
            Editor.SetInsertSpacesOnTabs(true);
            Editor.SetShowLineNumbersEnabled(true);
            Editor.SetShowMatchingBrackets(true);
            Editor.SetAutoIndentEnabled(true);
            Editor.SetShowScrollbarMiniMapEnabled(true);
            Editor.SetShowWhitespacesEnabled(false);
        }

        bool EditorPanel::IsEffectSource(const std::filesystem::path& Path)
        {
            const std::string Extension = LowerExtension(Path);
            return Extension == ".fx" || Extension == ".hlsl";
        }

        bool EditorPanel::IsProjectData(const std::filesystem::path& Path)
        {
            return dbc::language::IsDbProProject(Path) || LowerExtension(Path) == ".json";
        }

        bool EditorPanel::CanOpen(const std::filesystem::path& Path)
        {
            return dbc::language::IsLanguageSource(Path) || IsEffectSource(Path) || IsProjectData(Path);
        }

        void EditorPanel::AddDocument(std::filesystem::path Path, dbc::language::LanguageProfile Profile, bool Basic,
                                      const std::string& Text, const core::TextFormat& Format)
        {
            auto Doc = std::make_unique<Document>();
            Doc->Path = std::move(Path);
            Doc->Profile = Profile;
            Doc->Basic = Basic;
            Doc->Format = Format;
            Doc->Title = Doc->Path.empty() ? ("untitled-" + std::to_string(++UntitledCount_) + ".dba")
                                           : dbc::PathToUtf8(Doc->Path.filename());
            Doc->Editor = std::make_unique<TextEditor>();
            if (Basic)
                ConfigureEditor(*Doc->Editor, Profile);
            else
                ConfigureTextEditor(*Doc->Editor, Doc->Path);
            Doc->Editor->SetText(Text);
            Doc->SavedUndoIndex = Doc->Editor->GetUndoIndex();

            Documents_.push_back(std::move(Doc));
            Current_ = static_cast<int>(Documents_.size()) - 1;
        }

        bool EditorPanel::Open(const std::filesystem::path& Path)
        {
            std::error_code Ec;
            const std::filesystem::path Canonical = std::filesystem::weakly_canonical(Path, Ec);
            const std::filesystem::path& Target = Ec ? Path : Canonical;

            for (size_t I = 0; I < Documents_.size(); ++I)
            {
                if (Documents_[I]->Path == Target)
                {
                    Current_ = static_cast<int>(I);
                    SelectRequest_ = Documents_[I].get();
                    return true;
                }
            }

            if (!CanOpen(Target))
            {
                return false;
            }

            bool Ok = false;
            const std::string Bytes = ReadFile(Target, Ok);
            if (!Ok)
            {
                return false;
            }
            core::TextFormat Format;
            const std::string Text = core::DecodeText(Bytes, Format);
            AddDocument(Target, ProjectProfile_, dbc::language::IsLanguageSource(Target), Text, Format);
            return true;
        }

        EditorPanel::Document* EditorPanel::Find(const std::filesystem::path& Path) const
        {
            std::error_code Ec;
            const std::filesystem::path Canonical = std::filesystem::weakly_canonical(Path, Ec);
            const std::filesystem::path& Target = Ec ? Path : Canonical;
            for (const std::unique_ptr<Document>& Doc : Documents_)
                if (!Doc->Path.empty() && Doc->Path == Target)
                    return Doc.get();
            return nullptr;
        }

        std::optional<std::string> EditorPanel::OpenText(const std::filesystem::path& Path) const
        {
            if (const Document* Doc = Find(Path))
                return Doc->Editor->GetText();
            return std::nullopt;
        }

        bool EditorPanel::ReplaceText(const std::filesystem::path& Path, const std::string& Text, bool Saved)
        {
            Document* Doc = Find(Path);
            if (Doc == nullptr)
                return false;
            SetBufferText(*Doc, Text);
            if (Saved)
                Doc->SavedUndoIndex = Doc->Editor->GetUndoIndex();
            return true;
        }

        void EditorPanel::SetBufferText(Document& Doc, const std::string& Text)
        {
            if (Doc.Editor->GetText() == Text)
                return;
            // SetText would reset the undo history and with it the dirty test.
            const TextEditor::DocPos Cursor = Doc.Editor->GetMainCursorPosition();
            Doc.Editor->SelectAll();
            Doc.Editor->ReplaceTextInCurrentCursor(Text);
            Doc.Editor->SetCursor(Cursor);
        }

        std::vector<core::UnsavedText> EditorPanel::UnsavedTexts() const
        {
            std::vector<core::UnsavedText> Result;
            for (const std::unique_ptr<Document>& Doc : Documents_)
            {
                if (IsDirty(*Doc))
                    Result.push_back({Doc->Path, Doc->Title, Doc->Editor->GetText()});
            }
            return Result;
        }

        void EditorPanel::OpenRecovered(const core::UnsavedText& Value)
        {
            Document* Doc = nullptr;
            std::error_code FileError;
            if (!Value.Path.empty() && std::filesystem::is_regular_file(Value.Path, FileError) && Open(Value.Path))
                Doc = Find(Value.Path);
            if (Doc == nullptr)
            {
                NewFile();
                Doc = Documents_.back().get();
            }
            SetBufferText(*Doc, Value.Text);
        }

        void EditorPanel::NewFile()
        {
            AddDocument(std::filesystem::path(), ProjectProfile_, true, std::string());
        }

        bool EditorPanel::IsDirty(const Document& Doc)
        {
            return Doc.Editor->GetUndoIndex() != Doc.SavedUndoIndex;
        }

        bool EditorPanel::AnyDirty() const
        {
            return std::any_of(Documents_.begin(), Documents_.end(),
                               [](const std::unique_ptr<Document>& D) { return IsDirty(*D); });
        }

        bool EditorPanel::AnyDirtyWithoutPath() const
        {
            return std::any_of(Documents_.begin(), Documents_.end(),
                               [](const std::unique_ptr<Document>& D) { return D->Path.empty() && IsDirty(*D); });
        }

        bool EditorPanel::AnyDirtyWithPath() const
        {
            return std::any_of(Documents_.begin(), Documents_.end(),
                               [](const std::unique_ptr<Document>& D) { return !D->Path.empty() && IsDirty(*D); });
        }

        bool EditorPanel::SelectFirstDirtyWithoutPath()
        {
            for (size_t I = 0; I < Documents_.size(); ++I)
            {
                if (Documents_[I]->Path.empty() && IsDirty(*Documents_[I]))
                {
                    Current_ = static_cast<int>(I);
                    SelectRequest_ = Documents_[I].get();
                    return true;
                }
            }
            return false;
        }

        std::filesystem::path EditorPanel::CurrentPath() const
        {
            return HasDocument() ? Documents_[Current_]->Path : std::filesystem::path();
        }

        std::vector<BufferedSource> EditorPanel::DirtySources(dbc::language::LanguageProfile Profile) const
        {
            std::vector<BufferedSource> Sources;
            for (const std::unique_ptr<Document>& Document : Documents_)
            {
                bool Lossless = true;
                if (Document->Basic && !Document->Path.empty() && Document->Profile == Profile && IsDirty(*Document))
                    Sources.push_back({Document->Path, EncodedText(*Document, Lossless)});
            }
            return Sources;
        }

        dbc::language::LanguageProfile EditorPanel::CurrentProfile() const
        {
            return HasDocument() ? Documents_[Current_]->Profile : ProjectProfile_;
        }

        TextEditor* EditorPanel::CurrentEditor()
        {
            return HasDocument() ? Documents_[Current_]->Editor.get() : nullptr;
        }

        bool EditorPanel::CurrentIsBasic() const
        {
            return HasDocument() && Documents_[Current_]->Basic;
        }

        void EditorPanel::GoToLine(std::uint32_t Line)
        {
            TextEditor* Editor = CurrentEditor();
            if (Editor == nullptr || Line == 0)
                return;
            // SetCursor before ScrollToLine: the scroll request overrides the cursor's.
            Editor->SetCursor(TextEditor::DocPos(Line - 1, 0));
            Editor->ScrollToLine(Line - 1, TextEditor::Scroll::alignMiddle);
            Editor->SetFocus();
        }

        std::string EditorPanel::EncodedText(const Document& Doc, bool& Lossless)
        {
            const std::string Text = Doc.Editor->GetText();
            std::optional<std::string> Bytes = core::EncodeText(Text, Doc.Format);
            Lossless = Bytes.has_value();
            if (!Bytes)
            {
                core::TextFormat Utf8 = Doc.Format;
                Utf8.Windows1252 = false;
                Bytes = core::EncodeText(Text, Utf8);
            }
            return std::move(*Bytes);
        }

        bool EditorPanel::WriteDocument(Document& Doc, const std::filesystem::path& Path)
        {
            // A tab keeps its kind: a BASIC buffer cannot be saved as an effect or the reverse.
            const bool SameKind = Doc.Basic                  ? dbc::language::IsLanguageSource(Path)
                                  : IsEffectSource(Doc.Path) ? IsEffectSource(Path)
                                                             : IsProjectData(Path);
            if (!SameKind)
            {
                return false;
            }
            bool Lossless = true;
            const std::string Bytes = EncodedText(Doc, Lossless);
            std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
            if (!Out)
            {
                return false;
            }

            Out << Bytes;
            if (!Out.good())
            {
                return false;
            }
            if (!Lossless)
            {
                // Characters typed since opening have no Windows-1252 byte; UTF-8 keeps them.
                Doc.Format.Windows1252 = false;
                Notices_.push_back(TrFmt("{0} is now saved as UTF-8: Windows-1252 cannot store some of its characters.",
                                         {dbc::PathToUtf8(Path.filename())}));
            }

            Doc.Path = Path;
            Doc.Profile = ProjectProfile_;
            if (Doc.Basic)
                ConfigureEditor(*Doc.Editor, Doc.Profile);
            Doc.Title = dbc::PathToUtf8(Path.filename());
            Doc.SavedUndoIndex = Doc.Editor->GetUndoIndex();
            return true;
        }

        bool EditorPanel::SaveCurrent()
        {
            if (!HasDocument() || Documents_[Current_]->Path.empty())
            {
                return false;
            }
            Document& Doc = *Documents_[Current_];
            return WriteDocument(Doc, Doc.Path);
        }

        bool EditorPanel::SaveCurrentAs(const std::filesystem::path& Path)
        {
            if (!HasDocument())
            {
                return false;
            }
            return WriteDocument(*Documents_[Current_], Path);
        }

        bool EditorPanel::SaveAll()
        {
            bool Saved = true;
            for (const std::unique_ptr<Document>& Doc : Documents_)
            {
                if (!IsDirty(*Doc))
                {
                    continue;
                }
                if (Doc->Path.empty() || !WriteDocument(*Doc, Doc->Path))
                {
                    Saved = false;
                }
            }
            return Saved;
        }

        std::vector<std::string> EditorPanel::TakeNotices()
        {
            return std::exchange(Notices_, {});
        }

        void EditorPanel::OpenFind(bool Replace)
        {
            TextEditor* Editor = CurrentEditor();
            if (Editor == nullptr)
                return;
            TextEditor::DocSelection Term = Editor->GetMainCursorSelection();
            if (Term.start == Term.end)
                Term = {Editor->FindWordStart(Term.start, true), Editor->FindWordEnd(Term.start, true)};
            if (Term.start != Term.end && Term.start.line == Term.end.line)
                std::snprintf(FindText_, sizeof(FindText_), "%s", Editor->GetSectionText(Term).c_str());
            FindVisible_ = true;
            FindReplacing_ = Replace;
            FindFocus_ = true;
            FindFailed_ = false;
            FindAnchor_ = Editor->GetMainCursorSelection().start;
        }

        void EditorPanel::CloseFind()
        {
            FindVisible_ = false;
            FindFailed_ = false;
            if (TextEditor* Editor = CurrentEditor())
                Editor->SetFocus();
        }

        void EditorPanel::SetFindText(const std::string& Text)
        {
            std::snprintf(FindText_, sizeof(FindText_), "%s", Text.c_str());
            SearchFromAnchor();
        }

        void EditorPanel::SetReplaceText(const std::string& Text)
        {
            std::snprintf(ReplaceText_, sizeof(ReplaceText_), "%s", Text.c_str());
        }

        void EditorPanel::SearchFromAnchor()
        {
            if (TextEditor* Editor = CurrentEditor())
            {
                Editor->SetCursor(FindAnchor_);
                FindNext();
            }
        }

        bool EditorPanel::FindNext()
        {
            TextEditor* Editor = CurrentEditor();
            FindFailed_ = false;
            if (Editor == nullptr || FindText_[0] == '\0')
                return false;
            // The widget leaves the cursor as it is without a match, so one with no selection tells.
            Editor->SetCursor(Editor->GetMainCursorSelection().end);
            Editor->SelectNextOccurrenceOf(FindText_, FindCaseSensitive_, FindWholeWord_);
            FindFailed_ = !Editor->MainCursorHasSelection();
            return !FindFailed_;
        }

        void EditorPanel::ReplaceCurrent()
        {
            TextEditor* Editor = CurrentEditor();
            if (Editor == nullptr)
                return;
            // From its start, a selection that is a match is found again.
            const TextEditor::DocSelection Selected = Editor->GetMainCursorSelection();
            Editor->SetCursor(Selected.start);
            if (!FindNext())
                return;
            const TextEditor::DocSelection Found = Editor->GetMainCursorSelection();
            if (Found.start == Selected.start && Found.end == Selected.end)
            {
                Editor->ReplaceTextInCurrentCursor(ReplaceText_);
                FindNext();
            }
        }

        void EditorPanel::ReplaceAll()
        {
            TextEditor* Editor = CurrentEditor();
            FindFailed_ = false;
            if (Editor == nullptr || FindText_[0] == '\0')
                return;
            // Without a match the selection would stay, and be what gets replaced.
            Editor->SetCursor(Editor->GetMainCursorSelection().start);
            Editor->SelectAllOccurrencesOf(FindText_, FindCaseSensitive_, FindWholeWord_);
            FindFailed_ = !Editor->AnyCursorHasSelection();
            if (!FindFailed_)
                Editor->ReplaceTextInAllCursors(ReplaceText_);
        }

        void EditorPanel::HandleFindShortcuts()
        {
            // Claimed over the widget, whose Ctrl+F and Ctrl+G work on its own find window.
            constexpr ImGuiInputFlags KOverWidget = ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused;
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, KOverWidget);
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, KOverWidget);

            const ImGuiIO& Io = ImGui::GetIO();
            if (!Io.KeyCtrl && !Io.KeySuper)
                return;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                OpenFind(Io.KeyShift);
            else if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !Io.KeyShift)
                OpenFind(false);
            else if (ImGui::IsKeyPressed(ImGuiKey_G, true))
                FindNext();
        }

        void EditorPanel::DrawFindBar(TextEditor& Editor)
        {
            const float FieldWidth = ImGui::GetFontSize() * 18.0f;
            // With the completion popup open, Escape is for the popup.
            const bool Escape = !Completion_.IsOpen() && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                                ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

            if (FindFocus_)
            {
                ImGui::SetKeyboardFocusHere();
                FindFocus_ = false;
            }
            ImGui::SetNextItemWidth(FieldWidth);
            if (FindFailed_)
                ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::ErrorLine);
            const bool Entered = ImGui::InputTextWithHint("##find", Tr("Find"), FindText_, sizeof(FindText_),
                                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (FindFailed_)
                ImGui::PopStyleColor();
            if (ImGui::IsItemActivated())
                FindAnchor_ = Editor.GetMainCursorSelection().start;
            // Escape puts the text back, which is no search.
            if (ImGui::IsItemEdited() && !Escape)
                SearchFromAnchor();
            if (Entered)
            {
                FindNext();
                // Enter leaves the field; staying in it makes the next one go on to the next match.
                ImGui::SetKeyboardFocusHere(-1);
            }

            ImGui::BeginDisabled(FindText_[0] == '\0');
            ImGui::SameLine();
            if (ImGui::Button(TrId("Next")))
            {
                FindNext();
                FindFocus_ = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Tr("Select the next match (Enter, Ctrl+G)"));
            ImGui::SameLine();
            if (ImGui::Button(TrId("All")))
            {
                Editor.SetCursor(Editor.GetMainCursorSelection().start);
                Editor.SelectAllOccurrencesOf(FindText_, FindCaseSensitive_, FindWholeWord_);
                FindFailed_ = !Editor.AnyCursorHasSelection();
                Editor.SetFocus();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Tr("Select every match, with a cursor on each"));
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool CaseChanged = ImGui::Checkbox("Aa", &FindCaseSensitive_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("Match case"));
            ImGui::SameLine();
            const bool WordChanged = ImGui::Checkbox(TrId("Word"), &FindWholeWord_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("Match whole words only"));
            if (CaseChanged || WordChanged)
                SearchFromAnchor();
            ImGui::SameLine();
            if (ImGui::Button("x"))
                CloseFind();
            if (FindFailed_)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImColor(Theme::Error), "%s", Tr("No results"));
            }

            if (FindReplacing_ && !Editor.IsReadOnlyEnabled())
            {
                ImGui::SetNextItemWidth(FieldWidth);
                if (ImGui::InputTextWithHint("##replace", Tr("Replace with"), ReplaceText_, sizeof(ReplaceText_),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    ReplaceCurrent();
                    ImGui::SetKeyboardFocusHere(-1);
                }
                ImGui::BeginDisabled(FindText_[0] == '\0');
                ImGui::SameLine();
                if (ImGui::Button(TrId("Replace")))
                    ReplaceCurrent();
                ImGui::SameLine();
                if (ImGui::Button(TrId("Replace All")))
                    ReplaceAll();
                ImGui::EndDisabled();
            }

            if (Escape)
                CloseFind();
        }

        void EditorPanel::FormatCurrent()
        {
            // DBA has no formatter yet; normalising trailing whitespace is the part
            // the Qt editor actually did.
            TextEditor* Editor = CurrentEditor();
            if (Editor == nullptr)
            {
                return;
            }

            std::istringstream In(Editor->GetText());
            std::ostringstream Out;
            std::string Line;
            bool First = true;
            while (std::getline(In, Line))
            {
                while (!Line.empty() && (Line.back() == ' ' || Line.back() == '\t' || Line.back() == '\r'))
                {
                    Line.pop_back();
                }
                if (!First)
                {
                    Out << '\n';
                }
                Out << Line;
                First = false;
            }

            const TextEditor::DocPos Cursor = Editor->GetMainCursorPosition();
            Editor->SelectAll();
            Editor->ReplaceTextInCurrentCursor(Out.str());
            Editor->SetCursor(Cursor);
        }

        void EditorPanel::SetErrorMarkers(const std::map<int, std::string>& Errors)
        {
            if (!HasDocument())
            {
                return;
            }
            SetErrorMarkers(Documents_[Current_]->Path, Errors);
        }

        void EditorPanel::SetErrorMarkers(const std::filesystem::path& Path, const std::map<int, std::string>& Errors)
        {
            const std::filesystem::path Target = std::filesystem::absolute(Path).lexically_normal();
            const auto It =
                std::find_if(Documents_.begin(), Documents_.end(), [&](const std::unique_ptr<Document>& Value)
                             { return std::filesystem::absolute(Value->Path).lexically_normal() == Target; });
            if (It == Documents_.end())
            {
                return;
            }

            (*It)->Errors = Errors;
            ApplyErrorMarkers(**It);
        }

        void EditorPanel::ClearErrorMarkers()
        {
            if (HasDocument())
            {
                Documents_[Current_]->Errors.clear();
                Documents_[Current_]->Editor->ClearMarkers();
            }
        }

        void EditorPanel::ApplyErrorMarkers(Document& Doc)
        {
            Doc.Editor->ClearMarkers();
            for (const auto& [Line, Message] : Doc.Errors)
            {
                // Diagnostic lines are 1-based, markers are 0-based.
                const size_t Index = (Line > 0) ? static_cast<size_t>(Line - 1) : 0;
                Doc.Editor->AddMarker(Index, Theme::Error, Theme::ErrorLine, Message, Message);
            }
        }

        void EditorPanel::ApplyTheme()
        {
            for (const std::unique_ptr<Document>& Doc : Documents_)
            {
                Doc->Editor->SetPalette(EditorPalette());
                // Markers hold their colors.
                ApplyErrorMarkers(*Doc);
            }
        }

        void EditorPanel::DrawCloseConfirm()
        {
            if (PendingClose_ < 0)
            {
                return;
            }

            const char* Popup = TrId("Unsaved changes");
            if (!ImGui::IsPopupOpen(Popup))
            {
                ImGui::OpenPopup(Popup);
            }

            if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            Document& Doc = *Documents_[PendingClose_];
            ImGui::TextUnformatted(TrFmt("{0} has unsaved changes.", {Doc.Title}).c_str());
            ImGui::Spacing();

            const auto Erase = [this]
            {
                if (SelectRequest_ == Documents_[PendingClose_].get())
                    SelectRequest_ = nullptr;
                Documents_.erase(Documents_.begin() + PendingClose_);
                PendingClose_ = -1;
                Current_ = Documents_.empty() ? -1 : std::min(Current_, static_cast<int>(Documents_.size()) - 1);
                ImGui::CloseCurrentPopup();
            };

            if (ImGui::Button(TrId("Save")))
            {
                if (!Doc.Path.empty() && WriteDocument(Doc, Doc.Path))
                {
                    Erase();
                }
                else
                {
                    // Needs a destination: keep the tab and let the user use Save As.
                    PendingClose_ = -1;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(TrId("Discard")))
            {
                Erase();
            }
            ImGui::SameLine();
            if (ImGui::Button(TrId("Cancel")))
            {
                Documents_[PendingClose_]->Open = true;
                PendingClose_ = -1;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void EditorPanel::Draw()
        {
            if (Documents_.empty())
            {
                ImGui::TextDisabled("%s", Tr("No file open. Use File > Open, or pick one in the Project panel."));
                return;
            }

            HandleFindShortcuts();

            if (ImGui::BeginTabBar("##documents", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
                                                      ImGuiTabBarFlags_FittingPolicyScroll))
            {
                for (size_t I = 0; I < Documents_.size(); ++I)
                {
                    Document& Doc = *Documents_[I];

                    std::string Label = Doc.Title;
                    if (IsDirty(Doc))
                    {
                        Label += " *";
                    }
                    Label += "###doc" + std::to_string(reinterpret_cast<uintptr_t>(&Doc));

                    Doc.Open = true;
                    ImGuiTabItemFlags Flags = ImGuiTabItemFlags_None;
                    if (SelectRequest_ == &Doc)
                    {
                        Flags = ImGuiTabItemFlags_SetSelected;
                        SelectRequest_ = nullptr;
                        // The editor may sit behind another tab of its dock node.
                        ImGui::SetWindowFocus();
                        Doc.Editor->SetFocus();
                    }
                    if (ImGui::BeginTabItem(Label.c_str(), &Doc.Open, Flags))
                    {
                        Current_ = static_cast<int>(I);

                        if (!Doc.Path.empty() && ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", dbc::PathToUtf8(Doc.Path).c_str());
                        }

                        if (FindVisible_)
                            DrawFindBar(*Doc.Editor);
                        const ImGuiID EditorId = ImGui::GetID("##editor");
                        if (Doc.Basic)
                        {
                            Completion_.BeforeEditor(*Doc.Editor, EditorId);
                        }
                        Theme::PushCodeFont();
                        Doc.Editor->Render("##editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
                        ImGui::PopFont();
                        if (Doc.Basic)
                        {
                            Completion_.AfterEditor(*Doc.Editor, Doc.Profile, EditorId);
                        }
                        ImGui::EndTabItem();
                    }

                    if (!Doc.Open && PendingClose_ < 0)
                    {
                        if (IsDirty(Doc))
                        {
                            PendingClose_ = static_cast<int>(I);
                        }
                        else
                        {
                            if (SelectRequest_ == &Doc)
                                SelectRequest_ = nullptr;
                            Documents_.erase(Documents_.begin() + static_cast<long>(I));
                            Current_ =
                                Documents_.empty() ? -1 : std::min(Current_, static_cast<int>(Documents_.size()) - 1);
                            break;
                        }
                    }
                }

                ImGui::EndTabBar();
            }

            DrawCloseConfirm();
        }
    } // namespace panels

} // namespace dbc::studio
