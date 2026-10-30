#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

// Interface strings. The English source text is the key, so a missing translation shows the
// English. What these functions return must never reach a project file, generated code or a setting.
namespace dbc::studio
{
    struct TextHash
    {
        using is_transparent = void;

        std::size_t operator()(std::string_view Text) const noexcept
        {
            return std::hash<std::string_view>{}(Text);
        }
    };

    // Source text -> translation.
    using TextTable = std::unordered_map<std::string, std::string, TextHash, std::equal_to<>>;

    // Null shows the English. Call between frames on the UI thread, since this invalidates what TrId
    // returned; the table must stay alive, as a worker may still be reading it.
    void SetActiveTranslations(const TextTable* Translations);

    // Safe on any thread: exports resolve their messages on a worker.
    const char* Tr(const char* Source);

    // A homonym: "scene node|Light" has its own catalogue entry and shows "Light" in English.
    const char* TrC(const char* ContextAndSource);

    // The label of anything with an ImGui ID: "Output" -> "Sortie###Output". The ID ignores the
    // language, so saved layouts survive a change and two labels translated alike never collide. A
    // "##" suffix of Source goes to the ID only. UI thread only; lasts until the language changes.
    const char* TrId(const char* Source);

    // Tr, then {0}, {1}... become Arguments, in the order the translation wants. Anything else in
    // braces stays as it is: a faulty translation must not throw or read arbitrary arguments.
    std::string TrFmt(const char* Source, std::initializer_list<std::string_view> Arguments);
} // namespace dbc::studio
