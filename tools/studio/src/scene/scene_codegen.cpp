#include "scene/scene_codegen.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>

#include "language/frontend.hpp"
#include "language/language_catalogue.hpp"

namespace dbc::studio
{
    namespace scene
    {
        namespace lang = dbc::language;
        using lang::SymbolId;

        namespace
        {
            constexpr float KPi = 3.14159265358979323846f;
            // Follows the name in an object's REM annotation.
            constexpr const char* KPaintedTag = "painted";

            // --- Writing ---------------------------------------------------

            std::string Integer(int Value)
            {
                return std::to_string(Value);
            }

            // BASIC strings have no escape, so a quote can never be stored.
            std::string Quoted(const std::string& Value)
            {
                std::string Result = "\"";
                for (const char Character : Value)
                    if (Character != '"' && Character != '\n' && Character != '\r')
                        Result.push_back(Character);
                Result.push_back('"');
                return Result;
            }

            std::string Color(Rgb Value)
            {
                return "RGB(" + Integer(static_cast<int>((Value >> 16) & 0xFF)) + ", " +
                       Integer(static_cast<int>((Value >> 8) & 0xFF)) + ", " + Integer(static_cast<int>(Value & 0xFF)) +
                       ")";
            }

            class Emitter
            {
              public:
                void Line(const std::string& Text)
                {
                    Out_ << std::string(Indent_ * 3, ' ') << Text << '\n';
                }

                void Command(SymbolId Id, std::initializer_list<std::string> Arguments = {})
                {
                    // The generator only writes forms the profile accepts; tests bind every one.
                    assert(lang::FindOverload(Id, KSceneProfile, static_cast<int>(Arguments.size())) != nullptr);
                    std::string Text(lang::Symbol(Id).CanonicalName);
                    bool First = true;
                    for (const std::string& Argument : Arguments)
                    {
                        Text += First ? " " : ", ";
                        Text += Argument;
                        First = false;
                    }
                    Line(Text);
                }

                void Vector(SymbolId Id, int Target, const glm::vec3& Value)
                {
                    Command(Id,
                            {Integer(Target), NumberLiteral(Value.x), NumberLiteral(Value.y), NumberLiteral(Value.z)});
                }

                void Push()
                {
                    ++Indent_;
                }

                void Pop()
                {
                    --Indent_;
                }

                std::string Take()
                {
                    return Out_.str();
                }

              private:
                std::ostringstream Out_;
                int Indent_ = 0;
            };

            bool IsObject(NodeKind Kind)
            {
                return IdSpaceOf(Kind) == IdSpace::Object;
            }

            const char* MetadataTag(NodeKind Kind)
            {
                switch (IdSpaceOf(Kind))
                {
                    case IdSpace::Matrix:
                        return "matrix";
                    case IdSpace::Terrain:
                        return "terrain";
                    case IdSpace::Light:
                        return "light";
                    case IdSpace::Sound:
                        return "sound";
                    default:
                        return "object";
                }
            }

            std::string NodeTexture(const SceneNode& Node)
            {
                return IsMarker(Node.Kind) ? std::string() : Node.Mat.Texture;
            }

            void WriteObject(Emitter& Out, const SceneNode& Node, const std::map<std::string, int>& Images)
            {
                const std::string Id = Integer(Node.Id);
                switch (Node.Kind)
                {
                    case NodeKind::Cube:
                        Out.Command(SymbolId::Command_MakeObjectCube, {Id, NumberLiteral(Node.Size)});
                        break;
                    case NodeKind::Box:
                        Out.Command(SymbolId::Command_MakeObjectBox,
                                    {Id, NumberLiteral(Node.Extent.x), NumberLiteral(Node.Extent.y),
                                     NumberLiteral(Node.Extent.z)});
                        break;
                    case NodeKind::Sphere:
                        if (Node.Detail)
                            Out.Command(
                                SymbolId::Command_MakeObjectSphere,
                                {Id, NumberLiteral(Node.Size), Integer(Node.Detail->x), Integer(Node.Detail->y)});
                        else
                            Out.Command(SymbolId::Command_MakeObjectSphere, {Id, NumberLiteral(Node.Size)});
                        break;
                    case NodeKind::Cylinder:
                        Out.Command(SymbolId::Command_MakeObjectCylinder, {Id, NumberLiteral(Node.Size)});
                        break;
                    case NodeKind::Cone:
                        Out.Command(SymbolId::Command_MakeObjectCone, {Id, NumberLiteral(Node.Size)});
                        break;
                    case NodeKind::Plain:
                        if (Node.PlainOrientation)
                            Out.Command(SymbolId::Command_MakeObjectPlain,
                                        {Id, NumberLiteral(Node.Extent.x), NumberLiteral(Node.Extent.y),
                                         Integer(*Node.PlainOrientation)});
                        else
                            Out.Command(SymbolId::Command_MakeObjectPlain,
                                        {Id, NumberLiteral(Node.Extent.x), NumberLiteral(Node.Extent.y)});
                        break;
                    case NodeKind::Triangle:
                    {
                        const auto& C = Node.Corners;
                        Out.Command(SymbolId::Command_MakeObjectTriangle,
                                    {Id, NumberLiteral(C[0].x), NumberLiteral(C[0].y), NumberLiteral(C[0].z),
                                     NumberLiteral(C[1].x), NumberLiteral(C[1].y), NumberLiteral(C[1].z),
                                     NumberLiteral(C[2].x), NumberLiteral(C[2].y), NumberLiteral(C[2].z)});
                        break;
                    }
                    case NodeKind::Model:
                        Out.Command(SymbolId::Command_LoadObject, {Quoted(Node.Asset), Id});
                        break;
                    default:
                        assert(false);
                        return;
                }

                if (Node.Position != glm::vec3(0.0f))
                    Out.Vector(SymbolId::Command_PositionObject, Node.Id, Node.Position);
                // A plain starts turned by 180 degrees, so its rotation is always
                // written: the editor's angles are then exactly the runtime's.
                if (Node.Rotation != glm::vec3(0.0f) || Node.Kind == NodeKind::Plain)
                    Out.Vector(SymbolId::Command_RotateObject, Node.Id, Node.Rotation);
                if (Node.Scale != glm::vec3(100.0f))
                    Out.Vector(SymbolId::Command_ScaleObject, Node.Id, Node.Scale);

                const Material& Mat = Node.Mat;
                if (!Mat.Texture.empty())
                    Out.Command(SymbolId::Command_TextureObject, {Id, Integer(Images.at(Mat.Texture))});
                if (Mat.Color)
                    Out.Command(SymbolId::Command_ColorObject, {Id, Color(*Mat.Color)});
                if (Mat.Ghost)
                {
                    if (Mat.GhostMode)
                        Out.Command(SymbolId::Command_GhostObjectOn, {Id, Integer(*Mat.GhostMode)});
                    else
                        Out.Command(SymbolId::Command_GhostObjectOn, {Id});
                }
                if (Mat.Flags != ObjectFlags{})
                {
                    const ObjectFlags& F = Mat.Flags;
                    // SET OBJECT's first flag is "solid": 0 draws the wireframe.
                    Out.Command(SymbolId::Command_SetObject,
                                {Id, Integer(F.Wireframe ? 0 : 1), Integer(F.Transparency), Integer(F.Cull ? 1 : 0),
                                 Integer(F.Filter ? 1 : 0), Integer(F.Lighting ? 1 : 0), Integer(F.Fog ? 1 : 0),
                                 Integer(F.Ambient ? 1 : 0)});
                }
                if (Mat.Diffuse)
                    Out.Command(SymbolId::Command_SetObjectDiffuse, {Id, Color(*Mat.Diffuse)});
                if (Mat.Ambience)
                    Out.Command(SymbolId::Command_SetObjectAmbience, {Id, Color(*Mat.Ambience)});
                if (Mat.Emissive)
                    Out.Command(SymbolId::Command_SetObjectEmissive, {Id, Color(*Mat.Emissive)});
                if (Mat.Specular)
                    Out.Command(SymbolId::Command_SetObjectSpecular, {Id, Color(*Mat.Specular)});
                if (Mat.SpecularPower)
                    Out.Command(SymbolId::Command_SetObjectSpecularPower, {Id, NumberLiteral(*Mat.SpecularPower)});
                if (Mat.CastShadow)
                    Out.Command(SymbolId::Command_SetShadowShadingOn, {Id});
            }

