#include "scene/scene_capture.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>

#include <nlohmann/json.hpp>

#include "interpreter/basic_path.hpp"
#include "path_utf8.hpp"

namespace dbc::studio
{
    namespace scene
    {
        namespace lang = dbc::language;
        using lang::SymbolId;

        namespace
        {
            using Json = nlohmann::json;

            // The capture comes from another process: nothing about its shape is assumed, and these never throw.
            float FloatOf(const Json& Value, float Default = 0.0f)
            {
                return Value.is_number() ? Value.get<float>() : Default;
            }

            int IntOf(const Json& Value, int Default = 0)
            {
                return Value.is_number() ? static_cast<int>(std::lround(Value.get<double>())) : Default;
            }

            const Json& Field(const Json& Object, const char* Key)
            {
                static const Json Missing;
                const auto Found = Object.find(Key);
                return Found == Object.end() ? Missing : *Found;
            }

            const Json& At(const Json& Array, std::size_t Index)
            {
                static const Json Missing;
                return Array.is_array() && Index < Array.size() ? Array[Index] : Missing;
            }

            int IntField(const Json& Object, const char* Key, int Default = 0)
            {
                return IntOf(Field(Object, Key), Default);
            }

            bool BoolField(const Json& Object, const char* Key, bool Default)
            {
                const Json& Value = Field(Object, Key);
                return Value.is_boolean() ? Value.get<bool>() : Default;
            }

            glm::vec3 Vec3Field(const Json& Object, const char* Key, glm::vec3 Default = glm::vec3(0.0f))
            {
                const Json& Value = Field(Object, Key);
                return {FloatOf(At(Value, 0), Default.x), FloatOf(At(Value, 1), Default.y),
                        FloatOf(At(Value, 2), Default.z)};
            }

            std::string TextOf(const Json& Value)
            {
                return Value.is_string() ? Value.get<std::string>() : std::string();
            }

            std::vector<std::filesystem::path> PathList(const Json& Value)
            {
                std::vector<std::filesystem::path> Result;
                if (Value.is_array())
                    for (const Json& Item : Value)
                        Result.push_back(dbc::PathFromUtf8(TextOf(Item)));
                return Result;
            }

            // As a level writes assets: relative to the asset root when under it.
            std::string AssetText(const std::filesystem::path& Absolute, const std::filesystem::path& AssetRoot)
            {
                const std::filesystem::path Relative = Absolute.lexically_relative(AssetRoot);
                const bool Inside = !Relative.empty() && *Relative.begin() != "..";
                return dbc::GenericPathToUtf8(Inside ? Relative : Absolute);
            }

            // Which string argument names a file, in the commands the Scene view reads.
            std::optional<std::size_t> AssetArgument(SymbolId Symbol)
            {
                switch (Symbol)
                {
                    case SymbolId::Command_LoadImage:
                    case SymbolId::Command_LoadObject:
                    case SymbolId::Command_Load3dsound:
                        return 0;
                    case SymbolId::Command_MakeTerrain:
                        return 1;
                    default:
                        return std::nullopt;
                }
            }

            CapturedMatrix ParseMatrix(const Json& Value)
            {
                CapturedMatrix Result;
                Result.Id = IntField(Value, "id");
                const Json& Size = Field(Value, "size");
                Result.Size = {FloatOf(At(Size, 0)), FloatOf(At(Size, 1))};
                const Json& Segments = Field(Value, "segments");
                Result.Segments = {IntOf(At(Segments, 0)), IntOf(At(Segments, 1))};
                Result.Position = Vec3Field(Value, "position");
                Result.Image = IntField(Value, "image");
                const Json& Tiles = Field(Value, "tiles");
                Result.Tiles = {IntOf(At(Tiles, 0), 1), IntOf(At(Tiles, 1), 1)};
                const Json& Heights = Field(Value, "heights");
                if (Heights.is_array())
                {
                    Result.Heights.reserve(Heights.size());
                    for (const Json& Height : Heights)
                        Result.Heights.push_back(FloatOf(Height));
                }
                const Json& Tile = Field(Value, "tile");
                if (Tile.is_array())
                {
                    Result.Tile.reserve(Tile.size());
                    for (const Json& Cell : Tile)
                        Result.Tile.push_back(IntOf(Cell, 1));
                }
                return Result;
            }

