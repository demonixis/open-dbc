#pragma once

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/image_store.hpp"
#include "../../engine/systems/sprite_system.hpp"

#include <string>

#ifdef _WIN32
#undef LoadImage
#endif

namespace dbc
{
    inline bool LoadImageFile(EngineKernel& K, const std::string& Path, int ImageId, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().LoadImageFile(Path, ImageId, 0, 0, Error);
    }

    inline bool DeleteImage(EngineKernel& K, int ImageId, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().DeleteImage(ImageId, Error);
    }

    inline int ImageExist(EngineKernel& K, int ImageId)
    {
        return K.ImageStoreRef().ImageExist(ImageId);
    }

    inline bool PasteImage(EngineKernel& K, int ImageId, int X, int Y, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().PasteImage(ImageId, X, Y, false, Error);
    }

    inline bool GetImage(EngineKernel& K, int ImageId, int Left, int Top, int Right, int Bottom,
                         std::string* Error = nullptr)
    {
        return K.ImageStoreRef().GetImage(ImageId, Left, Top, Right, Bottom, Error);
    }

    inline bool SaveImage(EngineKernel& K, const std::string& Filename, int ImageId, std::string* Error = nullptr)
    {
        return K.ImageStoreRef().SaveImage(Filename, ImageId, Error);
    }

    // --- Sprite commands (delegate to SpriteSystem) ---

    inline bool Sprite(EngineKernel& K, int Id, int X, int Y, int ImageId, std::string* Error = nullptr)
    {
        return K.Sprites().CreateSprite(Id, X, Y, ImageId, Error);
    }

    inline bool DeleteSprite(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().DeleteSprite(Id, Error);
    }

    inline bool HideSprite(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().HideSprite(Id, Error);
    }

    inline bool ShowSprite(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().ShowSprite(Id, Error);
    }

    inline void HideAllSprites(EngineKernel& K)
    {
        K.Sprites().HideAllSprites();
    }

    inline void ShowAllSprites(EngineKernel& K)
    {
        K.Sprites().ShowAllSprites();
    }

    inline bool MirrorSprite(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().MirrorSprite(Id, Error);
    }

    inline bool FlipSprite(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().FlipSprite(Id, Error);
    }

    inline bool SizeSprite(EngineKernel& K, int Id, int W, int H, std::string* Error = nullptr)
    {
        return K.Sprites().SizeSprite(Id, W, H, Error);
    }

    inline bool ScaleSprite(EngineKernel& K, int Id, int Scale, std::string* Error = nullptr)
    {
        return K.Sprites().ScaleSprite(Id, Scale, Error);
    }

    inline bool StretchSprite(EngineKernel& K, int Id, int Sx, int Sy, std::string* Error = nullptr)
    {
        return K.Sprites().StretchSprite(Id, Sx, Sy, Error);
    }

    inline bool OffsetSprite(EngineKernel& K, int Id, int Ox, int Oy, std::string* Error = nullptr)
    {
        return K.Sprites().OffsetSprite(Id, Ox, Oy, Error);
    }

    inline bool PasteSprite(EngineKernel& K, int Id, int X, int Y, std::string* Error = nullptr)
    {
        return K.Sprites().PasteSprite(Id, X, Y, Error);
    }

    inline bool SetSprite(EngineKernel& K, int Id, int Backsave = 1, int Transparency = 1, std::string* Error = nullptr)
    {
        return K.Sprites().SetSprite(Id, Backsave, Transparency, Error);
    }

    inline bool PositionSprite(EngineKernel& K, int Id, int X, int Y, std::string* Error = nullptr)
    {
        return K.Sprites().PositionSprite(Id, X, Y, Error);
    }

    inline bool RotateSprite(EngineKernel& K, int Id, float Angle, std::string* Error = nullptr)
    {
        return K.Sprites().RotateSprite(Id, Angle, Error);
    }

    inline bool MoveSprite(EngineKernel& K, int Id, float Distance, std::string* Error = nullptr)
    {
        return K.Sprites().MoveSprite(Id, Distance, Error);
    }

    inline bool CloneSprite(EngineKernel& K, int Id, int DestinationId, std::string* Error = nullptr)
    {
        return K.Sprites().CloneSprite(Id, DestinationId, Error);
    }

    inline bool CreateAnimatedSprite(EngineKernel& K, int Id, const std::string& Path, int Across, int Down,
                                     int ImageId, std::string* Error = nullptr)
    {
        return K.Sprites().CreateAnimatedSprite(Id, Path, Across, Down, ImageId, Error);
    }

    inline bool PlaySprite(EngineKernel& K, int Id, int Start, int End, int Delay, std::string* Error = nullptr)
    {
        return K.Sprites().PlaySprite(Id, Start, End, Delay, Error);
    }

    inline bool SetSpriteFrame(EngineKernel& K, int Id, int Frame, std::string* Error = nullptr)
    {
        return K.Sprites().SetSpriteFrame(Id, Frame, Error);
    }

    inline bool SetSpriteImage(EngineKernel& K, int Id, int ImageId, std::string* Error = nullptr)
    {
        return K.Sprites().SetSpriteImage(Id, ImageId, Error);
    }

    inline bool SetSpriteTextureCoord(EngineKernel& K, int Id, int Vertex, float U, float V,
                                      std::string* Error = nullptr)
    {
        return K.Sprites().SetSpriteTextureCoord(Id, Vertex, U, V, Error);
    }

    // --- Sprite queries ---

    inline int SpriteExist(EngineKernel& K, int Id)
    {
        return K.Sprites().SpriteExist(Id);
    }

    inline bool SpriteCollision(EngineKernel& K, int& Output, int Id, int Target = 0, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteCollision(Output, Id, Target, Error);
    }

    inline bool SpriteHit(EngineKernel& K, int& Output, int Id, int Target = 0, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteHit(Output, Id, Target, Error);
    }

    inline bool SpriteFlipped(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteFlipped(Output, Id, Error);
    }

    inline bool SpriteHeight(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteHeight(Output, Id, Error);
    }

    inline bool SpriteImage(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteImage(Output, Id, Error);
    }

    inline bool SpriteMirrored(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteMirrored(Output, Id, Error);
    }

    inline bool SpriteOffsetX(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteOffsetX(Output, Id, Error);
    }

    inline bool SpriteOffsetY(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteOffsetY(Output, Id, Error);
    }

    inline bool SpriteScaleX(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteScaleX(Output, Id, Error);
    }

    inline bool SpriteScaleY(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteScaleY(Output, Id, Error);
    }

    inline bool SpriteWidth(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteWidth(Output, Id, Error);
    }

    inline bool SpriteX(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteX(Output, Id, Error);
    }

    inline bool SpriteY(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteY(Output, Id, Error);
    }

    inline bool SpriteAngle(EngineKernel& K, float& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().SpriteAngle(Output, Id, Error);
    }

    inline int SpriteVisible(EngineKernel& K, int Id)
    {
        return K.Sprites().SpriteExist(Id);
    }

} // namespace dbc
