#pragma once

#include <imgui.h>

// Dark palette carried over from the Qt tools (VS Code "Dark+" family) so the
// studio keeps the look the editor and launcher already had.
namespace dbc::studio
{
    namespace Theme
    {
        // Chrome
        inline constexpr ImU32 Background = IM_COL32(0x1e, 0x1e, 0x1e, 0xff);
        inline constexpr ImU32 Panel = IM_COL32(0x25, 0x25, 0x26, 0xff);
        inline constexpr ImU32 PanelAlt = IM_COL32(0x2d, 0x2d, 0x2d, 0xff);
        inline constexpr ImU32 Border = IM_COL32(0x3c, 0x3c, 0x3c, 0xff);
        inline constexpr ImU32 Hover = IM_COL32(0x2a, 0x2d, 0x2e, 0xff);
        inline constexpr ImU32 Selection = IM_COL32(0x09, 0x47, 0x71, 0xff);
        inline constexpr ImU32 Accent = IM_COL32(0x0e, 0x63, 0x9c, 0xff);
        inline constexpr ImU32 AccentHover = IM_COL32(0x11, 0x77, 0xbb, 0xff);

        // Text
        inline constexpr ImU32 Text = IM_COL32(0xd4, 0xd4, 0xd4, 0xff);
        inline constexpr ImU32 TextDim = IM_COL32(0x96, 0x96, 0x96, 0xff);
        inline constexpr ImU32 LineNumber = IM_COL32(0x85, 0x85, 0x85, 0xff);

        // Syntax / diagnostics
        inline constexpr ImU32 Keyword = IM_COL32(0x56, 0x9c, 0xd6, 0xff);
        inline constexpr ImU32 Identifier = IM_COL32(0x9c, 0xdc, 0xfe, 0xff);
        inline constexpr ImU32 Command = IM_COL32(0x6c, 0x9b, 0xd2, 0xff);
        inline constexpr ImU32 String = IM_COL32(0xce, 0x91, 0x78, 0xff);
        inline constexpr ImU32 Number = IM_COL32(0xb5, 0xce, 0xa8, 0xff);
        inline constexpr ImU32 Comment = IM_COL32(0x6a, 0x99, 0x55, 0xff);
        inline constexpr ImU32 Control = IM_COL32(0xc5, 0x86, 0xc0, 0xff);
        inline constexpr ImU32 Error = IM_COL32(0xff, 0x66, 0x66, 0xff);
        inline constexpr ImU32 ErrorLine = IM_COL32(0x5a, 0x1d, 0x1d, 0xff);
        inline constexpr ImU32 Run = IM_COL32(0x4e, 0xc9, 0x6b, 0xff);
        inline constexpr ImU32 Warning = IM_COL32(0xe5, 0xc0, 0x7b, 0xff);

        // Applies colors, rounding and spacing to the current ImGui style.
        // `scale` is the display content scale used to bake sizes.
        void Apply(float Scale);

        // Call once the style's font scale is set, which decides the size of ImGui's default font.
        void AddFonts();
    } // namespace Theme

} // namespace dbc::studio
