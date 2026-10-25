#pragma once

#include "graphics_2d.hpp"
#include "text.hpp"

#include "../../engine/systems/camera_system.hpp"
#include "../../engine/systems/draw_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"

namespace dbc
{
    // --- Backdrop ---

    inline bool ColorBackdrop(EngineKernel& K, int RgbPacked, std::string* Error = nullptr)
    {
        if (!K.Camera().SetBackdropColor(RgbPacked, Error))
            return false;
        K.Draw().SetBackdropColor(RgbPacked);
        return true;
    }

    inline bool ColorBackdrop(EngineKernel& K, int CameraId, int RgbPacked, std::string* Error = nullptr)
    {
        if (!K.Camera().SetBackdropColor(CameraId, RgbPacked, Error))
            return false;
        if (CameraId == K.Camera().CurrentCameraId())
            K.Draw().SetBackdropColor(RgbPacked);
        return true;
    }

    inline bool TextureBackdrop(EngineKernel& K, int ImageId, std::string* Error = nullptr)
    {
        if (!K.Draw().SetBackdropImage(ImageId, Error))
            return false;
        return true;
    }

    inline void ScrollBackdrop(EngineKernel& K, float U, float V)
    {
        K.Draw().DoScrollBackdrop(U, V);
    }

    inline bool BackdropOn(EngineKernel& K, std::string* Error = nullptr)
    {
        if (!K.Camera().EnableBackdrop(Error))
            return false;
        K.Draw().EnableBackdrop();
        return true;
    }

    inline bool BackdropOn(EngineKernel& K, int CameraId, std::string* Error = nullptr)
    {
        if (!K.Camera().EnableBackdrop(CameraId, Error))
            return false;
        if (CameraId == K.Camera().CurrentCameraId())
            K.Draw().EnableBackdrop();
        return true;
    }

    inline bool BackdropOff(EngineKernel& K, std::string* Error = nullptr)
    {
        if (!K.Camera().DisableBackdrop(Error))
            return false;
        K.Draw().DisableBackdrop();
        return true;
    }

    inline bool BackdropOff(EngineKernel& K, int CameraId, std::string* Error = nullptr)
    {
        if (!K.Camera().DisableBackdrop(CameraId, Error))
            return false;
        if (CameraId == K.Camera().CurrentCameraId())
            K.Draw().DisableBackdrop();
        return true;
    }

    inline bool ClearCameraView(EngineKernel& K, int Color, std::string* Error = nullptr)
    {
        if (!K.Camera().ClearCameraView(Color, Error))
            return false;
        return true;
    }

    inline bool ClearCameraView(EngineKernel& K, int CameraId, int Color, std::string* Error = nullptr)
    {
        if (!K.Camera().ClearCameraView(CameraId, Color, Error))
            return false;
        return true;
    }

    inline void ClearCameraViewRgb(EngineKernel& K, int CameraId, int R, int G, int B)
    {
        K.Draw().DoClearCameraViewRgb(CameraId, R, G, B);
    }

    inline void DrawToFront(EngineKernel& K)
    {
        K.Draw().SetDrawToFront();
    }

    inline void DrawToBack(EngineKernel& K)
    {
        K.Draw().SetDrawToBack();
    }

    // --- Fog ---

    inline int FogAvailable()
    {
        return DrawSystem::IsFogAvailable();
    }

    inline void FogOn(EngineKernel& K)
    {
        K.Draw().EnableFog();
    }

    inline void FogOff(EngineKernel& K)
    {
        K.Draw().DisableFog();
    }

    inline bool FogColor(EngineKernel& K, int RgbPacked, bool SyncBackdrop, std::string* Error = nullptr)
    {
        K.Draw().SetFogColor(RgbPacked, SyncBackdrop);
        if (SyncBackdrop)
            if (!K.Camera().SetBackdropColor(RgbPacked, Error))
                return false;
        return true;
    }

    inline void FogDistance(EngineKernel& K, float Distance)
    {
        K.Draw().SetFogDistance(Distance);
    }

    inline void FogDistance(EngineKernel& K, float StartDistance, float EndDistance)
    {
        K.Draw().SetFogDistance(StartDistance, EndDistance);
    }

} // namespace dbc