            // A copy starts with the original's transform and everything SameLook compares.
            void WriteInstance(Emitter& Out, const SceneNode& Node, const SceneNode& Original)
            {
                Out.Command(SymbolId::Command_InstanceObject, {Integer(Node.Id), Integer(Original.Id)});
                if (Node.Position != Original.Position)
                    Out.Vector(SymbolId::Command_PositionObject, Node.Id, Node.Position);
                if (Node.Rotation != Original.Rotation)
                    Out.Vector(SymbolId::Command_RotateObject, Node.Id, Node.Rotation);
                if (Node.Scale != Original.Scale)
                    Out.Vector(SymbolId::Command_ScaleObject, Node.Id, Node.Scale);
            }

            // FILL MATRIX with the most common height, then each height that differs.
            void WriteMatrixHeights(Emitter& Out, const SceneNode& Node)
            {
                const std::vector<float>& Heights = MatrixHeights(Node);
                const int Tile = MatrixTile(Node);
                if (Heights.empty() && Tile == 1)
                    return;
                float Base = 0.0f;
                if (!Heights.empty())
                {
                    std::map<float, int> Counts;
                    int Best = 0;
                    for (const float Height : Heights)
                        if (const int Count = ++Counts[Height]; Count > Best)
                        {
                            Best = Count;
                            Base = Height;
                        }
                }
                const std::string Id = Integer(Node.Id);
                Out.Command(SymbolId::Command_FillMatrix, {Id, NumberLiteral(Base), Integer(Tile)});
                const int Columns = std::max(1, Node.XSegments) + 1;
                for (std::size_t Index = 0; Index < Heights.size(); ++Index)
                    if (Heights[Index] != Base)
                        Out.Command(SymbolId::Command_SetMatrixHeight,
                                    {Id, Integer(static_cast<int>(Index) % Columns),
                                     Integer(static_cast<int>(Index) / Columns), NumberLiteral(Heights[Index])});
                Out.Command(SymbolId::Command_UpdateMatrix, {Id});
            }

            void WriteNode(Emitter& Out, const SceneNode& Node, const SceneNode* Original,
                           const std::map<std::string, int>& Images)
            {
                Out.Line(std::string("REM @") + MetadataTag(Node.Kind) + " " + Integer(Node.Id) + " " +
                         Quoted(Node.Name) + (Node.Painted ? " " + std::string(KPaintedTag) : std::string()));
                const std::string Id = Integer(Node.Id);
                if (IsObject(Node.Kind))
                {
                    if (Original != nullptr)
                        WriteInstance(Out, Node, *Original);
                    else
                        WriteObject(Out, Node, Images);
                    if (Node.Speed)
                        Out.Command(SymbolId::Command_SetObjectSpeed, {Id, Integer(*Node.Speed)});
                    if (Node.Frames)
                        Out.Command(Node.Loop ? SymbolId::Command_LoopObject : SymbolId::Command_PlayObject,
                                    {Id, Integer(Node.Frames->x), Integer(Node.Frames->y)});
                    return;
                }
                switch (Node.Kind)
                {
                    case NodeKind::Matrix:
                        Out.Command(SymbolId::Command_MakeMatrix,
                                    {Id, NumberLiteral(Node.Width), NumberLiteral(Node.Depth), Integer(Node.XSegments),
                                     Integer(Node.ZSegments)});
                        if (!Node.Mat.Texture.empty())
                            Out.Command(SymbolId::Command_PrepareMatrixTexture,
                                        {Id, Integer(Images.at(Node.Mat.Texture)), Integer(Node.TilesAcross),
                                         Integer(Node.TilesDown)});
                        WriteMatrixHeights(Out, Node);
                        if (Node.Position != glm::vec3(0.0f))
                            Out.Vector(SymbolId::Command_PositionMatrix, Node.Id, Node.Position);
                        break;
                    case NodeKind::Terrain:
                        Out.Command(SymbolId::Command_MakeTerrain, {Id, Quoted(Node.Asset)});
                        if (!Node.Mat.Texture.empty())
                            Out.Command(SymbolId::Command_TextureTerrain, {Id, Integer(Images.at(Node.Mat.Texture))});
                        // MAKE TERRAIN starts at Z = its depth, so the position is always written.
                        Out.Vector(SymbolId::Command_PositionTerrain, Node.Id, Node.Position);
                        break;
                    case NodeKind::Light:
                        Out.Command(SymbolId::Command_MakeLight, {Id});
                        switch (Node.Light)
                        {
                            case LightKind::Point:
                                Out.Vector(SymbolId::Command_SetPointLight, Node.Id, Node.Position);
                                break;
                            case LightKind::Spot:
                                // SET SPOT LIGHT resets the position and direction, so it comes first.
                                Out.Command(SymbolId::Command_SetSpotLight,
                                            {Id, NumberLiteral(Node.SpotInner), NumberLiteral(Node.SpotOuter)});
                                Out.Vector(SymbolId::Command_PositionLight, Node.Id, Node.Position);
                                Out.Vector(SymbolId::Command_RotateLight, Node.Id, Node.Rotation);
                                break;
                            case LightKind::Directional:
                                Out.Vector(SymbolId::Command_SetDirectionalLight, Node.Id, glm::vec3(0, 0, 1));
                                if (Node.Position != glm::vec3(0.0f))
                                    Out.Vector(SymbolId::Command_PositionLight, Node.Id, Node.Position);
                                Out.Vector(SymbolId::Command_RotateLight, Node.Id, Node.Rotation);
                                break;
                        }
                        if (Node.Range)
                            Out.Command(SymbolId::Command_SetLightRange, {Id, NumberLiteral(*Node.Range)});
                        if (Node.LightColor)
                            Out.Command(SymbolId::Command_ColorLight, {Id, Color(*Node.LightColor)});
                        break;
                    case NodeKind::Sound:
                        Out.Command(SymbolId::Command_Load3dsound, {Quoted(Node.Asset), Id});
                        if (Node.Position != glm::vec3(0.0f))
                            Out.Vector(SymbolId::Command_PositionSound, Node.Id, Node.Position);
                        if (Node.Volume)
                            Out.Command(SymbolId::Command_SetSoundVolume, {Id, Integer(*Node.Volume)});
                        if (Node.PlayOnLoad)
                            Out.Command(Node.Loop ? SymbolId::Command_LoopSound : SymbolId::Command_PlaySound, {Id});
                        break;
                    default:
                        assert(false);
                        break;
                }
            }

