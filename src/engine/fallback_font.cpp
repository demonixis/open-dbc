#include "fallback_font.hpp"
#include "noto_sans_regular_ttf.hpp"

namespace dbc
{
    std::span<const unsigned char> FallbackFontData()
    {
        return {third_party_fonts_NotoSans_Regular_ttf, third_party_fonts_NotoSans_Regular_ttf_len};
    }
} // namespace dbc
