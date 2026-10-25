#pragma once

#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>

namespace dbc
{
    // A snapshot in OpenDBC coordinates: +X right, +Y up, +Z forward, clip depth [0, 1].
    // No camera or window state is read while this view is being rendered.
    struct RenderView
    {
        const glm::mat4 WorldToView;
        const glm::mat4 Projection;
        const glm::mat4 AdditiveProjection;
        const SDL_FRect LogicalViewport;
        const int LogicalWidth;
        const int LogicalHeight;
        const Uint32 PhysicalWidth;
        const Uint32 PhysicalHeight;
        const glm::vec3 CameraPosition = glm::vec3(glm::inverse(WorldToView)[3]);

        bool Valid() const
        {
            if (LogicalWidth <= 0 || LogicalHeight <= 0 || PhysicalWidth == 0 || PhysicalHeight == 0 ||
                PhysicalWidth > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
                PhysicalHeight > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
                !std::isfinite(LogicalViewport.x) || !std::isfinite(LogicalViewport.y) ||
                !std::isfinite(LogicalViewport.w) || !std::isfinite(LogicalViewport.h) || LogicalViewport.w <= 0.0f ||
                LogicalViewport.h <= 0.0f)
                return false;
            for (int Column = 0; Column < 4; ++Column)
                for (int Row = 0; Row < 4; ++Row)
                    if (!std::isfinite(WorldToView[Column][Row]) || !std::isfinite(Projection[Column][Row]) ||
                        !std::isfinite(AdditiveProjection[Column][Row]))
                        return false;
            const auto Viewport = PhysicalViewport();
            return std::isfinite(CameraPosition.x) && std::isfinite(CameraPosition.y) &&
                   std::isfinite(CameraPosition.z) && std::isfinite(Viewport.x) && std::isfinite(Viewport.y) &&
                   std::isfinite(Viewport.w) && std::isfinite(Viewport.h);
        }

        glm::vec4 ClipPlane(int Index, bool Additive = false) const
        {
            const auto Rows = glm::transpose(Additive ? AdditiveProjection : Projection);
            switch (Index)
            {
                case 0:
                    return Rows[3] + Rows[0];
                case 1:
                    return Rows[3] - Rows[0];
                case 2:
                    return Rows[3] + Rows[1];
                case 3:
                    return Rows[3] - Rows[1];
                case 4:
                    return Rows[2];
                default:
                    return Rows[3] - Rows[2];
            }
        }

        bool ContainsSphere(const glm::vec3& Center, float Radius, bool Additive = false) const
        {
            for (int Index = 0; Index < 6; ++Index)
            {
                const glm::vec4 Plane = ClipPlane(Index, Additive);
                if (glm::dot(Plane, glm::vec4(Center, 1.0f)) < -Radius * glm::length(glm::vec3(Plane)))
                    return false;
            }
            return true;
        }

        bool Project(const glm::vec3& Point, SDL_FPoint& Result) const
        {
            const glm::vec4 Clip = Projection * glm::vec4(Point, 1.0f);
            if (Clip.w <= 0.0f || Clip.z < 0.0f || Clip.z > Clip.w)
                return false;
            Result.x = LogicalViewport.x + (Clip.x / Clip.w + 1.0f) * LogicalViewport.w * 0.5f;
            Result.y = LogicalViewport.y + (1.0f - Clip.y / Clip.w) * LogicalViewport.h * 0.5f;
            return true;
        }

        SDL_GPUViewport PhysicalViewport() const
        {
            const float ScaleX = static_cast<float>(PhysicalWidth) / static_cast<float>(LogicalWidth);
            const float ScaleY = static_cast<float>(PhysicalHeight) / static_cast<float>(LogicalHeight);
            return {LogicalViewport.x * ScaleX,
                    LogicalViewport.y * ScaleY,
                    LogicalViewport.w * ScaleX,
                    LogicalViewport.h * ScaleY,
                    0.0f,
                    1.0f};
        }

        SDL_Rect PhysicalScissor() const
        {
            // Double keeps INT_MAX exact at the float-to-integer boundary.
            const double ScaleX = static_cast<double>(PhysicalWidth) / LogicalWidth;
            const double ScaleY = static_cast<double>(PhysicalHeight) / LogicalHeight;
            const int Left = static_cast<int>(
                std::clamp(std::floor(LogicalViewport.x * ScaleX), 0.0, static_cast<double>(PhysicalWidth)));
            const int Top = static_cast<int>(
                std::clamp(std::floor(LogicalViewport.y * ScaleY), 0.0, static_cast<double>(PhysicalHeight)));
            const int Right = static_cast<int>(
                std::clamp(std::ceil((static_cast<double>(LogicalViewport.x) + LogicalViewport.w) * ScaleX), 0.0,
                           static_cast<double>(PhysicalWidth)));
            const int Bottom = static_cast<int>(
                std::clamp(std::ceil((static_cast<double>(LogicalViewport.y) + LogicalViewport.h) * ScaleY), 0.0,
                           static_cast<double>(PhysicalHeight)));
            return {Left, Top, std::max(0, Right - Left), std::max(0, Bottom - Top)};
        }

        bool operator==(const RenderView& Other) const
        {
            return WorldToView == Other.WorldToView && Projection == Other.Projection &&
                   AdditiveProjection == Other.AdditiveProjection && LogicalViewport.x == Other.LogicalViewport.x &&
                   LogicalViewport.y == Other.LogicalViewport.y && LogicalViewport.w == Other.LogicalViewport.w &&
                   LogicalViewport.h == Other.LogicalViewport.h && LogicalWidth == Other.LogicalWidth &&
                   LogicalHeight == Other.LogicalHeight && PhysicalWidth == Other.PhysicalWidth &&
                   PhysicalHeight == Other.PhysicalHeight && CameraPosition == Other.CameraPosition;
        }
    };

    // Borrowed for recording only. The caller owns acquisition, submission and resource lifetime.
    struct GpuRenderTarget
    {
        SDL_GPUTexture* Color = nullptr;
        SDL_GPUTexture* Depth = nullptr;
        SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        Uint32 Width = 0;
        Uint32 Height = 0;
        Uint32 ColorLayer = 0;
        Uint32 DepthLayer = 0;
    };

    struct RenderFrameState
    {
        Uint64 Ticks = 0;
        bool Retain2d = false;
        bool HudLast = false;
        bool HudPrepared = false;
        bool WorldRendered = false;
        bool Finalized = false;
        bool HasErrors = false;
        int Vertices = 0;
        int Batches = 0;
        int Views = 0;
    };
} // namespace dbc
