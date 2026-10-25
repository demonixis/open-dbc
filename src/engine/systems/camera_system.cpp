#include "camera_system.hpp"
#include "../engine_math.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "object_manager.hpp"

#include <cassert>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dbc
{
    namespace
    {
        float WrapDegrees(float Value)
        {
            Value = std::fmod(Value, 360.0f);
            return Value < 0.0f ? Value + 360.0f : Value;
        }
    } // namespace

    CameraSystem::CameraSystem(EngineKernel& Kernel) : Kernel_(Kernel)
    {
        Cameras_.emplace(0, CameraState{});
    }

    CameraSystem::~CameraSystem() = default;

    CameraSystem::CameraState& CameraSystem::Current()
    {
        const auto It = Cameras_.find(CurrentCameraId_);
        assert(It != Cameras_.end());
        return It->second;
    }

    const CameraSystem::CameraState& CameraSystem::Current() const
    {
        const auto It = Cameras_.find(CurrentCameraId_);
        assert(It != Cameras_.end());
        return It->second;
    }

    CameraSystem::CameraState* CameraSystem::Require(int CameraId, const char* Command, std::string* Error)
    {
        const auto It = Cameras_.find(CameraId);
        if (It == Cameras_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing camera " + std::to_string(CameraId);
            return nullptr;
        }
        return &It->second;
    }

    const CameraSystem::CameraState* CameraSystem::Require(int CameraId, const char* Command, std::string* Error) const
    {
        const auto It = Cameras_.find(CameraId);
        if (It == Cameras_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing camera " + std::to_string(CameraId);
            return nullptr;
        }
        return &It->second;
    }

    bool CameraSystem::MakeCamera(int CameraId, std::string* Error)
    {
        if (CameraId <= 0 || CameraId > 65535)
        {
            if (Error)
                *Error = "MAKE CAMERA number must be between 1 and 65535";
            return false;
        }
        auto [It, Inserted] = Cameras_.emplace(CameraId, CameraState{});
        if (!Inserted)
        {
            if (Error)
                *Error = "MAKE CAMERA cannot replace camera " + std::to_string(CameraId);
            return false;
        }
        It->second.BackdropColor = 0x008000;
        return true;
    }

    bool CameraSystem::DeleteCamera(int CameraId, std::string* Error)
    {
        if (CameraId == 0)
        {
            if (Error)
                *Error = "DELETE CAMERA cannot delete camera zero";
            return false;
        }
        if (Cameras_.erase(CameraId) == 0)
        {
            if (Error)
                *Error = "DELETE CAMERA requires existing camera " + std::to_string(CameraId);
            return false;
        }
        if (CurrentCameraId_ == CameraId)
        {
            CurrentCameraId_ = 0;
            Current().Dirty = true;
        }
        return true;
    }

    bool CameraSystem::SetCurrentCamera(int CameraId, std::string* Error)
    {
        auto* Camera = Require(CameraId, "SET CURRENT CAMERA", Error);
        if (!Camera)
            return false;
        CurrentCameraId_ = CameraId;
        Current().Dirty = true;
        return true;
    }

    int CameraSystem::CameraExist(int CameraId) const
    {
        return Cameras_.contains(CameraId) ? 1 : 0;
    }

    bool CameraSystem::EnableBackdrop(std::string* Error)
    {
        if (!EnableBackdrop(CurrentCameraId_, Error))
            return false;
        return true;
    }

    bool CameraSystem::EnableBackdrop(int CameraId, std::string* Error)
    {
        auto* Camera = Require(CameraId, "BACKDROP ON", Error);
        if (!Camera)
            return false;
        Camera->BackdropEnabled = true;
        return true;
    }

    bool CameraSystem::DisableBackdrop(std::string* Error)
    {
        if (!DisableBackdrop(CurrentCameraId_, Error))
            return false;
        return true;
    }

    bool CameraSystem::DisableBackdrop(int CameraId, std::string* Error)
    {
        auto* Camera = Require(CameraId, "BACKDROP OFF", Error);
        if (!Camera)
            return false;
        Camera->BackdropEnabled = false;
        return true;
    }

    bool CameraSystem::SetBackdropColor(int Color, std::string* Error)
    {
        if (!SetBackdropColor(CurrentCameraId_, Color, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetBackdropColor(int CameraId, int Color, std::string* Error)
    {
        auto* Camera = Require(CameraId, "COLOR BACKDROP", Error);
        if (!Camera)
            return false;
        Camera->BackdropColor = Color;
        return true;
    }

    bool CameraSystem::BackdropEnabled() const
    {
        return Current().BackdropEnabled;
    }

    bool CameraSystem::BackdropEnabled(bool& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "BACKDROP", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->BackdropEnabled;
            return true;
        }
    }

    int CameraSystem::BackdropColor() const
    {
        return Current().BackdropColor;
    }

    bool CameraSystem::BackdropColor(int& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "COLOR BACKDROP", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->BackdropColor;
            return true;
        }
    }

    void CameraSystem::SetAutoCamOn()
    {
        AutoCamEnabled_ = true;
    }

    void CameraSystem::SetAutoCamOff()
    {
        AutoCamEnabled_ = false;
    }

    void CameraSystem::AutoPosition(float X, float Y, float Z, float Radius)
    {
        if (!AutoCamEnabled_)
            return;
        CameraState& Camera = Current();
        Camera.X = X;
        Camera.Y = Y + Radius * 0.5f;
        Camera.Z = std::min(Z - Radius * 3.0f, -2.0f);
        std::string Error;
        const bool Positioned = PointCamera(X, Y, Z, &Error);
        assert(Positioned);
        (void)Positioned;
    }

    // --- Position ---

    bool CameraSystem::PositionCamera(float X, float Y, float Z, std::string* Error)
    {
        if (!PositionCamera(CurrentCameraId_, X, Y, Z, Error))
            return false;
        return true;
    }

    bool CameraSystem::PositionCamera(int CameraId, float X, float Y, float Z, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "POSITION CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.X = X;
        Camera.Y = Y;
        Camera.Z = Z;
        Camera.Dirty = true;
        return true;
    }

    float CameraSystem::PositionX() const
    {
        return Current().X;
    }

    bool CameraSystem::PositionX(float& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "CAMERA POSITION X", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->X;
            return true;
        }
    }

    float CameraSystem::PositionY() const
    {
        return Current().Y;
    }

    bool CameraSystem::PositionY(float& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "CAMERA POSITION Y", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->Y;
            return true;
        }
    }

    float CameraSystem::PositionZ() const
    {
        return Current().Z;
    }

    bool CameraSystem::PositionZ(float& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "CAMERA POSITION Z", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->Z;
            return true;
        }
    }

    // --- Rotation ---

    bool CameraSystem::RotateCamera(float XDegrees, float YDegrees, float ZDegrees, std::string* Error)
    {
        if (!RotateCamera(CurrentCameraId_, XDegrees, YDegrees, ZDegrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::RotateCamera(int CameraId, float XDegrees, float YDegrees, float ZDegrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "ROTATE CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.PitchDeg = XDegrees;
        Camera.YawDeg = YDegrees;
        Camera.RollDeg = ZDegrees;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::XrotateCamera(float Degrees, std::string* Error)
    {
        if (!XrotateCamera(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::XrotateCamera(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "XROTATE CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.PitchDeg = Degrees;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::YrotateCamera(float Degrees, std::string* Error)
    {
        if (!YrotateCamera(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::YrotateCamera(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "YROTATE CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.YawDeg = Degrees;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::ZrotateCamera(float Degrees, std::string* Error)
    {
        if (!ZrotateCamera(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::ZrotateCamera(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "ZROTATE CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.RollDeg = Degrees;
        Camera.Dirty = true;
        return true;
    }

    float CameraSystem::AngleX() const
    {
        return Current().PitchDeg;
    }

    bool CameraSystem::AngleX(float& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "CAMERA ANGLE X", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->PitchDeg;
            return true;
        }
    }

    float CameraSystem::AngleY() const
    {
        return Current().YawDeg;
    }

    bool CameraSystem::AngleY(float& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "CAMERA ANGLE Y", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->YawDeg;
            return true;
        }
    }

    float CameraSystem::AngleZ() const
    {
        return Current().RollDeg;
    }

    bool CameraSystem::AngleZ(float& Output, int CameraId, std::string* Error) const
    {
        const auto* Camera = Require(CameraId, "CAMERA ANGLE Z", Error);
        if (!Camera)
            return false;
        {
            Output = Camera->RollDeg;
            return true;
        }
    }

    // --- Targeting ---

    bool CameraSystem::PointCamera(float X, float Y, float Z, std::string* Error)
    {
        if (!PointCamera(CurrentCameraId_, X, Y, Z, Error))
            return false;
        return true;
    }

    bool CameraSystem::PointCamera(int CameraId, float X, float Y, float Z, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "POINT CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        const float Dx = X - Camera.X;
        const float Dy = Y - Camera.Y;
        const float Dz = Z - Camera.Z;
        const float Flat = std::sqrt(Dx * Dx + Dz * Dz);
        Camera.YawDeg = static_cast<float>(std::atan2(Dx, Dz) * 180.0 / M_PI);
        if (Camera.YawDeg < 0.0f)
        {
            Camera.YawDeg += 360.0f;
        }
        Camera.PitchDeg = static_cast<float>(-std::atan2(Dy, std::max(0.0001f, Flat)) * 180.0 / M_PI);
        Camera.RollDeg = 0.0f;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::SetCameraToFollow(float TargetX, float TargetY, float TargetZ, float AngleDeg, float Distance,
                                         float Height, float Smooth, int CollisionFlag, std::string* Error)
    {
        if (!SetCameraToFollow(CurrentCameraId_, TargetX, TargetY, TargetZ, AngleDeg, Distance, Height, Smooth,
                               CollisionFlag, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetCameraToFollow(int CameraId, float TargetX, float TargetY, float TargetZ, float AngleDeg,
                                         float Distance, float Height, float Smooth, int CollisionFlag,
                                         std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA TO FOLLOW", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        if (CollisionFlag != 0 && CollisionFlag != 1)
        {
            if (Error)
                *Error = "SET CAMERA TO FOLLOW collision flag must be zero or one";
            return false;
        }
        const float Rad = DegToRad(AngleDeg);

        // Desired camera position: behind the target at the given angle/distance/height.
        const float DesiredX = TargetX - std::sin(Rad) * Distance;
        const float DesiredY = TargetY + Height;
        const float DesiredZ = TargetZ - std::cos(Rad) * Distance;

        // Smooth interpolation (1.0 = instant, higher = more lag).
        const float Factor = std::max(1.0f, Smooth);
        Camera.X += (DesiredX - Camera.X) / Factor;
        Camera.Y += (DesiredY - Camera.Y) / Factor;
        Camera.Z += (DesiredZ - Camera.Z) / Factor;

        // Optional static collision: ray from target to camera.
        if (CollisionFlag == 1)
        {
            const float Hit =
                Kernel_.Objects().GetStaticCollisionHit(TargetX, TargetY, TargetZ, Camera.X, Camera.Y, Camera.Z);
            if (Hit > 0.0f)
            {
                Camera.X = Kernel_.Objects().GetStaticCollisionX();
                Camera.Y = Kernel_.Objects().GetStaticCollisionY();
                Camera.Z = Kernel_.Objects().GetStaticCollisionZ();
            }
        }

        if (!PointCamera(CameraId, TargetX, TargetY, TargetZ, Error))
            return false;
        return true;
    }

    // --- Movement ---

    bool CameraSystem::MoveCamera(float Step, std::string* Error)
    {
        if (!MoveCamera(CurrentCameraId_, Step, Error))
            return false;
        return true;
    }

    bool CameraSystem::MoveCamera(int CameraId, float Step, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "MOVE CAMERA", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        const float Yaw = DegToRad(Camera.YawDeg);
        const float Pitch = DegToRad(Camera.PitchDeg);
        const float Cp = std::cos(Pitch);
        Camera.X += std::sin(Yaw) * Cp * Step;
        Camera.Y -= std::sin(Pitch) * Step;
        Camera.Z += std::cos(Yaw) * Cp * Step;
        Camera.Dirty = true;
        return true;
    }

    void CameraSystem::MoveCameraUp(float Step)
    {
        CameraState& Camera = Current();
        Camera.Y += Step;
        Camera.Dirty = true;
    }

    void CameraSystem::MoveCameraDown(float Step)
    {
        CameraState& Camera = Current();
        Camera.Y -= Step;
        Camera.Dirty = true;
    }

    bool CameraSystem::TurnCameraLeft(float Degrees, std::string* Error)
    {
        if (!TurnCameraLeft(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::TurnCameraLeft(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "TURN CAMERA LEFT", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.YawDeg = WrapDegrees(Camera.YawDeg - Degrees);
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::TurnCameraRight(float Degrees, std::string* Error)
    {
        if (!TurnCameraRight(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::TurnCameraRight(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "TURN CAMERA RIGHT", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.YawDeg = WrapDegrees(Camera.YawDeg + Degrees);
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::PitchCameraUp(float Degrees, std::string* Error)
    {
        if (!PitchCameraUp(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::PitchCameraUp(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "PITCH CAMERA UP", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.PitchDeg -= Degrees;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::PitchCameraDown(float Degrees, std::string* Error)
    {
        if (!PitchCameraDown(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::PitchCameraDown(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "PITCH CAMERA DOWN", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.PitchDeg += Degrees;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::RollCameraLeft(float Degrees, std::string* Error)
    {
        if (!RollCameraLeft(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::RollCameraLeft(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "ROLL CAMERA LEFT", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.RollDeg = WrapDegrees(Camera.RollDeg - Degrees);
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::RollCameraRight(float Degrees, std::string* Error)
    {
        if (!RollCameraRight(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::RollCameraRight(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "ROLL CAMERA RIGHT", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.RollDeg = WrapDegrees(Camera.RollDeg + Degrees);
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::ControlCameraUsingArrowKeys(int CameraId, float Velocity, float TurnSpeed, bool Up, bool Down,
                                                   bool Left, bool Right, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "CONTROL CAMERA USING ARROWKEYS", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        if (Up)
            if (!MoveCamera(CameraId, Velocity, Error))
                return false;
        if (Down)
            if (!MoveCamera(CameraId, -Velocity, Error))
                return false;
        if (Left)
            Camera.YawDeg = WrapDegrees(Camera.YawDeg - TurnSpeed);
        if (Right)
            Camera.YawDeg = WrapDegrees(Camera.YawDeg + TurnSpeed);
        Camera.PitchDeg = 0.0f;
        Camera.RollDeg = 0.0f;
        Camera.Dirty = true;
        return true;
    }

    // --- Projection ---

    bool CameraSystem::SetCameraFov(float Degrees, std::string* Error)
    {
        if (!SetCameraFov(CurrentCameraId_, Degrees, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetCameraFov(int CameraId, float Degrees, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA FOV", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.FovDeg = std::clamp(Degrees, 10.0f, 170.0f);
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::SetCameraRange(float NearPlane, float FarPlane, std::string* Error)
    {
        if (!SetCameraRange(CurrentCameraId_, NearPlane, FarPlane, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetCameraRange(int CameraId, float NearPlane, float FarPlane, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA RANGE", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.NearPlane = std::max(0.1f, NearPlane);
        Camera.FarPlane = std::max(Camera.NearPlane + 1.0f, FarPlane);
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::SetCameraAspect(float Aspect, std::string* Error)
    {
        if (!SetCameraAspect(CurrentCameraId_, Aspect, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetCameraAspect(int CameraId, float Aspect, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA ASPECT", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.Aspect = std::max(0.0001f, Aspect);
        Camera.Dirty = true;
        return true;
    }

    float CameraSystem::Fov() const
    {
        return Current().FovDeg;
    }

    float CameraSystem::NearPlane() const
    {
        return Current().NearPlane;
    }

    float CameraSystem::FarPlane() const
    {
        return Current().FarPlane;
    }

    float CameraSystem::Aspect() const
    {
        return Current().Aspect > 0.0f ? Current().Aspect : Kernel_.Core().CameraAspect();
    }

    float CameraSystem::ClampedNearPlane() const
    {
        return std::max(0.1f, Current().NearPlane);
    }

    float CameraSystem::ClampedFarPlane() const
    {
        const float N = ClampedNearPlane();
        return std::max(N + 0.01f, Current().FarPlane);
    }

    // --- Viewport ---

    bool CameraSystem::SetCameraView(int Left, int Top, int Right, int Bottom, std::string* Error)
    {
        if (!SetCameraView(CurrentCameraId_, Left, Top, Right, Bottom, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetCameraView(int CameraId, int Left, int Top, int Right, int Bottom, std::string* Error)
    {
        if (Left == Right || Top == Bottom)
        {
            if (Error)
                *Error = "SET CAMERA VIEW requires a positive rectangle";
            return false;
        }
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA VIEW", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.ViewLeft = std::min(Left, Right);
        Camera.ViewTop = std::min(Top, Bottom);
        Camera.ViewRight = std::max(Left, Right);
        Camera.ViewBottom = std::max(Top, Bottom);
        Camera.HasView = true;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::ClearCameraView(int Color, std::string* Error)
    {
        if (!ClearCameraView(CurrentCameraId_, Color, Error))
            return false;
        return true;
    }

    bool CameraSystem::ClearCameraView(int CameraId, int Color, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "CLEAR CAMERA VIEW", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.ClearColor = Color;
        Camera.ClearRequested = true;
        return true;
    }

    bool CameraSystem::SetRotationXyz(std::string* Error)
    {
        if (!SetRotationXyz(CurrentCameraId_, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetRotationXyz(int CameraId, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA ROTATION XYZ", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.UseZyxRotation = false;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::SetRotationZyx(std::string* Error)
    {
        if (!SetRotationZyx(CurrentCameraId_, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetRotationZyx(int CameraId, std::string* Error)
    {
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA ROTATION ZYX", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.UseZyxRotation = true;
        Camera.Dirty = true;
        return true;
    }

    bool CameraSystem::SetCameraToObjectOrientation(int ObjectId, std::string* Error)
    {
        if (!SetCameraToObjectOrientation(CurrentCameraId_, ObjectId, Error))
            return false;
        return true;
    }

    bool CameraSystem::SetCameraToObjectOrientation(int CameraId, int ObjectId, std::string* Error)
    {
        const Object3D* Obj = Kernel_.Objects().GetObject(ObjectId);
        if (!Obj)
        {
            if (Error)
                *Error = "SET CAMERA TO OBJECT ORIENTATION requires existing object " + std::to_string(ObjectId);
            return false;
        }
        CameraState* CameraPointer = Require(CameraId, "SET CAMERA TO OBJECT ORIENTATION", Error);
        if (!CameraPointer)
            return false;
        CameraState& Camera = *CameraPointer;
        Camera.PitchDeg = Obj->RotX;
        Camera.YawDeg = Obj->RotY;
        Camera.RollDeg = Obj->RotZ;
        Camera.Dirty = true;
        return true;
    }

} // namespace dbc
