#pragma once

#include <cstdint>
#include <unordered_map>

#include "../engine_types.hpp"

namespace dbc
{
    class EngineKernel;

    class MatrixManager
    {
      public:
        explicit MatrixManager(EngineKernel& Kernel);
        ~MatrixManager();

        bool MakeMatrix(int MatrixId, float Width, float Depth, int SegX, int SegZ, bool DefaultCull = true,
                        std::string* Error = nullptr);
        bool PrepareMatrixTexture(int MatrixId, int ImageId, int TilesX, int TilesY, bool SnapshotImage = false,
                                  std::string* Error = nullptr);
        bool FillMatrix(int MatrixId, float Height, int TileId, std::string* Error = nullptr);
        bool RandomizeMatrix(int MatrixId, float Range, std::string* Error = nullptr);
        bool SetMatrixHeight(int MatrixId, int X, int Z, float Height, std::string* Error = nullptr);
        bool SetMatrixTile(int MatrixId, int X, int Z, int TileId, std::string* Error = nullptr);
        bool UpdateMatrix(int MatrixId, std::string* Error = nullptr);
        bool GetMatrixHeight(float& Output, int MatrixId, int X, int Z, std::string* Error = nullptr) const;

        bool DeleteMatrix(int MatrixId, std::string* Error = nullptr);
        bool SetMatrixNormal(int MatrixId, int X, int Z, float Nx, float Ny, float Nz, std::string* Error = nullptr);
        bool SetMatrix(int MatrixId, int Wireframe, int Transparency, int Cull, int Filter, int Light, int Fog,
                       int Ambient, std::string* Error = nullptr);
        bool SetMatrixPriority(int MatrixId, int Priority, std::string* Error = nullptr);
        bool SetMatrixTexture(int MatrixId, int TextureMode, int MipMode, std::string* Error = nullptr);
        bool SetMatrixTrim(int MatrixId, float TrimX, float TrimY, std::string* Error = nullptr);
        bool SetMatrixWireframe(int MatrixId, bool Enabled, std::string* Error = nullptr);
        bool GhostMatrixOn(int MatrixId, int Mode = 0, std::string* Error = nullptr);
        bool GhostMatrixOff(int MatrixId, std::string* Error = nullptr);

        bool ShiftMatrixLeft(int MatrixId, std::string* Error = nullptr);
        bool ShiftMatrixRight(int MatrixId, std::string* Error = nullptr);
        bool ShiftMatrixDown(int MatrixId, std::string* Error = nullptr);
        bool ShiftMatrixUp(int MatrixId, std::string* Error = nullptr);

        bool PositionMatrix(int MatrixId, float X, float Y, float Z, std::string* Error = nullptr);
        bool GetGroundHeight(float& Output, int MatrixId, float X, float Z, std::string* Error = nullptr) const;
        bool MatrixExist(int& Output, int MatrixId, std::string* Error = nullptr) const;
        bool MatrixPositionX(float& Output, int MatrixId, std::string* Error = nullptr) const;
        bool MatrixPositionY(float& Output, int MatrixId, std::string* Error = nullptr) const;
        bool MatrixPositionZ(float& Output, int MatrixId, std::string* Error = nullptr) const;
        bool MatrixTileCount(int& Output, int MatrixId, std::string* Error = nullptr) const;
        bool MatrixTilesExist(int& Output, int MatrixId, std::string* Error = nullptr) const;
        bool MatrixWireframeState(int& Output, int MatrixId, std::string* Error = nullptr) const;

        [[nodiscard]] const Matrix* GetMatrix(int MatrixId) const;

        // Renderer iteration.
        template <typename Fn> void ForEachMatrix(Fn&& FnValue) const
        {
            for (const auto& [Id, MatrixValue] : Matrices_)
                FnValue(Id, MatrixValue);
        }

        bool MakeTerrain(int TerrainId, const std::string& Path, std::string* Error = nullptr);
        bool MakeAdvancedTerrain(int TerrainId, std::string* Error = nullptr);
        bool DeleteTerrain(int TerrainId, std::string* Error = nullptr);
        bool SetTerrainHeightmap(int TerrainId, const std::string& Path, std::string* Error = nullptr);
        bool SetTerrainScale(int TerrainId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetTerrainTexture(int TerrainId, int ImageId, std::string* Error = nullptr);
        bool SetTerrainTiling(int TerrainId, int TilesX, int TilesY, std::string* Error = nullptr);
        bool SetTerrainSplit(int TerrainId, int Split, std::string* Error = nullptr);
        bool BuildTerrain(int TerrainId, std::string* Error = nullptr);
        bool PositionTerrain(int TerrainId, float X, float Y, float Z, std::string* Error = nullptr);
        bool GetTerrainHeight(float& Output, int TerrainId, float X, float Z, std::string* Error = nullptr) const;
        bool GetTerrainSize(float& Output, int TerrainId, std::string* Error = nullptr) const;
        bool GetTotalTerrainHeight(float& Output, int TerrainId, std::string* Error = nullptr) const;
        bool TerrainExist(int& Output, int TerrainId, std::string* Error = nullptr) const;
        bool TerrainPositionX(float& Output, int TerrainId, std::string* Error = nullptr) const;
        bool TerrainPositionY(float& Output, int TerrainId, std::string* Error = nullptr) const;
        bool TerrainPositionZ(float& Output, int TerrainId, std::string* Error = nullptr) const;

        [[nodiscard]] const Matrix* GetTerrain(int TerrainId) const;

        template <typename Fn> void ForEachTerrain(Fn&& FnValue) const
        {
            for (const auto& [Id, Terrain] : Terrains_)
                FnValue(Id, Terrain);
        }

        // Pure helpers on a single Matrix (no EngineKernel needed).
        static float MatrixHeight(const Matrix& MatrixValue, int Vx, int Vz);
        static int MatrixTile(const Matrix& MatrixValue, int Tx, int Tz);

      private:
        Matrix* RequireMatrix(int MatrixId, const char* Command, std::string* Error = nullptr);
        const Matrix* RequireMatrix(int MatrixId, const char* Command, std::string* Error = nullptr) const;
        Matrix* RequireTerrain(int TerrainId, const char* Command, std::string* Error = nullptr);
        const Matrix* RequireTerrain(int TerrainId, const char* Command, std::string* Error = nullptr) const;
        bool LoadTerrainHeightmap(Matrix& Terrain, const std::string& Path, const char* Command,
                                  std::string* Error = nullptr);
        void InvalidateRenderer();

        EngineKernel& Kernel_;
        std::unordered_map<int, Matrix> Matrices_;
        std::unordered_map<int, Matrix> Terrains_;
        uint64_t TerrainRevision_ = 0;
    };

} // namespace dbc
