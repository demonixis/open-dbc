#include "engine/systems/camera_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/draw_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/gpu_command_buffer.hpp"
#include "engine/systems/graphics_device_system.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/text_system.hpp"
#include "engine/systems/vr_system_internal.hpp"
#include "interpreter/engine_builtin_context.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"
#include "interpreter/execution_service.hpp"
#include "language/dialect_detector.hpp"
#include <deque>
#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <type_traits>

namespace dbc
{
    struct VrSystemTestAccess
    {
        static auto& Get(VrSystem& System)
        {
            return *System.Impl_;
        }
    };
} // namespace dbc

namespace
{
    using namespace dbc;
    using namespace dbc::interpreter;
    using namespace dbc::interpreter::detail;
    using language::LanguageProfile;
    using language::SymbolId;

    bool Near(const glm::mat4& A, const glm::mat4& B)
    {
        for (int C = 0; C < 4; ++C)
            for (int R = 0; R < 4; ++R)
                if (std::abs(A[C][R] - B[C][R]) > 0.0001f)
                    return false;
        return true;
    }

    template <typename T> T Handle(std::uintptr_t Value)
    {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<T>(Value);
        else
            return static_cast<T>(Value);
    }
    template <typename T> struct Succeed;

    template <typename... Args> struct Succeed<XrResult(XRAPI_PTR*)(Args...)>
    {
        static XrResult XRAPI_CALL Call(Args...)
        {
            return XR_SUCCESS;
        }
    };

    struct FakeXr
    {
        EngineKernel Kernel;
        static inline FakeXr* Active = nullptr;
        std::vector<std::string> Events;
        std::deque<XrSessionState> States;
        int Sessions = 0, Destroyed = 0, Acquires = 0, Releases = 0, EndLayers = -1;
        int FailAcquire = -1, FailWait = -1;
        bool Stage = false, NoRender = true, InvalidViews = false, Focused = true, LostTracking = false;
        bool FailSession = false, FailSecondSwapchain = false;
        bool UseWindowFormat = false, RefuseCopy = false;
        int SwapchainCalls = 0;
        std::array<std::array<SDL_GPUTexture*, 1>, 2> Images{};

