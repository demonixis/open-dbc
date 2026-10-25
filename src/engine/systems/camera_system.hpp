#pragma once
#include <string>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace dbc
{
    class EngineKernel;

    class CameraSystem
    {
      public:
        explicit CameraSystem(EngineKernel& Kernel);
        ~CameraSystem();

        bool MakeCamera(int CameraId, std::string* Error = nullptr);
        bool DeleteCamera(int CameraId, std::string* Error = nullptr);
        bool SetCurrentCamera(int CameraId, std::string* Error = nullptr);
        int CameraExist(int CameraId) const;
        int CurrentCameraId() const
        {
            return CurrentCameraId_;
        }
        bool EnableBackdrop(std::string* Error = nullptr);
        bool EnableBackdrop(int CameraId, std::string* Error = nullptr);
        bool DisableBackdrop(std::string* Error = nullptr);
        bool DisableBackdrop(int CameraId, std::string* Error = nullptr);
        bool SetBackdropColor(int Color, std::string* Error = nullptr);
        bool SetBackdropColor(int CameraId, int Color, std::string* Error = nullptr);
        bool BackdropEnabled() const;
        bool BackdropEnabled(bool& Output, int CameraId, std::string* Error = nullptr) const;
        int BackdropColor() const;
        bool BackdropColor(int& Output, int CameraId, std::string* Error = nullptr) const;

        void SetAutoCamOn();
        void SetAutoCamOff();
        bool AutoCamEnabled() const
        {
            return AutoCamEnabled_;
        }
        void AutoPosition(float X, float Y, float Z, float Radius);

        // Position
        bool PositionCamera(float X, float Y, float Z, std::string* Error = nullptr);
        bool PositionCamera(int CameraId, float X, float Y, float Z, std::string* Error = nullptr);
        float PositionX() const;
        bool PositionX(float& Output, int CameraId, std::string* Error = nullptr) const;
        float PositionY() const;
        bool PositionY(float& Output, int CameraId, std::string* Error = nullptr) const;
        float PositionZ() const;
        bool PositionZ(float& Output, int CameraId, std::string* Error = nullptr) const;

        // Rotation
        bool RotateCamera(float XDegrees, float YDegrees, float ZDegrees, std::string* Error = nullptr);
        bool RotateCamera(int CameraId, float XDegrees, float YDegrees, float ZDegrees, std::string* Error = nullptr);
        bool XrotateCamera(float Degrees, std::string* Error = nullptr);
        bool XrotateCamera(int CameraId, float Degrees, std::string* Error = nullptr);
        bool YrotateCamera(float Degrees, std::string* Error = nullptr);
        bool YrotateCamera(int CameraId, float Degrees, std::string* Error = nullptr);
        bool ZrotateCamera(float Degrees, std::string* Error = nullptr);
        bool ZrotateCamera(int CameraId, float Degrees, std::string* Error = nullptr);
        float AngleX() const;
        bool AngleX(float& Output, int CameraId, std::string* Error = nullptr) const;
        float AngleY() const;
        bool AngleY(float& Output, int CameraId, std::string* Error = nullptr) const;
        float AngleZ() const;
        bool AngleZ(float& Output, int CameraId, std::string* Error = nullptr) const;

        // Targeting
        bool PointCamera(float X, float Y, float Z, std::string* Error = nullptr);
        bool PointCamera(int CameraId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SetCameraToFollow(float TargetX, float TargetY, float TargetZ, float AngleDeg, float Distance,
                               float Height, float Smooth, int CollisionFlag, std::string* Error = nullptr);
        bool SetCameraToFollow(int CameraId, float TargetX, float TargetY, float TargetZ, float AngleDeg,
                               float Distance, float Height, float Smooth, int CollisionFlag,
                               std::string* Error = nullptr);

        // Movement
        bool MoveCamera(float Step, std::string* Error = nullptr);
        bool MoveCamera(int CameraId, float Step, std::string* Error = nullptr);
        void MoveCameraUp(float Step);
        void MoveCameraDown(float Step);
        bool TurnCameraLeft(float Degrees, std::string* Error = nullptr);
        bool TurnCameraLeft(int CameraId, float Degrees, std::string* Error = nullptr);
        bool TurnCameraRight(float Degrees, std::string* Error = nullptr);
        bool TurnCameraRight(int CameraId, float Degrees, std::string* Error = nullptr);
        bool PitchCameraUp(float Degrees, std::string* Error = nullptr);
        bool PitchCameraUp(int CameraId, float Degrees, std::string* Error = nullptr);
        bool PitchCameraDown(float Degrees, std::string* Error = nullptr);
        bool PitchCameraDown(int CameraId, float Degrees, std::string* Error = nullptr);
        bool RollCameraLeft(float Degrees, std::string* Error = nullptr);
        bool RollCameraLeft(int CameraId, float Degrees, std::string* Error = nullptr);
        bool RollCameraRight(float Degrees, std::string* Error = nullptr);
        bool RollCameraRight(int CameraId, float Degrees, std::string* Error = nullptr);
        bool ControlCameraUsingArrowKeys(int CameraId, float Velocity, float TurnSpeed, bool Up, bool Down, bool Left,
                                         bool Right, std::string* Error = nullptr);

        // Projection
        bool SetCameraFov(float Degrees, std::string* Error = nullptr);
        bool SetCameraFov(int CameraId, float Degrees, std::string* Error = nullptr);
        bool SetCameraRange(float NearPlane, float FarPlane, std::string* Error = nullptr);
        bool SetCameraRange(int CameraId, float NearPlane, float FarPlane, std::string* Error = nullptr);
        bool SetCameraAspect(float Aspect, std::string* Error = nullptr);
        bool SetCameraAspect(int CameraId, float Aspect, std::string* Error = nullptr);
        float Fov() const;
        float NearPlane() const;
        float FarPlane() const;
        float Aspect() const;

        // Clamped accessors used by renderer pipeline (safe floor values).
        float ClampedNearPlane() const;
        float ClampedFarPlane() const;

        // Viewport (SET CAMERA VIEW)
        bool SetCameraView(int Left, int Top, int Right, int Bottom, std::string* Error = nullptr);
        bool SetCameraView(int CameraId, int Left, int Top, int Right, int Bottom, std::string* Error = nullptr);
        bool ClearCameraView(int Color, std::string* Error = nullptr);
        bool ClearCameraView(int CameraId, int Color, std::string* Error = nullptr);
        bool ClearRequested() const
        {
            return Current().ClearRequested;
        }
        int ClearColor() const
        {
            return Current().ClearColor;
        }
        void ConsumeClearRequest()
        {
            Current().ClearRequested = false;
        }
        bool HasCameraView() const
        {
            return Current().HasView;
        }
        int ViewLeft() const
        {
            return Current().ViewLeft;
        }
        int ViewTop() const
        {
            return Current().ViewTop;
        }
        int ViewRight() const
        {
            return Current().ViewRight;
        }
        int ViewBottom() const
        {
            return Current().ViewBottom;
        }

        // Rotation order
        bool SetRotationXyz(std::string* Error = nullptr);
        bool SetRotationXyz(int CameraId, std::string* Error = nullptr);
        bool SetRotationZyx(std::string* Error = nullptr);
        bool SetRotationZyx(int CameraId, std::string* Error = nullptr);
        [[nodiscard]] bool IsZyxRotation() const
        {
            return Current().UseZyxRotation;
        }

        // Copy orientation from an object
        bool SetCameraToObjectOrientation(int ObjectId, std::string* Error = nullptr);
        bool SetCameraToObjectOrientation(int CameraId, int ObjectId, std::string* Error = nullptr);

        // Dirty flag — set on any position/rotation/fov/range change,
        // cleared by the renderer after processing.
        [[nodiscard]] bool IsDirty() const
        {
            return Current().Dirty;
        }
        void ClearDirty()
        {
            Current().Dirty = false;
        }

      private:
        struct CameraState
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
            float PitchDeg = 0.0f;
            float YawDeg = 0.0f;
            float RollDeg = 0.0f;
            float FovDeg = 75.0f;
            float NearPlane = 0.1f;
            float FarPlane = 5000.0f;
            float Aspect = 0.0f;
            bool UseZyxRotation = false;
            bool Dirty = true;
            bool HasView = false;
            int ViewLeft = 0;
            int ViewTop = 0;
            int ViewRight = 0;
            int ViewBottom = 0;
            bool ClearRequested = false;
            int ClearColor = 0;
            bool BackdropEnabled = true;
            int BackdropColor = 0;
        };

        CameraState& Current();
        const CameraState& Current() const;
        CameraState* Require(int CameraId, const char* Command, std::string* Error = nullptr);
        const CameraState* Require(int CameraId, const char* Command, std::string* Error = nullptr) const;

        EngineKernel& Kernel_;
        std::unordered_map<int, CameraState> Cameras_;
        int CurrentCameraId_ = 0;
        bool AutoCamEnabled_ = true;
    };

} // namespace dbc
