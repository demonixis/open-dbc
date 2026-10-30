#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "i18n.hpp"

namespace dbc::studio
{
    namespace core
    {
        struct InterfaceLanguage
        {
            std::string Code; // "fr", the name of its catalogue file
            std::string Name; // "Français", its own name
        };

        struct TranslationCatalogue
        {
            std::string Name;
            TextTable Entries;
        };

        // A flat JSON object of source text -> translation, plus the language's "@name". Error also
        // reports entries that were ignored, so it can be set on a usable catalogue.
        std::optional<TranslationCatalogue> ParseTranslationCatalogue(std::string_view Text, std::string& Error);

        // English, then every embedded catalogue.
        const std::vector<InterfaceLanguage>& InterfaceLanguages();

        // Setting is "auto" or a language code. "auto" and an unknown code follow the system's locales
        // in order of preference, where "fr-CA" and "fr_CA" both mean "fr", then fall back to English.
        std::string ResolveInterfaceLanguage(std::string_view Setting, std::span<const std::string> SystemLocales);

        // An unknown code shows English. Same thread and timing rules as SetActiveTranslations.
        void ActivateInterfaceLanguage(std::string_view Code);
    } // namespace core

} // namespace dbc::studio
