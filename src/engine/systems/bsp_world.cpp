#include "bsp_world.hpp"
#include "../engine_math.hpp"
#include "camera_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "log.hpp"
#include "object_manager.hpp"
#include "renderer_system.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace dbc
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::size_t KTextureNameBytes = 21;
        constexpr float KPlaneEpsilon = 0.001f;

        class ByteReader
        {
          public:
            explicit ByteReader(std::span<const uint8_t> Bytes) : Bytes_(Bytes) {}

            template <typename T> bool Read(T& Value)
            {
                if (Bytes_.size() - Offset_ < sizeof(T))
                    return false;
                std::memcpy(&Value, Bytes_.data() + Offset_, sizeof(T));
                Offset_ += sizeof(T);
                return true;
            }

            bool ReadVec3(Vec3& Value)
            {
                return Read(Value.X) && Read(Value.Y) && Read(Value.Z);
            }

            bool Skip(std::size_t Count, const uint8_t*& Start)
            {
                if (Bytes_.size() - Offset_ < Count)
                    return false;
                Start = Bytes_.data() + Offset_;
                Offset_ += Count;
                return true;
            }

            // Rejects counts that cannot fit in the remaining bytes before anything is allocated.
            [[nodiscard]] bool CanHold(int64_t Count, std::size_t MinimumRecordBytes) const
            {
                return Count >= 0 &&
                       static_cast<uint64_t>(Count) <= (Bytes_.size() - Offset_) / MinimumRecordBytes;
            }

          private:
            std::span<const uint8_t> Bytes_;
            std::size_t Offset_ = 0;
        };

        bool Fail(std::string* Error, const std::string& Message)
        {
            if (Error)
                *Error = Message;
            return false;
        }

        bool ReadCount(ByteReader& Reader, std::size_t RecordBytes, const char* What, int32_t& Count,
                       std::string* Error)
        {
            if (!Reader.Read(Count))
                return Fail(Error, std::string("truncated before the ") + What + " count");
            if (!Reader.CanHold(Count, RecordBytes))
                return Fail(Error, std::string("invalid ") + What + " count " + std::to_string(Count));
            return true;
        }

        bool ResolveExisting(const fs::path& Path, fs::path& Output, std::string* Error)
        {
            bool Exists = false;
            if (!PathExists(Path, Exists, Error))
                return false;
            if (Exists)
            {
                Output = Path;
                return true;
            }
            const fs::path Base = Path.is_absolute() ? fs::path{} : fs::current_path();
            return ResolveCaseInsensitivePath(Output, Base, Path, Error);
        }

        bool ReadFileBytes(const fs::path& Path, std::vector<uint8_t>& Output)
        {
            std::ifstream Stream(Path, std::ios::binary);
            if (!Stream)
                return false;
            Output.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
            return !Stream.bad();
        }

        // Texture names are relative to the map and may not leave its folder.
        bool ConfinedTextureName(const std::string& Name)
        {
            const fs::path Relative = fs::path(Name).lexically_normal();
            if (Name.empty() || Relative.is_absolute() || Relative.has_root_name())
                return false;
            for (const fs::path& Part : Relative)
            {
                if (Part == "..")
                    return false;
            }
            return true;
        }
    } // namespace

    bool ParseDbProBsp(std::span<const uint8_t> Bytes, BspMap& Output, std::string* Error)
    {
        BspMap Map;
        ByteReader Reader(Bytes);
        int32_t Count = 0;

        if (!ReadCount(Reader, 13, "node", Count, Error))
            return false;
        Map.Nodes.resize(static_cast<std::size_t>(Count));
        for (BspNode& Node : Map.Nodes)
        {
            uint8_t IsLeaf = 0;
            if (!Reader.Read(IsLeaf) || !Reader.Read(Node.Plane) || !Reader.Read(Node.Front) || !Reader.Read(Node.Back))
                return Fail(Error, "truncated node array");
            Node.IsLeaf = IsLeaf != 0;
        }

        if (!ReadCount(Reader, 24, "plane", Count, Error))
            return false;
        Map.Planes.resize(static_cast<std::size_t>(Count));
        for (BspPlane& Plane : Map.Planes)
        {
            if (!Reader.ReadVec3(Plane.Point) || !Reader.ReadVec3(Plane.Normal))
                return Fail(Error, "truncated plane array");
        }

        if (!ReadCount(Reader, 36, "leaf", Count, Error))
            return false;
        Map.Leaves.resize(static_cast<std::size_t>(Count));
        for (BspLeaf& Leaf : Map.Leaves)
        {
            if (!Reader.Read(Leaf.FirstPolygon) || !Reader.Read(Leaf.EndPolygon) || !Reader.Read(Leaf.PvsOffset) ||
                !Reader.ReadVec3(Leaf.BoxMin) || !Reader.ReadVec3(Leaf.BoxMax))
                return Fail(Error, "truncated leaf array");
        }

        // Smallest polygon: vertex and index counts, normal and texture slot.
        if (!ReadCount(Reader, 18, "polygon", Count, Error))
            return false;
        Map.Polygons.resize(static_cast<std::size_t>(Count));
        for (BspPolygon& Polygon : Map.Polygons)
        {
            uint16_t VertexCount = 0;
            if (!Reader.Read(VertexCount) || !Reader.CanHold(VertexCount, 24))
                return Fail(Error, "truncated polygon vertices");
            Polygon.Vertices.resize(VertexCount);
            for (BspVertex& Vertex : Polygon.Vertices)
            {
                if (!Reader.ReadVec3(Vertex.Position) || !Reader.Read(Vertex.Color) || !Reader.Read(Vertex.U) ||
                    !Reader.Read(Vertex.V))
                    return Fail(Error, "truncated polygon vertices");
            }
            uint16_t IndexCount = 0;
            if (!Reader.Read(IndexCount) || !Reader.CanHold(IndexCount, 2))
                return Fail(Error, "truncated polygon indices");
            if (IndexCount % 3 != 0)
                return Fail(Error, "polygon index count " + std::to_string(IndexCount) + " is not a triangle list");
            Polygon.Indices.resize(IndexCount);
            for (uint16_t& Index : Polygon.Indices)
            {
                if (!Reader.Read(Index))
                    return Fail(Error, "truncated polygon indices");
                if (Index >= VertexCount)
                    return Fail(Error, "polygon index " + std::to_string(Index) + " is out of range");
            }
            if (!Reader.ReadVec3(Polygon.Normal) || !Reader.Read(Polygon.Texture))
                return Fail(Error, "truncated polygon");
        }

        if (!ReadCount(Reader, 1, "PVS byte", Count, Error))
            return false;
        const uint8_t* PvsStart = nullptr;
        if (!Reader.Skip(static_cast<std::size_t>(Count), PvsStart))
            return Fail(Error, "truncated PVS data");
        Map.Pvs.assign(PvsStart, PvsStart + Count);

        uint16_t TextureCount = 0;
        const uint8_t* Names = nullptr;
        if (!Reader.Read(TextureCount) || !Reader.Skip(TextureCount * KTextureNameBytes, Names))
            return Fail(Error, "truncated texture table");
        Map.TextureNames.reserve(TextureCount);
        for (uint16_t Index = 0; Index < TextureCount; ++Index)
        {
            const char* Name = reinterpret_cast<const char*>(Names + Index * KTextureNameBytes);
            Map.TextureNames.emplace_back(Name, strnlen(Name, KTextureNameBytes));
        }

        if (Map.Nodes.empty())
            return Fail(Error, "the map has no nodes");
        for (const BspNode& Node : Map.Nodes)
        {
            const std::size_t FrontLimit = Node.IsLeaf ? Map.Leaves.size() : Map.Nodes.size();
            if (Node.Plane >= Map.Planes.size() || Node.Front >= FrontLimit || Node.Back < -1 ||
                (Node.Back >= 0 && static_cast<std::size_t>(Node.Back) >= Map.Nodes.size()))
                return Fail(Error, "a node refers to a missing plane, leaf or node");
        }
        for (const BspLeaf& Leaf : Map.Leaves)
        {
            if (Leaf.FirstPolygon < 0 || Leaf.EndPolygon < Leaf.FirstPolygon ||
                static_cast<std::size_t>(Leaf.EndPolygon) > Map.Polygons.size() || Leaf.PvsOffset < 0 ||
                (!Map.Pvs.empty() && static_cast<std::size_t>(Leaf.PvsOffset) >= Map.Pvs.size()))
                return Fail(Error, "a leaf refers to missing polygons or PVS data");
        }
        for (const BspPolygon& Polygon : Map.Polygons)
        {
            if (Polygon.Texture >= Map.TextureNames.size())
                return Fail(Error, "a polygon refers to a missing texture slot");
        }

        Output = std::move(Map);
        return true;
    }

    BspFileFormat DetectQuakeBspFormat(std::span<const uint8_t> Bytes)
    {
        if (Bytes.size() < 8)
            return BspFileFormat::Unknown;
        int32_t First = 0;
        int32_t Version = 0;
        std::memcpy(&First, Bytes.data(), 4);
        std::memcpy(&Version, Bytes.data() + 4, 4);
        if (std::memcmp(Bytes.data(), "IBSP", 4) == 0)
        {
            if (Version == 38)
                return BspFileFormat::Quake2;
            if (Version == 46 || Version == 47)
                return BspFileFormat::Quake3;
            return BspFileFormat::Unknown;
        }
        if (First == 29)
            return BspFileFormat::Quake1;
        if (First == 30)
            return BspFileFormat::HalfLife;
        return BspFileFormat::Unknown;
    }

    const char* BspFileFormatName(BspFileFormat Format)
    {
        switch (Format)
        {
        case BspFileFormat::Quake1:
            return "Quake 1";
        case BspFileFormat::HalfLife:
            return "Half-Life";
        case BspFileFormat::Quake2:
            return "Quake 2";
        case BspFileFormat::Quake3:
            return "Quake 3";
        case BspFileFormat::Unknown:
            break;
        }
        return "unknown";
    }

    int FindBspLeaf(const BspMap& Map, const Vec3& Position)
    {
        if (Map.Nodes.empty())
            return -1;
        std::size_t NodeIndex = 0;
        // Bounded walk: a malformed tree with a cycle cannot hang the frame.
        for (std::size_t Step = 0; Step <= Map.Nodes.size(); ++Step)
        {
            const BspNode& Node = Map.Nodes[NodeIndex];
            const BspPlane& Plane = Map.Planes[Node.Plane];
            const float Distance = Vec3Dot(Vec3Sub(Position, Plane.Point), Plane.Normal);
            if (Distance >= -KPlaneEpsilon)
            {
                if (Node.IsLeaf)
                    return static_cast<int>(Node.Front);
                NodeIndex = Node.Front;
            }
            else
            {
                if (Node.Back < 0)
                    return -1;
                NodeIndex = static_cast<std::size_t>(Node.Back);
            }
        }
        return -1;
    }

    void DecodeBspPvs(const BspMap& Map, int Leaf, std::vector<uint8_t>& Visible)
    {
        const std::size_t LeafCount = Map.Leaves.size();
        Visible.assign(LeafCount, 0);
        if (Leaf < 0 || static_cast<std::size_t>(Leaf) >= LeafCount)
            return;
        // A map compiled without visibility data sees every leaf.
        if (Map.Pvs.empty())
        {
            Visible.assign(LeafCount, 1);
            return;
        }
        std::size_t Offset = static_cast<std::size_t>(Map.Leaves[static_cast<std::size_t>(Leaf)].PvsOffset);
        std::size_t Current = 0;
        while (Current < LeafCount && Offset < Map.Pvs.size())
        {
            const uint8_t Byte = Map.Pvs[Offset++];
            if (Byte != 0)
            {
                for (int Bit = 0; Bit < 8 && Current < LeafCount; ++Bit, ++Current)
                    Visible[Current] = (Byte >> Bit) & 1u;
                continue;
            }
            // A zero byte is followed by the number of zero bytes in the run.
            if (Offset >= Map.Pvs.size())
                break;
            Current += static_cast<std::size_t>(Map.Pvs[Offset++]) * 8u;
        }
    }

    BspWorldSystem::BspWorldSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    BspWorldSystem::~BspWorldSystem() = default;

    bool BspWorldSystem::Load(const std::string& Pk3Path, const std::vector<std::string>& MapPaths,
                              std::string* Error)
    {
        fs::path Found;
        for (const std::string& Candidate : MapPaths)
        {
            if (Candidate.empty())
                continue;
            if (!ResolveExisting(fs::path(Candidate), Found, Error))
                return false;
            if (!Found.empty())
                break;
        }
        const std::string Requested = MapPaths.empty() ? std::string{} : MapPaths.front();
        if (Found.empty())
        {
            fs::path Archive;
            if (!Pk3Path.empty() && !ResolveExisting(fs::path(Pk3Path), Archive, Error))
                return false;
            if (!Archive.empty())
                return Fail(Error, "LOAD BSP cannot read maps from pk3 archives (Quake 3): '" + Pk3Path + "'");
            return Fail(Error, "LOAD BSP cannot find BSP file '" + Requested + "'");
        }

        std::vector<uint8_t> Bytes;
        if (!ReadFileBytes(Found, Bytes))
            return Fail(Error, "LOAD BSP cannot read '" + Found.string() + "'");
        BspMap Parsed;
        std::string ParseError;
        if (!ParseDbProBsp(Bytes, Parsed, &ParseError))
        {
            const BspFileFormat Format = DetectQuakeBspFormat(Bytes);
            if (Format != BspFileFormat::Unknown)
                return Fail(Error, std::string("LOAD BSP cannot read ") + BspFileFormatName(Format) + " maps: '" +
                                       Found.string() + "'");
            return Fail(Error, "LOAD BSP cannot read '" + Found.string() + "': " + ParseError);
        }

        Delete();
        Map_ = std::move(Parsed);
        LoadTextures(Found.parent_path().string());
        BuildCollisionTriangles();
        Loaded_ = true;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    void BspWorldSystem::LoadTextures(const std::string& MapDirectory)
    {
        TextureImages_.assign(Map_.TextureNames.size(), 0);
        // Slots often repeat a name: each distinct file is resolved and reported once.
        std::unordered_map<std::string, int> Resolved;
        for (std::size_t Index = 0; Index < Map_.TextureNames.size(); ++Index)
        {
            const std::string& Name = Map_.TextureNames[Index];
            if (const auto Found = Resolved.find(Name); Found != Resolved.end())
            {
                TextureImages_[Index] = Found->second;
                continue;
            }
            TextureImages_[Index] = LoadTexture(MapDirectory, Name);
            Resolved.emplace(Name, TextureImages_[Index]);
        }
    }

    int BspWorldSystem::LoadTexture(const std::string& MapDirectory, const std::string& Name)
    {
        if (!ConfinedTextureName(Name))
        {
            DBC_LOG_WARN("LOAD BSP ignores texture '{}' outside the map folder", Name);
            return 0;
        }
        static constexpr std::array<const char*, 5> KConvertedExtensions{".png", ".jpg", ".dds", ".tga", ".bmp"};
        const fs::path Directory(MapDirectory);
        fs::path Resolved;
        std::string ResolveError;
        if (!ResolveCaseInsensitivePath(Resolved, Directory, fs::path(Name), &ResolveError))
            DBC_LOG_WARN("LOAD BSP cannot inspect texture '{}': {}", Name, ResolveError);
        // The shipped examples carry converted media: the same stem with another image extension.
        for (std::size_t Extension = 0; Resolved.empty() && Extension < KConvertedExtensions.size(); ++Extension)
        {
            fs::path Alternative = fs::path(Name).replace_extension(KConvertedExtensions[Extension]);
            if (!ResolveCaseInsensitivePath(Resolved, Directory, Alternative, nullptr))
                Resolved.clear();
        }
        if (Resolved.empty())
        {
            // DBPro drew the polygons untextured when a texture failed to load.
            DBC_LOG_WARN("LOAD BSP cannot find texture '{}'", Name);
            return 0;
        }
        const int ImageId = Kernel_.ImageStoreRef().LoadImageInternalFromPath(Resolved.string());
        if (ImageId == 0)
            DBC_LOG_WARN("LOAD BSP cannot load texture '{}'", Resolved.string());
        return ImageId;
    }

    void BspWorldSystem::Delete()
    {
        if (!Loaded_)
            return;
        Map_ = BspMap{};
        TextureImages_.clear();
        Triangles_.clear();
        Loaded_ = false;
        Kernel_.Renderer().InvalidateTriCache();
    }

    int BspWorldSystem::TextureImage(int TextureIndex) const
    {
        if (TextureIndex < 0 || static_cast<std::size_t>(TextureIndex) >= TextureImages_.size())
            return 0;
        return TextureImages_[static_cast<std::size_t>(TextureIndex)];
    }

    void BspWorldSystem::SetCullingCamera(int CameraId)
    {
        CullingCamera_ = CameraId;
        Kernel_.Renderer().InvalidateTriCache();
    }

    void BspWorldSystem::BuildCollisionTriangles()
    {
        Triangles_.clear();
        for (const BspPolygon& Polygon : Map_.Polygons)
        {
            for (std::size_t Index = 0; Index + 2 < Polygon.Indices.size(); Index += 3)
            {
                const Vec3& A = Polygon.Vertices[Polygon.Indices[Index]].Position;
                const Vec3& B = Polygon.Vertices[Polygon.Indices[Index + 1]].Position;
                const Vec3& C = Polygon.Vertices[Polygon.Indices[Index + 2]].Position;
                // The stored normal decides the solid side whatever the winding.
                const bool Agrees = Vec3Dot(Vec3Cross(Vec3Sub(B, A), Vec3Sub(C, A)), Polygon.Normal) >= 0.0f;
                Triangles_.push_back(Agrees ? MakeSlideTriangle(A, B, C) : MakeSlideTriangle(A, C, B));
            }
        }
    }

    bool BspWorldSystem::AcceptsSetup(int Index, const char* Command, std::string* Error)
    {
        if (Index < 1 || Index > KMaxCollisionIndex)
            return Fail(Error, std::string(Command) + " collision index must be between 1 and 24");
        if (Loaded_)
            return true;
        if (!WarnedSetupWithoutWorld_)
        {
            DBC_LOG_WARN("{} has no effect before LOAD BSP", Command);
            WarnedSetupWithoutWorld_ = true;
        }
        return true;
    }

    bool BspWorldSystem::EntityPosition(const Collision& Slot, Vec3& Output) const
    {
        if (Slot.Object)
        {
            const Object3D* Object = Kernel_.Objects().GetObject(Slot.Entity);
            if (!Object)
                return false;
            Output = {Object->PosX, Object->PosY - Slot.HeightAdjustment, Object->PosZ};
            return true;
        }
        const CameraSystem& Camera = Kernel_.Camera();
        if (Camera.CameraExist(Slot.Entity) == 0 || !Camera.PositionX(Output.X, Slot.Entity) ||
            !Camera.PositionY(Output.Y, Slot.Entity) || !Camera.PositionZ(Output.Z, Slot.Entity))
            return false;
        Output.Y -= Slot.HeightAdjustment;
        return true;
    }

    void BspWorldSystem::MoveEntity(const Collision& Slot, const Vec3& Position)
    {
        const float Y = Position.Y + Slot.HeightAdjustment;
        if (Slot.Object)
            Kernel_.Objects().PositionObject(Slot.Entity, Position.X, Y, Position.Z);
        else
            Kernel_.Camera().PositionCamera(Slot.Entity, Position.X, Y, Position.Z);
    }

    void BspWorldSystem::CaptureStart(Collision& Slot)
    {
        Vec3 Position;
        if (EntityPosition(Slot, Position))
            Slot.Start = Position;
    }

    bool BspWorldSystem::SetCollision(int Index, bool Object, int Entity, const Vec3& Radii, int Response,
                                      const char* Command, std::string* Error)
    {
        if (!AcceptsSetup(Index, Command, Error))
            return false;
        if (!Loaded_)
            return true;
        Collision& Slot = Collisions_[static_cast<std::size_t>(Index)];
        Slot.Active = true;
        Slot.Object = Object;
        Slot.Entity = Entity;
        Slot.Radii = Radii;
        Slot.Response = Response;
        Slot.Hit = false;
        CaptureStart(Slot);
        return true;
    }

    bool BspWorldSystem::SetCollisionRadius(int Index, bool Object, int Entity, const Vec3& Radii,
                                            const char* Command, std::string* Error)
    {
        if (!AcceptsSetup(Index, Command, Error))
            return false;
        if (!Loaded_)
            return true;
        // DBPro's radius forms keep the slot's response and hit flag.
        Collision& Slot = Collisions_[static_cast<std::size_t>(Index)];
        Slot.Active = true;
        Slot.Object = Object;
        Slot.Entity = Entity;
        Slot.Radii = Radii;
        CaptureStart(Slot);
        return true;
    }

    bool BspWorldSystem::SetCollisionOff(int Index, std::string* Error)
    {
        if (!AcceptsSetup(Index, "SET BSP COLLISION OFF", Error))
            return false;
        if (!Loaded_)
            return true;
        Collision& Slot = Collisions_[static_cast<std::size_t>(Index)];
        if (Slot.Active)
        {
            Slot.Active = false;
            CaptureStart(Slot);
        }
        return true;
    }

    bool BspWorldSystem::SetCollisionThreshold(int Index, float Threshold, std::string* Error)
    {
        if (!AcceptsSetup(Index, "SET BSP COLLISION THRESHHOLD", Error))
            return false;
        if (Loaded_)
            Collisions_[static_cast<std::size_t>(Index)].Threshold = std::max(Threshold, 0.0f);
        return true;
    }

    bool BspWorldSystem::SetCollisionHeightAdjustment(int Index, float Height, std::string* Error)
    {
        if (!AcceptsSetup(Index, "SET BSP COLLISION HEIGHT ADJUSTMENT", Error))
            return false;
        if (!Loaded_)
            return true;
        Collision& Slot = Collisions_[static_cast<std::size_t>(Index)];
        Slot.HeightAdjustment = Height;
        CaptureStart(Slot);
        return true;
    }

    bool BspWorldSystem::ProcessCollision(int Index, std::string* Error)
    {
        if (Index < 1 || Index > KMaxCollisionIndex)
            return Fail(Error, "PROCESS BSP COLLISION collision index must be between 1 and 24");
        Resolve(Collisions_[static_cast<std::size_t>(Index)]);
        return true;
    }

    bool BspWorldSystem::CollisionHit(int& Output, int Index, std::string* Error)
    {
        if (Index < 1 || Index > KMaxCollisionIndex)
            return Fail(Error, "BSP COLLISION HIT collision index must be between 1 and 24");
        Collision& Slot = Collisions_[static_cast<std::size_t>(Index)];
        Output = Slot.Hit ? 1 : 0;
        Slot.Hit = false;
        return true;
    }

    bool BspWorldSystem::CollisionOffset(float& Output, int Index, int Axis, const char* Command,
                                         std::string* Error) const
    {
        if (Index < 1 || Index > KMaxCollisionIndex)
            return Fail(Error, std::string(Command) + " collision index must be between 1 and 24");
        const Vec3& Offset = Collisions_[static_cast<std::size_t>(Index)].Offset;
        Output = Axis == 0 ? Offset.X : (Axis == 1 ? Offset.Y : Offset.Z);
        return true;
    }

    void BspWorldSystem::Resolve(Collision& Slot)
    {
        Vec3 Requested;
        if (!Slot.Active || Triangles_.empty() || !EntityPosition(Slot, Requested))
            return;
        EllipsoidSlideSettings Settings;
        Settings.MinimumMove = Slot.Threshold;
        const EllipsoidSlideResult Result = SlideEllipsoid(Triangles_, Slot.Start, Requested, Slot.Radii, Settings);
        Slot.Offset = Vec3Sub(Result.Position, Requested);
        if (!Result.Clipped)
            return;
        Slot.Hit = true;
        // Response 1 returns to the last free position; 0 keeps the slide.
        if (Slot.Response == 1)
            MoveEntity(Slot, Slot.Start);
        else if (Slot.Response == 0)
            MoveEntity(Slot, Result.Position);
    }

    void BspWorldSystem::ResolveCollisions()
    {
        for (Collision& Slot : Collisions_)
            Resolve(Slot);
    }

    void BspWorldSystem::CaptureCollisionStarts()
    {
        for (Collision& Slot : Collisions_)
        {
            if (Slot.Active)
                CaptureStart(Slot);
        }
    }

    Vec3 BspWorldSystem::CullingPosition() const
    {
        const CameraSystem& Camera = Kernel_.Camera();
        Vec3 Position{};
        if (Camera.CameraExist(CullingCamera_) == 0 || !Camera.PositionX(Position.X, CullingCamera_) ||
            !Camera.PositionY(Position.Y, CullingCamera_) || !Camera.PositionZ(Position.Z, CullingCamera_))
            return Vec3{};
        return Position;
    }
} // namespace dbc
