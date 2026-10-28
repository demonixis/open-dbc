#pragma once

#include <string>
#include <vector>

#include "language/language_profile.hpp"

// Hooks generated files into the program's main source: one #include and one
// call per generated function, added once and found again wherever the user
// moved them.
namespace dbc::studio
{
    namespace scene
    {
        struct MainLink
        {
            std::string IncludePath; // as written in #include, relative to the main file
            std::string Function;    // called with no arguments
        };

        struct LinkResult
        {
            std::string Text;
            bool Changed = false;
        };

        // Adds the missing includes to the include block under the file's leading
        // comments, then the missing calls after that block, in the order of
        // `Links`. Keeps the file's line endings.
        LinkResult LinkIntoMain(const std::string& MainText, dbc::language::LanguageProfile Profile,
                                const std::vector<MainLink>& Links);
    } // namespace scene

} // namespace dbc::studio
