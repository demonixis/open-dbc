#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dbc::language
{
    enum class LanguageProfile
    {
        Dbc,
        DbPro,
    };

    enum class LanguageFeature
    {
        ProfessionalGrammar,
    };

    std::string_view ProfileName(LanguageProfile Profile);
    std::optional<LanguageProfile> ParseProfile(std::string_view Name);
    bool IsLanguageSource(const std::filesystem::path& Path);
    bool IsDbProProject(const std::filesystem::path& Path);
    bool Supports(LanguageProfile Profile, LanguageFeature Feature);
} // namespace dbc::language
