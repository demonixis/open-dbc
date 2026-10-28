// The Scene view's Inspector and Level Settings panels. Widgets edit the document
// in place; SceneEditor::CommitIfIdle() records an undo step once no widget is held.

#include "panels/scene_editor.hpp"

#include <algorithm>
#include <cstring>

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
                    ImGui::SetTooltip("Unchecked: the engine default, and no statement");
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
                Optional(Label, Value, Default, [&](scene::Rgb& Color) { EditColor(Label, Color); });
            }

            void OptionalFloat(const char* Label, std::optional<float>& Value, float Default, float Speed, float Min,
                               float Max)
            {
                Optional(Label, Value, Default,
                         [&](float& Number) { ImGui::DragFloat(Label, &Number, Speed, Min, Max, "%.2f"); });
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
                ImGui::TextUnformatted(Label);
                ImGui::PopID();
                return Changed;
            }

            void Heading(const char* Label)
            {
                ImGui::Spacing();
                ImGui::SeparatorText(Label);
            }
        } // namespace

        void SceneEditor::CommitIfIdle()
        {
            if (Editable() && !GizmoActive_ && !ImGui::IsAnyItemActive())
                Commit();
        }

        void SceneEditor::DrawInspector()
        {
            if (!HasLevel_)
                return;
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
                ImGui::TextDisabled("Select a node in the Hierarchy or the Viewport.");
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
                ImGui::Text("%zu nodes selected", Nodes.size());
                ImGui::TextDisabled("Showing %s; edits apply to each node that has the field.", Node->Name.c_str());
            }
            else
            {
                ImGui::TextDisabled("%s", scene::NodeKindLabel(Node->Kind));
                char Name[128];
                std::strncpy(Name, Node->Name.c_str(), sizeof(Name) - 1);
                Name[sizeof(Name) - 1] = '\0';
                if (ImGui::InputText("Name", Name, sizeof(Name)))
                    Node->Name = Name;
                int Id = Node->Id;
                if (ImGui::InputInt("Number", &Id, 0) && Id > 0 &&
                    Document_.Find(scene::IdSpaceOf(Node->Kind), Id) == nullptr)
                {
                    Node->Id = Id;
                    Select(Node);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The object, matrix, terrain, light or sound number the code uses");
            }
            if (IsObject)
            {
                const bool Available = scene::StaticAvailable(Document_.Profile);
                ImGui::BeginDisabled(!Available);
                // A static object no longer exists to animate.
                if (ImGui::Checkbox("Make Static Object", &Node->Static) && Node->Static)
                {
                    Node->Frames.reset();
                    Node->Speed.reset();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", Available ? "MAKE STATIC OBJECT, then DELETE OBJECT: fixed scenery for the "
                                                       "static collision commands; its number is free at run time"
                                                     : "MAKE STATIC OBJECT is not implemented in the Pro profile");
            }

            Heading("Transform");
            ImGui::DragFloat3("Position", &Node->Position.x, 0.5f);
            const bool Turns = IsObject || (IsLight && Node->Light != scene::LightKind::Point);
            if (Turns)
                ImGui::DragFloat3("Rotation", &Node->Rotation.x, 0.5f);
            if (IsObject)
            {
                const glm::vec3 Before = Node->Scale;
                if (ImGui::DragFloat3("Scale %", &Node->Scale.x, 0.5f, 0.0f, 100000.0f) && UniformScale_)
                    Node->Scale = scene::ProportionalScale(Before, Node->Scale);
                ImGui::Checkbox("Uniform scale", &UniformScale_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Scaling one axis scales the others in proportion, here and with the gizmo");
            }
            if (Node->Kind == scene::NodeKind::Terrain)
                ImGui::TextDisabled("A terrain runs from its position toward -Z.");
            if (Node->Kind == scene::NodeKind::Matrix)
                ImGui::TextDisabled("A matrix runs from its position toward +X and +Z.");

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
                    ImGui::DragFloat("Size", &Node.Size, 0.25f, 0.01f, 100000.0f);
                    break;
                case scene::NodeKind::Box:
                    ImGui::DragFloat3("Width, height, depth", &Node.Extent.x, 0.25f, 0.01f, 100000.0f);
                    break;
                case scene::NodeKind::Sphere:
                    ImGui::DragFloat("Diameter", &Node.Size, 0.25f, 0.01f, 100000.0f);
                    if (Pro(Document_))
                        Optional("Rings, segments", Node.Detail, glm::ivec2(12, 16),
                                 [](glm::ivec2& Detail)
                                 {
                                     ImGui::DragInt2("Rings, segments", &Detail.x, 0.25f, 3, 100);
                                     Detail = glm::max(Detail, glm::ivec2(3));
                                 });
                    break;
                case scene::NodeKind::Cylinder:
                case scene::NodeKind::Cone:
                    ImGui::DragFloat("Size", &Node.Size, 0.25f, 0.01f, 100000.0f);
                    break;
                case scene::NodeKind::Plain:
                    ImGui::DragFloat2("Width, height", &Node.Extent.x, 0.25f, 0.01f, 100000.0f);
                    if (Pro(Document_))
                        Optional("Orientation", Node.PlainOrientation, 0,
                                 [](int& Orientation) { ImGui::InputInt("Orientation", &Orientation); });
                    break;
                case scene::NodeKind::Triangle:
                    ImGui::DragFloat3("Corner 1", &Node.Corners[0].x, 0.25f);
                    ImGui::DragFloat3("Corner 2", &Node.Corners[1].x, 0.25f);
                    ImGui::DragFloat3("Corner 3", &Node.Corners[2].x, 0.25f);
                    break;
                case scene::NodeKind::Model:
                    AssetField("Model file", Node.Asset, core::MediaKind::Model);
                    DrawAnimation(Node);
                    break;
                case scene::NodeKind::Terrain:
                    AssetField("Heightmap", Node.Asset, core::MediaKind::Image);
                    if (Node.Asset.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "A terrain needs a square heightmap image.");
                    DrawTerrainHeightmap(Node);
                    break;
                case scene::NodeKind::Matrix:
                    ImGui::DragFloat("Width", &Node.Width, 1.0f, 0.01f, 100000.0f);
                    ImGui::DragFloat("Depth", &Node.Depth, 1.0f, 0.01f, 100000.0f);
                    ImGui::DragInt("Tiles across", &Node.XSegments, 0.2f, 1, 256);
                    ImGui::DragInt("Tiles down", &Node.ZSegments, 0.2f, 1, 256);
                    DrawMatrixHeights(Node);
                    break;
                case scene::NodeKind::Light:
                {
                    int Kind = static_cast<int>(Node.Light);
                    if (ImGui::Combo("Type", &Kind, "Point\0Spot\0Directional\0"))
                        Node.Light = static_cast<scene::LightKind>(Kind);
                    if (Node.Light != scene::LightKind::Directional)
                        OptionalFloat("Range", Node.Range, 1000.0f, 1.0f, 0.0f, 100000.0f);
                    if (Node.Light == scene::LightKind::Spot)
                    {
                        ImGui::DragFloat("Inner cone", &Node.SpotInner, 0.25f, 0.0f, 180.0f);
                        ImGui::DragFloat("Outer cone", &Node.SpotOuter, 0.25f, 0.0f, 180.0f);
                    }
                    OptionalColor("Colour", Node.LightColor, 0xFFFFFF);
                    break;
                }
                case scene::NodeKind::Sound:
                    AssetField("Sound file", Node.Asset, core::MediaKind::Audio);
                    if (Node.Asset.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "A 3D sound needs a sound file.");
                    ImGui::Checkbox("Play on load", &Node.PlayOnLoad);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Unchecked, the sound waits for the program's PLAY SOUND or LOOP SOUND");
                    if (Node.PlayOnLoad)
                    {
                        ImGui::SameLine();
                        ImGui::Checkbox("Loop", &Node.Loop);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("LOOP SOUND repeats it; unchecked, PLAY SOUND plays it once");
                    }
                    Optional("Volume %", Node.Volume, 100,
                             [](int& Volume) { ImGui::SliderInt("Volume %", &Volume, 0, 100); });
                    ImGui::TextDisabled("The program hears it from POSITION LISTENER, at 0, 0, 0 until moved.");
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
                if (!scene::ReadModelAnimation(Project_->AssetRoot / Asset, *Read, Error))
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
                ImGui::TextDisabled("A static object does not animate.");
                return;
            }
            const scene::ModelAnimation* Info = ModelAnimationOf(Node.Asset);
            const bool HasClip = Info != nullptr && Info->Count > 0;
            if (!HasClip && !Node.Frames)
            {
                ImGui::TextDisabled("%s", Node.Asset.empty() ? "Pick a model file first."
                                          : Info == nullptr  ? "The model file cannot be read."
                                                             : "This model has no animation.");
                return;
            }

            // LOAD OBJECT keeps one timeline: an animation is a range of its frames.
            const int Last = HasClip ? Info->LastFrame : std::max(0, Node.Frames->y);
            const std::string Clip = (HasClip && !Info->Name.empty() ? Info->Name : std::string("Animation")) +
                                     " (frames 0 to " + std::to_string(Last) + ")";
            const char* Items[] = {"None", Clip.c_str()};
            int Choice = Node.Frames ? 1 : 0;
            if (ImGui::Combo("Animation", &Choice, Items, 2))
                Node.Frames = Choice == 0 ? std::nullopt : std::optional<glm::ivec2>(glm::ivec2(0, Last));
            if (HasClip && Info->Count > 1)
                ImGui::TextDisabled("The file holds %u animations; LOAD OBJECT plays the first.", Info->Count);
            if (!Node.Frames)
                return;

            ImGui::Checkbox("Loop", &Node.Loop);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("LOOP OBJECT repeats the frames; unchecked, PLAY OBJECT plays them once");
            glm::ivec2& Frames = *Node.Frames;
            ImGui::DragIntRange2("Frames", &Frames.x, &Frames.y, 0.25f, 0, Last, "First %d", "Last %d");
            Frames.x = std::clamp(Frames.x, 0, Last);
            Frames.y = std::clamp(Frames.y, Frames.x, Last);
            Optional("Speed", Node.Speed, 20, [](int& Speed) { ImGui::SliderInt("Speed", &Speed, 1, 200); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("SET OBJECT SPEED; 20 plays the file at its own rate");
            ImGui::TextDisabled("The viewport shows the model still.");
        }

        void SceneEditor::DrawMatrixHeights(scene::SceneNode& Node)
        {
            Heading("Heights");
            if (!Node.Heights.empty() && scene::MatrixHeights(Node).empty())
            {
                // The tile count changed: the old heights no longer fit.
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error),
                                   "The heights no longer fit this matrix.");
                if (ImGui::Button("Flatten"))
                    Node.Heights.clear();
            }

            const int TileCount =
                Node.Mat.Texture.empty() ? 1 : std::max(1, Node.TilesAcross) * std::max(1, Node.TilesDown);
            ImGui::DragFloat("Fill height", &FillHeight_, 0.25f);
            ImGui::BeginDisabled(TileCount == 1);
            ImGui::SliderInt("Fill tile", &FillTile_, 1, std::max(2, TileCount));
            ImGui::EndDisabled();
            FillTile_ = std::clamp(FillTile_, 1, TileCount);
            if (ImGui::Button("Fill"))
            {
                Node.Heights.assign(scene::MatrixVertexCount(Node), FillHeight_);
                Node.Tile = FillTile_;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("FILL MATRIX: every height to Fill height, every tile to Fill tile");

            ImGui::DragFloat("Height range", &HeightRange_, 0.25f, 0.0f, 100000.0f);
            if (ImGui::Button("Randomize"))
            {
                // RANDOMIZE MATRIX draws whole heights from 0 to the range, excluded.
                const int Maximum = static_cast<int>(HeightRange_);
                std::uniform_int_distribution<int> Distribution(0, std::max(0, Maximum - 1));
                Node.Heights.resize(scene::MatrixVertexCount(Node));
                for (float& Height : Node.Heights)
                    Height = static_cast<float>(Distribution(Random_));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("RANDOMIZE MATRIX: random whole heights below the range");
            ImGui::TextDisabled("The heights are saved in the level, so every run shows this matrix.");
        }

        void SceneEditor::DrawTerrainHeightmap(scene::SceneNode& Node)
        {
            Heading("Generate heightmap");
            static constexpr int KSizes[] = {64, 128, 256, 512};
            const std::string Current = std::to_string(HeightmapSize_) + " x " + std::to_string(HeightmapSize_);
            if (ImGui::BeginCombo("Size", Current.c_str()))
            {
                for (const int Size : KSizes)
                    if (ImGui::Selectable((std::to_string(Size) + " x " + std::to_string(Size)).c_str(),
                                          Size == HeightmapSize_))
                        HeightmapSize_ = Size;
                ImGui::EndCombo();
            }

            // Terrain heights are an image's grey levels, 0 to KTerrainHeightScale.
            std::optional<scene::Heightmap> Generated;
            ImGui::DragFloat("Fill height", &TerrainFillHeight_, 0.25f, 0.0f, scene::KTerrainHeightScale);
            if (ImGui::Button("Fill##terrain"))
                Generated = scene::FlatHeightmap(HeightmapSize_, TerrainFillHeight_);
            ImGui::DragFloat("Height range", &TerrainRange_, 0.25f, 0.0f, scene::KTerrainHeightScale);
            ImGui::DragInt("Hills across", &TerrainHills_, 0.1f, 1, 32);
            if (ImGui::Button("Randomize##terrain"))
                Generated = scene::RandomHeightmap(HeightmapSize_, TerrainRange_, TerrainHills_, Random_);

            const std::filesystem::path Relative = scene::TerrainHeightmapPath(Document_.Name, Node.Id);
            if (Generated && Project_)
            {
                // Always the level's own file: a heightmap the user picked is never overwritten.
                if (scene::WriteHeightmap(Project_->AssetRoot / Relative, HeightmapSize_, *Generated, TerrainError_))
                {
                    TerrainError_.clear();
                    Node.Asset = Relative.generic_string();
                    RefreshAssets();
                }
            }
            if (!TerrainError_.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error), "%s", TerrainError_.c_str());
            ImGui::TextDisabled("Writes %s", Relative.generic_string().c_str());
        }

        void SceneEditor::DrawMaterial(scene::SceneNode& Node)
        {
            scene::Material& Mat = Node.Mat;
            Heading("Material");
            AssetField("Texture", Mat.Texture, core::MediaKind::Image);
            if (Node.Kind == scene::NodeKind::Matrix && !Mat.Texture.empty())
            {
                ImGui::DragInt("Image tiles across", &Node.TilesAcross, 0.1f, 1, 64);
                ImGui::DragInt("Image tiles down", &Node.TilesDown, 0.1f, 1, 64);
            }
            if (scene::IdSpaceOf(Node.Kind) != scene::IdSpace::Object)
                return;

            OptionalColor("Colour", Mat.Color, 0xFFFFFF);
            ImGui::Checkbox("Ghost", &Mat.Ghost);
            if (Mat.Ghost)
            {
                ImGui::SameLine();
                Optional("Ghost mode", Mat.GhostMode, 0,
                         [](int& Mode) { ImGui::SliderInt("Ghost mode", &Mode, 0, 5); });
            }

            scene::ObjectFlags& Flags = Mat.Flags;
            ImGui::Checkbox("Wireframe", &Flags.Wireframe);
            if (Pro(Document_))
            {
                ImGui::SliderInt("Transparency", &Flags.Transparency, 0, 7);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("SET OBJECT transparency mode; 0 draws black as black");
            }
            else
            {
                bool Transparent = Flags.Transparency != scene::OpaqueTransparency(Document_.Profile);
                if (ImGui::Checkbox("Black is transparent", &Transparent))
                    Flags.Transparency = Transparent ? 0 : scene::OpaqueTransparency(Document_.Profile);
            }
            ImGui::Checkbox("Cull back faces", &Flags.Cull);
            ImGui::SameLine();
            ImGui::Checkbox("Filter", &Flags.Filter);
            ImGui::Checkbox("Lighting", &Flags.Lighting);
            ImGui::SameLine();
            ImGui::Checkbox("Fog", &Flags.Fog);
            ImGui::SameLine();
            ImGui::Checkbox("Ambient", &Flags.Ambient);

            if (!Pro(Document_))
                return;
            OptionalColor("Diffuse", Mat.Diffuse, 0xFFFFFF);
            OptionalColor("Ambience", Mat.Ambience, 0xFFFFFF);
            OptionalColor("Emissive", Mat.Emissive, 0x000000);
            OptionalColor("Specular", Mat.Specular, 0xFFFFFF);
            OptionalFloat("Specular power", Mat.SpecularPower, 10.0f, 0.25f, 0.0f, 255.0f);
            ImGui::Checkbox("Casts a shadow", &Mat.CastShadow);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("SET SHADOW SHADING ON; the viewport does not draw shadows");
        }

        void SceneEditor::DrawCameraSettings()
        {
            scene::Environment& Env = Document_.Env;
            ImGui::Checkbox("Use a level camera", &Env.UseCamera);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Writes AUTOCAM OFF and places camera 0; unchecked, the program keeps its camera");
            if (!Env.UseCamera)
                return;
            ImGui::DragFloat3("Camera position", &Env.CameraPosition.x, 0.5f);
            ImGui::DragFloat3("Camera rotation", &Env.CameraRotation.x, 0.5f);
            OptionalFloat("Field of view", Env.CameraFov, 75.0f, 0.25f, 1.0f, 179.0f);
            Optional("Near, far", Env.CameraRange, glm::vec2(0.1f, 5000.0f),
                     [](glm::vec2& Range) { ImGui::DragFloat2("Near, far", &Range.x, 0.5f, 0.01f, 1000000.0f); });
            if (ImGui::Button("Match the viewport"))
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
            if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("%s", scene::LevelRelativePath(Document_.Name).generic_string().c_str());
                ImGui::TextDisabled("Function %s()", scene::LevelFunctionName(Document_).c_str());

                Heading("Light and colour");
                Optional("Ambient %", Env.AmbientPercent, 25,
                         [](int& Percent) { ImGui::SliderInt("Ambient %", &Percent, 0, 100); });
                OptionalColor("Ambient colour", Env.AmbientColor, 0xFFFFFF);
                OptionalColor("Backdrop", Env.Backdrop, 0x000000);
                ImGui::Checkbox("Hide light 0", &Env.DefaultLightHidden);
                if (!Env.DefaultLightHidden)
                {
                    Optional("Light 0 rotation", Env.DefaultLightRotation, glm::vec3(45.0f, 0.0f, 0.0f),
                             [](glm::vec3& Rotation) { ImGui::DragFloat3("Light 0 rotation", &Rotation.x, 0.5f); });
                    OptionalColor("Light 0 colour", Env.DefaultLightColor, 0xFFFFFF);
                }

                Heading("Fog");
                ImGui::Checkbox("Fog", &Env.Fog);
                if (Env.Fog)
                {
                    OptionalColor("Fog colour", Env.FogColor, 0x000000);
                    OptionalFloat("Fog distance", Env.FogDistance, 3000.0f, 5.0f, 0.0f, 1000000.0f);
                }

                if (Pro(Document_))
                {
                    Heading("Shadows");
                    bool Shadows = Env.ShadowMode.has_value();
                    if (ImGui::Checkbox("Shadow source", &Shadows))
                        Env.ShadowMode = Shadows ? std::optional<int>(-1) : std::nullopt;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("SET SHADOW POSITION; objects cast with Casts a shadow in their material");
                    if (Env.ShadowMode)
                    {
                        bool Fixed = *Env.ShadowMode == -1;
                        if (ImGui::Checkbox("Fixed position", &Fixed))
                            Env.ShadowMode = Fixed ? -1 : 0;
                        if (Fixed)
                            ImGui::DragFloat3("Shadow position", &Env.ShadowPosition.x, 0.5f);
                        else
                            ImGui::InputInt("Shadow light", &*Env.ShadowMode);
                    }
                }

                Heading("Camera");
                DrawCameraSettings();

                Heading("Numbers");
                static const char* const Labels[] = {"First object", "First image", "First matrix", "First terrain",
                                                     "First light",  "First sound"};
                for (std::size_t Space = 0; Space < scene::KIdSpaceCount; ++Space)
                {
                    int Base = Document_.IdBase[Space];
                    if (ImGui::InputInt(Labels[Space], &Base) && Base > 0)
                        Document_.IdBase[Space] = Base;
                }
                ImGui::TextDisabled("New nodes take free numbers from these, away from the program's own.");
            }
            ImGui::EndDisabled();

            if (ImGui::CollapsingHeader("Game", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("%s, for every level", scene::KGameFileName);
                if (Project_ && Project_->HasDescriptor)
                    ImGui::TextWrapped("These statements run after the .dbpro project's own display settings.");
                for (const scene::ReadError& Error : GameErrors_)
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Line %u: %s", Error.Line,
                                       Error.Message.c_str());
                ImGui::BeginDisabled(!GameErrors_.empty() || !HasLevel_);
                scene::GameSettings& Game = Game_;
                Optional("Display mode", Game.DisplayMode, glm::ivec3(800, 600, 32),
                         [](glm::ivec3& Mode) { ImGui::InputInt3("Width, height, depth", &Mode.x); });
                if (Pro(Document_) && Game.DisplayMode)
                {
                    bool Vsync = Game.Vsync.value_or(0) != 0;
                    if (ImGui::Checkbox("Vertical sync", &Vsync))
                        Game.Vsync = Vsync ? 1 : 0;
                }
                int Window = Game.Windowed ? (*Game.Windowed ? 1 : 2) : 0;
                if (ImGui::Combo("Window", &Window, "Program default\0Windowed\0Full screen\0"))
                    Game.Windowed = Window == 0 ? std::nullopt : std::optional<bool>(Window == 1);
                Optional("Window size", Game.WindowSize, glm::ivec2(800, 600),
                         [](glm::ivec2& Size) { ImGui::InputInt2("Window size", &Size.x); });
                Optional("Title", Game.WindowTitle, std::string("My Game"),
                         [](std::string& Title)
                         {
                             char Buffer[128];
                             std::strncpy(Buffer, Title.c_str(), sizeof(Buffer) - 1);
                             Buffer[sizeof(Buffer) - 1] = '\0';
                             if (ImGui::InputText("Title", Buffer, sizeof(Buffer)))
                                 Title = Buffer;
                         });
                ImGui::Checkbox("SYNC ON", &Game.SyncOn);
                Optional("Sync rate", Game.SyncRate, 60, [](int& Rate) { ImGui::InputInt("Sync rate", &Rate); });
                ImGui::Checkbox("Hide the mouse", &Game.HideMouse);
                ImGui::EndDisabled();
            }
        }
    } // namespace panels

} // namespace dbc::studio
