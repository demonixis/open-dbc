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
                default:
                    return IdSpace::Object;
            }
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
    } // namespace scene

} // namespace dbc::studio
