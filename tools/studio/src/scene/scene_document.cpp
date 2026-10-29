#include "scene/scene_document.hpp"

#include <algorithm>
#include <cctype>

#include "language/language_catalogue.hpp"

namespace dbc::studio
{
    namespace scene
    {
        namespace lang = dbc::language;

        IdSpace IdSpaceOf(NodeKind Kind)
        {
            switch (Kind)
            {
                case NodeKind::Matrix:
                    return IdSpace::Matrix;
                case NodeKind::Terrain:
                    return IdSpace::Terrain;
                case NodeKind::Light:
                    return IdSpace::Light;
                case NodeKind::Sound:
                    return IdSpace::Sound;
                default:
                    return IdSpace::Object;
            }
        }

        bool IsMarker(NodeKind Kind)
        {
            return Kind == NodeKind::Light || Kind == NodeKind::Sound;
        }

        const char* NodeKindLabel(NodeKind Kind)
        {
            switch (Kind)
            {
                case NodeKind::Cube:
                    return "Cube";
                case NodeKind::Box:
                    return "Box";
                case NodeKind::Sphere:
                    return "Sphere";
                case NodeKind::Cylinder:
                    return "Cylinder";
                case NodeKind::Cone:
                    return "Cone";
                case NodeKind::Plain:
                    return "Plain";
                case NodeKind::Triangle:
                    return "Triangle";
                case NodeKind::Model:
                    return "Model";
                case NodeKind::Matrix:
                    return "Matrix";
                case NodeKind::Terrain:
                    return "Terrain";
                case NodeKind::Light:
                    return "Light";
                case NodeKind::Sound:
                    return "3D Sound";
            }
            return "Node";
        }

        namespace
        {
            lang::SymbolId CreatingSymbol(NodeKind Kind)
            {
                switch (Kind)
                {
                    case NodeKind::Cube:
                        return lang::SymbolId::Command_MakeObjectCube;
                    case NodeKind::Box:
                        return lang::SymbolId::Command_MakeObjectBox;
                    case NodeKind::Sphere:
                        return lang::SymbolId::Command_MakeObjectSphere;
                    case NodeKind::Cylinder:
                        return lang::SymbolId::Command_MakeObjectCylinder;
                    case NodeKind::Cone:
                        return lang::SymbolId::Command_MakeObjectCone;
                    case NodeKind::Plain:
                        return lang::SymbolId::Command_MakeObjectPlain;
                    case NodeKind::Triangle:
                        return lang::SymbolId::Command_MakeObjectTriangle;
                    case NodeKind::Model:
                        return lang::SymbolId::Command_LoadObject;
                    case NodeKind::Matrix:
                        return lang::SymbolId::Command_MakeMatrix;
                    case NodeKind::Terrain:
                        return lang::SymbolId::Command_MakeTerrain;
                    case NodeKind::Light:
                        return lang::SymbolId::Command_MakeLight;
                    case NodeKind::Sound:
                        return lang::SymbolId::Command_Load3dsound;
                }
                return lang::SymbolId::Command_MakeObjectCube;
            }
        } // namespace

        bool KindAvailable(NodeKind Kind, lang::LanguageProfile Profile)
        {
            return lang::SupportFor(lang::Symbol(CreatingSymbol(Kind)), Profile) == lang::SupportStatus::Implemented;
        }

        bool StaticAvailable(lang::LanguageProfile Profile)
        {
            return lang::SupportFor(lang::Symbol(lang::SymbolId::Command_MakeStaticObject), Profile) ==
                   lang::SupportStatus::Implemented;
        }

        int OpaqueTransparency(lang::LanguageProfile Profile)
        {
            return Profile == lang::LanguageProfile::Dbc ? 1 : 0;
        }

        ObjectFlags DefaultObjectFlags(lang::LanguageProfile Profile)
        {
            ObjectFlags Flags;
            Flags.Transparency = OpaqueTransparency(Profile);
            return Flags;
        }

        int SceneDocument::NextId(IdSpace Space) const
        {
            int Candidate = Base(Space);
            for (;;)
            {
                if (Find(Space, Candidate) == nullptr)
                    return Candidate;
                ++Candidate;
            }
        }

        SceneNode* SceneDocument::Find(IdSpace Space, int Id)
        {
            for (SceneNode& Node : Nodes)
                if (Node.Id == Id && IdSpaceOf(Node.Kind) == Space)
                    return &Node;
            return nullptr;
        }

