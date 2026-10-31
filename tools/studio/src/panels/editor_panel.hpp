#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/recovery.hpp"
#include "core/text_encoding.hpp"
#include "language/language_profile.hpp"
#include "panels/completion_popup.hpp"
#include <TextEditor.h>

namespace dbc::studio
{
    namespace dba
    {
        class CommandDatabase;
    }

    namespace panels
    {
        struct BufferedSource
        {
            std::filesystem::path LogicalPath;
            std::string Text;
        };

        // Document tabs and TextEditor integration.
        class EditorPanel
        {
          public:
            void Initialize(const dba::CommandDatabase* Commands);
            void SetProfile(dbc::language::LanguageProfile Profile);

            // HLSL effects open as plain text: highlighted, but without BASIC
            // completion, outline or Run.
            static bool IsEffectSource(const std::filesystem::path& Path);
            // A .dbpro or any JSON file: plain text, like an effect.
            static bool IsProjectData(const std::filesystem::path& Path);
            static bool CanOpen(const std::filesystem::path& Path);

            // Opens the file, focusing an already open tab if there is one.
            // Returns false when the file cannot be read or CanOpen refuses it.
            bool Open(const std::filesystem::path& Path);
            void NewFile();

            void Draw();

            bool HasDocument() const
            {
                return Current_ >= 0 && Current_ < static_cast<int>(Documents_.size());
            }

            bool AnyDirty() const;
            bool AnyDirtyWithoutPath() const;
            bool AnyDirtyWithPath() const;
            bool SelectFirstDirtyWithoutPath();
            std::filesystem::path CurrentPath() const;
            std::vector<BufferedSource> DirtySources(dbc::language::LanguageProfile Profile) const;
            TextEditor* CurrentEditor();
            bool CurrentIsBasic() const;
            dbc::language::LanguageProfile CurrentProfile() const;

            // The buffer of the tab holding `Path`, which may differ from the file on disk.
            std::optional<std::string> OpenText(const std::filesystem::path& Path) const;
            // Replaces that tab's text as one undoable edit. `Saved` records it as the
            // file's content (the caller wrote it to disk); otherwise the tab is dirty.
            // False when no tab holds `Path`.
            bool ReplaceText(const std::filesystem::path& Path, const std::string& Text, bool Saved);

            // Moves the current tab's cursor to a 1-based line and focuses it.
            void GoToLine(std::uint32_t Line);

            // Writes the current document; false when it has no path yet.
            bool SaveCurrent();
            bool SaveCurrentAs(const std::filesystem::path& Path);
            bool SaveAll();
            // Warnings from saving since the last call, for the Output panel.
            std::vector<std::string> TakeNotices();

            // Every tab with unsaved changes, untitled ones included, for recovery copies.
            std::vector<core::UnsavedText> UnsavedTexts() const;
            // Reopens a recovered tab: the file when it still exists, else an untitled tab, holding the
            // recovered text as an unsaved change.
            void OpenRecovered(const core::UnsavedText& Value);

            // Opens the bar above the current tab, on the selection or the word at the cursor if any.
            void OpenFind(bool Replace);
            void CloseFind();

            bool FindVisible() const
            {
                return FindVisible_;
            }

            bool FindReplacing() const
            {
                return FindVisible_ && FindReplacing_;
            }

            // The last search had no match.
            bool FindFailed() const
            {
                return FindFailed_;
            }

            // Searches for Text from where the last search began, as typing into the bar does.
            void SetFindText(const std::string& Text);
            void SetReplaceText(const std::string& Text);
            // Selects the next match after the cursor, past the end back to the start. False without a match.
            bool FindNext();
            // Replaces the selected match and selects the next. Any other selection stays and only
            // moves on to the next match.
            void ReplaceCurrent();
            void ReplaceAll();

            void FormatCurrent();

            // Execution diagnostics, keyed by 1-based line number.
            void SetErrorMarkers(const std::map<int, std::string>& Errors);
            void SetErrorMarkers(const std::filesystem::path& Path, const std::map<int, std::string>& Errors);
            void ClearErrorMarkers();

            // Recolors the open documents after a change of theme.
            void ApplyTheme();

          private:
            struct Document
            {
                std::filesystem::path Path;
                dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
                bool Basic = true;
                std::string Title;
                std::unique_ptr<TextEditor> Editor;
                // The editor holds UTF-8 with LF line endings; saving restores the file's own format.
                core::TextFormat Format;
                size_t SavedUndoIndex = 0;
                bool Open = true;
                // What the markers show, kept so a change of theme can recolor them.
                std::map<int, std::string> Errors;
            };

            void ApplyErrorMarkers(Document& Doc);

            void AddDocument(std::filesystem::path Path, dbc::language::LanguageProfile Profile, bool Basic,
                             const std::string& Text, const core::TextFormat& Format = {});
            // The document's text in its file format, or UTF-8 when the code page cannot hold it.
            static std::string EncodedText(const Document& Doc, bool& Lossless);
            // Replaces the whole text as one undoable edit, which leaves the tab dirty.
            static void SetBufferText(Document& Doc, const std::string& Text);
            void ConfigureEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile) const;
            void ConfigureTextEditor(TextEditor& Editor, const std::filesystem::path& Path) const;
            bool WriteDocument(Document& Doc, const std::filesystem::path& Path);
            Document* Find(const std::filesystem::path& Path) const;
            static bool IsDirty(const Document& Doc);
            void DrawCloseConfirm();
            void HandleFindShortcuts();
            void DrawFindBar(TextEditor& Editor);
            void SearchFromAnchor();

            std::vector<std::unique_ptr<Document>> Documents_;
            int Current_ = -1;
            // A tab to bring to the front on the next Draw: ImGui only selects a tab
            // the user did not click when asked to.
            const Document* SelectRequest_ = nullptr;
            int UntitledCount_ = 0;
            int PendingClose_ = -1;
            std::vector<std::string> Notices_;

            // The widget's own find window has a replace row and gives Enter back to the text,
            // where it would replace the match with a line break, so the panel has a bar of its own.
            bool FindVisible_ = false;
            bool FindReplacing_ = false;
            bool FindFocus_ = false;
            bool FindFailed_ = false;
            bool FindCaseSensitive_ = false;
            bool FindWholeWord_ = false;
            char FindText_[256] = {};
            char ReplaceText_[256] = {};
            // Where typing searches from, so that each new letter can still match where the last did.
            TextEditor::DocPos FindAnchor_;

            const dba::CommandDatabase* Commands_ = nullptr;
            CompletionPopup Completion_;
            dbc::language::LanguageProfile ProjectProfile_ = dbc::language::LanguageProfile::Dbc;
        };
    } // namespace panels

} // namespace dbc::studio
