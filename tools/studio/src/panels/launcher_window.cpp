#include "launcher_window.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include <imgui.h>

#include "core/file_dialog.hpp"
#include "core/platform.hpp"
#include "core/workspace.hpp"
#include "dba/command_database.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace
    {
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

        std::string HumanSize(std::uintmax_t Bytes)
        {
            char Buffer[32];
            if (Bytes < 1024)
            {
                SDL_snprintf(Buffer, sizeof(Buffer), "%llu B", static_cast<unsigned long long>(Bytes));
            }
            else if (Bytes < 1024 * 1024)
            {
                SDL_snprintf(Buffer, sizeof(Buffer), "%.1f KB", static_cast<double>(Bytes) / 1024.0);
            }
            else
            {
                SDL_snprintf(Buffer, sizeof(Buffer), "%.1f MB", static_cast<double>(Bytes) / (1024.0 * 1024.0));
            }
            return Buffer;
        }

        void LabelledValue(const char* Label, std::string_view Value)
        {
            if (Value.empty())
            {
                return;
            }
            ImGui::TextDisabled("%s", Label);
            ImGui::SameLine(110.0f);
            ImGui::TextUnformatted(Value.data(), Value.data() + Value.size());
        }
    } // namespace

    namespace panels
    {
        void LauncherWindow::Initialize(const dba::CommandDatabase* Commands, const core::Workspace* WorkspaceValue,
                                        core::FileDialog* Dialog)
        {
            Commands_ = Commands;
            Workspace_ = WorkspaceValue;
            Dialog_ = Dialog;
        }

        void LauncherWindow::SetFolder(const std::filesystem::path& Folder, dbc::language::LanguageProfile Profile)
        {
            Folder_ = Folder;
            Profile_ = Profile;
            Programs_ = dba::ScanPrograms(Folder, Profile);
            Selected_ = Programs_.empty() ? -1 : 0;
            Select(Selected_);
        }

        void LauncherWindow::SetProfile(dbc::language::LanguageProfile Profile)
        {
            SetFolder(Folder_, Profile);
        }

        void LauncherWindow::SetProject(const dbc::interpreter::ResolvedProject& Project)
        {
            Folder_ = Project.ProjectRoot;
            Profile_ = Project.Profile;
            Programs_.clear();
            dba::ProgramInfo Info;
            Info.Path = Project.EntrySource;
            Info.RelativeName = Project.DescriptorPath
                                    ? Project.DescriptorPath->filename().string()
                                    : Project.EntrySource.lexically_relative(Project.ProjectRoot).generic_string();
            Info.Profile = Project.Profile;
            Info.Title = Project.Startup.Title;
            std::error_code Error;
            Info.Size = std::filesystem::file_size(Project.EntrySource, Error);
            Programs_.push_back(std::move(Info));
            Select(0);
        }

        void LauncherWindow::Select(int Index)
        {
            Selected_ = Index;
            if (Index < 0 || Index >= static_cast<int>(Programs_.size()))
            {
                return;
            }
            // Only the selection is fully read: a folder of hundreds of scripts
            // still opens instantly.
            dba::AnalyzeProgram(Programs_[static_cast<size_t>(Index)]);
        }

        void LauncherWindow::DrawToolbar(SDL_Window* Parent)
        {
            if constexpr (core::platform::HasNativeFileDialog())
            {
                if (ImGui::Button("Open Folder...") && Dialog_ != nullptr)
                {
                    Dialog_->OpenFolder(Parent, Folder_.string(),
                                        [this](const std::string& Path)
                                        {
                                            if (!Path.empty())
                                                SetFolder(Path, Profile_);
                                        });
                }
            }
            else if (Workspace_ != nullptr)
            {
                // No file picker on iOS: the workspace roots are the only way in.
                ImGui::SetNextItemWidth(220.0f);
                const std::string Preview =
                    Folder_.empty() ? std::string("Choose a location") : Folder_.filename().string();
                if (ImGui::BeginCombo("##location", Preview.c_str()))
                {
                    for (const core::Workspace::Location& LocationValue : Workspace_->Locations())
                    {
                        if (ImGui::Selectable(LocationValue.Name.c_str(), Folder_ == LocationValue.Path))
                        {
                            SetFolder(LocationValue.Path, Profile_);
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh") && !Folder_.empty())
            {
                SetFolder(Folder_, Profile_);
            }

            ImGui::SameLine();
            if (Folder_.empty())
            {
                ImGui::TextDisabled("no folder");
            }
            else
            {
                ImGui::TextDisabled("%s  -  %zu program(s)", Folder_.string().c_str(), Programs_.size());
            }
        }

        void LauncherWindow::DrawList()
        {
            ImGui::SetNextItemWidth(-1.0f);
            char Filter[256];
            SDL_snprintf(Filter, sizeof(Filter), "%s", Filter_.c_str());
            if (ImGui::InputTextWithHint("##filter", "Filter", Filter, sizeof(Filter)))
            {
                Filter_ = Filter;
            }

            ImGui::Separator();

            if (!ImGui::BeginChild("##programs", ImVec2(0.0f, 0.0f)))
            {
                ImGui::EndChild();
                return;
            }

            for (size_t I = 0; I < Programs_.size(); ++I)
            {
                const dba::ProgramInfo& Program = Programs_[I];
                if (!ContainsNoCase(Program.RelativeName, Filter_) && !ContainsNoCase(Program.Title, Filter_))
                {
                    continue;
                }

                ImGui::PushID(static_cast<int>(I));
                if (ImGui::Selectable(Program.RelativeName.c_str(), Selected_ == static_cast<int>(I)))
                {
                    Select(static_cast<int>(I));
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnRun_)
                {
                    OnRun_(Program.Path);
                }

                if (Program.Profile == dbc::language::LanguageProfile::DbPro)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Control), "DBPro");
                }
                ImGui::PopID();
            }

            ImGui::EndChild();
        }

        void LauncherWindow::DrawDetails()
        {
            if (Selected_ < 0 || Selected_ >= static_cast<int>(Programs_.size()))
            {
                ImGui::TextDisabled("Select a program.");
                return;
            }

            const dba::ProgramInfo& Program = Programs_[static_cast<size_t>(Selected_)];

            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.3f);
            ImGui::TextUnformatted(Program.Title.empty() ? Program.Path.filename().string().c_str()
                                                         : Program.Title.c_str());
            ImGui::PopFont();
            ImGui::TextDisabled("%s", Program.RelativeName.c_str());

            ImGui::Spacing();
            if (ImGui::Button("Run"))
            {
                if (OnRun_)
                {
                    OnRun_(Program.Path);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Open in Editor"))
            {
                if (OnEdit_)
                {
                    OnEdit_(Program.Path);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            LabelledValue("Author", Program.Author);
            LabelledValue("Date", Program.Date);
            LabelledValue("Dialect", dbc::language::ProfileName(Program.Profile));
            LabelledValue("Size", HumanSize(Program.Size));

            if (Program.Analyzed)
            {
                char Lines[64];
                SDL_snprintf(Lines, sizeof(Lines), "%d  (%d code, %d comments)", Program.LineCount, Program.CodeLines,
                             Program.CommentLines);
                LabelledValue("Lines", Lines);
            }

            ImGui::Spacing();

            if (!Program.Unrecognised.empty())
            {
                // The signal that matters before pressing Run.
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Theme::Error));
                const bool Open = ImGui::CollapsingHeader("Unrecognised statements");
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::TextDisabled("(%zu)", Program.Unrecognised.size());

                if (Open)
                {
                    ImGui::TextDisabled("Not a comment, label, assignment, flow control or known command.");
                    if (ImGui::BeginChild("##unknown", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders))
                    {
                        for (const dba::ProgramInfo::UnknownLine& Unknown : Program.Unrecognised)
                        {
                            ImGui::TextDisabled("%4d", Unknown.Line);
                            ImGui::SameLine();
                            ImGui::TextUnformatted(Unknown.Text.c_str());
                        }
                    }
                    ImGui::EndChild();
                }
            }

            if (!Program.UnsupportedUsed.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Theme::Number));
                const bool Open = ImGui::CollapsingHeader("Unsupported commands");
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::TextDisabled("(%zu)", Program.UnsupportedUsed.size());

                if (Open)
                {
                    ImGui::TextDisabled("These commands are rejected for this language profile.");
                    if (ImGui::BeginChild("##unsupported", ImVec2(0.0f, 120.0f), ImGuiChildFlags_Borders))
                    {
                        for (const std::string& Command : Program.UnsupportedUsed)
                        {
                            ImGui::TextUnformatted(Command.c_str());
                        }
                    }
                    ImGui::EndChild();
                }
            }

            if (!Program.CompatibilityStubsUsed.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Theme::Number));
                const bool Open = ImGui::CollapsingHeader("Compatibility stubs");
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::TextDisabled("(%zu)", Program.CompatibilityStubsUsed.size());

                if (Open)
                {
                    ImGui::TextDisabled("These commands execute with documented neutral compatibility behavior.");
                    if (ImGui::BeginChild("##compatibility_stubs", ImVec2(0.0f, 120.0f), ImGuiChildFlags_Borders))
                    {
                        for (const dba::ProgramInfo::CompatibilityStubUse& Command : Program.CompatibilityStubsUsed)
                        {
                            ImGui::TextUnformatted(Command.Name.c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled(Command.Permanent ? "(permanent)" : "(temporary)");
                        }
                    }
                    ImGui::EndChild();
                }
            }

            if (!Program.Commands.empty())
            {
                const bool Open = ImGui::CollapsingHeader("Commands used");
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu)", Program.Commands.size());

                if (Open && ImGui::BeginChild("##commands", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders))
                {
                    for (const std::string& Command : Program.Commands)
                    {
                        ImGui::TextUnformatted(Command.c_str());
                        if (Commands_ != nullptr)
                        {
                            if (const dba::Command* Entry = Commands_->Find(Program.Profile, Command))
                            {
                                if (!Entry->Description.empty() && ImGui::IsItemHovered())
                                {
                                    ImGui::SetTooltip("%s", Entry->Description.c_str());
                                }
                            }
                        }
                    }
                }
                if (Open)
                {
                    ImGui::EndChild();
                }
            }
        }

        void LauncherWindow::Draw(SDL_Window* Parent)
        {
            if (!Open_)
            {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Launcher", &Open_))
            {
                ImGui::End();
                return;
            }

            DrawToolbar(Parent);
            ImGui::Separator();

            if (ImGui::BeginTable("##masterdetail", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("##list", ImGuiTableColumnFlags_WidthStretch, 0.38f);
                ImGui::TableSetupColumn("##details", ImGuiTableColumnFlags_WidthStretch, 0.62f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawList();

                ImGui::TableSetColumnIndex(1);
                DrawDetails();

                ImGui::EndTable();
            }

            ImGui::End();
        }
    } // namespace panels

} // namespace dbc::studio
