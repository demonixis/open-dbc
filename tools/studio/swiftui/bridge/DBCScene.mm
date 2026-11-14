// The Scene mode for the SwiftUI Studio. The level, its selection and its tools stay in
// panels::SceneEditor; the Hierarchy, the Inspector and the Level Settings are handed to the
// interface as rows, and the viewport keeps drawing through the editor into a Metal view.

#import "DBCStudio.h"

#include <algorithm>
#include <array>
#include <cfloat>

#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include "core/i18n.hpp"
#include "core/media_catalogue.hpp"
#include "path_utf8.hpp"
#include "scene/heightmap.hpp"
#include "scene/scene_geometry.hpp"
#include "studio_session.hpp"
#include "theme.hpp"

using namespace dbc::studio;
using namespace dbc::studio::swiftui;

namespace dbc::studio::panels
{
    struct SceneEditorAccess
    {
        using Form = swiftui::FormBuilder;

        static std::string AxisLabel(const char* Label, int Axis)
        {
            static constexpr const char* KAxes[] = {"X", "Y", "Z"};
            return std::string(Tr(Label)) + " " + KAxes[Axis];
        }

        static void AxisLocks(SceneEditor& E, Form& F, const scene::SceneNode& Node, scene::TransformPart Part)
        {
            const scene::NodeOrigin* Origin = E.Program_ ? E.OriginOf(Node) : nullptr;
            if (Origin == nullptr)
                return;
            static constexpr const char* KAxes[] = {"X", "Y", "Z"};
            const auto& Axes = Origin->Axes[static_cast<std::size_t>(Part)];
            for (std::size_t Index = 0; Index < Axes.size(); ++Index)
            {
                if (Axes[Index].Access == scene::AxisAccess::Locked)
                    F.Hint(TrFmt("{0} locked: {1}", {KAxes[Index], Axes[Index].Reason}));
            }
        }

        static void ModelDetails(SceneEditor& E, Form& F, const std::string& Asset)
        {
            F.Heading("Model details");
            if (Asset.empty())
            {
                F.Hint(Tr("Pick a model file first."));
                return;
            }
            const bool WasEnabled = F.Enabled;
            F.Enabled = true;
            F.Button(Tr("Refresh"), [&E, Asset] { E.ModelInfos_.erase(Asset); });
            F.Hint(Asset);
            const SceneEditor::ModelInspection* Read = E.ModelInfoOf(Asset);
            if (Read == nullptr || !Read->Info)
            {
                F.Hint(Tr("The model file cannot be read."));
                if (Read != nullptr && !Read->Error.empty())
                    F.Hint(Read->Error);
                F.Enabled = WasEnabled;
                return;
            }
            const scene::ModelInfo& Info = *Read->Info;
            const auto Row = [&](const char* Label, const std::string& Value)
            {
                Field& Value_ = F.Add(FieldKind::Text, Tr(Label));
                Value_.Text = Value;
                Value_.Enabled = false;
            };
            Row("Name", Info.Name);
            Row("Triangles", std::to_string(Info.Triangles));
            Row("Vertices", std::to_string(Info.Vertices));
            Row("Meshes", std::to_string(Info.Meshes));
            Row("Referenced textures", std::to_string(Info.ReferencedTextures));
            Row("Limbs", std::to_string(Info.Limbs.size()));
            F.Hint(Tr("File contents at LOAD OBJECT; later changes made by the program are not shown."));
            if (!Info.Limbs.empty())
            {
                F.Button(TrFmt("Limbs ({0})...", {std::to_string(Info.Limbs.size())}),
                         [&E] { E.ShowLimbs_ = !E.ShowLimbs_; })
                    .Tooltip = Tr("Lists each limb with the number LIMB commands take");
                if (E.ShowLimbs_)
                {
                    for (const scene::ModelLimb& Limb : Info.Limbs)
                        Row(std::to_string(Limb.Id).c_str(), Limb.Name.empty() ? Tr("(unnamed)") : Limb.Name);
                }
            }
            F.Enabled = WasEnabled;
        }

        static void Animation(SceneEditor& E, Form& F, scene::SceneNode& Node)
        {
            F.Heading("Animation");
            const SceneEditor::ModelInspection* Read = E.ModelInfoOf(Node.Asset);
            const scene::ModelAnimation* Info = Read != nullptr && Read->Info ? &Read->Info->Animation : nullptr;
            const bool HasClip = Info != nullptr && Info->Count > 0;
            if (!HasClip && !Node.Frames)
            {
                F.Hint(Tr(Node.Asset.empty() ? "Pick a model file first."
                          : Info == nullptr  ? "The model file cannot be read."
                                             : "This model has no animation."));
                return;
            }
            // LOAD OBJECT keeps one timeline: an animation is a range of its frames.
            const int Last = HasClip ? Info->LastFrame : std::max(0, Node.Frames->y);
            const std::string Clip = TrFmt(
                "{0} (frames 0 to {1})",
                {HasClip && !Info->Name.empty() ? Info->Name : std::string(Tr("Animation")), std::to_string(Last)});
            scene::SceneNode* Target = &Node;
            F.Choice(Tr("Animation"), {Tr("None"), Clip}, Node.Frames ? 1 : 0,
                     [Target, Last](int Choice)
                     { Target->Frames = Choice == 0 ? std::nullopt : std::optional<glm::ivec2>(glm::ivec2(0, Last)); });
            if (HasClip && Info->Count > 1)
                F.Hint(TrFmt("The file holds {0} animations; LOAD OBJECT plays the first.",
                             {std::to_string(Info->Count)}));
            if (!Node.Frames)
                return;
            F.Toggle(Tr("Loop"), Node.Loop).Tooltip =
                Tr("LOOP OBJECT repeats the frames; unchecked, PLAY OBJECT plays them once");
            F.Int(TrFmt("First {0}", {""}), Node.Frames->x, 0, Last).SetNumber = [Target, Last](double Number)
            {
                Target->Frames->x = std::clamp(static_cast<int>(Number), 0, Last);
                Target->Frames->y = std::clamp(Target->Frames->y, Target->Frames->x, Last);
            };
            F.Int(TrFmt("Last {0}", {""}), Node.Frames->y, 0, Last).SetNumber = [Target, Last](double Number)
            { Target->Frames->y = std::clamp(static_cast<int>(Number), Target->Frames->x, Last); };
            F.OptionalInt("Speed", Node.Speed, 20, 1, 200, true);
            F.Hint(Tr("The viewport shows the model still."));
        }

        static void MatrixHeights(SceneEditor& E, Form& F, scene::SceneNode& Node)
        {
            F.Heading("Heights");
            scene::SceneNode* Target = &Node;
            if (!Node.Heights.empty() && scene::MatrixHeights(Node).empty())
            {
                // The tile count changed: the old heights no longer fit.
                F.Warning(Tr("The heights no longer fit this matrix."));
                F.Button(Tr("Flatten"), [Target] { Target->Heights.clear(); });
            }
            const int TileCount =
                Node.Mat.Texture.empty() ? 1 : std::max(1, Node.TilesAcross) * std::max(1, Node.TilesDown);
            E.FillTile_ = std::clamp(E.FillTile_, 1, TileCount);
            F.Float(Tr("Fill height"), E.FillHeight_, 0.25);
            F.Int(Tr("Fill tile"), E.FillTile_, 1, std::max(2, TileCount), true).Enabled = F.Enabled && TileCount > 1;
            F.Button(Tr("Fill"),
                     [&E, Target]
                     {
                         Target->Heights.assign(scene::MatrixVertexCount(*Target), E.FillHeight_);
                         Target->Tile = E.FillTile_;
                     })
                .Tooltip = Tr("FILL MATRIX: every height to Fill height, every tile to Fill tile");
            F.Float(Tr("Height range"), E.HeightRange_, 0.25, 0.0, 100000.0);
            F.Button(Tr("Randomize"),
                     [&E, Target]
                     {
                         // RANDOMIZE MATRIX draws whole heights from 0 to the range, excluded.
                         const int Maximum = static_cast<int>(E.HeightRange_);
                         std::uniform_int_distribution<int> Distribution(0, std::max(0, Maximum - 1));
                         Target->Heights.resize(scene::MatrixVertexCount(*Target));
                         for (float& Height : Target->Heights)
                             Height = static_cast<float>(Distribution(E.Random_));
                     })
                .Tooltip = Tr("RANDOMIZE MATRIX: random whole heights below the range");
            F.Hint(Tr("The heights are saved in the level, so every run shows this matrix."));
        }

        static void TerrainHeightmap(SceneEditor& E, Form& F, scene::SceneNode& Node)
        {
            F.Heading("Generate heightmap");
            static constexpr int KSizes[] = {64, 128, 256, 512};
            std::vector<std::string> Sizes;
            int Current = 1;
            for (int Index = 0; Index < 4; ++Index)
            {
                Sizes.push_back(std::to_string(KSizes[Index]) + " x " + std::to_string(KSizes[Index]));
                if (KSizes[Index] == E.HeightmapSize_)
                    Current = Index;
            }
            F.Choice(Tr("Size"), Sizes, Current, [&E](int Choice) { E.HeightmapSize_ = KSizes[std::clamp(Choice, 0, 3)]; });

            scene::SceneNode* Target = &Node;
            const std::filesystem::path Relative = scene::TerrainHeightmapPath(E.Document_.Name, Node.Id);
            const auto Write = [&E, Target, Relative](const scene::Heightmap& Generated)
            {
                // Always the level's own file: a heightmap the user picked is never overwritten.
                if (!E.Project_ || !scene::WriteHeightmap(E.Project_->AssetRoot / Relative, E.HeightmapSize_, Generated,
                                                          E.TerrainError_))
                    return;
                E.TerrainError_.clear();
                Target->Asset = dbc::GenericPathToUtf8(Relative);
                E.RefreshAssets();
            };
            // Terrain heights are an image's grey levels, 0 to KTerrainHeightScale.
            F.Float(Tr("Fill height"), E.TerrainFillHeight_, 0.25, 0.0, scene::KTerrainHeightScale);
            F.Button(Tr("Fill"), [&E, Write] { Write(scene::FlatHeightmap(E.HeightmapSize_, E.TerrainFillHeight_)); });
            F.Float(Tr("Height range"), E.TerrainRange_, 0.25, 0.0, scene::KTerrainHeightScale);
            F.Int(Tr("Hills across"), E.TerrainHills_, 1, 32);
            F.Button(Tr("Randomize"),
                     [&E, Write]
                     {
                         Write(scene::RandomHeightmap(E.HeightmapSize_, E.TerrainRange_, E.TerrainHills_, E.Random_));
                     });
            if (!E.TerrainError_.empty())
                F.Warning(E.TerrainError_);
            F.Hint(TrFmt("Writes {0}", {dbc::GenericPathToUtf8(Relative)}));
        }

