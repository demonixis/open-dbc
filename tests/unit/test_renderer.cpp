#include <doctest/doctest.h>

#include "engine/systems/camera_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/gpu_command_buffer.hpp"
#include "engine/systems/graphics_device_system.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/text_system.hpp"
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace
{
    int Submissions = 0;
    int Cancellations = 0;
    bool SDLCALL Submit(SDL_GPUCommandBuffer*)
    {
        ++Submissions;
        return true;
    }
    bool SDLCALL Cancel(SDL_GPUCommandBuffer*)
    {
        ++Cancellations;
        return true;
    }
} // namespace

TEST_CASE("GPU command ownership submits acquired window images even on abandonment")
{
    Submissions = 0;
    Cancellations = 0;
    auto* Handle = reinterpret_cast<SDL_GPUCommandBuffer*>(std::uintptr_t{1});
    const dbc::GpuCommandBuffer::Operations Calls{Submit, Cancel};
    {
        dbc::GpuCommandBuffer Commands(Handle, Calls);
    }
    CHECK(Cancellations == 1);
    CHECK(Submissions == 0);
    {
        dbc::GpuCommandBuffer Commands(Handle, Calls);
        Commands.SetSwapchainAcquired();
        Commands.Abandon();
        Commands.Abandon();
    }
    CHECK(Cancellations == 1);
    CHECK(Submissions == 1);
    {
        dbc::GpuCommandBuffer Commands(Handle, Calls);
        Commands.SetSwapchainAcquired();
        REQUIRE(Commands.Submit());
    }
    CHECK(Submissions == 2);
    CHECK(Cancellations == 1);
}

TEST_CASE("Render views share asymmetric projection between CPU culling and HiDPI viewport")
{
    const glm::mat4 Projection = glm::frustumLH_ZO(-0.5f, 1.5f, -1.0f, 1.0f, 1.0f, 100.0f);
    const dbc::RenderView View{glm::mat4(1.0f), Projection, Projection, {10.0f, 15.0f, 40.0f, 30.0f}, 80, 60, 160, 120};
    REQUIRE(View.Valid());
    const auto Viewport = View.PhysicalViewport();
    CHECK(Viewport.x == 20);
    CHECK(Viewport.y == 30);
    CHECK(Viewport.w == 80);
    CHECK(Viewport.h == 60);
    const auto Scissor = View.PhysicalScissor();
    CHECK(Scissor.x == 20);
    CHECK(Scissor.y == 30);
    CHECK(Scissor.w == 80);
    CHECK(Scissor.h == 60);
    const glm::vec3 Point{0.5f, 0.0f, 2.0f};
    SDL_FPoint Pixel{};
    REQUIRE(View.Project(Point, Pixel));
    const auto Clip = Projection * glm::vec4(Point, 1.0f);
    CHECK(Pixel.x == doctest::Approx(10.0f + (Clip.x / Clip.w + 1.0f) * 20.0f));
    CHECK(Pixel.y == 30.0f);
    CHECK(View.ContainsSphere(Point, 0.1f));
    CHECK_FALSE(View.ContainsSphere({-4.0f, 0.0f, 2.0f}, 0.1f));
    CHECK_FALSE(View.ContainsSphere({0.0f, 0.0f, 101.0f}, 0.1f));
    const dbc::RenderView Resized{View.WorldToView, Projection, Projection, View.LogicalViewport, 80, 60, 240, 180};
    CHECK_FALSE(View == Resized);
}

