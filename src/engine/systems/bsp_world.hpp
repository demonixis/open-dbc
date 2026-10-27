#pragma once

#include "../engine_types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dbc
{
    class EngineKernel;

    // DarkBASIC Professional's own BSP format (DBPro `Shared/CustomBSP`), little endian and
    // unpadded: node, plane, leaf and polygon arrays, run-length PVS bytes, then 21-byte texture
    // names. A node's front child is a leaf index when IsLeaf is set; Back is -1 for solid space.
    struct BspPlane
    {
        Vec3 Point{};
        Vec3 Normal{};
    };

    struct BspNode
    {
        bool IsLeaf = false;
        uint32_t Plane = 0;
        uint32_t Front = 0;
        int32_t Back = -1;
    };

    struct BspLeaf
    {
        int32_t FirstPolygon = 0;
        int32_t EndPolygon = 0;
        int32_t PvsOffset = 0;
        Vec3 BoxMin{};
        Vec3 BoxMax{};
    };

    struct BspVertex
    {
        Vec3 Position{};
        uint32_t Color = 0xFFFFFFFFu; // D3DCOLOR, ARGB
        float U = 0.0f;
        float V = 0.0f;
    };

    struct BspPolygon
    {
        std::vector<BspVertex> Vertices;
        std::vector<uint16_t> Indices; // triangle list
        Vec3 Normal{};
        uint16_t Texture = 0;
    };

    struct BspMap
    {
        std::vector<BspNode> Nodes;
        std::vector<BspPlane> Planes;
        std::vector<BspLeaf> Leaves;
        std::vector<BspPolygon> Polygons;
        std::vector<uint8_t> Pvs;
        std::vector<std::string> TextureNames;
    };

    enum class BspFileFormat
    {
        Unknown,
        Quake1,
        HalfLife,
        Quake2,
        Quake3,
    };

    // Parses and validates every index, so later traversal never leaves the arrays.
    bool ParseDbProBsp(std::span<const uint8_t> Bytes, BspMap& Output, std::string* Error = nullptr);
    // Recognises the Quake-family headers DBPro also accepted, to name them in errors.
    [[nodiscard]] BspFileFormat DetectQuakeBspFormat(std::span<const uint8_t> Bytes);
    [[nodiscard]] const char* BspFileFormatName(BspFileFormat Format);
    // Returns the leaf holding Position, or -1 in solid space (DBPro classifies with a 0.001 epsilon
    // and treats points on a plane as in front).
    [[nodiscard]] int FindBspLeaf(const BspMap& Map, const Vec3& Position);
    // Marks the leaves visible from Leaf in Visible (resized to the leaf count).
    void DecodeBspPvs(const BspMap& Map, int Leaf, std::vector<uint8_t>& Visible);

    class BspWorldSystem
    {
      public:
        explicit BspWorldSystem(EngineKernel& Kernel);
        ~BspWorldSystem();

        // MapPaths are tried in order (the name as given, then inside the pk3's folder), as DBPro did.
        bool Load(const std::string& Pk3Path, const std::vector<std::string>& MapPaths, std::string* Error = nullptr);
        void Delete();
        [[nodiscard]] bool HasWorld() const
        {
            return Loaded_;
        }
        [[nodiscard]] const BspMap& Map() const
        {
            return Map_;
        }
        // Internal image id of a texture slot, or 0 when its file could not be loaded.
        [[nodiscard]] int TextureImage(int TextureIndex) const;

        void SetCullingCamera(int CameraId);
        [[nodiscard]] int CullingCamera() const
        {
            return CullingCamera_;
        }
        // Position whose leaf selects the visible set: the BSP camera, or the origin when that camera
        // does not exist (DBPro's camera readers return 0 for a missing camera).
        [[nodiscard]] Vec3 CullingPosition() const;

      private:
        void LoadTextures(const std::string& MapDirectory);

        EngineKernel& Kernel_;
        BspMap Map_;
        std::vector<int> TextureImages_;
        bool Loaded_ = false;
        int CullingCamera_ = 0;
    };
} // namespace dbc
