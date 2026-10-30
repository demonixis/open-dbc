#include "problems_panel.hpp"

#include <string>
#include <utility>

#include <imgui.h>

#include "core/i18n.hpp"
#include "path_utf8.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace panels
    {
        void ProblemsPanel::SetOnSelect(SelectHandler Handler)
        {
            OnSelect_ = std::move(Handler);
        }

        void ProblemsPanel::Set(std::vector<toolchain::RunDiagnostic> Problems)
        {
            Problems_ = std::move(Problems);
        }

        void ProblemsPanel::Draw()
        {
            if (Problems_.empty())
            {
                ImGui::TextDisabled("%s", Tr("No problems in the last run or check."));
                return;
            }
            constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
            if (!ImGui::BeginTable("Problems", 4, Flags))
                return;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(Tr("File"), ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 12.0f);
            ImGui::TableSetupColumn(Tr("Line"), ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
            ImGui::TableSetupColumn(Tr("Code"), ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
            ImGui::TableSetupColumn(Tr("Message"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (std::size_t Index = 0; Index < Problems_.size(); ++Index)
            {
                const toolchain::RunDiagnostic& Problem = Problems_[Index];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(Index));
                const std::string File = Problem.Path.empty() ? "-" : dbc::PathToUtf8(Problem.Path.filename());
                const bool Located = !Problem.Path.empty() && Problem.Line > 0;
                if (ImGui::Selectable(File.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && Located && OnSelect_)
                    OnSelect_(Problem.Path, Problem.Line);
                if (!Problem.Path.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", dbc::PathToUtf8(Problem.Path).c_str());
                ImGui::PopID();
                ImGui::TableNextColumn();
                if (Problem.Line > 0)
                    ImGui::Text("%u", Problem.Line);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Problem.Code.c_str());
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, Problem.Error ? Theme::Error : Theme::Identifier);
                ImGui::TextUnformatted(Problem.Message.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
    } // namespace panels

} // namespace dbc::studio
