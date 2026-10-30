#include "locales.hpp"
#include "engine/systems/log.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

// Defines KTranslationCatalogueFiles from tools/studio/locales.
#include "studio_locales.inc"

namespace dbc::studio
{
    namespace core
    {
        namespace
        {
            struct EmbeddedCatalogue
            {
                std::string Code;
                TranslationCatalogue Catalogue;
            };

            // Never freed, so that a worker can keep reading translations across a language change.
            const std::vector<EmbeddedCatalogue>& EmbeddedCatalogues()
            {
                static const std::vector<EmbeddedCatalogue> Catalogues = []
                {
                    std::vector<EmbeddedCatalogue> Result;
                    for (const auto& [FileName, Text] : KTranslationCatalogueFiles)
                    {
                        std::string Error;
                        std::optional<TranslationCatalogue> Catalogue = ParseTranslationCatalogue(Text, Error);
                        if (!Error.empty())
                            DBC_LOG_ERROR("Translation catalogue {}: {}", FileName, Error);
                        if (Catalogue)
                            Result.push_back(
                                {std::string(FileName.substr(0, FileName.rfind('.'))), std::move(*Catalogue)});
                    }
                    return Result;
                }();
                return Catalogues;
            }

            // "fr-CA", "fr_CA" and "FR" -> "fr".
            std::string BaseLanguage(std::string_view Locale)
            {
                std::string Code(Locale.substr(0, Locale.find_first_of("-_")));
                std::transform(Code.begin(), Code.end(), Code.begin(),
                               [](unsigned char Value) { return static_cast<char>(std::tolower(Value)); });
                return Code;
            }

            bool IsInterfaceLanguage(std::string_view Code)
            {
                const std::vector<InterfaceLanguage>& Languages = InterfaceLanguages();
                return std::any_of(Languages.begin(), Languages.end(),
                                   [Code](const InterfaceLanguage& Language) { return Language.Code == Code; });
            }

            // Latin-1 only: the interface font has no other accented letters.
            std::string_view PseudoLetter(char Letter)
            {
                switch (Letter)
                {
                    case 'a':
                        return "à";
                    case 'c':
                        return "ç";
                    case 'e':
                        return "é";
                    case 'i':
                        return "î";
                    case 'n':
                        return "ñ";
                    case 'o':
                        return "ô";
                    case 'u':
                        return "ü";
                    case 'y':
                        return "ý";
                    case 'A':
                        return "À";
                    case 'C':
                        return "Ç";
                    case 'E':
                        return "É";
                    case 'I':
                        return "Î";
                    case 'N':
                        return "Ñ";
                    case 'O':
                        return "Ô";
                    case 'U':
                        return "Ü";
                    case 'Y':
                        return "Ý";
                    default:
                        return {};
                }
            }

            // How long the {0} or printf conversion that Text starts with is, else 0.
            std::size_t PlaceholderLength(std::string_view Text)
            {
                if (Text.front() == '{')
                {
                    const std::size_t End = Text.find_first_not_of("0123456789", 1);
                    return End != std::string_view::npos && End > 1 && Text[End] == '}' ? End + 1 : 0;
                }
                if (Text.front() == '%')
                {
                    const std::size_t End = Text.find_first_not_of("-+ #0123456789.lhzjt", 1);
                    return End != std::string_view::npos ? End + 1 : 0;
                }
                return 0;
            }

#ifndef NDEBUG
            constexpr std::string_view KPseudoCode = "pseudo";

            // A key's English text is all a pseudo-translation needs, so no catalogue of its own.
            const TextTable& PseudoTranslations()
            {
                static const TextTable Table = []
                {
                    TextTable Result;
                    for (const EmbeddedCatalogue& Embedded : EmbeddedCatalogues())
                    {
                        for (const auto& [Source, Translation] : Embedded.Catalogue.Entries)
                        {
                            const std::string_view Key(Source);
                            Result.emplace(Source, PseudoLocalize(Key.substr(Key.find('|') + 1)));
                        }
                    }
                    return Result;
                }();
                return Table;
            }
#endif
        } // namespace

        std::optional<TranslationCatalogue> ParseTranslationCatalogue(std::string_view Text, std::string& Error)
        {
            Error.clear();
            const nlohmann::json Json = nlohmann::json::parse(Text, nullptr, false);
            if (Json.is_discarded() || !Json.is_object())
            {
                Error = "A translation catalogue must be a JSON object";
                return std::nullopt;
            }
            const auto Name = Json.find("@name");
            if (Name == Json.end() || !Name->is_string())
            {
                Error = "A translation catalogue must give its language's @name";
                return std::nullopt;
            }

            TranslationCatalogue Catalogue;
            Catalogue.Name = Name->get<std::string>();
            for (const auto& [Source, Translation] : Json.items())
            {
                if (Source.starts_with('@'))
                    continue;
                if (Translation.is_string())
                    Catalogue.Entries.emplace(Source, Translation.get<std::string>());
                else if (Error.empty())
                    Error = "Ignoring a translation that is not a string: " + Source;
            }
            return Catalogue;
        }

