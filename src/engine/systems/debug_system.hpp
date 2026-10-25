#pragma once

#include <array>
#include <string>
#include <vector>

namespace dbc
{
    class EngineKernel;

    struct FrameStats
    {
        int Fps = 0;
        int DrawCalls = 0;
        int TotalVertices = 0;
        int TotalBatches = 0;
        int VisibleObjects = 0;
        int TotalObjects = 0;
    };

    class DebugSystem
    {
      public:
        explicit DebugSystem(EngineKernel& Kernel);
        ~DebugSystem() = default;

        // --- Overlay ---
        void SetOverlayEnabled(bool Enabled);
        bool IsOverlayEnabled() const;
        void ToggleOverlay();

        // Called by renderer to provide per-frame statistics.
        void UpdateFrameStats(const FrameStats& Stats);
        const FrameStats& CurrentStats() const;

        // Injects overlay text into TextSystem (called just before text rendering).
        void QueueOverlayText();

        // --- Render error state ---
        void SetLastRenderError(const std::string& Error);
        const std::string& LastRenderError() const;
        bool HasRenderError() const;
        void ClearRenderError();

        // --- Ring buffer of recent log messages ---
        void PushLogMessage(const std::string& Msg);
        std::vector<std::string> RecentLogMessages() const;

        // --- Diagnostic dumps ---
        void DumpScene(const std::string& Path);
        void DumpRenderer(const std::string& Path);
        // F4 / Studio: both dumps next to the log, without touching the program.
        void DumpDiagnostics();

      private:
        EngineKernel& Kernel_;
        bool OverlayEnabled_ = false;
        FrameStats FrameStats_{};
        std::string LastRenderError_;

        // Ring buffer: last N log messages.
        static constexpr size_t KRingBufferSize = 100;
        std::array<std::string, KRingBufferSize> LogRing_{};
        size_t RingHead_ = 0;
        size_t RingCount_ = 0;
    };

} // namespace dbc