            void WriteEnvironment(Emitter& Out, const Environment& Env)
            {
                if (Env.AmbientPercent)
                    Out.Command(SymbolId::Command_SetAmbientLight, {Integer(*Env.AmbientPercent)});
                if (Env.AmbientColor)
                    Out.Command(SymbolId::Command_ColorAmbientLight, {Color(*Env.AmbientColor)});
                if (Env.Backdrop)
                    Out.Command(SymbolId::Command_ColorBackdrop, {Color(*Env.Backdrop)});
                if (Env.Fog)
                {
                    Out.Command(SymbolId::Command_FogOn);
                    if (Env.FogColor)
                        Out.Command(SymbolId::Command_FogColor, {Color(*Env.FogColor)});
                    if (Env.FogDistance)
                        Out.Command(SymbolId::Command_FogDistance, {NumberLiteral(*Env.FogDistance)});
                }
                if (Env.DefaultLightHidden)
                    Out.Command(SymbolId::Command_HideLight, {"0"});
                if (Env.DefaultLightRotation)
                    Out.Vector(SymbolId::Command_RotateLight, 0, *Env.DefaultLightRotation);
                if (Env.DefaultLightColor)
                    Out.Command(SymbolId::Command_ColorLight, {"0", Color(*Env.DefaultLightColor)});
                if (Env.ShadowMode)
                    Out.Vector(SymbolId::Command_SetShadowPosition, *Env.ShadowMode, Env.ShadowPosition);
            }

            void WriteDisplay(Emitter& Out, const DisplaySettings& Settings)
            {
                if (Settings.DisplayMode)
                {
                    const glm::ivec3& M = *Settings.DisplayMode;
                    if (Settings.Vsync)
                        Out.Command(SymbolId::Command_SetDisplayMode,
                                    {Integer(M.x), Integer(M.y), Integer(M.z), Integer(*Settings.Vsync)});
                    else
                        Out.Command(SymbolId::Command_SetDisplayMode, {Integer(M.x), Integer(M.y), Integer(M.z)});
                }
                if (Settings.Windowed)
                    Out.Command(*Settings.Windowed ? SymbolId::Command_SetWindowOn : SymbolId::Command_SetWindowOff);
                if (Settings.WindowSize)
                    Out.Command(SymbolId::Command_SetWindowSize,
                                {Integer(Settings.WindowSize->x), Integer(Settings.WindowSize->y)});
                if (Settings.WindowTitle)
                    Out.Command(SymbolId::Command_SetWindowTitle, {Quoted(*Settings.WindowTitle)});
                if (Settings.SyncOn)
                    Out.Command(SymbolId::Command_SyncOn);
                if (Settings.SyncRate)
                    Out.Command(SymbolId::Command_SyncRate, {Integer(*Settings.SyncRate)});
                if (Settings.HideMouse)
                    Out.Command(SymbolId::Command_HideMouse);
            }

            void WriteCamera(Emitter& Out, const Environment& Env)
            {
                const glm::vec3& P = Env.CameraPosition;
                const glm::vec3& R = Env.CameraRotation;
                Out.Command(SymbolId::Command_PositionCamera,
                            {NumberLiteral(P.x), NumberLiteral(P.y), NumberLiteral(P.z)});
                Out.Command(SymbolId::Command_RotateCamera,
                            {NumberLiteral(R.x), NumberLiteral(R.y), NumberLiteral(R.z)});
                if (Env.CameraFov)
                    Out.Command(SymbolId::Command_SetCameraFov, {NumberLiteral(*Env.CameraFov)});
                if (Env.CameraRange)
                    Out.Command(SymbolId::Command_SetCameraRange,
                                {NumberLiteral(Env.CameraRange->x), NumberLiteral(Env.CameraRange->y)});
            }

            // --- Reading ---------------------------------------------------

            struct Argument
            {
                enum class Kind : std::uint8_t
                {
                    Number,
                    String,
                };
                Kind Type = Kind::Number;
                double Value = 0.0;
                std::string Text;
            };

            bool EqualsIgnoreCase(std::string_view Left, std::string_view Right)
            {
                return Left.size() == Right.size() &&
                       std::equal(Left.begin(), Left.end(), Right.begin(),
                                  [](char A, char B)
                                  {
                                      return std::toupper(static_cast<unsigned char>(A)) ==
                                             std::toupper(static_cast<unsigned char>(B));
                                  });
            }

            std::optional<double> LiteralNumber(const lang::Expression& Value)
            {
                if (Value.Kind == lang::ExpressionKind::IntegerLiteral ||
                    Value.Kind == lang::ExpressionKind::RealLiteral)
                    return Value.Number;
                if (Value.Kind == lang::ExpressionKind::Unary && Value.Text == "-" && Value.Children.size() == 1)
                    if (const auto Inner = LiteralNumber(*Value.Children.front()))
                        return -*Inner;
                return std::nullopt;
            }

            // Literals, negated literals and RGB() of three integer literals.
            std::optional<Argument> ReadArgument(const lang::Expression& Value)
            {
                if (Value.Kind == lang::ExpressionKind::StringLiteral)
                    return Argument{Argument::Kind::String, 0.0, Value.Text};
                if (const auto Literal = LiteralNumber(Value))
                    return Argument{Argument::Kind::Number, *Literal, {}};
                if ((Value.Kind == lang::ExpressionKind::CallOrIndex || Value.Kind == lang::ExpressionKind::Call) &&
                    EqualsIgnoreCase(Value.Text, "RGB") && Value.Children.size() == 3)
                {
                    Rgb Packed = 0;
                    for (const auto& Child : Value.Children)
                    {
                        const auto Channel = LiteralNumber(*Child);
                        if (!Channel || *Channel < 0.0 || *Channel > 255.0 || std::floor(*Channel) != *Channel)
                            return std::nullopt;
                        Packed = (Packed << 8) | static_cast<Rgb>(*Channel);
                    }
                    return Argument{Argument::Kind::Number, static_cast<double>(Packed), {}};
                }
                return std::nullopt;
            }

            struct Metadata
            {
                std::map<std::uint32_t, std::pair<std::string, std::string>> Tags; // line -> tag, rest
            };