        FakeXr()
        {
            Active = this;
            REQUIRE(Kernel.Initialize("fake-xr", 64, 64));
            auto& State = VrSystemTestAccess::Get(Kernel.Vr());
#define DBC_XR_FAKE(Name) State.Calls.Name = Succeed<decltype(State.Calls.Name)>::Call;
            DBC_XR_FUNCTIONS(DBC_XR_FAKE)
#undef DBC_XR_FAKE
            State.Capable = State.Supported = true;
            State.Instance = Handle<XrInstance>(1);
            State.System = 1;
            State.Calls.CreateSession = [](SDL_GPUDevice*, const XrSessionCreateInfo*, XrSession* Session)
            {
                if (Active->FailSession)
                    return XR_ERROR_RUNTIME_FAILURE;
                *Session = Handle<XrSession>(2);
                ++Active->Sessions;
                return XR_SUCCESS;
            };
            State.Calls.xrDestroySession = [](XrSession)
            {
                ++Active->Destroyed;
                return XR_SUCCESS;
            };
            State.Calls.xrEnumerateViewConfigurationViews = [](XrInstance, XrSystemId, XrViewConfigurationType,
                                                               uint32_t Capacity, uint32_t* Count,
                                                               XrViewConfigurationView* Views)
            {
                *Count = 2;
                for (uint32_t I = 0; I < Capacity; ++I)
                {
                    Views[I].recommendedImageRectWidth = 64;
                    Views[I].recommendedImageRectHeight = 64;
                }
                return XR_SUCCESS;
            };
            State.Calls.xrEnumerateEnvironmentBlendModes = [](XrInstance, XrSystemId, XrViewConfigurationType,
                                                              uint32_t Capacity, uint32_t* Count,
                                                              XrEnvironmentBlendMode* Modes)
            {
                *Count = 1;
                if (Capacity)
                    Modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                return XR_SUCCESS;
            };
            State.Calls.Formats = [](SDL_GPUDevice*, XrSession, int* Count)
            {
                *Count = 1;
                auto* Formats = static_cast<SDL_GPUTextureFormat*>(SDL_malloc(sizeof(SDL_GPUTextureFormat)));
                *Formats = Active->UseWindowFormat ? Active->Kernel.Gpu().GpuColorFormat()
                                                   : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
                return Formats;
            };
            State.Calls.CreateSwapchain = [](SDL_GPUDevice* Device, XrSession, const XrSwapchainCreateInfo* Info,
                                             SDL_GPUTextureFormat Format, XrSwapchain* Chain,
                                             SDL_GPUTexture*** Textures)
            {
                if (Active->RefuseCopy && (Info->usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT))
                    return XR_ERROR_FEATURE_UNSUPPORTED;
                const int Eye = Active->SwapchainCalls++ % 2;
                if (Active->FailSecondSwapchain && Active->SwapchainCalls >= 2)
                    return XR_ERROR_RUNTIME_FAILURE;
                *Chain = Handle<XrSwapchain>(10 + Eye);
                if (Device)
                {
                    SDL_GPUTextureCreateInfo TextureInfo{};
                    TextureInfo.type = SDL_GPU_TEXTURETYPE_2D;
                    TextureInfo.format = Format;
                    TextureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    TextureInfo.width = TextureInfo.height = 64;
                    TextureInfo.layer_count_or_depth = TextureInfo.num_levels = 1;
                    TextureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    Active->Images[Eye][0] = SDL_CreateGPUTexture(Device, &TextureInfo);
                    if (!Active->Images[Eye][0])
                        return XR_ERROR_RUNTIME_FAILURE;
                }
                *Textures = Active->Images[Eye].data();
                return XR_SUCCESS;
            };
            State.Calls.DestroySwapchain = [](SDL_GPUDevice* Device, XrSwapchain, SDL_GPUTexture** Textures)
            {
                if (Device && Textures && Textures[0])
                    SDL_ReleaseGPUTexture(Device, Textures[0]);
                if (Textures)
                    Textures[0] = nullptr;
                return XR_SUCCESS;
            };
            State.Calls.xrEnumerateSwapchainImages =
                [](XrSwapchain, uint32_t, uint32_t* Count, XrSwapchainImageBaseHeader*)
            {
                *Count = 1;
                return XR_SUCCESS;
            };
            State.Calls.xrEnumerateReferenceSpaces =
                [](XrSession, uint32_t Capacity, uint32_t* Count, XrReferenceSpaceType* Spaces)
            {
                *Count = Active->Stage ? 4 : 3;
                if (Capacity)
                {
                    Spaces[0] = XR_REFERENCE_SPACE_TYPE_VIEW;
                    Spaces[1] = XR_REFERENCE_SPACE_TYPE_LOCAL;
                    Spaces[2] = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
                    if (Active->Stage)
                        Spaces[3] = XR_REFERENCE_SPACE_TYPE_STAGE;
                }
                return XR_SUCCESS;
            };
            State.Calls.xrCreateReferenceSpace = [](XrSession, const XrReferenceSpaceCreateInfo* Info, XrSpace* Space)
            {
                *Space = Handle<XrSpace>(Info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? 20 : 21);
                return XR_SUCCESS;
            };
            State.Calls.xrCreateActionSet = [](XrInstance, const XrActionSetCreateInfo*, XrActionSet* Set)
            {
                *Set = Handle<XrActionSet>(22);
                return XR_SUCCESS;
            };
            State.Calls.xrCreateAction = [](XrActionSet, const XrActionCreateInfo*, XrAction* Action)
            {
                *Action = Handle<XrAction>(23);
                return XR_SUCCESS;
            };
            State.Calls.xrStringToPath = [](XrInstance, const char* Path, XrPath* Output)
            {
                *Output = std::string_view(Path).find("right") != std::string_view::npos ? 2 : 1;
                return XR_SUCCESS;
            };
            State.Calls.xrCreateActionSpace = [](XrSession, const XrActionSpaceCreateInfo* Info, XrSpace* Space)
            {
                *Space = Handle<XrSpace>(30 + Info->subactionPath);
                return XR_SUCCESS;
            };
            State.Calls.xrPollEvent = [](XrInstance, XrEventDataBuffer* Buffer)
            {
                if (Active->States.empty())
                    return XR_EVENT_UNAVAILABLE;
                auto* Change = reinterpret_cast<XrEventDataSessionStateChanged*>(Buffer);
                Change->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
                Change->session = Handle<XrSession>(2);
                Change->state = Active->States.front();
                Active->States.pop_front();
                return XR_SUCCESS;
            };
            State.Calls.xrWaitFrame = [](XrSession, const XrFrameWaitInfo*, XrFrameState* Frame)
            {
                Active->Events.push_back("wait");
                Frame->predictedDisplayTime = 100;
                Frame->shouldRender = !Active->NoRender;
                return XR_SUCCESS;
            };
            State.Calls.xrBeginFrame = [](XrSession, const XrFrameBeginInfo*)
            {
                Active->Events.push_back("begin");
                return XR_SUCCESS;
            };
            State.Calls.xrEndFrame = [](XrSession, const XrFrameEndInfo* Frame)
            {
                Active->Events.push_back("end");
                Active->EndLayers = Frame->layerCount;
                if (Frame->layerCount)
                {
                    auto* Layer = reinterpret_cast<const XrCompositionLayerProjection*>(Frame->layers[0]);
                    CHECK(Layer->views[0].pose.position.x == -0.032f);
                }
                return XR_SUCCESS;
            };
            State.Calls.xrLocateViews =
                [](XrSession, const XrViewLocateInfo*, XrViewState* State, uint32_t, uint32_t* Count, XrView* Views)
            {
                Active->Events.push_back("views");
                *Count = 2;
                State->viewStateFlags =
                    Active->InvalidViews ? 0 : XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
                for (int Eye = 0; Eye < 2; ++Eye)
                {
                    Views[Eye].pose.orientation.w = 1;
                    Views[Eye].pose.position.x = Eye ? 0.032f : -0.032f;
                    Views[Eye].fov = {-0.7f, 0.8f, 0.75f, -0.65f};
                }
                return XR_SUCCESS;
            };
            State.Calls.xrLocateSpace = [](XrSpace Space, XrSpace, XrTime, XrSpaceLocation* Location)
            {
                Location->locationFlags = Active->LostTracking ? 0
                                                               : XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                                                     XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                                                     XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                                                                     XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
                Location->pose.orientation.w = 1;
                Location->pose.position = {Space == Handle<XrSpace>(20) ? 0.25f : 0.5f, 1.6f, -2.0f};
                return XR_SUCCESS;
            };
            State.Calls.xrSyncActions = [](XrSession, const XrActionsSyncInfo*)
            {
                Active->Events.push_back("actions");
                return Active->Focused ? XR_SUCCESS : XR_SESSION_NOT_FOCUSED;
            };
            State.Calls.xrGetActionStatePose = [](XrSession, const XrActionStateGetInfo*, XrActionStatePose* Pose)
            {
                Pose->isActive = true;
                return XR_SUCCESS;
            };
            State.Calls.xrAcquireSwapchainImage = [](XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* Index)
            {
                Active->Events.push_back("acquire");
                *Index = 0;
                return Active->Acquires++ == Active->FailAcquire ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
            };
            State.Calls.xrWaitSwapchainImage = [](XrSwapchain, const XrSwapchainImageWaitInfo*)
            {
                Active->Events.push_back("image-wait");
                return Active->Acquires - 1 == Active->FailWait ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
            };
            State.Calls.xrReleaseSwapchainImage = [](XrSwapchain, const XrSwapchainImageReleaseInfo*)
            {
                ++Active->Releases;
                Active->Events.push_back("release");
                return XR_SUCCESS;
            };
        }