        const SceneNode* SceneDocument::Find(IdSpace Space, int Id) const
        {
            for (const SceneNode& Node : Nodes)
                if (Node.Id == Id && IdSpaceOf(Node.Kind) == Space)
                    return &Node;
            return nullptr;
        }

        std::string SceneDocument::CopyName(const std::string& OriginalName) const
        {
            // The number after the last '_', or nullopt when that suffix is not all digits.
            const auto Suffix = [](const std::string& Text, std::size_t Underscore) -> std::optional<int>
            {
                if (Underscore == std::string::npos || Underscore + 1 == Text.size() || Text.size() - Underscore > 10)
                    return std::nullopt;
                for (std::size_t Index = Underscore + 1; Index < Text.size(); ++Index)
                    if (Text[Index] < '0' || Text[Index] > '9')
                        return std::nullopt;
                return std::stoi(Text.substr(Underscore + 1));
            };
            const std::size_t Underscore = OriginalName.rfind('_');
            const std::string Stem =
                Suffix(OriginalName, Underscore) ? OriginalName.substr(0, Underscore) : OriginalName;
            int Highest = 0;
            for (const SceneNode& Node : Nodes)
            {
                const std::size_t At = Node.Name.rfind('_');
                if (At == Stem.size() && Node.Name.compare(0, At, Stem) == 0)
                    if (const std::optional<int> Count = Suffix(Node.Name, At))
                        Highest = std::max(Highest, *Count);
            }
            return Stem + "_" + std::to_string(Highest + 1);
        }

        SceneNode& SceneDocument::Add(NodeKind Kind)
        {
            SceneNode Node;
            Node.Kind = Kind;
            Node.Id = NextId(IdSpaceOf(Kind));
            Node.Name = std::string(NodeKindLabel(Kind)) + " " + std::to_string(Node.Id);
            Node.Mat.Flags = DefaultObjectFlags(Profile);
            if (Kind == NodeKind::Light)
                Node.Position = glm::vec3(0.0f, 50.0f, 0.0f);
            Nodes.push_back(std::move(Node));
            return Nodes.back();
        }

        std::size_t MatrixVertexCount(const SceneNode& Node)
        {
            return static_cast<std::size_t>(std::max(1, Node.XSegments) + 1) *
                   static_cast<std::size_t>(std::max(1, Node.ZSegments) + 1);
        }

        const std::vector<float>& MatrixHeights(const SceneNode& Node)
        {
            static const std::vector<float> KFlat;
            return Node.Heights.size() == MatrixVertexCount(Node) ? Node.Heights : KFlat;
        }

        int MatrixTile(const SceneNode& Node)
        {
            const int Tiles = Node.Mat.Texture.empty() ? 1 : std::max(1, Node.TilesAcross) * std::max(1, Node.TilesDown);
            return std::clamp(Node.Tile, 1, Tiles);
        }

        std::string SanitizeLevelName(const std::string& Name)
        {
            std::string Result;
            for (const char Character : Name)
            {
                const auto Byte = static_cast<unsigned char>(Character);
                if (std::isalnum(Byte) != 0 && Byte < 0x80)
                    Result.push_back(Character);
                else if (!Result.empty() && Result.back() != '_')
                    Result.push_back('_');
            }
            while (!Result.empty() && Result.back() == '_')
                Result.pop_back();
            if (Result.empty() || std::isdigit(static_cast<unsigned char>(Result.front())) != 0)
                Result.insert(0, "Level");
            return Result;
        }

        std::string LevelFunctionName(const SceneDocument& Document)
        {
            return "LoadLevel_" + SanitizeLevelName(Document.Name);
        }

        void SceneHistory::Reset(const SceneDocument& Document)
        {
            States_.assign(1, Document);
            Current_ = 0;
        }

        void SceneHistory::Commit(const SceneDocument& Document)
        {
            if (!States_.empty() && States_[Current_] == Document)
                return;
            States_.resize(States_.empty() ? 0 : Current_ + 1);
            States_.push_back(Document);
            Current_ = States_.size() - 1;
        }

        bool SceneHistory::CanUndo() const
        {
            return Current_ > 0;
        }

        bool SceneHistory::CanRedo() const
        {
            return Current_ + 1 < States_.size();
        }

        const SceneDocument& SceneHistory::Undo()
        {
            if (CanUndo())
                --Current_;
            return States_[Current_];
        }

        const SceneDocument& SceneHistory::Redo()
        {
            if (CanRedo())
                ++Current_;
            return States_[Current_];
        }

