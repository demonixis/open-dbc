// The Scene view's Inspector and Level Settings panels. Widgets edit the document
// in place; SceneEditor::CommitIfIdle() records an undo step once no widget is held.

#include "panels/scene_editor.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cstring>

#include "core/i18n.hpp"
#include "core/media_catalogue.hpp"
#include "scene/heightmap.hpp"
#include "scene/scene_geometry.hpp"
#include "theme.hpp"

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            bool Pro(const scene::SceneDocument& Document)
            {
                return Document.Profile == dbc::language::LanguageProfile::DbPro;
            }

            bool EditColor(const char* Label, scene::Rgb& Value)
            {
                float Channels[3] = {((Value >> 16) & 0xFF) / 255.0f, ((Value >> 8) & 0xFF) / 255.0f,
                                     (Value & 0xFF) / 255.0f};
                if (!ImGui::ColorEdit3(Label, Channels, ImGuiColorEditFlags_NoInputs))
                    return false;
                const auto Byte = [](float Channel)
                { return static_cast<scene::Rgb>(std::clamp(Channel, 0.0f, 1.0f) * 255.0f + 0.5f); };
                Value = (Byte(Channels[0]) << 16) | (Byte(Channels[1]) << 8) | Byte(Channels[2]);
                return true;
            }

            // A setting the level may leave to the engine: unchecked writes no statement.
            template <typename T, typename Edit>
            void Optional(const char* Label, std::optional<T>& Value, const T& Default, Edit&& Editor)
            {
                ImGui::PushID(Label);
                bool Enabled = Value.has_value();
                if (ImGui::Checkbox("##set", &Enabled))
                    Value = Enabled ? std::optional<T>(Default) : std::nullopt;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tr("Unchecked: the engine default, and no statement"));
                ImGui::SameLine();
                if (Value)
                {
                    Editor(*Value);
                }
                else
                {
                    ImGui::BeginDisabled();
                    T Shown = Default;
                    Editor(Shown);
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }

            void OptionalColor(const char* Label, std::optional<scene::Rgb>& Value, scene::Rgb Default)
            {
                Optional(Label, Value, Default, [&](scene::Rgb& Color) { EditColor(TrId(Label), Color); });
            }

            void OptionalFloat(const char* Label, std::optional<float>& Value, float Default, float Speed, float Min,
                               float Max)
            {
                Optional(Label, Value, Default,
                         [&](float& Number) { ImGui::DragFloat(TrId(Label), &Number, Speed, Min, Max, "%.2f"); });
            }

            // A path field that also takes a file dragged from the Assets panel.
            bool AssetField(const char* Label, std::string& Value, std::optional<core::MediaKind> Kind)
            {
                bool Changed = false;
                char Buffer[512];
                std::strncpy(Buffer, Value.c_str(), sizeof(Buffer) - 1);
                Buffer[sizeof(Buffer) - 1] = '\0';
                ImGui::PushID(Label);
                ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::GetFrameHeight() -
                                        ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::InputText("##path", Buffer, sizeof(Buffer)))
                {
                    Value = Buffer;
                    Changed = true;
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(SceneEditor::KAssetPayload))
                    {
                        const std::string Dropped(static_cast<const char*>(Payload->Data));
                        if (!Kind || core::ClassifyMedia(Dropped) == Kind)
                        {
                            Value = Dropped;
                            Changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                {
                    Value.clear();
                    Changed = true;
                }
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(Tr(Label));
                ImGui::PopID();
                return Changed;
            }

            void Heading(const char* Label)
            {
                ImGui::Spacing();
                ImGui::SeparatorText(Tr(Label));
            }
        } // namespace

        void SceneEditor::CommitIfIdle()
        {
            if (TransformEditable() && !GizmoActive_ && !VertexDragging_ && !Painting_ && !ImGui::IsAnyItemActive())
                Commit();
        }

        void SceneEditor::DrawNodeOrigin(const scene::SceneNode& Node)
        {
            if (!Program_)
                return;
            const scene::NodeOrigin* Origin = OriginOf(Node);
            if (Origin == nullptr || Origin->Creator.File.empty())
            {
                ImGui::TextDisabled("%s", Tr("No statement of the program creates it by this number."));
                return;
            }
            ImGui::TextDisabled("%s:%u", dbc::PathToUtf8(dbc::PathFromUtf8(Origin->Creator.File).filename()).c_str(),
                                Origin->Creator.Line);
            ImGui::SameLine();
            if (ImGui::SmallButton(TrId("Show in Code")) && Host_.ShowSource)
                Host_.ShowSource(dbc::PathFromUtf8(Origin->Creator.File), Origin->Creator.Line);
        }

        void SceneEditor::DrawAxisLocks(const scene::SceneNode& Node, scene::TransformPart Part)
        {
            const scene::NodeOrigin* Origin = Program_ ? OriginOf(Node) : nullptr;
            if (Origin == nullptr)
                return;
            constexpr std::array<const char*, 3> Names{"X", "Y", "Z"};
            const auto& Axes = Origin->Axes[static_cast<std::size_t>(Part)];

            struct Group
            {
                std::string Labels;
                const scene::AxisOrigin* Axis = nullptr;
            };

            std::vector<Group> Groups;
            for (std::size_t Index = 0; Index < Axes.size(); ++Index)
            {
                const scene::AxisOrigin& Axis = Axes[Index];
                if (Axis.Access != scene::AxisAccess::Locked)
                    continue;
                const auto Same =
                    std::find_if(Groups.begin(), Groups.end(), [&](const Group& Value)
                                 { return Value.Axis->Reason == Axis.Reason && Value.Axis->Writer == Axis.Writer; });
                if (Same == Groups.end())
                    Groups.push_back({Names[Index], &Axis});
                else
                    Same->Labels += std::string(", ") + Names[Index];
            }
            ImGui::PushID(static_cast<int>(Part));
            for (const Group& Value : Groups)
            {
                ImGui::PushID(Value.Labels.c_str());
                ImGui::TextDisabled("%s", TrFmt("{0} locked: {1}", {Value.Labels, Value.Axis->Reason}).c_str());
                if (Value.Axis->Writer.Line != 0)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TrId("Show")) && Host_.ShowSource)
                        Host_.ShowSource(dbc::PathFromUtf8(Value.Axis->Writer.File), Value.Axis->Writer.Line);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        void SceneEditor::DrawInspector()
        {
            if (!HasLevel_)
                return;
            if (const scene::SceneNode* Shown = CameraSelected_ ? nullptr : Selected())
                DrawNodeOrigin(*Shown);
            ImGui::BeginDisabled(!Editable());
            if (CameraSelected_)
            {
                DrawCameraSettings();
                ImGui::EndDisabled();
                return;
            }
            scene::SceneNode* Node = Selected();
            if (Node == nullptr)
            {
                ImGui::EndDisabled();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", Tr("Select a node in the Hierarchy or the Viewport."));
                ImGui::PopTextWrapPos();
                return;
            }

            const bool IsObject = scene::IdSpaceOf(Node->Kind) == scene::IdSpace::Object;
            const bool IsLight = Node->Kind == scene::NodeKind::Light;
            const bool IsMarker = scene::IsMarker(Node->Kind);
            // Several nodes: the widgets show the active node, and what they change is
            // copied to the others below, where it applies.
            const std::vector<scene::SceneNode*> Nodes = SelectedNodes();
            const bool Multi = Nodes.size() > 1;
            const scene::SceneNode Before = *Node;
            // Shape fields are shared by nodes of one kind; matrix and terrain height tools stay per node.
            const bool SharedShape =
                !Multi || (Node->Kind != scene::NodeKind::Matrix && Node->Kind != scene::NodeKind::Terrain &&
                           std::all_of(Nodes.begin(), Nodes.end(),
                                       [&](const scene::SceneNode* Other) { return Other->Kind == Node->Kind; }));
            if (Multi)
            {
                ImGui::TextUnformatted(TrFmt("{0} nodes selected", {std::to_string(Nodes.size())}).c_str());
                ImGui::TextDisabled(
                    "%s", TrFmt("Showing {0}; edits apply to each node that has the field.", {Node->Name}).c_str());
            }
            else
            {
                ImGui::TextDisabled("%s", Tr(scene::NodeKindLabel(Node->Kind)));
                char Name[128];
                std::strncpy(Name, Node->Name.c_str(), sizeof(Name) - 1);
                Name[sizeof(Name) - 1] = '\0';
                if (ImGui::InputText(TrId("Name"), Name, sizeof(Name)))
                    Node->Name = Name;
                int Id = Node->Id;
                if (ImGui::InputInt(TrId("Number"), &Id, 0) && Id > 0 &&
                    Document_.Find(scene::IdSpaceOf(Node->Kind), Id) == nullptr)
                {
                    Node->Id = Id;
                    Select(Node);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tr("The object, matrix, terrain, light or sound number the code uses"));
            }
            if (IsObject)
            {
                const bool Available = scene::StaticAvailable(Document_.Profile);
                ImGui::BeginDisabled(!Available);
                // A static object no longer exists to animate.
                if (ImGui::Checkbox(TrId("Make Static Object"), &Node->Static) && Node->Static)
                {
                    Node->Frames.reset();
                    Node->Speed.reset();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s",
                                      Tr(Available ? "MAKE STATIC OBJECT, then DELETE OBJECT: fixed scenery for the "
                                                     "static collision commands; its number is free at run time"
                                                   : "MAKE STATIC OBJECT is not implemented in the Pro profile"));
            }

            Heading("Transform");
            // The one section a program's scene edits: each part has its own state, not the Inspector's.
            ImGui::EndDisabled();
            const auto BeginPart = [&](scene::TransformPart Part)
            { ImGui::BeginDisabled(!TransformEditable() || !PartEditable(*Node, Part)); };
            const auto EndPart = [&](scene::TransformPart Part)
            {
                ImGui::EndDisabled();
                DrawAxisLocks(*Node, Part);
            };
            BeginPart(scene::TransformPart::Position);
            ImGui::DragFloat3(TrId("Position"), &Node->Position.x, 0.5f);
            EndPart(scene::TransformPart::Position);
            const bool Turns = IsObject || (IsLight && Node->Light != scene::LightKind::Point);
            if (Turns)
            {
                BeginPart(scene::TransformPart::Rotation);
                ImGui::DragFloat3(TrId("Rotation"), &Node->Rotation.x, 0.5f);
                EndPart(scene::TransformPart::Rotation);
            }
            if (IsObject)
            {
                BeginPart(scene::TransformPart::Scale);
                const glm::vec3 PreviousScale = Node->Scale;
                if (ImGui::DragFloat3(TrId("Scale %"), &Node->Scale.x, 0.5f, 0.0f, 100000.0f) && UniformScale_)
                    Node->Scale = scene::ProportionalScale(PreviousScale, Node->Scale);
                ImGui::Checkbox(TrId("Uniform scale"), &UniformScale_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s",
                                      Tr("Scaling one axis scales the others in proportion, here and with the gizmo"));
                EndPart(scene::TransformPart::Scale);
            }
            ImGui::BeginDisabled(!Editable());
            if (Node->Kind == scene::NodeKind::Terrain)
                ImGui::TextDisabled("%s", Tr("A terrain runs from its position toward -Z."));
            if (Node->Kind == scene::NodeKind::Matrix)
                ImGui::TextDisabled("%s", Tr("A matrix runs from its position toward +X and +Z."));

            if (SharedShape)
                DrawShape(*Node);

            if (!IsMarker)
                DrawMaterial(*Node);
            if (Multi && *Node != Before)
                for (scene::SceneNode* Other : Nodes)
                    if (Other != Node)
                        scene::ApplySharedEdit(Before, *Node, *Other);
            ImGui::EndDisabled();
        }

        void SceneEditor::DrawShape(scene::SceneNode& Node)
        {
            Heading(Node.Kind == scene::NodeKind::Sound ? "Sound" : "Shape");
            switch (Node.Kind)
            {
                case scene::NodeKind::Cube:
                    ImGui::DragFloat(TrId("Size"), &Node.Size, 0.25f, 0.01f, 100000.0f);
                    break;
                case scene::NodeKind::Box:
                    ImGui::DragFloat3(TrId("Width, height, depth"), &Node.Extent.x, 0.25f, 0.01f, 100000.0f);
                    break;
                case scene::NodeKind::Sphere:
                    ImGui::DragFloat(TrId("Diameter"), &Node.Size, 0.25f, 0.01f, 100000.0f);
                    if (Pro(Document_))
                        Optional("Rings, segments", Node.Detail, glm::ivec2(12, 16),
                                 [](glm::ivec2& Detail)
                                 {
                                     ImGui::DragInt2(TrId("Rings, segments"), &Detail.x, 0.25f, 3, 100);
                                     Detail = glm::max(Detail, glm::ivec2(3));
                                 });
                    break;
                case scene::NodeKind::Cylinder:
                case scene::NodeKind::Cone:
                    ImGui::DragFloat(TrId("Size"), &Node.Size, 0.25f, 0.01f, 100000.0f);
                    break;
                case scene::NodeKind::Plain:
                    ImGui::DragFloat2(TrId("Width, height"), &Node.Extent.x, 0.25f, 0.01f, 100000.0f);
                    if (Pro(Document_))
                        Optional("Orientation", Node.PlainOrientation, 0,
                                 [](int& Orientation) { ImGui::InputInt(TrId("Orientation"), &Orientation); });
                    break;
                case scene::NodeKind::Triangle:
                    ImGui::DragFloat3(TrId("Corner 1"), &Node.Corners[0].x, 0.25f);
                    ImGui::DragFloat3(TrId("Corner 2"), &Node.Corners[1].x, 0.25f);
                    ImGui::DragFloat3(TrId("Corner 3"), &Node.Corners[2].x, 0.25f);
                    break;
                case scene::NodeKind::Model:
                    AssetField("Model file", Node.Asset, core::MediaKind::Model);
                    DrawAnimation(Node);
                    break;
                case scene::NodeKind::Terrain:
                    AssetField("Heightmap", Node.Asset, core::MediaKind::Image);
                    if (Node.Asset.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s",
                                           Tr("A terrain needs a square heightmap image."));
                    DrawTerrainHeightmap(Node);
                    break;
                case scene::NodeKind::Matrix:
                    ImGui::DragFloat(TrId("Width"), &Node.Width, 1.0f, 0.01f, 100000.0f);
                    ImGui::DragFloat(TrId("Depth"), &Node.Depth, 1.0f, 0.01f, 100000.0f);
                    ImGui::DragInt(TrId("Tiles across"), &Node.XSegments, 0.2f, 1, 256);
                    ImGui::DragInt(TrId("Tiles down"), &Node.ZSegments, 0.2f, 1, 256);
                    DrawMatrixHeights(Node);
                    break;
                case scene::NodeKind::Light:
                {
                    int Kind = static_cast<int>(Node.Light);
                    const char* const Types[] = {Tr("Point"), Tr("Spot"), Tr("Directional")};
                    if (ImGui::Combo(TrId("Type"), &Kind, Types, 3))
                        Node.Light = static_cast<scene::LightKind>(Kind);
                    if (Node.Light != scene::LightKind::Directional)
                        OptionalFloat("Range", Node.Range, 1000.0f, 1.0f, 0.0f, 100000.0f);
                    if (Node.Light == scene::LightKind::Spot)
                    {
                        ImGui::DragFloat(TrId("Inner cone"), &Node.SpotInner, 0.25f, 0.0f, 180.0f);
                        ImGui::DragFloat(TrId("Outer cone"), &Node.SpotOuter, 0.25f, 0.0f, 180.0f);
                    }
                    OptionalColor("Colour", Node.LightColor, 0xFFFFFF);
                    break;
                }
                case scene::NodeKind::Sound:
                    AssetField("Sound file", Node.Asset, core::MediaKind::Audio);
                    if (Node.Asset.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Tr("A 3D sound needs a sound file."));
                    ImGui::Checkbox(TrId("Play on load"), &Node.PlayOnLoad);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s",
                                          Tr("Unchecked, the sound waits for the program's PLAY SOUND or LOOP SOUND"));
                    if (Node.PlayOnLoad)
                    {
                        ImGui::SameLine();
                        ImGui::Checkbox(TrId("Loop"), &Node.Loop);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", Tr("LOOP SOUND repeats it; unchecked, PLAY SOUND plays it once"));
                    }
                    Optional("Volume %", Node.Volume, 100,
                             [](int& Volume) { ImGui::SliderInt(TrId("Volume %"), &Volume, 0, 100); });
                    ImGui::TextDisabled("%s",
                                        Tr("The program hears it from POSITION LISTENER, at 0, 0, 0 until moved."));
                    break;
            }
        }

        const scene::ModelAnimation* SceneEditor::ModelAnimationOf(const std::string& Asset)
        {
            if (!Project_ || Asset.empty())
                return nullptr;
            auto Found = ModelAnimations_.find(Asset);
            if (Found == ModelAnimations_.end())
            {
                std::optional<scene::ModelAnimation> Read(std::in_place);
                std::string Error;
                if (!scene::ReadModelAnimation(Project_->AssetRoot / dbc::PathFromUtf8(Asset), *Read, Error))
                    Read.reset();
                Found = ModelAnimations_.emplace(Asset, std::move(Read)).first;
            }
            return Found->second ? &*Found->second : nullptr;
        }

        void SceneEditor::DrawAnimation(scene::SceneNode& Node)
        {
            Heading("Animation");
            if (Node.Static)
            {
                ImGui::TextDisabled("%s", Tr("A static object does not animate."));
                return;
            }
            const scene::ModelAnimation* Info = ModelAnimationOf(Node.Asset);
            const bool HasClip = Info != nullptr && Info->Count > 0;
            if (!HasClip && !Node.Frames)
            {
                ImGui::TextDisabled("%s", Tr(Node.Asset.empty() ? "Pick a model file first."
                                             : Info == nullptr  ? "The model file cannot be read."
                                                                : "This model has no animation."));
                return;
            }

            // LOAD OBJECT keeps one timeline: an animation is a range of its frames.
            const int Last = HasClip ? Info->LastFrame : std::max(0, Node.Frames->y);
            const std::string Clip = TrFmt(
                "{0} (frames 0 to {1})",
                {HasClip && !Info->Name.empty() ? Info->Name : std::string(Tr("Animation")), std::to_string(Last)});
            const char* Items[] = {Tr("None"), Clip.c_str()};
            int Choice = Node.Frames ? 1 : 0;
            if (ImGui::Combo(TrId("Animation"), &Choice, Items, 2))
                Node.Frames = Choice == 0 ? std::nullopt : std::optional<glm::ivec2>(glm::ivec2(0, Last));
            if (HasClip && Info->Count > 1)
                ImGui::TextDisabled("%s", TrFmt("The file holds {0} animations; LOAD OBJECT plays the first.",
                                                {std::to_string(Info->Count)})
                                              .c_str());
            if (!Node.Frames)
                return;

            ImGui::Checkbox(TrId("Loop"), &Node.Loop);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("LOOP OBJECT repeats the frames; unchecked, PLAY OBJECT plays them once"));
            glm::ivec2& Frames = *Node.Frames;
            ImGui::DragIntRange2(TrId("Frames"), &Frames.x, &Frames.y, 0.25f, 0, Last,
                                 TrFmt("First {0}", {"%d"}).c_str(), TrFmt("Last {0}", {"%d"}).c_str());
            Frames.x = std::clamp(Frames.x, 0, Last);
            Frames.y = std::clamp(Frames.y, Frames.x, Last);
            Optional("Speed", Node.Speed, 20, [](int& Speed) { ImGui::SliderInt(TrId("Speed"), &Speed, 1, 200); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("SET OBJECT SPEED; 20 plays the file at its own rate"));
            ImGui::TextDisabled("%s", Tr("The viewport shows the model still."));
        }

        void SceneEditor::DrawMatrixHeights(scene::SceneNode& Node)
        {
            Heading("Heights");
            if (!Node.Heights.empty() && scene::MatrixHeights(Node).empty())
            {
                // The tile count changed: the old heights no longer fit.
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error), "%s",
                                   Tr("The heights no longer fit this matrix."));
                if (ImGui::Button(TrId("Flatten")))
                    Node.Heights.clear();
            }

            const int TileCount =
                Node.Mat.Texture.empty() ? 1 : std::max(1, Node.TilesAcross) * std::max(1, Node.TilesDown);
            ImGui::DragFloat(TrId("Fill height"), &FillHeight_, 0.25f);
            ImGui::BeginDisabled(TileCount == 1);
            ImGui::SliderInt(TrId("Fill tile"), &FillTile_, 1, std::max(2, TileCount));
            ImGui::EndDisabled();
            FillTile_ = std::clamp(FillTile_, 1, TileCount);
            if (ImGui::Button(TrId("Fill")))
            {
                Node.Heights.assign(scene::MatrixVertexCount(Node), FillHeight_);
                Node.Tile = FillTile_;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("FILL MATRIX: every height to Fill height, every tile to Fill tile"));

            ImGui::DragFloat(TrId("Height range"), &HeightRange_, 0.25f, 0.0f, 100000.0f);
            if (ImGui::Button(TrId("Randomize")))
            {
                // RANDOMIZE MATRIX draws whole heights from 0 to the range, excluded.
                const int Maximum = static_cast<int>(HeightRange_);
                std::uniform_int_distribution<int> Distribution(0, std::max(0, Maximum - 1));
                Node.Heights.resize(scene::MatrixVertexCount(Node));
                for (float& Height : Node.Heights)
                    Height = static_cast<float>(Distribution(Random_));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("RANDOMIZE MATRIX: random whole heights below the range"));
            ImGui::TextDisabled("%s", Tr("The heights are saved in the level, so every run shows this matrix."));
        }

        void SceneEditor::DrawTerrainHeightmap(scene::SceneNode& Node)
        {
            Heading("Generate heightmap");
            static constexpr int KSizes[] = {64, 128, 256, 512};
            const std::string Current = std::to_string(HeightmapSize_) + " x " + std::to_string(HeightmapSize_);
            if (ImGui::BeginCombo(TrId("Size"), Current.c_str()))
            {
                for (const int Size : KSizes)
                    if (ImGui::Selectable((std::to_string(Size) + " x " + std::to_string(Size)).c_str(),
                                          Size == HeightmapSize_))
                        HeightmapSize_ = Size;
                ImGui::EndCombo();
            }

            // Terrain heights are an image's grey levels, 0 to KTerrainHeightScale.
            std::optional<scene::Heightmap> Generated;
            ImGui::DragFloat(TrId("Fill height"), &TerrainFillHeight_, 0.25f, 0.0f, scene::KTerrainHeightScale);
            if (ImGui::Button(TrId("Fill##terrain")))
                Generated = scene::FlatHeightmap(HeightmapSize_, TerrainFillHeight_);
            ImGui::DragFloat(TrId("Height range"), &TerrainRange_, 0.25f, 0.0f, scene::KTerrainHeightScale);
            ImGui::DragInt(TrId("Hills across"), &TerrainHills_, 0.1f, 1, 32);
            if (ImGui::Button(TrId("Randomize##terrain")))
                Generated = scene::RandomHeightmap(HeightmapSize_, TerrainRange_, TerrainHills_, Random_);

            const std::filesystem::path Relative = scene::TerrainHeightmapPath(Document_.Name, Node.Id);
            if (Generated && Project_)
            {
                // Always the level's own file: a heightmap the user picked is never overwritten.
                if (scene::WriteHeightmap(Project_->AssetRoot / Relative, HeightmapSize_, *Generated, TerrainError_))
                {
                    TerrainError_.clear();
                    Node.Asset = dbc::GenericPathToUtf8(Relative);
                    RefreshAssets();
                }
            }
            if (!TerrainError_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error), "%s", TerrainError_.c_str());
            ImGui::TextDisabled("%s", TrFmt("Writes {0}", {dbc::GenericPathToUtf8(Relative)}).c_str());
        }

        void SceneEditor::DrawMaterial(scene::SceneNode& Node)
        {
            scene::Material& Mat = Node.Mat;
            Heading("Material");
            AssetField("Texture", Mat.Texture, core::MediaKind::Image);
            if (Node.Kind == scene::NodeKind::Matrix && !Mat.Texture.empty())
            {
                ImGui::DragInt(TrId("Image tiles across"), &Node.TilesAcross, 0.1f, 1, 64);
                ImGui::DragInt(TrId("Image tiles down"), &Node.TilesDown, 0.1f, 1, 64);
            }
            if (scene::IdSpaceOf(Node.Kind) != scene::IdSpace::Object)
                return;

            OptionalColor("Colour", Mat.Color, 0xFFFFFF);
            ImGui::Checkbox(TrId("Ghost"), &Mat.Ghost);
            if (Mat.Ghost)
            {
                ImGui::SameLine();
                Optional("Ghost mode", Mat.GhostMode, 0,
                         [](int& Mode) { ImGui::SliderInt(TrId("Ghost mode"), &Mode, 0, 5); });
            }

            scene::ObjectFlags& Flags = Mat.Flags;
            ImGui::Checkbox(TrId("Wireframe"), &Flags.Wireframe);
            if (Pro(Document_))
            {
                ImGui::SliderInt(TrId("Transparency"), &Flags.Transparency, 0, 7);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Tr("SET OBJECT transparency mode; 0 draws black as black"));
            }
            else
            {
                bool Transparent = Flags.Transparency != scene::OpaqueTransparency(Document_.Profile);
                if (ImGui::Checkbox(TrId("Black is transparent"), &Transparent))
                    Flags.Transparency = Transparent ? 0 : scene::OpaqueTransparency(Document_.Profile);
            }
            ImGui::Checkbox(TrId("Cull back faces"), &Flags.Cull);
            ImGui::SameLine();
            ImGui::Checkbox(TrId("Filter"), &Flags.Filter);
            ImGui::Checkbox(TrId("Lighting"), &Flags.Lighting);
            ImGui::SameLine();
            ImGui::Checkbox(TrId("Fog"), &Flags.Fog);
            ImGui::SameLine();
            ImGui::Checkbox(TrId("Ambient"), &Flags.Ambient);

            if (!Pro(Document_))
                return;
            OptionalColor("Diffuse", Mat.Diffuse, 0xFFFFFF);
            OptionalColor("Ambience", Mat.Ambience, 0xFFFFFF);
            OptionalColor("Emissive", Mat.Emissive, 0x000000);
            OptionalColor("Specular", Mat.Specular, 0xFFFFFF);
            OptionalFloat("Specular power", Mat.SpecularPower, 10.0f, 0.25f, 0.0f, 255.0f);
            // Without SET SHADOW POSITION the engine casts from light 0 at the origin, which
            // lies on the ground of most levels; the first caster gets a fixed source above.
            if (ImGui::Checkbox(TrId("Casts a shadow"), &Mat.CastShadow) && Mat.CastShadow && !Document_.Env.ShadowMode)
                Document_.Env.ShadowMode = -1;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Tr("SET SHADOW SHADING ON, cast from the shadow source in Level Settings"));
        }

        void SceneEditor::DrawCameraSettings()
        {
            scene::Environment& Env = Document_.Env;
            ImGui::Checkbox(TrId("Use a level camera"), &Env.UseCamera);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "%s", Tr("Writes AUTOCAM OFF and places camera 0; unchecked, the program keeps its camera"));
            if (!Env.UseCamera)
                return;
            ImGui::DragFloat3(TrId("Camera position"), &Env.CameraPosition.x, 0.5f);
            ImGui::DragFloat3(TrId("Camera rotation"), &Env.CameraRotation.x, 0.5f);
            OptionalFloat("Field of view", Env.CameraFov, 75.0f, 0.25f, 1.0f, 179.0f);
            Optional("Near, far", Env.CameraRange, glm::vec2(0.1f, 5000.0f),
                     [](glm::vec2& Range) { ImGui::DragFloat2(TrId("Near, far"), &Range.x, 0.5f, 0.01f, 1000000.0f); });
            if (ImGui::Button(TrId("Match the viewport")))
            {
                Env.CameraPosition = Camera_.Position;
                Env.CameraRotation = glm::vec3(Camera_.Pitch, Camera_.Yaw, 0.0f);
            }
        }

        void SceneEditor::DrawSettings()
        {
            if (!HasLevel_)
                return;
            ImGui::BeginDisabled(!Editable());
            scene::Environment& Env = Document_.Env;
            if (ImGui::CollapsingHeader(TrId("Level"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("%s", dbc::GenericPathToUtf8(scene::LevelRelativePath(Document_.Name)).c_str());
                ImGui::TextDisabled("%s", TrFmt("Function {0}()", {scene::LevelFunctionName(Document_)}).c_str());

                Heading("Numbers");
                static const char* const Labels[] = {"First object",  "First image", "First matrix",
                                                     "First terrain", "First light", "First sound"};
                for (std::size_t Space = 0; Space < scene::KIdSpaceCount; ++Space)
                {
                    int Base = Document_.IdBase[Space];
                    if (ImGui::InputInt(TrId(Labels[Space]), &Base) && Base > 0)
                        Document_.IdBase[Space] = Base;
                }
                ImGui::TextDisabled("%s", Tr("New nodes take free numbers from these, away from the program's own."));
            }

            if (ImGui::CollapsingHeader(TrId("World Settings"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("%s", dbc::GenericPathToUtf8(scene::WorldRelativePath(Document_.Name)).c_str());
                ImGui::TextDisabled("%s", TrFmt("Function {0}()", {scene::WorldFunctionName(Document_)}).c_str());

                Heading("Light and colour");
                Optional("Ambient %", Env.AmbientPercent, 25,
                         [](int& Percent) { ImGui::SliderInt(TrId("Ambient %"), &Percent, 0, 100); });
                OptionalColor("Ambient colour", Env.AmbientColor, 0xFFFFFF);
                OptionalColor("Backdrop", Env.Backdrop, 0x000000);
                ImGui::Checkbox(TrId("Hide light 0"), &Env.DefaultLightHidden);
                if (!Env.DefaultLightHidden)
                {
                    Optional("Light 0 rotation", Env.DefaultLightRotation, glm::vec3(45.0f, 0.0f, 0.0f),
                             [](glm::vec3& Rotation)
                             { ImGui::DragFloat3(TrId("Light 0 rotation"), &Rotation.x, 0.5f); });
                    OptionalColor("Light 0 colour", Env.DefaultLightColor, 0xFFFFFF);
                }

                Heading("Fog");
                ImGui::Checkbox(TrId("Fog"), &Env.Fog);
                if (Env.Fog)
                {
                    OptionalColor("Fog colour", Env.FogColor, 0x000000);
                    OptionalFloat("Fog distance", Env.FogDistance, 3000.0f, 5.0f, 0.0f, 1000000.0f);
                }

                if (Pro(Document_))
                {
                    Heading("Shadows");
                    bool Shadows = Env.ShadowMode.has_value();
                    if (ImGui::Checkbox(TrId("Shadow source"), &Shadows))
                        Env.ShadowMode = Shadows ? std::optional<int>(-1) : std::nullopt;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "%s", Tr("SET SHADOW POSITION; objects cast with Casts a shadow in their material"));
                    if (Env.ShadowMode)
                    {
                        // Light 0 and missing lights sit at the origin, on the ground of most
                        // levels, where every volume lies along the floor; the labels say so.
                        const auto Label = [&](int Mode)
                        {
                            if (Mode == -1)
                                return std::string(Tr("Fixed position"));
                            const scene::SceneNode* Light =
                                Mode > 0 ? Document_.Find(scene::IdSpace::Light, Mode) : nullptr;
                            if (Light != nullptr)
                                return TrFmt("Light {0}: {1}", {std::to_string(Mode), Light->Name});
                            return TrFmt("Light {0} (at the origin)", {std::to_string(Mode)});
                        };
                        if (ImGui::BeginCombo(TrId("Cast from"), Label(*Env.ShadowMode).c_str()))
                        {
                            std::vector<int> Modes = {-1, 0};
                            for (const scene::SceneNode& Node : Document_.Nodes)
                                if (Node.Kind == scene::NodeKind::Light)
                                    Modes.push_back(Node.Id);
                            if (std::find(Modes.begin(), Modes.end(), *Env.ShadowMode) == Modes.end())
                                Modes.push_back(*Env.ShadowMode);
                            for (const int Mode : Modes)
                                if (ImGui::Selectable(Label(Mode).c_str(), Mode == *Env.ShadowMode))
                                    Env.ShadowMode = Mode;
                            ImGui::EndCombo();
                        }
                        if (*Env.ShadowMode == -1)
                            ImGui::DragFloat3(TrId("Shadow position"), &Env.ShadowPosition.x, 0.5f);
                    }
                }

                Heading("Camera");
                DrawCameraSettings();

                Heading("Display");
                if (Project_ && Project_->HasDescriptor)
                    ImGui::TextWrapped("%s",
                                       Tr("These statements run after the .dbpro project's own display settings."));
                scene::DisplaySettings& Display = Document_.Display;
                Optional("Display mode", Display.DisplayMode, glm::ivec3(800, 600, 32),
                         [](glm::ivec3& Mode) { ImGui::InputInt3(TrId("Width, height, depth"), &Mode.x); });
                if (Pro(Document_) && Display.DisplayMode)
                {
                    bool Vsync = Display.Vsync.value_or(0) != 0;
                    if (ImGui::Checkbox(TrId("Vertical sync"), &Vsync))
                        Display.Vsync = Vsync ? 1 : 0;
                }
                int Window = Display.Windowed ? (*Display.Windowed ? 1 : 2) : 0;
                const char* const Windows[] = {Tr("Program default"), Tr("Windowed"), Tr("Full screen")};
                if (ImGui::Combo(TrId("Window"), &Window, Windows, 3))
                    Display.Windowed = Window == 0 ? std::nullopt : std::optional<bool>(Window == 1);
                Optional("Window size", Display.WindowSize, glm::ivec2(800, 600),
                         [](glm::ivec2& Size) { ImGui::InputInt2(TrId("Window size"), &Size.x); });
                Optional("Title", Display.WindowTitle, std::string("My Game"),
                         [](std::string& Title)
                         {
                             char Buffer[128];
                             std::strncpy(Buffer, Title.c_str(), sizeof(Buffer) - 1);
                             Buffer[sizeof(Buffer) - 1] = '\0';
                             if (ImGui::InputText(TrId("Title"), Buffer, sizeof(Buffer)))
                                 Title = Buffer;
                         });
                ImGui::Checkbox("SYNC ON", &Display.SyncOn);
                Optional("Sync rate", Display.SyncRate, 60,
                         [](int& Rate) { ImGui::InputInt(TrId("Sync rate"), &Rate); });
                ImGui::Checkbox(TrId("Hide the mouse"), &Display.HideMouse);
            }
            ImGui::EndDisabled();
        }
    } // namespace panels

} // namespace dbc::studio