            // The lexer drops comments, so the editor's REM annotations are read from the text.
            Metadata ScanMetadata(const std::string& Text)
            {
                Metadata Result;
                std::uint32_t Line = 1;
                std::size_t Start = 0;
                while (Start <= Text.size())
                {
                    std::size_t End = Text.find('\n', Start);
                    if (End == std::string::npos)
                        End = Text.size();
                    std::string_view Row(Text.data() + Start, End - Start);
                    while (!Row.empty() && (Row.front() == ' ' || Row.front() == '\t'))
                        Row.remove_prefix(1);
                    while (!Row.empty() && (Row.back() == '\r' || Row.back() == ' '))
                        Row.remove_suffix(1);
                    if (Row.size() > 5 && EqualsIgnoreCase(Row.substr(0, 4), "REM ") && Row[4] == '@')
                    {
                        const std::string_view Body = Row.substr(5);
                        const std::size_t Space = Body.find(' ');
                        const std::string Tag(Body.substr(0, Space));
                        const std::string Rest(Space == std::string_view::npos ? std::string_view{}
                                                                               : Body.substr(Space + 1));
                        Result.Tags[Line] = {Tag, Rest};
                    }
                    if (End == Text.size())
                        break;
                    Start = End + 1;
                    ++Line;
                }
                return Result;
            }

            std::optional<IdSpace> SpaceOfTag(const std::string& Tag)
            {
                if (Tag == "object")
                    return IdSpace::Object;
                if (Tag == "matrix")
                    return IdSpace::Matrix;
                if (Tag == "terrain")
                    return IdSpace::Terrain;
                if (Tag == "light")
                    return IdSpace::Light;
                if (Tag == "sound")
                    return IdSpace::Sound;
                return std::nullopt;
            }

            class LevelReader
            {
              public:
                explicit LevelReader(LevelReadResult& Result, std::set<std::string>* Skipped = nullptr,
                                     bool World = false)
                    : Result_(Result), Skipped_(Skipped), World_(World)
                {
                }

                void Error(std::uint32_t Line, std::string Message)
                {
                    if (Skipped_ == nullptr)
                        Result_.Errors.push_back({Line, std::move(Message)});
                }

                // A call a program made, which is valid by definition; its overload still bounds Apply's arguments.
                void Call(const ReplayCall& Value)
                {
                    const std::string Name(lang::Symbol(Value.Symbol).CanonicalName);
                    if (lang::FindOverload(Value.Symbol, KSceneProfile, static_cast<int>(Value.Arguments.size())) ==
                        nullptr)
                    {
                        Skipped_->insert(Name);
                        return;
                    }
                    std::vector<Argument> Args;
                    Args.reserve(Value.Arguments.size());
                    for (const ReplayArgument& Item : Value.Arguments)
                        Args.push_back(
                            {Item.IsText ? Argument::Kind::String : Argument::Kind::Number, Item.Number, Item.Text});
                    Name_ = &Name;
                    Apply(Value.Symbol, Args);
                }

                void Statement(const lang::Statement& Value)
                {
                    const std::uint32_t Line = Value.Span.Begin.Line;
                    if (Value.Kind != lang::StatementKind::Command || !Value.Builtin)
                    {
                        Error(Line, "The Scene view only reads the statements it writes");
                        return;
                    }
                    std::vector<Argument> Args;
                    for (const auto& Expression : Value.Expressions)
                    {
                        const auto Arg = ReadArgument(*Expression);
                        if (!Arg)
                        {
                            Error(Line, "Arguments must be literal values: " + Value.Name);
                            return;
                        }
                        Args.push_back(*Arg);
                    }
                    const SymbolId Id = *Value.Builtin;
                    if (lang::SupportFor(lang::Symbol(Id), KSceneProfile) != lang::SupportStatus::Implemented ||
                        lang::FindOverload(Id, KSceneProfile, static_cast<int>(Args.size())) == nullptr)
                    {
                        Error(Line, Value.Name + " is not available in this form for the " +
                                        std::string(lang::ProfileName(KSceneProfile)) + " profile");
                        return;
                    }
                    Line_ = Line;
                    Name_ = &Value.Name;
                    Apply(Id, Args);
                }

                void Finish(const Metadata& Meta)
                {
                    for (const auto& [Line, Entry] : Meta.Tags)
                    {
                        const auto& [Tag, Rest] = Entry;
                        if (Tag == "level")
                        {
                            ReadBases(Line, Rest);
                            continue;
                        }
                        const auto Space = SpaceOfTag(Tag);
                        if (!Space)
                            continue;
                        int Id = 0;
                        const auto Parsed = std::from_chars(Rest.data(), Rest.data() + Rest.size(), Id);
                        const std::size_t Open = Rest.find('"');
                        const std::size_t Close = Rest.rfind('"');
                        SceneNode* Node = Indexed(*Space, Id);
                        if (Parsed.ec != std::errc() || Node == nullptr || Open == std::string::npos || Close <= Open)
                        {
                            Error(Line, "Unmatched editor annotation: @" + Tag + " " + Rest);
                            continue;
                        }
                        Node->Name = Rest.substr(Open + 1, Close - Open - 1);
                        Node->Painted = Rest.substr(Close + 1) == " " + std::string(KPaintedTag);
                    }
                }

              private:
                void ReadBases(std::uint32_t Line, const std::string& Rest)
                {
                    static const std::pair<const char*, IdSpace> Keys[] = {
                        {"objects", IdSpace::Object},   {"images", IdSpace::Image}, {"matrices", IdSpace::Matrix},
                        {"terrains", IdSpace::Terrain}, {"lights", IdSpace::Light}, {"sounds", IdSpace::Sound}};
                    std::istringstream Stream(Rest);
                    std::string Pair;
                    while (Stream >> Pair)
                    {
                        const std::size_t Equal = Pair.find('=');
                        bool Known = false;
                        for (const auto& [Key, Space] : Keys)
                        {
                            if (Equal == std::string::npos || Pair.compare(0, Equal, Key) != 0)
                                continue;
                            int Value = 0;
                            const auto Parsed =
                                std::from_chars(Pair.data() + Equal + 1, Pair.data() + Pair.size(), Value);
                            if (Parsed.ec == std::errc() && Value > 0)
                            {
                                Result_.Document.Base(Space) = Value;
                                Known = true;
                            }
                        }
                        if (!Known)
                            Error(Line, "Unknown level setting: " + Pair);
                    }
                }

                float F(const std::vector<Argument>& Args, std::size_t Index) const
                {
                    return static_cast<float>(Args[Index].Value);
                }

                int I(const std::vector<Argument>& Args, std::size_t Index) const
                {
                    return static_cast<int>(std::lround(Args[Index].Value));
                }

                glm::vec3 V(const std::vector<Argument>& Args, std::size_t First) const
                {
                    return {F(Args, First), F(Args, First + 1), F(Args, First + 2)};
                }

                bool S(const std::vector<Argument>& Args, std::size_t Index, std::string& Out)
                {
                    if (Args[Index].Type != Argument::Kind::String)
                    {
                        Error(Line_, *Name_ + " expects a file name in quotes");
                        return false;
                    }
                    Out = Args[Index].Text;
                    return true;
                }

