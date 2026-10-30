// The Scene view's paint tool: places copies of an object over the level.

#include "panels/scene_editor.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include "scene/scene_geometry.hpp"

namespace dbc::studio
{
    namespace panels
    {
        void SceneEditor::EnterPaintTool()
        {
            const scene::SceneNode* Node = Selected();
            if (Node != nullptr && scene::IdSpaceOf(Node->Kind) == scene::IdSpace::Object)
                SetPaintObject(*Node);
        }

        void SceneEditor::SetPaintObject(const scene::SceneNode& Node)
        {
            PaintObject_ = Node;
            // The first frame after a new object has not loaded its mesh yet.
            const scene::CpuMesh* Mesh = Renderer_.Mesh(Node);
            const scene::CpuMesh Built = Mesh == nullptr && Node.Kind != scene::NodeKind::Model
                                             ? scene::BuildMesh(Node, Document_.Profile)
                                             : scene::CpuMesh{};
            PaintMin_ = Mesh != nullptr ? Mesh->Min : Built.Min;
            PaintMax_ = Mesh != nullptr ? Mesh->Max : Built.Max;
        }

        void SceneEditor::SetPaintModel(const std::string& Relative)
        {
            scene::SceneNode Node;
            Node.Kind = scene::NodeKind::Model;
            Node.Asset = Relative;
            Node.Name = dbc::PathToUtf8(dbc::PathFromUtf8(Relative).stem());
            Node.Mat.Flags = scene::DefaultObjectFlags(Document_.Profile);
            PaintObject_ = Node;
            PaintMin_ = PaintMax_ = glm::vec3(0.0f);
            scene::CpuMesh Mesh;
            std::string Error;
            if (scene::LoadModelMesh(Project_->AssetRoot / dbc::PathFromUtf8(Relative), Mesh, Error))
            {
                PaintMin_ = Mesh.Min;
                PaintMax_ = Mesh.Max;
            }
            else
            {
                Host_.Log("Cannot paint " + Relative + ": " + Error, true);
                PaintObject_.reset();
            }
        }

        void SceneEditor::DrawPaintBar()
        {
            const ImGuiStyle& Style = ImGui::GetStyle();
            const float FontSize = ImGui::GetFontSize();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Style.ItemSpacing.x);

            ImGui::SetNextItemWidth(FontSize * 10.0f);
            if (ImGui::BeginCombo("##paint-object", PaintObject_ ? PaintObject_->Name.c_str() : "Choose an object"))
            {
                // Copies of one object appear once.
                std::vector<const scene::SceneNode*> Looks;
                for (const scene::SceneNode& Node : Document_.Nodes)
                {
                    if (scene::IdSpaceOf(Node.Kind) != scene::IdSpace::Object ||
                        std::any_of(Looks.begin(), Looks.end(),
                                    [&](const scene::SceneNode* Other) { return scene::SameLook(*Other, Node); }))
                        continue;
                    Looks.push_back(&Node);
                    ImGui::PushID(Node.Id);
                    if (ImGui::Selectable(Node.Painted ? scene::PaintedStem(Node.Name).c_str() : Node.Name.c_str()))
                        SetPaintObject(Node);
                    ImGui::PopID();
                }
                ImGui::SeparatorText("Model files");
                for (const core::MediaFile& File : Assets().Files)
                    if (File.Kind == core::MediaKind::Model && ImGui::Selectable(File.RelativeName.c_str()))
                        SetPaintModel(File.RelativeName);
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("What to place. Entering the tool takes the selected object.\n"
                                  "Click to place; hold Shift to erase its painted copies within the radius");
            ImGui::SameLine();

            const auto ModeButton = [&](const char* Label, PaintMode Mode, const char* Tip)
            {
                if (ImGui::RadioButton(Label, PaintMode_ == Mode))
                    PaintMode_ = Mode;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tip);
                ImGui::SameLine();
            };
            ModeButton("One", PaintMode::One, "A click places one object, on the grid while snapping (1)");
            ModeButton("Several", PaintMode::Several, "A click scatters a number of objects over the radius (2)");
            ModeButton("Brush", PaintMode::Brush, "A drag fills the radius up to the density (3)");

