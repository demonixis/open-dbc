#include "i18n.hpp"

#include <atomic>
#include <charconv>
#include <cstring>
#include <system_error>

namespace dbc::studio
{
    namespace
    {
        std::atomic<const TextTable*> ActiveTranslations{nullptr};

        // ImGui reads labels every frame: composing each one once keeps that free of allocations.
        TextTable& IdLabels()
        {
            static TextTable Labels;
            return Labels;
        }
    } // namespace

    void SetActiveTranslations(const TextTable* Translations)
    {
        ActiveTranslations.store(Translations, std::memory_order_release);
        IdLabels().clear();
    }

    const char* Tr(const char* Source)
    {
        const TextTable* Translations = ActiveTranslations.load(std::memory_order_acquire);
        if (Translations == nullptr)
            return Source;
        const auto Found = Translations->find(std::string_view(Source));
        return Found == Translations->end() ? Source : Found->second.c_str();
    }

    const char* TrC(const char* ContextAndSource)
    {
        const char* Translated = Tr(ContextAndSource);
        if (Translated != ContextAndSource)
            return Translated;
        const char* Separator = std::strchr(ContextAndSource, '|');
        return Separator == nullptr ? ContextAndSource : Separator + 1;
    }

    const char* TrId(const char* Source)
    {
        TextTable& Labels = IdLabels();
        const auto Found = Labels.find(std::string_view(Source));
        if (Found != Labels.end())
            return Found->second.c_str();

        const std::string_view Whole(Source);
        const std::string Text(Whole.substr(0, Whole.find("##")));
        std::string Label = std::string(Tr(Text.c_str())) + "###" + std::string(Whole);
        return Labels.emplace(Whole, std::move(Label)).first->second.c_str();
    }

    std::string TrFmt(const char* Source, std::initializer_list<std::string_view> Arguments)
    {
        const std::string_view Pattern(Tr(Source));
        const char* const End = Pattern.data() + Pattern.size();
        std::string Result;
        Result.reserve(Pattern.size());
        for (const char* At = Pattern.data(); At != End;)
        {
            if (*At != '{')
            {
                Result.push_back(*At++);
                continue;
            }
            std::size_t Index = 0;
            const auto [Next, Status] = std::from_chars(At + 1, End, Index);
            if (Status != std::errc{} || Next == End || *Next != '}' || Index >= Arguments.size())
            {
                Result.push_back(*At++);
                continue;
            }
            Result.append(*(Arguments.begin() + Index));
            At = Next + 1;
        }
        return Result;
    }
} // namespace dbc::studio
