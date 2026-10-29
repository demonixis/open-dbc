#pragma once

#include "../../engine/systems/camera_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"
#include "math.hpp"

namespace dbc
{
    // Math utilities (have real logic, implemented in core_system.cpp)
    float Curvevalue(EngineKernel& K, float Target, float Current, float Speed);
    float Wrapvalue(EngineKernel& K, float Value);
    float Newxvalue(EngineKernel& K, float X, float AngleDegrees, float Step);
    float Newyvalue(EngineKernel& K, float Y, float AngleDegrees, float Step);
    float Newzvalue(EngineKernel& K, float Z, float AngleDegrees, float Step);

    // Camera wrappers (inline to CameraSystem)
    inline bool PositionCamera(EngineKernel& K, float X, float Y, float Z, std::string* Error = nullptr)
    {
        if (!K.Camera().PositionCamera(X, Y, Z, Error))
            return false;
        return true;
    }

    inline bool XrotateCamera(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().XrotateCamera(Degrees, Error))
            return false;
        return true;
    }

    inline bool YrotateCamera(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().YrotateCamera(Degrees, Error))
            return false;
        return true;
    }

    inline bool ZrotateCamera(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().ZrotateCamera(Degrees, Error))
            return false;
        return true;
    }

    inline bool RotateCamera(EngineKernel& K, float XDegrees, float YDegrees, float ZDegrees,
                             std::string* Error = nullptr)
    {
        if (!K.Camera().RotateCamera(XDegrees, YDegrees, ZDegrees, Error))
            return false;
        return true;
    }

    inline bool PointCamera(EngineKernel& K, float X, float Y, float Z, std::string* Error = nullptr)
    {
        if (!K.Camera().PointCamera(X, Y, Z, Error))
            return false;
        return true;
    }

    inline bool SetCameraFov(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().SetCameraFov(Degrees, Error))
            return false;
        return true;
    }

    inline bool SetCameraRange(EngineKernel& K, float NearPlane, float FarPlane, std::string* Error = nullptr)
    {
        if (!K.Camera().SetCameraRange(NearPlane, FarPlane, Error))
            return false;
        return true;
    }

    inline bool SetCameraAspect(EngineKernel& K, float Aspect, std::string* Error = nullptr)
    {
        if (!K.Camera().SetCameraAspect(Aspect, Error))
            return false;
        return true;
    }

    inline bool MoveCamera(EngineKernel& K, float Step, std::string* Error = nullptr)
    {
        if (!K.Camera().MoveCamera(Step, Error))
            return false;
        return true;
    }

    inline void MoveCameraUp(EngineKernel& K, float Step)
    {
        K.Camera().MoveCameraUp(Step);
    }

    inline void MoveCameraDown(EngineKernel& K, float Step)
    {
        K.Camera().MoveCameraDown(Step);
    }

    inline bool TurnCameraLeft(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().TurnCameraLeft(Degrees, Error))
            return false;
        return true;
    }

    inline bool TurnCameraRight(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().TurnCameraRight(Degrees, Error))
            return false;
        return true;
    }

    inline bool PitchCameraUp(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().PitchCameraUp(Degrees, Error))
            return false;
        return true;
    }

    inline bool PitchCameraDown(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().PitchCameraDown(Degrees, Error))
            return false;
        return true;
    }

    inline bool RollCameraLeft(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().RollCameraLeft(Degrees, Error))
            return false;
        return true;
    }

    inline bool RollCameraRight(EngineKernel& K, float Degrees, std::string* Error = nullptr)
    {
        if (!K.Camera().RollCameraRight(Degrees, Error))
            return false;
        return true;
    }

    inline float CameraPositionX(EngineKernel& K)
    {
        return K.Camera().PositionX();
    }

    inline float CameraPositionY(EngineKernel& K)
    {
        return K.Camera().PositionY();
    }

    inline float CameraPositionZ(EngineKernel& K)
    {
        return K.Camera().PositionZ();
    }

    inline float CameraAngleX(EngineKernel& K)
    {
        return K.Camera().AngleX();
    }

    inline float CameraAngleY(EngineKernel& K)
    {
        return K.Camera().AngleY();
    }

    inline float CameraAngleZ(EngineKernel& K)
    {
        return K.Camera().AngleZ();
    }

    inline bool SetCameraToFollow(EngineKernel& K, float Tx, float Ty, float Tz, float Angle, float Dist, float Height,
                                  float Smooth, int ColFlag, std::string* Error = nullptr)
    {
        if (!K.Camera().SetCameraToFollow(Tx, Ty, Tz, Angle, Dist, Height, Smooth, ColFlag, Error))
            return false;
        return true;
    }

    inline bool SetCameraView(EngineKernel& K, int Left, int Top, int Right, int Bottom, std::string* Error = nullptr)
    {
        if (!K.Camera().SetCameraView(Left, Top, Right, Bottom, Error))
            return false;
        return true;
    }

    inline bool SetCameraRotationXyz(EngineKernel& K, std::string* Error = nullptr)
    {
        if (!K.Camera().SetRotationXyz(Error))
            return false;
        return true;
    }

    inline bool SetCameraRotationZyx(EngineKernel& K, std::string* Error = nullptr)
    {
        if (!K.Camera().SetRotationZyx(Error))
            return false;
        return true;
    }

    inline bool SetCameraToObjectOrientation(EngineKernel& K, int ObjectId, std::string* Error = nullptr)
    {
        if (!K.Camera().SetCameraToObjectOrientation(ObjectId, Error))
            return false;
        return true;
    }

} // namespace dbc