        static void Shape(SceneEditor& E, Form& F, scene::SceneNode& Node)
        {
            F.Heading(Node.Kind == scene::NodeKind::Sound ? "Sound" : "Shape");
            static constexpr const char* KBox[] = {"Width", "Height", "Depth"};
            switch (Node.Kind)
            {
                case scene::NodeKind::Cube:
                case scene::NodeKind::Cylinder:
                case scene::NodeKind::Cone:
                    F.Float(Tr("Size"), Node.Size, 0.25, 0.01, 100000.0);
                    break;
                case scene::NodeKind::Box:
                    for (int Axis = 0; Axis < 3; ++Axis)
                        F.Float(Tr(KBox[Axis]), Node.Extent[Axis], 0.25, 0.01, 100000.0);
                    break;
                case scene::NodeKind::Sphere:
                {
                    F.Float(Tr("Diameter"), Node.Size, 0.25, 0.01, 100000.0);
                    glm::ivec2 Shown(0);
                    F.Optional(Node.Detail, glm::ivec2(12, 16), Shown,
                               [&](glm::ivec2& Detail)
                               {
                                   F.Int(Tr("Rings"), Detail.x, 3, 100);
                                   F.Int(Tr("Segments"), Detail.y, 3, 100);
                               });
                    break;
                }
                case scene::NodeKind::Plain:
                {
                    F.Float(Tr("Width"), Node.Extent.x, 0.25, 0.01, 100000.0);
                    F.Float(Tr("Height"), Node.Extent.y, 0.25, 0.01, 100000.0);
                    F.OptionalInt("Orientation", Node.PlainOrientation, 0, -1000000, 1000000);
                    break;
                }
                case scene::NodeKind::Triangle:
                    for (int Corner = 0; Corner < 3; ++Corner)
                    {
                        const std::string Label = TrFmt("Corner {0}", {std::to_string(Corner + 1)});
                        for (int Axis = 0; Axis < 3; ++Axis)
                            F.Float(AxisLabel(Label.c_str(), Axis), Node.Corners[static_cast<std::size_t>(Corner)][Axis],
                                    0.25);
                    }
                    break;
                case scene::NodeKind::Model:
                    F.Asset(Tr("Model file"), Node.Asset, static_cast<int>(core::MediaKind::Model));
                    Animation(E, F, Node);
                    break;
                case scene::NodeKind::Terrain:
                    F.Asset(Tr("Heightmap"), Node.Asset, static_cast<int>(core::MediaKind::Image));
                    if (Node.Asset.empty())
                        F.Warning(Tr("A terrain needs a square heightmap image."));
                    TerrainHeightmap(E, F, Node);
                    break;
                case scene::NodeKind::Matrix:
                    F.Float(Tr("Width"), Node.Width, 1.0, 0.01, 100000.0);
                    F.Float(Tr("Depth"), Node.Depth, 1.0, 0.01, 100000.0);
                    F.Int(Tr("Tiles across"), Node.XSegments, 1, 256);
                    F.Int(Tr("Tiles down"), Node.ZSegments, 1, 256);
                    MatrixHeights(E, F, Node);
                    break;
                case scene::NodeKind::Light:
                {
                    scene::SceneNode* Target = &Node;
                    F.Choice(Tr("Type"), {Tr("Point"), Tr("Spot"), Tr("Directional")}, static_cast<int>(Node.Light),
                             [Target](int Kind) { Target->Light = static_cast<scene::LightKind>(Kind); });
                    if (Node.Light != scene::LightKind::Directional)
                        F.OptionalFloat("Range", Node.Range, 1000.0f, 1.0, 0.0, 100000.0);
                    if (Node.Light == scene::LightKind::Spot)
                    {
                        F.Float(Tr("Inner cone"), Node.SpotInner, 0.25, 0.0, 180.0);
                        F.Float(Tr("Outer cone"), Node.SpotOuter, 0.25, 0.0, 180.0);
                    }
                    F.OptionalColor("Colour", Node.LightColor, 0xFFFFFF);
                    break;
                }
                case scene::NodeKind::Sound:
                    F.Asset(Tr("Sound file"), Node.Asset, static_cast<int>(core::MediaKind::Audio));
                    if (Node.Asset.empty())
                        F.Warning(Tr("A 3D sound needs a sound file."));
                    F.Toggle(Tr("Play on load"), Node.PlayOnLoad).Tooltip =
                        Tr("Unchecked, the sound waits for the program's PLAY SOUND or LOOP SOUND");
                    if (Node.PlayOnLoad)
                        F.Toggle(Tr("Loop"), Node.Loop).Tooltip =
                            Tr("LOOP SOUND repeats it; unchecked, PLAY SOUND plays it once");
                    F.OptionalInt("Volume %", Node.Volume, 100, 0, 100, true);
                    F.Hint(Tr("The program hears it from POSITION LISTENER, at 0, 0, 0 until moved."));
                    break;
            }
        }

