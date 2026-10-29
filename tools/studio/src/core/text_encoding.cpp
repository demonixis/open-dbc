#include "text_encoding.hpp"

#include <array>
#include <cstdint>

namespace dbc::studio::core
{
    namespace
    {
        constexpr std::string_view KUtf8Bom = "\xEF\xBB\xBF";

        // Windows-1252 bytes 0x80 to 0x9F. The five unassigned bytes keep their C1 code points, as in the
        // WHATWG mapping, so every byte survives a load and a save.
        constexpr std::array<char32_t, 32> KWindows1252High = {
            0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
            0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
            0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
        };

        // Reads one code point; false on a malformed, overlong or surrogate sequence.
        bool ReadUtf8(std::string_view Text, std::size_t& Index, char32_t& Result)
        {
            const auto Byte = [&](std::size_t Offset) { return static_cast<unsigned char>(Text[Index + Offset]); };
            const unsigned char Lead = Byte(0);
            std::size_t Length = 1;
            char32_t Minimum = 0;
            if (Lead < 0x80)
            {
                Result = Lead;
            }
            else if ((Lead & 0xE0) == 0xC0)
            {
                Length = 2;
                Minimum = 0x80;
                Result = Lead & 0x1F;
            }
            else if ((Lead & 0xF0) == 0xE0)
            {
                Length = 3;
                Minimum = 0x800;
                Result = Lead & 0x0F;
            }
            else if ((Lead & 0xF8) == 0xF0)
            {
                Length = 4;
                Minimum = 0x10000;
                Result = Lead & 0x07;
            }
            else
            {
                return false;
            }
            if (Index + Length > Text.size())
                return false;
            for (std::size_t Offset = 1; Offset < Length; ++Offset)
            {
                if ((Byte(Offset) & 0xC0) != 0x80)
                    return false;
                Result = (Result << 6) | (Byte(Offset) & 0x3F);
            }
            if (Result < Minimum || Result > 0x10FFFF || (Result >= 0xD800 && Result <= 0xDFFF))
                return false;
            Index += Length;
            return true;
        }

        void AppendUtf8(std::string& Output, char32_t Value)
        {
            if (Value < 0x80)
            {
                Output.push_back(static_cast<char>(Value));
            }
            else if (Value < 0x800)
            {
                Output.push_back(static_cast<char>(0xC0 | (Value >> 6)));
                Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
            }
            else if (Value < 0x10000)
            {
                Output.push_back(static_cast<char>(0xE0 | (Value >> 12)));
                Output.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3F)));
                Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
            }
            else
            {
                Output.push_back(static_cast<char>(0xF0 | (Value >> 18)));
                Output.push_back(static_cast<char>(0x80 | ((Value >> 12) & 0x3F)));
                Output.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3F)));
                Output.push_back(static_cast<char>(0x80 | (Value & 0x3F)));
            }
        }

        bool IsUtf8(std::string_view Text)
        {
            std::size_t Index = 0;
            char32_t Ignored = 0;
            while (Index < Text.size())
            {
                if (!ReadUtf8(Text, Index, Ignored))
                    return false;
            }
            return true;
        }

        std::optional<char> Windows1252Byte(char32_t Value)
        {
            if (Value < 0x80 || (Value >= 0xA0 && Value <= 0xFF))
                return static_cast<char>(Value);
            for (std::size_t Index = 0; Index < KWindows1252High.size(); ++Index)
            {
                if (KWindows1252High[Index] == Value)
                    return static_cast<char>(0x80 + Index);
            }
            return std::nullopt;
        }
    } // namespace

    std::string DecodeText(std::string_view Bytes, TextFormat& Format)
    {
        Format = {};
        Format.Bom = Bytes.starts_with(KUtf8Bom);
        if (Format.Bom)
            Bytes.remove_prefix(KUtf8Bom.size());

        std::size_t Crlf = 0;
        std::size_t Lf = 0;
        for (std::size_t Index = 0; Index < Bytes.size(); ++Index)
        {
            if (Bytes[Index] != '\n')
                continue;
            if (Index > 0 && Bytes[Index - 1] == '\r')
                ++Crlf;
            else
                ++Lf;
        }
        Format.Crlf = Crlf > Lf;

        Format.Windows1252 = !IsUtf8(Bytes);
        if (!Format.Windows1252)
            return std::string(Bytes);
        std::string Text;
        Text.reserve(Bytes.size());
        for (const char Character : Bytes)
        {
            const auto Byte = static_cast<unsigned char>(Character);
            AppendUtf8(Text, Byte >= 0x80 && Byte < 0xA0 ? KWindows1252High[Byte - 0x80] : char32_t{Byte});
        }
        return Text;
    }

    std::optional<std::string> EncodeText(std::string_view Text, const TextFormat& Format)
    {
        std::string Bytes;
        Bytes.reserve(Text.size() + Text.size() / 32 + KUtf8Bom.size());
        if (Format.Bom)
            Bytes += KUtf8Bom;
        std::size_t Index = 0;
        while (Index < Text.size())
        {
            const std::size_t Begin = Index;
            char32_t Value = 0;
            if (!ReadUtf8(Text, Index, Value))
                return std::nullopt;
            if (Value == '\n' && Format.Crlf)
                Bytes.push_back('\r');
            if (!Format.Windows1252)
            {
                Bytes.append(Text.substr(Begin, Index - Begin));
                continue;
            }
            const std::optional<char> Byte = Windows1252Byte(Value);
            if (!Byte)
                return std::nullopt;
            Bytes.push_back(*Byte);
        }
        return Bytes;
    }
} // namespace dbc::studio::core
