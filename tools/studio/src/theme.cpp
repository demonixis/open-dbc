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

        void Apply(float Scale)
        {
            ImGui::StyleColorsDark();

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

            Style.WindowPadding = ImVec2(8.0f, 8.0f);
            Style.FramePadding = ImVec2(6.0f, 4.0f);
            Style.ItemSpacing = ImVec2(8.0f, 5.0f);
            Style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
            Style.IndentSpacing = 16.0f;
            Style.ScrollbarSize = 12.0f;
            Style.GrabMinSize = 10.0f;

            Style.WindowMenuButtonPosition = ImGuiDir_None;
            Style.SeparatorTextBorderSize = 1.0f;

            ImVec4* C = Style.Colors;
            C[ImGuiCol_Text] = Col(Text);
            C[ImGuiCol_TextDisabled] = Col(TextDim);
            C[ImGuiCol_WindowBg] = Col(Background);
            C[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            C[ImGuiCol_PopupBg] = Col(Panel);
            C[ImGuiCol_Border] = Col(Border);
            C[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

            C[ImGuiCol_FrameBg] = Col(PanelAlt);
            C[ImGuiCol_FrameBgHovered] = Col(Border);
            C[ImGuiCol_FrameBgActive] = Col(Selection);

            C[ImGuiCol_TitleBg] = Col(Panel);
            C[ImGuiCol_TitleBgActive] = Col(Panel);
            C[ImGuiCol_TitleBgCollapsed] = Col(Panel);
            C[ImGuiCol_MenuBarBg] = Col(Panel);

            C[ImGuiCol_ScrollbarBg] = Col(Background);
            C[ImGuiCol_ScrollbarGrab] = Col(Border);
            C[ImGuiCol_ScrollbarGrabHovered] = Alpha(Text, 0.35f);
            C[ImGuiCol_ScrollbarGrabActive] = Alpha(Text, 0.55f);

            C[ImGuiCol_CheckMark] = Col(Identifier);
            C[ImGuiCol_SliderGrab] = Col(Accent);
            C[ImGuiCol_SliderGrabActive] = Col(AccentHover);

            C[ImGuiCol_Button] = Col(Accent);
            C[ImGuiCol_ButtonHovered] = Col(AccentHover);
            C[ImGuiCol_ButtonActive] = Col(Selection);

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
            C[ImGuiCol_TabSelectedOverline] = Col(Accent);
            C[ImGuiCol_TabDimmed] = Col(Panel);
            C[ImGuiCol_TabDimmedSelected] = Col(PanelAlt);
            C[ImGuiCol_TabDimmedSelectedOverline] = Col(Border);

            C[ImGuiCol_DockingPreview] = Alpha(Accent, 0.7f);
            C[ImGuiCol_DockingEmptyBg] = Col(Background);

            C[ImGuiCol_TableHeaderBg] = Col(Panel);
            C[ImGuiCol_TableBorderStrong] = Col(Border);
            C[ImGuiCol_TableBorderLight] = Col(PanelAlt);
            C[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            C[ImGuiCol_TableRowBgAlt] = Alpha(Text, 0.03f);

            C[ImGuiCol_TextSelectedBg] = Col(Selection);
            C[ImGuiCol_NavCursor] = Col(Accent);

            Style.ScaleAllSizes(Scale);
        }
    } // namespace Theme

} // namespace dbc::studio