        ~FakeXr()
        {
            Kernel.Shutdown();
            Active = nullptr;
        }

        void Enable(bool Gpu = false)
        {
            if (Gpu)
            {
                // The fake runtime borrows real SDL textures on the normal platform backend.
                auto& State = VrSystemTestAccess::Get(Kernel.Vr());
                State.Capable = false;
                REQUIRE(Kernel.Core().EnsureInitialized());
                State.Device = Kernel.Gpu().GpuDevice();
                State.Supported = true;
                State.Capable = true;
            }
            REQUIRE(Kernel.Vr().SetEnabled(1));
            REQUIRE(Kernel.Vr().Enabled());
            States.push_back(XR_SESSION_STATE_READY);
        }
    };
} // namespace

TEST_CASE("VR rig composes locomotion and preserves physical height and last tracked poses")
{
    dbc::VrTracking Tracking;
    CHECK(Tracking.Compose(0, glm::translate(glm::mat4(1), glm::vec3(9))).Position == glm::vec3(0));
    const auto Pose =
        glm::translate(glm::mat4(1), glm::vec3(2, 1.6f, 3)) * glm::rotate(glm::mat4(1), 0.5f, glm::vec3(0, 1, 0));
    Tracking.Update(0, Pose, true);
    Tracking.Height = 0.2f;
    Tracking.Space = dbc::VrTrackingSpace::LocalFloor;
    REQUIRE(Tracking.RecenterHead());
    const auto Player = glm::translate(glm::mat4(1), glm::vec3(10, 0, 20));
    auto Result = Tracking.Compose(0, Player);
    CHECK(Result.Position.x == doctest::Approx(10));
    CHECK(Result.Position.y == doctest::Approx(1.8f));
    CHECK(Result.Position.z == doctest::Approx(20));
    CHECK(Result.Rotation.y == doctest::Approx(0).epsilon(0.0001));
    Tracking.Update(0, glm::mat4(0), false);
    CHECK_FALSE(Tracking.Compose(0, Player).Tracked);
    CHECK(Tracking.Compose(0, Player).Position == Result.Position);
    CHECK_FALSE(Tracking.RecenterHead());
    Tracking.Update(0, Pose, true);
    Tracking.Space = dbc::VrTrackingSpace::Local;
    REQUIRE(Tracking.RecenterHead());
    CHECK(Tracking.Compose(0, Player).Position.y == doctest::Approx(0.2f));
    const auto Converted = dbc::VrTracking::ConvertPose({1, 2, -3}, {0, 0, 0, 1});
    CHECK(glm::vec3(Converted[3]) == glm::vec3(1, 2, 3));
    const auto Projection = dbc::VrTracking::Projection(-0.4f, 0.8f, 0.6f, -0.5f, 0.1f, 100);
    const glm::vec4 Near = Projection * glm::vec4(0, 0, 0.1f, 1);
    const glm::vec4 Far = Projection * glm::vec4(0, 0, 100, 1);
    CHECK(Near.z == doctest::Approx(0));
    CHECK(Far.z / Far.w == doctest::Approx(1));
    CHECK(Projection[2][0] < 0);
    const auto Left = Projection * glm::vec4(std::tan(-0.4f), 0, 1, 1);
    const auto Right = Projection * glm::vec4(std::tan(0.8f), 0, 1, 1);
    const auto Up = Projection * glm::vec4(0, std::tan(0.6f), 1, 1);
    const auto Down = Projection * glm::vec4(0, std::tan(-0.5f), 1, 1);
    CHECK(Left.x / Left.w == doctest::Approx(-1));
    CHECK(Right.x / Right.w == doctest::Approx(1));
    CHECK(Up.y / Up.w == doctest::Approx(1));
    CHECK(Down.y / Down.w == doctest::Approx(-1));
    const dbc::RenderView View{glm::mat4(1), Projection, Projection, {0, 0, 100, 100}, 100, 100, 100, 100};
    CHECK(View.ContainsSphere({0, 0, 1}, 0));
    CHECK_FALSE(View.ContainsSphere({0, 0, 0.05f}, 0));
    CHECK_FALSE(View.ContainsSphere({0, 0, 101}, 0, true));
    CHECK_FALSE(View.ContainsSphere({2, 0, 1}, 0));
    CHECK(dbc::VrTracking::Letterbox(0, 0, 100, 100, 200, 100) == glm::vec4(0, 25, 100, 50));
}

