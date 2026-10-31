#include "theme.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace dbc::studio;

namespace
{
    ImVec4 Over(ImVec4 Top, ImU32 Below)
    {
        const ImVec4 Back = ImGui::ColorConvertU32ToFloat4(Below);
        const float A = Top.w;
        return ImVec4(Top.x * A + Back.x * (1.0f - A), Top.y * A + Back.y * (1.0f - A), Top.z * A + Back.z * (1.0f - A),
                      1.0f);
    }

    float Luminance(ImVec4 C)
    {
        const auto Linear = [](float V) { return V <= 0.03928f ? V / 12.92f : std::pow((V + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * Linear(C.x) + 0.7152f * Linear(C.y) + 0.0722f * Linear(C.z);
    }

    float Contrast(ImVec4 A, ImVec4 B)
    {
        const float La = Luminance(A);
        const float Lb = Luminance(B);
        return (std::max(La, Lb) + 0.05f) / (std::min(La, Lb) + 0.05f);
    }

    float Contrast(ImU32 A, ImU32 B)
    {
        return Contrast(ImGui::ColorConvertU32ToFloat4(A), ImGui::ColorConvertU32ToFloat4(B));
    }
} // namespace

TEST_CASE("The theme setting resolves to an appearance")
{
    using Theme::Appearance;
    CHECK(Theme::ResolveAppearance("dark", true) == Appearance::Dark);
    CHECK(Theme::ResolveAppearance("light", false) == Appearance::Light);
    CHECK(Theme::ResolveAppearance("auto", true) == Appearance::Light);
    CHECK(Theme::ResolveAppearance("auto", false) == Appearance::Dark);
    CHECK(Theme::ResolveAppearance("", true) == Appearance::Dark);
    CHECK(Theme::ResolveAppearance("sepia", true) == Appearance::Dark);
}

TEST_CASE("Both appearances keep text readable")
{
    ImGui::CreateContext();
    for (const Theme::Appearance Value : {Theme::Appearance::Dark, Theme::Appearance::Light})
    {
        CAPTURE(static_cast<int>(Value));
        Theme::SetAppearance(Value);
        CHECK(Theme::Current == Value);

        CHECK(Contrast(Theme::Text, Theme::Background) >= 7.0f);
        CHECK(Contrast(Theme::Text, Theme::Panel) >= 7.0f);
        CHECK(Contrast(Theme::Text, Theme::PanelAlt) >= 7.0f);
        CHECK(Contrast(Theme::Text, Theme::Hover) >= 4.5f);
        CHECK(Contrast(Theme::Text, Theme::Selection) >= 4.5f);
        CHECK(Contrast(Theme::TextDim, Theme::Background) >= 4.5f);
        CHECK(Contrast(Theme::LineNumber, Theme::Background) >= 3.0f);

        for (const ImU32 Token : {Theme::Keyword, Theme::Identifier, Theme::Command, Theme::String, Theme::Number,
                                  Theme::Comment, Theme::Control, Theme::Error, Theme::Run, Theme::Warning})
            CHECK(Contrast(Token, Theme::Background) >= 4.5f);
        CHECK(Contrast(Theme::Error, Theme::ErrorLine) >= 3.0f);

        const ImVec4* Colors = ImGui::GetStyle().Colors;
        const ImVec4 TextColor = Colors[ImGuiCol_Text];
        for (const ImGuiCol Slot : {ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive})
            CHECK(Contrast(TextColor, Over(Colors[Slot], Theme::Background)) >= 4.5f);
    }
    ImGui::DestroyContext();
}

TEST_CASE("Changing the appearance leaves the scaled metrics alone")
{
    ImGui::CreateContext();
    Theme::ApplyMetrics(2.0f);
    const float Padding = ImGui::GetStyle().FramePadding.x;
    CHECK(Padding == doctest::Approx(12.0f));

    Theme::SetAppearance(Theme::Appearance::Light);
    Theme::SetAppearance(Theme::Appearance::Dark);
    CHECK(ImGui::GetStyle().FramePadding.x == doctest::Approx(Padding));
    ImGui::DestroyContext();
}
