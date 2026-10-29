#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/text_encoding.hpp"
#include "language/language_profile.hpp"
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
            static bool CanOpen(const std::filesystem::path& Path);

            // Opens the file, focusing an already open tab if there is one.
            // Returns false when the file cannot be read or is neither BASIC nor an effect.
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

            void OpenFindReplace();
            void FormatCurrent();

            // Execution diagnostics, keyed by 1-based line number.
            void SetErrorMarkers(const std::map<int, std::string>& Errors);
            void SetErrorMarkers(const std::filesystem::path& Path, const std::map<int, std::string>& Errors);
            void ClearErrorMarkers();

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
            };

            void AddDocument(std::filesystem::path Path, dbc::language::LanguageProfile Profile, bool Basic,
                             const std::string& Text, const core::TextFormat& Format = {});
            // The document's text in its file format, or UTF-8 when the code page cannot hold it.
            static std::string EncodedText(const Document& Doc, bool& Lossless);
            void ConfigureEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile) const;
            void ConfigureEffectEditor(TextEditor& Editor) const;
            bool WriteDocument(Document& Doc, const std::filesystem::path& Path);
            Document* Find(const std::filesystem::path& Path) const;
            static bool IsDirty(const Document& Doc);
            void DrawCloseConfirm();

            std::vector<std::unique_ptr<Document>> Documents_;
            int Current_ = -1;
            // A tab to bring to the front on the next Draw: ImGui only selects a tab
            // the user did not click when asked to.
            const Document* SelectRequest_ = nullptr;
            int UntitledCount_ = 0;
            int PendingClose_ = -1;
            std::vector<std::string> Notices_;

            const dba::CommandDatabase* Commands_ = nullptr;
            TextEditor::AutoCompleteConfig AutoComplete_;
            dbc::language::LanguageProfile ProjectProfile_ = dbc::language::LanguageProfile::Dbc;
        };
    } // namespace panels

} // namespace dbc::studio