                bool Numbers(const std::vector<Argument>& Args, std::size_t From = 0)
                {
                    for (std::size_t Index = From; Index < Args.size(); ++Index)
                        if (Args[Index].Type != Argument::Kind::Number)
                        {
                            Error(Line_, *Name_ + " expects numbers");
                            return false;
                        }
                    return true;
                }

                // Painted levels hold thousands of nodes, each named by several statements.
                SceneNode* Indexed(IdSpace Space, int Id)
                {
                    const auto Found = Index_.find({Space, Id});
                    return Found == Index_.end() ? nullptr : &Result_.Document.Nodes[Found->second];
                }

                SceneNode* Create(NodeKind Kind, int Id)
                {
                    const IdSpace Space = IdSpaceOf(Kind);
                    SceneNode* Used = Indexed(Space, Id);
                    if (Id <= 0 || (Used != nullptr && Skipped_ == nullptr))
                    {
                        Error(Line_, "Number " + Integer(Id) + " is already used or invalid");
                        return nullptr;
                    }
                    SceneNode Node;
                    Node.Kind = Kind;
                    Node.Id = Id;
                    Node.Name = DefaultNodeName(Kind, Id);
                    // A sound plays only when PLAY SOUND or LOOP SOUND follows.
                    Node.PlayOnLoad = Kind != NodeKind::Sound;
                    // A running program may have deleted the number's previous owner.
                    if (Used != nullptr)
                    {
                        *Used = std::move(Node);
                        return Used;
                    }
                    Index_[{Space, Id}] = Result_.Document.Nodes.size();
                    Result_.Document.Nodes.push_back(std::move(Node));
                    return &Result_.Document.Nodes.back();
                }

                SceneNode* Existing(IdSpace Space, int Id)
                {
                    SceneNode* Node = Indexed(Space, Id);
                    if (Node == nullptr)
                        Error(Line_, *Name_ + " refers to " + Integer(Id) + ", which this level does not create");
                    return Node;
                }

                SceneNode* Object(int Id)
                {
                    return Existing(IdSpace::Object, Id);
                }

                bool Texture(int ImageId, std::string& Out)
                {
                    const auto Found = Images_.find(ImageId);
                    if (Found == Images_.end())
                    {
                        Error(Line_, *Name_ + " uses image " + Integer(ImageId) + " before LOAD IMAGE");
                        return false;
                    }
                    Out = Found->second;
                    return true;
                }

                // What a world settings file may hold, beside the display.
                static bool IsEnvironmentCommand(SymbolId Id)
                {
                    switch (Id)
                    {
                        case SymbolId::Command_SetAmbientLight:
                        case SymbolId::Command_ColorAmbientLight:
                        case SymbolId::Command_ColorBackdrop:
                        case SymbolId::Command_FogOn:
                        case SymbolId::Command_FogColor:
                        case SymbolId::Command_FogDistance:
                        case SymbolId::Command_RotateLight:
                        case SymbolId::Command_ColorLight:
                        case SymbolId::Command_HideLight:
                        case SymbolId::Command_SetShadowPosition:
                        case SymbolId::Command_AutocamOff:
                        case SymbolId::Command_PositionCamera:
                        case SymbolId::Command_RotateCamera:
                        case SymbolId::Command_SetCameraFov:
                        case SymbolId::Command_SetCameraRange:
                            return true;
                        default:
                            return false;
                    }
                }

                // Returns whether the command belongs to the display.
                bool ApplyDisplay(SymbolId Id, const std::vector<Argument>& A)
                {
                    DisplaySettings& Display = Result_.Document.Display;
                    switch (Id)
                    {
                        case SymbolId::Command_SetDisplayMode:
                            if (A.size() < 3)
                            {
                                Error(Line_, "SET DISPLAY MODE is written with a depth");
                                return true;
                            }
                            Display.DisplayMode = glm::ivec3(I(A, 0), I(A, 1), I(A, 2));
                            if (A.size() == 4)
                                Display.Vsync = I(A, 3);
                            return true;
                        case SymbolId::Command_SetWindowOn:
                            Display.Windowed = true;
                            return true;
                        case SymbolId::Command_SetWindowOff:
                            Display.Windowed = false;
                            return true;
                        case SymbolId::Command_SetWindowSize:
                            Display.WindowSize = glm::ivec2(I(A, 0), I(A, 1));
                            return true;
                        case SymbolId::Command_SetWindowTitle:
                            if (A[0].Type == Argument::Kind::String)
                                Display.WindowTitle = A[0].Text;
                            return true;
                        case SymbolId::Command_SyncOn:
                            Display.SyncOn = true;
                            return true;
                        case SymbolId::Command_SyncRate:
                            Display.SyncRate = I(A, 0);
                            return true;
                        case SymbolId::Command_HideMouse:
                            Display.HideMouse = true;
                            return true;
                        default:
                            return false;
                    }
                }