        static void TextureStages(SceneEditor& E, Form& F, scene::Material& Mat)
        {
            const int Image = static_cast<int>(core::MediaKind::Image);
            F.Heading("Texture stages");
            F.Hint(Tr("Written to the level; the Scene view shows the first texture only."));
            scene::Material* Target = &Mat;

            F.Asset(Tr("Effect"), Mat.Effect, static_cast<int>(core::MediaKind::Effect));
            if (!Mat.Effect.empty())
            {
                F.Toggle(Tr("Effect textures"), Mat.EffectTextures).Tooltip =
                    Tr("LOAD EFFECT's texture flag: the effect loads the textures it names");
                // LOAD EFFECT reads the compiled manifest beside the file; without it SET OBJECT EFFECT stops the game.
                std::filesystem::path Manifest = dbc::PathFromUtf8(Mat.Effect);
                if (!Mat.Effect.ends_with(".effect.json"))
                    Manifest.replace_extension(".effect.json");
                std::error_code Ignored;
                if (E.Project_ && !std::filesystem::is_regular_file(E.Project_->AssetRoot / Manifest, Ignored))
                    F.Warning(TrFmt("{0} is missing: compile the effect from the Build menu.",
                                    {dbc::GenericPathToUtf8(Manifest)}));
            }

            // An effect takes the object over from cartoon and rainbow shading.
            F.Choice(Tr("Shading"), {Tr("None"), Tr("Cartoon"), Tr("Rainbow")}, static_cast<int>(Mat.Shade),
                     [Target](int Shade) { Target->Shade = static_cast<scene::Shading>(Shade); })
                .Enabled = F.Enabled && Mat.Effect.empty();
            if (Mat.Shade != scene::Shading::None)
            {
                F.Asset(Tr(Mat.Shade == scene::Shading::Cartoon ? "Shade image" : "Rainbow image"), Mat.ShadeTexture,
                        Image);
                if (Mat.Shade == scene::Shading::Cartoon)
                    F.Asset(Tr("Edge image"), Mat.EdgeTexture, Image);
                F.Hint(Tr("Cartoon and rainbow shading replace the texture and every other stage."));
            }
            else
            {
                F.Choice(Tr("Second texture"),
                         {Tr("None"), Tr("Light map"), Tr("Detail map"), Tr("Sphere map"), Tr("Normal map"),
                          Tr("Blend")},
                         static_cast<int>(Mat.Second),
                         [Target](int Second) { Target->Second = static_cast<scene::SecondMap>(Second); });
                if (Mat.Second != scene::SecondMap::None)
                {
                    F.Asset(Tr(Mat.Second == scene::SecondMap::Bump ? "Normal map image" : "Second image"),
                            Mat.SecondTexture, Image);
                    // Without its image the stage writes no statement.
                    if (Mat.SecondTexture.empty())
                        F.Warning(Tr("Not written to the level until its image is chosen."));
                    if (Mat.Second == scene::SecondMap::Light)
                        F.Toggle(Tr("Reverse stages"), Mat.LightReverse);
                    if (Mat.Second == scene::SecondMap::Blend)
                        F.Int(Tr("Blend mode"), Mat.BlendMode, 1, 26, true);
                }

                // A cube map takes the second stage, which only a normal map shares with it.
                const bool CubeAllowed = Mat.Second == scene::SecondMap::None || Mat.Second == scene::SecondMap::Bump;
                Field& Cube = F.Toggle(Tr("Reflection map (cube)"), Mat.Cube.has_value(),
                                       [Target](bool On)
                                       {
                                           Target->Cube = On ? std::optional<std::array<std::string, 6>>(std::in_place)
                                                             : std::nullopt;
                                       });
                Cube.Enabled = F.Enabled && CubeAllowed;
                Cube.Tooltip = Tr("SET CUBE MAPPING ON; it combines with a bump map only");
                if (Mat.Cube)
                {
                    static constexpr const char* KFaces[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
                    bool Complete = true;
                    for (std::size_t Face = 0; Face < Mat.Cube->size(); ++Face)
                    {
                        F.Asset(KFaces[Face], (*Mat.Cube)[Face], Image);
                        Complete = Complete && !(*Mat.Cube)[Face].empty();
                    }
                    if (!Complete)
                        F.Warning(Tr("Not written to the level until its six images are chosen."));
                }

                for (std::size_t Index = 0; Index < Mat.Blends.size(); ++Index)
                {
                    scene::BlendStage& Blend = Mat.Blends[Index];
                    F.Int(Tr("Stage"), Blend.Stage, 2, 7, true);
                    F.Asset(Tr("Stage image"), Blend.Texture, Image);
                    scene::BlendStage* Stage = &Blend;
                    F.Int(Tr("Coordinate mode"), Blend.CoordinateMode, 0, 17).SetNumber = [Stage](double Number)
                    {
                        // The engine takes 0 to 3 and 10 to 17.
                        const int Mode = std::clamp(static_cast<int>(Number), 0, 17);
                        Stage->CoordinateMode = Mode > 3 && Mode < 10 ? 10 : Mode;
                    };
                    F.Int(Tr("Blend mode"), Blend.BlendMode, 1, 26, true);
                    F.Button(Tr("Remove stage"),
                             [Target, Index]
                             { Target->Blends.erase(Target->Blends.begin() + static_cast<std::ptrdiff_t>(Index)); });
                }
                if (Mat.Blends.size() < 6)
                    F.Button(Tr("Add blend stage"),
                             [Target]
                             {
                                 // The first stage from 2 that no entry holds.
                                 scene::BlendStage Blend;
                                 while (std::any_of(Target->Blends.begin(), Target->Blends.end(),
                                                    [&](const scene::BlendStage& Other)
                                                    { return Other.Stage == Blend.Stage; }))
                                     ++Blend.Stage;
                                 Target->Blends.push_back(std::move(Blend));
                             });
            }

            F.OptionalFloat("Alpha", Mat.Alpha, 100.0f, 0.5, 0.0, 100.0);
            F.Toggle(Tr("Mirror (reflection shading)"), Mat.Reflection).Tooltip =
                Tr("SET REFLECTION SHADING ON; it ends shadow casting");
        }

        static void Material(SceneEditor& E, Form& F, scene::SceneNode& Node)
        {
            scene::Material& Mat = Node.Mat;
            F.Heading("Material");
            F.Asset(Tr("Texture"), Mat.Texture, static_cast<int>(core::MediaKind::Image));
            if (Node.Kind == scene::NodeKind::Matrix && !Mat.Texture.empty())
            {
                F.Int(Tr("Image tiles across"), Node.TilesAcross, 1, 64);
                F.Int(Tr("Image tiles down"), Node.TilesDown, 1, 64);
            }
            if (scene::IdSpaceOf(Node.Kind) != scene::IdSpace::Object)
                return;

            F.OptionalColor("Colour", Mat.Color, 0xFFFFFF);
            F.Toggle(Tr("Ghost"), Mat.Ghost);
            if (Mat.Ghost)
                F.OptionalInt("Ghost mode", Mat.GhostMode, 0, 0, 5, true);

            scene::ObjectFlags& Flags = Mat.Flags;
            F.Toggle(Tr("Wireframe"), Flags.Wireframe);
            F.Int(Tr("Transparency"), Flags.Transparency, 0, 7, true).Tooltip =
                Tr("SET OBJECT transparency mode; 0 draws black as black");
            F.Toggle(Tr("Cull back faces"), Flags.Cull);
            F.Toggle(Tr("Filter"), Flags.Filter);
            F.Toggle(Tr("Lighting"), Flags.Lighting);
            F.Toggle(Tr("Fog"), Flags.Fog);
            F.Toggle(Tr("Ambient"), Flags.Ambient);
            F.Toggle(Tr("Depth buffer"), Mat.ZDepth).Tooltip =
                Tr("Unchecked writes DISABLE OBJECT ZDEPTH; the Scene view does not preview it");
            F.Toggle(Tr("Depth read"), Mat.ZRead);
            F.Toggle(Tr("Depth write"), Mat.ZWrite);

            F.OptionalColor("Diffuse", Mat.Diffuse, 0xFFFFFF);
            F.OptionalColor("Ambience", Mat.Ambience, 0xFFFFFF);
            F.OptionalColor("Emissive", Mat.Emissive, 0x000000);
            F.OptionalColor("Specular", Mat.Specular, 0xFFFFFF);
            F.OptionalFloat("Specular power", Mat.SpecularPower, 10.0f, 0.25, 0.0, 255.0);
            scene::Material* Target = &Mat;
            // Without SET SHADOW POSITION the engine casts from light 0 at the origin, which
            // lies on the ground of most levels; the first caster gets a fixed source above.
            F.Toggle(Tr("Casts a shadow"), Mat.CastShadow,
                     [&E, Target](bool On)
                     {
                         Target->CastShadow = On;
                         if (!On)
                             return;
                         // The engine keeps one of shadow casting and reflection shading.
                         Target->Reflection = false;
                         if (!E.Document_.Env.ShadowMode)
                             E.Document_.Env.ShadowMode = -1;
                     })
                .Tooltip = Tr("SET SHADOW SHADING ON, cast from the shadow source in Level Settings");
            TextureStages(E, F, Mat);
        }

        static void Glue(SceneEditor& E, Form& F, scene::SceneNode& Node)
        {
            F.Heading("Attachment");
            const auto Label = [](const scene::SceneNode& Other)
            { return Other.Name + " (" + std::to_string(Other.Id) + ")"; };
            const scene::SceneNode* Current = scene::GlueTarget(E.Document_, Node);
            std::vector<std::string> Names = {Tr("None")};
            std::vector<int> Ids = {0};
            int Choice = 0;
            for (const scene::SceneNode& Other : E.Document_.Nodes)
            {
                if (&Other == &Node || scene::IdSpaceOf(Other.Kind) != scene::IdSpace::Object)
                    continue;
                // Only what the engine accepts is offered: no object that already follows this one.
                scene::SceneNode Candidate = Node;
                Candidate.Glued = scene::Glue{Other.Id, 0, Node.Glued ? Node.Glued->Mode : 0};
                if (scene::GlueTarget(E.Document_, Candidate) == nullptr)
                    continue;
                if (Current == &Other)
                    Choice = static_cast<int>(Names.size());
                Names.push_back(Label(Other));
                Ids.push_back(Other.Id);
            }
            if (Node.Glued && Current == nullptr)
            {
                Choice = static_cast<int>(Names.size());
                Names.push_back(TrFmt("Object {0} cannot hold it", {std::to_string(Node.Glued->Object)}));
                Ids.push_back(Node.Glued->Object);
            }
            scene::SceneNode* Target = &Node;
            F.Choice(Tr("Glued to"), Names, Choice,
                     [Target, Ids](int Index)
                     {
                         if (Index <= 0 || static_cast<std::size_t>(Index) >= Ids.size())
                             Target->Glued.reset();
                         else if (!Target->Glued || Target->Glued->Object != Ids[static_cast<std::size_t>(Index)])
                             Target->Glued = scene::Glue{Ids[static_cast<std::size_t>(Index)], 0,
                                                         Target->Glued ? Target->Glued->Mode : 0};
                     })
                .Tooltip = Tr("GLUE OBJECT TO LIMB, written once every object exists");
            if (!Node.Glued)
                return;

            // A model lists its limbs; any other object has its root alone, limb 0.
            const SceneEditor::ModelInspection* Read =
                Current != nullptr && Current->Kind == scene::NodeKind::Model ? E.ModelInfoOf(Current->Asset) : nullptr;
            if (Read != nullptr && Read->Info && !Read->Info->Limbs.empty())
            {
                std::vector<std::string> Limbs;
                std::vector<int> LimbIds;
                int Chosen = 0;
                for (const scene::ModelLimb& Limb : Read->Info->Limbs)
                {
                    if (Limb.Id == Node.Glued->Limb)
                        Chosen = static_cast<int>(Limbs.size());
                    Limbs.push_back(std::to_string(Limb.Id) + "  " +
                                    (Limb.Name.empty() ? std::string(Tr("(unnamed)")) : Limb.Name));
                    LimbIds.push_back(Limb.Id);
                }
                F.Choice(Tr("Limb"), Limbs, Chosen,
                         [Target, LimbIds](int Index)
                         {
                             if (Index >= 0 && static_cast<std::size_t>(Index) < LimbIds.size())
                                 Target->Glued->Limb = LimbIds[static_cast<std::size_t>(Index)];
                         });
            }
            else
            {
                F.Int(Tr("Limb"), Node.Glued->Limb, 0, 1000000);
            }
            F.Int(Tr("Glue mode"), Node.Glued->Mode, 0, 2, true);
            F.Hint(Tr("The Scene view draws a glued object where it stands, not on the limb."));
        }

        static void CameraSettings(SceneEditor& E, Form& F)
        {
            scene::Environment& Env = E.Document_.Env;
            F.Toggle(Tr("Use a level camera"), Env.UseCamera).Tooltip =
                Tr("Writes AUTOCAM OFF and places camera 0; unchecked, the program keeps its camera");
            if (!Env.UseCamera)
                return;
            F.Vector("Camera position", &Env.CameraPosition.x, 3, 0.5);
            F.Vector("Camera rotation", &Env.CameraRotation.x, 3, 0.5);
            F.OptionalFloat("Field of view", Env.CameraFov, 75.0f, 0.25, 1.0, 179.0);
            glm::vec2 Shown(0.0f);
            F.Optional(Env.CameraRange, glm::vec2(0.1f, 5000.0f), Shown,
                       [&](glm::vec2& Range)
                       {
                           F.Float(Tr("Near"), Range.x, 0.5, 0.01, 1000000.0);
                           F.Float(Tr("Far"), Range.y, 0.5, 0.01, 1000000.0);
                       });
            F.Button(Tr("Match the viewport"),
                     [&E]
                     {
                         E.Document_.Env.CameraPosition = E.Camera_.Position;
                         E.Document_.Env.CameraRotation = glm::vec3(E.Camera_.Pitch, E.Camera_.Yaw, 0.0f);
                     });
        }

        static void Inspector(SceneEditor& E, Form& F)
        {
            if (!E.InspectedModel_.empty())
            {
                ModelDetails(E, F, E.InspectedModel_);
                return;
            }
            if (!E.HasLevel_)
                return;
            F.Enabled = E.Editable();
            if (E.CameraSelected_)
            {
                F.Heading("Level Camera");
                CameraSettings(E, F);
                return;
            }
            scene::SceneNode* Node = E.Selected();
            if (Node == nullptr)
            {
                F.Hint(Tr("Select a node in the Hierarchy or the Viewport."));
                return;
            }

            const bool IsObject = scene::IdSpaceOf(Node->Kind) == scene::IdSpace::Object;
            const bool IsLight = Node->Kind == scene::NodeKind::Light;
            const std::vector<scene::SceneNode*> Nodes = E.SelectedNodes();
            const bool Multi = Nodes.size() > 1;
            // Shape fields are shared by nodes of one kind; matrix and terrain height tools stay per node.
            const bool SharedShape =
                !Multi || (Node->Kind != scene::NodeKind::Matrix && Node->Kind != scene::NodeKind::Terrain &&
                           std::all_of(Nodes.begin(), Nodes.end(),
                                       [&](const scene::SceneNode* Other) { return Other->Kind == Node->Kind; }));
            if (Multi)
            {
                F.Heading(TrFmt("{0} nodes selected", {std::to_string(Nodes.size())}).c_str());
                F.Hint(TrFmt("Showing {0}; edits apply to each node that has the field.", {Node->Name}));
            }
            else
            {
                F.Heading(scene::NodeKindLabel(Node->Kind));
                F.Text(Tr("Name"), Node->Name);
                Field& Number = F.Int(Tr("Number"), Node->Id, 1, 1000000000);
                Number.Tooltip = Tr("The object, matrix, terrain, light or sound number the code uses");
                Number.SetNumber = [&E, Node](double Value)
                {
                    const int Id = static_cast<int>(Value);
                    if (Id <= 0 || E.Document_.Find(scene::IdSpaceOf(Node->Kind), Id) != nullptr)
                        return;
                    // What is glued to this object, and its BSP collisions, follow its new number.
                    if (scene::IdSpaceOf(Node->Kind) == scene::IdSpace::Object)
                    {
                        for (scene::SceneNode& Other : E.Document_.Nodes)
                            if (Other.Glued && Other.Glued->Object == Node->Id)
                                Other.Glued->Object = Id;
                        if (E.Document_.Bsp)
                            for (scene::BspCollision& Collision : E.Document_.Bsp->Collisions)
                                if (Collision.OfObject && Collision.Id == Node->Id)
                                    Collision.Id = Id;
                    }
                    Node->Id = Id;
                    E.Select(Node);
                };
                if (Node->Kind == scene::NodeKind::Model)
                    ModelDetails(E, F, Node->Asset);
            }

            F.Heading("Transform");
            // The one section a program's scene edits: each part has its own state, not the Inspector's.
            const bool Editable = F.Enabled;
            const auto Part = [&](scene::TransformPart Which)
            { F.Enabled = E.TransformEditable() && E.PartEditable(*Node, Which); };
            Part(scene::TransformPart::Position);
            F.Vector("Position", &Node->Position.x, 3, 0.5);
            AxisLocks(E, F, *Node, scene::TransformPart::Position);
            if (IsObject || (IsLight && Node->Light != scene::LightKind::Point))
            {
                Part(scene::TransformPart::Rotation);
                F.Vector("Rotation", &Node->Rotation.x, 3, 0.5);
                AxisLocks(E, F, *Node, scene::TransformPart::Rotation);
            }
            if (IsObject)
            {
                Part(scene::TransformPart::Scale);
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    F.Float(AxisLabel("Scale %", Axis), Node->Scale[Axis], 0.5, 0.0, 100000.0).SetNumber =
                        [&E, Node, Axis](double Value)
                    {
                        const glm::vec3 Previous = Node->Scale;
                        Node->Scale[Axis] = static_cast<float>(std::clamp(Value, 0.0, 100000.0));
                        if (E.UniformScale_)
                            Node->Scale = scene::ProportionalScale(Previous, Node->Scale);
                    };
                }
                F.Toggle(Tr("Uniform scale"), E.UniformScale_).Tooltip =
                    Tr("Scaling one axis scales the others in proportion, here and with the gizmo");
                AxisLocks(E, F, *Node, scene::TransformPart::Scale);
            }
            F.Enabled = Editable;
            if (Node->Kind == scene::NodeKind::Terrain)
                F.Hint(Tr("A terrain runs from its position toward -Z."));
            if (Node->Kind == scene::NodeKind::Matrix)
                F.Hint(Tr("A matrix runs from its position toward +X and +Z."));

            if (SharedShape)
                Shape(E, F, *Node);
            if (!scene::IsMarker(Node->Kind))
                Material(E, F, *Node);
            // One object at a time: a glue names another node, which a shared edit would hand to itself.
            if (!Multi && IsObject)
                Glue(E, F, *Node);
        }

        static void BspWorld(SceneEditor& E, Form& F)
        {
            scene::SceneDocument& Document = E.Document_;
            F.Heading("BSP world");
            F.Toggle(Tr("Load a BSP world"), Document.Bsp.has_value(),
                     [&Document](bool On)
                     { Document.Bsp = On ? std::optional<scene::BspWorld>(std::in_place) : std::nullopt; });
            if (!Document.Bsp)
                return;
            scene::BspWorld& World = *Document.Bsp;
            F.Asset(Tr("Map file"), World.Map, -1);
            F.Hint(Tr("LOAD BSP, written first; the Scene view does not draw the world."));
            F.OptionalInt("BSP camera", World.Camera, 0, 0, 1000000);

            const auto ObjectLabel = [&Document](int Id)
            {
                const scene::SceneNode* Node = Document.Find(scene::IdSpace::Object, Id);
                return Node != nullptr ? Node->Name + " (" + std::to_string(Id) + ")"
                                       : TrFmt("Object {0} is not in the level", {std::to_string(Id)});
            };
            for (std::size_t Entry = 0; Entry < World.Collisions.size(); ++Entry)
            {
                scene::BspCollision& Collision = World.Collisions[Entry];
                scene::BspCollision* Target = &Collision;
                F.Heading(TrFmt("Collision {0}", {std::to_string(Entry + 1)}).c_str());
                F.Int(Tr("Collision index"), Collision.Index, 1, 24, true);
                F.Choice(Tr("Collides"), {Tr("Camera"), Tr("Object")}, Collision.OfObject ? 1 : 0,
                         [Target](int Kind)
                         {
                             Target->OfObject = Kind == 1;
                             Target->Id = 0;
                         });
                if (Collision.OfObject)
                {
                    std::vector<std::string> Names;
                    std::vector<int> Ids;
                    int Current = -1;
                    for (const scene::SceneNode& Node : Document.Nodes)
                    {
                        if (scene::IdSpaceOf(Node.Kind) != scene::IdSpace::Object)
                            continue;
                        if (Node.Id == Collision.Id)
                            Current = static_cast<int>(Names.size());
                        Names.push_back(ObjectLabel(Node.Id));
                        Ids.push_back(Node.Id);
                    }
                    if (Current < 0)
                    {
                        Current = static_cast<int>(Names.size());
                        Names.push_back(ObjectLabel(Collision.Id));
                        Ids.push_back(Collision.Id);
                    }
                    F.Choice(Tr("Object"), Names, Current,
                             [Target, Ids](int Index)
                             {
                                 if (Index >= 0 && static_cast<std::size_t>(Index) < Ids.size())
                                     Target->Id = Ids[static_cast<std::size_t>(Index)];
                             });
                }
                else
                {
                    F.Int(Tr("Camera"), Collision.Id, 0, 1000000);
                }
                F.Float(Tr("Radius"), Collision.Radius, 0.25, 0.01, 100000.0);
                F.Choice(Tr("Response"), {Tr("Slide"), Tr("Go back"), Tr("Record the hit only")},
                         std::clamp(Collision.Response, 0, 2), [Target](int Response) { Target->Response = Response; });
                if (!scene::BspCollisionValid(Document, Collision))
                    F.Warning(Tr("Not written: the index is used above or the object is missing."));
                F.Button(Tr("Remove collision"),
                         [&Document, Entry]
                         {
                             Document.Bsp->Collisions.erase(Document.Bsp->Collisions.begin() +
                                                            static_cast<std::ptrdiff_t>(Entry));
                         });
            }
            if (World.Collisions.size() < 24)
                F.Button(Tr("Add BSP collision"),
                         [&Document]
                         {
                             // The first index no entry holds.
                             scene::BspCollision Collision;
                             std::vector<scene::BspCollision>& All = Document.Bsp->Collisions;
                             while (std::any_of(All.begin(), All.end(), [&](const scene::BspCollision& Other)
                                                { return Other.Index == Collision.Index; }))
                                 ++Collision.Index;
                             All.push_back(Collision);
                         });
        }