            struct Creator
            {
                IdSpace Space = IdSpace::Object;
                int IdArgument = 0;
            };

            std::optional<Creator> CreatorOf(SymbolId Symbol)
            {
                switch (Symbol)
                {
                    case SymbolId::Command_LoadObject:
                        return Creator{IdSpace::Object, 1};
                    case SymbolId::Command_MakeObject:
                    case SymbolId::Command_MakeObjectBox:
                    case SymbolId::Command_MakeObjectCone:
                    case SymbolId::Command_MakeObjectCube:
                    case SymbolId::Command_MakeObjectCylinder:
                    case SymbolId::Command_MakeObjectFromLimb:
                    case SymbolId::Command_MakeObjectPlain:
                    case SymbolId::Command_MakeObjectPlane:
                    case SymbolId::Command_MakeObjectSphere:
                    case SymbolId::Command_MakeObjectTerrain:
                    case SymbolId::Command_MakeObjectTriangle:
                    case SymbolId::Command_CloneObject:
                    case SymbolId::Command_InstanceObject:
                        return Creator{IdSpace::Object, 0};
                    case SymbolId::Command_MakeMatrix:
                        return Creator{IdSpace::Matrix, 0};
                    case SymbolId::Command_MakeTerrain:
                        return Creator{IdSpace::Terrain, 0};
                    case SymbolId::Command_MakeLight:
                        return Creator{IdSpace::Light, 0};
                    case SymbolId::Command_Load3dsound:
                        return Creator{IdSpace::Sound, 1};
                    default:
                        return std::nullopt;
                }
            }

            // A statement that sets transform axes of one node, its number being the first argument.
            struct TransformWriter
            {
                IdSpace Space = IdSpace::Object;
                TransformPart Part = TransformPart::Position;
                int FirstAxis = 0;
                int AxisCount = 3;
            };

            std::optional<TransformWriter> WriterOf(SymbolId Symbol, std::size_t ArgumentCount)
            {
                const auto Whole = [&](IdSpace Space, TransformPart Part) -> std::optional<TransformWriter>
                {
                    if (ArgumentCount != 4)
                        return std::nullopt;
                    return TransformWriter{Space, Part, 0, 3};
                };
                switch (Symbol)
                {
                    case SymbolId::Command_PositionObject:
                        return Whole(IdSpace::Object, TransformPart::Position);
                    case SymbolId::Command_RotateObject:
                        return Whole(IdSpace::Object, TransformPart::Rotation);
                    case SymbolId::Command_ScaleObject:
                        return Whole(IdSpace::Object, TransformPart::Scale);
                    case SymbolId::Command_XrotateObject:
                        return TransformWriter{IdSpace::Object, TransformPart::Rotation, 0, 1};
                    case SymbolId::Command_YrotateObject:
                        return TransformWriter{IdSpace::Object, TransformPart::Rotation, 1, 1};
                    case SymbolId::Command_ZrotateObject:
                        return TransformWriter{IdSpace::Object, TransformPart::Rotation, 2, 1};
                    case SymbolId::Command_PositionMatrix:
                        return Whole(IdSpace::Matrix, TransformPart::Position);
                    case SymbolId::Command_PositionTerrain:
                        return Whole(IdSpace::Terrain, TransformPart::Position);
                    case SymbolId::Command_PositionLight:
                    case SymbolId::Command_SetPointLight:
                        return Whole(IdSpace::Light, TransformPart::Position);
                    case SymbolId::Command_RotateLight:
                        return Whole(IdSpace::Light, TransformPart::Rotation);
                    case SymbolId::Command_PositionSound:
                        return Whole(IdSpace::Sound, TransformPart::Position);
                    default:
                        return std::nullopt;
                }
            }

            struct Track
            {
                int Creator = -1;
                // The last call setting each axis, by part.
                std::array<std::array<int, 3>, KTransformPartCount> Writers{{{-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}}};
            };

            using SiteKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, SymbolId>;

