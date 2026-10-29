#pragma once
#include <string>

#include <vector>

#include "../engine_types.hpp"

namespace dbc
{
    class EngineKernel;

    class DrawSystem
    {
      public:
        explicit DrawSystem(EngineKernel& Kernel);
        ~DrawSystem();

        // --- Color packing (pure function) ---

        static int PackRgb(int R, int G, int B);

        // --- 2D drawing primitives ---

        void SetInk(int ForeRgbPacked, int BackRgbPacked = -1);
        bool DrawDot(int X, int Y, std::string* Error = nullptr);
        bool DrawDot(int X, int Y, int Color, std::string* Error = nullptr);
        bool DrawLine(int X1, int Y1, int X2, int Y2, std::string* Error = nullptr);
        bool DrawBox(int Left, int Top, int Right, int Bottom, std::string* Error = nullptr);
        bool DrawBox(int Left, int Top, int Right, int Bottom, int TopLeft, int TopRight, int BottomRight,
                     int BottomLeft, std::string* Error = nullptr);
        bool DrawCircle(int X, int Y, int Radius, std::string* Error = nullptr);
        bool DrawEllipse(int X, int Y, int XRadius, int YRadius, std::string* Error = nullptr);
        bool ReadPoint(int& Output, int X, int Y, std::string* Error = nullptr);
        bool ClearScreen(int RgbPacked, std::string* Error = nullptr);

        // --- Fog ---

        static int IsFogAvailable();
        void EnableFog();
        void DisableFog();
        void SetFogColor(int RgbPacked, bool SyncBackdrop = true);
        void SetFogDistance(float Distance);
        void SetFogDistance(float StartDistance, float EndDistance);

        // --- Backdrop ---

        void EnableBackdrop();
        void DisableBackdrop();
        void SetBackdropColor(int RgbPacked);
        bool SetBackdropImage(int ImageId, std::string* Error = nullptr);
        void DoScrollBackdrop(float U, float V);
        void DoClearCameraView();
        void DoClearCameraViewRgb(int CameraId, int R, int G, int B);

        // --- Renderer iteration ---

        template <typename Fn> void ForEachPrimitive2D(Fn&& FnValue) const
        {
            for (const auto& P : Primitives2d_)
                FnValue(P);
        }

        void ClearPrimitives2D();

        int& BackdropColor();
        int BackdropColor() const;

        int& FogColor();
        int FogColor() const;

        float& FogDistance();
        float FogDistance() const;

        float& FogStartDistance();
        float FogStartDistance() const;

        bool& FogOn();
        bool FogOn() const;

        bool& BackdropOn();
        bool BackdropOn() const;

        int& BackdropImageId();
        int BackdropImageId() const;

        float& BackdropScrollX();
        float BackdropScrollX() const;

        float& BackdropScrollY();
        float BackdropScrollY() const;

        // --- Draw order ---
        void SetDrawToFront();
        void SetDrawToBack();

        bool IsDrawToFront() const
        {
            return DrawToFront_;
        }

      private:
        EngineKernel& Kernel_;
        std::vector<Primitive2D> Primitives2d_;
        int BackdropColor_ = 0;
        int FogColor_ = 0;
        float FogStartDistance_ = 0.0f;
        float FogDistance_ = 3000.0f;
        bool FogOn_ = false;
        bool BackdropOn_ = true;
        int BackdropImageId_ = 0;
        float BackdropScrollX_ = 0.0f;
        float BackdropScrollY_ = 0.0f;
        bool DrawToFront_ = true;
    };

} // namespace dbc
