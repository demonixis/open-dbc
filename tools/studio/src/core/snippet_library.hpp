#pragma once

#include "language/language_profile.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        // A piece of BASIC the editor inserts at the cursor. Each one is a .dba
        // file whose leading lines may describe it:
        //
        //   rem Snippet: FPS Camera
        //   rem Profiles: dbc dbpro
        //   rem Description: Mouse look with WASD movement.
        //
        // The header is not inserted. Without it the name is the file stem and
        // the snippet is offered to both profiles.
        struct Snippet
        {
            std::string Name;
            std::string Description;
            bool Dbc = true;
            bool DbPro = true;
            std::string Body;
            // Empty for the snippets built into the Studio.
            std::filesystem::path Path;

            bool Supports(dbc::language::LanguageProfile Profile) const
            {
                return Profile == dbc::language::LanguageProfile::DbPro ? DbPro : Dbc;
            }
        };

        Snippet ParseSnippet(std::string_view FileName, std::string_view Text);
        std::string FormatSnippet(const Snippet& Value);

        // Built-in snippets come first, then the user's, each sorted by name.
        std::vector<Snippet> BuiltinSnippets();
        std::vector<Snippet> LoadSnippets(const std::filesystem::path& Folder);

        // Writes `Value` to Folder/<name>.dba, never replacing an existing file.
        // Returns the written path, or an empty path with `Error` set.
        std::filesystem::path SaveSnippet(const std::filesystem::path& Folder, const Snippet& Value,
                                          std::string& Error);
    } // namespace core

} // namespace dbc::studio
