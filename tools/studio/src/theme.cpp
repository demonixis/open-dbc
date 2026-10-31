#include "theme.hpp"
#include "engine/fallback_font.hpp"

#include <misc/freetype/imgui_freetype.h>

#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

// Defines KStudioFontFiles from Dear ImGui's misc/fonts.
#include "studio_fonts.inc"

namespace dbc::studio
{
    namespace
    {
        constexpr float KInterfaceFontSize = 15.0f;
        constexpr float KCodeFontSize = 14.0f;

        // The data lives as long as the process: the atlas neither frees nor writes it.
        ImFont* AddFont(std::span<const unsigned char> Data, float Size, ImFontConfig Config)
        {
            Config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(Data.data()),
                                                              static_cast<int>(Data.size()), Size, &Config);
        }

        ImFont* AddEmbeddedFont(std::string_view File, float Size)
        {
            for (const auto& [Name, Bytes] : KStudioFontFiles)
            {
                if (Name != File)
                    continue;
                ImFontConfig Config;
                std::snprintf(Config.Name, sizeof(Config.Name), "%.*s", static_cast<int>(File.size()), File.data());
                return AddFont(std::span(reinterpret_cast<const unsigned char*>(Bytes.data()), Bytes.size()), Size,
                               Config);
            }
            return nullptr;
        }

        // Noto Sans, already embedded for the engine, supplies what the font before it lacks.
        void MergeFallback(float Size)
        {
            ImFontConfig Config;
            Config.MergeMode = true;
            AddFont(dbc::FallbackFontData(), Size, Config);
        }

        ImVec4 Col(ImU32 C)
        {
            return ImGui::ColorConvertU32ToFloat4(C);
        }

        ImVec4 Alpha(ImU32 C, float A)
        {
            ImVec4 V = Col(C);
            V.w = A;
            return V;
        }

        void FillDarkPalette()
        {
            using namespace Theme;
            Background = IM_COL32(0x2d, 0x30, 0x34, 0xff);
            Panel = IM_COL32(0x36, 0x39, 0x3e, 0xff);
            PanelAlt = IM_COL32(0x21, 0x24, 0x28, 0xff);
            Border = IM_COL32(0x4a, 0x4e, 0x54, 0xff);
            Hover = IM_COL32(0x3e, 0x42, 0x48, 0xff);
            Selection = IM_COL32(0x2d, 0x5f, 0x9e, 0xff);
            Accent = IM_COL32(0x2f, 0x6d, 0xb5, 0xff);
            AccentHover = IM_COL32(0x42, 0x96, 0xfa, 0xff);

            Text = IM_COL32(0xf0, 0xf1, 0xf2, 0xff);
            TextDim = IM_COL32(0xa0, 0xa4, 0xaa, 0xff);
            LineNumber = IM_COL32(0x88, 0x8c, 0x92, 0xff);

            Keyword = IM_COL32(0x5a, 0x9f, 0xd8, 0xff);
            Identifier = IM_COL32(0x9c, 0xdc, 0xfe, 0xff);
            Command = IM_COL32(0x6c, 0x9b, 0xd2, 0xff);
            String = IM_COL32(0xce, 0x91, 0x78, 0xff);
            Number = IM_COL32(0xb5, 0xce, 0xa8, 0xff);
            Comment = IM_COL32(0x7f, 0xad, 0x6a, 0xff);
            Control = IM_COL32(0xc5, 0x86, 0xc0, 0xff);
            Error = IM_COL32(0xff, 0x66, 0x66, 0xff);
            ErrorLine = IM_COL32(0x5a, 0x1d, 0x1d, 0xff);
            Run = IM_COL32(0x4e, 0xc9, 0x6b, 0xff);
            Warning = IM_COL32(0xe5, 0xc0, 0x7b, 0xff);
        }

