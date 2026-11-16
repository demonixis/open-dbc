// The Scene view's sculpt tool: a round brush that edits a terrain's heightmap.

#include "panels/scene_editor.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "core/i18n.hpp"
#include "scene/heightmap.hpp"
#include "scene/scene_geometry.hpp"

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            // Undo steps of terrain pixels kept, all terrains together: a 512 x 512 one is a megabyte.
            constexpr std::size_t KMaxTerrainStates = 48;
        } // namespace

        scene::Heightmap* SceneEditor::TerrainHeights(const scene::SceneNode& Node)
        {
            if (TerrainCache_.Id == Node.Id && TerrainCache_.Asset == Node.Asset &&
                TerrainCache_.Revision == Node.HeightRevision)
                return &TerrainCache_.Map;
            if (!Project_ || Node.Asset.empty())
                return nullptr;
            scene::Heightmap Map;
            std::string Error;
            if (!scene::ReadHeightmap(Project_->AssetRoot / dbc::PathFromUtf8(Node.Asset), Map, Error))
                return nullptr;
            TerrainCache_ = TerrainCache{Node.Id, Node.Asset, Node.HeightRevision, std::move(Map)};
            return &TerrainCache_.Map;
        }

        bool SceneEditor::SetTerrainHeights(scene::SceneNode& Node, const scene::Heightmap& Map)
        {
            if (!Project_)
                return false;
            // Always the level's own file: a heightmap the user picked is never overwritten.
            const std::filesystem::path Relative = scene::TerrainHeightmapPath(Document_.Name, Node.Id);
            const std::filesystem::path Path = Project_->AssetRoot / Relative;
            const std::string Own = dbc::GenericPathToUtf8(Relative);
            // What the file holds now is what undo puts back.
            if (Node.Asset == Own && !TerrainStates_.contains({Node.Id, Node.HeightRevision}))
            {
                scene::Heightmap Before;
                std::string Ignored;
                if (scene::ReadHeightmap(Path, Before, Ignored))
                    TerrainStates_[{Node.Id, Node.HeightRevision}] = std::move(Before);
            }
            if (!scene::WriteHeightmap(Path, Map, TerrainError_))
                return false;
            TerrainError_.clear();
            Node.Asset = Own;
            Node.HeightRevision = ++TerrainRevisions_;
            TerrainOnDisk_[Node.Id] = Node.HeightRevision;
            TerrainStates_[{Node.Id, Node.HeightRevision}] = Map;
            while (TerrainStates_.size() > KMaxTerrainStates)
                TerrainStates_.erase(std::min_element(TerrainStates_.begin(), TerrainStates_.end(),
                                                      [](const auto& Left, const auto& Right)
                                                      { return Left.first.second < Right.first.second; }));
            if (&Map != &TerrainCache_.Map)
                TerrainCache_.Map = Map;
            TerrainCache_.Id = Node.Id;
            TerrainCache_.Asset = Own;
            TerrainCache_.Revision = Node.HeightRevision;
            RefreshAssets();
            return true;
        }

        void SceneEditor::RestoreTerrainHeights()
        {
            if (!Project_)
                return;
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                if (Node.Kind != scene::NodeKind::Terrain)
                    continue;
                const std::filesystem::path Relative = scene::TerrainHeightmapPath(Document_.Name, Node.Id);
                const auto OnDisk = TerrainOnDisk_.find(Node.Id);
                // A terrain on an image the user picked shows that image, which no edit touched.
                if (Node.Asset != dbc::GenericPathToUtf8(Relative) || OnDisk == TerrainOnDisk_.end() ||
                    OnDisk->second == Node.HeightRevision)
                    continue;
                const auto State = TerrainStates_.find({Node.Id, Node.HeightRevision});
                if (State == TerrainStates_.end())
                {
                    Host_.Log(TrFmt("The heights of {0} are too old to undo", {Node.Name}), true);
                    continue;
                }
                std::string Error;
                if (scene::WriteHeightmap(Project_->AssetRoot / Relative, State->second, Error))
                    OnDisk->second = Node.HeightRevision;
                else
                    Host_.Log(Error, true);
            }
            // The image changed under the cache's key.
            TerrainCache_ = {};
        }

        void SceneEditor::ResetTerrainEdits()
        {
            Sculpting_ = false;
            Renderer_.SetLiveTerrain(0, nullptr, 0);
            TerrainCache_ = {};
            TerrainStates_.clear();
            TerrainOnDisk_.clear();
        }

        bool SceneEditor::UpdateSculpt(bool Hovered, ImDrawList* Draw)
        {
            const auto EndStroke = [&]
            {
                if (!Sculpting_)
                    return;
                Sculpting_ = false;
                Renderer_.SetLiveTerrain(0, nullptr, 0);
                if (!SculptChanged_)
                    return;
                for (scene::SceneNode& Node : Document_.Nodes)
                    if (Node.Kind == scene::NodeKind::Terrain && Node.Id == SculptTerrain_ &&
                        TerrainCache_.Id == Node.Id)
                    {
                        if (SetTerrainHeights(Node, TerrainCache_.Map))
                            Commit();
                        else
                        {
                            Host_.Log(TerrainError_, true);
                            TerrainCache_ = {};
                        }
                        break;
                    }
            };
            if (Tool_ != Tool::Sculpt || !Editable())
            {
                EndStroke();
                return false;
            }

            const ImGuiIO& Io = ImGui::GetIO();
            const scene::Ray Ray = RayAt(ImGui::GetMousePos());
            // The nearest terrain under the pointer; a stroke stays on the one it began on.
            const scene::SceneNode* Hit = nullptr;
            float Nearest = 0.0f;
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                if (Node.Kind != scene::NodeKind::Terrain || (Sculpting_ && Node.Id != SculptTerrain_))
                    continue;
                const scene::CpuMesh* Mesh = Renderer_.Mesh(Node);
                if (Mesh == nullptr)
                    continue;
                const std::optional<float> Distance = scene::Intersect(Ray, *Mesh, scene::NodeWorld(Node));
                if (Distance && (Hit == nullptr || *Distance < Nearest))
                {
                    Hit = &Node;
                    Nearest = *Distance;
                }
            }
            scene::Heightmap* Map = Hit != nullptr ? TerrainHeights(*Hit) : nullptr;

            // A terrain's pixels are Size / (Size - 1) units apart, from its position toward -Z.
            const float Step =
                Map != nullptr ? static_cast<float>(Map->Size) / static_cast<float>(Map->Size - 1) : 1.0f;
            glm::vec2 Center(0.0f);
            if (Map != nullptr)
            {
                const glm::vec3 Point = Ray.Origin + Ray.Direction * Nearest;
                Center = glm::vec2(Point.x - Hit->Position.x, Hit->Position.z - Point.z) / Step;
            }
            const bool Lower = (SculptTool_ == SculptTool::Lower) != (Io.KeyShift && SculptTool_ <= SculptTool::Lower);

            if (Map != nullptr && (Hovered || Sculpting_))
            {
                // The brush lies on the ground: its edge, and inside it where the falloff begins.
                const auto Circle = [&](float Radius, ImU32 Color, float Thickness)
                {
                    constexpr int KSteps = 64;
                    ImVec2 Previous;
                    bool HasPrevious = false;
                    for (int Index = 0; Index <= KSteps; ++Index)
                    {
                        const float Angle = glm::two_pi<float>() * static_cast<float>(Index) / KSteps;
                        const glm::vec2 Pixel = Center + glm::vec2(std::cos(Angle), std::sin(Angle)) * Radius / Step;
                        const glm::vec3 World(Hit->Position.x + Pixel.x * Step,
                                              Hit->Position.y + scene::SampleHeightmap(*Map, Pixel.x, Pixel.y),
                                              Hit->Position.z - Pixel.y * Step);
                        ImVec2 At;
                        const bool Visible = ProjectToScreen(World, At);
                        if (Visible && HasPrevious)
                            Draw->AddLine(Previous, At, Color, Thickness);
                        Previous = At;
                        HasPrevious = Visible;
                    }
                };
                const ImU32 Color = Lower ? IM_COL32(255, 110, 90, 255) : IM_COL32(120, 230, 140, 255);
                Circle(SculptBrush_.Radius, Color, 2.0f);
                if (SculptBrush_.Falloff > 0.0f && SculptBrush_.Falloff < 1.0f)
                    Circle(SculptBrush_.Radius * (1.0f - SculptBrush_.Falloff), (Color & 0x00ffffffu) | 0x80000000u,
                           1.0f);
            }

            if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Io.KeyAlt && !Navigating_ && Map != nullptr)
            {
                Sculpting_ = true;
                SculptChanged_ = false;
                SculptTerrain_ = Hit->Id;
                SculptTarget_ = scene::SampleHeightmap(*Map, Center.x, Center.y);
            }
            if (Sculpting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                EndStroke();
            if (Sculpting_ && Map != nullptr)
            {
                // The brush works for as long as it is held: its strength is per second.
                const scene::TerrainBrush Brush{SculptBrush_.Radius / Step, SculptBrush_.Falloff};
                const float Time = std::min(Io.DeltaTime, 0.1f);
                bool Changed = false;
                switch (SculptTool_)
                {
                    case SculptTool::Raise:
                    case SculptTool::Lower:
                        Changed = scene::SculptHeightmap(*Map, scene::SculptMode::Raise, Brush, Center,
                                                         (Lower ? -SculptStrength_ : SculptStrength_) * Time);
                        break;
                    case SculptTool::Smooth:
                        Changed = scene::SculptHeightmap(*Map, scene::SculptMode::Smooth, Brush, Center,
                                                         SculptStrength_ * 0.1f * Time);
                        break;
                    case SculptTool::Flatten:
                        Changed = scene::SculptHeightmap(*Map, scene::SculptMode::Flatten, Brush, Center,
                                                         SculptStrength_ * 0.1f * Time, SculptTarget_);
                        break;
                }
                if (Changed)
                {
                    SculptChanged_ = true;
                    Renderer_.SetLiveTerrain(SculptTerrain_, Map, ++SculptRevision_);
                }
            }
            return true;
        }
    } // namespace panels

} // namespace dbc::studio
