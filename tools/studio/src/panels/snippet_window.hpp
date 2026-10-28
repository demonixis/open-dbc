#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/snippet_library.hpp"

namespace dbc::studio
{
    namespace panels
    {
        // Edit > Insert Snippet and Edit > Save Selection as Snippet: the built-in
        // snippets plus the user's own, stored as .dba files in `UserFolder`.
        class SnippetWindow
        {
          public:
            void SetUserFolder(std::filesystem::path Folder)
            {
                UserFolder_ = std::move(Folder);
            }

            // Lists the snippets the editor's profile can use.
            void OpenInsert(dbc::language::LanguageProfile Profile);
            // Offers to store `Body`, written for `Profile`, as a user snippet.
            void OpenSave(std::string Body, dbc::language::LanguageProfile Profile);

            // Returns the body to insert once the user picks a snippet.
            std::optional<std::string> Draw();

          private:
            std::optional<std::string> DrawInsert();
            void DrawSave();
            void Reload();

            std::filesystem::path UserFolder_;
            std::vector<core::Snippet> Snippets_;
            std::size_t BuiltinCount_ = 0;
            dbc::language::LanguageProfile Profile_ = dbc::language::LanguageProfile::Dbc;
            int Selected_ = -1;
            std::array<char, 64> Filter_{};
            bool InsertRequested_ = false;

            bool SaveRequested_ = false;
            std::array<char, 64> SaveName_{};
            std::array<char, 256> SaveDescription_{};
            bool SaveBothProfiles_ = false;
            std::string SaveBody_;
            std::string SaveError_;
        };
    } // namespace panels

} // namespace dbc::studio
