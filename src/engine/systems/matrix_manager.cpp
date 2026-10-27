#include "matrix_manager.hpp"
#include "../engine_types.hpp"
#include "camera_system.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "platform_io.hpp"
#include "renderer_system.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include <string>

namespace dbc
{
    namespace
    {
        int WrapIndex(int Value, int Count)
        {
            const int Result = Value % Count;
            return Result < 0 ? Result + Count : Result;
        }

        void ShiftMatrixStorage(Matrix& MatrixValue, int SourceOffsetX, int SourceOffsetZ)
        {
            const std::vector<float> Heights = MatrixValue.Heights;
            const int VertexColumns = MatrixValue.SegX + 1;
            const int VertexRows = MatrixValue.SegZ + 1;
            for (int Z = 0; Z < VertexRows; ++Z)
            {
                for (int X = 0; X < VertexColumns; ++X)
                {
                    const int SourceX = WrapIndex(X + SourceOffsetX, VertexColumns);
                    const int SourceZ = WrapIndex(Z + SourceOffsetZ, VertexRows);
                    MatrixValue.Heights[static_cast<size_t>(Z) * VertexColumns + X] =
                        Heights[static_cast<size_t>(SourceZ) * VertexColumns + SourceX];
                }
            }

            const std::vector<int> Tiles = MatrixValue.Tiles;
            for (int Z = 0; Z < MatrixValue.SegZ; ++Z)
            {
                for (int X = 0; X < MatrixValue.SegX; ++X)
                {
                    const int SourceX = WrapIndex(X + SourceOffsetX, MatrixValue.SegX);
                    const int SourceZ = WrapIndex(Z + SourceOffsetZ, MatrixValue.SegZ);
                    MatrixValue.Tiles[static_cast<size_t>(Z) * MatrixValue.SegX + X] =
                        Tiles[static_cast<size_t>(SourceZ) * MatrixValue.SegX + SourceX];
                }
            }

            if (!MatrixValue.Normals.empty())
            {
                const std::vector<float> Normals = MatrixValue.Normals;
                for (int Z = 0; Z < VertexRows; ++Z)
                {
                    for (int X = 0; X < VertexColumns; ++X)
                    {
                        const int SourceX = WrapIndex(X + SourceOffsetX, VertexColumns);
                        const int SourceZ = WrapIndex(Z + SourceOffsetZ, VertexRows);
                        const size_t Destination = (static_cast<size_t>(Z) * VertexColumns + X) * 3;
                        const size_t Source = (static_cast<size_t>(SourceZ) * VertexColumns + SourceX) * 3;
                        MatrixValue.Normals[Destination] = Normals[Source];
                        MatrixValue.Normals[Destination + 1] = Normals[Source + 1];
                        MatrixValue.Normals[Destination + 2] = Normals[Source + 2];
                    }
                }
            }
        }
    } // namespace

    MatrixManager::MatrixManager(EngineKernel& Kernel) : Kernel_(Kernel) {}

    MatrixManager::~MatrixManager()
    {
        for (auto& [Id, MatrixValue] : Matrices_)
        {
            (void)Id;
            if (MatrixValue.OwnsImage)
                Kernel_.ImageStoreRef().ReleaseImageInternal(MatrixValue.ImageId);
        }
        for (auto& [Id, TerrainValue] : Terrains_)
        {
            (void)Id;
            if (TerrainValue.OwnsImage)
                Kernel_.ImageStoreRef().ReleaseImageInternal(TerrainValue.ImageId);
        }
    }

    const Matrix* MatrixManager::GetMatrix(int MatrixId) const
    {
        auto It = Matrices_.find(MatrixId);
        return (It != Matrices_.end()) ? &It->second : nullptr;
    }

    const Matrix* MatrixManager::GetTerrain(int TerrainId) const
    {
        const auto It = Terrains_.find(TerrainId);
        return It != Terrains_.end() ? &It->second : nullptr;
    }

    Matrix* MatrixManager::RequireMatrix(int MatrixId, const char* Command, std::string* Error)
    {
        if (MatrixId < 1 || MatrixId > 65535)
        {
            if (Error)
                *Error = std::string(Command) + " matrix number must be between 1 and 65535";
            return nullptr;
        }
        const auto It = Matrices_.find(MatrixId);
        if (It == Matrices_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing matrix " + std::to_string(MatrixId);
            return nullptr;
        }
        return &It->second;
    }

