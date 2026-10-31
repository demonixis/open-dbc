#include "vr_presenter.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "gpu_command_buffer.hpp"
#include "graphics_device_system.hpp"
#include "log.hpp"
#include "renderer_system.hpp"
#include <limits>
#include <utility>

namespace dbc
{
    namespace
    {
        bool Srgb(SDL_GPUTextureFormat Format)
        {
            return Format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB ||
                   Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
        }
    } // namespace

    VrPresenter::~VrPresenter()
    {
        Reset();
    }

    void VrPresenter::Reset()
    {
        auto* Device = Kernel_.Gpu().GpuDevice();
        const auto Release = [&](Texture& Value)
        {
            if (Device && Value.Handle)
                SDL_ReleaseGPUTexture(Device, Value.Handle);
            Value = {};
        };
        for (auto& Value : Depth_)
            Release(Value);
        for (auto& Value : Color_)
            Release(Value);
        Release(Preview_);
        Release(PendingPreview_);
        for (auto [Format, Pipeline] : Pipelines_)
        {
            if (Device)
                SDL_ReleaseGPUGraphicsPipeline(Device, Pipeline);
        }
        Pipelines_.clear();
        Eyes_ = {};
    }

    bool VrPresenter::Ensure(Texture& Target, Uint32 Width, Uint32 Height, SDL_GPUTextureFormat Format, bool Depth)
    {
        if (!Width || !Height)
            return false;
        if (Target.Handle && Target.Width == Width && Target.Height == Height && Target.Format == Format)
            return true;
        SDL_GPUTextureCreateInfo Info{};
        Info.type = SDL_GPU_TEXTURETYPE_2D;
        Info.format = Format;
        Info.usage = Depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                           : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        Info.width = Width;
        Info.height = Height;
        Info.layer_count_or_depth = 1;
        Info.num_levels = 1;
        Info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        auto* Device = Kernel_.Gpu().GpuDevice();
        auto* Handle = SDL_CreateGPUTexture(Device, &Info);
        if (!Handle)
            return false;
        if (Target.Handle)
            SDL_ReleaseGPUTexture(Device, Target.Handle);
        Target = {Handle, Width, Height, Format};
        return true;
    }

