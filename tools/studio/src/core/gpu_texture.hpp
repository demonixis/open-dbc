#pragma once

#include <filesystem>
#include <string>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_surface.h>

namespace dbc::studio
{
    namespace core
    {
        struct GpuImage
        {
            SDL_GPUTexture* Texture = nullptr;
            int Width = 0;
            int Height = 0;
        };

        // Loads an image with SDL_image and uploads it as an RGBA8 sampler texture,
        // waiting for the transfer: images are opened rarely, and the frame loop then
        // never sees a pending upload. `MaxSize` > 0 shrinks it to fit, for thumbnails.
        bool LoadGpuImage(SDL_GPUDevice* Gpu, const std::filesystem::path& Path, GpuImage& Out, std::string& Error,
                          int MaxSize = 0);
        // Same upload for a surface the caller already has.
        bool UploadGpuImage(SDL_GPUDevice* Gpu, SDL_Surface* Surface, GpuImage& Out, std::string& Error);
        void ReleaseGpuImage(SDL_GPUDevice* Gpu, GpuImage& Image);
    } // namespace core

} // namespace dbc::studio