                void Apply(SymbolId Id, const std::vector<Argument>& A)
                {
                    if (World_)
                    {
                        if (ApplyDisplay(Id, A))
                            return;
                        if (!IsEnvironmentCommand(Id))
                        {
                            Error(Line_, *Name_ + " belongs in the level file, not in the world settings");
                            return;
                        }
                    }
                    Environment& Env = Result_.Document.Env;
                    if (Skipped_ != nullptr)
                    {
                        if (Id == SymbolId::Command_CloneObject)
                            Id = SymbolId::Command_InstanceObject;
                        else if (Id == SymbolId::Command_FogOff)
                        {
                            Env.Fog = false;
                            return;
                        }
                        else if (Id == SymbolId::Command_ScaleObjectTexture)
                        {
                            // Other stages have no place in the preview.
                            SceneNode* Node = Indexed(IdSpace::Object, I(A, 0));
                            const bool Stage = A.size() == 4 && I(A, 1) != 0;
                            if (Node != nullptr && !Stage)
                                Node->Mat.TextureScale *= glm::vec2(F(A, A.size() - 2), F(A, A.size() - 1));
                            return;
                        }
                    }
                    switch (Id)
                    {
                        case SymbolId::Command_LoadImage:
                        {
                            std::string Path;
                            if (A.size() != 2 || !S(A, 0, Path) || !Numbers(A, 1))
                                return;
                            if (!Images_.emplace(I(A, 1), Path).second)
                            {
                                Error(Line_, "Image " + Integer(I(A, 1)) + " is loaded twice");
                                Images_[I(A, 1)] = Path;
                            }
                            return;
                        }
                        case SymbolId::Command_LoadObject:
                        {
                            std::string Path;
                            if (A.size() != 2 || !S(A, 0, Path) || !Numbers(A, 1))
                                return;
                            if (SceneNode* Node = Create(NodeKind::Model, I(A, 1)))
                                Node->Asset = Path;
                            return;
                        }
                        case SymbolId::Command_Load3dsound:
                        {
                            std::string Path;
                            if (A.size() != 2 || !S(A, 0, Path) || !Numbers(A, 1))
                            {
                                if (A.size() != 2)
                                    Error(Line_, "LOAD 3DSOUND is written with the file and the sound alone");
                                return;
                            }
                            if (SceneNode* Node = Create(NodeKind::Sound, I(A, 1)))
                                Node->Asset = Path;
                            return;
                        }
                        case SymbolId::Command_MakeTerrain:
                        {
                            std::string Path;
                            if (A[0].Type != Argument::Kind::Number || !S(A, 1, Path))
                                return;
                            if (SceneNode* Node = Create(NodeKind::Terrain, I(A, 0)))
                                Node->Asset = Path;
                            return;
                        }
                        default:
                            break;
                    }

                    if (!Numbers(A))
                        return;
                    switch (Id)
                    {
                        case SymbolId::Command_MakeObjectCube:
                            if (SceneNode* Node = Create(NodeKind::Cube, I(A, 0)))
                                Node->Size = F(A, 1);
                            break;
                        case SymbolId::Command_MakeObjectBox:
                            if (SceneNode* Node = Create(NodeKind::Box, I(A, 0)))
                                Node->Extent = V(A, 1);
                            break;
                        case SymbolId::Command_MakeObjectSphere:
                            if (SceneNode* Node = Create(NodeKind::Sphere, I(A, 0)))
                            {
                                Node->Size = F(A, 1);
                                if (A.size() == 4)
                                    Node->Detail = glm::ivec2(I(A, 2), I(A, 3));
                            }
                            break;
                        case SymbolId::Command_MakeObjectCylinder:
                            if (SceneNode* Node = Create(NodeKind::Cylinder, I(A, 0)))
                                Node->Size = F(A, 1);
                            break;
                        case SymbolId::Command_MakeObjectCone:
                            if (SceneNode* Node = Create(NodeKind::Cone, I(A, 0)))
                                Node->Size = F(A, 1);
                            break;
                        case SymbolId::Command_MakeObjectPlain:
                            if (SceneNode* Node = Create(NodeKind::Plain, I(A, 0)))
                            {
                                Node->Extent = glm::vec3(F(A, 1), F(A, 2), 0.0f);
                                if (A.size() == 4)
                                    Node->PlainOrientation = I(A, 3);
                            }
                            break;
                        case SymbolId::Command_MakeObjectTriangle:
                            if (SceneNode* Node = Create(NodeKind::Triangle, I(A, 0)))
                                Node->Corners = {V(A, 1), V(A, 4), V(A, 7)};
                            break;
                        case SymbolId::Command_InstanceObject:
                            // Copied first: adding the node moves the original.
                            if (const SceneNode* Original = Object(I(A, 1)))
                            {
                                SceneNode Copy = *Original;
                                if (SceneNode* Node = Create(Copy.Kind, I(A, 0)))
                                {
                                    // Animation is each object's own, as it is in WriteNode.
                                    Copy.Frames.reset();
                                    Copy.Speed.reset();
                                    Copy.Loop = true;
                                    Copy.Id = Node->Id;
                                    Copy.Name = Node->Name;
                                    *Node = std::move(Copy);
                                }
                            }
                            break;
                        case SymbolId::Command_PositionObject:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Position = V(A, 1);
                            break;
                        case SymbolId::Command_RotateObject:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Rotation = V(A, 1);
                            break;
                        case SymbolId::Command_ScaleObject:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Scale = V(A, 1);
                            break;
                        case SymbolId::Command_TextureObject:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Texture(I(A, 1), Node->Mat.Texture);
                            break;
                        case SymbolId::Command_ColorObject:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.Color = static_cast<Rgb>(I(A, 1));
                            break;
                        case SymbolId::Command_GhostObjectOn:
                            if (SceneNode* Node = Object(I(A, 0)))
                            {
                                Node->Mat.Ghost = true;
                                if (A.size() == 2)
                                    Node->Mat.GhostMode = I(A, 1);
                            }
                            break;
                        case SymbolId::Command_SetObject:
                            if (A.size() != 8 && Skipped_ == nullptr)
                            {
                                Error(Line_, "SET OBJECT is written with all seven flags");
                                break;
                            }
                            if (SceneNode* Node = Object(I(A, 0)))
                            {
                                // A program may give only the first flags; the others keep their value.
                                ObjectFlags& Flags = Node->Mat.Flags;
                                const std::size_t Count = A.size();
                                Flags.Wireframe = Count > 1 ? I(A, 1) == 0 : Flags.Wireframe;
                                Flags.Transparency = Count > 2 ? I(A, 2) : Flags.Transparency;
                                Flags.Cull = Count > 3 ? I(A, 3) != 0 : Flags.Cull;
                                Flags.Filter = Count > 4 ? I(A, 4) != 0 : Flags.Filter;
                                Flags.Lighting = Count > 5 ? I(A, 5) != 0 : Flags.Lighting;
                                Flags.Fog = Count > 6 ? I(A, 6) != 0 : Flags.Fog;
                                Flags.Ambient = Count > 7 ? I(A, 7) != 0 : Flags.Ambient;
                            }
                            break;
                        case SymbolId::Command_SetObjectDiffuse:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.Diffuse = static_cast<Rgb>(I(A, 1));
                            break;
                        case SymbolId::Command_SetObjectAmbience:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.Ambience = static_cast<Rgb>(I(A, 1));
                            break;
                        case SymbolId::Command_SetObjectEmissive:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.Emissive = static_cast<Rgb>(I(A, 1));
                            break;
                        case SymbolId::Command_SetObjectSpecular:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.Specular = static_cast<Rgb>(I(A, 1));
                            break;
                        case SymbolId::Command_SetObjectSpecularPower:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.SpecularPower = F(A, 1);
                            break;
                        case SymbolId::Command_SetShadowShadingOn:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Mat.CastShadow = true;
                            break;
                        case SymbolId::Command_SetObjectSpeed:
                            if (SceneNode* Node = Object(I(A, 0)))
                                Node->Speed = I(A, 1);
                            break;
                        case SymbolId::Command_PlayObject:
                        case SymbolId::Command_LoopObject:
                            if (A.size() != 3)
                            {
                                Error(Line_, *Name_ + " is written with its first and last frame");
                                break;
                            }
                            if (SceneNode* Node = Object(I(A, 0)))
                            {
                                Node->Frames = glm::ivec2(I(A, 1), I(A, 2));
                                Node->Loop = Id == SymbolId::Command_LoopObject;
                            }
                            break;

                        case SymbolId::Command_MakeMatrix:
                            if (SceneNode* Node = Create(NodeKind::Matrix, I(A, 0)))
                            {
                                Node->Width = F(A, 1);
                                Node->Depth = F(A, 2);
                                Node->XSegments = I(A, 3);
                                Node->ZSegments = I(A, 4);
                            }
                            break;
                        case SymbolId::Command_PrepareMatrixTexture:
                            if (SceneNode* Node = Existing(IdSpace::Matrix, I(A, 0)))
                            {
                                Texture(I(A, 1), Node->Mat.Texture);
                                Node->TilesAcross = I(A, 2);
                                Node->TilesDown = I(A, 3);
                            }
                            break;
                        case SymbolId::Command_FillMatrix:
                            if (SceneNode* Node = Existing(IdSpace::Matrix, I(A, 0)))
                            {
                                Node->Heights.assign(MatrixVertexCount(*Node), F(A, 1));
                                Node->Tile = I(A, 2);
                            }
                            break;
                        case SymbolId::Command_SetMatrixHeight:
                            if (SceneNode* Node = Existing(IdSpace::Matrix, I(A, 0)))
                            {
                                const int X = I(A, 1), Z = I(A, 2);
                                if (X < 0 || Z < 0 || X > Node->XSegments || Z > Node->ZSegments)
                                {
                                    Error(Line_, "SET MATRIX HEIGHT is outside matrix " + Integer(Node->Id));
                                    break;
                                }
                                if (Node->Heights.size() != MatrixVertexCount(*Node))
                                    Node->Heights.assign(MatrixVertexCount(*Node), 0.0f);
                                Node->Heights[static_cast<std::size_t>(Z) * (Node->XSegments + 1) + X] = F(A, 3);
                            }
                            break;
                        case SymbolId::Command_UpdateMatrix:
                            Existing(IdSpace::Matrix, I(A, 0));
                            break;
                        case SymbolId::Command_PositionMatrix:
                            if (SceneNode* Node = Existing(IdSpace::Matrix, I(A, 0)))
                                Node->Position = V(A, 1);
                            break;
                        case SymbolId::Command_TextureTerrain:
                            if (SceneNode* Node = Existing(IdSpace::Terrain, I(A, 0)))
                                Texture(I(A, 1), Node->Mat.Texture);
                            break;
                        case SymbolId::Command_PositionTerrain:
                            if (SceneNode* Node = Existing(IdSpace::Terrain, I(A, 0)))
                                Node->Position = V(A, 1);
                            break;

                        case SymbolId::Command_MakeLight:
                            Create(NodeKind::Light, I(A, 0));
                            break;
                        case SymbolId::Command_SetPointLight:
                            if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                            {
                                Node->Light = LightKind::Point;
                                Node->Position = V(A, 1);
                            }
                            break;
                        case SymbolId::Command_SetSpotLight:
                            if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                            {
                                Node->Light = LightKind::Spot;
                                Node->SpotInner = F(A, 1);
                                Node->SpotOuter = F(A, 2);
                            }
                            break;
                        case SymbolId::Command_SetDirectionalLight:
                            if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                            {
                                Node->Light = LightKind::Directional;
                                // Same angles ROTATE LIGHT would need for this direction.
                                const glm::vec3 D = V(A, 1);
                                const float Length = std::sqrt(D.x * D.x + D.y * D.y + D.z * D.z);
                                if (Length > 0.0f)
                                    Node->Rotation = glm::vec3(std::asin(-D.y / Length) * 180.0f / KPi,
                                                               std::atan2(D.x, D.z) * 180.0f / KPi, 0.0f);
                            }
                            break;
                        case SymbolId::Command_PositionLight:
                            if (A.size() == 4)
                                if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                                    Node->Position = V(A, 1);
                            break;
                        case SymbolId::Command_RotateLight:
                            if (A.size() != 4)
                                break;
                            if (I(A, 0) == 0)
                                Env.DefaultLightRotation = V(A, 1);
                            else if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                                Node->Rotation = V(A, 1);
                            break;
                        case SymbolId::Command_SetLightRange:
                            if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                                Node->Range = F(A, 1);
                            break;
                        case SymbolId::Command_ColorLight:
                            if (A.size() != 2)
                            {
                                Error(Line_, "COLOR LIGHT is written as COLOR LIGHT light, RGB(r, g, b)");
                                break;
                            }
                            if (I(A, 0) == 0)
                                Env.DefaultLightColor = static_cast<Rgb>(I(A, 1));
                            else if (SceneNode* Node = Existing(IdSpace::Light, I(A, 0)))
                                Node->LightColor = static_cast<Rgb>(I(A, 1));
                            break;
                        case SymbolId::Command_HideLight:
                            if (I(A, 0) == 0)
                                Env.DefaultLightHidden = true;
                            else
                                Error(Line_, "Only light 0 is hidden by the level");
                            break;

                        case SymbolId::Command_PositionSound:
                            if (SceneNode* Node = Existing(IdSpace::Sound, I(A, 0)))
                                Node->Position = V(A, 1);
                            break;
                        case SymbolId::Command_SetSoundVolume:
                            if (SceneNode* Node = Existing(IdSpace::Sound, I(A, 0)))
                                Node->Volume = I(A, 1);
                            break;
                        case SymbolId::Command_PlaySound:
                        case SymbolId::Command_LoopSound:
                            if (A.size() != 1)
                            {
                                Error(Line_, *Name_ + " is written with the sound alone");
                                break;
                            }
                            if (SceneNode* Node = Existing(IdSpace::Sound, I(A, 0)))
                            {
                                Node->PlayOnLoad = true;
                                Node->Loop = Id == SymbolId::Command_LoopSound;
                            }
                            break;

                        case SymbolId::Command_SetAmbientLight:
                            Env.AmbientPercent = I(A, 0);
                            break;
                        case SymbolId::Command_ColorAmbientLight:
                            Env.AmbientColor = static_cast<Rgb>(I(A, 0));
                            break;
                        case SymbolId::Command_ColorBackdrop:
                            Env.Backdrop = static_cast<Rgb>(I(A, A.size() - 1));
                            break;
                        case SymbolId::Command_FogOn:
                            Env.Fog = true;
                            break;
                        case SymbolId::Command_FogColor:
                            Env.FogColor = static_cast<Rgb>(I(A, 0));
                            break;
                        case SymbolId::Command_FogDistance:
                            Env.FogDistance = F(A, 0);
                            break;
                        case SymbolId::Command_SetShadowPosition:
                            Env.ShadowMode = I(A, 0);
                            Env.ShadowPosition = V(A, 1);
                            break;

                        case SymbolId::Command_AutocamOff:
                            Env.UseCamera = true;
                            break;
                        case SymbolId::Command_PositionCamera:
                            Env.UseCamera = true;
                            Env.CameraPosition = V(A, 0);
                            break;
                        case SymbolId::Command_RotateCamera:
                            Env.UseCamera = true;
                            Env.CameraRotation = V(A, 0);
                            break;
                        case SymbolId::Command_SetCameraFov:
                            Env.CameraFov = F(A, 0);
                            break;
                        case SymbolId::Command_SetCameraRange:
                            Env.CameraRange = glm::vec2(F(A, 0), F(A, 1));
                            break;
                        default:
                            if (Skipped_ != nullptr)
                                Skipped_->insert(*Name_);
                            Error(Line_, "The Scene view does not edit " + *Name_);
                            break;
                    }
                }

