#pragma once

#include <span>

namespace dbc
{
    // Noto Sans Regular, embedded: the bytes of a TrueType file that lives as long as the process.
    std::span<const unsigned char> FallbackFontData();
} // namespace dbc
