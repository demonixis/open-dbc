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
        constexpr const char* KHintWindowName = "##parameter-hint";
        constexpr float KHintGlyphs = 72.0f;
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

        // DocPos counts glyphs, the text bytes.
        size_t GlyphCount(std::string_view Utf8)
        {
            return static_cast<size_t>(std::count_if(Utf8.begin(), Utf8.end(), [](char Byte)
                                                     { return (static_cast<unsigned char>(Byte) & 0xC0) != 0x80; }));
        }

        // Screen position of the top left of a glyph.
        ImVec2 GlyphPosition(const TextEditor& Editor, const ImGuiWindow& EditorWindow, TextEditor::DocPos Position)
        {
            const TextEditor::VisPos Visual = Editor.DocPos2VisPos(Position);
            const ImVec2 Origin = EditorWindow.DC.CursorStartPos;
            return ImVec2(Origin.x + TextLeftOffset(Editor) +
                              static_cast<float>(Visual.column) * Editor.GetGlyphWidth(),
                          Origin.y + static_cast<float>(Visual.row) * Editor.GetLineHeight());
        }

        // A popup detached from its line would act on text out of sight.
        bool LineIsVisible(const TextEditor& Editor, const ImGuiWindow& EditorWindow, float LineTop)
        {
            return LineTop + Editor.GetLineHeight() >= EditorWindow.Pos.y &&
                   LineTop <= EditorWindow.Pos.y + EditorWindow.Size.y;
        }

        // Writes an overload piece by piece, the parameter being typed standing out.
        void DrawSignature(const dba::Signature& Signature, size_t Argument, float MaxWidth)
        {
            const size_t Active = Signature.ParameterFor(Argument);
            // An overload that takes fewer arguments than typed is no longer a candidate.
            const ImU32 Base = Active != dba::Signature::NoParameter || (Signature.Parameters.empty() && Argument == 0)
                                   ? Theme::Text
                                   : Theme::TextDim;
            const float Start = ImGui::GetCursorPosX();
            bool First = true;
            const auto Emit = [&](std::string_view Text, ImU32 Color)
            {
                if (Text.empty())
                {
                    return;
                }
                const char* Begin = Text.data();
                const char* End = Begin + Text.size();
                if (!First)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    if (ImGui::GetCursorPosX() - Start + ImGui::CalcTextSize(Begin, End).x > MaxWidth)
                    {
                        ImGui::NewLine();
                    }
                }
                First = false;
                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                ImGui::TextUnformatted(Begin, End);
                ImGui::PopStyleColor();
            };

            Emit(Signature.Prefix, Base);
            for (size_t Index = 0; Index < Signature.Parameters.size(); ++Index)
            {
                if (Index > 0)
                {
                    Emit(", ", Base);
                }
                Emit(Signature.Parameters[Index], Index == Active ? Theme::Identifier : Base);
                if (Index == Active)
                {
                    const ImVec2 Min = ImGui::GetItemRectMin();
                    const ImVec2 Max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(Min.x, Max.y), Max, Theme::Identifier);
                }
            }
            Emit(Signature.Suffix, Base);
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
            if (IsOpen() && Owner_ != &Editor)
            {
                CloseAll();
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

        void CompletionPopup::CloseAll()
        {
            Close();
            Hint_.reset();
        }

        void CompletionPopup::Refresh(TextEditor& Editor, dbc::language::LanguageProfile Profile,
                                      const std::string& Before, bool InBlockComment, bool Manual)
        {
            const TextEditor::DocPos Cursor = Editor.GetMainCursorPosition();
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

            ReplaceStart_ = TextEditor::DocPos(
                Cursor.line, GlyphCount(std::string_view(Before).substr(0, Completion_.ReplaceStart)));
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

        void CompletionPopup::RefreshHint(TextEditor& Editor, dbc::language::LanguageProfile Profile,
                                          const std::string& Before, bool InBlockComment, bool Follow, bool Reopen)
        {
            std::optional<dba::ParameterHint> Found =
                dba::FindParameterHint(*Commands_, Profile, Before, InBlockComment);
            if (!Found)
            {
                Hint_.reset();
                HintDismissed_.reset();
                return;
            }

            const TextEditor::DocPos Start(Editor.GetMainCursorPosition().line,
                                           GlyphCount(std::string_view(Before).substr(0, Found->NameStart)));
            if (Follow)
            {
                // Moving through code never opens a hint, and one follows only its own call.
                if (!Hint_ || Start != HintStart_)
                {
                    Hint_.reset();
                    return;
                }
            }
            else if (HintDismissed_ == Start && !Reopen)
            {
                return;
            }
            else
            {
                HintDismissed_.reset();
            }
            Hint_ = std::move(Found);
            HintStart_ = Start;
            Owner_ = &Editor;
        }

        void CompletionPopup::AfterEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile, ImGuiID EditorId)
        {
            static const ImGuiKeyChord KListTrigger = TextEditor::AutoCompleteConfig{}.triggerShortcut;
            static const ImGuiKeyChord KHintTrigger = KListTrigger | ImGuiMod_Shift;

            const bool Focused = EditorHasFocus(EditorId);
            const TextEditor::DocPos Cursor = Editor.GetMainCursorPosition();
            const size_t UndoIndex = Editor.GetUndoIndex();
            const bool Changed = Cursor != LastCursor_ || UndoIndex != LastUndoIndex_;
            LastCursor_ = Cursor;
            LastUndoIndex_ = UndoIndex;

            const bool AskedList = Focused && ImGui::IsKeyChordPressed(KListTrigger);
            const bool AskedHint = Focused && ImGui::IsKeyChordPressed(KHintTrigger);
            const bool Typed = TypedCharacter_ != 0 && Cursor != CursorBefore_;

            if (AskedList || AskedHint || Typed || (IsOpen() && Changed))
            {
                if (Commands_ == nullptr || Editor.IsReadOnlyEnabled() || Editor.GetNumberOfCursors() > 1 ||
                    Editor.AnyCursorHasSelection())
                {
                    CloseAll();
                    return;
                }

                const std::string Before = Editor.GetSectionText(TextEditor::DocPos(Cursor.line, 0), Cursor);
                bool InBlockComment = false;
                for (size_t Line = 0; Line < Cursor.line; ++Line)
                {
                    dba::EndsInCommentOrString(Editor.GetLineText(Line), InBlockComment);
                }

                if (AskedList || Typed)
                {
                    Refresh(Editor, Profile, Before, InBlockComment, AskedList);
                }
                else if (Open_ && Changed)
                {
                    // Moving or deleting keeps the list only while it completes the same words.
                    const TextEditor::DocPos Previous = ReplaceStart_;
                    Refresh(Editor, Profile, Before, InBlockComment, false);
                    if (Open_ && ReplaceStart_ != Previous)
                    {
                        Close();
                    }
                }
                RefreshHint(Editor, Profile, Before, InBlockComment, !AskedHint && !Typed,
                            AskedHint || TypedCharacter_ == ',' || TypedCharacter_ == '(');
            }

            if (!IsOpen())
            {
                return;
            }
            if (!Focused && !PopupHasFocus())
            {
                CloseAll();
                return;
            }
            // The editor does not route Escape by itself, and keyboard navigation would
            // use it to leave the editor. Routing it takes effect on the next frame, so
            // this must run from the frame anything opens.
            if (ImGui::Shortcut(ImGuiKey_Escape))
            {
                if (Open_)
                {
                    Close();
                }
                else
                {
                    HintDismissed_ = HintStart_;
                    Hint_.reset();
                }
                if (!IsOpen())
                {
                    return;
                }
            }

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
                CloseAll();
                return;
            }
            if (Open_)
            {
                DrawList(Editor, *EditorWindow);
            }
            if (Hint_)
            {
                DrawHint(Editor, *EditorWindow);
            }
        }

        void CompletionPopup::DrawHint(const TextEditor& Editor, const ImGuiWindow& EditorWindow)
        {
            const ImVec2 Anchor = GlyphPosition(Editor, EditorWindow, HintStart_);
            if (!LineIsVisible(Editor, EditorWindow, Anchor.y))
            {
                Hint_.reset();
                return;
            }

            const ImGuiStyle& Style = ImGui::GetStyle();
            const ImGuiViewport* Viewport = ImGui::GetWindowViewport();
            const float MaxWidth =
                std::min(KHintGlyphs * Editor.GetGlyphWidth(), Viewport->WorkSize.x - Style.FramePadding.x * 2.0f);

            float Width = 0.0f;
            for (const dba::Signature& Value : Hint_->Overloads)
            {
                Width = std::max(Width, ImGui::CalcTextSize(Value.Text().c_str()).x);
            }
            const float WindowWidth = std::min(Width, MaxWidth) + Style.FramePadding.x * 2.0f;
            const float WindowHeight =
                static_cast<float>(Hint_->Overloads.size()) * ImGui::GetTextLineHeightWithSpacing() +
                Style.FramePadding.y * 2.0f;

            // Above the line, as the list takes the space below it, or above the list when
            // that flipped too. A first line leaves no room, and there the list has priority.
            const float Bottom = Open_ ? std::min(Anchor.y, ListTop_) : Anchor.y;
            const bool Above = Bottom - WindowHeight >= Viewport->WorkPos.y;
            if (!Above && Open_)
            {
                return;
            }
            const float X = std::max(std::min(Anchor.x, Viewport->WorkPos.x + Viewport->WorkSize.x - WindowWidth),
                                     Viewport->WorkPos.x);
            ImGui::SetNextWindowPos(Above ? ImVec2(X, Bottom) : ImVec2(X, Anchor.y + Editor.GetLineHeight()),
                                    ImGuiCond_Always, ImVec2(0.0f, Above ? 1.0f : 0.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Style.FramePadding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::PanelAlt);

            constexpr ImGuiWindowFlags KHintFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                                    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;

            if (ImGui::Begin(KHintWindowName, nullptr, KHintFlags))
            {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                for (const dba::Signature& Value : Hint_->Overloads)
                {
                    DrawSignature(Value, Hint_->Argument, MaxWidth);
                }
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(4);
        }

        void CompletionPopup::DrawList(TextEditor& Editor, const ImGuiWindow& EditorWindow)
        {
            const float GlyphWidth = Editor.GetGlyphWidth();
            const float LineHeight = Editor.GetLineHeight();
            const ImVec2 Anchor = GlyphPosition(Editor, EditorWindow, ReplaceStart_);
            const float X = Anchor.x;
            const float LineTop = Anchor.y;

            if (!LineIsVisible(Editor, EditorWindow, LineTop))
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
            ListTop_ = Position.y;

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