    const Matrix* MatrixManager::RequireMatrix(int MatrixId, const char* Command, std::string* Error) const
    {
        if (MatrixId < 1 || MatrixId > 65535)
        {
            if (Error)
                *Error = std::string(Command) + " matrix number must be between 1 and 65535";
            return nullptr;
        }
        const auto It = Matrices_.find(MatrixId);
        if (It == Matrices_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing matrix " + std::to_string(MatrixId);
            return nullptr;
        }
        return &It->second;
    }

    Matrix* MatrixManager::RequireTerrain(int TerrainId, const char* Command, std::string* Error)
    {
        if (TerrainId < 1 || TerrainId > 65535)
        {
            if (Error)
                *Error = std::string(Command) + " terrain number must be between 1 and 65535";
            return nullptr;
        }
        const auto It = Terrains_.find(TerrainId);
        if (It == Terrains_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing terrain " + std::to_string(TerrainId);
            return nullptr;
        }
        return &It->second;
    }

    const Matrix* MatrixManager::RequireTerrain(int TerrainId, const char* Command, std::string* Error) const
    {
        if (TerrainId < 1 || TerrainId > 65535)
        {
            if (Error)
                *Error = std::string(Command) + " terrain number must be between 1 and 65535";
            return nullptr;
        }
        const auto It = Terrains_.find(TerrainId);
        if (It == Terrains_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing terrain " + std::to_string(TerrainId);
            return nullptr;
        }
        return &It->second;
    }

    void MatrixManager::InvalidateRenderer()
    {
        Kernel_.Renderer().InvalidateTriCache();
    }

    bool MatrixManager::MakeMatrix(int MatrixId, float Width, float Depth, int SegX, int SegZ, bool DefaultCull,
                                   std::string* Error)
    {
        if (MatrixId < 1 || MatrixId > 65535)
        {
            if (Error)
                *Error = "MAKE MATRIX number must be between 1 and 65535";
            return false;
        }
        if (Matrices_.contains(MatrixId))
        {
            if (Error)
                *Error = "MAKE MATRIX cannot replace matrix " + std::to_string(MatrixId);
            return false;
        }
        if (!std::isfinite(Width) || !std::isfinite(Depth) || Width <= 0.0f || Depth <= 0.0f)
        {
            if (Error)
                *Error = "MAKE MATRIX dimensions must be positive finite values";
            return false;
        }
        if (SegX <= 0 || SegZ <= 0 || SegX == std::numeric_limits<int>::max() ||
            SegZ == std::numeric_limits<int>::max())
        {
            if (Error)
                *Error = "MAKE MATRIX segment counts must be positive";
            return false;
        }
        const size_t VertexColumns = static_cast<size_t>(SegX) + 1;
        const size_t VertexRows = static_cast<size_t>(SegZ) + 1;
        if (VertexColumns > std::numeric_limits<size_t>::max() / VertexRows ||
            static_cast<size_t>(SegX) > std::numeric_limits<size_t>::max() / static_cast<size_t>(SegZ))
        {
            if (Error)
                *Error = "MAKE MATRIX dimensions are too large";
            return false;
        }

        Matrix MatrixValue;
        MatrixValue.Width = Width;
        MatrixValue.Depth = Depth;
        MatrixValue.SegX = SegX;
        MatrixValue.SegZ = SegZ;
        MatrixValue.Cull = DefaultCull;

        MatrixValue.Heights.assign(VertexColumns * VertexRows, 0.0f);
        MatrixValue.Tiles.assign(static_cast<size_t>(MatrixValue.SegX) * MatrixValue.SegZ, 1);

        Matrices_.emplace(MatrixId, std::move(MatrixValue));
        InvalidateRenderer();
        Kernel_.Camera().AutoPosition(Width * 0.5f, 0.0f, Depth * 0.5f, (Width + Depth) / 10.0f);
        return true;
    }

    bool MatrixManager::PrepareMatrixTexture(int MatrixId, int ImageId, int TilesX, int TilesY, bool SnapshotImage,
                                             std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "PREPARE MATRIX TEXTURE", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "PREPARE MATRIX TEXTURE requires existing image " + std::to_string(ImageId);
            return false;
        }
        if (TilesX <= 0 || TilesY <= 0)
        {
            if (Error)
                *Error = "PREPARE MATRIX TEXTURE tile divisions must be positive";
            return false;
        }
        if (static_cast<size_t>(TilesX) >
            static_cast<size_t>(std::numeric_limits<int>::max()) / static_cast<size_t>(TilesY))
        {
            if (Error)
                *Error = "PREPARE MATRIX TEXTURE tile count is too large";
            return false;
        }