        void ApplySharedEdit(const SceneNode& Before, const SceneNode& After, SceneNode& Target)
        {
            const auto Take = [](const auto& From, const auto& To, auto& Into)
            {
                if (From != To)
                    Into = To;
            };
            const auto TakeAxes = [](const glm::vec3& From, const glm::vec3& To, glm::vec3& Into)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                    if (From[Axis] != To[Axis])
                        Into[Axis] = To[Axis];
            };
            const bool Object = IdSpaceOf(Target.Kind) == IdSpace::Object;
            TakeAxes(Before.Position, After.Position, Target.Position);
            if (Object || Target.Kind == NodeKind::Light)
                TakeAxes(Before.Rotation, After.Rotation, Target.Rotation);
            if (Object)
            {
                TakeAxes(Before.Scale, After.Scale, Target.Scale);
                Take(Before.Static, After.Static, Target.Static);
            }

            if (Target.Kind == After.Kind)
            {
                Take(Before.Size, After.Size, Target.Size);
                TakeAxes(Before.Extent, After.Extent, Target.Extent);
                Take(Before.Detail, After.Detail, Target.Detail);
                Take(Before.PlainOrientation, After.PlainOrientation, Target.PlainOrientation);
                for (std::size_t Corner = 0; Corner < Target.Corners.size(); ++Corner)
                    TakeAxes(Before.Corners[Corner], After.Corners[Corner], Target.Corners[Corner]);
                Take(Before.Asset, After.Asset, Target.Asset);
                Take(Before.Width, After.Width, Target.Width);
                Take(Before.Depth, After.Depth, Target.Depth);
                Take(Before.XSegments, After.XSegments, Target.XSegments);
                Take(Before.ZSegments, After.ZSegments, Target.ZSegments);
                Take(Before.TilesAcross, After.TilesAcross, Target.TilesAcross);
                Take(Before.TilesDown, After.TilesDown, Target.TilesDown);
                Take(Before.Tile, After.Tile, Target.Tile);
                Take(Before.Light, After.Light, Target.Light);
                Take(Before.Range, After.Range, Target.Range);
                Take(Before.SpotInner, After.SpotInner, Target.SpotInner);
                Take(Before.SpotOuter, After.SpotOuter, Target.SpotOuter);
                Take(Before.LightColor, After.LightColor, Target.LightColor);
                Take(Before.PlayOnLoad, After.PlayOnLoad, Target.PlayOnLoad);
                Take(Before.Volume, After.Volume, Target.Volume);
                Take(Before.Frames, After.Frames, Target.Frames);
                Take(Before.Loop, After.Loop, Target.Loop);
                Take(Before.Speed, After.Speed, Target.Speed);
            }
            // A static object no longer exists to animate.
            if (Target.Static)
            {
                Target.Frames.reset();
                Target.Speed.reset();
            }

            const Material& From = Before.Mat;
            const Material& To = After.Mat;
            Material& Into = Target.Mat;
            if (!IsMarker(Target.Kind))
                Take(From.Texture, To.Texture, Into.Texture);
            if (!Object)
                return;
            Take(From.Color, To.Color, Into.Color);
            Take(From.Ghost, To.Ghost, Into.Ghost);
            Take(From.GhostMode, To.GhostMode, Into.GhostMode);
            Take(From.Flags.Wireframe, To.Flags.Wireframe, Into.Flags.Wireframe);
            Take(From.Flags.Transparency, To.Flags.Transparency, Into.Flags.Transparency);
            Take(From.Flags.Cull, To.Flags.Cull, Into.Flags.Cull);
            Take(From.Flags.Filter, To.Flags.Filter, Into.Flags.Filter);
            Take(From.Flags.Lighting, To.Flags.Lighting, Into.Flags.Lighting);
            Take(From.Flags.Fog, To.Flags.Fog, Into.Flags.Fog);
            Take(From.Flags.Ambient, To.Flags.Ambient, Into.Flags.Ambient);
            Take(From.Diffuse, To.Diffuse, Into.Diffuse);
            Take(From.Ambience, To.Ambience, Into.Ambience);
            Take(From.Emissive, To.Emissive, Into.Emissive);
            Take(From.Specular, To.Specular, Into.Specular);
            Take(From.SpecularPower, To.SpecularPower, Into.SpecularPower);
            Take(From.CastShadow, To.CastShadow, Into.CastShadow);
        }
    } // namespace scene

} // namespace dbc::studio