        static void Settings(SceneEditor& E, Form& F)
        {
            if (!E.HasLevel_)
                return;
            F.Enabled = E.Editable();
            scene::SceneDocument& Document = E.Document_;
            scene::Environment& Env = Document.Env;

            F.Heading("Level");
            F.Hint(dbc::GenericPathToUtf8(scene::LevelRelativePath(Document.Name)));
            F.Hint(TrFmt("Function {0}()", {scene::LevelFunctionName(Document)}));
            F.Heading("Numbers");
            static constexpr const char* KLabels[] = {"First object", "First image", "First matrix", "First terrain",
                                                      "First light",  "First sound", "First effect"};
            for (std::size_t Space = 0; Space < scene::KIdSpaceCount; ++Space)
                F.Int(Tr(KLabels[Space]), Document.IdBase[Space], 1, 1000000000);
            F.Hint(Tr("New nodes take free numbers from these, away from the program's own."));
            BspWorld(E, F);

            F.Heading("World Settings");
            F.Hint(dbc::GenericPathToUtf8(scene::WorldRelativePath(Document.Name)));
            F.Hint(TrFmt("Function {0}()", {scene::WorldFunctionName(Document)}));

            F.Heading("Light and colour");
            F.OptionalInt("Ambient %", Env.AmbientPercent, 25, 0, 100, true);
            F.OptionalColor("Ambient colour", Env.AmbientColor, 0xFFFFFF);
            F.OptionalColor("Backdrop", Env.Backdrop, 0x000000);
            F.Toggle(Tr("Hide light 0"), Env.DefaultLightHidden);
            if (!Env.DefaultLightHidden)
            {
                glm::vec3 Shown(0.0f);
                F.Optional(Env.DefaultLightRotation, glm::vec3(45.0f, 0.0f, 0.0f), Shown,
                           [&](glm::vec3& Rotation) { F.Vector("Light 0 rotation", &Rotation.x, 3, 0.5); });
                F.OptionalColor("Light 0 colour", Env.DefaultLightColor, 0xFFFFFF);
            }

            F.Heading("Fog");
            F.Toggle(Tr("Fog"), Env.Fog);
            if (Env.Fog)
            {
                F.OptionalColor("Fog colour", Env.FogColor, 0x000000);
                F.OptionalFloat("Fog distance", Env.FogDistance, 3000.0f, 5.0, 0.0, 1000000.0);
            }

            F.Heading("Shadows");
            F.Toggle(Tr("Shadow source"), Env.ShadowMode.has_value(),
                     [&Env](bool On) { Env.ShadowMode = On ? std::optional<int>(-1) : std::nullopt; })
                .Tooltip = Tr("SET SHADOW POSITION; objects cast with Casts a shadow in their material");
            if (Env.ShadowMode)
            {
                // Light 0 and missing lights sit at the origin, on the ground of most
                // levels, where every volume lies along the floor; the labels say so.
                const auto Label = [&](int Mode)
                {
                    if (Mode == -1)
                        return std::string(Tr("Fixed position"));
                    const scene::SceneNode* Light = Mode > 0 ? Document.Find(scene::IdSpace::Light, Mode) : nullptr;
                    if (Light != nullptr)
                        return TrFmt("Light {0}: {1}", {std::to_string(Mode), Light->Name});
                    return TrFmt("Light {0} (at the origin)", {std::to_string(Mode)});
                };
                std::vector<int> Modes = {-1, 0};
                for (const scene::SceneNode& Node : Document.Nodes)
                    if (Node.Kind == scene::NodeKind::Light)
                        Modes.push_back(Node.Id);
                if (std::find(Modes.begin(), Modes.end(), *Env.ShadowMode) == Modes.end())
                    Modes.push_back(*Env.ShadowMode);
                std::vector<std::string> Names;
                int Current = 0;
                for (std::size_t Index = 0; Index < Modes.size(); ++Index)
                {
                    Names.push_back(Label(Modes[Index]));
                    if (Modes[Index] == *Env.ShadowMode)
                        Current = static_cast<int>(Index);
                }
                F.Choice(Tr("Cast from"), Names, Current,
                         [&Env, Modes](int Index)
                         {
                             if (Index >= 0 && static_cast<std::size_t>(Index) < Modes.size())
                                 Env.ShadowMode = Modes[static_cast<std::size_t>(Index)];
                         });
                if (*Env.ShadowMode == -1)
                    F.Vector("Shadow position", &Env.ShadowPosition.x, 3, 0.5);
            }

            F.Heading("Camera");
            CameraSettings(E, F);

            F.Heading("Display");
            if (E.Project_ && E.Project_->HasDescriptor)
                F.Hint(Tr("These statements run after the .dbpro project's own display settings."));
            scene::DisplaySettings& Display = Document.Display;
            glm::ivec3 ShownMode(0);
            F.Optional(Display.DisplayMode, glm::ivec3(800, 600, 32), ShownMode,
                       [&](glm::ivec3& Mode)
                       {
                           F.Int(Tr("Display width"), Mode.x, 1, 100000);
                           F.Int(Tr("Display height"), Mode.y, 1, 100000);
                           F.Int(Tr("Display depth"), Mode.z, 1, 64);
                       });
            if (Display.DisplayMode)
                F.Toggle(Tr("Vertical sync"), Display.Vsync.value_or(0) != 0,
                         [&Display](bool On) { Display.Vsync = On ? 1 : 0; });
            F.Choice(Tr("Window"), {Tr("Program default"), Tr("Windowed"), Tr("Full screen")},
                     Display.Windowed ? (*Display.Windowed ? 1 : 2) : 0,
                     [&Display](int Window)
                     { Display.Windowed = Window == 0 ? std::nullopt : std::optional<bool>(Window == 1); });
            glm::ivec2 ShownSize(0);
            F.Optional(Display.WindowSize, glm::ivec2(800, 600), ShownSize,
                       [&](glm::ivec2& Size)
                       {
                           F.Int(Tr("Window width"), Size.x, 1, 100000);
                           F.Int(Tr("Window height"), Size.y, 1, 100000);
                       });
            std::string ShownTitle;
            F.Optional(Display.WindowTitle, std::string("My Game"), ShownTitle,
                       [&](std::string& Title) { F.Text(Tr("Title"), Title); });
            F.Toggle("SYNC ON", Display.SyncOn);
            F.OptionalInt("Sync rate", Display.SyncRate, 60, 1, 1000);
            F.Toggle(Tr("Hide the mouse"), Display.HideMouse);
        }

        static void Paint(SceneEditor& E, Form& F)
        {
            // Copies of one object appear once.
            std::vector<const scene::SceneNode*> Looks;
            std::vector<std::string> Names = {Tr("Choose an object")};
            for (const scene::SceneNode& Node : E.Document_.Nodes)
            {
                if (scene::IdSpaceOf(Node.Kind) != scene::IdSpace::Object ||
                    std::any_of(Looks.begin(), Looks.end(),
                                [&](const scene::SceneNode* Other) { return scene::SameLook(*Other, Node); }))
                    continue;
                Looks.push_back(&Node);
                Names.push_back(Node.Painted ? scene::PaintedStem(Node.Name) : Node.Name);
            }
            std::vector<std::string> Models;
            if (E.Project_)
            {
                for (const core::MediaFile& File : E.Assets().Files)
                    if (File.Kind == core::MediaKind::Model)
                        Models.push_back(File.RelativeName);
            }
            int Current = 0;
            if (E.PaintObject_)
            {
                for (std::size_t Index = 0; Index < Looks.size(); ++Index)
                    if (scene::SameLook(*Looks[Index], *E.PaintObject_))
                        Current = static_cast<int>(Index) + 1;
                if (Current == 0)
                {
                    for (std::size_t Index = 0; Index < Models.size(); ++Index)
                        if (Models[Index] == E.PaintObject_->Asset)
                            Current = static_cast<int>(1 + Looks.size() + Index);
                }
            }
            for (const std::string& Model : Models)
                Names.push_back(Model);
            std::vector<int> LookIds;
            for (const scene::SceneNode* Look : Looks)
                LookIds.push_back(Look->Id);
            F.Choice(Tr("Object"), Names, Current,
                     [&E, LookIds, Models](int Index)
                     {
                         if (Index <= 0)
                             return;
                         const std::size_t Entry = static_cast<std::size_t>(Index - 1);
                         if (Entry < LookIds.size())
                         {
                             if (const scene::SceneNode* Node = E.Document_.Find(scene::IdSpace::Object, LookIds[Entry]))
                                 E.SetPaintObject(*Node);
                         }
                         else if (Entry - LookIds.size() < Models.size())
                         {
                             E.SetPaintModel(Models[Entry - LookIds.size()]);
                         }
                     })
                .Tooltip = Tr("What to place. Entering the tool takes the selected object.\n"
                              "Click to place; hold Shift to erase its painted copies within the radius");
            F.Choice(Tr("Mode"), {Tr("One"), Tr("Several"), Tr("Brush")}, static_cast<int>(E.PaintMode_),
                     [&E](int Mode) { E.PaintMode_ = static_cast<SceneEditor::PaintMode>(Mode); });
            if (E.PaintMode_ == SceneEditor::PaintMode::Several)
                F.Int(Tr("Count"), E.PaintCount_, 1, 200);
            F.Float(Tr("Radius"), E.PaintRadius_, 0.5, 1.0, 2000.0);
            if (E.PaintMode_ == SceneEditor::PaintMode::Brush)
                F.Float(Tr("Density"), E.PaintDensity_, 0.1, 0.1, 500.0).Tooltip = Tr("Objects per 100 x 100 units");
            F.Toggle(Tr("Random turn"), E.PaintTurn_);
            F.Toggle(Tr("Random scale"), E.PaintRandomScale_);
            if (E.PaintRandomScale_)
            {
                F.Float(Tr("Smallest scale %"), E.PaintScale_.x, 0.5, 1.0, 1000.0).SetNumber = [&E](double Value)
                {
                    E.PaintScale_.x = static_cast<float>(std::clamp(Value, 1.0, 1000.0));
                    E.PaintScale_.y = std::max(E.PaintScale_.x, E.PaintScale_.y);
                };
                F.Float(Tr("Largest scale %"), E.PaintScale_.y, 0.5, 1.0, 1000.0).SetNumber = [&E](double Value)
                { E.PaintScale_.y = std::max(E.PaintScale_.x, static_cast<float>(std::clamp(Value, 1.0, 1000.0))); };
            }
        }

        static void SnapSteps(SceneEditor& E, Form& F)
        {
            F.Float(Tr("Move"), E.MoveSnap_, 0.1, scene::KMinGridStep, scene::KMaxGridStep);
            F.Float(Tr("Rotate (degrees)"), E.RotateSnap_, 0.5, 0.1, 180.0);
            F.Float(Tr("Scale (%)"), E.ScaleSnap_, 0.5, 0.1, 1000.0);
        }