        int PreparedImageId = ImageId;
        if (SnapshotImage)
        {
            if (!Kernel_.ImageStoreRef().CloneImageInternal(PreparedImageId, ImageId, Error))
                return false;
            if (PreparedImageId == 0)
            {
                if (Error)
                    *Error = "PREPARE MATRIX TEXTURE could not snapshot image " + std::to_string(ImageId);
                return false;
            }
        }
        if (MatrixValue.OwnsImage)
            Kernel_.ImageStoreRef().ReleaseImageInternal(MatrixValue.ImageId);
        MatrixValue.ImageId = PreparedImageId;
        MatrixValue.OwnsImage = SnapshotImage;
        MatrixValue.TilesX = TilesX;
        MatrixValue.TilesY = TilesY;
        std::fill(MatrixValue.Tiles.begin(), MatrixValue.Tiles.end(), 1);
        MatrixValue.Wireframe = false;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::FillMatrix(int MatrixId, float Height, int TileId, std::string* Error)
    {
        if (!std::isfinite(Height))
        {
            if (Error)
                *Error = "FILL MATRIX requires a finite height";
            return false;
        }
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "FILL MATRIX", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        const int TileCount = MatrixValue.TilesX * MatrixValue.TilesY;
        if (TileId < 1 || TileId > TileCount)
        {
            if (Error)
                *Error = "FILL MATRIX tile number must be between 1 and " + std::to_string(TileCount);
            return false;
        }
        std::fill(MatrixValue.Heights.begin(), MatrixValue.Heights.end(), Height);
        std::fill(MatrixValue.Tiles.begin(), MatrixValue.Tiles.end(), TileId);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::RandomizeMatrix(int MatrixId, float Range, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "RANDOMIZE MATRIX", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (!std::isfinite(Range) || static_cast<double>(Range) >= std::ldexp(1.0, std::numeric_limits<int>::digits))
        {
            if (Error)
                *Error = "RANDOMIZE MATRIX maximum height is outside the integer range";
            return false;
        }
        const int MaximumHeight = Range <= 0.0f ? 0 : static_cast<int>(Range);
        if (MaximumHeight <= 0)
        {
            std::fill(MatrixValue.Heights.begin(), MatrixValue.Heights.end(), 0.0f);
        }
        else
        {
            std::uniform_int_distribution<int> Distribution(0, MaximumHeight - 1);
            for (float& Height : MatrixValue.Heights)
                Height = static_cast<float>(Distribution(Kernel_.Core().Rng()));
        }
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixHeight(int MatrixId, int X, int Z, float Height, std::string* Error)
    {
        if (!std::isfinite(Height))
        {
            if (Error)
                *Error = "SET MATRIX HEIGHT requires a finite height";
            return false;
        }
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "SET MATRIX HEIGHT", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (X < 0 || X > MatrixValue.SegX || Z < 0 || Z > MatrixValue.SegZ)
        {
            if (Error)
                *Error = "SET MATRIX HEIGHT tile coordinates are outside matrix " + std::to_string(MatrixId);
            return false;
        }
        MatrixValue.Heights[static_cast<size_t>(Z) * (MatrixValue.SegX + 1) + X] = Height;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixTile(int MatrixId, int X, int Z, int TileId, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "SET MATRIX TILE", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (X < 0 || X >= MatrixValue.SegX || Z < 0 || Z >= MatrixValue.SegZ)
        {
            if (Error)
                *Error = "SET MATRIX TILE tile coordinates are outside matrix " + std::to_string(MatrixId);
            return false;
        }
        const int TileCount = MatrixValue.TilesX * MatrixValue.TilesY;
        if (TileId < 1 || TileId > TileCount)
        {
            if (Error)
                *Error = "SET MATRIX TILE tile number must be between 1 and " + std::to_string(TileCount);
            return false;
        }
        MatrixValue.Tiles[static_cast<size_t>(Z) * MatrixValue.SegX + X] = TileId;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::UpdateMatrix(int MatrixId, std::string* Error)
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "UPDATE MATRIX", Error);
        if (!CheckedMatrix)
            return false;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::GetMatrixHeight(float& Output, int MatrixId, int X, int Z, std::string* Error) const
    {
        const Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "GET MATRIX HEIGHT", Error);
        if (!MatrixValuePointer)
            return false;
        const Matrix& MatrixValue = *MatrixValuePointer;
        if (X < 0 || X > MatrixValue.SegX || Z < 0 || Z > MatrixValue.SegZ)
        {
            if (Error)
                *Error = "GET MATRIX HEIGHT tile coordinates are outside matrix " + std::to_string(MatrixId);
            return false;
        }
        {
            Output = MatrixValue.Heights[static_cast<size_t>(Z) * (MatrixValue.SegX + 1) + X];
            return true;
        }
    }

    bool MatrixManager::DeleteMatrix(int MatrixId, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "DELETE MATRIX", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (MatrixValue.OwnsImage)
            Kernel_.ImageStoreRef().ReleaseImageInternal(MatrixValue.ImageId);
        Matrices_.erase(MatrixId);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixNormal(int MatrixId, int X, int Z, float Nx, float Ny, float Nz, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "SET MATRIX NORMAL", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (X < 0 || X > MatrixValue.SegX || Z < 0 || Z > MatrixValue.SegZ)
        {
            if (Error)
                *Error = "SET MATRIX NORMAL tile coordinates are outside matrix " + std::to_string(MatrixId);
            return false;
        }
        const size_t VertexCount = static_cast<size_t>(MatrixValue.SegX + 1) * (MatrixValue.SegZ + 1);

        // Lazy-init normals array (default Y-up).
        if (MatrixValue.Normals.size() != VertexCount * 3)
        {
            MatrixValue.Normals.resize(VertexCount * 3, 0.0f);
            for (size_t I = 0; I < VertexCount; ++I)
            {
                MatrixValue.Normals[I * 3 + 1] = 1.0f; // default: (0, 1, 0)
            }
        }

        const size_t Idx = (static_cast<size_t>(Z) * (MatrixValue.SegX + 1) + X) * 3;
        MatrixValue.Normals[Idx + 0] = Nx;
        MatrixValue.Normals[Idx + 1] = Ny;
        MatrixValue.Normals[Idx + 2] = Nz;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrix(int MatrixId, int Wireframe, int Transparency, int Cull, int Filter, int Light,
                                  int Fog, int Ambient, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "SET MATRIX", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        MatrixValue.Wireframe = Wireframe == 1;
        MatrixValue.Transparency = Transparency == 1;
        MatrixValue.Cull = Cull == 1;
        MatrixValue.Filter = Filter != 1;
        MatrixValue.Light = Light == 1;
        MatrixValue.Fog = Fog == 1;
        MatrixValue.Ambient = Ambient == 1;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixPriority(int MatrixId, int Priority, std::string* Error)
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "SET MATRIX PRIORITY", Error);
        if (!CheckedMatrix)
            return false;
        CheckedMatrix->RenderAfterObjects = Priority == 1;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixTexture(int MatrixId, int TextureMode, int MipMode, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "SET MATRIX TEXTURE", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (TextureMode < 0 || TextureMode > 2)
        {
            if (Error)
                *Error = "SET MATRIX TEXTURE texture mode must be between 0 and 2";
            return false;
        }
        if (MipMode < 0 || MipMode > 2)
        {
            if (Error)
                *Error = "SET MATRIX TEXTURE mip mode must be between 0 and 2";
            return false;
        }
        MatrixValue.TextureMode = TextureMode;
        MatrixValue.MipMode = MipMode;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixTrim(int MatrixId, float TrimX, float TrimY, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "SET MATRIX TRIM", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (!std::isfinite(TrimX) || !std::isfinite(TrimY))
        {
            if (Error)
                *Error = "SET MATRIX TRIM values must be finite";
            return false;
        }
        MatrixValue.TrimX = TrimX;
        MatrixValue.TrimY = TrimY;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetMatrixWireframe(int MatrixId, bool Enabled, std::string* Error)
    {
        auto* CheckedMatrix =
            RequireMatrix(MatrixId, Enabled ? "SET MATRIX WIREFRAME ON" : "SET MATRIX WIREFRAME OFF", Error);
        if (!CheckedMatrix)
            return false;
        CheckedMatrix->Wireframe = Enabled;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::GhostMatrixOn(int MatrixId, int Mode, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "GHOST MATRIX ON", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        if (Mode < 0 || Mode > 4)
        {
            if (Error)
                *Error = "GHOST MATRIX ON mode must be between 0 and 4";
            return false;
        }
        MatrixValue.Ghost = true;
        MatrixValue.GhostMode = Mode;
        MatrixValue.RenderAfterObjects = true;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::GhostMatrixOff(int MatrixId, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "GHOST MATRIX OFF", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        MatrixValue.Ghost = false;
        MatrixValue.RenderAfterObjects = false;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::ShiftMatrixLeft(int MatrixId, std::string* Error)
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "SHIFT MATRIX LEFT", Error);
        if (!CheckedMatrix)
            return false;
        ShiftMatrixStorage(*CheckedMatrix, 1, 0);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::ShiftMatrixRight(int MatrixId, std::string* Error)
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "SHIFT MATRIX RIGHT", Error);
        if (!CheckedMatrix)
            return false;
        ShiftMatrixStorage(*CheckedMatrix, -1, 0);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::ShiftMatrixDown(int MatrixId, std::string* Error)
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "SHIFT MATRIX DOWN", Error);
        if (!CheckedMatrix)
            return false;
        ShiftMatrixStorage(*CheckedMatrix, 0, -1);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::ShiftMatrixUp(int MatrixId, std::string* Error)
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "SHIFT MATRIX UP", Error);
        if (!CheckedMatrix)
            return false;
        ShiftMatrixStorage(*CheckedMatrix, 0, 1);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::PositionMatrix(int MatrixId, float X, float Y, float Z, std::string* Error)
    {
        Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "POSITION MATRIX", Error);
        if (!MatrixValuePointer)
            return false;
        Matrix& MatrixValue = *MatrixValuePointer;
        MatrixValue.PosX = X;
        MatrixValue.PosY = Y;
        MatrixValue.PosZ = Z;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::GetGroundHeight(float& Output, int MatrixId, float X, float Z, std::string* Error) const
    {
        if (!std::isfinite(X) || !std::isfinite(Z))
        {
            if (Error)
                *Error = "GET GROUND HEIGHT coordinates must be finite";
            return false;
        }
        const Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "GET GROUND HEIGHT", Error);
        if (!MatrixValuePointer)
            return false;
        const Matrix& MatrixValue = *MatrixValuePointer;
        const float LocalX = X;
        const float LocalZ = Z;

        const float StepX = MatrixValue.Width / static_cast<float>(MatrixValue.SegX);
        const float StepZ = MatrixValue.Depth / static_cast<float>(MatrixValue.SegZ);

        const float Fx = std::clamp(LocalX / StepX, 0.0f, static_cast<float>(MatrixValue.SegX));
        const float Fz = std::clamp(LocalZ / StepZ, 0.0f, static_cast<float>(MatrixValue.SegZ));

        const int Ix = std::clamp(static_cast<int>(std::floor(Fx)), 0, MatrixValue.SegX - 1);
        const int Iz = std::clamp(static_cast<int>(std::floor(Fz)), 0, MatrixValue.SegZ - 1);

        const float Tx = Fx - static_cast<float>(Ix);
        const float Tz = Fz - static_cast<float>(Iz);

        const float H00 = MatrixHeight(MatrixValue, Ix, Iz);
        const float H10 = MatrixHeight(MatrixValue, Ix + 1, Iz);
        const float H01 = MatrixHeight(MatrixValue, Ix, Iz + 1);
        const float H11 = MatrixHeight(MatrixValue, Ix + 1, Iz + 1);

        if (Tz < Tx)
        {
            Output = H00 * (1.0f - Tx) + H10 * (Tx - Tz) + H11 * Tz;
            return true;
        }
        {
            Output = H00 * (1.0f - Tz) + H01 * (Tz - Tx) + H11 * Tx;
            return true;
        }
    }

    bool MatrixManager::MatrixExist(int& Output, int MatrixId, std::string* Error) const
    {
        if (MatrixId < 1 || MatrixId > 65535)
        {
            if (Error)
                *Error = "MATRIX EXIST matrix number must be between 1 and 65535";
            return false;
        }
        {
            Output = Matrices_.contains(MatrixId) ? 1 : 0;
            return true;
        }
    }

    bool MatrixManager::MatrixPositionX(float& Output, int MatrixId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "MATRIX POSITION X", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->PosX;
            return true;
        }
    }

    bool MatrixManager::MatrixPositionY(float& Output, int MatrixId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "MATRIX POSITION Y", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->PosY;
            return true;
        }
    }

    bool MatrixManager::MatrixPositionZ(float& Output, int MatrixId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "MATRIX POSITION Z", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->PosZ;
            return true;
        }
    }

    bool MatrixManager::MatrixTileCount(int& Output, int MatrixId, std::string* Error) const
    {
        const Matrix* MatrixValuePointer = RequireMatrix(MatrixId, "MATRIX TILE COUNT", Error);
        if (!MatrixValuePointer)
            return false;
        const Matrix& MatrixValue = *MatrixValuePointer;
        {
            Output = MatrixValue.TilesX * MatrixValue.TilesY;
            return true;
        }
    }

    bool MatrixManager::MatrixTilesExist(int& Output, int MatrixId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "MATRIX TILES EXIST", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->ImageId != 0 ? 1 : 0;
            return true;
        }
    }

