#include "media_panel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>

#include <imgui.h>

namespace dbc::studio
{
    namespace
    {
        // Enough for any DBC/DBPro game; stops a stray home folder from stalling the UI.
        constexpr std::size_t KMediaLimit = 5000;

        bool ContainsNoCase(const std::string& Haystack, const char* Needle)
        {
            const std::string_view Wanted(Needle);
            const auto It = std::search(
                Haystack.begin(), Haystack.end(), Wanted.begin(), Wanted.end(), [](char A, char B)
                { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
            return It != Haystack.end();
        }

        std::string FormatSize(std::uintmax_t Bytes)
        {
            char Text[32];
            if (Bytes < 1024)
                std::snprintf(Text, sizeof(Text), "%ju B", Bytes);
            else if (Bytes < 1024 * 1024)
                std::snprintf(Text, sizeof(Text), "%.1f KB", static_cast<double>(Bytes) / 1024.0);
            else
                std::snprintf(Text, sizeof(Text), "%.1f MB", static_cast<double>(Bytes) / (1024.0 * 1024.0));
            return Text;
        }
    } // namespace

    namespace panels
    {
        void MediaPanel::SetRoot(const std::filesystem::path& Root)
        {
            if (Root == Root_)
                return;
            Root_ = Root;
            Refresh();
        }

        void MediaPanel::Refresh()
        {
            Scan_ = core::ScanMedia(Root_, KMediaLimit);
        }

        void MediaPanel::Draw()
        {
            if (Root_.empty())
            {
                ImGui::TextDisabled("No folder opened.");
                return;
            }

            std::array<std::size_t, core::KMediaKinds.size()> Counts{};
            for (const core::MediaFile& File : Scan_.Files)
                ++Counts[static_cast<std::size_t>(File.Kind)];

            const auto Label = [&](std::optional<core::MediaKind> Kind)
            {
                if (!Kind)
                    return "All (" + std::to_string(Scan_.Files.size()) + ")";
                return std::string(core::MediaKindLabel(*Kind)) + " (" +
                       std::to_string(Counts[static_cast<std::size_t>(*Kind)]) + ")";
            };

            const float RefreshWidth = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - RefreshWidth - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::BeginCombo("##media-kind", Label(Filter_).c_str()))
            {
                if (ImGui::Selectable(Label(std::nullopt).c_str(), !Filter_))
                    Filter_.reset();
                for (const core::MediaKind Kind : core::KMediaKinds)
                {
                    if (ImGui::Selectable(Label(Kind).c_str(), Filter_ == Kind))
                        Filter_ = Kind;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
                Refresh();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##media-search", "Filter by name", Search_, sizeof(Search_));
            if (Scan_.Truncated)
                ImGui::TextDisabled("Showing the first %zu files.", KMediaLimit);
            ImGui::Separator();

            if (!ImGui::BeginTable("##media", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit))
                return;
            ImGui::TableSetupScrollFreeze(0, 0);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size");
            for (const core::MediaFile& File : Scan_.Files)
            {
                if ((Filter_ && File.Kind != *Filter_) || !ContainsNoCase(File.RelativeName, Search_))
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(File.RelativeName.c_str());
                ImGui::Selectable(File.RelativeName.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemHovered())
                {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnActivate_)
                        OnActivate_(File.Path);
                    ImGui::SetTooltip("%s\n%s", std::string(core::MediaKindLabel(File.Kind)).c_str(),
                                      File.Path.string().c_str());
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", FormatSize(File.Size).c_str());
            }
            ImGui::EndTable();
        }
    } // namespace panels

} // namespace dbc::studio
