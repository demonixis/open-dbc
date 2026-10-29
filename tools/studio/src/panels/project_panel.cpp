#include "project_panel.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>

#include "language/language_profile.hpp"
#include <imgui.h>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string ToLower(std::string S)
        {
            std::transform(S.begin(), S.end(), S.begin(),
                           [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
            return S;
        }

        bool IsHidden(const fs::path& Path)
        {
            const std::string Name = dbc::PathToUtf8(Path.filename());
            return !Name.empty() && Name.front() == '.';
        }

        bool IsDba(const fs::path& Path)
        {
            return dbc::language::IsLanguageSource(Path);
        }
    } // namespace

    namespace panels
    {
        void ProjectPanel::SetRoot(const fs::path& Root)
        {
            Root_ = Root;
            Refresh();
        }

        void ProjectPanel::SetDbProProject(const std::optional<fs::path>& Descriptor,
                                           std::vector<fs::path> OrderedSources, fs::path AssetRoot)
        {
            Descriptor_ = Descriptor;
            OrderedSources_ = std::move(OrderedSources);
            AssetRoot_ = std::move(AssetRoot);
        }

        void ProjectPanel::Refresh()
        {
            Tree_ = Node{};
            Tree_.Path = Root_;
            Tree_.Name = dbc::PathToUtf8(Root_.filename());
            Tree_.Directory = true;

            if (!Root_.empty())
            {
                ScanDirectory(Tree_);
            }
        }

        void ProjectPanel::ScanDirectory(Node& NodeValue)
        {
            NodeValue.Children.clear();
            NodeValue.Scanned = true;

            std::error_code Ec;
            std::vector<fs::directory_entry> Entries;
            for (const fs::directory_entry& Entry : fs::directory_iterator(NodeValue.Path, Ec))
            {
                if (!IsHidden(Entry.path()))
                {
                    Entries.push_back(Entry);
                }
            }
            if (Ec)
            {
                return;
            }

            // Directories first, then files, both alphabetical.
            std::sort(Entries.begin(), Entries.end(),
                      [](const fs::directory_entry& A, const fs::directory_entry& B)
                      {
                          std::error_code DirA;
                          std::error_code DirB;
                          const bool ADir = A.is_directory(DirA);
                          const bool BDir = B.is_directory(DirB);
                          if (ADir != BDir)
                          {
                              return ADir;
                          }
                          return ToLower(dbc::PathToUtf8(A.path().filename())) <
                                 ToLower(dbc::PathToUtf8(B.path().filename()));
                      });

            for (const fs::directory_entry& Entry : Entries)
            {
                std::error_code DirEc;
                Node Child;
                Child.Path = Entry.path();
                Child.Name = dbc::PathToUtf8(Entry.path().filename());
                Child.Directory = Entry.is_directory(DirEc);
                NodeValue.Children.push_back(std::move(Child));
            }
        }

        void ProjectPanel::DrawNode(Node& NodeValue)
        {
            ImGui::PushID(NodeValue.Name.c_str());

            if (NodeValue.Directory)
            {
                const bool Open = ImGui::TreeNodeEx(NodeValue.Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                if (Open)
                {
                    if (!NodeValue.Scanned)
                    {
                        ScanDirectory(NodeValue);
                    }
                    for (Node& Child : NodeValue.Children)
                    {
                        DrawNode(Child);
                    }
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGuiTreeNodeFlags Flags =
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                ImGui::TreeNodeEx(NodeValue.Name.c_str(), Flags);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnActivate_)
                {
                    OnActivate_(NodeValue.Path);
                }

                if (IsDba(NodeValue.Path) && ImGui::BeginPopupContextItem("##filemenu"))
                {
                    if (ImGui::MenuItem("Open"))
                    {
                        if (OnActivate_)
                        {
                            OnActivate_(NodeValue.Path);
                        }
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::PopID();
        }

        void ProjectPanel::Draw()
        {
            if (Root_.empty())
            {
                ImGui::TextDisabled("No folder opened.");
                ImGui::TextDisabled("File > Open Folder...");
                return;
            }

            ImGui::TextUnformatted(Tree_.Name.empty() ? dbc::PathToUtf8(Root_).c_str() : Tree_.Name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Refresh").x -
                            ImGui::GetStyle().FramePadding.x * 2.0f);
            if (ImGui::SmallButton("Refresh"))
            {
                Refresh();
            }
            ImGui::Separator();

            if (Descriptor_)
            {
                if (ImGui::TreeNodeEx("Sources", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    for (std::size_t Index = 0; Index < OrderedSources_.size(); ++Index)
                    {
                        const std::string Name = (Index == 0 ? "Main: " : std::to_string(Index) + ": ") +
                                                 dbc::PathToUtf8(OrderedSources_[Index].filename());
                        ImGui::TreeNodeEx(Name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                            ImGuiTreeNodeFlags_SpanAvailWidth);
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnActivate_)
                            OnActivate_(OrderedSources_[Index]);
                    }
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx("Media", ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    ImGui::TextDisabled("%s", dbc::PathToUtf8(AssetRoot_).c_str());
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx("Other files", ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    for (Node& Child : Tree_.Children)
                        DrawNode(Child);
                    ImGui::TreePop();
                }
            }
            else
            {
                for (Node& Child : Tree_.Children)
                    DrawNode(Child);
            }
        }
    } // namespace panels

} // namespace dbc::studio
