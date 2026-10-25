#include "debug_system.hpp"
#include "camera_system.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "light_manager.hpp"
#include "log.hpp"
#include "object_manager.hpp"
#include "text_system.hpp"

#include <SDL3/SDL_gpu.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dbc
{

    // --- RingBufferSink implementation (declared in log.hpp) ---

    namespace log::detail
    {
        void RingBufferSink::sink_it_(const spdlog::details::log_msg& Msg)
        {
            if (!Debug_)
            {
                return;
            }
            spdlog::memory_buf_t Formatted;
            spdlog::sinks::base_sink<std::mutex>::formatter_->format(Msg, Formatted);
            Debug_->PushLogMessage(fmt::to_string(Formatted));
        }
    } // namespace log::detail

    // --- DebugSystem ---

    DebugSystem::DebugSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    // --- Overlay ---

    void DebugSystem::SetOverlayEnabled(bool Enabled)
    {
        OverlayEnabled_ = Enabled;
    }

    bool DebugSystem::IsOverlayEnabled() const
    {
        return OverlayEnabled_;
    }

    void DebugSystem::ToggleOverlay()
    {
        OverlayEnabled_ = !OverlayEnabled_;
    }

    void DebugSystem::UpdateFrameStats(const FrameStats& Stats)
    {
        FrameStats_ = Stats;
    }

    const FrameStats& DebugSystem::CurrentStats() const
    {
        return FrameStats_;
    }

    void DebugSystem::QueueOverlayText()
    {
        if (!OverlayEnabled_)
        {
            return;
        }

        const int LineHeight = 18;
        int Y = 4;
        const int X = 4;

        // Save current ink color, set overlay color to yellow.
        const int SavedColor = Kernel_.Text().InkForeColor();
        Kernel_.Text().InkForeColor() = 0x00FFFF00;

        // Line 1: FPS + GPU
        const char* GpuDriver = "unknown";
        if (Kernel_.Gpu().GpuDevice())
        {
            GpuDriver = SDL_GetGPUDeviceDriver(Kernel_.Gpu().GpuDevice());
        }
        Kernel_.Text().DrawOverlayText(X, Y,
                                       "FPS: " + std::to_string(FrameStats_.Fps) + " | GPU: " + GpuDriver +
                                           " | Objects: " + std::to_string(FrameStats_.VisibleObjects) + "/" +
                                           std::to_string(FrameStats_.TotalObjects));
        Y += LineHeight;

        // Line 2: Draw stats
        Kernel_.Text().DrawOverlayText(X, Y,
                                       "Draw: " + std::to_string(FrameStats_.DrawCalls) + " calls | " +
                                           std::to_string(FrameStats_.TotalVertices) + " verts | " +
                                           std::to_string(FrameStats_.TotalBatches) + " batches");
        Y += LineHeight;

        // Line 3: Camera
        Kernel_.Text().DrawOverlayText(X, Y,
                                       "Cam: (" + std::to_string(static_cast<int>(Kernel_.Camera().PositionX())) +
                                           ", " + std::to_string(static_cast<int>(Kernel_.Camera().PositionY())) +
                                           ", " + std::to_string(static_cast<int>(Kernel_.Camera().PositionZ())) + ")");
        Y += LineHeight;

        // Line 4: Sync + error
        std::string Line4 = "Sync: " + std::string(Kernel_.Core().SyncOn() ? "ON" : "OFF");
        if (!LastRenderError_.empty())
        {
            Line4 += " | Err: " + LastRenderError_;
        }
        Kernel_.Text().DrawOverlayText(X, Y, Line4);

        // Restore ink color.
        Kernel_.Text().InkForeColor() = SavedColor;
    }

    // --- Render error ---

    void DebugSystem::SetLastRenderError(const std::string& Error)
    {
        LastRenderError_ = Error;
    }

    const std::string& DebugSystem::LastRenderError() const
    {
        return LastRenderError_;
    }

    bool DebugSystem::HasRenderError() const
    {
        return !LastRenderError_.empty();
    }

    void DebugSystem::ClearRenderError()
    {
        LastRenderError_.clear();
    }

    // --- Ring buffer ---

    void DebugSystem::PushLogMessage(const std::string& Msg)
    {
        LogRing_[RingHead_] = Msg;
        RingHead_ = (RingHead_ + 1) % KRingBufferSize;
        if (RingCount_ < KRingBufferSize)
        {
            ++RingCount_;
        }
    }

    std::vector<std::string> DebugSystem::RecentLogMessages() const
    {
        std::vector<std::string> Result;
        Result.reserve(RingCount_);
        if (RingCount_ == 0)
        {
            return Result;
        }

        // Start from the oldest message in the buffer.
        size_t Start = (RingCount_ < KRingBufferSize) ? 0 : RingHead_;
        for (size_t I = 0; I < RingCount_; ++I)
        {
            const size_t Idx = (Start + I) % KRingBufferSize;
            Result.push_back(LogRing_[Idx]);
        }
        return Result;
    }

    // --- Diagnostic dumps ---

    void DebugSystem::DumpDiagnostics()
    {
        DumpScene(log::Directory() + "/open-dbc-dump-scene.txt");
        DumpRenderer(log::Directory() + "/open-dbc-dump-renderer.txt");
    }

    void DebugSystem::DumpScene(const std::string& Path)
    {
        const std::string OutPath = Path.empty() ? "open-dbc-dump-scene.txt" : Path;
        std::ofstream Out(OutPath);
        if (!Out.is_open())
        {
            DBC_LOG_ERROR("DumpScene: failed to open {}", OutPath);
            return;
        }

        Out << "=== Scene Dump ===\n\n";

        // Camera
        Out << "--- Camera ---\n";
        Out << "Position: (" << Kernel_.Camera().PositionX() << ", " << Kernel_.Camera().PositionY() << ", "
            << Kernel_.Camera().PositionZ() << ")\n";
        Out << "Rotation: (" << Kernel_.Camera().AngleX() << ", " << Kernel_.Camera().AngleY() << ", "
            << Kernel_.Camera().AngleZ() << ")\n";
        Out << "FOV: " << Kernel_.Camera().Fov() << "\n";
        Out << "Near: " << Kernel_.Camera().NearPlane() << " Far: " << Kernel_.Camera().FarPlane() << "\n\n";

        // Objects
        Out << "--- Objects ---\n";
        int ObjCount = 0;
        Kernel_.Objects().ForEachObject(
            [&](int Id, const Object3D& Obj)
            {
                Out << "  [" << Id << "] pos=(" << Obj.PosX << "," << Obj.PosY << "," << Obj.PosZ << ") rot=("
                    << Obj.RotX << "," << Obj.RotY << "," << Obj.RotZ << ") visible=" << Obj.Visible
                    << " meshes=" << Obj.Meshes.size() << "\n";
                ++ObjCount;
            });
        Out << "Total: " << ObjCount << "\n\n";

        // Lights
        Out << "--- Lights ---\n";
        Kernel_.Lights().ForEachLight(
            [&](int Id, const Light3D& Light)
            {
                Out << "  [" << Id << "] pos=(" << Light.PosX << "," << Light.PosY << "," << Light.PosZ << ") color=0x"
                    << std::hex << Light.Color << std::dec << " range=" << Light.Range << "\n";
            });

        Out.close();
        DBC_LOG_INFO("Scene dumped to {}", OutPath);
    }

    void DebugSystem::DumpRenderer(const std::string& Path)
    {
        const std::string OutPath = Path.empty() ? "open-dbc-dump-renderer.txt" : Path;
        std::ofstream Out(OutPath);
        if (!Out.is_open())
        {
            DBC_LOG_ERROR("DumpRenderer: failed to open {}", OutPath);
            return;
        }

        Out << "=== Renderer Dump ===\n\n";

        if (Kernel_.Gpu().GpuDevice())
        {
            Out << "GPU Driver: " << SDL_GetGPUDeviceDriver(Kernel_.Gpu().GpuDevice()) << "\n";
        }
        else
        {
            Out << "GPU Driver: (null device)\n";
        }

        Out << "Pipeline: " << (Kernel_.Gpu().GpuPipeline() ? "OK" : "NULL") << "\n";
        Out << "Pipeline Transparent: " << (Kernel_.Gpu().GpuPipelineTransparent() ? "OK" : "NULL") << "\n";
        Out << "Pipeline Additive: " << (Kernel_.Gpu().GpuPipelineAdditive() ? "OK" : "NULL") << "\n";
        Out << "Sampler: " << (Kernel_.Gpu().GpuSampler() ? "OK" : "NULL") << "\n";
        Out << "Vertex Capacity: " << Kernel_.Gpu().GpuVertexCapacity() << "\n";
        Out << "Depth: " << Kernel_.Gpu().GpuDepthWidth() << "x" << Kernel_.Gpu().GpuDepthHeight() << "\n";
        Out << "Window: " << Kernel_.Core().WindowWidth() << "x" << Kernel_.Core().WindowHeight() << "\n\n";

        Out << "--- Frame Stats ---\n";
        Out << "FPS: " << FrameStats_.Fps << "\n";
        Out << "Draw calls: " << FrameStats_.DrawCalls << "\n";
        Out << "Vertices: " << FrameStats_.TotalVertices << "\n";
        Out << "Batches: " << FrameStats_.TotalBatches << "\n";
        Out << "Visible objects: " << FrameStats_.VisibleObjects << "\n";
        Out << "Total objects: " << FrameStats_.TotalObjects << "\n";

        if (!LastRenderError_.empty())
        {
            Out << "\nLast render error: " << LastRenderError_ << "\n";
        }

        Out.close();
        DBC_LOG_INFO("Renderer dumped to {}", OutPath);
    }

} // namespace dbc
