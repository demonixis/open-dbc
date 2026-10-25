#include "platform_io.hpp"

#include <SDL3/SDL_error.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    constexpr uint32_t DdsMagic = 0x20534444u;
    constexpr uint32_t DdsPixelFormatAlphaPixels = 0x00000001u;
    constexpr uint32_t DdsPixelFormatFourCc = 0x00000004u;
    constexpr uint32_t DdsPixelFormatRgb = 0x00000040u;
    constexpr uint32_t DdsHeaderPitch = 0x00000008u;
    constexpr uint32_t DdsCaps2Cubemap = 0x00000200u;
    constexpr uint32_t DdsCaps2Volume = 0x00200000u;
    constexpr uint32_t MaximumDimension = 16384u;
    constexpr uint64_t MaximumPayloadBytes = 512u * 1024u * 1024u;

    constexpr uint32_t FourCc(char A, char B, char C, char D)
    {
        return static_cast<uint32_t>(A) | (static_cast<uint32_t>(B) << 8u) | (static_cast<uint32_t>(C) << 16u) |
               (static_cast<uint32_t>(D) << 24u);
    }

    uint32_t ReadU32(const uint8_t* Bytes)
    {
        return static_cast<uint32_t>(Bytes[0]) | (static_cast<uint32_t>(Bytes[1]) << 8u) |
               (static_cast<uint32_t>(Bytes[2]) << 16u) | (static_cast<uint32_t>(Bytes[3]) << 24u);
    }

    uint16_t ReadU16(const uint8_t* Bytes)
    {
        return static_cast<uint16_t>(Bytes[0]) | static_cast<uint16_t>(Bytes[1] << 8u);
    }

    bool ReadExact(SDL_IOStream* Io, void* Data, size_t Size)
    {
        auto* Output = static_cast<uint8_t*>(Data);
        while (Size > 0)
        {
            const size_t Read = SDL_ReadIO(Io, Output, Size);
            if (Read == 0)
                return false;
            Output += Read;
            Size -= Read;
        }
        return true;
    }

    SDL_Surface* DdsError(const char* Message)
    {
        SDL_SetError("DDS: %s", Message);
        return nullptr;
    }

    uint8_t ExpandChannel(uint32_t Value, uint32_t Mask, uint8_t Fallback)
    {
        if (Mask == 0)
            return Fallback;
        unsigned Shift = 0;
        while ((Mask & 1u) == 0u)
        {
            Mask >>= 1u;
            ++Shift;
        }
        const uint32_t Component = (Value >> Shift) & Mask;
        return static_cast<uint8_t>((Component * 255u + Mask / 2u) / Mask);
    }

    bool IsContiguousMask(uint32_t Mask)
    {
        if (Mask == 0u)
            return false;
        while ((Mask & 1u) == 0u)
            Mask >>= 1u;
        return (Mask & (Mask + 1u)) == 0u;
    }

    std::array<uint8_t, 4> DecodeRgb565(uint16_t Value)
    {
        const uint8_t Red5 = static_cast<uint8_t>((Value >> 11u) & 31u);
        const uint8_t Green6 = static_cast<uint8_t>((Value >> 5u) & 63u);
        const uint8_t Blue5 = static_cast<uint8_t>(Value & 31u);
        const uint8_t Red = static_cast<uint8_t>((Red5 << 3u) | (Red5 >> 2u));
        const uint8_t Green = static_cast<uint8_t>((Green6 << 2u) | (Green6 >> 4u));
        const uint8_t Blue = static_cast<uint8_t>((Blue5 << 3u) | (Blue5 >> 2u));
        return {Red, Green, Blue, 255u};
    }

    std::array<std::array<uint8_t, 4>, 4> DecodeDxtColors(const uint8_t* Block, bool ForceFourColors)
    {
        const uint16_t Color0 = ReadU16(Block);
        const uint16_t Color1 = ReadU16(Block + 2);
        std::array<std::array<uint8_t, 4>, 4> Colors{};
        Colors[0] = DecodeRgb565(Color0);
        Colors[1] = DecodeRgb565(Color1);
        if (Color0 > Color1 || ForceFourColors)
        {
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                Colors[2][Channel] = static_cast<uint8_t>((2u * Colors[0][Channel] + Colors[1][Channel]) / 3u);
                Colors[3][Channel] = static_cast<uint8_t>((Colors[0][Channel] + 2u * Colors[1][Channel]) / 3u);
            }
            Colors[2][3] = 255u;
            Colors[3][3] = 255u;
        }
        else
        {
            for (int Channel = 0; Channel < 3; ++Channel)
                Colors[2][Channel] = static_cast<uint8_t>((Colors[0][Channel] + Colors[1][Channel]) / 2u);
            Colors[2][3] = 255u;
            Colors[3] = {0u, 0u, 0u, 0u};
        }
        return Colors;
    }

    void WritePixel(SDL_Surface* Surface, uint32_t X, uint32_t Y, const std::array<uint8_t, 4>& Color)
    {
        auto* Pixel = static_cast<uint8_t*>(Surface->pixels) + static_cast<size_t>(Y) * Surface->pitch + X * 4u;
        Pixel[0] = Color[0];
        Pixel[1] = Color[1];
        Pixel[2] = Color[2];
        Pixel[3] = Color[3];
    }

    void DecodeDxtBlock(SDL_Surface* Surface, const uint8_t* Block, uint32_t BlockX, uint32_t BlockY, uint32_t Width,
                        uint32_t Height, uint32_t FourCcValue)
    {
        const bool Dxt1 = FourCcValue == FourCc('D', 'X', 'T', '1');
        const uint8_t* ColorBlock = Dxt1 ? Block : Block + 8;
        const auto Colors = DecodeDxtColors(ColorBlock, !Dxt1);
        const uint32_t ColorIndices = ReadU32(ColorBlock + 4);
        std::array<uint8_t, 16> Alpha{};
        Alpha.fill(255u);

        if (FourCcValue == FourCc('D', 'X', 'T', '3'))
        {
            uint64_t Bits = 0;
            for (int Byte = 0; Byte < 8; ++Byte)
                Bits |= static_cast<uint64_t>(Block[Byte]) << (Byte * 8u);
            for (int Pixel = 0; Pixel < 16; ++Pixel)
                Alpha[Pixel] = static_cast<uint8_t>(((Bits >> (Pixel * 4u)) & 0x0fu) * 17u);
        }
        else if (FourCcValue == FourCc('D', 'X', 'T', '5'))
        {
            std::array<uint8_t, 8> Palette{};
            Palette[0] = Block[0];
            Palette[1] = Block[1];
            if (Palette[0] > Palette[1])
            {
                for (int Index = 2; Index < 8; ++Index)
                    Palette[Index] = static_cast<uint8_t>(((8 - Index) * Palette[0] + (Index - 1) * Palette[1]) / 7);
            }
            else
            {
                for (int Index = 2; Index < 6; ++Index)
                    Palette[Index] = static_cast<uint8_t>(((6 - Index) * Palette[0] + (Index - 1) * Palette[1]) / 5);
                Palette[6] = 0u;
                Palette[7] = 255u;
            }
            uint64_t Bits = 0;
            for (int Byte = 0; Byte < 6; ++Byte)
                Bits |= static_cast<uint64_t>(Block[2 + Byte]) << (Byte * 8u);
            for (int Pixel = 0; Pixel < 16; ++Pixel)
                Alpha[Pixel] = Palette[(Bits >> (Pixel * 3u)) & 0x07u];
        }

        for (uint32_t Y = 0; Y < 4; ++Y)
        {
            for (uint32_t X = 0; X < 4; ++X)
            {
                const uint32_t TargetX = BlockX * 4u + X;
                const uint32_t TargetY = BlockY * 4u + Y;
                if (TargetX >= Width || TargetY >= Height)
                    continue;
                const uint32_t Pixel = Y * 4u + X;
                auto Color = Colors[(ColorIndices >> (Pixel * 2u)) & 0x03u];
                if (!Dxt1 || Color[3] != 0u)
                    Color[3] = Alpha[Pixel];
                WritePixel(Surface, TargetX, TargetY, Color);
            }
        }
    }

    SDL_Surface* LoadDdsSurface(SDL_IOStream* Io)
    {
        std::array<uint8_t, 128> Header{};
        if (SDL_SeekIO(Io, 0, SDL_IO_SEEK_SET) < 0 || !ReadExact(Io, Header.data(), Header.size()))
            return DdsError("truncated header");
        if (ReadU32(Header.data()) != DdsMagic || ReadU32(Header.data() + 4) != 124u ||
            ReadU32(Header.data() + 76) != 32u)
            return DdsError("invalid header");

        const uint32_t Flags = ReadU32(Header.data() + 8);
        const uint32_t Height = ReadU32(Header.data() + 12);
        const uint32_t Width = ReadU32(Header.data() + 16);
        const uint32_t PitchOrLinearSize = ReadU32(Header.data() + 20);
        const uint32_t Depth = ReadU32(Header.data() + 24);
        const uint32_t PixelFlags = ReadU32(Header.data() + 80);
        const uint32_t FourCcValue = ReadU32(Header.data() + 84);
        const uint32_t BitsPerPixel = ReadU32(Header.data() + 88);
        const uint32_t RedMask = ReadU32(Header.data() + 92);
        const uint32_t GreenMask = ReadU32(Header.data() + 96);
        const uint32_t BlueMask = ReadU32(Header.data() + 100);
        const uint32_t AlphaMask = ReadU32(Header.data() + 104);
        const uint32_t Caps2 = ReadU32(Header.data() + 112);

        if (Width == 0u || Height == 0u || Width > MaximumDimension || Height > MaximumDimension)
            return DdsError("invalid dimensions");
        if (Depth > 1u || (Caps2 & DdsCaps2Volume) != 0u)
            return DdsError("volume textures are unsupported");
        if ((Caps2 & DdsCaps2Cubemap) != 0u)
            return DdsError("cube containers are unsupported");
        if (static_cast<uint64_t>(Width) * Height * 4u > MaximumPayloadBytes)
            return DdsError("decoded surface is too large");

        const bool Compressed = (PixelFlags & DdsPixelFormatFourCc) != 0u;
        const bool SupportedCompression = FourCcValue == FourCc('D', 'X', 'T', '1') ||
                                          FourCcValue == FourCc('D', 'X', 'T', '3') ||
                                          FourCcValue == FourCc('D', 'X', 'T', '5');
        if (Compressed && !SupportedCompression)
            return DdsError("only DXT1, DXT3 and DXT5 are supported");
        if (!Compressed && ((PixelFlags & DdsPixelFormatRgb) == 0u || (BitsPerPixel != 24u && BitsPerPixel != 32u)))
            return DdsError("only RGB24 and RGB32 surfaces are supported");
        if (!Compressed)
        {
            const uint64_t ValidBits = (UINT64_C(1) << BitsPerPixel) - 1u;
            const uint32_t ColorMasks = RedMask | GreenMask | BlueMask;
            const bool Overlapping = (RedMask & GreenMask) != 0u || (RedMask & BlueMask) != 0u ||
                                     (GreenMask & BlueMask) != 0u || (AlphaMask & ColorMasks) != 0u;
            if (!IsContiguousMask(RedMask) || !IsContiguousMask(GreenMask) || !IsContiguousMask(BlueMask) ||
                Overlapping || (static_cast<uint64_t>(ColorMasks | AlphaMask) & ~ValidBits) != 0u ||
                ((PixelFlags & DdsPixelFormatAlphaPixels) != 0u && !IsContiguousMask(AlphaMask)))
                return DdsError("invalid RGB channel masks");
        }

        uint64_t PayloadSize = 0;
        uint32_t SourcePitch = 0;
        if (Compressed)
        {
            const uint64_t BlockWidth = (static_cast<uint64_t>(Width) + 3u) / 4u;
            const uint64_t BlockHeight = (static_cast<uint64_t>(Height) + 3u) / 4u;
            PayloadSize = BlockWidth * BlockHeight * (FourCcValue == FourCc('D', 'X', 'T', '1') ? 8u : 16u);
        }
        else
        {
            const uint64_t RowBytes = static_cast<uint64_t>(Width) * (BitsPerPixel / 8u);
            SourcePitch = static_cast<uint32_t>(RowBytes);
            if ((Flags & DdsHeaderPitch) != 0u && PitchOrLinearSize >= RowBytes)
                SourcePitch = PitchOrLinearSize;
            PayloadSize = static_cast<uint64_t>(SourcePitch) * Height;
        }
        if (PayloadSize == 0u || PayloadSize > MaximumPayloadBytes || PayloadSize > std::numeric_limits<size_t>::max())
            return DdsError("invalid mip payload size");

        std::vector<uint8_t> Payload(static_cast<size_t>(PayloadSize));
        if (!ReadExact(Io, Payload.data(), Payload.size()))
            return DdsError("truncated mip payload");

        SDL_Surface* Surface =
            SDL_CreateSurface(static_cast<int>(Width), static_cast<int>(Height), SDL_PIXELFORMAT_RGBA32);
        if (!Surface)
            return nullptr;
        if (!SDL_LockSurface(Surface))
        {
            SDL_DestroySurface(Surface);
            return nullptr;
        }

        if (Compressed)
        {
            const uint32_t BlockWidth = (Width + 3u) / 4u;
            const uint32_t BlockHeight = (Height + 3u) / 4u;
            const uint32_t BlockSize = FourCcValue == FourCc('D', 'X', 'T', '1') ? 8u : 16u;
            for (uint32_t BlockY = 0; BlockY < BlockHeight; ++BlockY)
            {
                for (uint32_t BlockX = 0; BlockX < BlockWidth; ++BlockX)
                {
                    const size_t Offset = (static_cast<size_t>(BlockY) * BlockWidth + BlockX) * BlockSize;
                    DecodeDxtBlock(Surface, Payload.data() + Offset, BlockX, BlockY, Width, Height, FourCcValue);
                }
            }
        }
        else
        {
            const uint32_t BytesPerPixel = BitsPerPixel / 8u;
            const bool HasAlpha = (PixelFlags & DdsPixelFormatAlphaPixels) != 0u && AlphaMask != 0u;
            for (uint32_t Y = 0; Y < Height; ++Y)
            {
                const uint8_t* Row = Payload.data() + static_cast<size_t>(Y) * SourcePitch;
                for (uint32_t X = 0; X < Width; ++X)
                {
                    uint32_t Value = 0;
                    for (uint32_t Byte = 0; Byte < BytesPerPixel; ++Byte)
                        Value |= static_cast<uint32_t>(Row[X * BytesPerPixel + Byte]) << (Byte * 8u);
                    WritePixel(Surface, X, Y,
                               {ExpandChannel(Value, RedMask, 0u), ExpandChannel(Value, GreenMask, 0u),
                                ExpandChannel(Value, BlueMask, 0u),
                                ExpandChannel(Value, HasAlpha ? AlphaMask : 0u, 255u)});
                }
            }
        }

        SDL_UnlockSurface(Surface);
        SDL_ClearError();
        return Surface;
    }
} // namespace

namespace dbc
{
    SDL_Surface* LoadSurface(const std::string& Path)
    {
        SDL_IOStream* Io = SDL_IOFromFile(Path.c_str(), "rb");
        if (!Io)
            return nullptr;

        if (SDL_Surface* Surface = IMG_Load_IO(Io, false))
        {
            SDL_CloseIO(Io);
            return Surface;
        }

        if (FileExtensionLower(Path) == ".dds")
        {
            SDL_Surface* Surface = LoadDdsSurface(Io);
            SDL_CloseIO(Io);
            return Surface;
        }

        if (FileExtensionLower(Path) == ".tga")
        {
            SDL_SeekIO(Io, 0, SDL_IO_SEEK_SET);
            SDL_Surface* Surface = IMG_LoadTyped_IO(Io, false, "TGA");
            SDL_CloseIO(Io);
            if (Surface)
                SDL_ClearError();
            return Surface;
        }

        SDL_SeekIO(Io, 0, SDL_IO_SEEK_SET);
        SDL_Surface* Surface = SDL_LoadBMP_IO(Io, true);
        if (Surface)
            SDL_ClearError();
        return Surface;
    }
} // namespace dbc