            SiteKey KeyOf(const SceneCapture& Capture, std::size_t Index)
            {
                const CallSite& Site = Capture.Sites[Index];
                return {Site.File, Site.Line, Site.Column, Capture.Commands[Index].Symbol};
            }

            SourceRef RefOf(const SceneCapture& Capture, std::size_t Index)
            {
                const CallSite& Site = Capture.Sites[Index];
                return {Site.File < Capture.Files.size() ? Capture.Files[Site.File] : std::string(), Site.Line,
                        Site.Column};
            }

            bool Same(TransformPart Part, float Left, float Right)
            {
                if (Part != TransformPart::Rotation)
                    return std::abs(Left - Right) <= 0.01f;
                const float Turn = std::fmod(std::abs(Left - Right), 360.0f);
                return std::min(Turn, 360.0f - Turn) <= 0.05f;
            }

            struct StateTransform
            {
                glm::vec3 Position{0.0f};
                glm::vec3 Rotation{0.0f};
                glm::vec3 Scale{100.0f};
            };

            float StateAxis(const StateTransform& State, TransformPart Part, int Axis)
            {
                const glm::vec3& Value = Part == TransformPart::Position   ? State.Position
                                         : Part == TransformPart::Rotation ? State.Rotation
                                                                           : State.Scale;
                return Value[Axis];
            }

            AxisOrigin ClassifyAxis(const SceneCapture& Capture, const CaptureReadOptions& Options,
                                    const std::map<SiteKey, int>& Runs, const Track& Tracked,
                                    const std::optional<StateTransform>& State, TransformPart Part, int Axis)
            {
                constexpr const char* KChangedAtRun = "Another command changes it while the program runs.";
                AxisOrigin Origin;
                const float Default = Part == TransformPart::Scale ? 100.0f : 0.0f;
                const int Writer = Tracked.Writers[static_cast<std::size_t>(Part)][Axis];
                if (Writer >= 0)
                {
                    const auto Index = static_cast<std::size_t>(Writer);
                    const std::optional<TransformWriter> Form =
                        WriterOf(Capture.Commands[Index].Symbol, Capture.Commands[Index].Arguments.size());
                    Origin.Writer = RefOf(Capture, Index);
                    Origin.Argument = 1 + Axis - Form->FirstAxis;
                    const float Written = static_cast<float>(Capture.Commands[Index].Arguments[Origin.Argument].Number);
                    const int Count = Runs.at(KeyOf(Capture, Index));
                    if (Count > 1)
                        Origin.Reason = "Its statement runs " + std::to_string(Count) + " times.";
                    else if (Capture.Sites[Index].Frame >= Options.SetupFrames)
                        Origin.Reason = "The game loop sets it.";
                    else if (State && !Same(Part, StateAxis(*State, Part, Axis), Written))
                        Origin.Reason = KChangedAtRun;
                    else
                        Origin.Access = AxisAccess::Replace;
                    return Origin;
                }
                if (Tracked.Creator < 0)
                {
                    Origin.Reason = "No statement of the program sets it.";
                    return Origin;
                }
                const auto Created = static_cast<std::size_t>(Tracked.Creator);
                const int Count = Runs.at(KeyOf(Capture, Created));
                if (Count > 1)
                    Origin.Reason = "Its creation runs " + std::to_string(Count) + " times.";
                else if (Capture.Sites[Created].Frame >= Options.SetupFrames)
                    Origin.Reason = "The game loop creates it.";
                else if (State && !Same(Part, StateAxis(*State, Part, Axis), Default))
                    Origin.Reason = KChangedAtRun;
                else
                    Origin.Access = AxisAccess::Insert;
                return Origin;
            }

            // The same angles ROTATE LIGHT would need for a direction.
            glm::vec3 AnglesOf(const glm::vec3& Direction)
            {
                constexpr float KDegrees = 180.0f / 3.14159265358979323846f;
                const float Length =
                    std::sqrt(Direction.x * Direction.x + Direction.y * Direction.y + Direction.z * Direction.z);
                if (Length <= 0.0f)
                    return glm::vec3(0.0f);
                return {std::asin(-Direction.y / Length) * KDegrees, std::atan2(Direction.x, Direction.z) * KDegrees,
                        0.0f};
            }