                LevelReadResult& Result_;
                std::map<std::pair<IdSpace, int>, std::size_t> Index_;
                std::map<int, std::string> Images_;
                // Set when replaying a program: nothing is an error, and this names what is not understood.
                std::set<std::string>* Skipped_ = nullptr;
                // Set when reading a world settings file: no nodes, and the display as well.
                bool World_ = false;
                std::uint32_t Line_ = 0;
                const std::string* Name_ = nullptr;
            };

            // Parses `Text` and returns the body of its only function, which must be `Function`.
            const lang::FunctionDeclaration* SingleFunction(const lang::Program& Program, const std::string& Function,
                                                            std::vector<ReadError>& Errors)
            {
                for (const auto& Diagnostic : Program.Diagnostics)
                    if (Diagnostic.Severity == lang::DiagnosticSeverity::Error)
                        Errors.push_back({Diagnostic.Span.Begin.Line, Diagnostic.Message});
                if (!Errors.empty())
                    return nullptr;
                if (!Program.Statements.empty() || !Program.Includes.empty() || !Program.Types.empty() ||
                    !Program.Constants.empty())
                {
                    const std::uint32_t Line =
                        Program.Statements.empty() ? 1 : Program.Statements.front().Span.Begin.Line;
                    Errors.push_back({Line, "Only " + Function + "() may be declared in this file"});
                    return nullptr;
                }
                if (Program.Functions.size() != 1 || !EqualsIgnoreCase(Program.Functions.front().Name, Function))
                {
                    Errors.push_back({1, "Expected a single FUNCTION " + Function + "()"});
                    return nullptr;
                }
                const lang::FunctionDeclaration& Declaration = Program.Functions.front();
                if (!Declaration.Parameters.empty() || Declaration.FinalReturn)
                {
                    Errors.push_back(
                        {Declaration.Span.Begin.Line, Function + "() takes no parameters and returns nothing"});
                    return nullptr;
                }
                return &Declaration;
            }

