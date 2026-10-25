#pragma once

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/image_store.hpp"

#include <string>

namespace dbc
{
    inline bool LoadBitmap(EngineKernel& K, const std::string& Path, int BitmapId, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().LoadBitmap(Path, BitmapId, Error);
    }
    inline bool DeleteBitmap(EngineKernel& K, int BitmapId, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().DeleteBitmap(BitmapId, Error);
    }
    inline bool SetCurrentBitmap(EngineKernel& K, int BitmapId, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().SetCurrentBitmap(BitmapId, Error);
    }
    inline int CurrentBitmap(EngineKernel& K)
    {
        return K.ImageStoreRef().CurrentBitmap();
    }
    inline bool BlurBitmap(EngineKernel& K, int BitmapId, int BlurLevel, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().BlurBitmap(BitmapId, BlurLevel, Error);
    }

    inline int BitmapWidth(EngineKernel& K, int BitmapId = 0)
    {
        return K.ImageStoreRef().BitmapWidth(BitmapId);
    }
    inline int BitmapHeight(EngineKernel& K, int BitmapId = 0)
    {
        return K.ImageStoreRef().BitmapHeight(BitmapId);
    }
    inline int BitmapExist(EngineKernel& K, int BitmapId)
    {
        return K.ImageStoreRef().BitmapExist(BitmapId);
    }
    inline bool CreateBitmap(EngineKernel& K, int BitmapId, int Width, int Height, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().CreateBitmap(BitmapId, Width, Height, Error);
    }

    inline bool SaveBitmap(EngineKernel& K, const std::string& Filename, int BitmapId = 0, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().SaveBitmap(Filename, BitmapId, Error);
    }

} // namespace dbc
