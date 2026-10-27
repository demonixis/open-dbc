#include "code_panel.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <TextEditor.h>
#include <imgui.h>

namespace dbc::studio
{
    namespace
    {
        // Long enough to skip re-parsing on every keystroke, short enough to feel live.
        constexpr double KReparseDelay = 0.3;

        bool LessNoCase(const std::string& Left, const std::string& Right)
        {
            return std::lexicographical_compare(
                Left.begin(), Left.end(), Right.begin(), Right.end(), [](char A, char B)
                { return std::tolower(static_cast<unsigned char>(A)) < std::tolower(static_cast<unsigned char>(B)); });
        }
    } // namespace

    namespace panels
    {
        void CodePanel::Update(const TextEditor* Editor, const std::filesystem::path& Path,
                               dbc::language::LanguageProfile Profile, double Now)
        {
            if (Editor == nullptr)
            {
                Editor_ = nullptr;
                Outline_ = {};
                Pending_ = false;
                return;
            }

            const bool Switched = Editor != Editor_ || Profile != Profile_;
            if (Switched || Editor->GetUndoIndex() != UndoIndex_)
            {
                Editor_ = Editor;
                Profile_ = Profile;
                UndoIndex_ = Editor->GetUndoIndex();
                ChangedAt_ = Switched ? Now - KReparseDelay : Now;
                Pending_ = true;
            }

            if (Pending_ && Now - ChangedAt_ >= KReparseDelay)
            {
                Pending_ = false;
                Outline_ = dba::BuildCodeOutline(Path, Editor->GetText(), Profile);
            }
        }

        void CodePanel::DrawSection(const char* Title, const char* Id, const std::vector<dba::OutlineEntry>& Entries,
                                    const char* CountTooltip)
        {
            const std::string Header =
                std::string(Title) + " (" + std::to_string(Entries.size()) + ")###" + std::string(Id);
            if (!ImGui::TreeNodeEx(Header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
                return;

            std::vector<const dba::OutlineEntry*> Order;
            Order.reserve(Entries.size());
            for (const dba::OutlineEntry& Entry : Entries)
                Order.push_back(&Entry);
            if (SortByName_)
                std::stable_sort(Order.begin(), Order.end(), [](const dba::OutlineEntry* Left, const dba::OutlineEntry* Right)
                                 { return LessNoCase(Left->Name, Right->Name); });

            if (ImGui::BeginTable(Id, CountTooltip ? 3 : 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                if (CountTooltip)
                    ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("Line");
                for (const dba::OutlineEntry* Entry : Order)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(Entry);
                    const std::string Label = Entry->Array ? Entry->Name + "()" : Entry->Name;
                    if (ImGui::Selectable(Label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && OnSelect_)
                        OnSelect_(Entry->Line);
                    if (CountTooltip && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%d %s", Entry->Count, CountTooltip);
                    ImGui::PopID();
                    if (CountTooltip)
                    {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%d", Entry->Count);
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%u", Entry->Line);
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        void CodePanel::Draw()
        {
            if (Editor_ == nullptr)
            {
                ImGui::TextDisabled("No source open.");
                return;
            }

            ImGui::Checkbox("Sort by name", &SortByName_);
            ImGui::Separator();
            DrawSection("Functions", "functions", Outline_.Functions, "call(s)");
            DrawSection("Labels", "labels", Outline_.Labels, "GOTO/GOSUB jump(s)");
            DrawSection("Types", "types", Outline_.Types, "field(s)");
            DrawSection("Variables", "variables", Outline_.Variables, nullptr);
        }
    } // namespace panels

} // namespace dbc::studio