        static void Build(SceneEditor& E, DBCForm Which, Form& F)
        {
            switch (Which)
            {
                case DBCFormInspector:
                    Inspector(E, F);
                    break;
                case DBCFormLevelSettings:
                    Settings(E, F);
                    break;
                case DBCFormPaint:
                    Paint(E, F);
                    break;
                case DBCFormSnapSteps:
                    SnapSteps(E, F);
                    break;
            }
        }

        // Runs one edit of a form's field. The form is built again first: its rows point into the
        // document, which may have changed since the interface last read them.
        static void Edit(SceneEditor& E, DBCForm Which, NSInteger FieldIndex, bool Commit,
                         const std::function<void(Field&)>& Apply)
        {
            Session& S = TheSession();
            Form F;
            Build(E, Which, F);
            if (FieldIndex < 0 || static_cast<std::size_t>(FieldIndex) >= F.Fields.size())
                return;
            Field& Row = F.Fields[static_cast<std::size_t>(FieldIndex)];
            if (!Row.Enabled)
                return;
            const bool InInspector = Which == DBCFormInspector && E.InspectedModel_.empty() && !E.CameraSelected_;
            scene::SceneNode* Node = InInspector ? E.Selected() : nullptr;
            const std::vector<scene::SceneNode*> Nodes = Node != nullptr ? E.SelectedNodes() : std::vector<scene::SceneNode*>{};
            std::optional<scene::SceneNode> Before;
            if (Node != nullptr)
                Before = *Node;
            Apply(Row);
            // The selection is found again: an edit may have renumbered the node.
            Node = InInspector ? E.Selected() : nullptr;
            if (Node != nullptr && Before)
            {
                if (!scene::IsMarker(Node->Kind))
                    scene::ResolveMaterial(Node->Mat);
                // Several nodes: what the edit changed is copied to the others, where it applies.
                if (Nodes.size() > 1 && *Node != *Before)
                    for (scene::SceneNode* Other : Nodes)
                        if (Other != Node)
                            scene::ApplySharedEdit(*Before, *Node, *Other);
            }
            E.EnforceCaptureLocks();
            S.FormEditing = !Commit;
            if (Commit && E.TransformEditable())
                E.Commit();
        }

        static NSString* NodeKey(const scene::SceneNode& Node)
        {
            return [NSString stringWithFormat:@"n/%d/%d", static_cast<int>(scene::IdSpaceOf(Node.Kind)), Node.Id];
        }

        static DBCSceneRow* NodeRow(SceneEditor& E, const scene::SceneNode& Node)
        {
            DBCSceneRow* Row = [DBCSceneRow new];
            Row.key = NodeKey(Node);
            Row.name = ToNs(Node.Name);
            Row.kind = static_cast<NSInteger>(Node.Kind);
            Row.hidden = Node.Hidden;
            Row.selected = E.IsSelected(Node);
            Row.tooltip = ToNs(std::string(Tr(scene::NodeKindLabel(Node.Kind))) + " " + std::to_string(Node.Id));
            return Row;
        }

        static NSArray<DBCSceneRow*>* Rows(SceneEditor& E)
        {
            NSMutableArray<DBCSceneRow*>* Out = [NSMutableArray array];
            if (!E.HasLevel_)
                return Out;
            // Painted objects fold into one row per look, where the first of them sits.
            std::vector<std::vector<const scene::SceneNode*>> Groups;
            for (const scene::SceneNode& Node : E.Document_.Nodes)
            {
                if (!Node.Painted)
                    continue;
                const auto Found = std::find_if(Groups.begin(), Groups.end(), [&](const auto& Group)
                                                { return scene::SameLook(*Group.front(), Node); });
                if (Found != Groups.end())
                    Found->push_back(&Node);
                else
                    Groups.push_back({&Node});
            }
            for (const scene::SceneNode& Node : E.Document_.Nodes)
            {
                if (!Node.Painted)
                {
                    [Out addObject:NodeRow(E, Node)];
                    continue;
                }
                const auto Group = std::find_if(Groups.begin(), Groups.end(),
                                                [&](const auto& Other) { return Other.front() == &Node; });
                if (Group == Groups.end())
                    continue;
                DBCSceneRow* Row = [DBCSceneRow new];
                Row.key = [NSString stringWithFormat:@"g/%d", Node.Id];
                Row.name = ToNs(scene::PaintedStem(Node.Name) + " (" + std::to_string(Group->size()) + ")");
                Row.tooltip = @"";
                Row.kind = -2;
                Row.selected = std::all_of(Group->begin(), Group->end(), [&](const scene::SceneNode* Member)
                                           { return E.IsSelected(*Member); });
                NSMutableArray<DBCSceneRow*>* Children = [NSMutableArray arrayWithCapacity:Group->size()];
                for (const scene::SceneNode* Member : *Group)
                    [Children addObject:NodeRow(E, *Member)];
                Row.children = Children;
                [Out addObject:Row];
            }
            if (E.Document_.Env.UseCamera)
            {
                DBCSceneRow* Row = [DBCSceneRow new];
                Row.key = @"camera";
                Row.name = ToNs(Tr("Level Camera"));
                Row.tooltip = @"";
                Row.kind = -1;
                Row.selected = E.CameraSelected_;
                [Out addObject:Row];
            }
            return Out;
        }

        static void SelectRow(SceneEditor& E, NSString* Key, bool Toggle, bool Range)
        {
            if ([Key isEqualToString:@"camera"])
            {
                E.Select(nullptr);
                E.CameraSelected_ = true;
                return;
            }
            NSArray<NSString*>* Parts = [Key componentsSeparatedByString:@"/"];
            if (Parts.count < 2)
                return;
            if ([Parts[0] isEqualToString:@"g"])
            {
                const scene::SceneNode* First = E.Document_.Find(scene::IdSpace::Object, Parts[1].intValue);
                if (First == nullptr)
                    return;
                // A tap selects the whole group, to move, edit or delete it together.
                E.InspectedModel_.clear();
                if (!Toggle)
                    E.Select(nullptr);
                E.CameraSelected_ = false;
                for (const scene::SceneNode& Node : E.Document_.Nodes)
                    if (Node.Painted && scene::SameLook(*First, Node))
                        E.Selection_.push_back(SceneEditor::Selection{scene::IdSpaceOf(Node.Kind), Node.Id});
                return;
            }
            if (Parts.count < 3)
                return;
            const scene::SceneNode* Node =
                E.Document_.Find(static_cast<scene::IdSpace>(Parts[1].intValue), Parts[2].intValue);
            if (Node == nullptr)
                return;
            const scene::SceneNode* Anchor = E.Selected();
            if (Toggle)
            {
                E.ToggleSelect(*Node);
            }
            else if (Range && Anchor != nullptr)
            {
                // The rows from the active node to this one, which becomes active.
                const std::size_t From = static_cast<std::size_t>(Anchor - E.Document_.Nodes.data());
                const std::size_t To = static_cast<std::size_t>(Node - E.Document_.Nodes.data());
                E.Select(Anchor);
                for (std::size_t Index = From; Index != To;)
                {
                    Index = To > From ? Index + 1 : Index - 1;
                    const scene::SceneNode& Row = E.Document_.Nodes[Index];
                    E.Selection_.push_back(SceneEditor::Selection{scene::IdSpaceOf(Row.Kind), Row.Id});
                }
            }
            else
            {
                E.Select(Node);
            }
        }

        static void SelectAll(SceneEditor& E)
        {
            E.Select(nullptr);
            for (const scene::SceneNode& Node : E.Document_.Nodes)
                E.Selection_.push_back(SceneEditor::Selection{scene::IdSpaceOf(Node.Kind), Node.Id});
        }

        static glm::vec3 Ground(SceneEditor& E, CGPoint Point)
        {
            // Dropped on the ground plane, or in front of the camera when looking up.
            const scene::Ray Ray = E.RayAt(ImVec2(static_cast<float>(Point.x), static_cast<float>(Point.y)));
            if (Ray.Direction.y < -0.001f)
                return Ray.Origin + Ray.Direction * (-Ray.Origin.y / Ray.Direction.y);
            return Ray.Origin + Ray.Direction * E.Camera_.Distance;
        }

        static void DropAsset(SceneEditor& E, const std::string& Relative, CGPoint Point)
        {
            if (!E.Editable())
                return;
            const std::optional<core::MediaKind> Kind = core::ClassifyMedia(Relative);
            if (Kind == core::MediaKind::Image)
            {
                E.PickAt(ImVec2(static_cast<float>(Point.x), static_cast<float>(Point.y)));
                if (scene::SceneNode* Node = E.Selected(); Node != nullptr && !scene::IsMarker(Node->Kind))
                {
                    Node->Mat.Texture = Relative;
                    E.Commit();
                }
            }
            else if (Kind == core::MediaKind::Model || Kind == core::MediaKind::Audio)
            {
                E.CreateFromAsset(Kind == core::MediaKind::Model ? scene::NodeKind::Model : scene::NodeKind::Sound,
                                  Relative, Ground(E, Point));
            }
        }

        static void ActivateAsset(SceneEditor& E, const std::string& Relative)
        {
            if (!E.Editable())
                return;
            const std::optional<core::MediaKind> Kind = core::ClassifyMedia(Relative);
            scene::SceneNode* Node = E.Selected();
            if (Kind == core::MediaKind::Image && Node != nullptr && !scene::IsMarker(Node->Kind))
            {
                // A selected terrain without a heightmap takes the image as one.
                if (Node->Kind == scene::NodeKind::Terrain && Node->Asset.empty())
                    Node->Asset = Relative;
                else
                    Node->Mat.Texture = Relative;
                E.Commit();
            }
            else if (Kind == core::MediaKind::Model)
            {
                E.CreateFromAsset(scene::NodeKind::Model, Relative, std::nullopt);
            }
            else if (Kind == core::MediaKind::Audio && Node != nullptr && Node->Kind == scene::NodeKind::Sound)
            {
                Node->Asset = Relative;
                E.Commit();
            }
            else if (Kind == core::MediaKind::Audio)
            {
                E.CreateFromAsset(scene::NodeKind::Sound, Relative, std::nullopt);
            }
        }

        // What the interface shows of the editor: when it differs from the last frame's, the
        // Hierarchy and the Inspector are read again.
        struct Shown
        {
            scene::SceneDocument Document;
            std::vector<std::uint64_t> Selection;
            bool Camera = false;
            bool HasLevel = false;
            int Tool = 0;
            float MoveSnap = 0.0f;
            bool CanUndo = false;
            bool CanRedo = false;

            bool operator==(const Shown&) const = default;
        };

        static bool TakeChange(SceneEditor& E)
        {
            static Shown Last;
            // A drag changes the document every frame: the panels follow once it ends.
            if (E.GizmoActive_ || E.Painting_ || E.VertexDragging_)
                return false;
            std::vector<std::uint64_t> Selection;
            Selection.reserve(E.Selection_.size());
            for (const SceneEditor::Selection& Entry : E.Selection_)
                Selection.push_back(Entry.Key());
            const bool Same = Last.Selection == Selection && Last.Camera == E.CameraSelected_ &&
                              Last.HasLevel == E.HasLevel_ && Last.Tool == static_cast<int>(E.Tool_) &&
                              Last.MoveSnap == E.MoveSnap_ && Last.CanUndo == E.History_.CanUndo() &&
                              Last.CanRedo == E.History_.CanRedo() && Last.Document == E.Document_;
            if (Same)
                return false;
            Last.Document = E.Document_;
            Last.Selection = std::move(Selection);
            Last.Camera = E.CameraSelected_;
            Last.HasLevel = E.HasLevel_;
            Last.Tool = static_cast<int>(E.Tool_);
            Last.MoveSnap = E.MoveSnap_;
            Last.CanUndo = E.History_.CanUndo();
            Last.CanRedo = E.History_.CanRedo();
            return true;
        }