            const auto Width = [&] { ImGui::SetNextItemWidth(FontSize * 4.0f); };
            if (PaintMode_ == PaintMode::Several)
            {
                Width();
                ImGui::DragInt("Count", &PaintCount_, 0.2f, 1, 200);
                ImGui::SameLine();
            }
            Width();
            ImGui::DragFloat("Radius", &PaintRadius_, 0.5f, 1.0f, 2000.0f, "%.0f");
            ImGui::SameLine();
            if (PaintMode_ == PaintMode::Brush)
            {
                Width();
                ImGui::DragFloat("Density", &PaintDensity_, 0.1f, 0.1f, 500.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Objects per 100 x 100 units");
                ImGui::SameLine();
            }
            ImGui::Checkbox("Random turn", &PaintTurn_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(FontSize * 7.0f);
            if (ImGui::DragFloat2("Scale %", &PaintScale_.x, 0.5f, 1.0f, 1000.0f, "%.0f"))
                PaintScale_.y = std::max(PaintScale_.x, PaintScale_.y);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Each object's scale is multiplied by a value in this range");
        }

        std::optional<float> SceneEditor::PaintSurfaceDistance(const scene::Ray& Ray) const
        {
            std::optional<float> Nearest;
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                // What is painted is not a surface to paint on, or trees would grow on trees.
                if (Node.Painted || scene::IsMarker(Node.Kind) ||
                    (PaintObject_ && scene::SameLook(Node, *PaintObject_)))
                    continue;
                const scene::CpuMesh* Mesh = Renderer_.Mesh(Node);
                if (Mesh == nullptr)
                    continue;
                const std::optional<float> Distance = scene::Intersect(Ray, *Mesh, scene::NodeWorld(Node));
                if (Distance && (!Nearest || *Distance < *Nearest))
                    Nearest = Distance;
            }
            return Nearest;
        }

        void SceneEditor::PaintStamp(const glm::vec3& Center, bool OnMesh)
        {
            const glm::vec2 Middle(Center.x, Center.z);
            std::vector<glm::vec2> Spots;
            switch (PaintMode_)
            {
                case PaintMode::One:
                {
                    const glm::vec3 At = SnapActive() ? scene::SnapToGrid(Center, MoveSnap_) : Center;
                    Spots.push_back(glm::vec2(At.x, At.z));
                    break;
                }
                case PaintMode::Several:
                    Spots = scene::ScatterInDisc(Middle, PaintRadius_, PaintCount_, Random_);
                    break;
                case PaintMode::Brush:
                {
                    std::vector<glm::vec2> Existing;
                    for (const scene::SceneNode& Node : Document_.Nodes)
                        if (scene::IdSpaceOf(Node.Kind) == scene::IdSpace::Object &&
                            scene::SameLook(Node, *PaintObject_))
                            Existing.push_back(glm::vec2(Node.Position.x, Node.Position.z));
                    Spots = scene::ScatterToDensity(Middle, PaintRadius_, PaintDensity_, Existing, Random_);
                    break;
                }
            }

            const std::vector<int> Ids = Document_.NextIds(scene::IdSpace::Object, Spots.size());
            std::uniform_real_distribution<float> Turn(0.0f, 360.0f);
            std::uniform_real_distribution<float> Scale(PaintScale_.x, PaintScale_.y);
            for (std::size_t Index = 0; Index < Spots.size(); ++Index)
            {
                const glm::vec2 Spot = Spots[Index];
                float Ground = Center.y;
                if (Spot != Middle)
                {
                    // Straight down from well above the middle of the stamp, in the same frame as its height.
                    constexpr float KAbove = 10000.0f;
                    const scene::Ray Down{glm::vec3(Spot.x, Center.y + KAbove, Spot.y), glm::vec3(0.0f, -1.0f, 0.0f)};
                    const std::optional<float> Distance = PaintSurfaceDistance(Down);
                    // Past a mesh's edge there is nothing to stand on.
                    if (!Distance && OnMesh)
                        continue;
                    Ground = Distance ? Center.y + KAbove - *Distance : 0.0f;
                }

                scene::SceneNode Node = *PaintObject_;
                Node.Id = Ids[Index];
                Node.Name = scene::PaintedName(PaintObject_->Name, Node.Id);
                Node.Painted = true;
                Node.Scale = PaintObject_->Scale * (Scale(Random_) / 100.0f);
                if (PaintTurn_)
                    Node.Rotation.y += Turn(Random_);
                const float Height = scene::RestingHeight(PaintMin_, PaintMax_, Node.Rotation, Node.Scale);
                Node.Position = glm::vec3(Spot.x, Ground + Height, Spot.y);
                Document_.Nodes.push_back(std::move(Node));
            }
        }

