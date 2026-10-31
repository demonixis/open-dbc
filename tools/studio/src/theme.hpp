#pragma once

#include <imgui.h>

#include <string_view>

// The dark and light palettes share these names, and SetAppearance fills them. Both derive from
// Doug Binks' ImGui style, on the VS Code "Dark+" and "Light+" syntax colors.
namespace dbc::studio
{
    namespace Theme
    {
        enum class Appearance
        {
            Dark,
            Light,
        };

        inline Appearance Current = Appearance::Dark;

        // Chrome
        inline ImU32 Background = 0;
        inline ImU32 Panel = 0;
        inline ImU32 PanelAlt = 0;
        inline ImU32 Border = 0;
        inline ImU32 Hover = 0;
        inline ImU32 Selection = 0;
        inline ImU32 Accent = 0;
        inline ImU32 AccentHover = 0;

        // Text
        inline ImU32 Text = 0;
        inline ImU32 TextDim = 0;
        inline ImU32 LineNumber = 0;

        // Syntax / diagnostics
        inline ImU32 Keyword = 0;
        inline ImU32 Identifier = 0;
        inline ImU32 Command = 0;
        inline ImU32 String = 0;
        inline ImU32 Number = 0;
        inline ImU32 Comment = 0;
        inline ImU32 Control = 0;
        inline ImU32 Error = 0;
        inline ImU32 ErrorLine = 0;
        inline ImU32 Run = 0;
        inline ImU32 Warning = 0;

        // The theme setting: "auto" follows the system, "light" is light and anything else dark.
        Appearance ResolveAppearance(std::string_view Setting, bool SystemIsLight);

        // Applies rounding and spacing to the current ImGui style, once.
        // `Scale` is the display content scale used to bake sizes.
        void ApplyMetrics(float Scale);

        // Fills the palette and the current ImGui style's colors. Call between frames.
        void SetAppearance(Appearance Value);

        // Adds Roboto as the interface font and Cousine as the code font.
        void AddFonts();

        // The fixed-pitch font TextEditor assumes. Null until AddFonts, which then keeps the current font.
        inline ImFont* CodeFont = nullptr;
        inline float CodeFontSize = 0.0f;

        // Pair with ImGui::PopFont.
        inline void PushCodeFont()
        {
            ImGui::PushFont(CodeFont, CodeFontSize);
        }
    } // namespace Theme

} // namespace dbc::studio