        // What the Objective-C side reads and calls, which is not a friend of the editor.
        static const std::vector<std::string>& ReadErrors(SceneEditor& E) { return E.ReadErrors_; }
        static std::vector<std::string> LevelNames(SceneEditor& E) { return E.LevelNames(); }
        static void OpenLevel(SceneEditor& E, const std::string& Name) { E.OpenLevel(Name); }
        static bool NewLevel(SceneEditor& E, const std::string& Name) { return E.NewLevel(Name); }
        static bool Editable(SceneEditor& E) { return E.Editable(); }
        static bool HasSelection(SceneEditor& E) { return !E.Selection_.empty() || E.CameraSelected_; }
        static bool CanUndo(SceneEditor& E) { return E.TransformEditable() && E.History_.CanUndo(); }
        static bool CanRedo(SceneEditor& E) { return E.TransformEditable() && E.History_.CanRedo(); }
        static void Undo(SceneEditor& E) { E.Undo(); }
        static void Redo(SceneEditor& E) { E.Redo(); }
        static void Duplicate(SceneEditor& E) { E.DuplicateSelected(); }
        static void Delete(SceneEditor& E) { E.DeleteSelected(); }
        static void ClearSelection(SceneEditor& E) { E.Select(nullptr); }
        static void Frame(SceneEditor& E) { E.FrameSelection(); }
        static void Align(SceneEditor& E) { E.AlignSelectionToGrid(); }
        static void Create(SceneEditor& E, scene::NodeKind Kind, scene::LightKind Light, std::optional<glm::vec3> At)
        {
            E.Create(Kind, Light, At);
        }
        static void CreateFromAsset(SceneEditor& E, scene::NodeKind Kind, const std::string& Relative)
        {
            E.CreateFromAsset(Kind, Relative, std::nullopt);
        }
        static void ToggleLevelCamera(SceneEditor& E)
        {
            if (!E.Editable())
                return;
            E.Document_.Env.UseCamera = !E.Document_.Env.UseCamera;
            E.Commit();
        }
        static bool UsesLevelCamera(SceneEditor& E) { return E.Document_.Env.UseCamera; }
        static int Tool(SceneEditor& E) { return static_cast<int>(E.Tool_); }
        static void SetTool(SceneEditor& E, int Tool)
        {
            E.Tool_ = static_cast<SceneEditor::Tool>(Tool);
            if (E.Tool_ == SceneEditor::Tool::Paint)
                E.EnterPaintTool();
        }
        static bool& LocalGizmo(SceneEditor& E) { return E.LocalGizmo_; }
        static bool& Snap(SceneEditor& E) { return E.Snap_; }
        static bool& Grid(SceneEditor& E) { return E.ShowGrid_; }
        static bool CameraView(SceneEditor& E) { return E.LookThroughCamera_; }
        static void SetCameraView(SceneEditor& E, bool On) { E.LookThroughCamera_ = On && E.Document_.Env.UseCamera; }
        static float MoveStep(SceneEditor& E) { return E.MoveSnap_; }
        static const std::string& InspectedModel(SceneEditor& E) { return E.InspectedModel_; }
        static void SetInspectedModel(SceneEditor& E, const std::string& Model)
        {
            if (!Model.empty())
                E.Select(nullptr);
            E.InspectedModel_ = Model;
        }
        static void AddFlySteer(SceneEditor& E, float X, float Y) { E.FlySteer_ += glm::vec2(X, Y); }
        // The renderer's colour target at the viewport's size, or nullptr before it has drawn.
        static SDL_GPUTexture* Target(SceneEditor& E, int Width, int Height)
        {
            return E.RendererReady_ ? E.Renderer_.Target(Width, Height) : nullptr;
        }
        static glm::vec2 ViewportSize(SceneEditor& E) { return glm::vec2(E.ViewportSize_.x, E.ViewportSize_.y); }

        static void DrawViewport(SceneEditor& E)
        {
            // Only a viewport that draws keeps flying.
            E.Flying_ = false;
            E.ShowViewportBar_ = false;
            E.DrawViewport();
        }

        static void EndFrame(SceneEditor& E, bool FormEditing)
        {
            E.EnforceCaptureLocks();
            if (!FormEditing)
                E.CommitIfIdle();
        }
    };
} // namespace dbc::studio::panels

using Access = dbc::studio::panels::SceneEditorAccess;

namespace
{
    panels::SceneHost MakeSceneHost()
    {
        panels::SceneHost Host;
        Host.ReadSource = [](const std::filesystem::path& Path) { return ReadSource(Path); };
        Host.WriteSource = [](const std::filesystem::path& Path, const std::string& Text, bool Generated)
        { return WriteSource(Path, Text, Generated); };
        Host.Log = [](const std::string& Message, bool Error)
        { AppendOutput(Message, Error ? DBCSeverityError : DBCSeverityNotice); };
        // A sandboxed Studio has no separate process to read a program's scene with.
        Host.CaptureProgram = [](const std::filesystem::path&, int) {};
        Host.ShowSource = [](const std::filesystem::path& Path, std::uint32_t Line)
        {
            OpenFile(Path);
            [TheStudio().delegate studioGoToLine:static_cast<NSInteger>(Line)];
            [TheStudio().delegate studioShowCodeMode];
        };
        Host.OpenSource = [](const std::filesystem::path& Path)
        { return static_cast<bool>([TheStudio().delegate studioOpenInEditor:ToNs(Path)]); };
        Host.CaptureRunning = [] { return false; };
        Host.FilesMoved = [] { [TheStudio().delegate studioProjectChanged]; };
        return Host;
    }

    void EnsureSceneHost()
    {
        static bool Ready = false;
        if (Ready)
            return;
        Ready = true;
        TheSession().Scene.Initialize(TheSession().Gpu, MakeSceneHost());
    }

    DBCField* ToBridge(const Field& Value, NSInteger Index)
    {
        DBCField* Out = [DBCField new];
        Out.identifier = Index;
        Out.kind = static_cast<DBCFieldKind>(Value.Kind);
        Out.label = ToNs(Value.Label);
        Out.tooltip = ToNs(Value.Tooltip);
        Out.enabled = Value.Enabled;
        Out.optional = Value.Optional;
        Out.isSet = Value.Set;
        Out.number = Value.Number;
        Out.minimum = Value.Min;
        Out.maximum = Value.Max;
        Out.step = Value.Step;
        Out.integer = Value.Integer;
        Out.slider = Value.Slider;
        Out.text = ToNs(Value.Text);
        Out.flag = Value.Flag;
        Out.color = Value.Color;
        NSMutableArray<NSString*>* Choices = [NSMutableArray arrayWithCapacity:Value.Choices.size()];
        for (const std::string& Choice : Value.Choices)
            [Choices addObject:ToNs(Choice)];
        Out.choices = Choices;
        Out.choice = Value.Choice;
        Out.assetKind = Value.AssetKind;
        return Out;
    }
} // namespace

namespace dbc::studio::swiftui
{
    void UpdateSceneProject()
    {
        Session& S = TheSession();
        EnsureSceneHost();
        if (!SceneBlocker().empty())
        {
            S.Scene.SetProject(std::nullopt);
            if (S.SceneMode)
            {
                S.SceneMode = false;
                [TheStudio().delegate studioShowCodeMode];
            }
            return;
        }
        panels::SceneProject Project;
        Project.Root = S.Resolved->ProjectRoot;
        Project.AssetRoot = S.Resolved->AssetRoot.empty() ? S.Resolved->ProjectRoot : S.Resolved->AssetRoot;
        Project.EntrySource = S.Resolved->EntrySource;
        Project.HasDescriptor = S.Resolved->DescriptorPath.has_value();
        S.Scene.SetProject(std::move(Project));
    }

