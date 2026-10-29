#pragma once

#include "renderer_system.hpp"
#include "vr_presenter.hpp"
#include "vr_system.hpp"

#if defined(OPENDBC_OPENXR)
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>

// SDL must see the complete OpenXR types before declaring its interop functions.
#include <SDL3/SDL_openxr.h>

// clang-format off
#define DBC_XR_FUNCTIONS(F) \
    F(xrGetSystemProperties) \
    F(xrEnumerateViewConfigurationViews) \
    F(xrEnumerateEnvironmentBlendModes) \
    F(xrEnumerateReferenceSpaces) \
    F(xrCreateReferenceSpace) \
    F(xrDestroySpace) \
    F(xrLocateSpace) \
    F(xrPollEvent) \
    F(xrBeginSession) \
    F(xrEndSession) \
    F(xrDestroySession) \
    F(xrWaitFrame) \
    F(xrBeginFrame) \
    F(xrEndFrame) \
    F(xrLocateViews) \
    F(xrAcquireSwapchainImage) \
    F(xrWaitSwapchainImage) \
    F(xrReleaseSwapchainImage) \
    F(xrEnumerateSwapchainImages) \
    F(xrStringToPath) \
    F(xrCreateActionSet) \
    F(xrDestroyActionSet) \
    F(xrCreateAction) \
    F(xrSuggestInteractionProfileBindings) \
    F(xrAttachSessionActionSets) \
    F(xrCreateActionSpace) \
    F(xrSyncActions) \
    F(xrGetActionStatePose)
// clang-format on
#endif

namespace dbc
{
    struct VrSystem::Impl
    {
        explicit Impl(EngineKernel& Value) : Kernel(Value), Presenter(Value) {}

        EngineKernel& Kernel;
        VrPresenter Presenter;
        VrTracking Tracking;
        VrTrackingSpace FallbackSpace = VrTrackingSpace::Local;
        VrPreviewMode Preview = VrPreviewMode::Both;
        SDL_GPUDevice* Device = nullptr;
        bool Capable = false;
        bool Supported = false;
        bool Enabled = false;
        bool Running = false;
        bool WarnedRecenter = false;
        int WarnedSpace = -1;
        bool WarnedUnavailable = false;
        bool WarnedMirror = false;
        std::array<PreparedViewGeometry, 2> Geometry;
#if defined(OPENDBC_OPENXR)
        struct Operations
        {
#define DBC_XR_DECLARE(Name) PFN_##Name Name = nullptr;
            DBC_XR_FUNCTIONS(DBC_XR_DECLARE)
#undef DBC_XR_DECLARE
            decltype(&SDL_CreateGPUXRSession) CreateSession = SDL_CreateGPUXRSession;
            decltype(&SDL_GetGPUXRSwapchainFormats) Formats = SDL_GetGPUXRSwapchainFormats;
            decltype(&SDL_CreateGPUXRSwapchain) CreateSwapchain = SDL_CreateGPUXRSwapchain;
            decltype(&SDL_DestroyGPUXRSwapchain) DestroySwapchain = SDL_DestroyGPUXRSwapchain;
        } Calls;

        XrInstance Instance = XR_NULL_HANDLE;
        XrSystemId System = XR_NULL_SYSTEM_ID;
        XrSession Session = XR_NULL_HANDLE;
        XrSpace Space = XR_NULL_HANDLE;
        XrSpace ViewSpace = XR_NULL_HANDLE;
        XrActionSet ActionSet = XR_NULL_HANDLE;
        XrAction Grip = XR_NULL_HANDLE;
        std::array<XrPath, 2> Hands{};
        std::array<XrSpace, 2> GripSpaces{};
        std::array<XrSwapchain, 2> Swapchains{};
        std::array<SDL_GPUTexture**, 2> Images{};
        std::array<Uint32, 2> ImageCounts{};
        std::array<bool, 2> Acquired{};
        std::array<bool, 2> Waited{};
        std::array<XrViewConfigurationView, 2> Configs{};
        std::array<XrView, 2> Views{};
        XrFrameState Frame{};
        SDL_GPUTextureFormat Format = SDL_GPU_TEXTUREFORMAT_INVALID;
        bool CopySource = true;
        bool Begun = false;
        XrTime SpaceChangeTime = 0;
        std::string Failure;

        bool Check(XrResult Result, const char* Operation);
        bool InitializeSession();
        bool CreateSpace(VrTrackingSpace Mode);
        bool CreateActions();
        bool PollEvents();
        bool Locate();
        bool FinishFrame(bool Submitted);
        void UpdatePose(std::size_t Index, const XrSpaceLocation& Location, bool Active = true);
#endif
        void Disable();
        glm::mat4 Player() const;
    };
} // namespace dbc