            lang::Program Parse(const std::string& Text, const std::string& LogicalName)
            {
                auto Source = std::make_shared<lang::SourceDocument>();
                Source->LogicalPath = LogicalName;
                Source->Text = Text;
                Source->Profile = KSceneProfile;
                return lang::LexAndParse(Source);
            }
        } // namespace

        std::string NumberLiteral(float Value)
        {
            if (Value == 0.0f || !std::isfinite(Value))
                return "0";
            char Buffer[64];
            const auto Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), Value, std::chars_format::fixed);
            return std::string(Buffer, Result.ptr);
        }

        std::string WriteLevel(const SceneDocument& Document)
        {
            Emitter Out;
            Out.Line("REM OpenDBC level. Edited by the Studio Scene view.");
            Out.Line("REM @level objects=" + Integer(Document.Base(IdSpace::Object)) + " images=" +
                     Integer(Document.Base(IdSpace::Image)) + " matrices=" + Integer(Document.Base(IdSpace::Matrix)) +
                     " terrains=" + Integer(Document.Base(IdSpace::Terrain)) + " lights=" +
                     Integer(Document.Base(IdSpace::Light)) + " sounds=" + Integer(Document.Base(IdSpace::Sound)));
            Out.Line("FUNCTION " + LevelFunctionName(Document) + "()");
            Out.Push();

            std::map<std::string, int> Images;
            int NextImage = Document.Base(IdSpace::Image);
            for (const SceneNode& Node : Document.Nodes)
            {
                const std::string Texture = NodeTexture(Node);
                if (Texture.empty() || Images.count(Texture) != 0)
                    continue;
                Images[Texture] = NextImage;
                Out.Command(SymbolId::Command_LoadImage, {Quoted(Texture), Integer(NextImage)});
                ++NextImage;
            }

            // Painted objects copy the first object that looks the same.
            std::vector<const SceneNode*> Originals;
            for (const SceneNode& Node : Document.Nodes)
            {
                const SceneNode* Original = nullptr;
                if (IsObject(Node.Kind))
                {
                    const auto Found = std::find_if(Originals.begin(), Originals.end(),
                                                    [&](const SceneNode* Other) { return SameLook(*Other, Node); });
                    if (Found == Originals.end())
                        Originals.push_back(&Node);
                    else if (Node.Painted)
                        Original = *Found;
                }
                WriteNode(Out, Node, Original, Images);
            }

            Out.Pop();
            Out.Line("ENDFUNCTION");
            return Out.Take();
        }

        LevelReadResult ReadLevel(const std::string& Text, const std::string& Name)
        {
            LevelReadResult Result;
            Result.Document.Name = Name;
            const std::string Function = LevelFunctionName(Result.Document);
            const lang::Program Program = Parse(Text, Name + ".dba");
            const lang::FunctionDeclaration* Declaration = SingleFunction(Program, Function, Result.Errors);
            if (Declaration == nullptr)
                return Result;

            LevelReader Reader(Result);
            for (const lang::Statement& Value : Declaration->Body)
                Reader.Statement(Value);
            Reader.Finish(ScanMetadata(Text));
            return Result;
        }

        ReplayResult ReplayCalls(std::span<const ReplayCall> Calls, const std::string& Name)
        {
            LevelReadResult Level;
            Level.Document.Name = Name;
            ReplayResult Result;
            LevelReader Reader(Level, &Result.Skipped);
            for (const ReplayCall& Value : Calls)
                Reader.Call(Value);
            Result.Document = std::move(Level.Document);
            return Result;
        }

        std::string WriteWorld(const SceneDocument& Document)
        {
            Emitter Out;
            Out.Line("REM OpenDBC world settings. Edited by the Studio Scene view.");
            Out.Line("FUNCTION " + WorldFunctionName(Document) + "()");
            Out.Push();
            WriteDisplay(Out, Document.Display);
            const Environment& Env = Document.Env;
            // Before any MAKE OBJECT, which would otherwise move the camera.
            if (Env.UseCamera)
                Out.Command(SymbolId::Command_AutocamOff);
            WriteEnvironment(Out, Env);
            if (Env.UseCamera)
                WriteCamera(Out, Env);
            Out.Pop();
            Out.Line("ENDFUNCTION");
            return Out.Take();
        }

        std::vector<ReadError> ReadWorld(const std::string& Text, SceneDocument& Document)
        {
            LevelReadResult Result;
            const lang::Program Program = Parse(Text, KWorldFileName);
            const lang::FunctionDeclaration* Declaration =
                SingleFunction(Program, WorldFunctionName(Document), Result.Errors);
            if (Declaration == nullptr)
                return std::move(Result.Errors);

            Result.Document = std::move(Document);
            LevelReader Reader(Result, nullptr, true);
            for (const lang::Statement& Value : Declaration->Body)
                Reader.Statement(Value);
            Document = std::move(Result.Document);
            return std::move(Result.Errors);
        }

        std::filesystem::path LevelRelativePath(const std::string& Name)
        {
            const std::string Clean = SanitizeLevelName(Name);
            return dbc::PathFromUtf8(KLevelFolder) / Clean / (Clean + ".dba");
        }

        std::filesystem::path WorldRelativePath(const std::string& Name)
        {
            return dbc::PathFromUtf8(KLevelFolder) / SanitizeLevelName(Name) / KWorldFileName;
        }
    } // namespace scene

} // namespace dbc::studio
