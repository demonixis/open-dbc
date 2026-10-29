#include "camera_system.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "gpu_command_buffer.hpp"
#include "graphics_device_system.hpp"
#include "log.hpp"
#include "path_utf8.hpp"
#include "vr_system_internal.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace dbc
{
    namespace
    {
        bool Invalid(std::string* Error, const char* Message)
        {
            if (Error)
                *Error = Message;
            return false;
        }
#if defined(OPENDBC_OPENXR)
        XrReferenceSpaceType ReferenceType(VrTrackingSpace Mode)
        {
            switch (Mode)
            {
                case VrTrackingSpace::LocalFloor:
                    return XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
                case VrTrackingSpace::Stage:
                    return XR_REFERENCE_SPACE_TYPE_STAGE;
                default:
                    return XR_REFERENCE_SPACE_TYPE_LOCAL;
            }
        }
        glm::mat4 TrackingMatrix(const XrPosef& Pose)
        {
            return VrTracking::ConvertPose(
                {Pose.position.x, Pose.position.y, Pose.position.z},
                {Pose.orientation.x, Pose.orientation.y, Pose.orientation.z, Pose.orientation.w});
        }
#endif
    } // namespace

    VrSystem::VrSystem(EngineKernel& Kernel) : Impl_(std::make_unique<Impl>(Kernel)) {}
    VrSystem::~VrSystem()
    {
        DetachDevice();
    }

    bool VrSystem::Configure(bool VrCapable, std::string* Error)
    {
        if (Impl_->Kernel.Gpu().GpuDevice() && VrCapable && !Impl_->Capable)
            return Invalid(Error, "VR capability must be configured before runtime initialization");
        Impl_->Disable();
        Impl_->Capable = VrCapable;
        Impl_->Tracking = {};
        Impl_->FallbackSpace = VrTrackingSpace::Local;
        Impl_->Preview = VrPreviewMode::Both;
        Impl_->WarnedUnavailable = false;
        Impl_->WarnedRecenter = false;
        Impl_->WarnedSpace = -1;
        return true;
    }
    bool VrSystem::Capable() const
    {
        return Impl_->Capable;
    }
    bool VrSystem::Supported() const
    {
        return Impl_->Supported;
    }
    bool VrSystem::Enabled() const
    {
        return Impl_->Enabled;
    }
    bool VrSystem::Pacing() const
    {
        return Impl_->Enabled && Impl_->Running;
    }
    float VrSystem::CameraHeight() const
    {
        return Impl_->Tracking.Height;
    }

    glm::mat4 VrSystem::Impl::Player() const
    {
        return glm::inverse(Kernel.Renderer().CameraView(1, 1).WorldToView);
    }
    VrPose VrSystem::HeadPose() const
    {
        return Impl_->Tracking.Compose(0, Impl_->Player());
    }
    bool VrSystem::ControllerPose(int Hand, VrPose& Pose, std::string* Error) const
    {
        if (Hand < 0 || Hand > 1)
            return Invalid(Error, "VR hand must be 0 (left) or 1 (right)");
        Pose = Impl_->Tracking.Compose(static_cast<std::size_t>(Hand + 1), Impl_->Player());
        return true;
    }
    bool VrSystem::SetCameraHeight(float Height, std::string* Error)
    {
        if (!std::isfinite(Height))
            return Invalid(Error, "VR camera height must be finite");
        Impl_->Tracking.Height = Height;
        return true;
    }
    bool VrSystem::SetPreviewMode(int Mode, std::string* Error)
    {
        if (Mode < 0 || Mode > 2)
            return Invalid(Error, "VR preview mode must be 0, 1 or 2");
        Impl_->Preview = static_cast<VrPreviewMode>(Mode);
        return true;
    }
    void VrSystem::RecenterCamera()
    {
        const bool Recentered = Impl_->Tracking.RecenterHead();
        if (!Recentered && !Impl_->WarnedRecenter)
            DBC_LOG_WARN("VR recenter requires a tracked head pose");
        Impl_->WarnedRecenter = !Recentered;
    }
    bool VrSystem::SetTrackingSpace(int Mode, std::string* Error)
    {
        if (Mode < 0 || Mode > 2)
            return Invalid(Error, "VR tracking space must be 0, 1 or 2");
        const auto Next = static_cast<VrTrackingSpace>(Mode);
        if (Next == Impl_->Tracking.Space)
            return true;
#if defined(OPENDBC_OPENXR)
        if (Impl_->Session && !Impl_->CreateSpace(Next))
        {
            if (Impl_->WarnedSpace != Mode)
                DBC_LOG_WARN("VR tracking space unchanged: {}", Impl_->Failure);
            Impl_->WarnedSpace = Mode;
            return true;
        }
#endif
        Impl_->WarnedSpace = -1;
        if (Next != VrTrackingSpace::Stage)
            Impl_->FallbackSpace = Next;
        Impl_->Tracking.Space = Next;
        Impl_->Tracking.Recenter = glm::mat4(1.0f);
        return true;
    }

    bool VrSystem::SetEnabled(int Mode, std::string* Error)
    {
        if (Mode < 0 || Mode > 1)
            return Invalid(Error, "VR enabled mode must be 0 or 1");
        if (!Mode)
        {
            Impl_->Disable();
            return true;
        }
        if (Impl_->Enabled)
            return true;
#if defined(OPENDBC_OPENXR)
        if (Impl_->Supported)
        {
            if (Impl_->InitializeSession())
            {
                Impl_->Enabled = true;
                Impl_->WarnedUnavailable = false;
                return true;
            }
            if (!Impl_->WarnedUnavailable)
                DBC_LOG_WARN("VR activation failed: {}", Impl_->Failure);
            Impl_->WarnedUnavailable = true;
            Impl_->Disable();
            return true;
        }
#endif
        if (!Impl_->WarnedUnavailable)
            DBC_LOG_WARN("VR is unavailable; SET VR ENABLED 1 has no effect");
        Impl_->WarnedUnavailable = true;
        return true;
    }

    void VrSystem::Impl::Disable()
    {
#if defined(OPENDBC_OPENXR)
        if (Begun)
            FinishFrame(false);
#endif
#if defined(OPENDBC_OPENXR)
        if (Device && Session && !SDL_WaitForGPUIdle(Device))
            DBC_LOG_WARN("VR GPU shutdown wait failed: {}", SDL_GetError());
#endif
        Presenter.Reset();
#if defined(OPENDBC_OPENXR)
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            if (Swapchains[Eye] &&
                !Check(Calls.DestroySwapchain(Device, Swapchains[Eye], Images[Eye]), "destroy swapchain"))
                DBC_LOG_WARN("VR cleanup: {}", Failure);
            Swapchains[Eye] = XR_NULL_HANDLE;
            Images[Eye] = nullptr;
            ImageCounts[Eye] = 0;
            if (GripSpaces[Eye])
                Calls.xrDestroySpace(GripSpaces[Eye]);
            GripSpaces[Eye] = XR_NULL_HANDLE;
        }
        if (ViewSpace)
            Calls.xrDestroySpace(ViewSpace);
        if (Space)
            Calls.xrDestroySpace(Space);
        ViewSpace = Space = XR_NULL_HANDLE;
        if (Session && !Check(Calls.xrDestroySession(Session), "destroy session"))
            DBC_LOG_WARN("VR cleanup: {}", Failure);
        Session = XR_NULL_HANDLE;
        if (ActionSet)
            Calls.xrDestroyActionSet(ActionSet);
        ActionSet = XR_NULL_HANDLE;
        Grip = XR_NULL_HANDLE;
        Acquired = {};
        Waited = {};
        Begun = false;
        SpaceChangeTime = 0;
#endif
        Enabled = Running = false;
        Tracking.Tracked.fill(false);
    }

    void VrSystem::DetachDevice()
    {
        if (!Impl_)
            return;
        Impl_->Disable();
        Impl_->Device = nullptr;
        Impl_->Supported = false;
#if defined(OPENDBC_OPENXR)
        Impl_->Instance = XR_NULL_HANDLE;
        Impl_->System = XR_NULL_SYSTEM_ID;
#endif
    }

    SDL_GPUDevice* VrSystem::CreateDevice()
    {
        if (!Impl_->Capable)
            return nullptr;
#if defined(OPENDBC_OPENXR)
        auto& State = *Impl_;
        if (!SDL_GetHint(SDL_HINT_OPENXR_LIBRARY))
        {
#if defined(_WIN32)
            constexpr auto Loader = "openxr_loader.dll";
#elif defined(__APPLE__)
            constexpr auto Loader = "libopenxr_loader.dylib";
#else
            constexpr auto Loader = "libopenxr_loader.so.1";
#endif
            if (const char* Base = SDL_GetBasePath())
            {
                std::error_code Error;
                const auto Path = dbc::PathFromUtf8(Base) / Loader;
                if (std::filesystem::is_regular_file(Path, Error))
                    SDL_SetHint(SDL_HINT_OPENXR_LIBRARY, dbc::PathToUtf8(Path).c_str());
            }
        }
        const auto Props = SDL_CreateProperties();
        if (!Props)
            return nullptr;
        const bool Configured =
            SDL_SetStringProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan") &&
            SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true) &&
            SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true) &&
            SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_ENABLE_BOOLEAN, true) &&
            SDL_SetPointerProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_INSTANCE_POINTER, &State.Instance) &&
            SDL_SetPointerProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_SYSTEM_ID_POINTER, &State.System) &&
            SDL_SetNumberProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_VERSION_NUMBER, XR_MAKE_VERSION(1, 1, 0)) &&
            SDL_SetNumberProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_FORM_FACTOR_NUMBER,
                                  XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) &&
            SDL_SetStringProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_APPLICATION_NAME_STRING, "OpenDBC") &&
            SDL_SetStringProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_XR_ENGINE_NAME_STRING, "OpenDBC");
        SDL_GPUDevice* Device = Configured ? SDL_CreateGPUDeviceWithProperties(Props) : nullptr;
        SDL_DestroyProperties(Props);
        if (Device)
        {
            auto GetProc = SDL_OpenXR_GetXrGetInstanceProcAddr();
            bool Loaded = GetProc && State.Instance && State.System;
#define DBC_XR_LOAD(Name)                                                                                              \
    if (Loaded &&                                                                                                      \
        (!State.Check(GetProc(State.Instance, #Name, reinterpret_cast<PFN_xrVoidFunction*>(&State.Calls.Name)),        \
                      #Name) ||                                                                                        \
         !State.Calls.Name))                                                                                           \
        Loaded = false;
            DBC_XR_FUNCTIONS(DBC_XR_LOAD)
#undef DBC_XR_LOAD
            uint32_t Count = 0;
            if (Loaded &&
                State.Check(State.Calls.xrEnumerateViewConfigurationViews(State.Instance, State.System,
                                                                          XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                                          &Count, nullptr),
                            "enumerate stereo views") &&
                Count == 2)
            {
                State.Device = Device;
                State.Supported = true;
                return Device;
            }
            SDL_DestroyGPUDevice(Device);
        }
        State.Instance = XR_NULL_HANDLE;
        State.System = XR_NULL_SYSTEM_ID;
        DBC_LOG_WARN("OpenXR Vulkan initialization failed; using the flat renderer: {} {}", SDL_GetError(),
                     State.Failure);
#else
        DBC_LOG_WARN("OpenXR is unavailable on this build; using the flat renderer");
#endif
        return nullptr;
    }

#if defined(OPENDBC_OPENXR)
    bool VrSystem::Impl::Check(XrResult Result, const char* Operation)
    {
        if (XR_SUCCEEDED(Result))
            return true;
        if (Result == XR_ERROR_INSTANCE_LOST)
            Supported = false;
        Failure = std::string(Operation) + " failed (OpenXR " + std::to_string(Result) + ")";
        return false;
    }

    bool VrSystem::Impl::CreateSpace(VrTrackingSpace Mode)
    {
        uint32_t Count = 0;
        if (!Check(Calls.xrEnumerateReferenceSpaces(Session, 0, &Count, nullptr), "enumerate reference spaces"))
            return false;
        std::vector<XrReferenceSpaceType> Spaces(Count);
        if (!Check(Calls.xrEnumerateReferenceSpaces(Session, Count, &Count, Spaces.data()),
                   "enumerate reference spaces"))
            return false;
        if (std::find(Spaces.begin(), Spaces.end(), ReferenceType(Mode)) == Spaces.end())
        {
            Failure = "requested reference space is unavailable";
            return false;
        }
        XrReferenceSpaceCreateInfo Info{};
        Info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        Info.referenceSpaceType = ReferenceType(Mode);
        Info.poseInReferenceSpace.orientation.w = 1;
        XrSpace Next = XR_NULL_HANDLE;
        if (!Check(Calls.xrCreateReferenceSpace(Session, &Info, &Next), "create reference space"))
            return false;
        if (Space)
            Calls.xrDestroySpace(Space);
        Space = Next;
        Tracking.Space = Mode;
        Tracking.Recenter = glm::mat4(1.0f);
        Tracking.Tracked.fill(false);
        // Cached poses must not be reinterpreted in a different reference frame.
        Tracking.Valid.fill(false);
        return true;
    }

    bool VrSystem::Impl::CreateActions()
    {
        XrActionSetCreateInfo Set{};
        Set.type = XR_TYPE_ACTION_SET_CREATE_INFO;
        SDL_strlcpy(Set.actionSetName, "opendbc_grip", sizeof(Set.actionSetName));
        SDL_strlcpy(Set.localizedActionSetName, "OpenDBC controllers", sizeof(Set.localizedActionSetName));
        if (!Check(Calls.xrCreateActionSet(Instance, &Set, &ActionSet), "create action set"))
            return false;
        for (std::size_t Hand = 0; Hand < 2; ++Hand)
            if (!Check(Calls.xrStringToPath(Instance, Hand == 0 ? "/user/hand/left" : "/user/hand/right", &Hands[Hand]),
                       "hand path"))
                return false;
        XrActionCreateInfo Action{};
        Action.type = XR_TYPE_ACTION_CREATE_INFO;
        Action.actionType = XR_ACTION_TYPE_POSE_INPUT;
        SDL_strlcpy(Action.actionName, "grip_pose", sizeof(Action.actionName));
        SDL_strlcpy(Action.localizedActionName, "Grip pose", sizeof(Action.localizedActionName));
        Action.countSubactionPaths = 2;
        Action.subactionPaths = Hands.data();
        if (!Check(Calls.xrCreateAction(ActionSet, &Action, &Grip), "create grip action"))
            return false;
        std::array<XrActionSuggestedBinding, 2> Bindings{};
        for (std::size_t Hand = 0; Hand < 2; ++Hand)
        {
            Bindings[Hand].action = Grip;
            if (!Check(Calls.xrStringToPath(
                           Instance, Hand == 0 ? "/user/hand/left/input/grip/pose" : "/user/hand/right/input/grip/pose",
                           &Bindings[Hand].binding),
                       "grip path"))
                return false;
        }
        for (const char* Profile :
             {"/interaction_profiles/khr/simple_controller", "/interaction_profiles/oculus/touch_controller",
              "/interaction_profiles/valve/index_controller", "/interaction_profiles/htc/vive_controller",
              "/interaction_profiles/microsoft/motion_controller"})
        {
            XrInteractionProfileSuggestedBinding Suggest{};
            Suggest.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
            if (!Check(Calls.xrStringToPath(Instance, Profile, &Suggest.interactionProfile),
                       "interaction profile path"))
                return false;
            Suggest.countSuggestedBindings = 2;
            Suggest.suggestedBindings = Bindings.data();
            const auto Result = Calls.xrSuggestInteractionProfileBindings(Instance, &Suggest);
            if (Result != XR_ERROR_PATH_UNSUPPORTED && !Check(Result, "suggest grip bindings"))
                return false;
        }
        XrSessionActionSetsAttachInfo Attach{};
        Attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        Attach.countActionSets = 1;
        Attach.actionSets = &ActionSet;
        if (!Check(Calls.xrAttachSessionActionSets(Session, &Attach), "attach action set"))
            return false;
        for (std::size_t Hand = 0; Hand < 2; ++Hand)
        {
            XrActionSpaceCreateInfo Info{};
            Info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
            Info.action = Grip;
            Info.subactionPath = Hands[Hand];
            Info.poseInActionSpace.orientation.w = 1;
            if (!Check(Calls.xrCreateActionSpace(Session, &Info, &GripSpaces[Hand]), "create grip space"))
                return false;
        }
        return true;
    }

    bool VrSystem::Impl::InitializeSession()
    {
        Failure.clear();
        XrSessionCreateInfo Create{};
        Create.type = XR_TYPE_SESSION_CREATE_INFO;
        if (!Check(Calls.CreateSession(Device, &Create, &Session), "create session"))
            return false;
        uint32_t Count = 0;
        for (auto& Config : Configs)
            Config = {XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 0, 0, 0, 0, 0, 0};
        if (!Check(Calls.xrEnumerateViewConfigurationViews(Instance, System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                           2, &Count, Configs.data()),
                   "enumerate stereo configuration") ||
            Count != 2)
            return false;
        if (!Check(Calls.xrEnumerateEnvironmentBlendModes(Instance, System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                          0, &Count, nullptr),
                   "enumerate blend modes"))
            return false;
        std::vector<XrEnvironmentBlendMode> Modes(Count);
        if (!Check(Calls.xrEnumerateEnvironmentBlendModes(Instance, System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                          Count, &Count, Modes.data()),
                   "enumerate blend modes"))
            return false;
        if (std::find(Modes.begin(), Modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == Modes.end())
        {
            Failure = "VR requires an opaque stereo environment";
            return false;
        }
        int FormatCount = 0;
        auto* Formats = Calls.Formats(Device, Session, &FormatCount);
        Format = SDL_GPU_TEXTUREFORMAT_INVALID;
        if (Formats)
        {
            for (auto Preferred : {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB,
                                   SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM})
                if (std::find(Formats, Formats + FormatCount, Preferred) != Formats + FormatCount)
                {
                    Format = Preferred;
                    break;
                }
            SDL_free(Formats);
        }
        if (Format == SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            Failure = "no compatible XR color format";
            return false;
        }
        CopySource = true;
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            XrSwapchainCreateInfo Info{};
            Info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
            Info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
            Info.width = Configs[Eye].recommendedImageRectWidth;
            Info.height = Configs[Eye].recommendedImageRectHeight;
            if (!Info.width || !Info.height || Info.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                Info.height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
            {
                Failure = "invalid XR eye dimensions";
                return false;
            }
            Info.sampleCount = Info.faceCount = Info.arraySize = Info.mipCount = 1;
            auto Result = Calls.CreateSwapchain(Device, Session, &Info, Format, &Swapchains[Eye], &Images[Eye]);
            if (XR_FAILED(Result))
            {
                Info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
                Result = Calls.CreateSwapchain(Device, Session, &Info, Format, &Swapchains[Eye], &Images[Eye]);
                CopySource = false;
            }
            if (!Check(Result, "create eye swapchain"))
                return false;
            if (!Check(Calls.xrEnumerateSwapchainImages(Swapchains[Eye], 0, &ImageCounts[Eye], nullptr),
                       "count eye images") ||
                !ImageCounts[Eye])
                return false;
        }
        const auto RequestedSpace = Tracking.Space;
        if (!CreateSpace(RequestedSpace))
        {
            if (RequestedSpace != VrTrackingSpace::Stage)
                return false;
            DBC_LOG_WARN("STAGE unavailable; retaining the previous tracking space");
            if (!CreateSpace(FallbackSpace))
                return false;
        }
        XrReferenceSpaceCreateInfo ViewInfo{};
        ViewInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        ViewInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        ViewInfo.poseInReferenceSpace.orientation.w = 1;
        if (!Check(Calls.xrCreateReferenceSpace(Session, &ViewInfo, &ViewSpace), "create VIEW space"))
            return false;
        return CreateActions();
    }

    bool VrSystem::Impl::PollEvents()
    {
        XrEventDataBuffer Event{};
        Event.type = XR_TYPE_EVENT_DATA_BUFFER;
        for (;;)
        {
            const auto Result = Calls.xrPollEvent(Instance, &Event);
            if (Result == XR_EVENT_UNAVAILABLE)
                return true;
            if (!Check(Result, "poll events"))
                return false;
            if (Event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            {
                Failure = "OpenXR instance lost";
                Supported = false;
                return false;
            }
            if (Event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto& Change = reinterpret_cast<const XrEventDataSessionStateChanged&>(Event);
                if (Change.session == Session)
                {
                    if (Change.state == XR_SESSION_STATE_READY && !Running)
                    {
                        XrSessionBeginInfo Info{};
                        Info.type = XR_TYPE_SESSION_BEGIN_INFO;
                        Info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        if (!Check(Calls.xrBeginSession(Session, &Info), "begin session"))
                            return false;
                        Running = true;
                    }
                    if (Change.state == XR_SESSION_STATE_STOPPING)
                    {
                        if (Running && !Check(Calls.xrEndSession(Session), "end session"))
                            return false;
                        Running = false;
                        Tracking.Tracked.fill(false);
                    }
                    if (Change.state == XR_SESSION_STATE_LOSS_PENDING || Change.state == XR_SESSION_STATE_EXITING)
                    {
                        Failure = "OpenXR session ended or was lost";
                        return false;
                    }
                }
            }
            if (Event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
            {
                const auto& Change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(Event);
                if (Change.session == Session && Change.referenceSpaceType == ReferenceType(Tracking.Space))
                    SpaceChangeTime = Change.changeTime;
            }
            Event = {};
            Event.type = XR_TYPE_EVENT_DATA_BUFFER;
        }
    }

    void VrSystem::Impl::UpdatePose(std::size_t Index, const XrSpaceLocation& Location, bool Active)
    {
        constexpr auto Required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                  XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        Tracking.Update(Index, TrackingMatrix(Location.pose),
                        Active && (Location.locationFlags & Required) == Required);
    }

    bool VrSystem::Impl::Locate()
    {
        Frame = {};
        Frame.type = XR_TYPE_FRAME_STATE;
        XrFrameWaitInfo Wait{};
        Wait.type = XR_TYPE_FRAME_WAIT_INFO;
        if (!Check(Calls.xrWaitFrame(Session, &Wait, &Frame), "wait frame"))
            return false;
        XrFrameBeginInfo Begin{};
        Begin.type = XR_TYPE_FRAME_BEGIN_INFO;
        if (!Check(Calls.xrBeginFrame(Session, &Begin), "begin frame"))
            return false;
        Begun = true;
        if (SpaceChangeTime && Frame.predictedDisplayTime >= SpaceChangeTime)
        {
            SpaceChangeTime = 0;
            if (!CreateSpace(Tracking.Space))
                return false;
        }
        XrViewLocateInfo LocateInfo{};
        LocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
        LocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        LocateInfo.displayTime = Frame.predictedDisplayTime;
        LocateInfo.space = Space;
        for (auto& View : Views)
        {
            View = {};
            View.type = XR_TYPE_VIEW;
        }
        XrViewState State{};
        State.type = XR_TYPE_VIEW_STATE;
        uint32_t Count = 0;
        if (!Check(Calls.xrLocateViews(Session, &LocateInfo, &State, 2, &Count, Views.data()), "locate stereo views"))
            return false;
        constexpr auto Required = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        Frame.shouldRender = Frame.shouldRender && Count == 2 && (State.viewStateFlags & Required) == Required;
        XrSpaceLocation Head{};
        Head.type = XR_TYPE_SPACE_LOCATION;
        if (!Check(Calls.xrLocateSpace(ViewSpace, Space, Frame.predictedDisplayTime, &Head), "locate head"))
            return false;
        UpdatePose(0, Head);
        XrActiveActionSet Active{ActionSet, XR_NULL_PATH};
        XrActionsSyncInfo Sync{};
        Sync.type = XR_TYPE_ACTIONS_SYNC_INFO;
        Sync.countActiveActionSets = 1;
        Sync.activeActionSets = &Active;
        const auto SyncResult = Calls.xrSyncActions(Session, &Sync);
        if (SyncResult == XR_SESSION_NOT_FOCUSED)
        {
            Tracking.Tracked[1] = Tracking.Tracked[2] = false;
            return true;
        }
        if (!Check(SyncResult, "sync grip actions"))
            return false;
        for (std::size_t Hand = 0; Hand < 2; ++Hand)
        {
            XrActionStateGetInfo Info{};
            Info.type = XR_TYPE_ACTION_STATE_GET_INFO;
            Info.action = Grip;
            Info.subactionPath = Hands[Hand];
            XrActionStatePose Pose{};
            Pose.type = XR_TYPE_ACTION_STATE_POSE;
            if (!Check(Calls.xrGetActionStatePose(Session, &Info, &Pose), "read grip action"))
                return false;
            XrSpaceLocation Location{};
            Location.type = XR_TYPE_SPACE_LOCATION;
            if (Pose.isActive &&
                !Check(Calls.xrLocateSpace(GripSpaces[Hand], Space, Frame.predictedDisplayTime, &Location),
                       "locate grip"))
                return false;
            UpdatePose(Hand + 1, Location, Pose.isActive);
        }
        return true;
    }

    bool VrSystem::Impl::FinishFrame(bool Submitted)
    {
        if (!Begun)
            return true;
        const bool Both = Waited[0] && Waited[1];
        bool Success = true;
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            if (!Waited[Eye])
                continue;
            XrSwapchainImageReleaseInfo Info{};
            Info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            Success = Check(Calls.xrReleaseSwapchainImage(Swapchains[Eye], &Info), "release eye") && Success;
            Acquired[Eye] = Waited[Eye] = false;
        }
        std::array<XrCompositionLayerProjectionView, 2> Projection{};
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            Projection[Eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            Projection[Eye].pose = Views[Eye].pose;
            Projection[Eye].fov = Views[Eye].fov;
            Projection[Eye].subImage.swapchain = Swapchains[Eye];
            Projection[Eye].subImage.imageRect.extent = {static_cast<int32_t>(Configs[Eye].recommendedImageRectWidth),
                                                         static_cast<int32_t>(Configs[Eye].recommendedImageRectHeight)};
        }
        XrCompositionLayerProjection Layer{};
        Layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        Layer.space = Space;
        Layer.viewCount = 2;
        Layer.views = Projection.data();
        const auto* LayerPointer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&Layer);
        XrFrameEndInfo End{};
        End.type = XR_TYPE_FRAME_END_INFO;
        End.displayTime = Frame.predictedDisplayTime;
        End.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        End.layerCount = Submitted && Both && Success && Frame.shouldRender ? 1 : 0;
        End.layers = End.layerCount ? &LayerPointer : nullptr;
        Success = Check(Calls.xrEndFrame(Session, &End), "end frame") && Success;
        Begun = false;
        return Success;
    }
#endif

    bool VrSystem::RenderFrame(double DeltaSeconds, bool Retain2d)
    {
#if defined(OPENDBC_OPENXR)
        auto& State = *Impl_;
        if (!State.Enabled)
            return false;
        const auto Fail = [&]()
        {
            DBC_LOG_WARN("VR stopped: {}", State.Failure);
            State.Disable();
        };
        if (!State.PollEvents())
        {
            Fail();
            return false;
        }
        if (!State.Running)
            return false;
        if (!State.Locate())
        {
            Fail();
            return true;
        }
        if (!State.Frame.shouldRender)
        {
            if (!State.FinishFrame(false))
                Fail();
            return true;
        }
        std::array<SDL_GPUTexture*, 2> Images{};
        std::array<Uint32, 2> Widths{}, Heights{};
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            uint32_t Index = 0;
            XrSwapchainImageAcquireInfo Acquire{};
            Acquire.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
            if (!State.Check(State.Calls.xrAcquireSwapchainImage(State.Swapchains[Eye], &Acquire, &Index),
                             "acquire eye"))
            {
                Fail();
                return true;
            }
            State.Acquired[Eye] = true;
            XrSwapchainImageWaitInfo Wait{};
            Wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            Wait.timeout = XR_INFINITE_DURATION;
            const auto Result = State.Calls.xrWaitSwapchainImage(State.Swapchains[Eye], &Wait);
            if (Result == XR_TIMEOUT_EXPIRED || !State.Check(Result, "wait eye"))
            {
                State.Failure = "XR eye image wait failed";
                Fail();
                return true;
            }
            State.Waited[Eye] = true;
            if (Index >= State.ImageCounts[Eye])
            {
                State.Failure = "XR image index out of bounds";
                Fail();
                return true;
            }
            Images[Eye] = State.Images[Eye][Index];
            Widths[Eye] = State.Configs[Eye].recommendedImageRectWidth;
            Heights[Eye] = State.Configs[Eye].recommendedImageRectHeight;
        }
        auto& Renderer = State.Kernel.Renderer();
        auto& Camera = State.Kernel.Camera();
        std::array<GpuRenderTarget, 2> Targets{};
        GpuCommandBuffer Commands(SDL_AcquireGPUCommandBuffer(State.Device));
        if (!Commands.Get() ||
            !State.Presenter.Targets(Images, State.Format, State.CopySource, Widths, Heights, Targets))
        {
            State.Failure = "XR render target setup failed";
            Commands.Abandon();
            Fail();
            return true;
        }
        auto Frame = Renderer.PrepareFrame(DeltaSeconds, Retain2d, true);
        const auto Origin = State.Player() * State.Tracking.Origin();
        std::array<std::optional<RenderView>, 2> RenderViews;
        bool Success = true;
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            const auto& Fov = State.Views[Eye].fov;
            const auto Projection = VrTracking::Projection(Fov.angleLeft, Fov.angleRight, Fov.angleUp, Fov.angleDown,
                                                           Camera.ClampedNearPlane(), Camera.ClampedFarPlane());
            const int W = State.Kernel.Core().WindowWidth(), H = State.Kernel.Core().WindowHeight();
            RenderViews[Eye].emplace(RenderView{glm::inverse(Origin * TrackingMatrix(State.Views[Eye].pose)),
                                                Projection,
                                                Projection,
                                                {0, 0, static_cast<float>(W), static_cast<float>(H)},
                                                W,
                                                H,
                                                Widths[Eye],
                                                Heights[Eye]});
            Success = Renderer.BuildPreparedView(*RenderViews[Eye], State.Geometry[Eye], Frame) && Success;
        }
        Success = Success && Renderer.UploadPreparedViews(State.Geometry, Commands.Get());
        for (std::size_t Eye = 0; Success && Eye < 2; ++Eye)
            Success = Renderer.RecordPreparedView(*RenderViews[Eye], Targets[Eye], State.Geometry[Eye], Commands.Get(),
                                                  Frame);
        Success = Success && State.Presenter.Present(Commands.Get(), Images, State.Preview);
        if (Success)
        {
            if (!State.Presenter.Mirror(Commands) && !State.WarnedMirror)
            {
                DBC_LOG_WARN("VR window preview unavailable: {}", SDL_GetError());
                State.WarnedMirror = true;
            }
            Success = Commands.Submit();
        }
        else
            Commands.Abandon();
        const bool Ended = State.FinishFrame(Success);
        if (Success && Ended)
        {
            State.Presenter.Commit();
            Renderer.FinalizeFrame(Frame);
        }
        if (!Success || !Ended)
        {
            if (!Success)
                State.Failure = "XR rendering or GPU submission failed";
            Fail();
        }
        return true;
#else
        (void)DeltaSeconds;
        (void)Retain2d;
        return false;
#endif
    }
    SDL_Surface* VrSystem::CapturePreview()
    {
        return Impl_->Presenter.Capture();
    }
} // namespace dbc
