#include "output_panel.hpp"

#include <algorithm>
#include <cctype>

#include <imgui.h>

#include "core/i18n.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace
    {
        constexpr size_t KMaxLines = 5000;

        bool ContainsNoCase(const std::string& Haystack, const std::string& Needle)
        {
            if (Needle.empty())
            {
                return true;
            }
            const auto It = std::search(
                Haystack.begin(), Haystack.end(), Needle.begin(), Needle.end(), [](char A, char B)
                { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
            return It != Haystack.end();
        }

        ImU32 ColorFor(panels::OutputPanel::Severity SeverityValue)
        {
            switch (SeverityValue)
            {
                case panels::OutputPanel::Severity::Error:
                    return Theme::Error;
                case panels::OutputPanel::Severity::Notice:
                    return Theme::Identifier;
                case panels::OutputPanel::Severity::Normal:
                    break;
            }
            return Theme::Text;
        }
    } // namespace

    namespace panels
    {
        void OutputPanel::Append(const std::string& Line, Severity SeverityValue)
        {
            Lines_.push_back(Entry{Line, SeverityValue});

            // Keep the buffer bounded: a runaway script can log for a long time.
            if (Lines_.size() > KMaxLines)
            {
                const size_t Dropped = Lines_.size() - KMaxLines;
                Lines_.erase(Lines_.begin(), Lines_.begin() + static_cast<long>(Dropped));

                // Selection indexes m_lines, so it has to slide with it.
                const auto Shift = [Dropped](int& Index)
                {
                    if (Index >= 0)
                    {
                        Index = (static_cast<size_t>(Index) >= Dropped) ? Index - static_cast<int>(Dropped) : -1;
                    }
                };
                Shift(Anchor_);
                Shift(Cursor_);
            }

            ScrollPending_ = true;
        }

        void OutputPanel::Clear()
        {
            Lines_.clear();
            Anchor_ = -1;
            Cursor_ = -1;
        }

        bool OutputPanel::IsSelected(size_t Index) const
        {
            if (Anchor_ < 0 || Cursor_ < 0)
            {
                return false;
            }
            const size_t Low = static_cast<size_t>(std::min(Anchor_, Cursor_));
            const size_t High = static_cast<size_t>(std::max(Anchor_, Cursor_));
            return Index >= Low && Index <= High;
        }

        void OutputPanel::Select(size_t Index, bool Extend)
        {
            if (Extend && Anchor_ >= 0)
            {
                Cursor_ = static_cast<int>(Index);
            }
            else
            {
                Anchor_ = static_cast<int>(Index);
                Cursor_ = Anchor_;
            }
        }

        std::string OutputPanel::CopyText(bool SelectionOnly) const
        {
            std::string Out;
            for (const size_t Index : Visible_)
            {
                if (SelectionOnly && Anchor_ >= 0 && !IsSelected(Index))
                {
                    continue;
                }
                Out += Lines_[Index].Text;
                Out.push_back('\n');
            }
            return Out;
        }

        void OutputPanel::Draw()
        {
            if (ImGui::Button(TrId("Clear")))
            {
                Clear();
            }

            ImGui::SameLine();
            const bool HasSelection = Anchor_ >= 0;
            if (ImGui::Button(TrId(HasSelection ? "Copy selection" : "Copy all")))
            {
                ImGui::SetClipboardText(CopyText(true).c_str());
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", Tr("Ctrl+C. Click a line to select it, Shift+click to extend."));
            }

            ImGui::SameLine();
            ImGui::Checkbox(TrId("Auto-scroll"), &AutoScroll_);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            char Filter[256];
            std::snprintf(Filter, sizeof(Filter), "%s", Filter_.c_str());
            if (ImGui::InputTextWithHint("##filter", Tr("Filter"), Filter, sizeof(Filter)))
            {
                Filter_ = Filter;
            }

            ImGui::Separator();

            if (!ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                                   ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::EndChild();
                return;
            }

            Visible_.clear();
            Visible_.reserve(Lines_.size());

            // m_visible is only complete once the loop below has run, so copy
            // requests raised inside it are acted on afterwards.
            bool CopyAll = false;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
            for (size_t I = 0; I < Lines_.size(); ++I)
            {
                const Entry& EntryValue = Lines_[I];
                if (!ContainsNoCase(EntryValue.Text, Filter_))
                {
                    continue;
                }
                Visible_.push_back(I);

                ImGui::PushID(static_cast<int>(I));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ColorFor(EntryValue.Level)));

                Theme::PushCodeFont();
                if (ImGui::Selectable(EntryValue.Text.c_str(), IsSelected(I)))
                {
                    Select(I, ImGui::GetIO().KeyShift);
                }
                ImGui::PopFont();

                ImGui::PopStyleColor();

                if (ImGui::BeginPopupContextItem("##linemenu"))
                {
                    if (ImGui::MenuItem(TrId("Copy line")))
                    {
                        ImGui::SetClipboardText(EntryValue.Text.c_str());
                    }
                    if (ImGui::MenuItem(TrId("Copy all")))
                    {
                        CopyAll = true;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopStyleVar();

            if (CopyAll)
            {
                ImGui::SetClipboardText(CopyText(false).c_str());
            }

            // Ctrl+C (Cmd+C on macOS, ImGui maps the modifier) while the log has focus.
            if (ImGui::IsWindowFocused() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
            {
                ImGui::SetClipboardText(CopyText(true).c_str());
            }
            if (ImGui::IsWindowFocused() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A) && !Lines_.empty())
            {
                Anchor_ = 0;
                Cursor_ = static_cast<int>(Lines_.size()) - 1;
            }

            if (ScrollPending_ && AutoScroll_)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ScrollPending_ = false;

            ImGui::EndChild();
        }
    } // namespace panels

} // namespace dbc::studio
