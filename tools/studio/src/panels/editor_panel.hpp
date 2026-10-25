#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

            // Opens the file, focusing an already open tab if there is one.
            // Returns false when the file cannot be read.
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

            // Writes the current document; false when it has no path yet.
            bool SaveCurrent();
            bool SaveCurrentAs(const std::filesystem::path& Path);
            bool SaveAll();

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
                std::string Title;
                std::unique_ptr<TextEditor> Editor;
                size_t SavedUndoIndex = 0;
                bool Open = true;
            };

            void AddDocument(std::filesystem::path Path, dbc::language::LanguageProfile Profile,
                             const std::string& Text);
            void ConfigureEditor(TextEditor& Editor, dbc::language::LanguageProfile Profile) const;
            dbc::language::LanguageProfile CurrentProfile() const;
            bool WriteDocument(Document& Doc, const std::filesystem::path& Path);
            static bool IsDirty(const Document& Doc);
            void DrawCloseConfirm();

            std::vector<std::unique_ptr<Document>> Documents_;
            int Current_ = -1;
            int UntitledCount_ = 0;
            int PendingClose_ = -1;

            const dba::CommandDatabase* Commands_ = nullptr;
            TextEditor::AutoCompleteConfig AutoComplete_;
            dbc::language::LanguageProfile ProjectProfile_ = dbc::language::LanguageProfile::Dbc;
        };
    } // namespace panels

} // namespace dbc::studio