TEST_CASE("VR builtins expose shared poses and validate destination vectors in both profiles")
{
    for (auto Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
    {
        FakeXr Fake;
        auto& Kernel = Fake.Kernel;
        auto& State = dbc::VrSystemTestAccess::Get(Kernel.Vr());
        BuiltinContext Context;
        Context.Kernel = &Kernel;
        Context.Profile = Profile;
        std::string Error;
        const auto Command = [&](SymbolId Id, std::initializer_list<double> Values)
        {
            std::vector<BuiltinValue> Arguments;
            for (double Value : Values)
                Arguments.push_back(BuiltinValue::FromNumber(Value));
            REQUIRE_MESSAGE(InvokeCommand(Context, Id, Arguments, Error), Error);
        };
        const auto Query = [&](SymbolId Id, std::initializer_list<double> Values = {})
        {
            std::vector<BuiltinValue> Arguments;
            for (double Value : Values)
                Arguments.push_back(BuiltinValue::FromNumber(Value));
            BuiltinValue Result;
            REQUIRE_MESSAGE(InvokeFunction(Context, Id, Arguments, Result, Error), Error);
            return Result.Number;
        };
        CHECK(Query(SymbolId::Function_IsVrSupported) == 1);
        Command(SymbolId::Command_SetVrEnabled, {1});
        CHECK(Query(SymbolId::Function_IsVrEnabled) == 1);
        Command(SymbolId::Command_SetVrCameraHeight, {0.5});
        CHECK(Query(SymbolId::Function_GetVrCameraHeight) == 0.5);
        Command(SymbolId::Command_SetVrPreviewMode, {2});
        CHECK(State.Preview == VrPreviewMode::Right);
        Command(SymbolId::Command_SetTrackingSpace, {1});
        CHECK(State.Tracking.Space == VrTrackingSpace::LocalFloor);
        const auto Physical = glm::translate(glm::mat4(1), glm::vec3(1, 2, 3));
        State.Tracking.Update(0, Physical, true);
        State.Tracking.Update(1, Physical, true);
        State.Tracking.Update(2, Physical, true);
        CHECK(Query(SymbolId::Function_IsVrHeadTracked) == 1);
        CHECK(Query(SymbolId::Function_IsVrControllerTracked, {0}) == 1);
        Context.Vectors4[7] = {9, 9, 9, 9};
        Command(SymbolId::Command_GetVrHeadPosition, {7});
        CHECK(Context.Vectors4.count(7) == 0);
        CHECK(Query(SymbolId::Function_XVector3, {7}) == 1);
        CHECK(Query(SymbolId::Function_YVector3, {7}) == 2.5);
        CHECK(Query(SymbolId::Function_ZVector3, {7}) == 3);
        Command(SymbolId::Command_GetVrRotation, {7});
        CHECK(Context.Vectors.at(7) == std::array<float, 3>{0, 0, 0});
        Command(SymbolId::Command_GetVrControllerPosition, {1, 7});
        CHECK(Context.Vectors.at(7)[2] == 3);
        Command(SymbolId::Command_GetVrControllerRotation, {0, 7});
        CHECK(Context.Vectors.at(7)[1] == 0);
        Command(SymbolId::Command_RecenterVrCamera, {});
        Command(SymbolId::Command_GetVrHeadPosition, {7});
        CHECK(Context.Vectors.at(7)[0] == 0);
        CHECK(Context.Vectors.at(7)[1] == 2.5);
        for (auto Id :
             {SymbolId::Command_SetVrEnabled, SymbolId::Command_SetTrackingSpace, SymbolId::Command_SetVrPreviewMode})
            CHECK_FALSE(InvokeCommand(Context, Id, {BuiltinValue::FromNumber(99)}, Error));
        CHECK_FALSE(InvokeCommand(Context, SymbolId::Command_GetVrControllerPosition,
                                  {BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(7)}, Error));
        CHECK_FALSE(InvokeCommand(Context, SymbolId::Command_GetVrHeadPosition, {BuiltinValue::FromNumber(0)}, Error));
        CHECK_FALSE(Kernel.Vr().SetCameraHeight(std::numeric_limits<float>::infinity(), &Error));
        CHECK(Context.Vectors.at(7)[1] == 2.5);
        Command(SymbolId::Command_SetVrEnabled, {0});
        CHECK(Query(SymbolId::Function_IsVrEnabled) == 0);
        CHECK(Query(SymbolId::Function_IsVrHeadTracked) == 0);
        State.Supported = false;
        Command(SymbolId::Command_SetVrEnabled, {1});
        CHECK(Query(SymbolId::Function_IsVrEnabled) == 0);
        CHECK(Query(SymbolId::Function_IsVrSupported) == 0);
    }
}

TEST_CASE("VR capability detection includes compiled branches and source units without textual false positives")
{
    EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("capability", 64, 64));
    ExecutionService Service(Kernel);
    const auto Prepare = [&](const std::string& Text)
    {
        ExecutionRequest Request;
        Request.RootLogicalPath = "/virtual/main.dba";
        Request.Profile = LanguageProfile::Dbc;
        Request.Sources = {{Request.RootLogicalPath, Text},
                           {"/virtual/included.dba", "set vr preview mode 1\n"},
                           {"/virtual/unused.dba", "set vr enabled 1\n"}};
        REQUIRE_MESSAGE(Service.Prepare(std::move(Request)),
                        (Service.Result().Diagnostics.empty() ? "" : Service.Result().Diagnostics.front().Message));
    };
    Prepare("print \"set vr enabled 1\"\nrem set tracking space 2\nvrvalue=1\n");
    CHECK_FALSE(Kernel.Vr().Capable());
    Prepare("if 0 then set vr enabled 1\n");
    CHECK(Kernel.Vr().Capable());
    Prepare("function feature()\nendfunction is vr supported()\n");
    CHECK(Kernel.Vr().Capable());
    Prepare("#include \"included.dba\"\n");
    CHECK(Kernel.Vr().Capable());
    Prepare("print x vector3(1)\n");
    CHECK_FALSE(Kernel.Vr().Capable());
    Prepare("print 1\n");
    CHECK_FALSE(Kernel.Vr().Capable());
    CHECK(language::DetectDialect({"vr.dba", "set vr enabled 1\nprint x vector3(1)\n"}).Kind ==
          language::DialectDetectionKind::Ambiguous);
    CHECK(language::DetectDialect({"vr.dba", "set vr enabled 1\nbob 1,2,3,4\n"}).Kind ==
          language::DialectDetectionKind::Dbc);
    CHECK(language::DetectDialect({"vr.dba", "set vr enabled 1\nmake vector3 1\n"}).Kind ==
          language::DialectDetectionKind::DbPro);
}