            std::string ImageText(const SceneCapture& Capture, int Image, std::size_t& Generated)
            {
                if (Image <= 0)
                    return {};
                const auto Found = Capture.Images.find(Image);
                if (Found == Capture.Images.end() || Found->second.empty())
                {
                    ++Generated;
                    return {};
                }
                return Found->second;
            }

            bool IsNotShown(std::string_view Command)
            {
                constexpr std::string_view Words[] = {"LIMB", "MESH", "PARTICLES", "VERTEXDATA", "BSP", "APPEND"};
                return std::any_of(std::begin(Words), std::end(Words),
                                   [&](std::string_view Word) { return Command.find(Word) != std::string_view::npos; });
            }
        } // namespace

        bool ParseSceneCapture(const std::string& Text, const std::filesystem::path& SourceRoot,
                               const std::filesystem::path& AssetRoot, SceneCapture& Out, std::string& Error)
        {
            const Json Root = Json::parse(Text, nullptr, false);
            if (Root.is_discarded() || !Root.is_object() || TextOf(Field(Root, "schema")) != "opendbc-scene-capture" ||
                IntField(Root, "version") != 1)
            {
                Error = "The scene capture is not readable";
                return false;
            }
            const std::string Profile = TextOf(Field(Root, "profile"));
            const std::optional<lang::LanguageProfile> Parsed = lang::ParseProfile(Profile);
            if (!Parsed)
            {
                Error = "The scene capture names an unknown dialect: " + Profile;
                return false;
            }
            Out = {};
            Out.Profile = *Parsed;
            Out.Status = TextOf(Field(Root, "status"));
            Out.Frames = static_cast<std::size_t>(std::max(IntField(Root, "frames"), 0));
            Out.Truncated = BoolField(Root, "truncated", false);

            const std::filesystem::path NormalRoot = AssetRoot.lexically_normal();
            for (const std::filesystem::path& File : PathList(Field(Root, "files")))
                Out.Files.push_back(dbc::GenericPathToUtf8((SourceRoot / File).lexically_normal()));
            const std::vector<std::filesystem::path> Roots = PathList(Field(Root, "roots"));

            const Json& Calls = Field(Root, "calls");
            if (Calls.is_array())
            {
                Out.Commands.reserve(Calls.size());
                Out.Sites.reserve(Calls.size());
                for (const Json& Call : Calls)
                {
                    const int Symbol = IntOf(At(Call, 0), -1);
                    if (Symbol < 0 || Symbol >= static_cast<int>(SymbolId::Count))
                        continue;
                    ReplayCall Command;
                    Command.Symbol = static_cast<SymbolId>(Symbol);
                    const Json& Arguments = At(Call, 6);
                    if (Arguments.is_array())
                        for (const Json& Value : Arguments)
                        {
                            ReplayArgument Argument;
                            Argument.IsText = Value.is_string();
                            if (Argument.IsText)
                                Argument.Text = Value.get<std::string>();
                            else if (Value.is_number())
                                Argument.Number = Value.get<double>();
                            Command.Arguments.push_back(std::move(Argument));
                        }
                    const std::size_t RootIndex = static_cast<std::size_t>(std::max(IntOf(At(Call, 5)), 0));
                    const std::optional<std::size_t> Asset = AssetArgument(Command.Symbol);
                    if (Asset && *Asset < Command.Arguments.size() && Command.Arguments[*Asset].IsText &&
                        RootIndex < Roots.size())
                    {
                        std::string& Name = Command.Arguments[*Asset].Text;
                        Name = AssetText(interpreter::ResolveBasicPath(Roots[RootIndex], dbc::PathFromUtf8(Name)),
                                         NormalRoot);
                    }
                    Out.Commands.push_back(std::move(Command));
                    Out.Sites.push_back({static_cast<std::uint32_t>(std::max(IntOf(At(Call, 1)), 0)),
                                         static_cast<std::uint32_t>(std::max(IntOf(At(Call, 2)), 0)),
                                         static_cast<std::uint32_t>(std::max(IntOf(At(Call, 3)), 0)),
                                         IntOf(At(Call, 4))});
                }
            }

            const Json& Objects = Field(Root, "objects");
            if (Objects.is_array())
                for (const Json& Value : Objects)
                {
                    CapturedObject Object;
                    Object.Id = IntField(Value, "id");
                    Object.Position = Vec3Field(Value, "position");
                    Object.Rotation = Vec3Field(Value, "rotation");
                    Object.Scale = Vec3Field(Value, "scale", glm::vec3(100.0f));
                    Object.Min = Vec3Field(Value, "min");
                    Object.Max = Vec3Field(Value, "max");
                    Object.Visible = BoolField(Value, "visible", true);
                    Object.Screen = BoolField(Value, "screen", false);
                    Object.Image = IntField(Value, "image");
                    Out.Objects.push_back(Object);
                }
            const Json& Lights = Field(Root, "lights");
            if (Lights.is_array())
                for (const Json& Value : Lights)
                {
                    CapturedLight Light;
                    Light.Id = IntField(Value, "id");
                    Light.Type = IntField(Value, "type", 1);
                    Light.Visible = BoolField(Value, "visible", true);
                    Light.Position = Vec3Field(Value, "position");
                    Light.Rotation = Vec3Field(Value, "rotation");
                    Light.Direction = Vec3Field(Value, "direction", Light.Direction);
                    Light.Range = FloatOf(Field(Value, "range"));
                    Light.Color = static_cast<Rgb>(IntField(Value, "color", 0xFFFFFF)) & 0xFFFFFFu;
                    Out.Lights.push_back(Light);
                }
            const Json& Matrices = Field(Root, "matrices");
            if (Matrices.is_array())
                for (const Json& Value : Matrices)
                    Out.Matrices.push_back(ParseMatrix(Value));
            const Json& Terrains = Field(Root, "terrains");
            if (Terrains.is_array())
                for (const Json& Value : Terrains)
                    Out.Terrains.push_back(ParseMatrix(Value));
            const Json& Images = Field(Root, "images");
            if (Images.is_array())
                for (const Json& Value : Images)
                {
                    const std::string Path = TextOf(At(Value, 1));
                    Out.Images[IntOf(At(Value, 0))] =
                        Path.empty() ? std::string()
                                     : AssetText(dbc::PathFromUtf8(Path).lexically_normal(), NormalRoot);
                }
            const Json& Camera = Field(Root, "camera");
            Out.HasCamera = Camera.is_object();
            if (Out.HasCamera)
            {
                Out.CameraPosition = Vec3Field(Camera, "position");
                Out.CameraRotation = Vec3Field(Camera, "rotation");
                Out.CameraFov = FloatOf(Field(Camera, "fov"));
            }
            return true;
        }

