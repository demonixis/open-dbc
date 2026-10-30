#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace dbc
{
    namespace interpreter
    {
        struct BenchmarkResult
        {
            double PrepareMilliseconds = 0.0;
            double TotalMilliseconds = 0.0;
            std::size_t FrameCount = 0;
            double AverageFrameMilliseconds = 0.0;
            double MinimumFrameMilliseconds = 0.0;
            double P50FrameMilliseconds = 0.0;
            double P95FrameMilliseconds = 0.0;
            double P99FrameMilliseconds = 0.0;
            double MaximumFrameMilliseconds = 0.0;
        };

        // Measures a run's frame times for --benchmark.
        class FrameMeter
        {
          public:
            // Forgets any earlier run; the time of its first frame counts from Started.
            void Start(std::chrono::steady_clock::time_point Started);
            // The next frame's time counts from now, leaving out what came before it.
            void RestartFrame();
            void RecordFrame();
            [[nodiscard]] double MillisecondsSinceStart() const;
            // Fills everything but PrepareMilliseconds; does nothing before Start.
            void Finish(BenchmarkResult& Output) const;

          private:
            std::chrono::steady_clock::time_point Started_{};
            std::chrono::steady_clock::time_point FrameStarted_{};
            double SumMilliseconds_ = 0.0;
            double MinimumMilliseconds_ = 1e18;
            double MaximumMilliseconds_ = 0.0;
            std::vector<double> Samples_{};
        };
    } // namespace interpreter
} // namespace dbc