TEST_CASE("XR session events keep enabled independent of focus and balance empty frames")
{
    FakeXr Fake;
    Fake.Enable();
    auto& Vr = Fake.Kernel.Vr();
    REQUIRE(Vr.RenderFrame(0.01, false));
    CHECK(Vr.Enabled());
    CHECK(Vr.Pacing());
    CHECK(Fake.Events == std::vector<std::string>{"wait", "begin", "views", "actions", "end"});
    CHECK(Fake.EndLayers == 0);
    CHECK(Fake.Acquires == 0);
    CHECK(Vr.HeadPose().Tracked);
    const auto Last = Vr.HeadPose().Position;
    Fake.LostTracking = true;
    Fake.Focused = false;
    REQUIRE(Vr.RenderFrame(0.01, true));
    CHECK(Vr.Enabled());
    CHECK_FALSE(Vr.HeadPose().Tracked);
    CHECK(Vr.HeadPose().Position == Last);
    REQUIRE(Vr.SetTrackingSpace(2));
    CHECK(VrSystemTestAccess::Get(Vr).Tracking.Space == VrTrackingSpace::Local);
    REQUIRE(Vr.SetTrackingSpace(1));
    CHECK(VrSystemTestAccess::Get(Vr).Tracking.Space == VrTrackingSpace::LocalFloor);
    Fake.Stage = true;
    Fake.LostTracking = false;
    Fake.Focused = true;
    REQUIRE(Vr.RenderFrame(0.01, true));
    REQUIRE(Vr.SetCameraHeight(0.3f));
    Vr.RecenterCamera();
    REQUIRE(Vr.SetTrackingSpace(2));
    auto& Tracking = VrSystemTestAccess::Get(Vr).Tracking;
    CHECK(Tracking.Space == VrTrackingSpace::Stage);
    CHECK(Tracking.Height == 0.3f);
    CHECK(Near(Tracking.Recenter, glm::mat4(1)));
    CHECK(Vr.HeadPose().Position == glm::vec3(0));
    REQUIRE(Vr.RenderFrame(0.01, true));
    CHECK(Vr.HeadPose().Tracked);
    Fake.Kernel.Core().DoSyncRate(1);
    CHECK_FALSE(Fake.Kernel.Core().NextSyncDeadline().has_value());
    Fake.States.push_back(XR_SESSION_STATE_STOPPING);
    CHECK_FALSE(Vr.RenderFrame(0.01, true));
    CHECK(Vr.Enabled());
    CHECK_FALSE(Vr.Pacing());
    REQUIRE(Vr.SetEnabled(0));
    CHECK(Fake.Destroyed == 1);
    Fake.Enable();
    CHECK(Fake.Sessions == 2);
    Fake.States.push_back(XR_SESSION_STATE_LOSS_PENDING);
    Vr.RenderFrame(0.01, true);
    CHECK_FALSE(Vr.Enabled());
    REQUIRE(Vr.SetTrackingSpace(1));
    REQUIRE(Vr.SetTrackingSpace(2));
    Fake.Stage = false;
    Fake.Enable();
    CHECK(Tracking.Space == VrTrackingSpace::LocalFloor);
    CHECK(Tracking.Height == 0.3f);
}

TEST_CASE("XR partial acquisitions release only images whose wait succeeded")
{
    for (int Failure = 0; Failure < 3; ++Failure)
    {
        FakeXr Fake;
        Fake.NoRender = false;
        if (Failure < 2)
            Fake.FailWait = Failure;
        else
            Fake.FailAcquire = 1;
        Fake.Enable();
        REQUIRE(Fake.Kernel.Vr().RenderFrame(0.01, false));
        CHECK_FALSE(Fake.Kernel.Vr().Enabled());
        CHECK(Fake.Releases == (Failure == 0 ? 0 : 1));
        CHECK(Fake.EndLayers == 0);
        CHECK(Fake.Destroyed == 1);
    }
    {
        FakeXr Failed;
        Failed.FailSession = true;
        REQUIRE(Failed.Kernel.Vr().SetEnabled(1));
        CHECK_FALSE(Failed.Kernel.Vr().Enabled());
        CHECK(Failed.Destroyed == 0);
        Failed.FailSession = false;
        Failed.Enable();
        REQUIRE(Failed.Kernel.Vr().SetEnabled(1));
        CHECK(Failed.Sessions == 1);
        Failed.NoRender = false;
        Failed.InvalidViews = true;
        REQUIRE(Failed.Kernel.Vr().RenderFrame(0.01, true));
        CHECK(Failed.Acquires == 0);
        CHECK(Failed.EndLayers == 0);
    }
    FakeXr Fake;
    Fake.FailSecondSwapchain = true;
    REQUIRE(Fake.Kernel.Vr().SetEnabled(1));
    CHECK_FALSE(Fake.Kernel.Vr().Enabled());
    CHECK(Fake.Destroyed == 1);
}

TEST_CASE("XR stereo GPU frames share one upload and retain a gamma corrected capture while minimized")
{
    FakeXr Fake;
    Fake.Enable(true);
    Fake.NoRender = false;
    auto& K = Fake.Kernel;
    K.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(K.Core().SetWindowSize(80, 60));
    K.Camera().SetAutoCamOff();
    REQUIRE(K.Camera().PositionCamera(0, 0, 0));
    REQUIRE(K.Camera().SetBackdropColor(0x808080));
    K.Renderer().SetGamma(128, 255, 255);
    K.Objects().MakeObjectCube(1, 0.4f);
    K.Objects().PositionObject(1, 0, 0, 2);
    REQUIRE(K.Objects().SetObjectKeyframe(1, 0));
    REQUIRE(K.Objects().SetObjectKeyframe(1, 100));
    REQUIRE(K.Objects().SetObjectFrame(1, 0));
    REQUIRE(K.Objects().PlayObject(1));
    const auto Camera = K.Renderer().CameraView(80, 60).WorldToView;
    REQUIRE(K.Vr().RenderFrame(0.1, true));
    CHECK(Fake.EndLayers == 1);
    CHECK(Fake.Releases == 2);
    CHECK(K.Objects().GetObject(1)->AnimTimeSeconds == doctest::Approx(0.1));
    auto& State = VrSystemTestAccess::Get(K.Vr());
    // Each eye's block holds its vertices, then the instance records of its GPU meshes.
    CHECK(State.Geometry[0].MeshInstances == 1);
    CHECK(State.Geometry[1].VertexOffset == State.Geometry[0].Vertices.size() * sizeof(dbc::GpuVertex) +
                                                State.Geometry[0].MeshInstances * sizeof(dbc::GpuMeshInstance));
    CHECK(Near(K.Renderer().CameraView(80, 60).WorldToView, Camera));
    auto* Surface = K.Renderer().CaptureScreen();
    REQUIRE(Surface != nullptr);
    Uint8 R = 0, G = 0, B = 0, A = 0;
    REQUIRE(SDL_ReadSurfacePixel(Surface, Surface->w / 4, Surface->h / 2 + Surface->h / 8, &R, &G, &B, &A));
    CHECK(R == doctest::Approx(64).epsilon(0.04));
    CHECK(G == doctest::Approx(128).epsilon(0.04));
    CHECK(B == G);
    SDL_DestroySurface(Surface);
    CHECK(K.Objects().GetObject(1)->AnimTimeSeconds == doctest::Approx(0.1));
    SDL_Event Event{};
    Event.type = SDL_EVENT_WINDOW_MINIMIZED;
    Event.window.windowID = SDL_GetWindowID(K.Core().Window());
    K.Input().HandleEvent(Event);
    CHECK_FALSE(K.Core().Suspended());
    Fake.NoRender = true;
    REQUIRE(K.Vr().RenderFrame(0.1, true));
    CHECK(Fake.EndLayers == 0);
    Surface = K.Renderer().CaptureScreen();
    REQUIRE(Surface != nullptr);
    SDL_DestroySurface(Surface);
    CHECK(K.Objects().GetObject(1)->AnimTimeSeconds == doctest::Approx(0.1));
}

