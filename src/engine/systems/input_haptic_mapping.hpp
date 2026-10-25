#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace dbc::detail
{
    struct ForceAngleHapticParameters
    {
        std::int32_t Direction = 0;
        std::int16_t Level = 0;
        std::uint32_t Duration = 0;
    };

    struct ForceWaterHapticParameters
    {
        std::int16_t Coefficient = 0;
        std::uint16_t Saturation = 0;
        std::uint32_t Duration = 0;
    };

    struct ForcePeriodicHapticParameters
    {
        std::int16_t Magnitude = 0;
        std::uint32_t Duration = 0;
    };

    inline bool MakeForceAngleHapticParameters(int Magnitude, double Angle, int DurationMs,
                                               ForceAngleHapticParameters& Result, std::string* Error = nullptr)
    {
        if (Magnitude < 0 || Magnitude > 100)
        {
            if (Error)
                *Error = "FORCE ANGLE magnitude must be between zero and 100";
            return false;
        }
        if (!std::isfinite(Angle) || Angle < 0.0 || Angle > 360.0)
        {
            if (Error)
                *Error = "FORCE ANGLE angle must be between zero and 360 degrees";
            return false;
        }
        if (DurationMs < 0)
        {
            if (Error)
                *Error = "FORCE ANGLE duration must be non-negative";
            return false;
        }

        const std::int64_t Hundredths = std::llround(Angle * 100.0);
        Result.Direction = static_cast<std::int32_t>(Hundredths % 36000);
        Result.Level = static_cast<std::int16_t>(Magnitude * std::numeric_limits<std::int16_t>::max() / 100);
        Result.Duration = DurationMs == 0 ? std::numeric_limits<std::uint32_t>::max()
                                          : static_cast<std::uint32_t>(50 + (DurationMs / 1000) * 1000);
        return true;
    }

    inline bool MakeForceWaterHapticParameters(int Magnitude, int DurationMs, ForceWaterHapticParameters& Result,
                                               std::string* Error = nullptr)
    {
        if (Magnitude < 0 || Magnitude > 100)
        {
            if (Error)
                *Error = "FORCE WATER EFFECT magnitude must be between zero and 100";
            return false;
        }
        if (DurationMs < 0)
        {
            if (Error)
                *Error = "FORCE WATER EFFECT duration must be non-negative";
            return false;
        }

        Result.Coefficient = static_cast<std::int16_t>(Magnitude * std::numeric_limits<std::int16_t>::max() / 200);
        Result.Saturation = std::numeric_limits<std::uint16_t>::max();
        Result.Duration = static_cast<std::uint32_t>(static_cast<std::uint64_t>(DurationMs) + 50);
        return true;
    }

    inline bool MakeForcePeriodicHapticParameters(const char* Command, int Magnitude, int DurationMs,
                                                  ForcePeriodicHapticParameters& Result, std::string* Error = nullptr)
    {
        if (Magnitude < 0 || Magnitude > 100)
        {
            if (Error)
                *Error = std::string(Command) + " magnitude must be between zero and 100";
            return false;
        }
        if (DurationMs < 0)
        {
            if (Error)
                *Error = std::string(Command) + " duration must be non-negative";
            return false;
        }

        Result.Magnitude = static_cast<std::int16_t>(Magnitude * std::numeric_limits<std::int16_t>::max() / 100);
        Result.Duration =
            DurationMs == 0 ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(DurationMs);
        return true;
    }
} // namespace dbc::detail
