#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "language/language_profile.hpp"

// Hooks generated files into the program's main source: one #include and one
// call per generated function, added once and found again wherever the user
// moved them. What the program already imports stays its own business.
namespace dbc::studio
{
    namespace scene
    {
        struct MainLink
        {
            std::string IncludePath; // as written in #include, relative to the main file
            std::string Function;    // called with no arguments
            bool Imported = false;   // the program reaches the file through another of its includes
        };

        struct LinkResult
        {
            std::string Text;
            bool Changed = false;
        };

        // Adds the missing includes to the include block under the file's leading
        // comments, then their calls after that block, in the order of `Links`. An
        // imported file gets no call: the program calls it where it wants. Keeps the
        // file's line endings.
        LinkResult LinkIntoMain(const std::string& MainText, dbc::language::LanguageProfile Profile,
                                const std::vector<MainLink>& Links);

        using SourceReader = std::function<std::optional<std::string>(const std::filesystem::path&)>;

        // Whether `Root` reaches `Target` through its #include lines, read with `Read`.
        bool SourceIncludes(const std::filesystem::path& Root, const std::filesystem::path& Target,
                            dbc::language::LanguageProfile Profile, const SourceReader& Read);

        // What a program already imports from the levels folder, as seen from one level.
        struct LevelImports
        {
            bool Level = false;      // this level's file
            bool World = false;      // this level's world settings
            bool OtherLevel = false; // any file of another level
        };

        LevelImports FindLevelImports(const std::filesystem::path& Main, const std::filesystem::path& LevelFolder,
                                      const std::string& LevelName, dbc::language::LanguageProfile Profile,
                                      const SourceReader& Read);
    } // namespace scene

} // namespace dbc::studio
