#pragma once

#include "dba/completion.hpp"
#include "language/language_profile.hpp"
#include <TextEditor.h>

namespace dbc::studio
{
    namespace panels
    {
        // Wrapped at `WrapPos` (see ImGui::PushTextWrapPos).
        void DrawCommandHelp(const dba::CommandHelp& Help, float WrapPos);

        // The suggestion list under the cursor. It replaces TextEditor's own popup,
        // which completes one word at a time and shows nothing but names.
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
                return Open_;
            }

          private:
            void Refresh(TextEditor& Editor, dbc::language::LanguageProfile Profile, bool Manual);
            void HandleKeys(TextEditor& Editor);
            void Select(int Item, bool Wrap);
            void UpdateHelp();
            void Accept(TextEditor& Editor, size_t Item);
            void Draw(TextEditor& Editor, ImGuiID EditorId);
            void Close();

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
            // -1 when nothing is selected.
            int Selected_ = -1;
            bool Soft_ = false;
            bool ScrollToSelected_ = false;

            TextEditor::DocPos CursorBefore_;
            TextEditor::DocPos LastCursor_;
            size_t LastUndoIndex_ = 0;
            ImWchar TypedCharacter_ = 0;
        };
    } // namespace panels

} // namespace dbc::studio