    bool MatrixManager::MatrixWireframeState(int& Output, int MatrixId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireMatrix(MatrixId, "MATRIX WIREFRAME STATE", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->Wireframe ? 1 : 0;
            return true;
        }
    }

    float MatrixManager::MatrixHeight(const Matrix& MatrixValue, int Vx, int Vz)
    {
        const int ClampedX = std::clamp(Vx, 0, MatrixValue.SegX);
        const int ClampedZ = std::clamp(Vz, 0, MatrixValue.SegZ);
        return MatrixValue.Heights[static_cast<size_t>(ClampedZ) * (MatrixValue.SegX + 1) + ClampedX];
    }

    int MatrixManager::MatrixTile(const Matrix& MatrixValue, int Tx, int Tz)
    {
        const int ClampedX = std::clamp(Tx, 0, MatrixValue.SegX - 1);
        const int ClampedZ = std::clamp(Tz, 0, MatrixValue.SegZ - 1);
        return MatrixValue.Tiles[static_cast<size_t>(ClampedZ) * MatrixValue.SegX + ClampedX];
    }

    bool MatrixManager::LoadTerrainHeightmap(Matrix& Terrain, const std::string& Path, const char* Command,
                                             std::string* Error)
    {
        SDL_Surface* Surface = LoadSurface(Path);
        if (!Surface)
        {
            if (Error)
                *Error = std::string(Command) + " failed to load heightmap '" + Path + "': " + SDL_GetError();
            return false;
        }

        SDL_Surface* Rgba = SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(Surface);
        if (!Rgba)
        {
            if (Error)
                *Error = std::string(Command) + " failed to convert heightmap '" + Path + "': " + SDL_GetError();
            return false;
        }
        if (Rgba->w != Rgba->h || Rgba->w < 2)
        {
            SDL_DestroySurface(Rgba);
            {
                if (Error)
                    *Error = std::string(Command) + " heightmap must be a square image of at least 2x2 pixels";
                return false;
            }
        }
        if (!SDL_LockSurface(Rgba))
        {
            const std::string OperationError = SDL_GetError();
            SDL_DestroySurface(Rgba);
            {
                if (Error)
                    *Error = std::string(Command) + " failed to read heightmap '" + Path + "': " + OperationError;
                return false;
            }
        }

        Terrain.SegX = Rgba->w - 1;
        Terrain.SegZ = Rgba->h - 1;
        Terrain.Width = static_cast<float>(Rgba->w);
        Terrain.Depth = static_cast<float>(Rgba->h);
        Terrain.HeightScale = 100.0f;
        Terrain.Heights.resize(static_cast<size_t>(Rgba->w) * static_cast<size_t>(Rgba->h));
        Terrain.Tiles.assign(static_cast<size_t>(Terrain.SegX) * static_cast<size_t>(Terrain.SegZ), 1);

        const auto* Pixels = static_cast<const uint8_t*>(Rgba->pixels);
        for (int Z = 0; Z < Rgba->h; ++Z)
        {
            const auto* Row = Pixels + static_cast<size_t>(Z) * static_cast<size_t>(Rgba->pitch);
            for (int X = 0; X < Rgba->w; ++X)
            {
                const auto* Pixel = Row + static_cast<size_t>(X) * 4;
                Terrain.Heights[static_cast<size_t>(Z) * static_cast<size_t>(Rgba->w) + static_cast<size_t>(X)] =
                    (static_cast<float>(Pixel[0]) + static_cast<float>(Pixel[1]) + static_cast<float>(Pixel[2])) /
                    (255.0f * 3.0f);
            }
        }
        SDL_UnlockSurface(Rgba);
        SDL_DestroySurface(Rgba);
        return true;
    }