    bool EnsurePresentation()
    {
        Session& S = TheSession();
        if (S.ImGuiReady)
            return true;
        if (S.ViewWindow == nullptr)
        {
            // Never shown: only its Metal view is used, inside the interface.
            S.ViewWindow =
                SDL_CreateWindow("OpenDBC Scene", 1024, 768, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
            if (S.ViewWindow == nullptr)
            {
                AppendOutput(std::string("SDL_CreateWindow: ") + SDL_GetError(), DBCSeverityError);
                return false;
            }
        }
        if (S.Gpu == nullptr)
        {
            S.Gpu = SDL_CreateGPUDevice(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
            if (S.Gpu == nullptr || !SDL_ClaimWindowForGPUDevice(S.Gpu, S.ViewWindow) ||
                !SDL_SetGPUSwapchainParameters(S.Gpu, S.ViewWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                               SDL_GPU_PRESENTMODE_VSYNC))
            {
                AppendOutput(std::string("The Scene viewport cannot start: ") + SDL_GetError(), DBCSeverityError);
                return false;
            }
        }
        Theme::AddFonts();
        ImGui_ImplSDLGPU3_InitInfo Init = {};
        Init.Device = S.Gpu;
        Init.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(S.Gpu, S.ViewWindow);
        Init.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        Init.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
        Init.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
        if (!ImGui_ImplSDLGPU3_Init(&Init))
        {
            AppendOutput("ImGui_ImplSDLGPU3_Init failed", DBCSeverityError);
            return false;
        }
        S.Scene.Initialize(S.Gpu, MakeSceneHost());
        S.ImGuiReady = true;
        return true;
    }
} // namespace dbc::studio::swiftui

@implementation DBCStudio (Scene)

- (NSString*)sceneBlocker
{
    const std::string Blocker = SceneBlocker();
    return Blocker.empty() ? @"" : ToNs(Tr(Blocker.c_str()));
}

- (BOOL)enterSceneMode
{
    Session& S = TheSession();
    const std::string Blocker = SceneBlocker();
    if (!Blocker.empty())
    {
        SetStatus(Tr(Blocker.c_str()));
        return NO;
    }
    EnsureSceneHost();
    S.SceneMode = true;
    S.Scene.Activate();
    return YES;
}

- (void)leaveSceneMode
{
    TheSession().SceneMode = false;
}

- (void)runScene
{
    Session& S = TheSession();
    if (S.Busy || !S.Resolved)
        return;
    if (S.Scene.Dirty() && !S.Scene.Save())
        return;
    OpenFile(S.Resolved->EntrySource);
    [self runCurrentDocument];
}

- (NSString*)levelName
{
    return ToNs(TheSession().Scene.LevelName());
}

- (NSArray<NSString*>*)levelNames
{
    NSMutableArray<NSString*>* Out = [NSMutableArray array];
    if (TheSession().Scene.Project())
    {
        for (const std::string& Name : Access::LevelNames(TheSession().Scene))
            [Out addObject:ToNs(Name)];
    }
    return Out;
}

- (BOOL)sceneDirty
{
    return TheSession().Scene.Dirty();
}

- (BOOL)sceneEditable
{
    return Access::Editable(TheSession().Scene);
}

- (NSArray<NSString*>*)sceneReadErrors
{
    NSMutableArray<NSString*>* Out = [NSMutableArray array];
    for (const std::string& Error : Access::ReadErrors(TheSession().Scene))
        [Out addObject:ToNs(Error)];
    return Out;
}

- (void)openLevel:(NSString*)name
{
    panels::SceneEditor& E = TheSession().Scene;
    if (!E.Project())
        return;
    if (E.Dirty())
        E.Save();
    Access::OpenLevel(E, FromNs(name));
    NotifySceneChanged();
}

- (void)newLevel:(NSString*)name
{
    panels::SceneEditor& E = TheSession().Scene;
    if (!E.Project())
        return;
    if (E.Dirty())
        E.Save();
    // Asked for by name, it should exist from here on, unlike the level Activate() starts.
    if (Access::NewLevel(E, FromNs(name)))
        E.Save();
    NotifySceneChanged();
}

- (BOOL)saveScene
{
    const bool Saved = TheSession().Scene.SaveAndRefresh();
    if (Saved)
        SetStatus(Tr("Level saved"));
    NotifySceneChanged();
    return Saved;
}

- (NSArray<DBCSceneRow*>*)sceneRows
{
    return Access::Rows(TheSession().Scene);
}

- (void)selectSceneRow:(NSString*)key toggle:(BOOL)toggle range:(BOOL)range
{
    Access::SelectRow(TheSession().Scene, key, toggle, range);
    NotifySceneChanged();
}

- (void)clearSceneSelection
{
    Access::ClearSelection(TheSession().Scene);
    NotifySceneChanged();
}

- (void)frameSceneSelection
{
    Access::Frame(TheSession().Scene);
}

- (BOOL)sceneHasSelection
{
    return Access::HasSelection(TheSession().Scene);
}

- (BOOL)sceneCanUndo
{
    return Access::CanUndo(TheSession().Scene);
}

- (BOOL)sceneCanRedo
{
    return Access::CanRedo(TheSession().Scene);
}

- (void)sceneUndo
{
    Access::Undo(TheSession().Scene);
    NotifySceneChanged();
}

- (void)sceneRedo
{
    Access::Redo(TheSession().Scene);
    NotifySceneChanged();
}

- (void)sceneDuplicate
{
    Access::Duplicate(TheSession().Scene);
    NotifySceneChanged();
}

- (void)sceneDelete
{
    Access::Delete(TheSession().Scene);
    NotifySceneChanged();
}

- (void)sceneSelectAll
{
    Access::SelectAll(TheSession().Scene);
    NotifySceneChanged();
}

- (void)sceneAlignToGrid
{
    Access::Align(TheSession().Scene);
    NotifySceneChanged();
}

- (void)sceneCreate:(NSInteger)kind light:(NSInteger)light
{
    Access::Create(TheSession().Scene, static_cast<scene::NodeKind>(kind), static_cast<scene::LightKind>(light),
                   std::nullopt);
    NotifySceneChanged();
}

- (void)sceneCreateFromAsset:(NSString*)relativePath
{
    const std::string Relative = FromNs(relativePath);
    const std::optional<core::MediaKind> Kind = core::ClassifyMedia(Relative);
    if (Kind == core::MediaKind::Model || Kind == core::MediaKind::Audio)
        Access::CreateFromAsset(TheSession().Scene,
                                Kind == core::MediaKind::Model ? scene::NodeKind::Model : scene::NodeKind::Sound,
                                Relative);
    NotifySceneChanged();
}

- (void)sceneToggleLevelCamera
{
    Access::ToggleLevelCamera(TheSession().Scene);
    NotifySceneChanged();
}

- (BOOL)sceneUsesLevelCamera
{
    return Access::UsesLevelCamera(TheSession().Scene);
}

- (void)sceneActivateAsset:(NSString*)relativePath
{
    Access::ActivateAsset(TheSession().Scene, FromNs(relativePath));
    NotifySceneChanged();
}

- (void)sceneDropAsset:(NSString*)relativePath at:(CGPoint)point
{
    Access::DropAsset(TheSession().Scene, FromNs(relativePath), point);
    NotifySceneChanged();
}

- (void)sceneDropPrimitive:(NSInteger)kind at:(CGPoint)point
{
    panels::SceneEditor& E = TheSession().Scene;
    Access::Create(E, static_cast<scene::NodeKind>(kind), scene::LightKind::Point, Access::Ground(E, point));
    NotifySceneChanged();
}

- (NSArray<DBCField*>*)sceneForm:(DBCForm)form
{
    FormBuilder Builder;
    Access::Build(TheSession().Scene, form, Builder);
    NSMutableArray<DBCField*>* Out = [NSMutableArray arrayWithCapacity:Builder.Fields.size()];
    for (std::size_t Index = 0; Index < Builder.Fields.size(); ++Index)
        [Out addObject:ToBridge(Builder.Fields[Index], static_cast<NSInteger>(Index))];
    return Out;
}

- (void)sceneForm:(DBCForm)form setNumber:(double)value field:(NSInteger)field commit:(BOOL)commit
{
    Access::Edit(TheSession().Scene, form, field, commit,
                 [value](Field& Row)
                 {
                     if (Row.SetNumber)
                         Row.SetNumber(value);
                 });
    if (commit)
        NotifySceneChanged();
}

- (void)sceneForm:(DBCForm)form setText:(NSString*)value field:(NSInteger)field commit:(BOOL)commit
{
    const std::string Text = FromNs(value);
    Access::Edit(TheSession().Scene, form, field, commit,
                 [&Text](Field& Row)
                 {
                     if (Row.SetText)
                         Row.SetText(Text);
                 });
    if (commit)
        NotifySceneChanged();
}

- (void)sceneForm:(DBCForm)form setFlag:(BOOL)value field:(NSInteger)field
{
    Access::Edit(TheSession().Scene, form, field, true,
                 [value](Field& Row)
                 {
                     if (Row.SetFlag)
                         Row.SetFlag(value);
                 });
    NotifySceneChanged();
}

- (void)sceneForm:(DBCForm)form setOptional:(BOOL)isSet field:(NSInteger)field
{
    Access::Edit(TheSession().Scene, form, field, true,
                 [isSet](Field& Row)
                 {
                     if (Row.SetOptional)
                         Row.SetOptional(isSet);
                 });
    NotifySceneChanged();
}

- (void)sceneForm:(DBCForm)form press:(NSInteger)field
{
    Access::Edit(TheSession().Scene, form, field, true,
                 [](Field& Row)
                 {
                     if (Row.Press)
                         Row.Press();
                 });
    NotifySceneChanged();
}

- (DBCSceneTool)sceneTool
{
    return static_cast<DBCSceneTool>(Access::Tool(TheSession().Scene));
}

- (void)setSceneTool:(DBCSceneTool)sceneTool
{
    Access::SetTool(TheSession().Scene, static_cast<int>(sceneTool));
    NotifySceneChanged();
}

- (BOOL)sceneLocalGizmo
{
    return Access::LocalGizmo(TheSession().Scene);
}

- (void)setSceneLocalGizmo:(BOOL)sceneLocalGizmo
{
    Access::LocalGizmo(TheSession().Scene) = sceneLocalGizmo;
}

- (BOOL)sceneSnap
{
    return Access::Snap(TheSession().Scene);
}

- (void)setSceneSnap:(BOOL)sceneSnap
{
    Access::Snap(TheSession().Scene) = sceneSnap;
}

- (BOOL)sceneGrid
{
    return Access::Grid(TheSession().Scene);
}

- (void)setSceneGrid:(BOOL)sceneGrid
{
    Access::Grid(TheSession().Scene) = sceneGrid;
}

- (BOOL)sceneCameraView
{
    return Access::CameraView(TheSession().Scene);
}

- (void)setSceneCameraView:(BOOL)sceneCameraView
{
    Access::SetCameraView(TheSession().Scene, sceneCameraView);
}

- (double)sceneMoveStep
{
    return Access::MoveStep(TheSession().Scene);
}

- (BOOL)writeViewportImage:(NSString*)path
{
    Session& S = TheSession();
    if (!S.ImGuiReady)
        return NO;
    const glm::vec2 Points = Access::ViewportSize(S.Scene);
    const float Scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const int Width = static_cast<int>(Points.x * Scale);
    const int Height = static_cast<int>(Points.y * Scale);
    SDL_GPUTexture* Texture = Width > 0 && Height > 0 ? Access::Target(S.Scene, Width, Height) : nullptr;
    if (Texture == nullptr)
        return NO;
    const Uint32 Bytes = static_cast<Uint32>(Width) * static_cast<Uint32>(Height) * 4;
    SDL_GPUTransferBufferCreateInfo Info{};
    Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    Info.size = Bytes;
    SDL_GPUTransferBuffer* Buffer = SDL_CreateGPUTransferBuffer(S.Gpu, &Info);
    SDL_GPUCommandBuffer* Commands = Buffer != nullptr ? SDL_AcquireGPUCommandBuffer(S.Gpu) : nullptr;
    if (Commands == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(S.Gpu, Buffer);
        return NO;
    }
    SDL_GPUCopyPass* Pass = SDL_BeginGPUCopyPass(Commands);
    SDL_GPUTextureRegion Region{};
    Region.texture = Texture;
    Region.w = static_cast<Uint32>(Width);
    Region.h = static_cast<Uint32>(Height);
    Region.d = 1;
    SDL_GPUTextureTransferInfo Destination{};
    Destination.transfer_buffer = Buffer;
    SDL_DownloadFromGPUTexture(Pass, &Region, &Destination);
    SDL_EndGPUCopyPass(Pass);
    SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
    BOOL Written = NO;
    if (Fence != nullptr)
    {
        SDL_WaitForGPUFences(S.Gpu, true, &Fence, 1);
        SDL_ReleaseGPUFence(S.Gpu, Fence);
        if (void* Pixels = SDL_MapGPUTransferBuffer(S.Gpu, Buffer, false))
        {
            NSData* Data = [NSData dataWithBytes:Pixels length:Bytes];
            SDL_UnmapGPUTransferBuffer(S.Gpu, Buffer);
            CGDataProviderRef Provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)Data);
            CGColorSpaceRef Space = CGColorSpaceCreateDeviceRGB();
            CGImageRef Image =
                CGImageCreate(static_cast<size_t>(Width), static_cast<size_t>(Height), 8, 32,
                              static_cast<size_t>(Width) * 4, Space,
                              static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrderDefault, Provider,
                              nullptr, false, kCGRenderingIntentDefault);
            if (Image != nullptr)
            {
                Written = [UIImagePNGRepresentation([UIImage imageWithCGImage:Image]) writeToFile:path atomically:YES];
                CGImageRelease(Image);
            }
            CGColorSpaceRelease(Space);
            CGDataProviderRelease(Provider);
        }
    }
    SDL_ReleaseGPUTransferBuffer(S.Gpu, Buffer);
    return Written;
}

- (NSString*)sceneInspectedModel
{
    return ToNs(Access::InspectedModel(TheSession().Scene));
}

- (void)setSceneInspectedModel:(NSString*)sceneInspectedModel
{
    Access::SetInspectedModel(TheSession().Scene, FromNs(sceneInspectedModel));
    NotifySceneChanged();
}

@end

#pragma mark Viewport

namespace
{
    // A key at a position of the keyboard, as ImGui names it on a US layout: the viewport's
    // movement keys are positions, not letters.
    ImGuiKey KeyOf(UIKeyboardHIDUsage Usage)
    {
        if (Usage >= UIKeyboardHIDUsageKeyboardA && Usage <= UIKeyboardHIDUsageKeyboardZ)
            return static_cast<ImGuiKey>(ImGuiKey_A + (Usage - UIKeyboardHIDUsageKeyboardA));
        if (Usage >= UIKeyboardHIDUsageKeyboard1 && Usage <= UIKeyboardHIDUsageKeyboard9)
            return static_cast<ImGuiKey>(ImGuiKey_1 + (Usage - UIKeyboardHIDUsageKeyboard1));
        switch (Usage)
        {
            case UIKeyboardHIDUsageKeyboard0:
                return ImGuiKey_0;
            case UIKeyboardHIDUsageKeyboardHyphen:
                return ImGuiKey_Minus;
            case UIKeyboardHIDUsageKeyboardEqualSign:
                return ImGuiKey_Equal;
            case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
                return ImGuiKey_Backspace;
            case UIKeyboardHIDUsageKeyboardDeleteForward:
                return ImGuiKey_Delete;
            case UIKeyboardHIDUsageKeyboardEscape:
                return ImGuiKey_Escape;
            case UIKeyboardHIDUsageKeyboardLeftShift:
            case UIKeyboardHIDUsageKeyboardRightShift:
                return ImGuiMod_Shift;
            case UIKeyboardHIDUsageKeyboardLeftControl:
            case UIKeyboardHIDUsageKeyboardRightControl:
                return ImGuiMod_Ctrl;
            case UIKeyboardHIDUsageKeyboardLeftAlt:
            case UIKeyboardHIDUsageKeyboardRightAlt:
                return ImGuiMod_Alt;
            case UIKeyboardHIDUsageKeyboardLeftGUI:
            case UIKeyboardHIDUsageKeyboardRightGUI:
                return ImGuiMod_Super;
            default:
                return ImGuiKey_None;
        }
    }
} // namespace

@implementation DBCViewportView
{
    UIView* _metalView;
    CADisplayLink* _displayLink;
    CFTimeInterval _lastFrame;
    // Fingers on the view, in the order they landed.
    NSMutableArray<UITouch*>* _fingers;
    // The finger that stands for the mouse, and whether its press has been sent: a press waits
    // until the finger moves or lifts, so that a second finger can still make it a pan.
    UITouch* _pointer;
    BOOL _pressSent;
    CGPoint _pointerStart;
    CGPoint _lastMouse;
    BOOL _renderFailed;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self == nil)
        return nil;
    _fingers = [NSMutableArray array];
    self.multipleTouchEnabled = YES;
    self.backgroundColor = UIColor.blackColor;

    UIHoverGestureRecognizer* Hover = [[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(hover:)];
    [self addGestureRecognizer:Hover];
    // A mouse wheel or two fingers on a trackpad.
    UIPanGestureRecognizer* Scroll = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(scroll:)];
    Scroll.allowedScrollTypesMask = UIScrollTypeMaskAll;
    Scroll.allowedTouchTypes = @[];
    [self addGestureRecognizer:Scroll];
    UIPinchGestureRecognizer* Pinch = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinch:)];
    Pinch.allowedTouchTypes = @[@(UITouchTypeIndirectPointer)];
    [self addGestureRecognizer:Pinch];
    return self;
}

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

