#pragma once

#include "log.hpp"
#include <SDL3/SDL_gpu.h>
#include <utility>

namespace dbc
{
    // Owns command submission, including the SDL rule that acquired window images cannot be cancelled.
    class GpuCommandBuffer
    {
      public:
        struct Operations
        {
            bool(SDLCALL* Submit)(SDL_GPUCommandBuffer*) = SDL_SubmitGPUCommandBuffer;
            bool(SDLCALL* Cancel)(SDL_GPUCommandBuffer*) = SDL_CancelGPUCommandBuffer;
        };

        explicit GpuCommandBuffer(SDL_GPUCommandBuffer* Handle) : Handle_(Handle) {}
        GpuCommandBuffer(SDL_GPUCommandBuffer* Handle, Operations Calls) : Handle_(Handle), Calls_(Calls) {}
        ~GpuCommandBuffer()
        {
            Abandon();
        }
        GpuCommandBuffer(const GpuCommandBuffer&) = delete;
        GpuCommandBuffer& operator=(const GpuCommandBuffer&) = delete;

        SDL_GPUCommandBuffer* Get() const
        {
            return Handle_;
        }
        void Reset(SDL_GPUCommandBuffer* Handle)
        {
            Abandon();
            Handle_ = Handle;
        }
        void SetSwapchainAcquired()
        {
            SwapchainAcquired_ = true;
        }

        bool AcquireSwapchain(SDL_Window* Window, SDL_GPUTexture** Texture, Uint32* Width, Uint32* Height)
        {
            const bool Acquired = SDL_WaitAndAcquireGPUSwapchainTexture(Handle_, Window, Texture, Width, Height);
            if (*Texture)
                SwapchainAcquired_ = true;
            return Acquired;
        }

        bool TryAcquireSwapchain(SDL_Window* Window, SDL_GPUTexture** Texture, Uint32* Width, Uint32* Height)
        {
            const bool Acquired = SDL_AcquireGPUSwapchainTexture(Handle_, Window, Texture, Width, Height);
            if (*Texture)
                SwapchainAcquired_ = true;
            return Acquired;
        }

        bool Submit()
        {
            SwapchainAcquired_ = false;
            return Handle_ && Calls_.Submit(std::exchange(Handle_, nullptr));
        }

        SDL_GPUFence* SubmitAndAcquireFence()
        {
            SwapchainAcquired_ = false;
            return Handle_ ? SDL_SubmitGPUCommandBufferAndAcquireFence(std::exchange(Handle_, nullptr)) : nullptr;
        }

        void Abandon()
        {
            if (!Handle_)
                return;
            const bool Completed = SwapchainAcquired_ ? Submit() : Calls_.Cancel(std::exchange(Handle_, nullptr));
            SwapchainAcquired_ = false;
            if (!Completed)
                DBC_LOG_ERROR("GPU command cleanup failed: {}", SDL_GetError());
        }

      private:
        SDL_GPUCommandBuffer* Handle_ = nullptr;
        Operations Calls_{};
        bool SwapchainAcquired_ = false;
    };
} // namespace dbc