TEST_CASE("Two borrowed GPU views advance simulation once and finalize once")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("render-views", 80, 60));
    Kernel.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Classic());
    REQUIRE(Kernel.Core().EnsureInitialized());
    std::string InitialSizeError;
    REQUIRE(Kernel.Core().SetWindowSize(80, 60, &InitialSizeError));
    Kernel.Camera().SetAutoCamOff();
    REQUIRE_MESSAGE(Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &InitialSizeError), InitialSizeError);
    REQUIRE_MESSAGE(Kernel.Camera().SetCameraView(20, 15, 60, 45, &InitialSizeError), InitialSizeError);
    REQUIRE_MESSAGE(Kernel.Camera().SetBackdropColor(0, &InitialSizeError), InitialSizeError);
    Kernel.Objects().MakeObjectCube(1, 1.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectLight(1, 0));
    Kernel.Objects().ColorObject(1, 0x00FF00);
    REQUIRE(Kernel.Objects().SetObjectKeyframe(1, 0));
    REQUIRE(Kernel.Objects().SetObjectKeyframe(1, 100));
    REQUIRE(Kernel.Objects().SetObjectFrame(1, 0));
    REQUIRE(Kernel.Objects().PlayObject(1));
    Kernel.Objects().MakeObjectCube(2, 1.0f);
    REQUIRE(Kernel.Objects().SetObjectKeyframe(2, 0));
    REQUIRE(Kernel.Objects().SetObjectKeyframe(2, 100));
    REQUIRE(Kernel.Objects().SetObjectFrame(2, 0));
    REQUIRE(Kernel.Objects().PlayObject(2));
    Kernel.Objects().HideObject(2);
    REQUIRE(Kernel.Objects().MakeObjectPlain(3, 2.0f, 2.0f));
    Kernel.Objects().PositionObject(3, 0.0f, 0.0f, 7.0f);
    REQUIRE(Kernel.Objects().SetReflectionShadingOn(3));
    REQUIRE(Kernel.Objects().MakeParticles(1, 0, 50, 2.0f));
    REQUIRE(Kernel.Objects().PositionParticles(1, 0.0f, 0.0f, -10.0f));
    REQUIRE(Kernel.Gpu().EnsureGpuDepthTarget(160, 120));
    REQUIRE(Kernel.Gpu().EnsureGpuCaptureTarget(160, 120));
    auto* Device = Kernel.Gpu().GpuDevice();
    const auto Format = SDL_GetGPUSwapchainTextureFormat(Device, Kernel.Core().Window());
    const dbc::GpuRenderTarget Target{Kernel.Gpu().GpuCaptureTexture(),
                                      Kernel.Gpu().GpuDepthTexture(),
                                      Format,
                                      Kernel.Gpu().GpuDepthFormat(),
                                      160,
                                      120};
    Kernel.Text().DrawText(0, 0, "same frame");
    const auto QueuedTextCount = [&]()
    {
        int Count = 0;
        Kernel.Text().ForEachDebugTextLine([&](const auto&) { ++Count; });
        return Count;
    };
    auto Frame = Kernel.Renderer().PrepareFrame(0.1);
    CHECK(QueuedTextCount() == 1);
    const auto* Emitter = Kernel.Objects().GetParticles(1);
    REQUIRE(Emitter != nullptr);
    const auto SimulationTime = Emitter->SimulationTime;
    const auto RandomState = Emitter->RandomState;
    CHECK(SimulationTime == doctest::Approx(0.00503f));
    CHECK(Kernel.Objects().GetObject(1)->AnimTimeSeconds == doctest::Approx(0.1));
    CHECK(Kernel.Objects().GetObject(2)->AnimTimeSeconds == 0.0);
    const auto FirstView = Kernel.Renderer().CameraView(160, 120);
    REQUIRE_MESSAGE(Kernel.Camera().PositionCamera(20.0f, 0.0f, 0.0f, &InitialSizeError), InitialSizeError);
    const auto SecondView = Kernel.Renderer().CameraView(160, 120);
    SDL_GPUTransferBufferCreateInfo TransferInfo{};
    TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    TransferInfo.size = 512;
    auto* Transfer = SDL_CreateGPUTransferBuffer(Device, &TransferInfo);
    REQUIRE(Transfer != nullptr);
    struct TransferCleanup
    {
        SDL_GPUDevice* Device;
        SDL_GPUTransferBuffer* Transfer;
        ~TransferCleanup()
        {
            SDL_ReleaseGPUTransferBuffer(Device, Transfer);
        }
    } Cleanup{Device, Transfer};
    auto* RawCommands = SDL_AcquireGPUCommandBuffer(Device);
    REQUIRE(RawCommands != nullptr);
    dbc::GpuCommandBuffer Commands(RawCommands);
    const auto CaptureCenter = [&](Uint32 Offset)
    {
        auto* Copy = SDL_BeginGPUCopyPass(RawCommands);
        REQUIRE(Copy != nullptr);
        SDL_GPUTextureRegion Region{};
        Region.texture = Target.Color;
        Region.x = 80;
        Region.y = 60;
        Region.w = Region.h = Region.d = 1;
        SDL_GPUTextureTransferInfo Destination{};
        Destination.transfer_buffer = Transfer;
        Destination.offset = Offset;
        SDL_DownloadFromGPUTexture(Copy, &Region, &Destination);
        SDL_EndGPUCopyPass(Copy);
    };
    REQUIRE(Kernel.Renderer().RenderPreparedView(FirstView, Target, RawCommands, Frame));
    const auto FirstTriangles = Kernel.Renderer().CachedWorldTris().size();
    CHECK(FirstTriangles > 0);
    CaptureCenter(0);
    REQUIRE(Kernel.Renderer().RenderPreparedView(SecondView, Target, RawCommands, Frame));
    CHECK(Kernel.Renderer().CachedWorldTris().empty());
    CaptureCenter(256);
    CHECK(Frame.Views == 2);
    CHECK(QueuedTextCount() == 1);
    CHECK(Emitter->SimulationTime == SimulationTime);
    CHECK(Emitter->RandomState == RandomState);
    CHECK(Kernel.Objects().GetObject(1)->AnimTimeSeconds == doctest::Approx(0.1));
    REQUIRE(Commands.Submit());
    REQUIRE(SDL_WaitForGPUIdle(Device));
    auto* Pixels = static_cast<const Uint8*>(SDL_MapGPUTransferBuffer(Device, Transfer, false));
    REQUIRE(Pixels != nullptr);
    CHECK(Pixels[1] > 200);
    CHECK(Pixels[0] < 20);
    CHECK(Pixels[2] < 20);
    CHECK(Pixels[256] < 20);
    CHECK(Pixels[257] < 20);
    CHECK(Pixels[258] < 20);
    SDL_UnmapGPUTransferBuffer(Device, Transfer);
    Kernel.Renderer().FinalizeFrame(Frame);
    CHECK(QueuedTextCount() == 0);
    const auto Frames = Kernel.Core().FramesThisSecond();
    Kernel.Renderer().FinalizeFrame(Frame);
    CHECK(Kernel.Core().FramesThisSecond() == Frames);
    CHECK(Frame.Finalized);
    CHECK_FALSE(Kernel.Renderer().RenderPreparedView(FirstView, Target, nullptr, Frame));
    std::string ResizeError;
    REQUIRE(Kernel.Core().SetWindowSize(100, 60, &ResizeError));
    CHECK_FALSE(Kernel.Renderer().HasCachedTris());
    const auto NextFrame = Kernel.Renderer().PrepareFrame(0.1);
    CHECK(Kernel.Objects().GetObject(1)->AnimTimeSeconds == doctest::Approx(0.2));
    CHECK(Kernel.Objects().GetObject(2)->AnimTimeSeconds == 0.0);
    CHECK_FALSE(NextFrame.Finalized);
}

