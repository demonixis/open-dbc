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
        struct ProgramInfo
        {
            std::filesystem::path Path;
            std::string RelativeName;
            std::uintmax_t Size = 0;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
            std::string Title;
            std::string Author;
            std::string Date;
            bool Analyzed = false;
            int LineCount = 0;
            int CodeLines = 0;
            int CommentLines = 0;
            std::vector<std::string> Commands;
            std::vector<std::string> UnsupportedUsed;

            struct CompatibilityStubUse
            {
                std::string Name;
                bool Permanent = false;
            };
            std::vector<CompatibilityStubUse> CompatibilityStubsUsed;

            struct UnknownLine
            {
                int Line = 0;
                std::string Text;
            };
            std::vector<UnknownLine> Unrecognised;
        };

        std::vector<ProgramInfo> ScanPrograms(const std::filesystem::path& Root,
                                              dbc::language::LanguageProfile Profile);
        void AnalyzeProgram(ProgramInfo& Info);
    } // namespace dba

} // namespace dbc::studio
