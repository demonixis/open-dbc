#pragma once

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string>

struct SDL_GPUDevice;
struct SDL_Surface;

namespace dbc
{
    class EngineKernel;
    struct VrSystemTestAccess;

    enum class VrHand
    {
        Left = 0,
        Right = 1
    };
    enum class VrTrackingSpace
    {
        Local = 0,
        LocalFloor = 1,
        Stage = 2
    };
    enum class VrPreviewMode
    {
        Both = 0,
        Left = 1,
        Right = 2
    };

    struct VrPose
    {
        glm::vec3 Position{};
        glm::vec3 Rotation{};
        bool Tracked = false;
    };

    // Physical poses are cached in tracking coordinates, independently of locomotion.
    struct VrTracking
    {
        VrTrackingSpace Space = VrTrackingSpace::Local;
        float Height = 0.0f;
        glm::mat4 Recenter{1.0f};
        std::array<glm::mat4, 3> Poses{glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        std::array<bool, 3> Valid{};
        std::array<bool, 3> Tracked{};

        void Update(std::size_t Index, const glm::mat4& Pose, bool IsTracked);
        bool RecenterHead();
        glm::mat4 Origin() const;
        VrPose Compose(std::size_t Index, const glm::mat4& Player) const;
        static glm::mat4 ConvertPose(const glm::vec3& Position, const glm::vec4& Quaternion);
        static glm::mat4 Projection(float Left, float Right, float Up, float Down, float Near, float Far);
        static glm::vec4 Letterbox(float X, float Y, float Width, float Height, float ImageWidth, float ImageHeight);
    };

    class VrSystem
    {
      public:
        explicit VrSystem(EngineKernel& Kernel);
        ~VrSystem();
        VrSystem(const VrSystem&) = delete;
        VrSystem& operator=(const VrSystem&) = delete;

        bool Configure(bool VrCapable, std::string* Error = nullptr);
        bool Capable() const;
        SDL_GPUDevice* CreateDevice();
        void DetachDevice();
        bool SetEnabled(int Mode, std::string* Error = nullptr);
        bool SetTrackingSpace(int Mode, std::string* Error = nullptr);
        bool SetPreviewMode(int Mode, std::string* Error = nullptr);
        bool SetCameraHeight(float Height, std::string* Error = nullptr);
        void RecenterCamera();
        float CameraHeight() const;
        bool Supported() const;
        bool Enabled() const;
        bool Pacing() const;
        VrPose HeadPose() const;
        bool ControllerPose(int Hand, VrPose& Pose, std::string* Error = nullptr) const;
        // True means XR owns this presentation, including a valid empty XR frame.
        bool RenderFrame(double DeltaSeconds, bool Retain2d);
        SDL_Surface* CapturePreview();

      private:
        friend struct VrSystemTestAccess;
        struct Impl;
        std::unique_ptr<Impl> Impl_;
    };
} // namespace dbc
