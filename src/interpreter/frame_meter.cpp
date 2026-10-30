#include "frame_meter.hpp"

#include <algorithm>
#include <cmath>

namespace dbc
{
    namespace interpreter
    {
        namespace
        {
            [[nodiscard]] double MillisecondsBetween(std::chrono::steady_clock::time_point From,
                                                     std::chrono::steady_clock::time_point To)
            {
                return std::chrono::duration<double, std::milli>(To - From).count();
            }
        } // namespace

        void FrameMeter::Start(std::chrono::steady_clock::time_point Started)
        {
            *this = FrameMeter{};
            Started_ = Started;
            FrameStarted_ = Started;
        }

        void FrameMeter::RestartFrame()
        {
            FrameStarted_ = std::chrono::steady_clock::now();
        }

        void FrameMeter::RecordFrame()
        {
            const auto Now = std::chrono::steady_clock::now();
            const double Milliseconds = MillisecondsBetween(FrameStarted_, Now);
            SumMilliseconds_ += Milliseconds;
            MinimumMilliseconds_ = std::min(MinimumMilliseconds_, Milliseconds);
            MaximumMilliseconds_ = std::max(MaximumMilliseconds_, Milliseconds);
            Samples_.push_back(Milliseconds);
            FrameStarted_ = Now;
        }

        double FrameMeter::MillisecondsSinceStart() const
        {
            return MillisecondsBetween(Started_, std::chrono::steady_clock::now());
        }

        void FrameMeter::Finish(BenchmarkResult& Output) const
        {
            if (Started_ == std::chrono::steady_clock::time_point{})
                return;
            Output.TotalMilliseconds = MillisecondsSinceStart();
            Output.FrameCount = Samples_.size();
            if (Samples_.empty())
                return;
            Output.AverageFrameMilliseconds = SumMilliseconds_ / static_cast<double>(Samples_.size());
            Output.MinimumFrameMilliseconds = MinimumMilliseconds_;
            Output.MaximumFrameMilliseconds = MaximumMilliseconds_;
            std::vector<double> Sorted = Samples_;
            std::sort(Sorted.begin(), Sorted.end());
            const auto Percentile = [&](double Fraction)
            {
                const std::size_t Index =
                    std::min(Sorted.size() - 1,
                             static_cast<std::size_t>(std::ceil(Fraction * static_cast<double>(Sorted.size()))) - 1);
                return Sorted[Index];
            };
            Output.P50FrameMilliseconds = Percentile(0.50);
            Output.P95FrameMilliseconds = Percentile(0.95);
            Output.P99FrameMilliseconds = Percentile(0.99);
        }
    } // namespace interpreter
} // namespace dbc
