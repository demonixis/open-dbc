#pragma once

#include "../engine_types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace dbc
{
    // Matrices and terrains are drawn in square blocks of cells. A block outside the view is skipped whole, and
    // a terrain block far from the camera uses a coarser grid, as DBPro's LOD terrain did.
    constexpr int MatrixBlockCells = 16;

    // Vertex normal from the stored normals or the height gradient at grid position (X, Z).
    [[nodiscard]] Vec3 MatrixVertexNormal(const Matrix& MatrixValue, int X, int Z, float StepX, float StepZ);
    // Grid step of a terrain block: one cell near the camera, doubling with distance up to the block size.
    [[nodiscard]] int TerrainBlockStep(float Distance, float BlockSize);

    // World-space terrain vertex for the static GPU path; the vertex shader applies the view.
    struct TerrainVertex
    {
        float Position[3]{};
        float Normal[3]{};
        float Uv[2]{};
    };

    struct TerrainBlock
    {
        int X0 = 0;
        int X1 = 0;
        int Z0 = 0;
        int Z1 = 0;
        Vec3 Minimum{};
        Vec3 Maximum{};
    };

    // The whole grid at full resolution: every LOD indexes a subset of the same vertices.
    struct TerrainMesh
    {
        int Columns = 0;
        int BlocksX = 0;
        int BlocksZ = 0;
        bool Lod = false;
        float BlockSize = 0.0f;
        std::vector<TerrainVertex> Vertices;
        std::vector<TerrainBlock> Blocks;
    };

    void BuildTerrainMesh(const Matrix& Terrain, TerrainMesh& Output);

    enum TerrainEdge : std::size_t
    {
        TerrainEdgeBelow, // lower Z
        TerrainEdgeAbove,
        TerrainEdgeLeft, // lower X
        TerrainEdgeRight,
    };

    // Appends the triangle list of one block at Step. On an edge whose neighbour uses a coarser step, the extra
    // vertices snap onto the neighbour's grid, so the shared edge matches and no crack opens; the resulting
    // degenerate triangles are dropped.
    void AppendTerrainBlockIndices(const TerrainMesh& Mesh, const TerrainBlock& Block, int Step,
                                   const std::array<int, 4>& NeighbourSteps, std::vector<uint32_t>& Indices);
} // namespace dbc
