#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dbc
{
    // Narrow text at SDL, BASIC, UI and serialization boundaries is always UTF-8.
    // std::filesystem::path's narrow constructor and string() use the ANSI code page on Windows.
    inline std::filesystem::path PathFromUtf8(std::string_view Text)
    {
        return std::filesystem::path(std::u8string(Text.begin(), Text.end()));
    }

    inline std::string PathToUtf8(const std::filesystem::path& Path)
    {
        const std::u8string Text = Path.u8string();
        return std::string(Text.begin(), Text.end());
    }

    inline std::string GenericPathToUtf8(const std::filesystem::path& Path)
    {
        const std::u8string Text = Path.generic_u8string();
        return std::string(Text.begin(), Text.end());
    }
} // namespace dbc
