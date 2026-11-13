#include "asset_library_window.hpp"

#include "core/i18n.hpp"
#include "theme.hpp"

#include <algorithm>

#include <imgui.h>

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            // Thumbnails load synchronously, so only a few per frame.
            constexpr int KThumbnailsPerFrame = 4;
            constexpr int KThumbnailSize = 128;
        } // namespace

        void AssetLibraryWindow::Initialize(SDL_GPUDevice* Gpu, std::filesystem::path Folder)
        {
            Gpu_ = Gpu;
            Folder_ = std::move(Folder);
        }

        void AssetLibraryWindow::Shutdown()
        {
            for (auto& [Name, Image] : Thumbnails_)
                core::ReleaseGpuImage(Gpu_, Image);
            Thumbnails_.clear();
        }

        void AssetLibraryWindow::Open()
        {
            Open_ = true;
            if (!Scanned_)
            {
                Textures_ = core::ScanTextureLibrary(Folder_);
                Scanned_ = true;
            }
        }

        std::optional<core::LibraryTexture> AssetLibraryWindow::Draw(const std::filesystem::path& ProjectRoot)
        {
            if (!Open_)
                return std::nullopt;
            std::optional<core::LibraryTexture> Picked;
            const float Font = ImGui::GetFontSize();
            ImGui::SetNextWindowSize(ImVec2(Font * 40.0f, Font * 30.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(TrId("Texture Library"), &Open_, ImGuiWindowFlags_NoDocking))
            {
                if (ProjectRoot.empty())
                    ImGui::TextDisabled("%s", Tr("Open a project to add textures to it."));
                else if (Textures_.empty())
                    ImGui::TextDisabled("%s", Tr("No texture library is installed."));
                else
                    ImGui::TextDisabled("%s", Tr("Click a texture to copy it into the project's textures folder."));

                const ImGuiStyle& Style = ImGui::GetStyle();
                const float Tile = Font * 6.0f;
                const float Cell = Tile + Style.ItemSpacing.x;
                const int Columns =
                    std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + Style.ItemSpacing.x) / Cell));
                int Loaded = 0;
                int Column = 0;
                ImGui::BeginChild("##library-grid");
                ImGui::BeginDisabled(ProjectRoot.empty());
                for (const core::LibraryTexture& Texture : Textures_)
                {
                    ImGui::PushID(Texture.Name.c_str());
                    if (Column > 0)
                        ImGui::SameLine();
                    ImGui::BeginGroup();
                    const ImVec2 Min = ImGui::GetCursorScreenPos();
                    const ImVec2 Max(Min.x + Tile, Min.y + Tile);
                    if (ImGui::InvisibleButton("##tile", ImVec2(Tile, Tile)))
                        Picked = Texture;
                    const bool Hovered = ImGui::IsItemHovered();
                    ImDrawList* Draw = ImGui::GetWindowDrawList();
                    Draw->AddRectFilled(Min, Max, Hovered ? Theme::Hover : Theme::Panel, 4.0f);

                    if (ImGui::IsItemVisible())
                    {
                        auto Found = Thumbnails_.find(Texture.Name);
                        if (Found == Thumbnails_.end() && Loaded < KThumbnailsPerFrame && Gpu_ != nullptr)
                        {
                            core::GpuImage Fresh;
                            std::string Error;
                            core::LoadGpuImage(Gpu_, Texture.Preview, Fresh, Error, KThumbnailSize);
                            Found = Thumbnails_.emplace(Texture.Name, Fresh).first;
                            ++Loaded;
                        }
                        if (Found != Thumbnails_.end() && Found->second.Texture != nullptr)
                            Draw->AddImageRounded(reinterpret_cast<ImTextureID>(Found->second.Texture),
                                                  ImVec2(Min.x + 3.0f, Min.y + 3.0f),
                                                  ImVec2(Max.x - 3.0f, Max.y - 3.0f), ImVec2(0.0f, 0.0f),
                                                  ImVec2(1.0f, 1.0f), IM_COL32_WHITE, 3.0f);
                    }
                    // A tick on what the project has already.
                    if (!ProjectRoot.empty() && core::HasLibraryTexture(Texture, ProjectRoot))
                    {
                        const ImVec2 Centre(Max.x - Font * 0.8f, Min.y + Font * 0.8f);
                        const float R = Font * 0.55f;
                        Draw->AddCircleFilled(Centre, R, Theme::Run);
                        const ImVec2 Tick[] = {ImVec2(Centre.x - R * 0.5f, Centre.y),
                                               ImVec2(Centre.x - R * 0.1f, Centre.y + R * 0.4f),
                                               ImVec2(Centre.x + R * 0.5f, Centre.y - R * 0.35f)};
                        Draw->AddPolyline(Tick, 3, IM_COL32_WHITE, ImDrawFlags_None, std::max(1.5f, R * 0.25f));
                    }
                    if (Hovered)
                        ImGui::SetTooltip("%s", Tr(Texture.CubeMap ? "Environment cube map: six faces"
                                                                   : "Tileable texture"));

                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Tile);
                    ImGui::TextUnformatted(Texture.Name.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::PopID();
                    Column = (Column + 1) % Columns;
                }
                ImGui::EndDisabled();
                ImGui::EndChild();
            }
            ImGui::End();
            return Picked;
        }
    } // namespace panels

} // namespace dbc::studio
