#include "engine/systems/bsp_world.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>

namespace
{
    std::vector<uint8_t> ReadTutorialMap()
    {
        std::ifstream Stream(std::filesystem::path(TEST_SOURCE_DIR) / "examples/dbpro/tutorials/world/ikzdm1.BSP",
                             std::ios::binary);
        REQUIRE(Stream);
        return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
    }

    template <typename T> void Append(std::vector<uint8_t>& Bytes, T Value)
    {
        const auto* Raw = reinterpret_cast<const uint8_t*>(&Value);
        Bytes.insert(Bytes.end(), Raw, Raw + sizeof(T));
    }

    // One leaf behind one plane, one triangle, no PVS and one texture slot.
    std::vector<uint8_t> MinimalMap(uint16_t ThirdIndex)
    {
        std::vector<uint8_t> Bytes;
        Append<int32_t>(Bytes, 1);
        Append<uint8_t>(Bytes, 1);
        Append<uint32_t>(Bytes, 0);
        Append<uint32_t>(Bytes, 0);
        Append<int32_t>(Bytes, -1);
        Append<int32_t>(Bytes, 1);
        for (float Value : {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})
            Append(Bytes, Value);
        Append<int32_t>(Bytes, 1);
        Append<int32_t>(Bytes, 0);
        Append<int32_t>(Bytes, 1);
        Append<int32_t>(Bytes, 0);
        for (float Value : {-1.0f, 0.0f, -1.0f, 1.0f, 1.0f, 1.0f})
            Append(Bytes, Value);
        Append<int32_t>(Bytes, 1);
        Append<uint16_t>(Bytes, 3);
        for (int Vertex = 0; Vertex < 3; ++Vertex)
        {
            for (float Value : {static_cast<float>(Vertex), 0.0f, static_cast<float>(Vertex == 2)})
                Append(Bytes, Value);
            Append<uint32_t>(Bytes, 0xFFFFFFFFu);
            Append(Bytes, 0.0f);
            Append(Bytes, 0.0f);
        }
        Append<uint16_t>(Bytes, 3);
        Append<uint16_t>(Bytes, 0);
        Append<uint16_t>(Bytes, 1);
        Append<uint16_t>(Bytes, ThirdIndex);
        for (float Value : {0.0f, 1.0f, 0.0f})
            Append(Bytes, Value);
        Append<uint16_t>(Bytes, 0);
        Append<int32_t>(Bytes, 0);
        Append<uint16_t>(Bytes, 1);
        char Name[21]{};
        std::strcpy(Name, "floor.bmp");
        Bytes.insert(Bytes.end(), Name, Name + sizeof(Name));
        return Bytes;
    }
} // namespace

TEST_CASE("The DBPro world tutorial map parses as DBPro's own BSP format")
{
    const std::vector<uint8_t> Bytes = ReadTutorialMap();
    dbc::BspMap Map;
    std::string Error;
    REQUIRE_MESSAGE(dbc::ParseDbProBsp(Bytes, Map, &Error), Error);
    CHECK(Map.Nodes.size() == 228);
    CHECK(Map.Planes.size() == 228);
    CHECK(Map.Leaves.size() == 20);
    CHECK(Map.Polygons.size() == 232);
    CHECK(Map.Pvs.size() == 100);
    REQUIRE(Map.TextureNames.size() == 20);
    CHECK(Map.TextureNames.front() == "CYGRASS1.bmp");
    CHECK(Map.TextureNames.back() == "CYWALLT.bmp");
    const std::size_t Triangles = std::accumulate(Map.Polygons.begin(), Map.Polygons.end(), std::size_t{0},
                                                  [](std::size_t Sum, const dbc::BspPolygon& Polygon)
                                                  { return Sum + Polygon.Indices.size() / 3; });
    CHECK(Triangles == 296);
    CHECK(dbc::DetectQuakeBspFormat(Bytes) == dbc::BspFileFormat::Unknown);
}

TEST_CASE("BSP leaves come from the plane tree and visibility from the run-length PVS")
{
    dbc::BspMap Map;
    REQUIRE(dbc::ParseDbProBsp(ReadTutorialMap(), Map));
    // The tutorial camera starts above the floor at y = -32; below it is solid space.
    CHECK(dbc::FindBspLeaf(Map, dbc::Vec3{2.0f, 3.0f, 2.0f}) == 0);
    CHECK(dbc::FindBspLeaf(Map, dbc::Vec3{2.0f, -31.5f, 2.0f}) == 0);
    CHECK(dbc::FindBspLeaf(Map, dbc::Vec3{2.0f, -40.0f, 2.0f}) == -1);

    std::vector<uint8_t> Visible;
    dbc::DecodeBspPvs(Map, 0, Visible);
    REQUIRE(Visible.size() == 20);
    CHECK(std::accumulate(Visible.begin(), Visible.end(), 0) == 19);
    CHECK(Visible[0] == 1);
    dbc::DecodeBspPvs(Map, -1, Visible);
    CHECK(std::accumulate(Visible.begin(), Visible.end(), 0) == 0);
}

TEST_CASE("BSP parsing rejects truncated, inconsistent and Quake-family files")
{
    dbc::BspMap Map;
    std::string Error;
    REQUIRE(dbc::ParseDbProBsp(MinimalMap(2), Map, &Error));
    CHECK(Map.TextureNames == std::vector<std::string>{"floor.bmp"});
    std::vector<uint8_t> Visible;
    dbc::DecodeBspPvs(Map, 0, Visible);
    CHECK(Visible == std::vector<uint8_t>{1});

    CHECK_FALSE(dbc::ParseDbProBsp(MinimalMap(3), Map, &Error));
    CHECK(Error.find("out of range") != std::string::npos);

    std::vector<uint8_t> Truncated = MinimalMap(2);
    Truncated.resize(Truncated.size() - 5);
    CHECK_FALSE(dbc::ParseDbProBsp(Truncated, Map, &Error));
    CHECK(Error.find("truncated") != std::string::npos);

    std::vector<uint8_t> Huge;
    Append<int32_t>(Huge, 0x7FFFFFFF);
    CHECK_FALSE(dbc::ParseDbProBsp(Huge, Map, &Error));
    CHECK(Error.find("invalid node count") != std::string::npos);

    std::vector<uint8_t> Quake3{'I', 'B', 'S', 'P'};
    Append<int32_t>(Quake3, 46);
    Quake3.resize(64, 0);
    CHECK_FALSE(dbc::ParseDbProBsp(Quake3, Map, &Error));
    CHECK(dbc::DetectQuakeBspFormat(Quake3) == dbc::BspFileFormat::Quake3);
    std::vector<uint8_t> Quake1;
    Append<int32_t>(Quake1, 29);
    Append<int32_t>(Quake1, 0);
    CHECK(dbc::DetectQuakeBspFormat(Quake1) == dbc::BspFileFormat::Quake1);
}
