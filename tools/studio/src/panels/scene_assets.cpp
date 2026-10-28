// The Scene view's Assets panel: the project's images, models and sounds, as tiles
// that drag onto the viewport and the inspector's file fields.

#include "panels/scene_editor.hpp"

#include <algorithm>
#include <cctype>

#include "theme.hpp"

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            constexpr std::size_t KAssetLimit = 5000;
            // Thumbnails load synchronously, so only a few per frame; a model's import
            // can take tens of milliseconds, so one of those.
            constexpr int KThumbnailsPerFrame = 4;
            constexpr int KModelThumbnailsPerFrame = 1;
            constexpr int KThumbnailSize = 96;

            std::string Lower(std::string Text)
            {
                std::transform(Text.begin(), Text.end(), Text.begin(),
                               [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
                return Text;
            }
        } // namespace

        void SceneEditor::ReleaseThumbnails()
        {
            for (auto& [Path, Image] : Thumbnails_)
                core::ReleaseGpuImage(Gpu_, Image);
            Thumbnails_.clear();
            // Also frees the textures the models used.
            ThumbnailRenderer_.Shutdown();
            ThumbnailRendererReady_.reset();
        }

        bool SceneEditor::RenderModelThumbnail(const core::MediaFile& File, core::GpuImage& Out)
        {
            std::string Error;
            if (!ThumbnailRendererReady_)
                ThumbnailRendererReady_ = ThumbnailRenderer_.Initialize(Gpu_, Error);
            if (!*ThumbnailRendererReady_)
                return false;
            // The tile's own colour around the model, as a 0xRRGGBB value.
            const scene::Rgb Backdrop = ((Theme::Panel & 0xFF) << 16) | (Theme::Panel & 0xFF00) |
                                        ((Theme::Panel >> 16) & 0xFF);
            return ThumbnailRenderer_.RenderThumbnail(Project_->AssetRoot, File.RelativeName, KThumbnailSize,
                                                      Backdrop, Out, Error);
        }

        void SceneEditor::RefreshAssets()
        {
            ReleaseThumbnails();
            ModelAnimations_.clear();
            AssetScan_ = {};
            AssetScanRoot_.clear();
            if (Project_)
            {
                AssetScan_ = core::ScanMedia(Project_->AssetRoot, KAssetLimit);
                AssetScanRoot_ = Project_->AssetRoot;
            }
        }

        const core::MediaScan& SceneEditor::Assets()
        {
            if (Project_ && AssetScanRoot_ != Project_->AssetRoot)
                RefreshAssets();
            return AssetScan_;
        }

        void SceneEditor::DrawAssets()
        {
            if (!Project_)
                return;
            Assets();

            if (ImGui::Button("Refresh"))
                RefreshAssets();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("##kind", &AssetFilterKind_, "All\0Images\0Models\0Sounds\0");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
            ImGui::InputTextWithHint("##filter", "Filter", AssetFilter_, sizeof(AssetFilter_));
            if (AssetScan_.Truncated)
                ImGui::TextDisabled("Only the first %zu files are listed.", KAssetLimit);

            const std::string Filter = Lower(AssetFilter_);
            const float Tile = KThumbnailSize * 0.75f;
            const ImGuiStyle& Style = ImGui::GetStyle();
            const float Cell = Tile + Style.ItemSpacing.x;
            const int Columns =
                std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + Style.ItemSpacing.x) / Cell));
            int Loaded = 0;
            int ModelsLoaded = 0;
            int Column = 0;

            ImGui::BeginChild("##asset-grid");
            for (const core::MediaFile& File : AssetScan_.Files)
            {
                const bool Image = File.Kind == core::MediaKind::Image;
                const bool Model = File.Kind == core::MediaKind::Model;
                const bool Sound = File.Kind == core::MediaKind::Audio;
                if ((!Image && !Model && !Sound) || (AssetFilterKind_ == 1 && !Image) ||
                    (AssetFilterKind_ == 2 && !Model) || (AssetFilterKind_ == 3 && !Sound))
                    continue;
                if (!Filter.empty() && Lower(File.RelativeName).find(Filter) == std::string::npos)
                    continue;

                ImGui::PushID(File.RelativeName.c_str());
                if (Column > 0)
                    ImGui::SameLine();
                ImGui::BeginGroup();
                const ImVec2 Min = ImGui::GetCursorScreenPos();
                const ImVec2 Max(Min.x + Tile, Min.y + Tile);
                ImGui::InvisibleButton("##tile", ImVec2(Tile, Tile));
                const bool Hovered = ImGui::IsItemHovered();
                ImDrawList* Draw = ImGui::GetWindowDrawList();
                Draw->AddRectFilled(Min, Max, Hovered ? Theme::Hover : Theme::Panel, 4.0f);

                SDL_GPUTexture* Thumbnail = nullptr;
                if ((Image || Model) && ImGui::IsItemVisible())
                {
                    auto Found = Thumbnails_.find(File.RelativeName);
                    int& Count = Model ? ModelsLoaded : Loaded;
                    if (Found == Thumbnails_.end() &&
                        Count < (Model ? KModelThumbnailsPerFrame : KThumbnailsPerFrame) && Gpu_ != nullptr)
                    {
                        // A failed load keeps an empty entry, so it is tried once.
                        core::GpuImage Fresh;
                        std::string Error;
                        if (Model)
                            RenderModelThumbnail(File, Fresh);
                        else
                            core::LoadGpuImage(Gpu_, File.Path, Fresh, Error, KThumbnailSize);
                        Found = Thumbnails_.emplace(File.RelativeName, Fresh).first;
                        ++Count;
                    }
                    if (Found != Thumbnails_.end())
                        Thumbnail = Found->second.Texture;
                    if (Thumbnail != nullptr)
                    {
                        const core::GpuImage& Picture = Found->second;
                        const float Scale = (Tile - 8.0f) / static_cast<float>(std::max(Picture.Width, Picture.Height));
                        const ImVec2 Size(Picture.Width * Scale, Picture.Height * Scale);
                        const ImVec2 Corner(Min.x + (Tile - Size.x) * 0.5f, Min.y + (Tile - Size.y) * 0.5f);
                        Draw->AddImage(reinterpret_cast<ImTextureID>(Thumbnail), Corner,
                                       ImVec2(Corner.x + Size.x, Corner.y + Size.y));
                    }
                }
                if (Thumbnail == nullptr)
                {
                    // A wireframe cube for models, a speaker for sounds, a frame for images without a preview.
                    const ImVec2 Center(Min.x + Tile * 0.5f, Min.y + Tile * 0.5f);
                    const float R = Tile * 0.22f;
                    if (Sound)
                    {
                        Draw->AddRectFilled(ImVec2(Center.x - R, Center.y - R * 0.35f),
                                            ImVec2(Center.x - R * 0.45f, Center.y + R * 0.35f), Theme::Number);
                        Draw->AddTriangleFilled(ImVec2(Center.x - R * 0.45f, Center.y - R * 0.35f),
                                                ImVec2(Center.x + R * 0.2f, Center.y - R),
                                                ImVec2(Center.x + R * 0.2f, Center.y + R), Theme::Number);
                        Draw->PathArcTo(ImVec2(Center.x + R * 0.2f, Center.y), R * 0.7f, -0.8f, 0.8f);
                        Draw->PathStroke(Theme::Number, 0, 1.5f);
                    }
                    else if (Model)
                    {
                        const float S = R * 0.6f;
                        Draw->AddRect(ImVec2(Center.x - R, Center.y - S), ImVec2(Center.x + S, Center.y + R),
                                      Theme::Keyword, 0.0f, 0, 1.5f);
                        Draw->AddRect(ImVec2(Center.x - S, Center.y - R), ImVec2(Center.x + R, Center.y + S),
                                      Theme::Keyword, 0.0f, 0, 1.5f);
                    }
                    else
                    {
                        Draw->AddRect(ImVec2(Center.x - R, Center.y - R), ImVec2(Center.x + R, Center.y + R),
                                      Theme::String, 2.0f, 0, 1.5f);
                    }
                }

                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(KAssetPayload, File.RelativeName.c_str(), File.RelativeName.size() + 1);
                    ImGui::TextUnformatted(File.RelativeName.c_str());
                    ImGui::EndDragDropSource();
                }
                if (Hovered)
                    ImGui::SetTooltip("%s\nDrag onto the viewport or a file field; double-click to apply.",
                                      File.RelativeName.c_str());
                if (Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && Editable())
                {
                    scene::SceneNode* Node = Selected();
                    if (Image && Node != nullptr && !scene::IsMarker(Node->Kind))
                    {
                        // A selected terrain without a heightmap takes the image as one.
                        if (Node->Kind == scene::NodeKind::Terrain && Node->Asset.empty())
                            Node->Asset = File.RelativeName;
                        else
                            Node->Mat.Texture = File.RelativeName;
                    }
                    else if (Model)
                    {
                        CreateFromAsset(scene::NodeKind::Model, File.RelativeName, std::nullopt);
                    }
                    else if (Sound && Node != nullptr && Node->Kind == scene::NodeKind::Sound)
                    {
                        Node->Asset = File.RelativeName;
                    }
                    else if (Sound)
                    {
                        CreateFromAsset(scene::NodeKind::Sound, File.RelativeName, std::nullopt);
                    }
                }

                const std::string Label = File.Path.filename().string();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Tile);
                ImGui::TextDisabled("%s", Label.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
                Column = (Column + 1) % Columns;
            }
            ImGui::EndChild();
        }
    } // namespace panels

} // namespace dbc::studio
