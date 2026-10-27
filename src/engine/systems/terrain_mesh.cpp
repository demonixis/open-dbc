#include "terrain_mesh.hpp"
#include "matrix_manager.hpp"

#include <algorithm>
#include <cmath>

namespace dbc
{
    Vec3 MatrixVertexNormal(const Matrix& MatrixValue, int X, int Z, float StepX, float StepZ)
    {
        const size_t VertexCount = static_cast<size_t>(MatrixValue.SegX + 1) * (MatrixValue.SegZ + 1);
        if (MatrixValue.Normals.size() == VertexCount * 3)
        {
            const size_t Index = (static_cast<size_t>(Z) * (MatrixValue.SegX + 1) + X) * 3;
            Vec3 Normal{MatrixValue.Normals[Index], MatrixValue.Normals[Index + 1], MatrixValue.Normals[Index + 2]};
            const float Length = std::sqrt(Normal.X * Normal.X + Normal.Y * Normal.Y + Normal.Z * Normal.Z);
            if (Length > 0.0001f)
            {
                Normal.X /= Length;
                Normal.Y /= Length;
                Normal.Z /= Length;
                return Normal;
            }
            return {0.0f, 1.0f, 0.0f};
        }

        const int Xm = std::max(X - 1, 0);
        const int Xp = std::min(X + 1, MatrixValue.SegX);
        const int Zm = std::max(Z - 1, 0);
        const int Zp = std::min(Z + 1, MatrixValue.SegZ);

        const float HeightScale = MatrixValue.IsTerrain ? MatrixValue.HeightScale : 1.0f;
        const float HL = MatrixManager::MatrixHeight(MatrixValue, Xm, Z) * HeightScale;
        const float HR = MatrixManager::MatrixHeight(MatrixValue, Xp, Z) * HeightScale;
        const float HD = MatrixManager::MatrixHeight(MatrixValue, X, Zm) * HeightScale;
        const float HU = MatrixManager::MatrixHeight(MatrixValue, X, Zp) * HeightScale;

        const float Dx = (Xp - Xm) * StepX;
        const float Dz = (Zp - Zm) * StepZ;

        // Normal = cross product of tangent vectors
        Vec3 N{-(HR - HL) * Dz, Dx * Dz, -(HU - HD) * Dx};
        const float Len = std::sqrt(N.X * N.X + N.Y * N.Y + N.Z * N.Z);
        if (Len > 0.0001f)
        {
            N.X /= Len;
            N.Y /= Len;
            N.Z /= Len;
        }
        else
        {
            N = {0.0f, 1.0f, 0.0f};
        }
        if (MatrixValue.IsTerrain)
            N.Z = -N.Z;
        return N;
    }

    int TerrainBlockStep(float Distance, float BlockSize)
    {
        int Step = 1;
        while (Step < MatrixBlockCells && Distance > BlockSize * 4.0f * static_cast<float>(Step))
            Step *= 2;
        return Step;
    }

