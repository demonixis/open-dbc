#pragma once

#include "language/language_profile.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace dba
    {
        // One declared name in a source, spelled as it first appears.
        struct OutlineEntry
        {
            std::string Name;
            std::uint32_t Line = 0; // 1-based
            // Calls for a function, GOTO/GOSUB jumps for a label, fields for a type.
            int Count = 0;
            // Variables only: true for arrays.
            bool Array = false;
        };

        // The declarations of a single source, as the Studio's Code panel lists them.
        // Built from the frontend parser without binding, so it also works on code
        // that does not compile yet; INCLUDEd files are not followed.
        struct CodeOutline
        {
            std::vector<OutlineEntry> Functions;
            std::vector<OutlineEntry> Labels;
            std::vector<OutlineEntry> Types;
            // Main-program variables plus globals declared in functions; function
            // locals and parameters are left out.
            std::vector<OutlineEntry> Variables;
        };

        CodeOutline BuildCodeOutline(const std::filesystem::path& LogicalPath, std::string Text,
                                     dbc::language::LanguageProfile Profile);
    } // namespace dba

} // namespace dbc::studio
