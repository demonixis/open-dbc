#pragma once

#include "language/language_profile.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace dba
    {
        // A variable, a parameter or a field of a user type, spelled as it is declared.
        struct SourceVariable
        {
            std::string Name;
            bool Array = false;
            // "integer", "real", "string", "boolean" or the user type as declared.
            std::string Type;
            // The user type's canonical name, empty for a builtin type.
            std::string UserType;
        };

        struct SourceType
        {
            std::string Name;
            // Canonical, as SourceVariable::UserType names it.
            std::string Key;
            std::vector<SourceVariable> Fields;
        };

        struct SourceFunction
        {
            std::string Name;
            // "x", "speed#", "name$ as string": one per parameter.
            std::vector<std::string> Parameters;
            // 1-based lines of FUNCTION and ENDFUNCTION in the edited file; 0 for an included one.
            std::uint32_t FirstLine = 0;
            std::uint32_t LastLine = 0;
            // Parameters, then what the body declares or first assigns.
            std::vector<SourceVariable> Locals;
        };

        // The names a program declares, for completion. Built from the parser without binding,
        // so it also works on code that does not compile yet.
        struct SourceSymbols
        {
            std::vector<SourceFunction> Functions;
            std::vector<SourceType> Types;
            // Visible everywhere: GLOBAL variables and arrays.
            std::vector<SourceVariable> Shared;
            // Visible outside functions only: the main program's other variables.
            std::vector<SourceVariable> Main;
            std::vector<std::string> Labels;
            std::vector<std::string> Constants;
            // #INCLUDE paths as written.
            std::vector<std::filesystem::path> Includes;

            // The function whose body holds a 1-based line of the edited file, if any.
            const SourceFunction* FunctionAt(std::uint32_t Line) const;
            const SourceType* FindType(std::string_view Key) const;
        };

        SourceSymbols BuildSourceSymbols(const std::filesystem::path& LogicalPath, std::string Text,
                                         dbc::language::LanguageProfile Profile);

        // The text of a file, nothing when it cannot be read.
        using SourceReader = std::function<std::optional<std::string>(const std::filesystem::path&)>;

        // The edited file and every file its #INCLUDE lines reach, each resolved from the folder of the
        // file that names it and read once. An included function has no lines.
        SourceSymbols BuildProgramSymbols(const std::filesystem::path& Path, std::string Text,
                                          dbc::language::LanguageProfile Profile, const SourceReader& Read);
    } // namespace dba

} // namespace dbc::studio