TEST_CASE("Failed GPU submission consumes ownership without retry or cancellation")
{
    Submissions = 0;
    Cancellations = 0;
    auto* Handle = reinterpret_cast<SDL_GPUCommandBuffer*>(std::uintptr_t{1});
    const dbc::GpuCommandBuffer::Operations Calls{+[](SDL_GPUCommandBuffer*)
                                                  {
                                                      ++Submissions;
                                                      return false;
                                                  },
                                                  Cancel};
    {
        dbc::GpuCommandBuffer Commands(Handle, Calls);
        Commands.SetSwapchainAcquired();
        CHECK_FALSE(Commands.Submit());
        CHECK(Commands.Get() == nullptr);
        CHECK_FALSE(Commands.Submit());
    }
    CHECK(Submissions == 1);
    CHECK(Cancellations == 0);
}

TEST_CASE("Render view bounds reject overflow before SDL viewport conversion")
{
    const glm::mat4 Identity(1.0f);
    const dbc::RenderView Wide{
        Identity, Identity, Identity, {0, 0, 1, 1}, 1, 1, static_cast<Uint32>(std::numeric_limits<int>::max()), 1};
    REQUIRE(Wide.Valid());
    CHECK(Wide.PhysicalScissor().w == std::numeric_limits<int>::max());
    const dbc::RenderView Overflow{Identity, Identity, Identity, {std::numeric_limits<float>::max(), 0, 1, 1},
                                   1,        1,        160,      120};
    CHECK_FALSE(Overflow.Valid());
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("wide-view", 80, 60));
    REQUIRE(Kernel.Camera().SetCameraView(std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max(), 60));
    const auto CameraView = Kernel.Renderer().CameraView(160, 120);
    REQUIRE(CameraView.Valid());
    CHECK(CameraView.LogicalViewport.w > 4.0e9f);
    CHECK(CameraView.PhysicalScissor().w == 160);
}

TEST_CASE("GPU initialization failure releases partial state and permits retry")
{
    const char* Previous = SDL_GetHint(SDL_HINT_GPU_DRIVER);
    const std::optional<std::string> PreviousDriver = Previous ? std::optional<std::string>(Previous) : std::nullopt;
    struct HintCleanup
    {
        std::optional<std::string> Driver;
        void Restore() const
        {
            SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, Driver ? Driver->c_str() : nullptr, SDL_HINT_OVERRIDE);
        }
        ~HintCleanup()
        {
            Restore();
        }
    } Hint{PreviousDriver};
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    struct VideoCleanup
    {
        ~VideoCleanup()
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    } Video;
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("initialization", 80, 60));
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, "opendbc-missing-driver", SDL_HINT_OVERRIDE));
    CHECK_FALSE(Kernel.Core().EnsureInitialized());
    CHECK_FALSE(Kernel.Core().Initialized());
    CHECK(Kernel.Core().Window() == nullptr);
    CHECK(Kernel.Gpu().GpuDevice() == nullptr);
    CHECK(Kernel.Gpu().Renderer() == nullptr);
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0);
    Hint.Restore();
    REQUIRE(Kernel.Core().EnsureInitialized());
    CHECK(Kernel.Gpu().GpuPipeline() != nullptr);
    REQUIRE(Kernel.Initialize("reinitialization", 80, 60));
    CHECK_FALSE(Kernel.Core().Initialized());
    CHECK(Kernel.Gpu().GpuDevice() == nullptr);
    REQUIRE(Kernel.Core().EnsureInitialized());
}