- (void)attachMetalView
{
    if (!EnsurePresentation())
        return;
    Session& S = TheSession();
    const SDL_PropertiesID Properties = SDL_GetWindowProperties(S.ViewWindow);
    UIWindow* Window =
        (__bridge UIWindow*)SDL_GetPointerProperty(Properties, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    const NSInteger Tag =
        static_cast<NSInteger>(SDL_GetNumberProperty(Properties, SDL_PROP_WINDOW_UIKIT_METAL_VIEW_TAG_NUMBER, 0));
    static __weak UIView* Metal = nil;
    UIView* Found = Metal;
    // SDL makes its Metal view the view of the window's controller; a window that was never
    // shown has not loaded it into its own hierarchy.
    if (Found == nil && Window.rootViewController.view.tag == Tag)
        Found = Window.rootViewController.view;
    if (Found == nil)
        Found = [Window viewWithTag:Tag];
    if (Found == nil)
        return;
    Metal = Found;
    // SDL made the view for its own window, which is never shown: it is drawn here instead.
    [Found removeFromSuperview];
    Found.frame = self.bounds;
    Found.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    Found.userInteractionEnabled = NO;
    [self insertSubview:Found atIndex:0];
    _metalView = Found;
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    [_displayLink invalidate];
    _displayLink = nil;
    if (self.window == nil)
        return;
    [self attachMetalView];
    _lastFrame = CACurrentMediaTime();
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame)];
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)drawFrame
{
    Session& S = TheSession();
    if (_metalView == nil || _metalView.superview != self || !S.ImGuiReady || S.InBackground || S.Busy)
        return;
    const CGSize Size = self.bounds.size;
    if (Size.width < 1.0 || Size.height < 1.0)
        return;

    ImGuiIO& Io = ImGui::GetIO();
    const CFTimeInterval Now = CACurrentMediaTime();
    Io.DeltaTime = static_cast<float>(std::clamp(Now - _lastFrame, 1.0 / 240.0, 0.25));
    _lastFrame = Now;
    Io.DisplaySize = ImVec2(static_cast<float>(Size.width), static_cast<float>(Size.height));
    const float Scale = static_cast<float>(_metalView.layer.contentsScale);
    Io.DisplayFramebufferScale = ImVec2(Scale, Scale);

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(Io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const bool Open = ImGui::Begin("##SceneViewport", nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(3);
    if (Open)
        Access::DrawViewport(S.Scene);
    ImGui::End();
    Access::EndFrame(S.Scene, S.FormEditing);
    ImGui::Render();
    [self present];

    if (Access::TakeChange(S.Scene))
        NotifySceneChanged();
}

- (void)present
{
    Session& S = TheSession();
    const auto Fail = [&](const char* Operation)
    {
        if (!_renderFailed)
            AppendOutput(std::string("Scene viewport ") + Operation + " failed: " + SDL_GetError(), DBCSeverityError);
        _renderFailed = YES;
    };
    ImDrawData* DrawData = ImGui::GetDrawData();
    if (DrawData == nullptr || DrawData->DisplaySize.x <= 0.0f || DrawData->DisplaySize.y <= 0.0f)
        return;
    SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(S.Gpu);
    if (Commands == nullptr)
    {
        Fail("command acquisition");
        return;
    }
    SDL_GPUTexture* Swapchain = nullptr;
    // Never blocks: the interface's own drawing runs on this thread.
    if (!SDL_AcquireGPUSwapchainTexture(Commands, S.ViewWindow, &Swapchain, nullptr, nullptr))
    {
        Fail("swapchain acquisition");
        SDL_CancelGPUCommandBuffer(Commands);
        return;
    }
    if (Swapchain == nullptr)
    {
        SDL_CancelGPUCommandBuffer(Commands);
        return;
    }
    S.Scene.RecordGpu(Commands);
    ImGui_ImplSDLGPU3_PrepareDrawData(DrawData, Commands);
    const ImVec4 Clear = ImGui::ColorConvertU32ToFloat4(Theme::Background);
    SDL_GPUColorTargetInfo Target{};
    Target.texture = Swapchain;
    Target.clear_color = SDL_FColor{Clear.x, Clear.y, Clear.z, Clear.w};
    Target.load_op = SDL_GPU_LOADOP_CLEAR;
    Target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* Pass = SDL_BeginGPURenderPass(Commands, &Target, 1, nullptr);
    if (Pass == nullptr)
    {
        Fail("render pass");
        // Once the window image is acquired SDL requires submission, including on failure.
        SDL_SubmitGPUCommandBuffer(Commands);
        return;
    }
    ImGui_ImplSDLGPU3_RenderDrawData(DrawData, Commands, Pass);
    SDL_EndGPURenderPass(Pass);
    if (!SDL_SubmitGPUCommandBuffer(Commands))
    {
        Fail("submission");
        return;
    }
    _renderFailed = NO;
}

#pragma mark Input

- (void)sendModifiers:(UIKeyModifierFlags)flags
{
    ImGuiIO& Io = ImGui::GetIO();
    Io.AddKeyEvent(ImGuiMod_Shift, (flags & UIKeyModifierShift) != 0);
    Io.AddKeyEvent(ImGuiMod_Ctrl, (flags & UIKeyModifierControl) != 0);
    Io.AddKeyEvent(ImGuiMod_Alt, (flags & UIKeyModifierAlternate) != 0);
    Io.AddKeyEvent(ImGuiMod_Super, (flags & UIKeyModifierCommand) != 0);
}

- (void)sendPress
{
    if (_pressSent || _pointer == nil)
        return;
    ImGuiIO& Io = ImGui::GetIO();
    Io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    Io.AddMousePosEvent(static_cast<float>(_pointerStart.x), static_cast<float>(_pointerStart.y));
    Io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    _pressSent = YES;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    [self becomeFirstResponder];
    [self sendModifiers:event.modifierFlags];
    ImGuiIO& Io = ImGui::GetIO();
    panels::SceneEditor& E = TheSession().Scene;
    for (UITouch* Touch in touches)
    {
        const CGPoint At = [Touch locationInView:self];
        if (Touch.type == UITouchTypeIndirectPointer)
        {
            // A mouse or a trackpad's pointer: its buttons are the mouse's.
            Io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            Io.AddMousePosEvent(static_cast<float>(At.x), static_cast<float>(At.y));
            const bool Secondary = (event.buttonMask & UIEventButtonMaskSecondary) != 0;
            Io.AddMouseButtonEvent(Secondary ? ImGuiMouseButton_Right : ImGuiMouseButton_Left, true);
            _lastMouse = At;
            continue;
        }
        [_fingers addObject:Touch];
        E.HandleFinger(SDL_EVENT_FINGER_DOWN, static_cast<SDL_FingerID>(Touch.hash),
                       glm::vec2(static_cast<float>(At.x), static_cast<float>(At.y)));
        if (_fingers.count == 1)
        {
            _pointer = Touch;
            _pressSent = NO;
            _pointerStart = At;
        }
        else if (_pointer != nil)
        {
            // Two fingers pan and zoom: the first one stops being the mouse.
            if (_pressSent)
                Io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            _pointer = nil;
        }
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    [self sendModifiers:event.modifierFlags];
    ImGuiIO& Io = ImGui::GetIO();
    panels::SceneEditor& E = TheSession().Scene;
    for (UITouch* Touch in touches)
    {
        const CGPoint At = [Touch locationInView:self];
        if (Touch.type == UITouchTypeIndirectPointer)
        {
            // A flight steers with the pointer's motion.
            if (E.Flying())
                Access::AddFlySteer(E, static_cast<float>(At.x - _lastMouse.x), static_cast<float>(At.y - _lastMouse.y));
            _lastMouse = At;
            Io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            Io.AddMousePosEvent(static_cast<float>(At.x), static_cast<float>(At.y));
            continue;
        }
        E.HandleFinger(SDL_EVENT_FINGER_MOTION, static_cast<SDL_FingerID>(Touch.hash),
                       glm::vec2(static_cast<float>(At.x), static_cast<float>(At.y)));
        if (Touch != _pointer)
            continue;
        constexpr CGFloat KSlop = 6.0;
        if (!_pressSent && hypot(At.x - _pointerStart.x, At.y - _pointerStart.y) < KSlop)
            continue;
        [self sendPress];
        Io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        Io.AddMousePosEvent(static_cast<float>(At.x), static_cast<float>(At.y));
    }
}

- (void)endTouches:(NSSet<UITouch*>*)touches event:(UIEvent*)event
{
    ImGuiIO& Io = ImGui::GetIO();
    panels::SceneEditor& E = TheSession().Scene;
    for (UITouch* Touch in touches)
    {
        const CGPoint At = [Touch locationInView:self];
        if (Touch.type == UITouchTypeIndirectPointer)
        {
            Io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            Io.AddMousePosEvent(static_cast<float>(At.x), static_cast<float>(At.y));
            Io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
            continue;
        }
        [_fingers removeObject:Touch];
        E.HandleFinger(SDL_EVENT_FINGER_UP, static_cast<SDL_FingerID>(Touch.hash),
                       glm::vec2(static_cast<float>(At.x), static_cast<float>(At.y)));
        if (Touch != _pointer)
            continue;
        // A tap: the press it held back, then its release.
        [self sendPress];
        Io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        Io.AddMousePosEvent(static_cast<float>(At.x), static_cast<float>(At.y));
        Io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        // A finger that left hovers nothing.
        Io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        _pointer = nil;
    }
    (void)event;
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    [self endTouches:touches event:event];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    [self endTouches:touches event:event];
}

- (void)hover:(UIHoverGestureRecognizer*)recognizer
{
    ImGuiIO& Io = ImGui::GetIO();
    Io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    if (recognizer.state == UIGestureRecognizerStateEnded || recognizer.state == UIGestureRecognizerStateCancelled)
    {
        Io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return;
    }
    const CGPoint At = [recognizer locationInView:self];
    panels::SceneEditor& E = TheSession().Scene;
    if (E.Flying())
        Access::AddFlySteer(E, static_cast<float>(At.x - _lastMouse.x), static_cast<float>(At.y - _lastMouse.y));
    _lastMouse = At;
    [self sendModifiers:recognizer.modifierFlags];
    Io.AddMousePosEvent(static_cast<float>(At.x), static_cast<float>(At.y));
}

- (void)scroll:(UIPanGestureRecognizer*)recognizer
{
    const CGPoint Moved = [recognizer translationInView:self];
    [recognizer setTranslation:CGPointZero inView:self];
    // About one wheel notch for forty points.
    ImGui::GetIO().AddMouseWheelEvent(0.0f, static_cast<float>(Moved.y / 40.0));
}

- (void)pinch:(UIPinchGestureRecognizer*)recognizer
{
    if (recognizer.state != UIGestureRecognizerStateChanged)
        return;
    ImGui::GetIO().AddMouseWheelEvent(0.0f, static_cast<float>((recognizer.scale - 1.0) * 8.0));
    recognizer.scale = 1.0;
}

- (void)sendPresses:(NSSet<UIPress*>*)presses down:(BOOL)down
{
    ImGuiIO& Io = ImGui::GetIO();
    panels::SceneEditor& E = TheSession().Scene;
    for (UIPress* Press in presses)
    {
        UIKey* Key = Press.key;
        if (Key == nil)
            continue;
        const ImGuiKey Value = KeyOf(Key.keyCode);
        if (Value == ImGuiKey_None)
            continue;
        Io.AddKeyEvent(Value, down);
        // The Scene menu's keys that are not commands of the interface.
        const bool Plain = (Key.modifierFlags & (UIKeyModifierCommand | UIKeyModifierControl)) == 0;
        if (down && Plain && (Value == ImGuiKey_Delete || Value == ImGuiKey_Backspace))
        {
            Access::Delete(E);
            NotifySceneChanged();
        }
        else if (down && Plain && Value == ImGuiKey_F && !E.Flying())
        {
            Access::Frame(E);
        }
    }
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self sendPresses:presses down:YES];
    [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self sendPresses:presses down:NO];
    [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self sendPresses:presses down:NO];
    [super pressesCancelled:presses withEvent:event];
}

@end
