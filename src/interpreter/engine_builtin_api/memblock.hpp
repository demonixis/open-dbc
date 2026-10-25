#pragma once

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/memblock_system.hpp"

namespace dbc
{
    // Allocation / destruction
    inline bool MakeMemblock(EngineKernel& K, int Num, int Size, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeMemblock(Num, Size, Error);
    }
    inline bool DeleteMemblock(EngineKernel& K, int Num, std::string* Error = nullptr)
    {
        return K.Memblocks().DeleteMemblock(Num, Error);
    }
    inline bool CopyMemblock(EngineKernel& K, int Src, int Dst, int SrcOff, int DstOff, int Size,
                             std::string* Error = nullptr)
    {
        return K.Memblocks().CopyMemblock(Src, Dst, SrcOff, DstOff, Size, Error);
    }

    // Write primitives
    inline bool WriteMemblockByte(EngineKernel& K, int Num, int Offset, int Val, std::string* Error = nullptr)
    {
        return K.Memblocks().WriteMemblockByte(Num, Offset, Val, Error);
    }
    inline bool WriteMemblockWord(EngineKernel& K, int Num, int Offset, int Val, std::string* Error = nullptr)
    {
        return K.Memblocks().WriteMemblockWord(Num, Offset, Val, Error);
    }
    inline bool WriteMemblockDword(EngineKernel& K, int Num, int Offset, int Val, std::string* Error = nullptr)
    {
        return K.Memblocks().WriteMemblockDword(Num, Offset, Val, Error);
    }
    inline bool WriteMemblockFloat(EngineKernel& K, int Num, int Offset, float Val, std::string* Error = nullptr)
    {
        return K.Memblocks().WriteMemblockFloat(Num, Offset, Val, Error);
    }
    inline bool WriteMemblockString(EngineKernel& K, int Num, int Offset, const char* S, std::string* Error = nullptr)
    {
        return K.Memblocks().WriteMemblockString(Num, Offset, S, Error);
    }

    // Read primitives
    inline bool ReadMemblockByte(EngineKernel& K, int& Output, int Num, int Offset, std::string* Error = nullptr)
    {
        return K.Memblocks().ReadMemblockByte(Output, Num, Offset, Error);
    }
    inline bool ReadMemblockWord(EngineKernel& K, int& Output, int Num, int Offset, std::string* Error = nullptr)
    {
        return K.Memblocks().ReadMemblockWord(Output, Num, Offset, Error);
    }
    inline bool ReadMemblockDword(EngineKernel& K, int& Output, int Num, int Offset, std::string* Error = nullptr)
    {
        return K.Memblocks().ReadMemblockDword(Output, Num, Offset, Error);
    }
    inline bool ReadMemblockFloat(EngineKernel& K, float& Output, int Num, int Offset, std::string* Error = nullptr)
    {
        return K.Memblocks().ReadMemblockFloat(Output, Num, Offset, Error);
    }

    // Queries
    inline bool MemblockExist(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.Memblocks().MemblockExist(Output, Num, Error);
    }
    inline bool MemblockSize(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.Memblocks().MemblockSize(Output, Num, Error);
    }

    // 3D mesh creation from the DBPro FVF memblock format
    inline bool MakeMeshFromMemblock(EngineKernel& K, int MeshId, int MemblockNum, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeMeshFromMemblock(MeshId, MemblockNum, Error);
    }
    inline bool ChangeMeshFromMemblock(EngineKernel& K, int MeshId, int MemblockNum, std::string* Error = nullptr)
    {
        return K.Memblocks().ChangeMeshFromMemblock(MeshId, MemblockNum, Error);
    }
    inline bool MakeMemblockFromMesh(EngineKernel& K, int MemblockNum, int MeshId, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeMemblockFromMesh(MemblockNum, MeshId, Error);
    }
    inline bool ChangeMesh(EngineKernel& K, int ObjId, int LimbId, int MeshId, std::string* Error = nullptr)
    {
        return K.Memblocks().ChangeMesh(ObjId, LimbId, MeshId, Error);
    }

    // Bitmap / Image ↔ Memblock conversions
    inline bool MakeMemblockFromBitmap(EngineKernel& K, int MemblockNum, int BitmapId, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeMemblockFromBitmap(MemblockNum, BitmapId, Error);
    }
    inline bool MakeBitmapFromMemblock(EngineKernel& K, int BitmapId, int MemblockNum, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeBitmapFromMemblock(BitmapId, MemblockNum, Error);
    }
    inline bool MakeMemblockFromImage(EngineKernel& K, int MemblockNum, int ImageId, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeMemblockFromImage(MemblockNum, ImageId, Error);
    }
    inline bool MakeImageFromMemblock(EngineKernel& K, int ImageId, int MemblockNum, std::string* Error = nullptr)
    {
        return K.Memblocks().MakeImageFromMemblock(ImageId, MemblockNum, Error);
    }

} // namespace dbc
