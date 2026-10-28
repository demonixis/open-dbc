#include "editor_panel.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

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
            std::string Extension = Path.extension().string();
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

            AutoComplete_.userData = this;
            AutoComplete_.callback = [](TextEditor::AutoCompleteState& State)
            {
                // The widget does not clear this between callbacks, so without it
                // the list keeps every match from every previous keystroke.
                State.suggestions.clear();

                const auto* Panel = static_cast<const EditorPanel*>(State.userData);
                if (Panel == nullptr || Panel->Commands_ == nullptr || State.searchTerm.empty())
                {
                    return;
                }

                State.suggestions = Panel->Commands_->Complete(Panel->CurrentProfile(), State.searchTerm, 40);
            };
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
        const TextEditor::Palette& EditorPalette()
        {
            static const TextEditor::Palette KPalette = []
            {
                TextEditor::Palette P = TextEditor::GetDarkPalette();
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
            }();
            return KPalette;
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
            Editor.SetAutoCompleteConfig(&AutoComplete_);

            // Hovering a command shows its signature and description.
            const dba::CommandDatabase* Commands = Commands_;
            TextEditor* Self = &Editor;
            Editor.SetTextHoverCallback(
                [Commands, Self, Profile](TextEditor::PopupData& Data)
                {
                    // The widget opens the popup for any glyph under the mouse, so a
                    // callback that draws nothing leaves an empty box floating around.
                    // Close it explicitly whenever there is no documentation to show.
                    const dba::Command* CommandValue = nullptr;

                    if (Commands != nullptr)
                    {
                        const TextEditor::DocPos Start = Self->FindWordStart(Data.pos);
                        const TextEditor::DocPos End = Self->FindWordEnd(Data.pos);
                        const std::string Word = Self->GetSectionText(Start, End);
                        if (!Word.empty())
                        {
                            CommandValue = Commands->Find(Profile, Word);
                        }
                    }

                    if (CommandValue == nullptr || CommandValue->Description.empty())
                    {
                        ImGui::CloseCurrentPopup();
                        return;
                    }

                    ImGui::TextUnformatted(CommandValue->Signature.empty() ? CommandValue->Classic.c_str()
                                                                           : CommandValue->Signature.c_str());
                    ImGui::Separator();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                    ImGui::TextUnformatted(CommandValue->Description.c_str());
                    ImGui::PopTextWrapPos();
                });
        }

        void EditorPanel::ConfigureEffectEditor(TextEditor& Editor) const
        {
            Editor.SetLanguage(TextEditor::Language::Hlsl());
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

        bool EditorPanel::CanOpen(const std::filesystem::path& Path)
        {
            return dbc::language::IsLanguageSource(Path) || IsEffectSource(Path);
        }

        void EditorPanel::AddDocument(std::filesystem::path Path, dbc::language::LanguageProfile Profile, bool Basic,
                                      const std::string& Text)
        {
            auto Doc = std::make_unique<Document>();
            Doc->Path = std::move(Path);
            Doc->Profile = Profile;
            Doc->Basic = Basic;
            Doc->Title = Doc->Path.empty() ? ("untitled-" + std::to_string(++UntitledCount_) + ".dba")
                                           : Doc->Path.filename().string();
            Doc->Editor = std::make_unique<TextEditor>();
            if (Basic)
                ConfigureEditor(*Doc->Editor, Profile);
            else
                ConfigureEffectEditor(*Doc->Editor);
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
                    return true;
                }
            }

            if (!CanOpen(Target))
            {
                return false;
            }

            bool Ok = false;
            const std::string Text = ReadFile(Target, Ok);
            if (!Ok)
            {
                return false;
            }
            AddDocument(Target, ProjectProfile_, dbc::language::IsLanguageSource(Target), Text);
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
            if (Doc->Editor->GetText() != Text)
            {
                // SetText would reset the undo history and with it the dirty test.
                const TextEditor::DocPos Cursor = Doc->Editor->GetMainCursorPosition();
                Doc->Editor->SelectAll();
                Doc->Editor->ReplaceTextInCurrentCursor(Text);
                Doc->Editor->SetCursor(Cursor);
            }
            if (Saved)
                Doc->SavedUndoIndex = Doc->Editor->GetUndoIndex();
            return true;
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
                if (Document->Basic && !Document->Path.empty() && Document->Profile == Profile && IsDirty(*Document))
                    Sources.push_back({Document->Path, Document->Editor->GetText()});
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

        bool EditorPanel::WriteDocument(Document& Doc, const std::filesystem::path& Path)
        {
            // A tab keeps its kind: a BASIC buffer cannot be saved as an effect or the reverse.
            if (Doc.Basic ? !dbc::language::IsLanguageSource(Path) : !IsEffectSource(Path))
            {
                return false;
            }
            std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
            if (!Out)
            {
                return false;
            }

            Out << Doc.Editor->GetText();
            if (!Out.good())
            {
                return false;
            }

            Doc.Path = Path;
            Doc.Profile = ProjectProfile_;
            if (Doc.Basic)
                ConfigureEditor(*Doc.Editor, Doc.Profile);
            Doc.Title = Path.filename().string();
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

        void EditorPanel::OpenFindReplace()
        {
            if (TextEditor* Editor = CurrentEditor())
            {
                Editor->OpenFindReplaceWindow();
            }
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

            TextEditor* Editor = (*It)->Editor.get();
            Editor->ClearMarkers();
            for (const auto& [Line, Message] : Errors)
            {
                // Diagnostic lines are 1-based, markers are 0-based.
                const size_t Index = (Line > 0) ? static_cast<size_t>(Line - 1) : 0;
                Editor->AddMarker(Index, Theme::Error, Theme::ErrorLine, Message, Message);
            }
        }

        void EditorPanel::ClearErrorMarkers()
        {
            if (TextEditor* Editor = CurrentEditor())
            {
                Editor->ClearMarkers();
            }
        }

        void EditorPanel::DrawCloseConfirm()
        {
            if (PendingClose_ < 0)
            {
                return;
            }

            if (!ImGui::IsPopupOpen("Unsaved changes"))
            {
                ImGui::OpenPopup("Unsaved changes");
            }

            if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            Document& Doc = *Documents_[PendingClose_];
            ImGui::Text("%s has unsaved changes.", Doc.Title.c_str());
            ImGui::Spacing();

            const auto Erase = [this]
            {
                Documents_.erase(Documents_.begin() + PendingClose_);
                PendingClose_ = -1;
                Current_ = Documents_.empty() ? -1 : std::min(Current_, static_cast<int>(Documents_.size()) - 1);
                ImGui::CloseCurrentPopup();
            };

            if (ImGui::Button("Save"))
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
            if (ImGui::Button("Discard"))
            {
                Erase();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
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
                ImGui::TextDisabled("No file open. Use File > Open, or pick one in the Project panel.");
                return;
            }

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
                    if (ImGui::BeginTabItem(Label.c_str(), &Doc.Open))
                    {
                        Current_ = static_cast<int>(I);

                        if (!Doc.Path.empty() && ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", Doc.Path.string().c_str());
                        }

                        Doc.Editor->Render("##editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
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
