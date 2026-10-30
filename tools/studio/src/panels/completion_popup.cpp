#include "completion_popup.hpp"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include "dba/command_database.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace
    {
        constexpr const char* KWindowName = "##completion";
        constexpr int KVisibleRows = 10;
        constexpr size_t KMinListGlyphs = 24;
        constexpr size_t KMaxListGlyphs = 56;
        constexpr float KHelpGlyphs = 46.0f;
        constexpr int KMinHelpRows = 5;

        bool EditorHasFocus(ImGuiID EditorId)
        {
            const ImGuiWindow* Focused = ImGui::GetCurrentContext()->NavWindow;
            return Focused != nullptr && Focused->ChildId == EditorId;
        }

        bool PopupHasFocus()
        {
            const ImGuiWindow* Focused = ImGui::GetCurrentContext()->NavWindow;
            return Focused != nullptr && Focused->RootWindow == ImGui::FindWindowByName(KWindowName);
        }

        // Reads the key and hides it from the editor this frame.
        bool ClaimKey(ImGuiKey Key, bool Repeat)
        {
            if (!ImGui::IsKeyPressed(Key, Repeat))
            {
                return false;
            }
            ImGui::SetKeyOwner(Key, ImGuiKeyOwner_Any, ImGuiInputFlags_LockThisFrame);
            return true;
        }

        // TextEditor keeps this offset private; this mirrors how it computes it, without
        // the line decorator the Studio does not use.
        float TextLeftOffset(const TextEditor& Editor)
        {
            size_t Glyphs = Editor.GetTextLeftMargin() + (Editor.IsLineFoldingEnabled() ? 1 : 0);
            if (Editor.IsShowLineNumbersEnabled())
            {
                Glyphs += Editor.GetLineNumberLeftMargin() +
                          static_cast<size_t>(std::log10(static_cast<float>(Editor.GetLineCount() + 1)) + 1.0f);
            }
            return static_cast<float>(Glyphs) * Editor.GetGlyphWidth();
        }

        // Highlights the letters of the name that the typed text walks through.
        void DrawName(ImDrawList& Draw, ImVec2 Position, const std::string& Name, const std::string& Typed)
        {
            const auto Lower = [](char Character)
            { return (Character >= 'A' && Character <= 'Z') ? static_cast<char>(Character - 'A' + 'a') : Character; };

            size_t Next = 0;
            for (const char& Character : Name)
            {
                while (Next < Typed.size() && (Typed[Next] == ' ' || Typed[Next] == '\t'))
                {
                    ++Next;
                }
                const bool Matched = Next < Typed.size() && Lower(Typed[Next]) == Lower(Character);
                Next += Matched ? 1 : 0;

                const char* End = &Character + 1;
                Draw.AddText(Position, Matched ? Theme::Identifier : Theme::Text, &Character, End);
                Position.x += ImGui::CalcTextSize(&Character, End).x;
            }
        }
    } // namespace

    namespace panels
    {
        void DrawCommandHelp(const dba::CommandHelp& Help, float WrapPos)
        {
            ImGui::PushTextWrapPos(WrapPos);
            for (const std::string& Signature : Help.Signatures)
            {
                ImGui::TextUnformatted(Signature.c_str());
            }
            if (!Help.Warning.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warning);
                ImGui::TextUnformatted(Help.Warning.c_str());
                ImGui::PopStyleColor();
            }
            if (!Help.Description.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted(Help.Description.c_str());
            }
            ImGui::PopTextWrapPos();
        }

        void CompletionPopup::BeforeEditor(TextEditor& Editor, ImGuiID EditorId)
        {
            if (Open_ && Owner_ != &Editor)
            {
                Close();
            }

            const ImGuiIO& Io = ImGui::GetIO();
            const bool Focused = EditorHasFocus(EditorId);
            TypedCharacter_ = Focused && !Io.InputQueueCharacters.empty() ? Io.InputQueueCharacters.back() : 0;

            if (Open_ && Focused && Io.KeyMods == ImGuiMod_None)
            {
                HandleKeys(Editor);
            }
            CursorBefore_ = Editor.GetMainCursorPosition();
        }

        void CompletionPopup::HandleKeys(TextEditor& Editor)
        {
            if (ClaimKey(ImGuiKey_DownArrow, true))
            {
                Select(Selected_ + 1, true);
            }
            else if (ClaimKey(ImGuiKey_UpArrow, true))
            {
                Select(Selected_ <= 0 ? -1 : Selected_ - 1, true);
            }
            else if (ClaimKey(ImGuiKey_PageDown, true))
            {
                Select(Selected_ + KVisibleRows, false);
            }
            else if (ClaimKey(ImGuiKey_PageUp, true))
            {
                Select(std::max(Selected_ - KVisibleRows, 0), false);
            }
            else if (Selected_ >= 0)
            {
                const bool Tab = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
                const bool Enter =
                    ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
                if (Tab || (Enter && !Soft_))
                {
                    ClaimKey(ImGuiKey_Tab, false);
                    ClaimKey(ImGuiKey_Enter, false);
                    ClaimKey(ImGuiKey_KeypadEnter, false);
                    Accept(Editor, static_cast<size_t>(Selected_));
                }
            }
        }

        void CompletionPopup::Select(int Item, bool Wrap)
        {
            const int Count = static_cast<int>(Completion_.Items.size());
            if (Wrap)
            {
                Selected_ = Item < 0 ? Count - 1 : Item % Count;
            }
            else
            {
                Selected_ = std::min(Item, Count - 1);
            }
            Soft_ = false;
            ScrollToSelected_ = true;
            UpdateHelp();
        }

        void CompletionPopup::UpdateHelp()
        {
            Help_ = Selected_ >= 0 ? dba::DescribeCommand(*Commands_, Profile_,
                                                          Completion_.Items[static_cast<size_t>(Selected_)]->Classic)
                                   : dba::CommandHelp{};
        }

        void CompletionPopup::Accept(TextEditor& Editor, size_t Item)
        {
            const std::string Name = dba::ApplyTypedCase(Completion_.Items[Item]->Classic, Completion_.Typed);
            const TextEditor::DocPos Cursor = Editor.GetMainCursorPosition();
            const std::string Line = Editor.GetLineText(Cursor.line);
            const size_t CursorByte = Editor.GetSectionText(TextEditor::DocPos(Cursor.line, 0), Cursor).size();
            const size_t Rest = dba::CompletionWordEnd(Line, CursorByte) - CursorByte;
            Editor.ReplaceSectionText(ReplaceStart_, TextEditor::DocPos(Cursor.line, Cursor.index + Rest), Name);
            Editor.SetFocus();
            Close();
        }

        void CompletionPopup::Close()
        {
            Open_ = false;
            Completion_ = {};
            Help_ = {};
        }

        void CompletionPopup::Refresh(TextEditor& Editor, dbc::language::LanguageProfile Profile, bool Manual)
        {
            const TextEditor::DocPos Cursor = Editor.GetMainCursorPosition();
            LastCursor_ = Cursor;
            LastUndoIndex_ = Editor.GetUndoIndex();

            if (Commands_ == nullptr || Editor.IsReadOnlyEnabled() || Editor.GetNumberOfCursors() > 1 ||
                Editor.AnyCursorHasSelection())
            {
                Close();
                return;
            }

            const std::string Before = Editor.GetSectionText(TextEditor::DocPos(Cursor.line, 0), Cursor);
            bool InBlockComment = false;
            for (size_t Line = 0; Line < Cursor.line; ++Line)
            {
                dba::EndsInCommentOrString(Editor.GetLineText(Line), InBlockComment);
            }
            if (dba::EndsInCommentOrString(Before, InBlockComment))
            {
                Close();
                return;
            }

            Completion_ = dba::Complete(*Commands_, Profile, Before, Manual);
            if (Completion_.Items.empty())
            {
                Close();
                return;
            }

            // Only ASCII is ever replaced, so bytes count glyphs here.
            ReplaceStart_ = TextEditor::DocPos(Cursor.line, Cursor.index - (Before.size() - Completion_.ReplaceStart));
            LongestName_ = 0;
            for (const dba::Command* Entry : Completion_.Items)
            {
                LongestName_ = std::max(LongestName_, Entry->Classic.size());
            }
            Selected_ = Completion_.Selection == dba::CompletionSelection::None ? -1 : 0;
            Soft_ = Completion_.Selection == dba::CompletionSelection::Soft;
            ScrollToSelected_ = true;
            Profile_ = Profile;
            UpdateHelp();
            Owner_ = &Editor;
            Open_ = true;
        }

        void CompletionPopup::AfterEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile, ImGuiID EditorId)
        {
            static const ImGuiKeyChord KManualTrigger = TextEditor::AutoCompleteConfig{}.triggerShortcut;

            const bool Focused = EditorHasFocus(EditorId);
            const TextEditor::DocPos Cursor = Editor.GetMainCursorPosition();

            if (Focused && ImGui::IsKeyChordPressed(KManualTrigger))
            {
                Refresh(Editor, Profile, true);
            }
            else if (TypedCharacter_ != 0 && Cursor != CursorBefore_)
            {
                Refresh(Editor, Profile, false);
            }
            else if (Open_ && (Cursor != LastCursor_ || Editor.GetUndoIndex() != LastUndoIndex_))
            {
                // Moving or deleting keeps the list only while it completes the same words.
                const TextEditor::DocPos Previous = ReplaceStart_;
                Refresh(Editor, Profile, false);
                if (Open_ && ReplaceStart_ != Previous)
                {
                    Close();
                }
            }

            if (!Open_)
            {
                return;
            }
            if (!Focused && !PopupHasFocus())
            {
                Close();
                return;
            }
            // The editor does not route Escape by itself, and keyboard navigation would
            // use it to leave the editor. Routing it takes effect on the next frame, so
            // this must run from the frame the list opens.
            if (ImGui::Shortcut(ImGuiKey_Escape))
            {
                Close();
                return;
            }
            Draw(Editor, EditorId);
        }

        void CompletionPopup::Draw(TextEditor& Editor, ImGuiID EditorId)
        {
            const ImGuiWindow* EditorWindow = nullptr;
            for (const ImGuiWindow* Child : ImGui::GetCurrentWindow()->DC.ChildWindows)
            {
                if (Child->ChildId == EditorId)
                {
                    EditorWindow = Child;
                }
            }
            if (EditorWindow == nullptr)
            {
                Close();
                return;
            }

            const float GlyphWidth = Editor.GetGlyphWidth();
            const float LineHeight = Editor.GetLineHeight();
            const TextEditor::VisPos Anchor = Editor.DocPos2VisPos(ReplaceStart_);
            const ImVec2 Origin = EditorWindow->DC.CursorStartPos;
            const float X = Origin.x + TextLeftOffset(Editor) + static_cast<float>(Anchor.column) * GlyphWidth;
            const float LineTop = Origin.y + static_cast<float>(Anchor.row) * LineHeight;

            // A list detached from its line would capture keys for text out of sight.
            if (LineTop + LineHeight < EditorWindow->Pos.y || LineTop > EditorWindow->Pos.y + EditorWindow->Size.y)
            {
                Close();
                return;
            }

            const ImGuiStyle& Style = ImGui::GetStyle();
            const int Count = static_cast<int>(Completion_.Items.size());
            const float RowHeight = ImGui::GetFrameHeight();
            const float ListHeight = static_cast<float>(std::min(Count, KVisibleRows)) * RowHeight;
            const size_t ListGlyphs = std::clamp(LongestName_ + 2, KMinListGlyphs, KMaxListGlyphs);
            const float ListWidth = static_cast<float>(ListGlyphs) * GlyphWidth + Style.FramePadding.x * 2.0f +
                                    (Count > KVisibleRows ? Style.ScrollbarSize : 0.0f);

            const bool HasHelp = Selected_ >= 0;
            const float HelpWidth = HasHelp ? KHelpGlyphs * GlyphWidth : 0.0f;
            // A short list still leaves room to read the help.
            const float HelpHeight = std::max(ListHeight, static_cast<float>(KMinHelpRows) * RowHeight);

            const ImVec2 Size(ListWidth + HelpWidth + 2.0f, (HasHelp ? HelpHeight : ListHeight) + 2.0f);
            const ImGuiViewport* Viewport = ImGui::GetWindowViewport();
            const ImVec2 WorkMax(Viewport->WorkPos.x + Viewport->WorkSize.x,
                                 Viewport->WorkPos.y + Viewport->WorkSize.y);
            ImVec2 Position(std::max(std::min(X, WorkMax.x - Size.x), Viewport->WorkPos.x), LineTop + LineHeight);
            if (Position.y + Size.y > WorkMax.y && LineTop - Size.y >= Viewport->WorkPos.y)
            {
                Position.y = LineTop - Size.y;
            }

            ImGui::SetNextWindowPos(Position);
            ImGui::SetNextWindowSize(Size);
            // The default minimum would leave a gap under a short list.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::PanelAlt);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::PanelAlt);

            constexpr ImGuiWindowFlags KWindowFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;

            int Picked = -1;
            if (ImGui::Begin(KWindowName, nullptr, KWindowFlags))
            {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

                ImGui::BeginChild("##list", ImVec2(ListWidth, ListHeight), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoNav);
                if (ScrollToSelected_)
                {
                    const float Top = static_cast<float>(std::max(Selected_, 0)) * RowHeight;
                    if (Top < ImGui::GetScrollY())
                    {
                        ImGui::SetScrollY(Top);
                    }
                    else if (Top + RowHeight > ImGui::GetScrollY() + ListHeight)
                    {
                        ImGui::SetScrollY(Top + RowHeight - ListHeight);
                    }
                    ScrollToSelected_ = false;
                }

                ImDrawList& DrawList = *ImGui::GetWindowDrawList();
                ImGuiListClipper Clipper;
                Clipper.Begin(Count, RowHeight);
                while (Clipper.Step())
                {
                    for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
                    {
                        ImGui::PushID(Row);
                        if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0.0f, RowHeight)))
                        {
                            Picked = Row;
                        }
                        ImGui::PopID();

                        const ImVec2 Min = ImGui::GetItemRectMin();
                        const ImVec2 Max = ImGui::GetItemRectMax();
                        if (Row == Selected_ && Soft_)
                        {
                            DrawList.AddRect(Min, Max, Theme::AccentHover);
                        }
                        else if (Row == Selected_)
                        {
                            DrawList.AddRectFilled(Min, Max, Theme::Selection);
                        }
                        DrawName(
                            DrawList,
                            ImVec2(Min.x + Style.FramePadding.x, Min.y + (RowHeight - ImGui::GetFontSize()) * 0.5f),
                            Completion_.Items[static_cast<size_t>(Row)]->Classic, Completion_.Typed);
                    }
                }
                ImGui::EndChild();

                if (HasHelp)
                {
                    ImGui::SameLine();
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Style.FramePadding);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, Style.FramePadding.y));
                    ImGui::BeginChild("##help", ImVec2(HelpWidth, HelpHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                                      ImGuiWindowFlags_NoNav);
                    DrawCommandHelp(Help_, 0.0f);
                    ImGui::EndChild();
                    ImGui::PopStyleVar(2);
                }
            }
            ImGui::End();

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);

            if (Picked >= 0)
            {
                Accept(Editor, static_cast<size_t>(Picked));
            }
            else if (PopupHasFocus() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // Dragging the scrollbar took the focus; typing belongs to the editor.
                Editor.SetFocus();
            }
        }
    } // namespace panels

} // namespace dbc::studio
