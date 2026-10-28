// The Scene view's viewport: editor camera, picking, gizmos and asset drops.

#include "panels/scene_editor.hpp"

#include <algorithm>
#include <cmath>

#include <ImGuizmo.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/media_catalogue.hpp"
#include "scene/scene_geometry.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            constexpr float KEditorFov = 60.0f;
            constexpr float KEditorNear = 0.5f;
            constexpr float KEditorFar = 20000.0f;
            // Engine defaults, used when the level camera does not set them.
            constexpr float KCameraFov = 75.0f;
            constexpr float KCameraNear = 0.1f;
            constexpr float KCameraFar = 5000.0f;
            constexpr float KIconRadius = 7.0f;

            glm::vec3 Forward(float Yaw, float Pitch)
            {
                return scene::LightDirection(glm::vec3(Pitch, Yaw, 0.0f));
            }

            glm::vec3 RightOf(float Yaw)
            {
                const float Radians = glm::radians(Yaw);
                return {std::cos(Radians), 0.0f, -std::sin(Radians)};
            }

            void AddLine(std::vector<scene::LineVertex>& Lines, const glm::vec3& A, const glm::vec3& B,
                         const glm::vec4& Color)
            {
                Lines.push_back({A, Color});
                Lines.push_back({B, Color});
            }

            void AddCircle(std::vector<scene::LineVertex>& Lines, const glm::vec3& Center, const glm::vec3& U,
                           const glm::vec3& V, float Radius, const glm::vec4& Color)
            {
                constexpr int KSteps = 48;
                for (int Step = 0; Step < KSteps; ++Step)
                {
                    const float A0 = glm::two_pi<float>() * Step / KSteps;
                    const float A1 = glm::two_pi<float>() * (Step + 1) / KSteps;
                    AddLine(Lines, Center + (U * std::cos(A0) + V * std::sin(A0)) * Radius,
                            Center + (U * std::cos(A1) + V * std::sin(A1)) * Radius, Color);
                }
            }

            // Two axes perpendicular to `Direction`.
            void Basis(const glm::vec3& Direction, glm::vec3& U, glm::vec3& V)
            {
                const glm::vec3 Helper = std::abs(Direction.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                U = glm::normalize(glm::cross(Helper, Direction));
                V = glm::cross(Direction, U);
            }

            glm::mat4 GizmoMatrix(const scene::SceneNode& Node)
            {
                glm::mat4 World = glm::translate(glm::mat4(1.0f), Node.Position);
                if (Node.Kind == scene::NodeKind::Light)
                    return World * glm::mat4(scene::RotationMatrix(Node.Rotation));
                return scene::NodeWorld(Node);
            }
        } // namespace

        bool SceneEditor::ProjectToScreen(const glm::vec3& Point, ImVec2& Screen) const
        {
            const glm::vec4 Clip = View_.Projection * View_.View * glm::vec4(Point, 1.0f);
            if (Clip.w <= 0.0001f)
                return false;
            const glm::vec3 Ndc = glm::vec3(Clip) / Clip.w;
            Screen = ImVec2(ViewportMin_.x + (Ndc.x + 1.0f) * 0.5f * ViewportSize_.x,
                            ViewportMin_.y + (1.0f - Ndc.y) * 0.5f * ViewportSize_.y);
            return Ndc.z >= 0.0f && Ndc.z <= 1.0f;
        }

        scene::Ray SceneEditor::RayAt(const ImVec2& Mouse) const
        {
            const float X = (Mouse.x - ViewportMin_.x) / ViewportSize_.x * 2.0f - 1.0f;
            const float Y = 1.0f - (Mouse.y - ViewportMin_.y) / ViewportSize_.y * 2.0f;
            const glm::mat4 Inverse = glm::inverse(View_.Projection * View_.View);
            glm::vec4 Near = Inverse * glm::vec4(X, Y, 0.0f, 1.0f);
            glm::vec4 Far = Inverse * glm::vec4(X, Y, 1.0f, 1.0f);
            Near /= Near.w;
            Far /= Far.w;
            return {glm::vec3(Near), glm::normalize(glm::vec3(Far - Near))};
        }

        scene::ViewParameters SceneEditor::ComputeView(float Aspect) const
        {
            scene::ViewParameters View;
            const scene::Environment& Env = Document_.Env;
            if (LookThroughCamera_ && Env.UseCamera)
            {
                const glm::mat3 Rotation = scene::RotationMatrix(Env.CameraRotation);
                const glm::vec3 Direction = Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
                View.Eye = Env.CameraPosition;
                View.View = glm::lookAt(View.Eye, View.Eye + Direction, Rotation * glm::vec3(0.0f, 1.0f, 0.0f));
                const glm::vec2 Range = Env.CameraRange.value_or(glm::vec2(KCameraNear, KCameraFar));
                View.Projection = glm::perspective(glm::radians(Env.CameraFov.value_or(KCameraFov)), Aspect,
                                                   std::max(Range.x, 0.01f), std::max(Range.y, Range.x + 1.0f));
                return View;
            }
            View.Eye = Camera_.Position;
            View.View = glm::lookAt(View.Eye, View.Eye + Forward(Camera_.Yaw, Camera_.Pitch), glm::vec3(0, 1, 0));
            View.Projection = glm::perspective(glm::radians(KEditorFov), Aspect, KEditorNear, KEditorFar);
            return View;
        }

        void SceneEditor::UpdateCamera(bool Hovered)
        {
            const ImGuiIO& Io = ImGui::GetIO();
            const bool Orbit = Io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (Hovered &&
                (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                 (Io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
                Navigating_ = true;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !Orbit)
                Navigating_ = false;

            // The level camera view only moves with its own gizmo.
            if (LookThroughCamera_ && Document_.Env.UseCamera)
                return;

            const ImVec2 Delta = Io.MouseDelta;
            const glm::vec3 Ahead = Forward(Camera_.Yaw, Camera_.Pitch);
            const glm::vec3 Pivot = Camera_.Position + Ahead * Camera_.Distance;
            if (Navigating_ && ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                // Fly: look around, and move with WASD while the button is held.
                Camera_.Yaw += Delta.x * 0.25f;
                Camera_.Pitch = std::clamp(Camera_.Pitch + Delta.y * 0.25f, -89.0f, 89.0f);
                const glm::vec3 Look = Forward(Camera_.Yaw, Camera_.Pitch);
                const glm::vec3 Right = RightOf(Camera_.Yaw);
                glm::vec3 Move(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_W))
                    Move += Look;
                if (ImGui::IsKeyDown(ImGuiKey_S))
                    Move -= Look;
                if (ImGui::IsKeyDown(ImGuiKey_D))
                    Move += Right;
                if (ImGui::IsKeyDown(ImGuiKey_A))
                    Move -= Right;
                if (ImGui::IsKeyDown(ImGuiKey_E))
                    Move.y += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_Q))
                    Move.y -= 1.0f;
                const float Speed = std::max(Camera_.Distance, 20.0f) * (Io.KeyShift ? 3.0f : 1.0f);
                Camera_.Position += Move * Speed * Io.DeltaTime;
            }
            else if (Navigating_ && Orbit)
            {
                Camera_.Yaw += Delta.x * 0.3f;
                Camera_.Pitch = std::clamp(Camera_.Pitch + Delta.y * 0.3f, -89.0f, 89.0f);
                Camera_.Position = Pivot - Forward(Camera_.Yaw, Camera_.Pitch) * Camera_.Distance;
            }
            else if (Navigating_ && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                const glm::vec3 Right = RightOf(Camera_.Yaw);
                const glm::vec3 Up = glm::cross(Ahead, Right);
                const float Scale = Camera_.Distance * 0.0016f;
                Camera_.Position += (-Right * Delta.x + Up * Delta.y) * Scale;
            }
            if (Hovered && Io.MouseWheel != 0.0f)
            {
                // Dolly toward the pivot, slower when close.
                Camera_.Distance = std::clamp(Camera_.Distance * (1.0f - Io.MouseWheel * 0.12f), 1.0f, 15000.0f);
                Camera_.Position = Pivot - Ahead * Camera_.Distance;
            }
        }

        void SceneEditor::FrameSelection()
        {
            glm::vec3 Center(0.0f);
            float Radius = 10.0f;
            if (CameraSelected_)
            {
                Center = Document_.Env.CameraPosition;
            }
            else if (const scene::SceneNode* Node = Selected())
            {
                Center = Node->Position;
                if (const scene::CpuMesh* Mesh = Renderer_.Mesh(*Node))
                {
                    const glm::mat4 World = scene::NodeWorld(*Node);
                    const glm::vec3 Min = glm::vec3(World * glm::vec4(Mesh->Min, 1.0f));
                    const glm::vec3 Max = glm::vec3(World * glm::vec4(Mesh->Max, 1.0f));
                    Center = (Min + Max) * 0.5f;
                    Radius = std::max(glm::length(Max - Min) * 0.5f, 1.0f);
                }
            }
            else
            {
                return;
            }
            Camera_.Distance = std::max(Radius * 2.5f, 10.0f);
            Camera_.Position = Center - Forward(Camera_.Yaw, Camera_.Pitch) * Camera_.Distance;
        }

        std::vector<scene::LineVertex> SceneEditor::BuildLines() const
        {
            std::vector<scene::LineVertex> Lines;
            const glm::vec4 Minor(0.32f, 0.33f, 0.36f, 1.0f);
            const glm::vec4 Major(0.45f, 0.46f, 0.50f, 1.0f);
            constexpr int KHalf = 50;
            constexpr float KStep = 10.0f;
            for (int Index = -KHalf; Index <= KHalf; ++Index)
            {
                const float At = Index * KStep;
                const float End = KHalf * KStep;
                const glm::vec4 Color = Index % 10 == 0 ? Major : Minor;
                AddLine(Lines, {At, 0.0f, -End}, {At, 0.0f, End}, Index == 0 ? glm::vec4(0.3f, 0.45f, 0.9f, 1) : Color);
                AddLine(Lines, {-End, 0.0f, At}, {End, 0.0f, At}, Index == 0 ? glm::vec4(0.85f, 0.3f, 0.3f, 1) : Color);
            }

            const glm::vec4 Highlight(1.0f, 0.62f, 0.15f, 1.0f);
            const glm::vec4 LightColor(1.0f, 0.9f, 0.35f, 1.0f);
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                const bool IsSelected = Selection_ && Selection_->Space == scene::IdSpaceOf(Node.Kind) &&
                                        Selection_->Id == Node.Id;
                if (Node.Kind == scene::NodeKind::Light)
                {
                    const glm::vec3 Direction = scene::LightDirection(Node.Rotation);
                    if (Node.Light == scene::LightKind::Directional)
                    {
                        AddLine(Lines, Node.Position, Node.Position + Direction * 40.0f, LightColor);
                        continue;
                    }
                    if (!IsSelected)
                        continue;
                    const float Range = Node.Range.value_or(1000.0f);
                    glm::vec3 U, V;
                    if (Node.Light == scene::LightKind::Point)
                    {
                        AddCircle(Lines, Node.Position, {1, 0, 0}, {0, 0, 1}, Range, LightColor);
                        AddCircle(Lines, Node.Position, {1, 0, 0}, {0, 1, 0}, Range, LightColor);
                        AddCircle(Lines, Node.Position, {0, 0, 1}, {0, 1, 0}, Range, LightColor);
                        continue;
                    }
                    Basis(Direction, U, V);
                    const float Length = std::min(Range, 200.0f);
                    const float Spread = std::tan(glm::radians(Node.SpotOuter * 0.5f)) * Length;
                    const glm::vec3 End = Node.Position + Direction * Length;
                    AddCircle(Lines, End, U, V, Spread, LightColor);
                    for (const glm::vec3& Side : {U, -U, V, -V})
                        AddLine(Lines, Node.Position, End + Side * Spread, LightColor);
                    continue;
                }
                if (!IsSelected)
                    continue;
                const scene::CpuMesh* Mesh = Renderer_.Mesh(Node);
                if (Mesh == nullptr)
                    continue;
                const glm::mat4 World = scene::NodeWorld(Node);
                glm::vec3 Corner[8];
                for (int Index = 0; Index < 8; ++Index)
                    Corner[Index] = glm::vec3(World * glm::vec4((Index & 1) ? Mesh->Max.x : Mesh->Min.x,
                                                                (Index & 2) ? Mesh->Max.y : Mesh->Min.y,
                                                                (Index & 4) ? Mesh->Max.z : Mesh->Min.z, 1.0f));
                for (int Index = 0; Index < 8; ++Index)
                    for (const int Bit : {1, 2, 4})
                        if ((Index & Bit) == 0)
                            AddLine(Lines, Corner[Index], Corner[Index | Bit], Highlight);
            }

            const scene::Environment& Env = Document_.Env;
            if (Env.UseCamera && !LookThroughCamera_)
            {
                const glm::mat3 Rotation = scene::RotationMatrix(Env.CameraRotation);
                const glm::vec3 Ahead = Rotation * glm::vec3(0, 0, 1);
                const glm::vec3 Right = Rotation * glm::vec3(1, 0, 0);
                const glm::vec3 Up = Rotation * glm::vec3(0, 1, 0);
                const float Half = std::tan(glm::radians(Env.CameraFov.value_or(KCameraFov) * 0.5f)) * 30.0f;
                const glm::vec3 Eye = Env.CameraPosition;
                const glm::vec3 Far = Eye + Ahead * 30.0f;
                const glm::vec4 Color = CameraSelected_ ? Highlight : glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
                const glm::vec3 Corners[] = {Far + (Right + Up) * Half, Far + (Right - Up) * Half,
                                             Far + (-Right - Up) * Half, Far + (-Right + Up) * Half};
                for (int Index = 0; Index < 4; ++Index)
                {
                    AddLine(Lines, Eye, Corners[Index], Color);
                    AddLine(Lines, Corners[Index], Corners[(Index + 1) % 4], Color);
                }
            }
            return Lines;
        }

        void SceneEditor::DrawIcons(ImDrawList* Draw)
        {
            const ImVec2 Mouse = ImGui::GetMousePos();
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                ImVec2 At;
                if (Node.Kind != scene::NodeKind::Light || !ProjectToScreen(Node.Position, At))
                    continue;
                const bool IsSelected =
                    Selection_ && Selection_->Space == scene::IdSpace::Light && Selection_->Id == Node.Id;
                Draw->AddCircleFilled(At, KIconRadius,
                                      IsSelected ? IM_COL32(255, 160, 40, 255) : IM_COL32(255, 220, 80, 230));
                Draw->AddCircle(At, KIconRadius, IM_COL32(40, 30, 0, 255), 0, 1.5f);
                for (int Ray = 0; Ray < 8; ++Ray)
                {
                    const float Angle = glm::two_pi<float>() * Ray / 8.0f;
                    const ImVec2 Direction(std::cos(Angle), std::sin(Angle));
                    const auto Spoke = [&](float Length)
                    { return ImVec2(At.x + Direction.x * Length, At.y + Direction.y * Length); };
                    Draw->AddLine(Spoke(KIconRadius + 2.0f), Spoke(KIconRadius + 5.0f), IM_COL32(255, 220, 80, 200),
                                  1.5f);
                }
                const float Dx = Mouse.x - At.x, Dy = Mouse.y - At.y;
                if (Dx * Dx + Dy * Dy < KIconRadius * KIconRadius * 4.0f && ImGui::IsWindowHovered())
                    ImGui::SetTooltip("%s", Node.Name.c_str());
            }
            ImVec2 At;
            if (Document_.Env.UseCamera && !LookThroughCamera_ && ProjectToScreen(Document_.Env.CameraPosition, At))
            {
                const ImU32 Color = CameraSelected_ ? IM_COL32(255, 160, 40, 255) : IM_COL32(200, 200, 215, 240);
                Draw->AddRectFilled(ImVec2(At.x - 8, At.y - 5), ImVec2(At.x + 4, At.y + 5), Color, 2.0f);
                Draw->AddTriangleFilled(ImVec2(At.x + 4, At.y), ImVec2(At.x + 9, At.y - 5), ImVec2(At.x + 9, At.y + 5),
                                        Color);
            }
        }

        void SceneEditor::PickAt(const ImVec2& Mouse)
        {
            const auto Near = [&](const glm::vec3& Point)
            {
                ImVec2 At;
                if (!ProjectToScreen(Point, At))
                    return false;
                const float Dx = Mouse.x - At.x, Dy = Mouse.y - At.y;
                return Dx * Dx + Dy * Dy <= (KIconRadius + 4.0f) * (KIconRadius + 4.0f);
            };
            CameraSelected_ = false;
            for (const scene::SceneNode& Node : Document_.Nodes)
                if (Node.Kind == scene::NodeKind::Light && Near(Node.Position))
                {
                    Select(&Node);
                    return;
                }
            if (Document_.Env.UseCamera && !LookThroughCamera_ && Near(Document_.Env.CameraPosition))
            {
                Selection_.reset();
                CameraSelected_ = true;
                return;
            }

            const scene::Ray Ray = RayAt(Mouse);
            const scene::SceneNode* Hit = nullptr;
            float Nearest = 0.0f;
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                const scene::CpuMesh* Mesh = Node.Kind == scene::NodeKind::Light ? nullptr : Renderer_.Mesh(Node);
                if (Mesh == nullptr)
                    continue;
                const std::optional<float> Distance = scene::Intersect(Ray, *Mesh, scene::NodeWorld(Node));
                if (Distance && (Hit == nullptr || *Distance < Nearest))
                {
                    Hit = &Node;
                    Nearest = *Distance;
                }
            }
            Select(Hit);
        }

        void SceneEditor::DrawGizmo()
        {
            if (!Editable() || Tool_ == Tool::View)
                return;
            scene::SceneNode* Node = CameraSelected_ ? nullptr : Selected();
            if (Node == nullptr && !(CameraSelected_ && Document_.Env.UseCamera && !LookThroughCamera_))
                return;

            ImGuizmo::OPERATION Operation = ImGuizmo::TRANSLATE;
            if (Tool_ == Tool::Rotate)
                Operation = ImGuizmo::ROTATE;
            else if (Tool_ == Tool::Scale)
                Operation = ImGuizmo::SCALE;
            // Matrices and terrains only move; lights and the camera move and turn.
            const bool CanTurn = Node == nullptr || Node->Kind == scene::NodeKind::Light ||
                                 scene::IdSpaceOf(Node->Kind) == scene::IdSpace::Object;
            const bool CanScale = Node != nullptr && scene::IdSpaceOf(Node->Kind) == scene::IdSpace::Object;
            if ((Operation == ImGuizmo::ROTATE && !CanTurn) || (Operation == ImGuizmo::SCALE && !CanScale))
                Operation = ImGuizmo::TRANSLATE;

            scene::Environment& Env = Document_.Env;
            glm::mat4 Model = Node != nullptr ? GizmoMatrix(*Node)
                                              : glm::translate(glm::mat4(1.0f), Env.CameraPosition) *
                                                    glm::mat4(scene::RotationMatrix(Env.CameraRotation));
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(ViewportMin_.x, ViewportMin_.y, ViewportSize_.x, ViewportSize_.y);
            const ImGuizmo::MODE Mode =
                LocalGizmo_ || Operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            if (ImGuizmo::Manipulate(glm::value_ptr(View_.View), glm::value_ptr(View_.Projection), Operation, Mode,
                                     glm::value_ptr(Model)))
            {
                GizmoActive_ = true;
                const glm::vec3 Position(Model[3]);
                glm::vec3 Scale(glm::length(glm::vec3(Model[0])), glm::length(glm::vec3(Model[1])),
                                glm::length(glm::vec3(Model[2])));
                Scale = glm::max(Scale, glm::vec3(1e-4f));
                const glm::mat3 Rotation(glm::vec3(Model[0]) / Scale.x, glm::vec3(Model[1]) / Scale.y,
                                         glm::vec3(Model[2]) / Scale.z);
                if (Node == nullptr)
                {
                    Env.CameraPosition = Position;
                    if (Operation == ImGuizmo::ROTATE)
                        Env.CameraRotation = scene::EulerDegrees(Rotation);
                }
                else
                {
                    Node->Position = Position;
                    // Angles are only rewritten by the rotate tool, so moving keeps them exact.
                    if (Operation == ImGuizmo::ROTATE)
                        Node->Rotation = scene::EulerDegrees(Rotation);
                    if (Operation == ImGuizmo::SCALE)
                        Node->Scale = Scale * 100.0f;
                }
            }
            if (GizmoActive_ && !ImGuizmo::IsUsing())
            {
                GizmoActive_ = false;
                Commit();
            }
        }

        void SceneEditor::AcceptAssetDrop()
        {
            if (!Editable() || !ImGui::BeginDragDropTarget())
                return;
            if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(KAssetPayload))
            {
                const std::string Relative(static_cast<const char*>(Payload->Data));
                const std::optional<core::MediaKind> Kind = core::ClassifyMedia(Relative);
                const ImVec2 Mouse = ImGui::GetMousePos();
                if (Kind == core::MediaKind::Image)
                {
                    PickAt(Mouse);
                    if (scene::SceneNode* Node = Selected(); Node != nullptr && Node->Kind != scene::NodeKind::Light)
                    {
                        Node->Mat.Texture = Relative;
                        Commit();
                    }
                }
                else if (Kind == core::MediaKind::Model)
                {
                    // Dropped on the ground plane, or in front of the camera when looking up.
                    const scene::Ray Ray = RayAt(Mouse);
                    glm::vec3 At = Ray.Origin + Ray.Direction * Camera_.Distance;
                    if (Ray.Direction.y < -0.001f)
                        At = Ray.Origin + Ray.Direction * (-Ray.Origin.y / Ray.Direction.y);
                    scene::SceneNode& Node = Document_.Add(scene::NodeKind::Model);
                    Node.Asset = Relative;
                    Node.Name = std::filesystem::path(Relative).stem().string();
                    Node.Position = At;
                    Select(&Node);
                    Commit();
                }
            }
            ImGui::EndDragDropTarget();
        }

        void SceneEditor::DrawViewportBar()
        {
            const auto ToolButton = [&](const char* Label, Tool Value, const char* Tip)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Tool_ == Value ? Theme::Selection : 0);
                if (ImGui::Button(Label))
                    Tool_ = Value;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tip);
                ImGui::SameLine();
            };
            // The window has no padding, so the bar adds its own below the tab.
            const ImVec2 Spacing = ImGui::GetStyle().ItemSpacing;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + Spacing.x, ImGui::GetCursorPosY() + Spacing.y));
            ToolButton("View", Tool::View, "Navigate only (Q)");
            ToolButton("Move", Tool::Move, "Move (W)");
            ToolButton("Rotate", Tool::Rotate, "Rotate (E)");
            ToolButton("Scale", Tool::Scale, "Scale (R)");
            ImGui::Checkbox("Local", &LocalGizmo_);
            ImGui::SameLine();
            ImGui::BeginDisabled(!Document_.Env.UseCamera);
            ImGui::Checkbox("Level camera view", &LookThroughCamera_);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Document_.Env.UseCamera ? "See the level through its camera"
                                                                : "Enable the level camera in Level Settings");
            if (!Document_.Env.UseCamera)
                LookThroughCamera_ = false;
        }

        void SceneEditor::DrawViewport()
        {
            ImGuizmo::BeginFrame();
            if (!HasLevel_)
                return;
            if (!RendererReady_ && Gpu_ != nullptr)
            {
                std::string Error;
                RendererReady_ = Renderer_.Initialize(Gpu_, Error);
                if (!RendererReady_)
                {
                    Host_.Log("The Scene viewport cannot render: " + Error, true);
                    Renderer_.Shutdown();
                    Gpu_ = nullptr;
                }
            }

            DrawViewportBar();
            const ImVec2 Start = ImGui::GetCursorScreenPos();
            const ImVec2 Available = ImGui::GetContentRegionAvail();
            ViewportMin_ = Start;
            ViewportSize_ = ImVec2(std::max(Available.x, 1.0f), std::max(Available.y, 1.0f));
            const ImVec2 Scale = ImGui::GetIO().DisplayFramebufferScale;
            SDL_GPUTexture* Texture =
                RendererReady_ ? Renderer_.Target(static_cast<int>(ViewportSize_.x * Scale.x),
                                                  static_cast<int>(ViewportSize_.y * Scale.y))
                               : nullptr;
            View_ = ComputeView(ViewportSize_.x / ViewportSize_.y);
            if (Texture == nullptr)
            {
                ImGui::Dummy(ViewportSize_);
                return;
            }
            Renderer_.Prepare(Document_, View_, Project_->AssetRoot, BuildLines());
            for (const std::string& Error : Renderer_.TakeErrors())
                Host_.Log(Error, true);

            ImGui::Image(reinterpret_cast<ImTextureID>(Texture), ViewportSize_);
            const bool Hovered = ImGui::IsItemHovered();
            AcceptAssetDrop();

            const ImGuiIO& Io = ImGui::GetIO();
            UpdateCamera(Hovered);
            ImDrawList* Draw = ImGui::GetWindowDrawList();
            DrawIcons(Draw);
            DrawGizmo();

            if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Io.KeyAlt && !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing())
                PickAt(ImGui::GetMousePos());

            // Tool keys apply while the viewport has focus and no button is navigating.
            if (ImGui::IsWindowFocused() && !Navigating_ && !Io.WantTextInput && !Io.KeyCtrl && !Io.KeySuper)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
                    Tool_ = Tool::View;
                else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                    Tool_ = Tool::Move;
                else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                    Tool_ = Tool::Rotate;
                else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                    Tool_ = Tool::Scale;
                else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                    FrameSelection();
            }

            if (!ReadErrors_.empty())
                Draw->AddText(ImVec2(Start.x + 8.0f, Start.y + 8.0f), IM_COL32(255, 120, 100, 255),
                              "Read-only level: see the Hierarchy panel");
        }
    } // namespace panels

} // namespace dbc::studio