TEST_CASE("VR uses both historical camera rotation orders and preserves rig settings across camera selection")
{
    FakeXr Fake;
    auto& K = Fake.Kernel;
    auto& Tracking = VrSystemTestAccess::Get(K.Vr()).Tracking;
    REQUIRE(K.Camera().MakeCamera(1));
    REQUIRE(K.Camera().PositionCamera(1, 5, 6, 7));
    REQUIRE(K.Camera().RotateCamera(1, 20, 35, 15));
    REQUIRE(K.Camera().SetCurrentCamera(1));
    REQUIRE(K.Vr().SetCameraHeight(0.25f));
    REQUIRE(K.Vr().SetTrackingSpace(1));
    Tracking.Update(0, glm::translate(glm::mat4(1), glm::vec3(1, 1.6f, 2)), true);
    K.Vr().RecenterCamera();
    for (bool Zyx : {false, true})
    {
        REQUIRE((Zyx ? K.Camera().SetRotationZyx() : K.Camera().SetRotationXyz()));
        const auto Player = glm::inverse(K.Renderer().CameraView(80, 60).WorldToView);
        const auto Pose = K.Vr().HeadPose();
        const auto Expected = Player * Tracking.Origin() * Tracking.Poses[0];
        CHECK(glm::length(Pose.Position - glm::vec3(Expected[3])) < 0.0001f);
        const auto Euler = glm::radians(Pose.Rotation);
        const auto Rotation = glm::rotate(glm::mat4(1), Euler.y, glm::vec3(0, 1, 0)) *
                              glm::rotate(glm::mat4(1), Euler.x, glm::vec3(1, 0, 0)) *
                              glm::rotate(glm::mat4(1), Euler.z, glm::vec3(0, 0, 1));
        CHECK(Near(glm::mat4(glm::mat3(Expected)), Rotation));
        Tracking.Update(0, glm::mat4(0), false);
        REQUIRE(K.Camera().MoveCamera(1));
        const auto Moved = K.Vr().HeadPose();
        CHECK_FALSE(Moved.Tracked);
        CHECK(glm::length(Moved.Position - Pose.Position) == doctest::Approx(1));
        Tracking.Update(0, Tracking.Poses[0], true);
    }
    const auto Recenter = Tracking.Recenter;
    REQUIRE(K.Camera().SetCurrentCamera(0));
    CHECK(Tracking.Space == VrTrackingSpace::LocalFloor);
    CHECK(K.Vr().CameraHeight() == 0.25f);
    CHECK(Near(Tracking.Recenter, Recenter));
    Tracking.Update(0, glm::translate(glm::mat4(1), glm::vec3(2, 2, 3)), true);
    CHECK(K.Vr().HeadPose().Tracked);
}

TEST_CASE("Missing XR loader recreates the ordinary device before graphics resources")
{
    const char* Hint = SDL_GetHint(SDL_HINT_OPENXR_LIBRARY);
    const std::optional<std::string> Previous = Hint ? std::optional<std::string>(Hint) : std::nullopt;

    struct HintScope
    {
        const std::optional<std::string>& Previous;

        ~HintScope()
        {
            if (Previous)
                SDL_SetHint(SDL_HINT_OPENXR_LIBRARY, Previous->c_str());
            else
                SDL_ResetHint(SDL_HINT_OPENXR_LIBRARY);
        }
    } Scope{Previous};
#if defined(__APPLE__)
    constexpr auto LibraryWithoutXrExports = "/usr/lib/libSystem.B.dylib";
#elif defined(_WIN32)
    constexpr auto LibraryWithoutXrExports = "kernel32.dll";
#else
    constexpr auto LibraryWithoutXrExports = "libc.so.6";
#endif
    for (const char* Loader : {"/missing-opendbc-openxr-loader", LibraryWithoutXrExports})
    {
        SDL_SetHint(SDL_HINT_OPENXR_LIBRARY, Loader);
        EngineKernel K;
        REQUIRE(K.Initialize("vr-fallback", 64, 64));
        REQUIRE(K.Vr().Configure(true));
        REQUIRE(K.Core().EnsureInitialized());
        CHECK_FALSE(K.Vr().Supported());
        REQUIRE(K.Vr().SetEnabled(1));
        CHECK_FALSE(K.Vr().Enabled());
#if defined(__APPLE__)
        CHECK(std::string(SDL_GetGPUDeviceDriver(K.Gpu().GpuDevice())) == "metal");
#endif
        auto* Surface = K.Renderer().CaptureScreen();
        REQUIRE(Surface != nullptr);
        SDL_DestroySurface(Surface);
    }
}

