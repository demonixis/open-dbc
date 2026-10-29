#include "core/gpu_texture.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <SDL3_image/SDL_image.h>

namespace dbc::studio
{
    namespace core
    {
        bool UploadGpuImage(SDL_GPUDevice* Gpu, SDL_Surface* Surface, GpuImage& Out, std::string& Error)
        {
            Out = {};
            if (Gpu == nullptr)
            {
                Error = "No GPU device";
                return false;
            }
            SDL_Surface* Rgba = SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_ABGR8888);
            if (Rgba == nullptr)
            {
                Error = std::string("Cannot convert image: ") + SDL_GetError();
                return false;
            }
            const std::uint64_t RequiredBytes = static_cast<std::uint64_t>(Rgba->w) * Rgba->h * 4u;
            if (Rgba->w <= 0 || Rgba->h <= 0 || RequiredBytes > std::numeric_limits<Uint32>::max())
            {
                SDL_DestroySurface(Rgba);
                Error = "Image is too large for a GPU transfer";
                return false;
            }

            SDL_GPUTextureCreateInfo Info = {};
            Info.type = SDL_GPU_TEXTURETYPE_2D;
            Info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            Info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            Info.width = static_cast<Uint32>(Rgba->w);
            Info.height = static_cast<Uint32>(Rgba->h);
            Info.layer_count_or_depth = 1;
            Info.num_levels = 1;
            SDL_GPUTexture* Texture = SDL_CreateGPUTexture(Gpu, &Info);
            if (Texture == nullptr)
            {
                SDL_DestroySurface(Rgba);
                Error = std::string("Cannot create texture: ") + SDL_GetError();
                return false;
            }

            SDL_GPUTransferBufferCreateInfo TransferInfo = {};
            TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            TransferInfo.size = static_cast<Uint32>(RequiredBytes);
            SDL_GPUTransferBuffer* Transfer = SDL_CreateGPUTransferBuffer(Gpu, &TransferInfo);
            const auto Fail = [&](const char* Operation)
            {
                Error = std::string(Operation) + ": " + SDL_GetError();
                if (Transfer != nullptr)
                    SDL_ReleaseGPUTransferBuffer(Gpu, Transfer);
                SDL_ReleaseGPUTexture(Gpu, Texture);
                SDL_DestroySurface(Rgba);
                return false;
            };
            if (Transfer == nullptr)
                return Fail("Cannot create transfer buffer");

            auto* Mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(Gpu, Transfer, false));
            if (Mapped == nullptr)
                return Fail("Cannot map image upload");
            // SDL surfaces can be padded, so copy row by row.
            const auto* Source = static_cast<const Uint8*>(Rgba->pixels);
            const std::size_t Row = static_cast<std::size_t>(Rgba->w) * 4;
            for (int Y = 0; Y < Rgba->h; ++Y)
                SDL_memcpy(Mapped + static_cast<std::size_t>(Y) * Row,
                           Source + static_cast<std::size_t>(Y) * Rgba->pitch, Row);
            SDL_UnmapGPUTransferBuffer(Gpu, Transfer);

            SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Gpu);
            if (Commands == nullptr)
                return Fail("Cannot acquire image upload command");
            SDL_GPUCopyPass* CopyPass = SDL_BeginGPUCopyPass(Commands);
            if (CopyPass == nullptr)
            {
                SDL_CancelGPUCommandBuffer(Commands);
                return Fail("Cannot begin image upload pass");
            }
            SDL_GPUTextureTransferInfo From = {};
            From.transfer_buffer = Transfer;
            SDL_GPUTextureRegion To = {};
            To.texture = Texture;
            To.w = Info.width;
            To.h = Info.height;
            To.d = 1;
            SDL_UploadToGPUTexture(CopyPass, &From, &To, false);
            SDL_EndGPUCopyPass(CopyPass);

            SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
            if (Fence == nullptr)
                return Fail("Cannot submit image upload");
            const bool Completed = SDL_WaitForGPUFences(Gpu, true, &Fence, 1);
            SDL_ReleaseGPUFence(Gpu, Fence);
            if (!Completed)
            {
                SDL_WaitForGPUIdle(Gpu);
                return Fail("Cannot wait for image upload");
            }

            SDL_ReleaseGPUTransferBuffer(Gpu, Transfer);
            Out.Texture = Texture;
            Out.Width = Rgba->w;
            Out.Height = Rgba->h;
            SDL_DestroySurface(Rgba);
            return true;
        }

        bool LoadGpuImage(SDL_GPUDevice* Gpu, const std::filesystem::path& Path, GpuImage& Out, std::string& Error,
                          int MaxSize)
        {
            SDL_Surface* Loaded = IMG_Load(dbc::PathToUtf8(Path).c_str());
            if (Loaded == nullptr)
            {
                Error = std::string("Cannot load image: ") + SDL_GetError();
                return false;
            }
            if (MaxSize > 0 && (Loaded->w > MaxSize || Loaded->h > MaxSize))
            {
                const float Scale = static_cast<float>(MaxSize) / static_cast<float>(std::max(Loaded->w, Loaded->h));
                SDL_Surface* Small =
                    SDL_ScaleSurface(Loaded, std::max(1, static_cast<int>(Loaded->w * Scale)),
                                     std::max(1, static_cast<int>(Loaded->h * Scale)), SDL_SCALEMODE_LINEAR);
                if (Small != nullptr)
                {
                    SDL_DestroySurface(Loaded);
                    Loaded = Small;
                }
            }
            const bool Uploaded = UploadGpuImage(Gpu, Loaded, Out, Error);
            SDL_DestroySurface(Loaded);
            return Uploaded;
        }

        void ReleaseGpuImage(SDL_GPUDevice* Gpu, GpuImage& Image)
        {
            if (Image.Texture != nullptr && Gpu != nullptr)
                SDL_ReleaseGPUTexture(Gpu, Image.Texture);
            Image = {};
        }
    } // namespace core

} // namespace dbc::studio
