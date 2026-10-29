#include "snippet_window.hpp"
#include "path_utf8.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cctype>

#include <imgui.h>

namespace dbc::studio
{
    namespace
    {
        bool ContainsIgnoringCase(const std::string& Text, const char* Needle)
        {
            const std::string_view Search(Needle);
            return std::search(Text.begin(), Text.end(), Search.begin(), Search.end(),
                               [](char Left, char Right)
                               {
                                   return std::tolower(static_cast<unsigned char>(Left)) ==
                                          std::tolower(static_cast<unsigned char>(Right));
                               }) != Text.end();
        }
    } // namespace

    namespace panels
    {
        void SnippetWindow::Reload()
        {
            Snippets_ = core::BuiltinSnippets();
            BuiltinCount_ = Snippets_.size();
            if (!UserFolder_.empty())
            {
                std::vector<core::Snippet> User = core::LoadSnippets(UserFolder_);
                Snippets_.insert(Snippets_.end(), std::make_move_iterator(User.begin()),
                                 std::make_move_iterator(User.end()));
            }
            Selected_ = -1;
        }

        void SnippetWindow::OpenInsert(dbc::language::LanguageProfile Profile)
        {
            // Reread the folder each time, so snippets copied in by hand show up.
            Reload();
            Profile_ = Profile;
            Filter_.fill('\0');
            InsertRequested_ = true;
        }

        void SnippetWindow::OpenSave(std::string Body, dbc::language::LanguageProfile Profile)
        {
            Profile_ = Profile;
            SaveBody_ = std::move(Body);
            SaveName_.fill('\0');
            SaveDescription_.fill('\0');
            SaveBothProfiles_ = false;
            SaveError_.clear();
            SaveRequested_ = true;
        }

        std::optional<core::Snippet> SnippetWindow::Draw()
        {
            DrawSave();
            return DrawInsert();
        }

        std::optional<core::Snippet> SnippetWindow::DrawInsert()
        {
            constexpr const char* Popup = "Insert Snippet";
            if (InsertRequested_)
            {
                ImGui::OpenPopup(Popup);
                InsertRequested_ = false;
            }
            const float Em = ImGui::GetFontSize();
            ImGui::SetNextWindowSize(ImVec2(52.0f * Em, 30.0f * Em), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal(Popup))
                return std::nullopt;

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(16.0f * Em);
            ImGui::InputTextWithHint("##filter", "Filter", Filter_.data(), Filter_.size());

            const auto Visible = [&](std::size_t Index)
            {
                const core::Snippet& Value = Snippets_[Index];
                return Value.Supports(Profile_) && ContainsIgnoringCase(Value.Name, Filter_.data());
            };
            if (Selected_ < 0 || static_cast<std::size_t>(Selected_) >= Snippets_.size() || !Visible(Selected_))
            {
                Selected_ = -1;
                for (std::size_t Index = 0; Index < Snippets_.size() && Selected_ < 0; ++Index)
                    if (Visible(Index))
                        Selected_ = static_cast<int>(Index);
            }

            std::optional<core::Snippet> Inserted;
            const float Footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            ImGui::BeginChild("##list", ImVec2(16.0f * Em, -Footer), ImGuiChildFlags_Borders);
            for (std::size_t Index = 0; Index < Snippets_.size(); ++Index)
            {
                if (Index == BuiltinCount_)
                    ImGui::SeparatorText("My Snippets");
                if (!Visible(Index))
                    continue;
                ImGui::PushID(static_cast<int>(Index));
                if (ImGui::Selectable(Snippets_[Index].Name.c_str(), Selected_ == static_cast<int>(Index),
                                      ImGuiSelectableFlags_AllowDoubleClick))
                {
                    Selected_ = static_cast<int>(Index);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        Inserted = Snippets_[Index];
                }
                ImGui::PopID();
            }
            if (BuiltinCount_ == Snippets_.size())
            {
                ImGui::SeparatorText("My Snippets");
                ImGui::TextDisabled("Edit > Save Selection as Snippet");
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginGroup();
            if (Selected_ >= 0)
            {
                core::Snippet& Value = Snippets_[Selected_];
                ImGui::TextUnformatted(Value.Name.c_str());
                if (!Value.Description.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Theme::TextDim));
                    ImGui::TextWrapped("%s", Value.Description.c_str());
                    ImGui::PopStyleColor();
                }
                // Read-only, so the buffer is never written through.
                ImGui::InputTextMultiline("##preview", Value.Body.data(), Value.Body.size() + 1, ImVec2(-1.0f, -Footer),
                                          ImGuiInputTextFlags_ReadOnly);
            }
            else
            {
                ImGui::TextDisabled("No snippet matches.");
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetContentRegionAvail().y - Footer));
            }
            ImGui::EndGroup();

            ImGui::BeginDisabled(Selected_ < 0);
            if (ImGui::Button("Insert") && Selected_ >= 0)
                Inserted = Snippets_[Selected_];
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();
            if (!UserFolder_.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("My Snippets: %s", dbc::PathToUtf8(UserFolder_).c_str());
            }
            if (Inserted)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return Inserted;
        }

        void SnippetWindow::DrawSave()
        {
            constexpr const char* Popup = "Save as Snippet";
            if (SaveRequested_)
            {
                ImGui::OpenPopup(Popup);
                SaveRequested_ = false;
            }
            if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const bool Entered =
                ImGui::InputText("Name", SaveName_.data(), SaveName_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::InputText("Description", SaveDescription_.data(), SaveDescription_.size());
            const bool Pro = Profile_ == dbc::language::LanguageProfile::DbPro;
            ImGui::Checkbox(Pro ? "Also offer it to DBC projects" : "Also offer it to DBPro projects",
                            &SaveBothProfiles_);
            ImGui::TextDisabled("Saved in %s", dbc::PathToUtf8(UserFolder_).c_str());
            if (!SaveError_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error), "%s", SaveError_.c_str());

            ImGui::BeginDisabled(SaveName_[0] == '\0');
            if (ImGui::Button("Save") || (Entered && SaveName_[0] != '\0'))
            {
                core::Snippet Value;
                Value.Name = SaveName_.data();
                Value.Description = SaveDescription_.data();
                Value.Dbc = !Pro || SaveBothProfiles_;
                Value.DbPro = Pro || SaveBothProfiles_;
                Value.Body = SaveBody_;
                if (!core::SaveSnippet(UserFolder_, Value, SaveError_).empty())
                    ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } // namespace panels

} // namespace dbc::studio