TEST_CASE("XR direct and converted eyes preserve HUD pixels and independent depth with no capture simulation")
{
    for (int Mode = 0; Mode < 3; ++Mode)
    {
        FakeXr Fake;
        Fake.UseWindowFormat = Mode == 0;
        Fake.RefuseCopy = Mode == 2;
        Fake.Enable(true);
        auto& K = Fake.Kernel;
        auto& State = VrSystemTestAccess::Get(K.Vr());
        REQUIRE(K.Core().SetWindowSize(80, 60));
        K.Camera().SetAutoCamOff();
        REQUIRE(K.Camera().PositionCamera(0, 0, 0));
        REQUIRE(K.Camera().SetCameraRange(0.05f, 10));
        REQUIRE(K.Camera().SetBackdropColor(0x808080));
        // Capture before the first frame must be black and must not wait/begin XR.
        auto* Black = K.Renderer().CaptureScreen();
        REQUIRE(Black != nullptr);
        Uint8 R = 0, G = 0, B = 0, A = 0;
        REQUIRE(SDL_ReadSurfacePixel(Black, 0, 0, &R, &G, &B, &A));
        CHECK(R == 0);
        CHECK(G == 0);
        CHECK(B == 0);
        SDL_DestroySurface(Black);
        CHECK(Fake.Events.empty());
        K.Renderer().SetGamma(Mode ? 128 : 255, 255, 255);
        K.Draw().SetInk(0xFF0000);
        REQUIRE(K.Draw().DrawBox(0, 0, 10, 10));
        K.Objects().MakeObjectCube(1, 0.4f);
        K.Objects().PositionObject(1, 0, 0, 2);
        REQUIRE(K.Objects().SetShadowShadingOn(1));
        REQUIRE(K.Objects().MakeObjectPlain(2, 2, 2));
        K.Objects().PositionObject(2, 0, 0, 4);
        REQUIRE(K.Objects().SetReflectionShadingOn(2));
        Fake.NoRender = false;
        REQUIRE(K.Vr().RenderFrame(0.01, true));
        CHECK(State.Geometry[0].HasReflection);
        CHECK(State.Geometry[1].HasReflection);
        CHECK(State.Geometry[0].HasShadow);
        CHECK(State.Geometry[1].HasShadow);
        CHECK(State.CopySource == (Mode != 2));
        std::array<GpuRenderTarget, 2> Targets{};
        REQUIRE(State.Presenter.Targets({Fake.Images[0][0], Fake.Images[1][0]}, State.Format, State.CopySource,
                                        {64, 64}, {64, 64}, Targets));
        CHECK(Targets[0].Depth != Targets[1].Depth);
        CHECK((Targets[0].Color == Fake.Images[0][0]) == (Mode == 0));
        // Read the actual runtime textures, not the intermediate preview source.
        auto* Device = K.Gpu().GpuDevice();
        SDL_GPUTransferBufferCreateInfo Info{};
        Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        Info.size = 64 * 64 * 4 * 2;
        auto* Buffer = SDL_CreateGPUTransferBuffer(Device, &Info);
        REQUIRE(Buffer != nullptr);
        GpuCommandBuffer Commands(SDL_AcquireGPUCommandBuffer(Device));
        REQUIRE(Commands.Get() != nullptr);
        auto* Copy = SDL_BeginGPUCopyPass(Commands.Get());
        REQUIRE(Copy != nullptr);
        for (int Eye = 0; Eye < 2; ++Eye)
        {
            SDL_GPUTextureRegion Source{};
            Source.texture = Fake.Images[Eye][0];
            Source.w = Source.h = 64;
            Source.d = 1;
            SDL_GPUTextureTransferInfo Destination{};
            Destination.transfer_buffer = Buffer;
            Destination.offset = Eye * 64 * 64 * 4;
            SDL_DownloadFromGPUTexture(Copy, &Source, &Destination);
        }
        SDL_EndGPUCopyPass(Copy);
        auto* Fence = Commands.SubmitAndAcquireFence();
        REQUIRE(Fence != nullptr);
        REQUIRE(SDL_WaitForGPUFences(Device, true, &Fence, 1));
        SDL_ReleaseGPUFence(Device, Fence);
        auto* Bytes = static_cast<const Uint8*>(SDL_MapGPUTransferBuffer(Device, Buffer, false));
        REQUIRE(Bytes != nullptr);
        const bool Bgra = State.Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                          State.Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
        for (int Eye = 0; Eye < 2; ++Eye)
        {
            const auto* Pixel = Bytes + (Eye * 64 * 64 + 4 * 64 + 4) * 4;
            CHECK(Pixel[Bgra ? 2 : 0] == doctest::Approx(Mode ? 128 : 255).epsilon(0.01));
            CHECK(Pixel[1] == 0);
            CHECK(Pixel[Bgra ? 0 : 2] == 0);
        }
        SDL_UnmapGPUTransferBuffer(Device, Buffer);
        SDL_ReleaseGPUTransferBuffer(Device, Buffer);
        const auto EventCount = Fake.Events.size();
        auto* Capture = K.Renderer().CaptureScreen();
        REQUIRE(Capture != nullptr);
        SDL_DestroySurface(Capture);
        CHECK(Fake.Events.size() == EventCount);
        Fake.NoRender = true;
        K.Core().DoSyncRate(1);
        K.Core().Sync();
        CHECK_FALSE(K.Core().NextSyncDeadline().has_value());
        REQUIRE(K.Vr().SetEnabled(0));
        CHECK(K.Core().NextSyncDeadline().has_value());
    }
}