    bool VrPresenter::Targets(const std::array<SDL_GPUTexture*, 2>& Images, SDL_GPUTextureFormat Format,
                              bool CopySource, const std::array<Uint32, 2>& Widths,
                              const std::array<Uint32, 2>& Heights, std::array<GpuRenderTarget, 2>& Targets)
    {
        auto& Gpu = Kernel_.Gpu();
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            if (!Ensure(Depth_[Eye], Widths[Eye], Heights[Eye], Gpu.GpuDepthFormat(), true))
                return false;
            Intermediate_[Eye] =
                !CopySource || Format != Gpu.GpuColorFormat() || Kernel_.Renderer().HasFinalColorTransform();
            if (!Ensure(Color_[Eye], Widths[Eye], Heights[Eye], Gpu.GpuColorFormat()))
                return false;
            Eyes_[Eye] = {Images[Eye], Widths[Eye], Heights[Eye], Format};
            Targets[Eye] = {Intermediate_[Eye] ? Color_[Eye].Handle : Images[Eye],
                            Depth_[Eye].Handle,
                            Gpu.GpuColorFormat(),
                            Gpu.GpuDepthFormat(),
                            Widths[Eye],
                            Heights[Eye]};
        }
        return true;
    }

    SDL_GPUGraphicsPipeline* VrPresenter::Pipeline(SDL_GPUTextureFormat Format)
    {
        if (auto It = Pipelines_.find(Format); It != Pipelines_.end())
            return It->second;
        SDL_GPUGraphicsPipeline* Pipeline = Kernel_.Gpu().CreateTransferPipeline(Format);
        if (Pipeline)
            Pipelines_.emplace(Format, Pipeline);
        return Pipeline;
    }

    bool VrPresenter::Transfer(SDL_GPUCommandBuffer* Commands, SDL_GPUTexture* Source,
                               SDL_GPUTextureFormat SourceFormat, const Texture& Destination, const glm::vec4& Rect,
                               const std::array<float, 3>& Scale, bool Clear)
    {
        auto* PipelineValue = Pipeline(Destination.Format);
        if (!PipelineValue || !Source || !Destination.Handle)
            return false;
        SDL_GPUColorTargetInfo Color{};
        Color.texture = Destination.Handle;
        Color.clear_color = {0, 0, 0, 1};
        Color.load_op = Clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        Color.store_op = SDL_GPU_STOREOP_STORE;
        auto* Pass = SDL_BeginGPURenderPass(Commands, &Color, 1, nullptr);
        if (!Pass)
            return false;
        SDL_BindGPUGraphicsPipeline(Pass, PipelineValue);
        SDL_GPUViewport Viewport{Rect.x, Rect.y, Rect.z, Rect.w, 0, 1};
        SDL_SetGPUViewport(Pass, &Viewport);
        // Sampling sRGB already decodes it. Return to DBC's encoded-value domain before gain.
        const std::array<float, 8> Uniforms{
            Scale[0], Scale[1], Scale[2], Srgb(Destination.Format) ? 1.0f : 0.0f, Srgb(SourceFormat) ? 1.0f : 0.0f,
            0,        0,        0};
        SDL_PushGPUFragmentUniformData(Commands, 0, Uniforms.data(), sizeof(Uniforms));
        SDL_GPUTextureSamplerBinding Binding{Source, Kernel_.Gpu().GpuSampler()};
        SDL_BindGPUFragmentSamplers(Pass, 0, &Binding, 1);
        SDL_DrawGPUPrimitives(Pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(Pass);
        return true;
    }

    bool VrPresenter::PreviewSize(Uint32& Width, Uint32& Height) const
    {
        int W = 0, H = 0;
        if (Kernel_.Core().Window())
            SDL_GetWindowSizeInPixels(Kernel_.Core().Window(), &W, &H);
        Width =
            W > 0 ? static_cast<Uint32>(W)
                  : (Preview_.Width ? Preview_.Width : static_cast<Uint32>(std::max(1, Kernel_.Core().WindowWidth())));
        Height = H > 0 ? static_cast<Uint32>(H)
                       : (Preview_.Height ? Preview_.Height
                                          : static_cast<Uint32>(std::max(1, Kernel_.Core().WindowHeight())));
        return Width && Height;
    }

    bool VrPresenter::Present(SDL_GPUCommandBuffer* Commands, const std::array<SDL_GPUTexture*, 2>& Images,
                              VrPreviewMode Mode)
    {
        const auto Scale = Kernel_.Renderer().FinalColorScale();
        for (std::size_t Eye = 0; Eye < 2; ++Eye)
        {
            if (Intermediate_[Eye])
            {
                if (!Transfer(Commands, Color_[Eye].Handle, Color_[Eye].Format, Eyes_[Eye],
                              {0, 0, Eyes_[Eye].Width, Eyes_[Eye].Height}, Scale, true))
                    return false;
            }
            else
            {
                // XR images remain color attachments outside passes. Copy to an ordinary
                // sampled texture instead of treating the runtime image as SDL sampler storage.
                auto* Copy = SDL_BeginGPUCopyPass(Commands);
                if (!Copy)
                    return false;
                SDL_GPUTextureLocation Source{};
                Source.texture = Images[Eye];
                SDL_GPUTextureLocation Destination{};
                Destination.texture = Color_[Eye].Handle;
                SDL_CopyGPUTextureToTexture(Copy, &Source, &Destination, Eyes_[Eye].Width, Eyes_[Eye].Height, 1, false);
                SDL_EndGPUCopyPass(Copy);
            }
        }
        Uint32 Width = 0, Height = 0;
        if (!PreviewSize(Width, Height) || !Ensure(PendingPreview_, Width, Height, Kernel_.Gpu().GpuColorFormat()))
            return false;
        const int Count = Mode == VrPreviewMode::Both ? 2 : 1;
        for (int Slot = 0; Slot < Count; ++Slot)
        {
            const std::size_t Eye = Mode == VrPreviewMode::Right ? 1 : static_cast<std::size_t>(Slot);
            const float W = static_cast<float>(Width) / Count;
            const auto Rect =
                VrTracking::Letterbox(Slot * W, 0, W, static_cast<float>(Height), static_cast<float>(Eyes_[Eye].Width),
                                      static_cast<float>(Eyes_[Eye].Height));
            // Color-only runtime images use the same intermediate and gain for their preview.
            auto* Source = Color_[Eye].Handle;
            const auto SourceFormat = Color_[Eye].Format;
            const auto Gain = Intermediate_[Eye] ? Scale : std::array<float, 3>{1, 1, 1};
            if (!Transfer(Commands, Source, SourceFormat, PendingPreview_, Rect, Gain, Slot == 0))
                return false;
        }
        return true;
    }

    bool VrPresenter::Mirror(GpuCommandBuffer& Commands)
    {
        auto* Window = Kernel_.Core().Window();
        if (!Window || (SDL_GetWindowFlags(Window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)))
            return true;
        SDL_GPUTexture* Image = nullptr;
        Uint32 Width = 0, Height = 0;
        if (!Commands.TryAcquireSwapchain(Window, &Image, &Width, &Height))
            return false;
        if (!Image || !Width || !Height)
            return true;
        SDL_GPUBlitInfo Blit{};
        Blit.source.texture = PendingPreview_.Handle;
        Blit.source.w = PendingPreview_.Width;
        Blit.source.h = PendingPreview_.Height;
        Blit.destination.texture = Image;
        Blit.destination.w = Width;
        Blit.destination.h = Height;
        Blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        Blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(Commands.Get(), &Blit);
        return true;
    }

    void VrPresenter::Commit()
    {
        std::swap(Preview_, PendingPreview_);
    }

    SDL_Surface* VrPresenter::Capture()
    {
        if (!Preview_.Handle)
        {
            Uint32 Width = 0, Height = 0;
            PreviewSize(Width, Height);
            auto* Surface =
                SDL_CreateSurface(static_cast<int>(Width), static_cast<int>(Height), SDL_PIXELFORMAT_RGBA32);
            if (Surface)
                SDL_FillSurfaceRect(Surface, nullptr, SDL_MapSurfaceRGBA(Surface, 0, 0, 0, 255));
            return Surface;
        }
        auto* Device = Kernel_.Gpu().GpuDevice();
        const std::uint64_t Bytes = static_cast<std::uint64_t>(Preview_.Width) * Preview_.Height * 4;
        if (Bytes > std::numeric_limits<Uint32>::max())
            return nullptr;
        SDL_GPUTransferBufferCreateInfo Info{};
        Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        Info.size = static_cast<Uint32>(Bytes);
        auto* Buffer = SDL_CreateGPUTransferBuffer(Device, &Info);
        if (!Buffer)
            return nullptr;
        GpuCommandBuffer Commands(SDL_AcquireGPUCommandBuffer(Device));
        SDL_Surface* Surface = nullptr;
        if (Commands.Get())
        {
            if (auto* Copy = SDL_BeginGPUCopyPass(Commands.Get()))
            {
                SDL_GPUTextureRegion Source{};
                Source.texture = Preview_.Handle;
                Source.w = Preview_.Width;
                Source.h = Preview_.Height;
                Source.d = 1;
                SDL_GPUTextureTransferInfo Destination{};
                Destination.transfer_buffer = Buffer;
                SDL_DownloadFromGPUTexture(Copy, &Source, &Destination);
                SDL_EndGPUCopyPass(Copy);
                if (auto* Fence = Commands.SubmitAndAcquireFence())
                {
                    if (SDL_WaitForGPUFences(Device, true, &Fence, 1))
                    {
                        if (const auto* Mapped =
                                static_cast<const Uint8*>(SDL_MapGPUTransferBuffer(Device, Buffer, false)))
                        {
                            const bool Bgra = Preview_.Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                                              Preview_.Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
                            Surface =
                                SDL_CreateSurface(static_cast<int>(Preview_.Width), static_cast<int>(Preview_.Height),
                                                  Bgra ? SDL_PIXELFORMAT_BGRA32 : SDL_PIXELFORMAT_RGBA32);
                            if (Surface)
                                for (Uint32 Row = 0; Row < Preview_.Height; ++Row)
                                    SDL_memcpy(static_cast<Uint8*>(Surface->pixels) + Row * Surface->pitch,
                                               Mapped + Row * Preview_.Width * 4, Preview_.Width * 4);
                            SDL_UnmapGPUTransferBuffer(Device, Buffer);
                        }
                    }
                    SDL_ReleaseGPUFence(Device, Fence);
                }
            }
        }
        SDL_ReleaseGPUTransferBuffer(Device, Buffer);
        if (!Surface)
            DBC_LOG_WARN("VR preview readback failed: {}", SDL_GetError());
        return Surface;
    }
} // namespace dbc