        void SceneEditor::PaintErase(const glm::vec3& Center)
        {
            const glm::vec2 Middle(Center.x, Center.z);
            std::erase_if(Document_.Nodes,
                          [&](const scene::SceneNode& Node)
                          {
                              return Node.Painted && scene::SameLook(Node, *PaintObject_) &&
                                     glm::length(glm::vec2(Node.Position.x, Node.Position.z) - Middle) <= PaintRadius_;
                          });
            PruneSelection();
        }

        bool SceneEditor::UpdatePaint(bool Hovered, ImDrawList* Draw)
        {
            if (Tool_ != Tool::Paint || !Editable())
            {
                Painting_ = false;
                return false;
            }
            const ImGuiIO& Io = ImGui::GetIO();
            const scene::Ray Ray = RayAt(ImGui::GetMousePos());
            const std::optional<float> Distance = PaintSurfaceDistance(Ray);
            std::optional<glm::vec3> Center;
            if (Distance)
                Center = Ray.Origin + Ray.Direction * *Distance;
            else if (Ray.Direction.y < -0.001f)
                Center = Ray.Origin + Ray.Direction * (-Ray.Origin.y / Ray.Direction.y);

            const bool Erasing = Io.KeyShift;
            if (Center && (Hovered || Painting_))
            {
                // The radius shows where it applies: to erase, and to every mode but One.
                const float Radius = Erasing || PaintMode_ != PaintMode::One ? PaintRadius_ : MoveSnap_ * 0.5f;
                const ImU32 Color = Erasing ? IM_COL32(255, 110, 90, 255) : IM_COL32(120, 230, 140, 255);
                constexpr int KSteps = 48;
                ImVec2 Previous;
                bool HasPrevious = false;
                for (int Step = 0; Step <= KSteps; ++Step)
                {
                    const float Angle = glm::two_pi<float>() * static_cast<float>(Step) / KSteps;
                    ImVec2 At;
                    const bool Visible =
                        ProjectToScreen(*Center + glm::vec3(std::cos(Angle), 0.0f, std::sin(Angle)) * Radius, At);
                    if (Visible && HasPrevious)
                        Draw->AddLine(Previous, At, Color, 2.0f);
                    Previous = At;
                    HasPrevious = Visible;
                }
            }

            if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Io.KeyAlt && !Navigating_ && PaintObject_)
            {
                Painting_ = true;
                PaintLast_.reset();
            }
            if (Painting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                Painting_ = false;
                Commit();
            }
            if (Painting_ && Center)
            {
                // Each stamp overlaps the last, so that a drag leaves no gaps.
                const bool Repeats = Erasing || PaintMode_ == PaintMode::Brush;
                if (!PaintLast_ || (Repeats && glm::length(*Center - *PaintLast_) >= PaintRadius_ * 0.5f))
                {
                    PaintLast_ = *Center;
                    if (Erasing)
                        PaintErase(*Center);
                    else
                        PaintStamp(*Center, Distance.has_value());
                }
            }
            return true;
        }
    } // namespace panels

} // namespace dbc::studio
