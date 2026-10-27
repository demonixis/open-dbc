#include "engine/systems/terrain_mesh.hpp"

#include <doctest/doctest.h>

#include <set>

namespace
{
    // A 32 x 32-cell terrain, 2 x 2 blocks, with a height ramp along X.
    dbc::Matrix RampTerrain()
    {
        dbc::Matrix Terrain;
        Terrain.IsTerrain = true;
        Terrain.SegX = 32;
        Terrain.SegZ = 32;
        Terrain.Width = 64.0f;
        Terrain.Depth = 32.0f;
        Terrain.HeightScale = 10.0f;
        Terrain.PosX = 5.0f;
        Terrain.PosY = 1.0f;
        Terrain.PosZ = 40.0f;
        Terrain.TilesX = 2;
        Terrain.TilesY = 1;
        Terrain.Heights.resize(33 * 33);
        for (int Z = 0; Z <= 32; ++Z)
            for (int X = 0; X <= 32; ++X)
                Terrain.Heights[static_cast<std::size_t>(Z) * 33 + X] = static_cast<float>(X) / 32.0f;
        return Terrain;
    }

    std::set<uint32_t> RowVertices(const std::vector<uint32_t>& Indices, int Row, int Columns)
    {
        std::set<uint32_t> Result;
        for (const uint32_t Index : Indices)
            if (static_cast<int>(Index) / Columns == Row)
                Result.insert(Index);
        return Result;
    }
} // namespace

TEST_CASE("Static terrain meshes keep the CPU path's world positions, UVs and block bounds")
{
    dbc::TerrainMesh Mesh;
    dbc::BuildTerrainMesh(RampTerrain(), Mesh);
    REQUIRE(Mesh.Vertices.size() == 33 * 33);
    CHECK(Mesh.Columns == 33);
    CHECK(Mesh.Lod);
    CHECK(Mesh.BlocksX == 2);
    CHECK(Mesh.BlocksZ == 2);
    CHECK(Mesh.BlockSize == doctest::Approx(32.0f));

    const dbc::TerrainVertex& Vertex = Mesh.Vertices[4 * 33 + 8];
    CHECK(Vertex.Position[0] == doctest::Approx(5.0f + 8 * 2.0f));
    CHECK(Vertex.Position[1] == doctest::Approx(1.0f + 8.0f / 32.0f * 10.0f));
    CHECK(Vertex.Position[2] == doctest::Approx(40.0f - 4.0f));
    CHECK(Vertex.Uv[0] == doctest::Approx(8.0f / 32.0f * 2.0f));
    CHECK(Vertex.Uv[1] == doctest::Approx(4.0f / 32.0f));
    // The ramp rises along +X, so the normal leans towards -X.
    CHECK(Vertex.Normal[0] < 0.0f);
    CHECK(Vertex.Normal[1] > 0.9f);

    const dbc::TerrainBlock& Block = Mesh.Blocks[1];
    CHECK(Block.X0 == 16);
    CHECK(Block.X1 == 32);
    CHECK(Block.Z0 == 0);
    CHECK(Block.Z1 == 16);
    CHECK(Block.Minimum.X == doctest::Approx(37.0f));
    CHECK(Block.Maximum.X == doctest::Approx(69.0f));
    CHECK(Block.Minimum.Y == doctest::Approx(6.0f));
    CHECK(Block.Maximum.Y == doctest::Approx(11.0f));
    CHECK(Block.Minimum.Z == doctest::Approx(24.0f));
    CHECK(Block.Maximum.Z == doctest::Approx(40.0f));
}

TEST_CASE("Terrain block indices coarsen with the step and stitch to coarser neighbours without cracks")
{
    dbc::TerrainMesh Mesh;
    dbc::BuildTerrainMesh(RampTerrain(), Mesh);
    const dbc::TerrainBlock& Lower = Mesh.Blocks[0];
    const dbc::TerrainBlock& Upper = Mesh.Blocks[2];

    std::vector<uint32_t> Indices;
    dbc::AppendTerrainBlockIndices(Mesh, Lower, 1, {1, 1, 1, 1}, Indices);
    CHECK(Indices.size() == 16 * 16 * 2 * 3);
    Indices.clear();
    dbc::AppendTerrainBlockIndices(Mesh, Lower, 4, {4, 4, 4, 4}, Indices);
    CHECK(Indices.size() == 4 * 4 * 2 * 3);

    // The fine lower block meets a step-4 block above it: its top row may only use the coarse vertices.
    std::vector<uint32_t> Fine;
    dbc::AppendTerrainBlockIndices(Mesh, Lower, 1, {1, 4, 1, 1}, Fine);
    std::vector<uint32_t> Coarse;
    dbc::AppendTerrainBlockIndices(Mesh, Upper, 4, {1, 4, 4, 4}, Coarse);
    CHECK(Fine.size() < 16 * 16 * 2 * 3);
    CHECK(Fine.size() % 3 == 0);
    const std::set<uint32_t> FineEdge = RowVertices(Fine, 16, Mesh.Columns);
    const std::set<uint32_t> CoarseEdge = RowVertices(Coarse, 16, Mesh.Columns);
    CHECK(FineEdge.size() == 5);
    CHECK(FineEdge == CoarseEdge);
    for (std::size_t Index = 0; Index < Fine.size(); Index += 3)
    {
        CHECK(Fine[Index] != Fine[Index + 1]);
        CHECK(Fine[Index + 1] != Fine[Index + 2]);
        CHECK(Fine[Index] != Fine[Index + 2]);
    }
}
