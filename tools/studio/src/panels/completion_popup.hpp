#pragma once

#include "dba/completion.hpp"
#include "language/language_profile.hpp"
#include <TextEditor.h>

#include <optional>
#include <string>

struct ImGuiWindow;

namespace dbc::studio
{
    namespace panels
    {
        // Wrapped at `WrapPos` (see ImGui::PushTextWrapPos).
        void DrawCommandHelp(const dba::CommandHelp& Help, float WrapPos);

        // The suggestion list under the cursor and the parameter hint above its line. The
        // list replaces TextEditor's own popup, which completes one word at a time and
        // shows nothing but names.
        class CompletionPopup
        {
          public:
            void Initialize(const dba::CommandDatabase* Commands)
            {
                Commands_ = Commands;
            }

            // Both run every frame around TextEditor::Render of the visible BASIC tab:
            // the list needs its keys before the editor reads them. `EditorId` is
            // ImGui::GetID of the title given to Render.
            void BeforeEditor(TextEditor& Editor, ImGuiID EditorId);
            void AfterEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile, ImGuiID EditorId);

            bool IsOpen() const
            {
                return Open_ || Hint_.has_value();
            }

          private:
            void Refresh(TextEditor& Editor, dbc::language::LanguageProfile Profile, const std::string& Before,
                         bool InBlockComment, bool Manual);
            // `Follow` only keeps an open hint on the call it describes. `Reopen` overrides Escape.
            void RefreshHint(TextEditor& Editor, dbc::language::LanguageProfile Profile, const std::string& Before,
                             bool InBlockComment, bool Follow, bool Reopen);
            void HandleKeys(TextEditor& Editor);
            void Select(int Item, bool Wrap);
            void UpdateHelp();
            void Accept(TextEditor& Editor, size_t Item);
            void DrawList(TextEditor& Editor, const ImGuiWindow& EditorWindow);
            void DrawHint(const TextEditor& Editor, const ImGuiWindow& EditorWindow);
            void Close();
            void CloseAll();

            const dba::CommandDatabase* Commands_ = nullptr;
            bool Open_ = false;
            // Only compared: the tab may have been closed since.
            const TextEditor* Owner_ = nullptr;

            dbc::language::LanguageProfile Profile_ = dbc::language::LanguageProfile::Dbc;
            dba::Completion Completion_;
            // Of the selected suggestion: built once per selection, not per frame.
            dba::CommandHelp Help_;
            TextEditor::DocPos ReplaceStart_;
            size_t LongestName_ = 0;
            float ListTop_ = 0.0f;
            // -1 when nothing is selected.
            int Selected_ = -1;
            bool Soft_ = false;
            bool ScrollToSelected_ = false;

            std::optional<dba::ParameterHint> Hint_;
            TextEditor::DocPos HintStart_;
            // The call Escape hid: typing inside it must not bring the hint straight back.
            std::optional<TextEditor::DocPos> HintDismissed_;

            TextEditor::DocPos CursorBefore_;
            TextEditor::DocPos LastCursor_;
            size_t LastUndoIndex_ = 0;
            ImWchar TypedCharacter_ = 0;
        };
    } // namespace panels

} // namespace dbc::studio
