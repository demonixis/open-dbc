#include "draw_system.hpp"
#include "../engine_types.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "sprite_system.hpp"
#include "text_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace dbc
{
    namespace
    {
        bool ActiveBitmap(EngineKernel& Kernel, int& BitmapId, SDL_Surface*& Surface, std::string* Error)
        {
            BitmapId = Kernel.ImageStoreRef().CurrentBitmap();
            Surface = BitmapId > 0 ? Kernel.ImageStoreRef().GetBitmapSurface(BitmapId)
                                   : Kernel.ImageStoreRef().EnsureScreenBitmap(Error);
            return Surface != nullptr;
        }

        bool MutableActiveBitmap(EngineKernel& Kernel, int& BitmapId, SDL_Surface*& Surface, std::string* Error)
        {
            if (Kernel.ImageStoreRef().CurrentBitmap() == 0)
                if (!Kernel.Sprites().MaterializePresentedBobTrails(Error))
                    return false;
            return ActiveBitmap(Kernel, BitmapId, Surface, Error);
        }

        void WritePixel(SDL_Surface* Surface, int X, int Y, int Color)
        {
            if (X < 0 || Y < 0 || X >= Surface->w || Y >= Surface->h)
            {
                return;
            }
            SDL_WriteSurfacePixel(Surface, X, Y, static_cast<Uint8>((Color >> 16) & 0xFF),
                                  static_cast<Uint8>((Color >> 8) & 0xFF), static_cast<Uint8>(Color & 0xFF), 255);
        }

        void DrawSurfaceLine(SDL_Surface* Surface, double StartX, double StartY, double EndX, double EndY, int Color)
        {
            // Clip before rasterizing so extreme off-screen BASIC coordinates cannot cause unbounded work.
            const double DeltaX = EndX - StartX;
            const double DeltaY = EndY - StartY;
            double Enter = 0.0;
            double Leave = 1.0;
            const auto Clip = [&](double Direction, double Distance)
            {
                if (Direction == 0.0)
                    return Distance >= 0.0;
                const double Ratio = Distance / Direction;
                if (Direction < 0.0)
                    Enter = std::max(Enter, Ratio);
                else
                    Leave = std::min(Leave, Ratio);
                return Enter <= Leave;
            };
            if (!Clip(-DeltaX, StartX) || !Clip(DeltaX, Surface->w - 1.0 - StartX) || !Clip(-DeltaY, StartY) ||
                !Clip(DeltaY, Surface->h - 1.0 - StartY))
                return;
            int X1 = static_cast<int>(std::clamp(std::round(StartX + Enter * DeltaX), 0.0, Surface->w - 1.0));
            int Y1 = static_cast<int>(std::clamp(std::round(StartY + Enter * DeltaY), 0.0, Surface->h - 1.0));
            const int X2 = static_cast<int>(std::clamp(std::round(StartX + Leave * DeltaX), 0.0, Surface->w - 1.0));
            const int Y2 = static_cast<int>(std::clamp(std::round(StartY + Leave * DeltaY), 0.0, Surface->h - 1.0));

            const std::int64_t Dx = std::abs(static_cast<std::int64_t>(X2) - X1);
            const int Sx = X1 < X2 ? 1 : -1;
            const std::int64_t Dy = -std::abs(static_cast<std::int64_t>(Y2) - Y1);
            const int Sy = Y1 < Y2 ? 1 : -1;
            std::int64_t Error = Dx + Dy;
            for (;;)
            {
                WritePixel(Surface, X1, Y1, Color);
                if (X1 == X2 && Y1 == Y2)
                {
                    break;
                }
                const std::int64_t Twice = Error * 2;
                if (Twice >= Dy)
                {
                    Error += Dy;
                    X1 += Sx;
                }
                if (Twice <= Dx)
                {
                    Error += Dx;
                    Y1 += Sy;
                }
            }
        }
    } // namespace

    DrawSystem::DrawSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    DrawSystem::~DrawSystem()
    {
        Primitives2d_.clear();
    }

    // === Color packing (pure function) ===

    int DrawSystem::PackRgb(int R, int G, int B)
    {
        const int Cr = std::clamp(R, 0, 255);
        const int Cg = std::clamp(G, 0, 255);
        const int Cb = std::clamp(B, 0, 255);
        return (Cr << 16) | (Cg << 8) | Cb; // DarkBASIC layout: 0x00RRGGBB
    }

    // === 2D drawing primitives ===

    void DrawSystem::SetInk(int ForeRgbPacked, int BackRgbPacked)
    {
        Kernel_.Text().InkForeColor() = ForeRgbPacked;
        if (BackRgbPacked >= 0)
        {
            BackdropColor_ = BackRgbPacked;
        }
    }

    bool DrawSystem::DrawDot(int X, int Y, std::string* Error)
    {
        return DrawDot(X, Y, Kernel_.Text().InkForeColor(), Error);
    }

    bool DrawSystem::DrawDot(int X, int Y, int Color, std::string* Error)
    {
        int BitmapId = 0;
        SDL_Surface* Surface = nullptr;
        if (!MutableActiveBitmap(Kernel_, BitmapId, Surface, Error))
            return false;
        {
            WritePixel(Surface, X, Y, Color);
            Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(BitmapId);
            if (BitmapId > 0)
                return true;
        }
        Primitives2d_.push_back(Primitive2D{
            Primitive2DType::Dot,
            X,
            Y,
            0,
            0,
            Color,
        });
        return true;
    }

    bool DrawSystem::DrawLine(int X1, int Y1, int X2, int Y2, std::string* Error)
    {
        int BitmapId = 0;
        SDL_Surface* Surface = nullptr;
        if (!MutableActiveBitmap(Kernel_, BitmapId, Surface, Error))
            return false;
        {
            DrawSurfaceLine(Surface, X1, Y1, X2, Y2, Kernel_.Text().InkForeColor());
            Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(BitmapId);
            if (BitmapId > 0)
                return true;
        }
        Primitives2d_.push_back(Primitive2D{
            Primitive2DType::Line,
            X1,
            Y1,
            X2,
            Y2,
            Kernel_.Text().InkForeColor(),
        });
        return true;
    }

    bool DrawSystem::DrawBox(int Left, int Top, int Right, int Bottom, std::string* Error)
    {
        const int Color = Kernel_.Text().InkForeColor();
        return DrawBox(Left, Top, Right, Bottom, Color, Color, Color, Color, Error);
    }

    // BOX fills its rectangle; the DBPro form shades it between four corner colours.
    bool DrawSystem::DrawBox(int Left, int Top, int Right, int Bottom, int TopLeft, int TopRight, int BottomRight,
                             int BottomLeft, std::string* Error)
    {
        int BitmapId = 0;
        SDL_Surface* Surface = nullptr;
        if (!MutableActiveBitmap(Kernel_, BitmapId, Surface, Error))
            return false;
        {
            const int X0 = std::min(Left, Right);
            const int X1 = std::max(Left, Right);
            const int Y0 = std::min(Top, Bottom);
            const int Y1 = std::max(Top, Bottom);
            const float Width = static_cast<float>(std::max(1.0, static_cast<double>(X1) - X0));
            const float Height = static_cast<float>(std::max(1.0, static_cast<double>(Y1) - Y0));
            const auto Channel = [](int Color, int Shift) { return static_cast<float>((Color >> Shift) & 0xFF); };
            const auto Blend = [&](int Shift, float U, float V)
            {
                const float TopRow = Channel(TopLeft, Shift) * (1.0f - U) + Channel(TopRight, Shift) * U;
                const float BottomRow = Channel(BottomLeft, Shift) * (1.0f - U) + Channel(BottomRight, Shift) * U;
                return static_cast<int>(std::lround(TopRow * (1.0f - V) + BottomRow * V));
            };
            for (int Y = std::max(0, Y0); Y <= std::min(Surface->h - 1, Y1); ++Y)
            {
                const float V = static_cast<float>(static_cast<double>(Y) - Y0) / Height;
                for (int X = std::max(0, X0); X <= std::min(Surface->w - 1, X1); ++X)
                {
                    const float U = static_cast<float>(static_cast<double>(X) - X0) / Width;
                    WritePixel(Surface, X, Y, (Blend(16, U, V) << 16) | (Blend(8, U, V) << 8) | Blend(0, U, V));
                }
            }
            Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(BitmapId);
            if (BitmapId > 0)
                return true;
        }
        Primitive2D Box{Primitive2DType::Box, Left, Top, Right, Bottom, TopLeft};
        Box.CornerColors[0] = TopLeft;
        Box.CornerColors[1] = TopRight;
        Box.CornerColors[2] = BottomRight;
        Box.CornerColors[3] = BottomLeft;
        Primitives2d_.push_back(Box);
        return true;
    }

    bool DrawSystem::DrawCircle(int X, int Y, int Radius, std::string* Error)
    {
        int BitmapId = 0;
        SDL_Surface* Surface = nullptr;
        if (!MutableActiveBitmap(Kernel_, BitmapId, Surface, Error))
            return false;
        {
            const double R = std::max(1.0, std::abs(static_cast<double>(Radius)));
            constexpr int Segments = 96;
            double PreviousX = X + R;
            double PreviousY = Y;
            for (int I = 1; I <= Segments; ++I)
            {
                const float Angle = static_cast<float>(I) * static_cast<float>(2.0 * M_PI / Segments);
                const double NextX = X + std::round(std::cos(Angle) * R);
                const double NextY = Y + std::round(std::sin(Angle) * R);
                DrawSurfaceLine(Surface, PreviousX, PreviousY, NextX, NextY, Kernel_.Text().InkForeColor());
                PreviousX = NextX;
                PreviousY = NextY;
            }
            Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(BitmapId);
            if (BitmapId > 0)
                return true;
        }
        Primitives2d_.push_back(Primitive2D{
            Primitive2DType::Circle,
            X,
            Y,
            Radius,
            Radius,
            Kernel_.Text().InkForeColor(),
        });
        return true;
    }

    bool DrawSystem::DrawEllipse(int X, int Y, int XRadius, int YRadius, std::string* Error)
    {
        int BitmapId = 0;
        SDL_Surface* Surface = nullptr;
        if (!MutableActiveBitmap(Kernel_, BitmapId, Surface, Error))
            return false;
        {
            const double Rx = std::max(1.0, std::abs(static_cast<double>(XRadius)));
            const double Ry = std::max(1.0, std::abs(static_cast<double>(YRadius)));
            constexpr int Segments = 96;
            double PreviousX = X + Rx;
            double PreviousY = Y;
            for (int I = 1; I <= Segments; ++I)
            {
                const float Angle = static_cast<float>(I) * static_cast<float>(2.0 * M_PI / Segments);
                const double NextX = X + std::round(std::cos(Angle) * Rx);
                const double NextY = Y + std::round(std::sin(Angle) * Ry);
                DrawSurfaceLine(Surface, PreviousX, PreviousY, NextX, NextY, Kernel_.Text().InkForeColor());
                PreviousX = NextX;
                PreviousY = NextY;
            }
            Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(BitmapId);
            if (BitmapId > 0)
                return true;
        }
        Primitives2d_.push_back(Primitive2D{
            Primitive2DType::Ellipse,
            X,
            Y,
            XRadius,
            YRadius,
            Kernel_.Text().InkForeColor(),
        });
        return true;
    }

    bool DrawSystem::ReadPoint(int& Output, int X, int Y, std::string* Error)
    {
        int BitmapId = 0;
        SDL_Surface* Surface = nullptr;
        if (!ActiveBitmap(Kernel_, BitmapId, Surface, Error))
            return false;
        {
            Uint8 R = 0;
            Uint8 G = 0;
            Uint8 B = 0;
            Uint8 A = 0;
            if (SDL_ReadSurfacePixel(Surface, X, Y, &R, &G, &B, &A))
            {
                Output = PackRgb(R, G, B);
                return true;
            }
        }
        Output = BackdropColor_;
        return true;
    }

    bool DrawSystem::ClearScreen(int RgbPacked, std::string* Error)
    {
        Primitives2d_.clear();
        Kernel_.Text().ClearDebugTextLines();
        Kernel_.ImageStoreRef().ClearOverlayBlits();
        if (!Kernel_.ImageStoreRef().DeleteBitmap(0, Error))
            return false;
        Kernel_.Text().ClsRequested() = true;
        Kernel_.Text().ClsColor() = RgbPacked;
        return true;
    }

    // === Fog ===

    int DrawSystem::IsFogAvailable()
    {
        return 1;
    }

    void DrawSystem::EnableFog()
    {
        FogOn_ = true;
    }

    void DrawSystem::DisableFog()
    {
        FogOn_ = false;
    }

    void DrawSystem::SetFogColor(int RgbPacked, bool SyncBackdrop)
    {
        FogColor_ = RgbPacked;
        if (SyncBackdrop)
            BackdropColor_ = RgbPacked;
    }

    void DrawSystem::SetFogDistance(float Distance)
    {
        FogDistance_ = Distance;
    }

    void DrawSystem::SetFogDistance(float StartDistance, float EndDistance)
    {
        FogStartDistance_ = StartDistance;
        FogDistance_ = EndDistance;
    }

    // === Backdrop ===

    void DrawSystem::EnableBackdrop()
    {
        BackdropOn_ = true;
    }

    void DrawSystem::DisableBackdrop()
    {
        BackdropOn_ = false;
    }

    void DrawSystem::SetBackdropColor(int RgbPacked)
    {
        BackdropColor_ = RgbPacked;
    }

    bool DrawSystem::SetBackdropImage(int ImageId, std::string* Error)
    {
        if (Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "TEXTURE BACKDROP requires existing image " + std::to_string(ImageId);
            return false;
        }
        BackdropImageId_ = ImageId;
        return true;
    }

    void DrawSystem::DoScrollBackdrop(float U, float V)
    {
        // DBC Classic: SCROLL BACKDROP sets absolute offset, not cumulative.
        // Swarm pattern: inc starscrolly#,1.0 / scroll backdrop 0,0-starscrolly#
        // → linearly increasing offset each frame → constant speed scrolling.
        BackdropScrollX_ = U;
        BackdropScrollY_ = V;
    }

    void DrawSystem::DoClearCameraView()
    {
        BackdropColor_ = 0x000000;
        BackdropOn_ = true;
    }

    void DrawSystem::DoClearCameraViewRgb(int /*camera_id*/, int R, int G, int B)
    {
        const int Cr = std::clamp(R, 0, 255);
        const int Cg = std::clamp(G, 0, 255);
        const int Cb = std::clamp(B, 0, 255);
        BackdropColor_ = (Cr << 16) | (Cg << 8) | Cb;
        BackdropOn_ = true;
    }

    // --- Mutable accessors ---

    void DrawSystem::ClearPrimitives2D()
    {
        Primitives2d_.clear();
    }

    int& DrawSystem::BackdropColor()
    {
        return BackdropColor_;
    }

    int DrawSystem::BackdropColor() const
    {
        return BackdropColor_;
    }

    int& DrawSystem::FogColor()
    {
        return FogColor_;
    }

    int DrawSystem::FogColor() const
    {
        return FogColor_;
    }

    float& DrawSystem::FogDistance()
    {
        return FogDistance_;
    }

    float DrawSystem::FogDistance() const
    {
        return FogDistance_;
    }

    float& DrawSystem::FogStartDistance()
    {
        return FogStartDistance_;
    }

    float DrawSystem::FogStartDistance() const
    {
        return FogStartDistance_;
    }

    bool& DrawSystem::FogOn()
    {
        return FogOn_;
    }

    bool DrawSystem::FogOn() const
    {
        return FogOn_;
    }

    bool& DrawSystem::BackdropOn()
    {
        return BackdropOn_;
    }

    bool DrawSystem::BackdropOn() const
    {
        return BackdropOn_;
    }

    int& DrawSystem::BackdropImageId()
    {
        return BackdropImageId_;
    }

    int DrawSystem::BackdropImageId() const
    {
        return BackdropImageId_;
    }

    float& DrawSystem::BackdropScrollX()
    {
        return BackdropScrollX_;
    }

    float DrawSystem::BackdropScrollX() const
    {
        return BackdropScrollX_;
    }

    float& DrawSystem::BackdropScrollY()
    {
        return BackdropScrollY_;
    }

    float DrawSystem::BackdropScrollY() const
    {
        return BackdropScrollY_;
    }

    void DrawSystem::SetDrawToFront()
    {
        DrawToFront_ = true;
    }

    void DrawSystem::SetDrawToBack()
    {
        DrawToFront_ = false;
    }

} // namespace dbc
