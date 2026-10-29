#pragma once

#include "render_view.hpp"
#include "vr_system.hpp"
#include <map>

namespace dbc
{
    class EngineKernel;
    class GpuCommandBuffer;

    class VrPresenter
    {
      public:
        explicit VrPresenter(EngineKernel& Kernel) : Kernel_(Kernel) {}

        ~VrPresenter();
        void Reset();
        bool Targets(const std::array<SDL_GPUTexture*, 2>& Images, SDL_GPUTextureFormat Format, bool CopySource,
                     const std::array<Uint32, 2>& Widths, const std::array<Uint32, 2>& Heights,
                     std::array<GpuRenderTarget, 2>& Targets);
        bool Present(SDL_GPUCommandBuffer* Commands, const std::array<SDL_GPUTexture*, 2>& Images, VrPreviewMode Mode);
        bool Mirror(GpuCommandBuffer& Commands);
        void Commit();
        SDL_Surface* Capture();

      private:
        struct Texture
        {
            SDL_GPUTexture* Handle = nullptr;
            Uint32 Width = 0;
            Uint32 Height = 0;
            SDL_GPUTextureFormat Format = SDL_GPU_TEXTUREFORMAT_INVALID;
        };

        bool Ensure(Texture& Target, Uint32 Width, Uint32 Height, SDL_GPUTextureFormat Format, bool Depth = false);
        bool Transfer(SDL_GPUCommandBuffer* Commands, SDL_GPUTexture* Source, SDL_GPUTextureFormat SourceFormat,
                      const Texture& Destination, const glm::vec4& Rect, const std::array<float, 3>& Scale, bool Clear);
        SDL_GPUGraphicsPipeline* Pipeline(SDL_GPUTextureFormat Format);
        bool PreviewSize(Uint32& Width, Uint32& Height) const;
        EngineKernel& Kernel_;
        std::array<Texture, 2> Depth_{};
        std::array<Texture, 2> Color_{};
        std::array<Texture, 2> Eyes_{};
        std::array<bool, 2> Intermediate_{};
        Texture Preview_{};
        Texture PendingPreview_{};
        std::map<SDL_GPUTextureFormat, SDL_GPUGraphicsPipeline*> Pipelines_;
    };
} // namespace dbc
