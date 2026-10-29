#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dbc::studio::core
{
    // How a source file stores its text, so that saving writes it back the same way.
    struct TextFormat
    {
        // Files that are not valid UTF-8 come from DarkBASIC's Windows code page.
        bool Windows1252 = false;
        bool Bom = false;
        bool Crlf = false;
    };

    // Detects the format of a file's bytes and returns its text as UTF-8, without the BOM.
    std::string DecodeText(std::string_view Bytes, TextFormat& Format);

    // Encodes editor text, UTF-8 with LF line endings, in Format. Nullopt when a character has no
    // Windows-1252 byte.
    std::optional<std::string> EncodeText(std::string_view Text, const TextFormat& Format);
} // namespace dbc::studio::core
