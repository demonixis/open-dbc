// The Scene view's Assets panel: the project's images, models and sounds, as tiles
// that drag onto the viewport and the inspector's file fields.

#include "panels/scene_editor.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

#include "core/i18n.hpp"
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

            // A primitive's outline, drawn in a square of half-side R around Centre.
            void DrawPrimitiveIcon(ImDrawList* Draw, scene::NodeKind Kind, ImVec2 Centre, float R)
            {
                const ImU32 Ink = Theme::Keyword;
                constexpr float KLine = 1.5f;
                const auto Ellipse = [&](float Y, float Rx, float Ry)
                {
                    constexpr int KSegments = 24;
                    ImVec2 Points[KSegments];
                    for (int Index = 0; Index < KSegments; ++Index)
                    {
                        const float Angle = static_cast<float>(Index) * (6.2831853f / KSegments);
                        Points[Index] = ImVec2(Centre.x + std::cos(Angle) * Rx, Y + std::sin(Angle) * Ry);
                    }
                    Draw->AddPolyline(Points, KSegments, Ink, ImDrawFlags_Closed, KLine);
                };
                // A box seen from above its front: two faces and the edges between them.
                const auto Cuboid = [&](float HalfW, float HalfH, float Shift)
                {
                    const ImVec2 Front(Centre.x - Shift * 0.5f, Centre.y + Shift * 0.5f);
                    const ImVec2 Back(Centre.x + Shift * 0.5f, Centre.y - Shift * 0.5f);
                    Draw->AddRect(ImVec2(Front.x - HalfW, Front.y - HalfH), ImVec2(Front.x + HalfW, Front.y + HalfH),
                                  Ink, 0.0f, 0, KLine);
                    Draw->AddRect(ImVec2(Back.x - HalfW, Back.y - HalfH), ImVec2(Back.x + HalfW, Back.y + HalfH), Ink,
                                  0.0f, 0, KLine);
                    for (const float Sx : {-1.0f, 1.0f})
                        for (const float Sy : {-1.0f, 1.0f})
                            Draw->AddLine(ImVec2(Front.x + Sx * HalfW, Front.y + Sy * HalfH),
                                          ImVec2(Back.x + Sx * HalfW, Back.y + Sy * HalfH), Ink, KLine);
                };
                switch (Kind)
                {
                    case scene::NodeKind::Cube:
                        Cuboid(R * 0.7f, R * 0.7f, R * 0.6f);
                        break;
                    case scene::NodeKind::Box:
                        Cuboid(R * 0.85f, R * 0.4f, R * 0.5f);
                        break;
                    case scene::NodeKind::Sphere:
                        Draw->AddCircle(Centre, R, Ink, 32, KLine);
                        Ellipse(Centre.y, R, R * 0.35f);
                        break;
                    case scene::NodeKind::Cylinder:
                        Ellipse(Centre.y - R * 0.7f, R * 0.7f, R * 0.25f);
                        Ellipse(Centre.y + R * 0.7f, R * 0.7f, R * 0.25f);
                        for (const float Side : {-1.0f, 1.0f})
                            Draw->AddLine(ImVec2(Centre.x + Side * R * 0.7f, Centre.y - R * 0.7f),
                                          ImVec2(Centre.x + Side * R * 0.7f, Centre.y + R * 0.7f), Ink, KLine);
                        break;
                    case scene::NodeKind::Cone:
                        Ellipse(Centre.y + R * 0.7f, R * 0.75f, R * 0.25f);
                        for (const float Side : {-1.0f, 1.0f})
                            Draw->AddLine(ImVec2(Centre.x, Centre.y - R),
                                          ImVec2(Centre.x + Side * R * 0.75f, Centre.y + R * 0.7f), Ink, KLine);
                        break;
                    case scene::NodeKind::Plain:
                        Draw->AddQuad(ImVec2(Centre.x - R * 0.5f, Centre.y - R * 0.45f),
                                      ImVec2(Centre.x + R, Centre.y - R * 0.45f),
                                      ImVec2(Centre.x + R * 0.5f, Centre.y + R * 0.45f),
                                      ImVec2(Centre.x - R, Centre.y + R * 0.45f), Ink, KLine);
                        break;
                    default:
                        Draw->AddTriangle(ImVec2(Centre.x, Centre.y - R * 0.85f),
                                          ImVec2(Centre.x + R, Centre.y + R * 0.75f),
                                          ImVec2(Centre.x - R, Centre.y + R * 0.75f), Ink, KLine);
                        break;
                }
            }

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

        void SceneEditor::ApplyTheme()
        {
            ReleaseThumbnails();
        }

        bool SceneEditor::RenderModelThumbnail(const core::MediaFile& File, core::GpuImage& Out)
        {
            std::string Error;
            if (!ThumbnailRendererReady_)
                ThumbnailRendererReady_ = ThumbnailRenderer_.Initialize(Gpu_, Error);
            if (!*ThumbnailRendererReady_)
                return false;
            // The tile's own colour around the model, as a 0xRRGGBB value.
            const scene::Rgb Backdrop =
                ((Theme::Panel & 0xFF) << 16) | (Theme::Panel & 0xFF00) | ((Theme::Panel >> 16) & 0xFF);
            return ThumbnailRenderer_.RenderThumbnail(Project_->AssetRoot, File.RelativeName, KThumbnailSize, Backdrop,
                                                      Out, Error);
        }

        void SceneEditor::RefreshAssets()
        {
            ReleaseThumbnails();
            ModelInfos_.clear();
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

            if (ImGui::Button(TrId("Refresh")))
                RefreshAssets();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const char* const Kinds[] = {Tr("All"), Tr("Images"), Tr("Models"), Tr("Sounds")};
            ImGui::Combo("##kind", &AssetFilterKind_, Kinds, 4);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
            ImGui::InputTextWithHint("##filter", Tr("Filter"), AssetFilter_, sizeof(AssetFilter_));
            if (AssetScan_.Truncated)
                ImGui::TextDisabled(
                    "%s", TrFmt("Only the first {0} files are listed.", {std::to_string(KAssetLimit)}).c_str());

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
            // The shapes come first: they need no file. They count as models for the filter.
            const bool Touch = ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
            for (const scene::NodeKind Kind : KPrimitives)
            {
                if (AssetFilterKind_ != 0 && AssetFilterKind_ != 2)
                    break;
                const char* Label = Tr(scene::NodeKindLabel(Kind));
                if (!Filter.empty() && Lower(Label).find(Filter) == std::string::npos)
                    continue;
                ImGui::PushID(static_cast<int>(Kind));
                if (Column > 0)
                    ImGui::SameLine();
                ImGui::BeginGroup();
                const ImVec2 Min = ImGui::GetCursorScreenPos();
                const bool Clicked = ImGui::InvisibleButton("##primitive", ImVec2(Tile, Tile));
                const bool Hovered = ImGui::IsItemHovered();
                ImDrawList* Draw = ImGui::GetWindowDrawList();
                Draw->AddRectFilled(Min, ImVec2(Min.x + Tile, Min.y + Tile), Hovered ? Theme::Hover : Theme::Panel,
                                    4.0f);
                DrawPrimitiveIcon(Draw, Kind, ImVec2(Min.x + Tile * 0.5f, Min.y + Tile * 0.5f), Tile * 0.28f);
                if (ImGui::BeginDragDropSource())
                {
                    const std::uint8_t Value = static_cast<std::uint8_t>(Kind);
                    ImGui::SetDragDropPayload(KPrimitivePayload, &Value, sizeof(Value));
                    ImGui::TextUnformatted(Label);
                    ImGui::EndDragDropSource();
                }
                if (Hovered)
                {
                    Theme::MarkTouchDragSource();
                    ImGui::SetTooltip("%s\n%s", Label, Tr("Drag onto the viewport or double-click to add."));
                }
                // A finger taps where a mouse double-clicks.
                if (Editable() && (Touch ? Clicked : Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))
                    Create(Kind);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Tile);
                ImGui::TextDisabled("%s", Label);
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
                Column = (Column + 1) % Columns;
            }
            bool FirstFile = false;
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
                if (!FirstFile)
                {
                    FirstFile = true;
                    FirstFileTile_ = glm::vec2(Min.x, Min.y);
                }
                const ImVec2 Max(Min.x + Tile, Min.y + Tile);
                const bool Clicked = ImGui::InvisibleButton("##tile", ImVec2(Tile, Tile));
                const bool Hovered = ImGui::IsItemHovered();
                // Wait for release so dragging preserves the Inspector's file drop targets.
                // The second click adds a node below; keep that node selected on release.
                if (Clicked && ImGui::GetIO().MouseClickedLastCount[ImGuiMouseButton_Left] == 1)
                {
                    if (Model)
                    {
                        Select(nullptr);
                        InspectedModel_ = File.RelativeName;
                    }
                    else
                        InspectedModel_.clear();
                }
                ImDrawList* Draw = ImGui::GetWindowDrawList();
                Draw->AddRectFilled(Min, Max, Hovered ? Theme::Hover : Theme::Panel, 4.0f);
                if (InspectedModel_ == File.RelativeName)
                    Draw->AddRect(Min, Max, Theme::Keyword, 4.0f, 0, 2.0f);

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
                    Theme::MarkTouchDragSource();
                if (Hovered)
                    ImGui::SetTooltip("%s\n%s", File.RelativeName.c_str(),
                                      Tr(Model ? "Click to inspect; drag onto the viewport or double-click to add."
                                               : "Drag onto the viewport or a file field; double-click to apply."));
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

                const std::string Label = dbc::PathToUtf8(File.Path.filename());
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
