#include "language_profile.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>

namespace
{
    std::string LowerAscii(std::string Value)
    {
        std::transform(Value.begin(), Value.end(), Value.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        return Value;
    }
} // namespace
namespace dbc::language
{
    std::string_view ProfileName(LanguageProfile Profile)
    {
        switch (Profile)
        {
            case LanguageProfile::Dbc:
                return "DBC";
            case LanguageProfile::DbPro:
                return "DBPro";
        }
        return {};
    }

    std::optional<LanguageProfile> ParseProfile(std::string_view Name)
    {
        const std::string Normalized = LowerAscii(std::string(Name));
        if (Normalized == "dbc")
        {
            return LanguageProfile::Dbc;
        }
        if (Normalized == "dbpro")
        {
            return LanguageProfile::DbPro;
        }
        return std::nullopt;
    }

    bool IsLanguageSource(const std::filesystem::path& Path)
    {
        return LowerAscii(dbc::PathToUtf8(Path.extension())) == ".dba";
    }

    bool IsDbProProject(const std::filesystem::path& Path)
    {
        return LowerAscii(dbc::PathToUtf8(Path.extension())) == ".dbpro";
    }

    bool Supports(LanguageProfile Profile, LanguageFeature Feature)
    {
        switch (Feature)
        {
            case LanguageFeature::ProfessionalGrammar:
                return Profile == LanguageProfile::DbPro;
        }
        return false;
    }
} // namespace dbc::language