        void FillLightPalette()
        {
            using namespace Theme;
            Background = IM_COL32(0xf1, 0xf2, 0xf4, 0xff);
            Panel = IM_COL32(0xe3, 0xe5, 0xe8, 0xff);
            PanelAlt = IM_COL32(0xff, 0xff, 0xff, 0xff);
            Border = IM_COL32(0xc2, 0xc6, 0xcc, 0xff);
            Hover = IM_COL32(0xd6, 0xda, 0xdf, 0xff);
            Selection = IM_COL32(0xb9, 0xd4, 0xf5, 0xff);
            Accent = IM_COL32(0x2f, 0x6d, 0xb5, 0xff);
            AccentHover = IM_COL32(0x2f, 0x7f, 0xe0, 0xff);

            Text = IM_COL32(0x1c, 0x1e, 0x21, 0xff);
            TextDim = IM_COL32(0x62, 0x67, 0x6f, 0xff);
            LineNumber = IM_COL32(0x7a, 0x7f, 0x86, 0xff);

            Keyword = IM_COL32(0x0b, 0x5c, 0xad, 0xff);
            Identifier = IM_COL32(0x0f, 0x4c, 0x81, 0xff);
            Command = IM_COL32(0x1f, 0x6a, 0xa8, 0xff);
            String = IM_COL32(0xa3, 0x15, 0x15, 0xff);
            Number = IM_COL32(0x0a, 0x73, 0x49, 0xff);
            Comment = IM_COL32(0x3d, 0x7a, 0x32, 0xff);
            Control = IM_COL32(0x8e, 0x16, 0xa8, 0xff);
            Error = IM_COL32(0xc6, 0x28, 0x28, 0xff);
            ErrorLine = IM_COL32(0xfb, 0xe3, 0xe1, 0xff);
            Run = IM_COL32(0x13, 0x79, 0x3a, 0xff);
            Warning = IM_COL32(0x86, 0x56, 0x00, 0xff);
        }
    } // namespace

    namespace Theme
    {
        void AddFonts()
        {
            ImGui::GetIO().Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
            ImGui::GetStyle().FontSizeBase = KInterfaceFontSize;

            // The first font added is ImGui's default.
            AddEmbeddedFont("Roboto-Medium.ttf", KInterfaceFontSize);
            MergeFallback(KInterfaceFontSize);

            CodeFont = AddEmbeddedFont("Cousine-Regular.ttf", KCodeFontSize);
            CodeFontSize = KCodeFontSize;
            MergeFallback(KCodeFontSize);
        }

        Appearance ResolveAppearance(std::string_view Setting, bool SystemIsLight)
        {
            const bool Light = Setting == "light" || (Setting == "auto" && SystemIsLight);
            return Light ? Appearance::Light : Appearance::Dark;
        }

        void ApplyMetrics(float Scale)
        {
            ImGuiStyle& Style = ImGui::GetStyle();

            Style.WindowRounding = 0.0f;
            Style.ChildRounding = 2.0f;
            Style.FrameRounding = 3.0f;
            Style.PopupRounding = 3.0f;
            Style.ScrollbarRounding = 3.0f;
            Style.GrabRounding = 3.0f;
            Style.TabRounding = 3.0f;

            Style.WindowBorderSize = 1.0f;
            Style.FrameBorderSize = 0.0f;
            Style.PopupBorderSize = 1.0f;
            Style.TabBarBorderSize = 1.0f;

            Style.WindowPadding = ImVec2(9.0f, 9.0f);
            Style.FramePadding = ImVec2(6.0f, 3.0f);
            Style.ItemSpacing = ImVec2(8.0f, 5.0f);
            Style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
            Style.IndentSpacing = 16.0f;
            Style.ScrollbarSize = 12.0f;
            Style.GrabMinSize = 10.0f;

            Style.WindowMenuButtonPosition = ImGuiDir_None;
            Style.SeparatorTextBorderSize = 1.0f;

            Style.ScaleAllSizes(Scale);
        }

        void SetAppearance(Appearance Value)
        {
            Current = Value;
            const bool Dark = Value == Appearance::Dark;
            if (Dark)
                FillDarkPalette();
            else
                FillLightPalette();

            // ImGui's own style covers the slots the Studio leaves alone.
            if (Dark)
                ImGui::StyleColorsDark();
            else
                ImGui::StyleColorsLight();

            ImVec4* C = ImGui::GetStyle().Colors;
            C[ImGuiCol_Text] = Col(Text);
            C[ImGuiCol_TextDisabled] = Col(TextDim);
            C[ImGuiCol_WindowBg] = Col(Background);
            C[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            C[ImGuiCol_PopupBg] = Col(Panel);
            C[ImGuiCol_Border] = Col(Border);
            C[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

            C[ImGuiCol_FrameBg] = Col(PanelAlt);
            C[ImGuiCol_FrameBgHovered] = Col(Hover);
            C[ImGuiCol_FrameBgActive] = Col(Selection);

            C[ImGuiCol_TitleBg] = Col(Panel);
            C[ImGuiCol_TitleBgActive] = Col(Panel);
            C[ImGuiCol_TitleBgCollapsed] = Col(Panel);
            C[ImGuiCol_MenuBarBg] = Col(Panel);

            C[ImGuiCol_ScrollbarBg] = Col(Background);
            C[ImGuiCol_ScrollbarGrab] = Col(Border);
            C[ImGuiCol_ScrollbarGrabHovered] = Alpha(Text, 0.35f);
            C[ImGuiCol_ScrollbarGrabActive] = Alpha(Text, 0.55f);

            C[ImGuiCol_CheckMark] = Col(AccentHover);
            C[ImGuiCol_CheckboxSelectedBg] = Col(PanelAlt);
            C[ImGuiCol_InputTextCursor] = Col(Text);
            C[ImGuiCol_UnsavedMarker] = Col(AccentHover);

            // The button text stays readable on each of these: see test_studio_theme.
            if (Dark)
            {
                C[ImGuiCol_SliderGrab] = Col(AccentHover);
                C[ImGuiCol_SliderGrabActive] = Col(IM_COL32(0x73, 0xb8, 0xff, 0xff));
                C[ImGuiCol_Button] = Alpha(AccentHover, 0.40f);
                C[ImGuiCol_ButtonHovered] = Col(Accent);
                C[ImGuiCol_ButtonActive] = Col(Selection);
                C[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
            }
            else
            {
                C[ImGuiCol_SliderGrab] = Col(Accent);
                C[ImGuiCol_SliderGrabActive] = Col(AccentHover);
                C[ImGuiCol_Button] = Alpha(AccentHover, 0.30f);
                C[ImGuiCol_ButtonHovered] = Alpha(AccentHover, 0.55f);
                C[ImGuiCol_ButtonActive] = Alpha(AccentHover, 0.75f);
            }

            C[ImGuiCol_Header] = Col(Selection);
            C[ImGuiCol_HeaderHovered] = Col(Hover);
            C[ImGuiCol_HeaderActive] = Col(Selection);

            C[ImGuiCol_Separator] = Col(Border);
            C[ImGuiCol_SeparatorHovered] = Col(Accent);
            C[ImGuiCol_SeparatorActive] = Col(AccentHover);

            C[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            C[ImGuiCol_ResizeGripHovered] = Alpha(Accent, 0.6f);
            C[ImGuiCol_ResizeGripActive] = Col(Accent);

            C[ImGuiCol_Tab] = Col(Panel);
            C[ImGuiCol_TabHovered] = Col(Hover);
            C[ImGuiCol_TabSelected] = Col(Background);
            C[ImGuiCol_TabSelectedOverline] = Col(AccentHover);
            C[ImGuiCol_TabDimmed] = Col(Panel);
            C[ImGuiCol_TabDimmedSelected] = Col(PanelAlt);
            C[ImGuiCol_TabDimmedSelectedOverline] = Col(Border);

            C[ImGuiCol_DockingPreview] = Alpha(AccentHover, 0.7f);
            C[ImGuiCol_DockingEmptyBg] = Col(Background);

            C[ImGuiCol_TableHeaderBg] = Col(Panel);
            C[ImGuiCol_TableBorderStrong] = Col(Border);
            C[ImGuiCol_TableBorderLight] = Col(PanelAlt);
            C[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            C[ImGuiCol_TableRowBgAlt] = Alpha(Text, 0.03f);

            C[ImGuiCol_PlotHistogram] = Col(Accent);
            C[ImGuiCol_TextSelectedBg] = Col(Selection);
            C[ImGuiCol_NavCursor] = Col(AccentHover);
        }
    } // namespace Theme

} // namespace dbc::studio
