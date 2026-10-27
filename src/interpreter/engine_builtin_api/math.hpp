#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dbc
{
    class EngineKernel;

    inline constexpr float KDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
    // Inverse trigonometry runs in double: single-precision libm results differ across
    // platforms (Apple asinf(1) is one ulp below pi/2), which would make BASIC output vary.
    inline constexpr double KRadiansToDegrees = 180.0 / std::numbers::pi;

    void Randomize(EngineKernel& K, int Seed);
    int Rnd(EngineKernel& K, int MaxValue);
    float Atanfull(EngineKernel& K, float X, float Z);

    inline int Rnd(EngineKernel& K, float MaxValue)
    {
        return Rnd(K, static_cast<int>(MaxValue));
    }

    inline void Inc(int& Value, int Step = 1)
    {
        Value += Step;
    }

    inline void Inc(float& Value, float Step = 1.0f)
    {
        Value += Step;
    }

    inline void Dec(int& Value, int Step = 1)
    {
        Value -= Step;
    }

    inline void Dec(float& Value, float Step = 1.0f)
    {
        Value -= Step;
    }

    inline float Sqrt(float Value)
    {
        return std::sqrt(std::max(0.0f, Value));
    }

    inline float Abs(float Value)
    {
        return std::fabs(Value);
    }

    inline int Int(float Value)
    {
        return static_cast<int>(std::floor(Value));
    }

    inline float Exp(float Value)
    {
        return std::exp(Value);
    }

    inline float Cos(float Degrees)
    {
        return std::cos(Degrees * KDegreesToRadians);
    }

    inline float Sin(float Degrees)
    {
        return std::sin(Degrees * KDegreesToRadians);
    }

    inline float Tan(float Degrees)
    {
        return std::tan(Degrees * KDegreesToRadians);
    }

    inline float Acos(float Value)
    {
        const double Clamped = std::clamp(static_cast<double>(Value), -1.0, 1.0);
        return static_cast<float>(std::acos(Clamped) * KRadiansToDegrees);
    }

    inline float Asin(float Value)
    {
        const double Clamped = std::clamp(static_cast<double>(Value), -1.0, 1.0);
        return static_cast<float>(std::asin(Clamped) * KRadiansToDegrees);
    }

    inline float Atan(float Value)
    {
        float Angle = static_cast<float>(std::atan(static_cast<double>(Value)) * KRadiansToDegrees);
        if (Angle < 0.0f)
        {
            Angle += 360.0f;
        }
        return Angle;
    }

    inline float Hcos(float Value)
    {
        return std::cosh(Value * KDegreesToRadians);
    }

    inline float Hsin(float Value)
    {
        return std::sinh(Value * KDegreesToRadians);
    }

    inline float Htan(float Value)
    {
        return std::tanh(Value * KDegreesToRadians);
    }

    inline float Ceil(float Value)
    {
        return std::ceil(Value);
    }

    inline float Floor(float Value)
    {
        return std::floor(Value);
    }

    inline float Curveangle(float Target, float Current, float Speed)
    {
        const float SafeSpeed = std::max(1.0f, std::fabs(Speed));

        float Delta = std::fmod(Target - Current, 360.0f);
        if (Delta > 180.0f)
        {
            Delta -= 360.0f;
        }
        if (Delta < -180.0f)
        {
            Delta += 360.0f;
        }

        float Out = Current + (Delta / SafeSpeed);
        Out = std::fmod(Out, 360.0f);
        if (Out < 0.0f)
        {
            Out += 360.0f;
        }
        return Out;
    }

} // namespace dbc
