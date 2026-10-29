#pragma once

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/light_manager.hpp"
#include "graphics_2d.hpp" // for Rgb()

namespace dbc
{
    inline void SetAmbientLight(EngineKernel& K, int Value)
    {
        K.Lights().SetAmbientLight(Value);
    }

    inline void ColorAmbientLight(EngineKernel& K, int RgbPacked)
    {
        K.Lights().ColorAmbientLight(RgbPacked);
    }

    inline bool MakeLight(EngineKernel& K, int LightId, std::string* Error = nullptr)
    {
        return K.Lights().MakeLight(LightId, Error);
    }

    inline bool DeleteLight(EngineKernel& K, int LightId, std::string* Error = nullptr)
    {
        return K.Lights().DeleteLight(LightId, Error);
    }

    inline bool ShowLight(EngineKernel& K, int LightId, std::string* Error = nullptr)
    {
        return K.Lights().ShowLight(LightId, Error);
    }

    inline bool HideLight(EngineKernel& K, int LightId, std::string* Error = nullptr)
    {
        return K.Lights().HideLight(LightId, Error);
    }

    inline int LightExist(EngineKernel& K, int LightId)
    {
        return K.Lights().LightExist(LightId);
    }

    inline int LightVisible(EngineKernel& K, int LightId)
    {
        return K.Lights().LightVisible(LightId);
    }

    inline int LightType(EngineKernel& K, int LightId)
    {
        return K.Lights().LightType(LightId);
    }

    inline bool ColorLight(EngineKernel& K, int LightId, int RgbPacked, std::string* Error = nullptr)
    {
        return K.Lights().ColorLight(LightId, RgbPacked, Error);
    }

    inline bool ColorLight(EngineKernel& K, int LightId, int Red, int Green, int Blue, std::string* Error = nullptr)
    {
        return K.Lights().ColorLight(LightId, Rgb(Red, Green, Blue), Error);
    }

    inline bool PositionLight(EngineKernel& K, int LightId, float X, float Y, float Z, std::string* Error = nullptr)
    {
        return K.Lights().PositionLight(LightId, X, Y, Z, Error);
    }

    inline bool RotateLight(EngineKernel& K, int LightId, float XDegrees, float YDegrees, float ZDegrees,
                            std::string* Error = nullptr)
    {
        return K.Lights().RotateLight(LightId, XDegrees, YDegrees, ZDegrees, Error);
    }

    inline bool PointLight(EngineKernel& K, int LightId, float X, float Y, float Z, std::string* Error = nullptr)
    {
        return K.Lights().PointLight(LightId, X, Y, Z, Error);
    }

    inline bool SetDirectionalLight(EngineKernel& K, int LightId, float X, float Y, float Z,
                                    std::string* Error = nullptr)
    {
        return K.Lights().SetDirectionalLight(LightId, X, Y, Z, Error);
    }

    inline bool SetPointLight(EngineKernel& K, int LightId, float X, float Y, float Z, std::string* Error = nullptr)
    {
        return K.Lights().SetPointLight(LightId, X, Y, Z, Error);
    }

    inline bool SetSpotLight(EngineKernel& K, int LightId, float InnerAngle, float OuterAngle,
                             std::string* Error = nullptr)
    {
        return K.Lights().SetSpotLight(LightId, InnerAngle, OuterAngle, Error);
    }

    inline bool SetLightRange(EngineKernel& K, int LightId, float Range, std::string* Error = nullptr)
    {
        return K.Lights().SetLightRange(LightId, Range, Error);
    }

    inline float LightPositionX(EngineKernel& K, int LightId)
    {
        return K.Lights().LightPositionX(LightId);
    }

    inline float LightPositionY(EngineKernel& K, int LightId)
    {
        return K.Lights().LightPositionY(LightId);
    }

    inline float LightPositionZ(EngineKernel& K, int LightId)
    {
        return K.Lights().LightPositionZ(LightId);
    }

    inline float LightDirectionX(EngineKernel& K, int LightId)
    {
        return K.Lights().LightDirectionX(LightId);
    }

    inline float LightDirectionY(EngineKernel& K, int LightId)
    {
        return K.Lights().LightDirectionY(LightId);
    }

    inline float LightDirectionZ(EngineKernel& K, int LightId)
    {
        return K.Lights().LightDirectionZ(LightId);
    }

    inline float LightRange(EngineKernel& K, int LightId)
    {
        return K.Lights().LightRange(LightId);
    }

    inline bool SetLightToObjectPosition(EngineKernel& K, int LightId, int ObjectId, std::string* Error = nullptr)
    {
        return K.Lights().SetLightToObjectPosition(LightId, ObjectId, Error);
    }

    inline bool SetLightToObjectOrientation(EngineKernel& K, int LightId, int ObjectId, std::string* Error = nullptr)
    {
        return K.Lights().SetLightToObjectOrientation(LightId, ObjectId, Error);
    }

} // namespace dbc
