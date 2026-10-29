#pragma once

#include "../../engine/systems/draw_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/image_store.hpp"
#include "../../engine/systems/sprite_system.hpp"

namespace dbc
{
    // --- Color packing (delegates to DrawSystem::PackRgb) ---

    inline int Rgb(int R, int G, int B)
    {
        return DrawSystem::PackRgb(R, G, B);
    }

    // --- 2D drawing primitives ---

    inline void Ink(EngineKernel& K, int ForeRgbPacked, int BackRgbPacked = -1)
    {
        K.Draw().SetInk(ForeRgbPacked, BackRgbPacked);
    }

    inline bool Dot(EngineKernel& K, int X, int Y, std::string* Error = nullptr)
    {
        return K.Draw().DrawDot(X, Y, Error);
    }

    inline bool Dot(EngineKernel& K, int X, int Y, int Color, std::string* Error = nullptr)
    {
        return K.Draw().DrawDot(X, Y, Color, Error);
    }

    inline bool Line(EngineKernel& K, int X1, int Y1, int X2, int Y2, std::string* Error = nullptr)
    {
        return K.Draw().DrawLine(X1, Y1, X2, Y2, Error);
    }

    inline bool Box(EngineKernel& K, int Left, int Top, int Right, int Bottom, int TopLeft, int TopRight,
                    int BottomRight, int BottomLeft, std::string* Error = nullptr)
    {
        return K.Draw().DrawBox(Left, Top, Right, Bottom, TopLeft, TopRight, BottomRight, BottomLeft, Error);
    }

    inline bool Box(EngineKernel& K, int Left, int Top, int Right, int Bottom, std::string* Error = nullptr)
    {
        return K.Draw().DrawBox(Left, Top, Right, Bottom, Error);
    }

    inline bool Circle(EngineKernel& K, int X, int Y, int Radius, std::string* Error = nullptr)
    {
        return K.Draw().DrawCircle(X, Y, Radius, Error);
    }

    inline bool Ellipse(EngineKernel& K, int X, int Y, int XRadius, int YRadius, std::string* Error = nullptr)
    {
        return K.Draw().DrawEllipse(X, Y, XRadius, YRadius, Error);
    }

    inline bool Point(EngineKernel& K, int& Output, int X, int Y, std::string* Error = nullptr)
    {
        return K.Draw().ReadPoint(Output, X, Y, Error);
    }

    inline bool Cls(EngineKernel& K, int RgbPacked, std::string* Error = nullptr)
    {
        // If a bitmap is active, clear its surface instead of the backbuffer.
        const int BmpId = K.ImageStoreRef().CurrentBitmap();
        if (BmpId > 0)
        {
            SDL_Surface* Bmp = K.ImageStoreRef().GetBitmapSurface(BmpId);
            if (Bmp)
            {
                const Uint32 Color = SDL_MapSurfaceRGBA(Bmp, static_cast<Uint8>((RgbPacked >> 16) & 0xFF),
                                                        static_cast<Uint8>((RgbPacked >> 8) & 0xFF),
                                                        static_cast<Uint8>(RgbPacked & 0xFF), 255);
                SDL_FillSurfaceRect(Bmp, nullptr, Color);
                K.ImageStoreRef().InvalidateBitmapGpuTexture(BmpId);
                return true;
            }
        }
        K.Sprites().ClearBobTrails();
        K.Sprites().ClearPastedSprites();
        return K.Draw().ClearScreen(RgbPacked, Error);
    }

    // --- BOB commands (delegate to SpriteSystem) ---

    inline bool Bob(EngineKernel& K, int Id, int X, int Y, int Image, std::string* Error = nullptr)
    {
        return K.Sprites().CreateBob(Id, X, Y, Image, Error);
    }

    inline bool DeleteBob(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().DeleteBob(Id, Error);
    }

    inline bool FlipBob(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().FlipBob(Id, Error);
    }

    inline bool MirrorBob(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().MirrorBob(Id, Error);
    }

    inline bool OffsetBob(EngineKernel& K, int Id, int Ox, int Oy, std::string* Error = nullptr)
    {
        return K.Sprites().OffsetBob(Id, Ox, Oy, Error);
    }

    inline bool PasteBob(EngineKernel& K, int Id, int X, int Y, std::string* Error = nullptr)
    {
        return K.Sprites().PasteBob(Id, X, Y, Error);
    }

    inline bool ScaleBob(EngineKernel& K, int Id, int Scale, std::string* Error = nullptr)
    {
        return K.Sprites().ScaleBob(Id, Scale, Error);
    }

    inline bool SetBob(EngineKernel& K, int Id, int Backsave = 1, int Transparency = 1, std::string* Error = nullptr)
    {
        return K.Sprites().SetBob(Id, Backsave, Transparency, Error);
    }

    inline bool StretchBob(EngineKernel& K, int Id, int Sx, int Sy, std::string* Error = nullptr)
    {
        return K.Sprites().StretchBob(Id, Sx, Sy, Error);
    }

    inline bool HideBob(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().HideBob(Id, Error);
    }

    inline bool ShowBob(EngineKernel& K, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().ShowBob(Id, Error);
    }

    inline bool PositionBob(EngineKernel& K, int Id, int X, int Y, std::string* Error = nullptr)
    {
        return K.Sprites().PositionBob(Id, X, Y, Error);
    }

    inline bool RotateBob(EngineKernel& K, int Id, float Angle, std::string* Error = nullptr)
    {
        return K.Sprites().RotateBob(Id, Angle, Error);
    }

    inline bool MoveBob(EngineKernel& K, int Id, float Distance, std::string* Error = nullptr)
    {
        return K.Sprites().MoveBob(Id, Distance, Error);
    }

    // BOB queries
    inline bool BobCollision(EngineKernel& K, int& Output, int Id, int Target = 0, std::string* Error = nullptr)
    {
        return K.Sprites().BobCollision(Output, Id, Target, Error);
    }

    inline int BobExist(EngineKernel& K, int Id)
    {
        return K.Sprites().BobExist(Id);
    }

    inline int BobFlipped(EngineKernel& K, int Id)
    {
        return K.Sprites().BobFlipped(Id);
    }

    inline bool BobHeight(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().BobHeight(Output, Id, Error);
    }

    inline bool BobHit(EngineKernel& K, int& Output, int Id, int Target = 0, std::string* Error = nullptr)
    {
        return K.Sprites().BobHit(Output, Id, Target, Error);
    }

    inline int BobImage(EngineKernel& K, int Id)
    {
        return K.Sprites().BobImage(Id);
    }

    inline int BobMirrored(EngineKernel& K, int Id)
    {
        return K.Sprites().BobMirrored(Id);
    }

    inline int BobOffsetX(EngineKernel& K, int Id)
    {
        return K.Sprites().BobOffsetX(Id);
    }

    inline int BobOffsetY(EngineKernel& K, int Id)
    {
        return K.Sprites().BobOffsetY(Id);
    }

    inline bool BobScaleX(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().BobScaleX(Output, Id, Error);
    }

    inline bool BobScaleY(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().BobScaleY(Output, Id, Error);
    }

    inline bool BobWidth(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().BobWidth(Output, Id, Error);
    }

    inline bool BobX(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().BobX(Output, Id, Error);
    }

    inline bool BobY(EngineKernel& K, int& Output, int Id, std::string* Error = nullptr)
    {
        return K.Sprites().BobY(Output, Id, Error);
    }

    inline float BobAngle(EngineKernel& K, int Id)
    {
        return K.Sprites().BobAngle(Id);
    }

} // namespace dbc