        CaptureReadResult ReadCapture(const SceneCapture& Capture, const CaptureReadOptions& Options)
        {
            CaptureReadResult Result;
            ReplayResult Replay = ReplayCalls(Capture.Commands, Options.Name, Capture.Profile);
            Result.Document = std::move(Replay.Document);
            SceneDocument& Document = Result.Document;

            std::map<SiteKey, int> Runs;
            std::map<NodeKey, Track> Tracks;
            for (std::size_t Index = 0; Index < Capture.Commands.size(); ++Index)
            {
                const ReplayCall& Command = Capture.Commands[Index];
                ++Runs[KeyOf(Capture, Index)];
                const auto IdAt = [&](std::size_t Argument)
                {
                    return Argument < Command.Arguments.size()
                               ? static_cast<int>(std::lround(Command.Arguments[Argument].Number))
                               : -1;
                };
                if (const std::optional<Creator> Made = CreatorOf(Command.Symbol))
                {
                    Track& Tracked = Tracks[{Made->Space, IdAt(static_cast<std::size_t>(Made->IdArgument))}];
                    Tracked = {};
                    Tracked.Creator = static_cast<int>(Index);
                }
                if (const std::optional<TransformWriter> Writer = WriterOf(Command.Symbol, Command.Arguments.size()))
                {
                    Track& Tracked = Tracks[{Writer->Space, IdAt(0)}];
                    for (int Axis = 0; Axis < Writer->AxisCount; ++Axis)
                        Tracked.Writers[static_cast<std::size_t>(Writer->Part)][Writer->FirstAxis + Axis] =
                            static_cast<int>(Index);
                }
            }

            std::map<int, const CapturedObject*> Objects;
            for (const CapturedObject& Object : Capture.Objects)
                Objects[Object.Id] = &Object;
            std::map<int, const CapturedLight*> Lights;
            for (const CapturedLight& Light : Capture.Lights)
                Lights[Light.Id] = &Light;
            std::map<int, const CapturedMatrix*> Matrices;
            for (const CapturedMatrix& Value : Capture.Matrices)
                Matrices[Value.Id] = &Value;
            std::map<int, const CapturedMatrix*> Terrains;
            for (const CapturedMatrix& Value : Capture.Terrains)
                Terrains[Value.Id] = &Value;

            const auto CreatedElsewhere = [&](const NodeKey& Key)
            {
                if (Options.OnlyFile.empty())
                    return false;
                const auto Found = Tracks.find(Key);
                return Found == Tracks.end() || Found->second.Creator < 0 ||
                       RefOf(Capture, static_cast<std::size_t>(Found->second.Creator)).File != Options.OnlyFile;
            };

            const auto OriginOf = [&](const NodeKey& Key, const std::optional<StateTransform>& State)
            {
                NodeOrigin Origin;
                const auto Tracked = Tracks.find(Key);
                if (Tracked == Tracks.end())
                    return Origin;
                if (Tracked->second.Creator >= 0)
                {
                    const auto Created = static_cast<std::size_t>(Tracked->second.Creator);
                    Origin.Creator = RefOf(Capture, Created);
                    Origin.CreatorSymbol = Capture.Commands[Created].Symbol;
                    Origin.CreatorIdArgument = CreatorOf(Origin.CreatorSymbol)->IdArgument;
                }
                for (std::size_t Part = 0; Part < KTransformPartCount; ++Part)
                    for (int Axis = 0; Axis < 3; ++Axis)
                        Origin.Axes[Part][static_cast<std::size_t>(Axis)] = ClassifyAxis(
                            Capture, Options, Runs, Tracked->second, State, static_cast<TransformPart>(Part), Axis);
                return Origin;
            };

            std::size_t Generated = 0;
            std::vector<SceneNode> Kept;
            Kept.reserve(Document.Nodes.size());
            for (SceneNode& Node : Document.Nodes)
            {
                const IdSpace Space = IdSpaceOf(Node.Kind);
                std::optional<StateTransform> State;
                switch (Space)
                {
                    case IdSpace::Object:
                    {
                        const auto Found = Objects.find(Node.Id);
                        if (Found == Objects.end())
                            break;
                        const CapturedObject& Object = *Found->second;
                        Node.Position = Object.Position;
                        Node.Rotation = Object.Rotation;
                        Node.Scale = Object.Scale;
                        Node.Hidden = !Object.Visible || Object.Screen;
                        Node.Mat.Texture = ImageText(Capture, Object.Image, Generated);
                        State = StateTransform{Object.Position, Object.Rotation, Object.Scale};
                        break;
                    }
                    case IdSpace::Matrix:
                    {
                        const auto Found = Matrices.find(Node.Id);
                        if (Found == Matrices.end())
                            break;
                        const CapturedMatrix& Value = *Found->second;
                        Node.Width = Value.Size.x;
                        Node.Depth = Value.Size.y;
                        Node.XSegments = Value.Segments.x;
                        Node.ZSegments = Value.Segments.y;
                        Node.Position = Value.Position;
                        Node.TilesAcross = Value.Tiles.x;
                        Node.TilesDown = Value.Tiles.y;
                        Node.Heights = Value.Heights;
                        Node.Tiles = Value.Tile;
                        Node.Mat.Texture = ImageText(Capture, Value.Image, Generated);
                        State = StateTransform{Value.Position};
                        break;
                    }
                    case IdSpace::Terrain:
                    {
                        const auto Found = Terrains.find(Node.Id);
                        if (Found == Terrains.end())
                            break;
                        Node.Position = Found->second->Position;
                        Node.Mat.Texture = ImageText(Capture, Found->second->Image, Generated);
                        State = StateTransform{Node.Position};
                        break;
                    }
                    case IdSpace::Light:
                    {
                        const auto Found = Lights.find(Node.Id);
                        if (Found == Lights.end())
                            break;
                        const CapturedLight& Light = *Found->second;
                        Node.Light = Light.Type == 3   ? LightKind::Directional
                                     : Light.Type == 2 ? LightKind::Spot
                                                       : LightKind::Point;
                        Node.Position = Light.Position;
                        Node.Rotation = Light.Type == 1 ? Light.Rotation : AnglesOf(Light.Direction);
                        Node.Range = Light.Range;
                        Node.LightColor = Light.Color;
                        Node.Hidden = !Light.Visible;
                        State = StateTransform{Light.Position, Node.Rotation};
                        break;
                    }
                    default:
                        break;
                }
                // A program may delete what it created; only a static object outlives its number.
                const bool Gone = !State && Space != IdSpace::Sound && !Node.Static;
                const NodeKey Key{Space, Node.Id};
                if (Gone || CreatedElsewhere(Key))
                    continue;

                Result.Origins[Key] = OriginOf(Key, State);
                Kept.push_back(std::move(Node));
            }
            Document.Nodes = std::move(Kept);

            // Objects whose geometry no level statement describes: a box of the same size stands in.
            std::size_t Stand = 0;
            for (const CapturedObject& Object : Capture.Objects)
            {
                const NodeKey Key{IdSpace::Object, Object.Id};
                if (Document.Find(IdSpace::Object, Object.Id) != nullptr || CreatedElsewhere(Key))
                    continue;
                SceneNode Node;
                Node.Kind = NodeKind::Box;
                Node.Id = Object.Id;
                Node.Name = "Object " + std::to_string(Object.Id);
                Node.Position = Object.Position;
                Node.Rotation = Object.Rotation;
                Node.Scale = Object.Scale;
                Node.Hidden = !Object.Visible || Object.Screen;
                const glm::vec3 Extent = Object.Max - Object.Min;
                Node.Extent = glm::vec3(std::max(Extent.x, 1.0f), std::max(Extent.y, 1.0f), std::max(Extent.z, 1.0f));
                Node.Mat.Flags = DefaultObjectFlags(Capture.Profile);
                Node.Mat.Texture = ImageText(Capture, Object.Image, Generated);
                Document.Nodes.push_back(std::move(Node));
                Result.Origins[Key] = OriginOf(Key, StateTransform{Object.Position, Object.Rotation, Object.Scale});
                ++Stand;
            }

            Environment& Env = Document.Env;
            const auto DefaultLight = Lights.find(0);
            if (DefaultLight != Lights.end())
            {
                const CapturedLight& Light = *DefaultLight->second;
                Env.DefaultLightHidden = !Light.Visible;
                Env.DefaultLightRotation = AnglesOf(Light.Direction);
                Env.DefaultLightColor = Light.Color;
            }
            if (Capture.HasCamera)
            {
                Env.UseCamera = true;
                Env.CameraPosition = Capture.CameraPosition;
                Env.CameraRotation = Capture.CameraRotation;
                Env.CameraFov = Capture.CameraFov;
            }

            if (Capture.Truncated)
                Result.Notes.push_back(
                    "The program runs more scene commands than a capture keeps: the last ones are missing.");
            if (Stand > 0)
                Result.Notes.push_back(std::to_string(Stand) + " objects have their own geometry and appear as boxes.");
            if (Generated > 0)
                Result.Notes.push_back(std::to_string(Generated) +
                                       " textures come from the program, not from a file: they are not shown.");
            std::string NotShown;
            for (const std::string& Command : Replay.Skipped)
                if (IsNotShown(Command))
                    NotShown += (NotShown.empty() ? "" : ", ") + Command;
            if (!NotShown.empty())
                Result.Notes.push_back("Not shown: " + NotShown + ".");
            return Result;
        }
    } // namespace scene
} // namespace dbc::studio
