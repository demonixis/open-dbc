#include "scene_capture.hpp"

#include "atomic_file.hpp"
#include "engine/systems/camera_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/light_manager.hpp"
#include "engine/systems/matrix_manager.hpp"
#include "engine/systems/object_manager.hpp"
#include "language/language_catalogue.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace dbc::interpreter
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        // A program that keeps calling scene commands during setup stays bounded; the Studio reports the cut.
        constexpr std::size_t KMaxCalls = 200'000;

        bool HasWord(std::string_view Name, std::string_view Word)
        {
            std::size_t Position = 0;
            while ((Position = Name.find(Word, Position)) != std::string_view::npos)
            {
                const std::size_t End = Position + Word.size();
                if ((Position == 0 || Name[Position - 1] == ' ') && (End == Name.size() || Name[End] == ' '))
                    return true;
                Position = End;
            }
            return false;
        }

        bool IsSceneCommand(language::SymbolId Symbol)
        {
            static const std::vector<bool> Table = []
            {
                constexpr std::array<std::string_view, 16> Words{
                    "OBJECT",    "LIMB", "MESH",     "MATRIX",  "LIGHT", "TERRAIN", "CAMERA", "AUTOCAM",
                    "PARTICLES", "FOG",  "BACKDROP", "3DSOUND", "SOUND", "SHADOW",  "BSP",    "VERTEXDATA"};
                std::vector<bool> Result(static_cast<std::size_t>(language::SymbolId::Count), false);
                for (const language::SymbolDescriptor& Descriptor : language::Symbols())
                {
                    if (Descriptor.Kind != language::SymbolKind::Command)
                        continue;
                    const std::string_view Name = Descriptor.CanonicalName;
                    Result[static_cast<std::size_t>(Descriptor.Id)] =
                        Name == "LOAD IMAGE" || std::any_of(Words.begin(), Words.end(),
                                                            [&](std::string_view Word) { return HasWord(Name, Word); });
                }
                return Result;
            }();
            return Table[static_cast<std::size_t>(Symbol)];
        }

        std::size_t Intern(std::vector<std::filesystem::path>& Table, const std::filesystem::path& Value)
        {
            const auto Found = std::find(Table.begin(), Table.end(), Value);
            if (Found != Table.end())
                return static_cast<std::size_t>(Found - Table.begin());
            Table.push_back(Value);
            return Table.size() - 1;
        }

        Json Paths(const std::vector<std::filesystem::path>& Table)
        {
            Json Result = Json::array();
            for (const std::filesystem::path& Value : Table)
                Result.push_back(dbc::GenericPathToUtf8(Value));
            return Result;
        }

        Json Triple(float X, float Y, float Z)
        {
            return Json::array({X, Y, Z});
        }

        Json ObjectState(int Id, const Object3D& Object, std::set<int>& Images)
        {
            Vec3 Min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
            Vec3 Max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest()};
            bool HasVertices = false;
            for (const Mesh& Value : Object.Meshes)
                for (const Vec3& Vertex : Value.Geometry().Vertices)
                {
                    HasVertices = true;
                    Min = {std::min(Min.X, Vertex.X), std::min(Min.Y, Vertex.Y), std::min(Min.Z, Vertex.Z)};
                    Max = {std::max(Max.X, Vertex.X), std::max(Max.Y, Vertex.Y), std::max(Max.Z, Vertex.Z)};
                }
            if (!HasVertices)
                Min = Max = {};
            if (Object.TextureImageId > 0)
                Images.insert(Object.TextureImageId);

            Json Result = Json::object();
            Result["id"] = Id;
            Result["position"] = Triple(Object.PosX, Object.PosY, Object.PosZ);
            Result["rotation"] = Triple(Object.RotX, Object.RotY, Object.RotZ);
            Result["scale"] = Triple(Object.ScaleX * 100.0f, Object.ScaleY * 100.0f, Object.ScaleZ * 100.0f);
            Result["visible"] = Object.Visible;
            Result["screen"] = Object.LockToScreen;
            Result["image"] = Object.TextureImageId;
            Result["min"] = Triple(Min.X, Min.Y, Min.Z);
            Result["max"] = Triple(Max.X, Max.Y, Max.Z);
            return Result;
        }

        Json MatrixState(int Id, const Matrix& Value, std::set<int>& Images)
        {
            if (Value.ImageId > 0)
                Images.insert(Value.ImageId);
            Json Result = Json::object();
            Result["id"] = Id;
            Result["size"] = Json::array({Value.Width, Value.Depth});
            Result["segments"] = Json::array({Value.SegX, Value.SegZ});
            Result["position"] = Triple(Value.PosX, Value.PosY, Value.PosZ);
            Result["image"] = Value.ImageId;
            Result["tiles"] = Json::array({Value.TilesX, Value.TilesY});
            Result["heights"] = Value.Heights;
            Result["tile"] = Value.Tiles;
            return Result;
        }

        void AddEngineState(Json& Capture, const EngineKernel& Kernel)
        {
            std::set<int> Images;
            Json Objects = Json::array();
            Kernel.Objects().ForEachObject(
                [&](int Id, const Object3D& Object)
                {
                    // Static objects have negative numbers and no statement of their own.
                    if (Id > 0)
                        Objects.push_back(ObjectState(Id, Object, Images));
                });
            Capture["objects"] = std::move(Objects);

            Json Lights = Json::array();
            Kernel.Lights().ForEachLight(
                [&](int Id, const Light3D& Light)
                {
                    Json Result = Json::object();
                    Result["id"] = Id;
                    Result["type"] = Light.Type;
                    Result["visible"] = Light.Visible;
                    Result["position"] = Triple(Light.PosX, Light.PosY, Light.PosZ);
                    Result["rotation"] = Triple(Light.RotX, Light.RotY, Light.RotZ);
                    Result["direction"] = Triple(Light.DirX, Light.DirY, Light.DirZ);
                    Result["range"] = Light.Range;
                    Result["color"] = Light.Color;
                    Lights.push_back(std::move(Result));
                });
            Capture["lights"] = std::move(Lights);

            Json Matrices = Json::array();
            Kernel.Matrices().ForEachMatrix([&](int Id, const Matrix& Value)
                                            { Matrices.push_back(MatrixState(Id, Value, Images)); });
            Capture["matrices"] = std::move(Matrices);

            Json Terrains = Json::array();
            Kernel.Matrices().ForEachTerrain([&](int Id, const Matrix& Value)
                                             { Terrains.push_back(MatrixState(Id, Value, Images)); });
            Capture["terrains"] = std::move(Terrains);

            Json ImageTable = Json::array();
            for (const int Id : Images)
                if (const Image* Value = Kernel.ImageStoreRef().GetImage(Id))
                    ImageTable.push_back(Json::array({Id, Value->SourcePath}));
            Capture["images"] = std::move(ImageTable);

            const CameraSystem& Camera = Kernel.Camera();
            Json CameraState = Json::object();
            CameraState["position"] = Triple(Camera.PositionX(), Camera.PositionY(), Camera.PositionZ());
            CameraState["rotation"] = Triple(Camera.AngleX(), Camera.AngleY(), Camera.AngleZ());
            CameraState["fov"] = Camera.Fov();
            Capture["camera"] = std::move(CameraState);
        }
    } // namespace

    void SceneRecorder::Record(language::SymbolId Symbol, std::span<const vm::Value> Arguments,
                               const language::SourceSpan& Span, int Frame, const std::filesystem::path& AssetRoot)
    {
        if (!IsSceneCommand(Symbol))
            return;
        if (Calls_.size() >= KMaxCalls)
        {
            Truncated_ = true;
            return;
        }
        Json Values = Json::array();
        for (const vm::Value& Argument : Arguments)
        {
            if (const std::string* Text = std::get_if<std::string>(&Argument.Data()))
                Values.push_back(*Text);
            else if (const std::int64_t* Whole = std::get_if<std::int64_t>(&Argument.Data()))
                Values.push_back(*Whole);
            else if (const double* Real = std::get_if<double>(&Argument.Data()))
                Values.push_back(*Real);
            else
                Values.push_back(nullptr);
        }
        Calls_.push_back(
            Json::array({static_cast<std::uint32_t>(Symbol), Intern(Files_, Span.LogicalPath), Span.Begin.Line,
                         Span.Begin.Column, Frame, Intern(Roots_, AssetRoot), std::move(Values)}));
    }

    bool SceneRecorder::Write(const std::filesystem::path& Path, language::LanguageProfile Profile,
                              std::string_view Status, std::size_t Frames, const EngineKernel* Kernel,
                              std::string& Error) const
    {
        Json Capture = Json::object();
        Capture["schema"] = "opendbc-scene-capture";
        Capture["version"] = 1;
        Capture["profile"] = language::ProfileName(Profile);
        Capture["status"] = Status;
        Capture["frames"] = Frames;
        Capture["truncated"] = Truncated_;
        Capture["files"] = Paths(Files_);
        Capture["roots"] = Paths(Roots_);
        Capture["calls"] = Calls_;
        if (Kernel)
            AddEngineState(Capture, *Kernel);
        // Strings a program passes may be in any encoding; replacing bad bytes keeps dump() from throwing.
        return AtomicWriteText(Path, Capture.dump(-1, ' ', false, Json::error_handler_t::replace), &Error);
    }
} // namespace dbc::interpreter