        const std::vector<InterfaceLanguage>& InterfaceLanguages()
        {
            static const std::vector<InterfaceLanguage> Languages = []
            {
                std::vector<InterfaceLanguage> Result{{"en", "English"}};
                for (const EmbeddedCatalogue& Embedded : EmbeddedCatalogues())
                    Result.push_back({Embedded.Code, Embedded.Catalogue.Name});
#ifndef NDEBUG
                Result.push_back({std::string(KPseudoCode), "Pseudo"});
#endif
                return Result;
            }();
            return Languages;
        }

        std::string ResolveInterfaceLanguage(std::string_view Setting, std::span<const std::string> SystemLocales)
        {
            if (Setting != "auto")
            {
                if (IsInterfaceLanguage(Setting))
                    return std::string(Setting);
                DBC_LOG_WARN("Unknown interface language: {}", Setting);
            }
            for (const std::string& Locale : SystemLocales)
            {
                std::string Code = BaseLanguage(Locale);
                if (IsInterfaceLanguage(Code))
                    return Code;
            }
            return "en";
        }

        void ActivateInterfaceLanguage(std::string_view Code)
        {
#ifndef NDEBUG
            if (Code == KPseudoCode)
            {
                SetActiveTranslations(&PseudoTranslations());
                return;
            }
#endif
            for (const EmbeddedCatalogue& Embedded : EmbeddedCatalogues())
            {
                if (Embedded.Code == Code)
                {
                    SetActiveTranslations(&Embedded.Catalogue.Entries);
                    return;
                }
            }
            SetActiveTranslations(nullptr);
        }

        std::string PseudoLocalize(std::string_view Text)
        {
            std::string Result = "[";
            std::size_t Letters = 0;
            for (std::size_t Index = 0; Index < Text.size();)
            {
                if (const std::size_t Length = PlaceholderLength(Text.substr(Index)))
                {
                    Result += Text.substr(Index, Length);
                    Index += Length;
                    continue;
                }
                const std::string_view Accented = PseudoLetter(Text[Index]);
                if (Accented.empty())
                    Result += Text[Index];
                else
                    Result += Accented;
                Letters += std::isalpha(static_cast<unsigned char>(Text[Index])) != 0;
                ++Index;
            }
            Result.append(std::max<std::size_t>(Letters * 2 / 5, 1), '~');
            Result += ']';
            return Result;
        }

        void TranslateImGuiStrings()
        {
            const ImGuiLocEntry Entries[] = {
                {ImGuiLocKey_TableSizeOne, TrId("Size column to fit###SizeOne")},
                {ImGuiLocKey_TableSizeAllFit, TrId("Size all columns to fit###SizeAll")},
                {ImGuiLocKey_TableSizeAllDefault, TrId("Size all columns to default###SizeAll")},
                {ImGuiLocKey_TableReset, Tr("Reset")},
                {ImGuiLocKey_TableResetOrder, TrId("Reset order###ResetOrder")},
                {ImGuiLocKey_TableResetVisibility, TrId("Reset visibility###ResetVisibility")},
                {ImGuiLocKey_WindowingMainMenuBar, Tr("(Main menu bar)")},
                {ImGuiLocKey_WindowingPopup, Tr("(Popup)")},
                {ImGuiLocKey_WindowingUntitled, Tr("(Untitled)")},
                {ImGuiLocKey_OpenLink_s, Tr("Open '%s'")},
                {ImGuiLocKey_CopyLink, TrId("Copy Link###CopyLink")},
                {ImGuiLocKey_DockingHideTabBar, TrId("Hide tab bar###HideTabBar")},
                {ImGuiLocKey_DockingHoldShiftToDock, Tr("Hold SHIFT to enable Docking window.")},
                {ImGuiLocKey_DockingDragToUndockOrMoveNode, Tr("Click and drag to move or undock whole node.")},
            };
            // Everything but the version string: a new ImGui key must be added above.
            static_assert(std::size(Entries) == ImGuiLocKey_COUNT - 1);
            ImGui::LocalizeRegisterEntries(Entries, static_cast<int>(std::size(Entries)));
        }
    } // namespace core

} // namespace dbc::studio