    void BuildTerrainMesh(const Matrix& Terrain, TerrainMesh& Output)
    {
        Output = TerrainMesh{};
        if (Terrain.SegX <= 0 || Terrain.SegZ <= 0)
            return;
        const int SegX = Terrain.SegX;
        const int SegZ = Terrain.SegZ;
        const float StepX = Terrain.Width / static_cast<float>(SegX);
        const float StepZ = Terrain.Depth / static_cast<float>(SegZ);
        Output.Columns = SegX + 1;
        Output.Lod = SegX % MatrixBlockCells == 0 && SegZ % MatrixBlockCells == 0;
        Output.BlockSize = static_cast<float>(MatrixBlockCells) * std::max(StepX, StepZ);

        // Terrains grow towards -Z from their position, as the CPU path draws them.
        Output.Vertices.resize(static_cast<std::size_t>(SegX + 1) * static_cast<std::size_t>(SegZ + 1));
        for (int Z = 0; Z <= SegZ; ++Z)
        {
            for (int X = 0; X <= SegX; ++X)
            {
                TerrainVertex& Vertex =
                    Output.Vertices[static_cast<std::size_t>(Z) * static_cast<std::size_t>(SegX + 1) + X];
                Vertex.Position[0] = Terrain.PosX + static_cast<float>(X) * StepX;
                Vertex.Position[1] = Terrain.PosY + MatrixManager::MatrixHeight(Terrain, X, Z) * Terrain.HeightScale;
                Vertex.Position[2] = Terrain.PosZ - static_cast<float>(Z) * StepZ;
                if (Terrain.Light)
                {
                    const Vec3 Normal = MatrixVertexNormal(Terrain, X, Z, StepX, StepZ);
                    Vertex.Normal[0] = Normal.X;
                    Vertex.Normal[1] = Normal.Y;
                    Vertex.Normal[2] = Normal.Z;
                }
                Vertex.Uv[0] = static_cast<float>(X) / static_cast<float>(SegX) * static_cast<float>(Terrain.TilesX);
                Vertex.Uv[1] = static_cast<float>(Z) / static_cast<float>(SegZ) * static_cast<float>(Terrain.TilesY);
            }
        }

        Output.BlocksX = (SegX + MatrixBlockCells - 1) / MatrixBlockCells;
        Output.BlocksZ = (SegZ + MatrixBlockCells - 1) / MatrixBlockCells;
        Output.Blocks.resize(static_cast<std::size_t>(Output.BlocksX) * static_cast<std::size_t>(Output.BlocksZ));
        for (int Bz = 0; Bz < Output.BlocksZ; ++Bz)
        {
            for (int Bx = 0; Bx < Output.BlocksX; ++Bx)
            {
                TerrainBlock& Block = Output.Blocks[static_cast<std::size_t>(Bz) * Output.BlocksX + Bx];
                Block.X0 = Bx * MatrixBlockCells;
                Block.X1 = std::min(Block.X0 + MatrixBlockCells, SegX);
                Block.Z0 = Bz * MatrixBlockCells;
                Block.Z1 = std::min(Block.Z0 + MatrixBlockCells, SegZ);
                bool First = true;
                for (int Z = Block.Z0; Z <= Block.Z1; ++Z)
                {
                    for (int X = Block.X0; X <= Block.X1; ++X)
                    {
                        const float* P =
                            Output.Vertices[static_cast<std::size_t>(Z) * static_cast<std::size_t>(SegX + 1) + X]
                                .Position;
                        if (First)
                        {
                            Block.Minimum = Block.Maximum = Vec3{P[0], P[1], P[2]};
                            First = false;
                            continue;
                        }
                        Block.Minimum = {std::min(Block.Minimum.X, P[0]), std::min(Block.Minimum.Y, P[1]),
                                         std::min(Block.Minimum.Z, P[2])};
                        Block.Maximum = {std::max(Block.Maximum.X, P[0]), std::max(Block.Maximum.Y, P[1]),
                                         std::max(Block.Maximum.Z, P[2])};
                    }
                }
            }
        }
    }

    void AppendTerrainBlockIndices(const TerrainMesh& Mesh, const TerrainBlock& Block, int Step,
                                   const std::array<int, 4>& NeighbourSteps, std::vector<uint32_t>& Indices)
    {
        // Grid coordinates are cell indices; a boundary vertex snaps down onto the coarser neighbour's grid.
        const auto Vertex = [&](int X, int Z)
        {
            if (Z == Block.Z0 && NeighbourSteps[TerrainEdgeBelow] > Step)
                X -= (X - Block.X0) % NeighbourSteps[TerrainEdgeBelow];
            else if (Z == Block.Z1 && NeighbourSteps[TerrainEdgeAbove] > Step)
                X -= (X - Block.X0) % NeighbourSteps[TerrainEdgeAbove];
            if (X == Block.X0 && NeighbourSteps[TerrainEdgeLeft] > Step)
                Z -= (Z - Block.Z0) % NeighbourSteps[TerrainEdgeLeft];
            else if (X == Block.X1 && NeighbourSteps[TerrainEdgeRight] > Step)
                Z -= (Z - Block.Z0) % NeighbourSteps[TerrainEdgeRight];
            return static_cast<uint32_t>(static_cast<std::size_t>(Z) * static_cast<std::size_t>(Mesh.Columns) + X);
        };
        const auto Triangle = [&](uint32_t A, uint32_t B, uint32_t C)
        {
            if (A == B || B == C || A == C)
                return;
            Indices.push_back(A);
            Indices.push_back(B);
            Indices.push_back(C);
        };
        for (int Z = Block.Z0; Z < Block.Z1; Z += Step)
        {
            for (int X = Block.X0; X < Block.X1; X += Step)
            {
                const int Xn = std::min(X + Step, Block.X1);
                const int Zn = std::min(Z + Step, Block.Z1);
                const uint32_t P00 = Vertex(X, Z);
                const uint32_t P10 = Vertex(Xn, Z);
                const uint32_t P11 = Vertex(Xn, Zn);
                const uint32_t P01 = Vertex(X, Zn);
                // Same winding as the CPU terrain triangles.
                Triangle(P00, P11, P10);
                Triangle(P00, P01, P11);
            }
        }
    }
} // namespace dbc