    bool MatrixManager::MakeTerrain(int TerrainId, const std::string& Path, std::string* Error)
    {
        if (TerrainId < 1 || TerrainId > 65535)
        {
            if (Error)
                *Error = "MAKE TERRAIN number must be between 1 and 65535";
            return false;
        }
        if (Terrains_.contains(TerrainId))
        {
            if (Error)
                *Error = "MAKE TERRAIN cannot replace terrain " + std::to_string(TerrainId);
            return false;
        }

        Matrix Terrain;
        Terrain.Wireframe = false;
        Terrain.IsTerrain = true;
        Terrain.TextureMode = 2;
        if (!LoadTerrainHeightmap(Terrain, Path, "MAKE TERRAIN", Error))
            return false;
        Terrain.PosZ = Terrain.Depth;
        Terrain.Revision = ++TerrainRevision_;
        const float Width = Terrain.Width;
        Terrains_.emplace(TerrainId, std::move(Terrain));
        InvalidateRenderer();
        Kernel_.Camera().AutoPosition(Width * 0.5f, 0.0f, Width * 0.5f, 50.0f);
        return true;
    }

    bool MatrixManager::MakeAdvancedTerrain(int TerrainId, std::string* Error)
    {
        if (TerrainId < 1 || TerrainId > 65535)
        {
            if (Error)
                *Error = "MAKE TERRAIN number must be between 1 and 65535";
            return false;
        }
        if (Terrains_.contains(TerrainId))
        {
            if (Error)
                *Error = "MAKE TERRAIN cannot replace terrain " + std::to_string(TerrainId);
            return false;
        }
        Matrix Terrain;
        Terrain.Wireframe = false;
        Terrain.IsTerrain = true;
        Terrain.SegX = 32;
        Terrain.SegZ = 32;
        Terrain.Width = 1000.0f;
        Terrain.Depth = 1000.0f;
        Terrain.Heights.assign(33 * 33, 0.0f);
        Terrain.Tiles.assign(32 * 32, 1);
        Terrain.TextureMode = 1;
        Terrain.Revision = ++TerrainRevision_;
        Terrains_.emplace(TerrainId, std::move(Terrain));
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::DeleteTerrain(int TerrainId, std::string* Error)
    {
        Matrix* TerrainPointer = RequireTerrain(TerrainId, "DELETE TERRAIN", Error);
        if (!TerrainPointer)
            return false;
        Matrix& Terrain = *TerrainPointer;
        if (Terrain.OwnsImage)
            Kernel_.ImageStoreRef().ReleaseImageInternal(Terrain.ImageId);
        Terrains_.erase(TerrainId);
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetTerrainHeightmap(int TerrainId, const std::string& Path, std::string* Error)
    {
        Matrix* TerrainPointer = RequireTerrain(TerrainId, "SET TERRAIN HEIGHTMAP", Error);
        if (!TerrainPointer)
            return false;
        Matrix& Terrain = *TerrainPointer;
        if (!LoadTerrainHeightmap(Terrain, Path, "SET TERRAIN HEIGHTMAP", Error))
            return false;
        Terrain.Revision = ++TerrainRevision_;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetTerrainScale(int TerrainId, float X, float Y, float Z, std::string* Error)
    {
        Matrix* TerrainPointer = RequireTerrain(TerrainId, "SET TERRAIN SCALE", Error);
        if (!TerrainPointer)
            return false;
        Matrix& Terrain = *TerrainPointer;
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || X <= 0.0f || Z <= 0.0f)
        {
            if (Error)
                *Error = "SET TERRAIN SCALE requires positive finite X/Z scales and a finite Y scale";
            return false;
        }
        Terrain.Width = X * static_cast<float>(Terrain.SegX);
        Terrain.Depth = Z * static_cast<float>(Terrain.SegZ);
        Terrain.HeightScale = Y;
        Terrain.Revision = ++TerrainRevision_;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetTerrainTexture(int TerrainId, int ImageId, std::string* Error)
    {
        Matrix* TerrainPointer = RequireTerrain(TerrainId, "TEXTURE TERRAIN", Error);
        if (!TerrainPointer)
            return false;
        Matrix& Terrain = *TerrainPointer;
        if (Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "TEXTURE TERRAIN requires existing image " + std::to_string(ImageId);
            return false;
        }
        if (Terrain.OwnsImage)
            Kernel_.ImageStoreRef().ReleaseImageInternal(Terrain.ImageId);
        Terrain.ImageId = ImageId;
        Terrain.OwnsImage = false;
        Terrain.TilesX = 1;
        Terrain.TilesY = 1;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetTerrainTiling(int TerrainId, int TilesX, int TilesY, std::string* Error)
    {
        Matrix* TerrainPointer = RequireTerrain(TerrainId, "SET TERRAIN TILING", Error);
        if (!TerrainPointer)
            return false;
        Matrix& Terrain = *TerrainPointer;
        if (TilesX <= 0 || TilesY <= 0)
        {
            if (Error)
                *Error = "SET TERRAIN TILING divisions must be positive";
            return false;
        }
        Terrain.TilesX = TilesX;
        Terrain.TilesY = TilesY;
        Terrain.Revision = ++TerrainRevision_;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::SetTerrainSplit(int TerrainId, int Split, std::string* Error)
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "SET TERRAIN SPLIT", Error);
        if (!CheckedMatrix)
            return false;
        CheckedMatrix->SplitLevel = Split;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::BuildTerrain(int TerrainId, std::string* Error)
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "BUILD TERRAIN", Error);
        if (!CheckedMatrix)
            return false;
        CheckedMatrix->Revision = ++TerrainRevision_;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::PositionTerrain(int TerrainId, float X, float Y, float Z, std::string* Error)
    {
        Matrix* TerrainPointer = RequireTerrain(TerrainId, "POSITION TERRAIN", Error);
        if (!TerrainPointer)
            return false;
        Matrix& Terrain = *TerrainPointer;
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z))
        {
            if (Error)
                *Error = "POSITION TERRAIN coordinates must be finite";
            return false;
        }
        Terrain.PosX = X;
        Terrain.PosY = Y;
        Terrain.PosZ = Z;
        Terrain.Revision = ++TerrainRevision_;
        InvalidateRenderer();
        return true;
    }

    bool MatrixManager::GetTerrainHeight(float& Output, int TerrainId, float X, float Z, std::string* Error) const
    {
        const Matrix* TerrainPointer = RequireTerrain(TerrainId, "GET TERRAIN HEIGHT", Error);
        if (!TerrainPointer)
            return false;
        const Matrix& Terrain = *TerrainPointer;
        if (!std::isfinite(X) || !std::isfinite(Z))
        {
            if (Error)
                *Error = "GET TERRAIN HEIGHT coordinates must be finite";
            return false;
        }
        const float RealX = X;
        const float RealZ = Terrain.Depth - Z;
        const int X0 = static_cast<int>(std::clamp(static_cast<double>(RealX), 0.0, static_cast<double>(Terrain.SegX)));
        const int Z0 = static_cast<int>(std::clamp(static_cast<double>(RealZ), 0.0, static_cast<double>(Terrain.SegZ)));
        const int X1 = std::clamp(X0 + 1, 0, Terrain.SegX);
        const int Z1 = std::clamp(Z0 + 1, 0, Terrain.SegZ);
        const float A = MatrixHeight(Terrain, X0, Z0) * Terrain.HeightScale;
        const float B = MatrixHeight(Terrain, X1, Z0) * Terrain.HeightScale;
        const float C = MatrixHeight(Terrain, X0, Z1) * Terrain.HeightScale;
        const float D = MatrixHeight(Terrain, X1, Z1) * Terrain.HeightScale;
        const float PercentX = RealX - static_cast<float>(X0);
        const float PercentZ = RealZ - static_cast<float>(Z0);
        const float HeightX = A + ((C + D) - (A + B)) * PercentX;
        const float HeightZ = A + ((B + D) - (A + C)) * PercentZ;
        {
            Output = (HeightX + HeightZ) * 0.5f;
            return true;
        }
    }

    bool MatrixManager::GetTerrainSize(float& Output, int TerrainId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "GET TERRAIN SIZE", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->Width;
            return true;
        }
    }

    bool MatrixManager::GetTotalTerrainHeight(float& Output, int TerrainId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "GET TOTAL TERRAIN HEIGHT", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = 100.0f;
            return true;
        }
    }

    bool MatrixManager::TerrainExist(int& Output, int TerrainId, std::string* Error) const
    {
        if (TerrainId < 1 || TerrainId > 65535)
        {
            if (Error)
                *Error = "TERRAIN EXIST terrain number must be between 1 and 65535";
            return false;
        }
        {
            Output = Terrains_.contains(TerrainId) ? 1 : 0;
            return true;
        }
    }

    bool MatrixManager::TerrainPositionX(float& Output, int TerrainId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "TERRAIN POSITION X", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->PosX;
            return true;
        }
    }

    bool MatrixManager::TerrainPositionY(float& Output, int TerrainId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "TERRAIN POSITION Y", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->PosY;
            return true;
        }
    }

    bool MatrixManager::TerrainPositionZ(float& Output, int TerrainId, std::string* Error) const
    {
        auto* CheckedMatrix = RequireTerrain(TerrainId, "TERRAIN POSITION Z", Error);
        if (!CheckedMatrix)
            return false;
        {
            Output = CheckedMatrix->PosZ;
            return true;
        }
    }

} // namespace dbc
