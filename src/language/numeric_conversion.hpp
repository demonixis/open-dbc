#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>

namespace dbc::language
{
    template <typename Integer> bool ConvertInteger(double Number, Integer& Result, std::string* Error = nullptr)
    {
        static_assert(std::is_integral_v<Integer> && std::is_signed_v<Integer>);
        const double Truncated = std::trunc(Number);
        // The exclusive power-of-two bound stays exact even when INT64_MAX rounds up in double.
        const double Limit = std::ldexp(1.0, std::numeric_limits<Integer>::digits);
        if (!std::isfinite(Number) || Truncated < -Limit || Truncated >= Limit)
        {
            if (Error)
                *Error = "Numeric value is out of integer range";
            return false;
        }
        Result = static_cast<Integer>(Truncated);
        return true;
    }

    inline bool ParseNumber(const std::string& Text, double& Result, std::string* Error = nullptr)
    {
        char* End = nullptr;
        errno = 0;
        const double Number = std::strtod(Text.c_str(), &End);
        if (End == Text.c_str() || errno == ERANGE || !std::isfinite(Number))
        {
            if (Error)
                *Error = "Expected a finite numeric value";
            return false;
        }
        // Preserve the historical numeric-prefix conversion, including leading whitespace.
        Result = Number;
        return true;
    }
} // namespace dbc::language
