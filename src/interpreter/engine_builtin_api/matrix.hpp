#pragma once

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/matrix_manager.hpp"

namespace dbc
{
    inline bool MakeMatrix(EngineKernel& K, int MatrixId, float Width, float Depth, int SegX, int SegZ,
                           std::string* Error = nullptr)
    {
        return K.Matrices().MakeMatrix(MatrixId, Width, Depth, SegX, SegZ, true, Error);
    }
    inline bool PrepareMatrixTexture(EngineKernel& K, int MatrixId, int ImageId, int TilesX, int TilesY,
                                     std::string* Error = nullptr)
    {
        return K.Matrices().PrepareMatrixTexture(MatrixId, ImageId, TilesX, TilesY, false, Error);
    }
    inline bool FillMatrix(EngineKernel& K, int MatrixId, float Height, int TileId, std::string* Error = nullptr)
    {
        return K.Matrices().FillMatrix(MatrixId, Height, TileId, Error);
    }
    inline bool RandomizeMatrix(EngineKernel& K, int MatrixId, float Range, std::string* Error = nullptr)
    {
        return K.Matrices().RandomizeMatrix(MatrixId, Range, Error);
    }
    inline bool SetMatrixHeight(EngineKernel& K, int MatrixId, int X, int Z, float Height, std::string* Error = nullptr)
    {
        return K.Matrices().SetMatrixHeight(MatrixId, X, Z, Height, Error);
    }
    inline bool SetMatrixTile(EngineKernel& K, int MatrixId, int X, int Z, int TileId, std::string* Error = nullptr)
    {
        return K.Matrices().SetMatrixTile(MatrixId, X, Z, TileId, Error);
    }
    inline bool UpdateMatrix(EngineKernel& K, int MatrixId, std::string* Error = nullptr)
    {
        return K.Matrices().UpdateMatrix(MatrixId, Error);
    }
    inline bool GetMatrixHeight(EngineKernel& K, float& Output, int MatrixId, int X, int Z,
                                std::string* Error = nullptr)
    {
        return K.Matrices().GetMatrixHeight(Output, MatrixId, X, Z, Error);
    }

    inline bool ShiftMatrixLeft(EngineKernel& K, int MatrixId, std::string* Error = nullptr)
    {
        return K.Matrices().ShiftMatrixLeft(MatrixId, Error);
    }
    inline bool ShiftMatrixRight(EngineKernel& K, int MatrixId, std::string* Error = nullptr)
    {
        return K.Matrices().ShiftMatrixRight(MatrixId, Error);
    }
    inline bool ShiftMatrixDown(EngineKernel& K, int MatrixId, std::string* Error = nullptr)
    {
        return K.Matrices().ShiftMatrixDown(MatrixId, Error);
    }
    inline bool ShiftMatrixUp(EngineKernel& K, int MatrixId, std::string* Error = nullptr)
    {
        return K.Matrices().ShiftMatrixUp(MatrixId, Error);
    }

    inline bool PositionMatrix(EngineKernel& K, int MatrixId, float X, float Y, float Z, std::string* Error = nullptr)
    {
        return K.Matrices().PositionMatrix(MatrixId, X, Y, Z, Error);
    }
    inline bool GetGroundHeight(EngineKernel& K, float& Output, int MatrixId, float X, float Z,
                                std::string* Error = nullptr)
    {
        return K.Matrices().GetGroundHeight(Output, MatrixId, X, Z, Error);
    }

} // namespace dbc