TEST_CASE("XR presentation continues while BASIC waits and a minimized host stays suspended")
{
    for (const std::string Wait : {"wait 100000", "sleep 100000", "wait key", "suspend for mouse", "input answer$"})
    {
        CAPTURE(Wait);
        FakeXr Fake;
        ExecutionService Service(Fake.Kernel);
        ExecutionRequest Request;
        Request.RootLogicalPath = "/virtual/vr_wait.dba";
        Request.Profile = LanguageProfile::Dbc;
        Request.Sources = {{Request.RootLogicalPath, "set vr enabled 1\nsync on\n" + Wait + "\nend\n"}};
        REQUIRE(Service.Prepare(std::move(Request)));
        Fake.Enable(true);
        auto Tick = Service.Tick();
        REQUIRE(Tick.State == ExecutionState::Suspended);
        const auto Reason = Tick.Suspension.Reason;
        Fake.Kernel.Renderer().RenderScene(true);
        REQUIRE(Fake.Kernel.Vr().Pacing());
        CHECK(Fake.EndLayers == 0);
        Fake.Kernel.Core().SetSuspended(true);
        Tick = Service.Tick();
        CHECK(Tick.State == ExecutionState::Suspended);
        CHECK(Tick.Suspension.Reason == Reason);
        const auto Frames = std::count(Fake.Events.begin(), Fake.Events.end(), "end");
        Fake.Kernel.Renderer().RenderScene(true);
        CHECK(std::count(Fake.Events.begin(), Fake.Events.end(), "end") == Frames + 1);
        Service.RequestStop();
        Service.Tick();
        CHECK(Service.Result().Finished());
    }
}

TEST_CASE("XR pacing cancels an already pending SYNC RATE deadline")
{
    FakeXr Fake;
    Fake.Enable(true);
    auto& K = Fake.Kernel;
    K.Core().DoSyncRate(1);
    K.Core().Sync();
    auto& State = VrSystemTestAccess::Get(K.Vr());
    State.Running = false;
    BuiltinContext Context;
    Context.Kernel = &K;
    Context.Profile = LanguageProfile::Dbc;
    EngineBuiltinHost Host(Context);
    vm::BuiltinInvocation Sync{1, SymbolId::Command_Sync, {}, {language::TypeKind::Void, {}}, false};
    const auto Pending = Host.Invoke(Sync);
    REQUIRE(Pending.State == vm::BuiltinState::Pending);
    REQUIRE(Pending.Wait.Kind == vm::SuspensionKind::Deadline);
    Fake.States.push_back(XR_SESSION_STATE_READY);
    K.Renderer().RenderScene(true);
    REQUIRE(K.Vr().Pacing());
    Sync.IsRetry = true;
    const auto Resumed = Host.Invoke(Sync);
    CHECK(Resumed.State == vm::BuiltinState::CompletedAndSuspend);
    CHECK(Resumed.Wait.Kind == vm::SuspensionKind::FrameBoundary);
}

TEST_CASE("XR preview selects either eye with black letterboxing and keeps a minimized window alive")
{
    FakeXr Fake;
    Fake.UseWindowFormat = true;
    Fake.Enable(true);
    auto& K = Fake.Kernel;
    REQUIRE(K.Core().SetWindowSize(80, 60));
    auto& State = VrSystemTestAccess::Get(K.Vr());
    const std::array<SDL_GPUTexture*, 2> Images{Fake.Images[0][0], Fake.Images[1][0]};
    std::array<GpuRenderTarget, 2> Targets{};
    REQUIRE(State.Presenter.Targets(Images, State.Format, true, {64, 64}, {64, 64}, Targets));
    int Width = 0, Height = 0;
    for (const auto Mode : {VrPreviewMode::Both, VrPreviewMode::Left, VrPreviewMode::Right})
    {
        GpuCommandBuffer Commands(SDL_AcquireGPUCommandBuffer(K.Gpu().GpuDevice()));
        REQUIRE(Commands.Get() != nullptr);
        for (int Eye = 0; Eye < 2; ++Eye)
        {
            SDL_GPUColorTargetInfo Color{};
            Color.texture = Targets[Eye].Color;
            Color.clear_color = Eye ? SDL_FColor{0, 1, 0, 1} : SDL_FColor{1, 0, 0, 1};
            Color.load_op = SDL_GPU_LOADOP_CLEAR;
            Color.store_op = SDL_GPU_STOREOP_STORE;
            auto* Pass = SDL_BeginGPURenderPass(Commands.Get(), &Color, 1, nullptr);
            REQUIRE(Pass != nullptr);
            SDL_EndGPURenderPass(Pass);
        }
        REQUIRE(State.Presenter.Present(Commands.Get(), Images, Mode));
        REQUIRE(Commands.Submit());
        State.Presenter.Commit();
        auto* Surface = K.Renderer().CaptureScreen();
        REQUIRE(Surface != nullptr);
        Width = Surface->w;
        Height = Surface->h;
        Uint8 R = 0, G = 0, B = 0, A = 0;
        REQUIRE(SDL_ReadSurfacePixel(Surface, 0, 0, &R, &G, &B, &A));
        CHECK(R == 0);
        CHECK(G == 0);
        CHECK(B == 0);
        REQUIRE(SDL_ReadSurfacePixel(Surface, Width / 4, Height / 2, &R, &G, &B, &A));
        CHECK(R == (Mode == VrPreviewMode::Right ? 0 : 255));
        CHECK(G == (Mode == VrPreviewMode::Right ? 255 : 0));
        REQUIRE(SDL_ReadSurfacePixel(Surface, 3 * Width / 4, Height / 2, &R, &G, &B, &A));
        CHECK(R == (Mode == VrPreviewMode::Left ? 255 : 0));
        CHECK(G == (Mode == VrPreviewMode::Left ? 0 : 255));
        SDL_DestroySurface(Surface);
    }
    REQUIRE(SDL_MinimizeWindow(K.Core().Window()));
    Fake.NoRender = false;
    REQUIRE(K.Vr().RenderFrame(0.01, true));
    CHECK(Fake.EndLayers == 1);
    CHECK(K.Vr().Enabled());
    auto* Surface = K.Renderer().CaptureScreen();
    REQUIRE(Surface != nullptr);
    CHECK(Surface->w == Width);
    CHECK(Surface->h == Height);
    SDL_DestroySurface(Surface);
}
