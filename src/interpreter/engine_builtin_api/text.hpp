#pragma once

#include <cstdint>
#include <limits>

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/text_state.hpp"
#include "../../engine/systems/text_system.hpp"
#include "../engine_builtin_context.hpp"
#include "language/numeric_conversion.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace dbc
{
    // Text / TextWidthPx / TextHeightPx: inline wrappers to TextSystem methods.
    inline bool Text(EngineKernel& K, int X, int Y, const std::string& Value, std::string* Error = nullptr)
    {
        return K.Text().DrawText(X, Y, Value, Error);
    }

    inline int TextWidthPx(EngineKernel& K, const std::string& Value)
    {
        return K.Text().TextWidthPx(Value);
    }

    inline int TextHeightPx(EngineKernel& K, const std::string& Value)
    {
        return K.Text().TextHeightPx(Value);
    }

    inline int Asc(const std::string& Value)
    {
        if (Value.empty())
        {
            return 0;
        }
        return static_cast<int>(static_cast<unsigned char>(Value[0]));
    }

    inline std::string Bin(int Value)
    {
        unsigned int Bits = static_cast<unsigned int>(Value);
        if (Bits == 0U)
        {
            return "0";
        }

        std::string Out;
        while (Bits != 0U)
        {
            Out.push_back((Bits & 1U) != 0U ? '1' : '0');
            Bits >>= 1U;
        }
        std::reverse(Out.begin(), Out.end());
        return Out;
    }

    inline std::string Chr(int Value)
    {
        return std::string(1, static_cast<char>(Value & 0xFF));
    }

    inline bool CenterText(EngineKernel& K, int X, int Y, const std::string& Value, std::string* Error = nullptr)
    {
        const std::int64_t Px = static_cast<std::int64_t>(X) - TextWidthPx(K, Value) / 2;
        if (Px < std::numeric_limits<int>::min())
        {
            if (Error)
                *Error = "CENTER TEXT position is out of integer range";
            return false;
        }
        return Text(K, static_cast<int>(Px), Y, Value, Error);
    }

    inline bool CenterText(EngineKernel& K, int X, int Y, const char* Value, std::string* Error = nullptr)
    {
        return CenterText(K, X, Y, Value ? std::string(Value) : std::string{}, Error);
    }

    inline bool CenterText(EngineKernel& K, int X, int Y, int Value, std::string* Error = nullptr)
    {
        return CenterText(K, X, Y, std::to_string(Value), Error);
    }

    inline bool CenterText(EngineKernel& K, int X, int Y, float Value, std::string* Error = nullptr)
    {
        return CenterText(K, X, Y, std::to_string(Value), Error);
    }

    inline bool CenterText(EngineKernel& K, int X, int Y, double Value, std::string* Error = nullptr)
    {
        return CenterText(K, X, Y, std::to_string(Value), Error);
    }

    inline std::string Hex(int Value)
    {
        constexpr char KHexDigits[] = "0123456789ABCDEF";
        unsigned int Bits = static_cast<unsigned int>(Value);
        if (Bits == 0U)
        {
            return "0";
        }

        std::string Out;
        while (Bits != 0U)
        {
            Out.push_back(KHexDigits[Bits & 0xFU]);
            Bits >>= 4U;
        }
        std::reverse(Out.begin(), Out.end());
        return Out;
    }

    inline int Instr(const std::string& Haystack, const std::string& Needle)
    {
        if (Needle.empty())
        {
            return 0;
        }
        const std::size_t Pos = Haystack.find(Needle);
        if (Pos == std::string::npos)
        {
            return 0;
        }
        return static_cast<int>(Pos) + 1; // DBA uses 1-based positions
    }

    inline std::string Append(const std::string& Left, const std::string& Right)
    {
        return Left + Right;
    }

    inline int CompareCase(const std::string& Left, const std::string& Right)
    {
        return Left == Right ? 1 : 0;
    }

    inline int FindFirstChar(const std::string& Source, const std::string& Character)
    {
        if (Character.empty())
            return 0;
        const std::size_t Position = Source.find(Character.front());
        return Position == std::string::npos ? 0 : static_cast<int>(Position + 1);
    }

    inline int FindLastChar(const std::string& Source, const std::string& Character)
    {
        if (Character.empty())
            return 0;
        const std::size_t Position = Source.rfind(Character.front());
        return Position == std::string::npos ? 0 : static_cast<int>(Position + 1);
    }

    inline int FindSubString(const std::string& Source, const std::string& Value)
    {
        if (Value.empty())
            return 0;
        const std::size_t Position = Source.find(Value);
        return Position == std::string::npos ? 0 : static_cast<int>(Position + 1);
    }

    inline std::string NextToken(interpreter::detail::BuiltinContext& Context, const std::string& Delimiter)
    {
        const std::string EffectiveDelimiter = Delimiter.empty() ? " " : Delimiter;
        const std::size_t Start = Context.TokenSource.find_first_not_of(EffectiveDelimiter, Context.TokenOffset);
        if (Start == std::string::npos)
        {
            Context.TokenOffset = Context.TokenSource.size();
            return {};
        }
        const std::size_t End = Context.TokenSource.find_first_of(EffectiveDelimiter, Start);
        Context.TokenOffset = End == std::string::npos ? Context.TokenSource.size() : End + 1;
        return Context.TokenSource.substr(Start, End == std::string::npos ? End : End - Start);
    }

    inline std::string FirstToken(interpreter::detail::BuiltinContext& Context, const std::string& Source,
                                  const std::string& Delimiter)
    {
        Context.TokenSource = Source;
        Context.TokenOffset = 0;
        return NextToken(Context, Delimiter);
    }

    inline std::string Reverse(std::string Value)
    {
        std::reverse(Value.begin(), Value.end());
        return Value;
    }

    inline std::string Left(const std::string& Value, int Count)
    {
        const int N = std::clamp(Count, 0, static_cast<int>(Value.size()));
        return Value.substr(0, static_cast<std::size_t>(N));
    }

    inline int Len(const std::string& Value)
    {
        return static_cast<int>(Value.size());
    }

    inline std::string Lower(const std::string& Value)
    {
        std::string Out = Value;
        std::transform(Out.begin(), Out.end(), Out.begin(),
                       [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        return Out;
    }

    inline std::string Mid(const std::string& Value, int StartPos, int Count = 1)
    {
        const int Start = std::clamp(StartPos - 1, 0, static_cast<int>(Value.size()));
        const int N = std::clamp(Count, 0, static_cast<int>(Value.size()) - Start);
        return Value.substr(static_cast<std::size_t>(Start), static_cast<std::size_t>(N));
    }

    inline void Print(EngineKernel& K, const std::string& Value)
    {
        Text(K, K.Text().State().CursorX, K.Text().State().CursorY, Value);
        K.Text().State().CursorX = 0;
        K.Text().State().CursorY += K.Text().State().Size;
    }

    inline void Printc(EngineKernel& K, const std::string& Value)
    {
        Text(K, K.Text().State().CursorX, K.Text().State().CursorY, Value);
        K.Text().State().CursorX += TextWidthPx(K, Value);
    }

    inline void Print(EngineKernel& K, const char* Value)
    {
        Print(K, Value ? std::string(Value) : std::string{});
    }

    inline void Print(EngineKernel& K, int Value)
    {
        Print(K, std::to_string(Value));
    }

    inline void Print(EngineKernel& K, float Value)
    {
        Print(K, std::to_string(Value));
    }

    inline void Print(EngineKernel& K, double Value)
    {
        Print(K, std::to_string(Value));
    }

    inline std::string Right(const std::string& Value, int Count)
    {
        const int N = std::clamp(Count, 0, static_cast<int>(Value.size()));
        return Value.substr(Value.size() - static_cast<std::size_t>(N));
    }

    inline void SetCursor(EngineKernel& K, int X, int Y)
    {
        K.Text().State().CursorX = X;
        K.Text().State().CursorY = Y;
    }

    inline void SetTextFont(EngineKernel& K, const std::string& Name)
    {
        K.Text().State().Font = Name;
    }

    inline void SetTextFont(EngineKernel& K, const char* Name)
    {
        K.Text().State().Font = Name ? std::string(Name) : std::string{};
    }

    inline void SetTextOpaque(EngineKernel& K)
    {
        K.Text().State().Opaque = true;
    }

    inline void SetTextSize(EngineKernel& K, int Size)
    {
        K.Text().State().Size = std::max(1, Size);
    }

    inline void SetTextToBold(EngineKernel& K)
    {
        K.Text().State().Style = 1;
    }

    inline void SetTextToBolditalic(EngineKernel& K)
    {
        K.Text().State().Style = 3;
    }

    inline void SetTextToItalic(EngineKernel& K)
    {
        K.Text().State().Style = 2;
    }

    inline void SetTextToNormal(EngineKernel& K)
    {
        K.Text().State().Style = 0;
    }

    inline void SetTextTransparent(EngineKernel& K)
    {
        K.Text().State().Opaque = false;
    }

    inline std::string Space(int Count)
    {
        return std::string(static_cast<std::size_t>(std::max(0, Count)), ' ');
    }

    inline std::string Str(const std::string& Value)
    {
        return Value;
    }

    inline std::string Str(const char* Value)
    {
        return Value ? std::string(Value) : std::string{};
    }

    inline std::string Str(int Value)
    {
        return std::to_string(Value);
    }

    inline std::string Str(float Value)
    {
        return std::to_string(Value);
    }

    inline std::string Str(double Value)
    {
        return std::to_string(Value);
    }

    inline int TextBackgroundType(EngineKernel& K)
    {
        return K.Text().State().Opaque ? 1 : 0;
    }

    inline std::string TextFont(EngineKernel& K)
    {
        return K.Text().State().Font;
    }

    inline int TextHeight(EngineKernel& K, const std::string& Value)
    {
        return TextHeightPx(K, Value);
    }

    inline int TextSize(EngineKernel& K)
    {
        return K.Text().State().Size;
    }

    inline int TextStyle(EngineKernel& K)
    {
        const int Style = K.Text().State().Style;
        if (Style == 1)
        {
            return 2;
        }
        if (Style == 2)
        {
            return 1;
        }
        if (Style == 3)
        {
            return 3;
        }
        return 0;
    }

    inline int TextWidth(EngineKernel& K, const std::string& Value)
    {
        return TextWidthPx(K, Value);
    }

    inline std::string Upper(const std::string& Value)
    {
        std::string Out = Value;
        std::transform(Out.begin(), Out.end(), Out.begin(),
                       [](unsigned char C) { return static_cast<char>(std::toupper(C)); });
        return Out;
    }

    inline double Val(const std::string& Value)
    {
        double Result = 0.0;
        std::string ConversionError;
        if (!language::ParseNumber(Value, Result, &ConversionError))
            return 0.0;
        return Result;
    }

    inline double Val(const char* Value)
    {
        return Val(Value ? std::string(Value) : std::string{});
    }

} // namespace dbc
