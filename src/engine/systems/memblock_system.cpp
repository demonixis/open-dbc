#include "memblock_system.hpp"
#include "../engine_types.hpp"
#include "audio_system.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "object_manager.hpp"

#include <SDL3/SDL_surface.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

namespace dbc
{
    namespace
    {
        constexpr std::size_t ClassicMeshHeaderSize = 32u;
        constexpr std::size_t ClassicMeshFaceSize = 14u;
        constexpr std::size_t ClassicVectorSize = 12u;
        constexpr std::size_t ClassicTextureCoordinateSize = 8u;
        constexpr std::size_t ClassicSoundHeaderSize = 12u;
        constexpr std::size_t ProfessionalSoundHeaderSize = 28u;
        static_assert(sizeof(Vec3) == ClassicVectorSize);
        static_assert(sizeof(SDL_FPoint) == ClassicTextureCoordinateSize);

        bool HasRange(const std::vector<uint8_t>& Data, int Offset, std::size_t Size)
        {
            return Offset >= 0 && static_cast<std::size_t>(Offset) <= Data.size() &&
                   Size <= Data.size() - static_cast<std::size_t>(Offset);
        }

        template <typename T> T ReadScalar(const std::vector<uint8_t>& Data, std::size_t Offset)
        {
            T Value{};
            std::memcpy(&Value, Data.data() + Offset, sizeof(Value));
            return Value;
        }

        template <typename T> void WriteScalar(std::vector<uint8_t>& Data, std::size_t Offset, T Value)
        {
            std::memcpy(Data.data() + Offset, &Value, sizeof(Value));
        }

        bool RequireSection(const std::vector<uint8_t>& Data, uint32_t Offset, std::size_t Size, const char* Command,
                            const char* Section, std::string* Error = nullptr)
        {
            if (static_cast<std::size_t>(Offset) > Data.size() || Size > Data.size() - static_cast<std::size_t>(Offset))
            {
                if (Error)
                    *Error = std::string(Command) + " has an invalid " + Section + " section";
                return false;
            }
            return true;
        }

        bool CheckedSectionSize(std::size_t& Output, uint32_t Count, std::size_t ElementSize, const char* Command,
                                const char* Section, std::string* Error = nullptr)
        {
            if (Count > std::numeric_limits<std::size_t>::max() / ElementSize)
            {
                if (Error)
                    *Error = std::string(Command) + " " + Section + " section is too large";
                return false;
            }
            {
                Output = static_cast<std::size_t>(Count) * ElementSize;
                return true;
            }
        }

        bool RequireFinite(const Vec3& Value, const char* Command, std::string* Error = nullptr)
        {
            if (!std::isfinite(Value.X) || !std::isfinite(Value.Y) || !std::isfinite(Value.Z))
            {
                if (Error)
                    *Error = std::string(Command) + " contains a non-finite vector";
                return false;
            }
            return true;
        }

        bool RequireFinite(const SDL_FPoint& Value, const char* Command, std::string* Error = nullptr)
        {
            if (!std::isfinite(Value.x) || !std::isfinite(Value.y))
            {
                if (Error)
                    *Error = std::string(Command) + " contains non-finite texture coordinates";
                return false;
            }
            return true;
        }

        bool DecodeClassicMesh(Mesh& Output, const std::vector<uint8_t>& Data, std::string* Error = nullptr)
        {
            constexpr const char* Command = "MAKE MESH FROM MEMBLOCK";
            if (Data.size() < ClassicMeshHeaderSize)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a 32-byte Classic mesh header";
                return false;
            }

            const uint32_t VertexCount = ReadScalar<uint32_t>(Data, 0);
            const uint32_t VertexOffset = ReadScalar<uint32_t>(Data, 4);
            const uint32_t NormalCount = ReadScalar<uint32_t>(Data, 8);
            const uint32_t NormalOffset = ReadScalar<uint32_t>(Data, 12);
            const uint32_t FaceCount = ReadScalar<uint32_t>(Data, 16);
            const uint32_t FaceOffset = ReadScalar<uint32_t>(Data, 20);
            const uint32_t FaceDataSize = ReadScalar<uint32_t>(Data, 24);
            const uint32_t TextureOffset = ReadScalar<uint32_t>(Data, 28);
            std::size_t VertexBytes{};
            if (!CheckedSectionSize(VertexBytes, VertexCount, ClassicVectorSize, Command, "vertex", Error))
                return false;
            std::size_t NormalBytes{};
            if (!CheckedSectionSize(NormalBytes, NormalCount, ClassicVectorSize, Command, "normal", Error))
                return false;
            std::size_t RequiredFaceBytes{};
            if (!CheckedSectionSize(RequiredFaceBytes, FaceCount, ClassicMeshFaceSize, Command, "face", Error))
                return false;
            std::size_t TextureBytes{};
            if (!CheckedSectionSize(TextureBytes, VertexCount, ClassicTextureCoordinateSize, Command, "texture", Error))
                return false;
            if (FaceDataSize < RequiredFaceBytes)
            {
                if (Error)
                    *Error = std::string(Command) + " has truncated Classic face data";
                return false;
            }
            if (!RequireSection(Data, VertexOffset, VertexBytes, Command, "vertex", Error))
                return false;
            if (!RequireSection(Data, NormalOffset, NormalBytes, Command, "normal", Error))
                return false;
            if (!RequireSection(Data, FaceOffset, FaceDataSize, Command, "face", Error))
                return false;
            if (!RequireSection(Data, TextureOffset, TextureBytes, Command, "texture", Error))
                return false;
            if (VertexOffset < ClassicMeshHeaderSize ||
                static_cast<std::size_t>(NormalOffset) < static_cast<std::size_t>(VertexOffset) + VertexBytes ||
                static_cast<std::size_t>(FaceOffset) < static_cast<std::size_t>(NormalOffset) + NormalBytes ||
                static_cast<std::size_t>(TextureOffset) < static_cast<std::size_t>(FaceOffset) + FaceDataSize)
            {
                if (Error)
                    *Error = std::string(Command) + " has overlapping Classic mesh sections";
                return false;
            }

            std::vector<Vec3> Vertices(VertexCount);
            std::vector<Vec3> Normals(NormalCount);
            std::vector<SDL_FPoint> TextureCoordinates(VertexCount);
            for (uint32_t Index = 0; Index < VertexCount; ++Index)
            {
                Vertices[Index] = ReadScalar<Vec3>(Data, static_cast<std::size_t>(VertexOffset) +
                                                             static_cast<std::size_t>(Index) * ClassicVectorSize);
                TextureCoordinates[Index] =
                    ReadScalar<SDL_FPoint>(Data, static_cast<std::size_t>(TextureOffset) +
                                                     static_cast<std::size_t>(Index) * ClassicTextureCoordinateSize);
                if (!RequireFinite(Vertices[Index], Command, Error))
                    return false;
                if (!RequireFinite(TextureCoordinates[Index], Command, Error))
                    return false;
            }
            for (uint32_t Index = 0; Index < NormalCount; ++Index)
            {
                Normals[Index] = ReadScalar<Vec3>(Data, static_cast<std::size_t>(NormalOffset) +
                                                            static_cast<std::size_t>(Index) * ClassicVectorSize);
                if (!RequireFinite(Normals[Index], Command, Error))
                    return false;
            }

            Mesh Result;
            if (FaceCount == 0)
            {
                Result.Vertices = std::move(Vertices);
                Result.Normals.resize(Result.Vertices.size());
                for (std::size_t Index = 0; Index < Result.Normals.size() && Index < Normals.size(); ++Index)
                    Result.Normals[Index] = Normals[Index];
                Result.Uvs = std::move(TextureCoordinates);
                Result.DiffuseColors.assign(Result.Vertices.size(), 0xFFFFFFFFu);
                {
                    Output = std::move(Result);
                    return true;
                }
            }

            std::size_t ExpandedCount{};
            if (!CheckedSectionSize(ExpandedCount, FaceCount, 3u, Command, "expanded face", Error))
                return false;
            Result.Vertices.reserve(ExpandedCount);
            Result.Normals.reserve(ExpandedCount);
            Result.Uvs.reserve(ExpandedCount);
            Result.DiffuseColors.reserve(ExpandedCount);
            Result.Indices.reserve(ExpandedCount);
            for (uint32_t Face = 0; Face < FaceCount; ++Face)
            {
                const std::size_t Record =
                    static_cast<std::size_t>(FaceOffset) + static_cast<std::size_t>(Face) * ClassicMeshFaceSize;
                if (ReadScalar<uint16_t>(Data, Record) != 3u)
                {
                    if (Error)
                        *Error = std::string(Command) + " supports triangular Classic faces only";
                    return false;
                }
                for (std::size_t Corner = 0; Corner < 3u; ++Corner)
                {
                    const uint16_t VertexIndex = ReadScalar<uint16_t>(Data, Record + 2u + Corner * 4u);
                    const uint16_t NormalIndex = ReadScalar<uint16_t>(Data, Record + 4u + Corner * 4u);
                    if (VertexIndex >= Vertices.size() || NormalIndex >= Normals.size())
                    {
                        if (Error)
                            *Error = std::string(Command) + " has an out-of-range Classic face index";
                        return false;
                    }
                    Result.Vertices.push_back(Vertices[VertexIndex]);
                    Result.Normals.push_back(Normals[NormalIndex]);
                    Result.Uvs.push_back(TextureCoordinates[VertexIndex]);
                    Result.DiffuseColors.push_back(0xFFFFFFFFu);
                    Result.Indices.push_back(static_cast<uint32_t>(Result.Indices.size()));
                }
            }
            {
                Output = std::move(Result);
                return true;
            }
        }

        bool EncodeClassicMesh(std::vector<uint8_t>& Output, const Mesh& Source, std::string* Error = nullptr)
        {
            constexpr const char* Command = "MAKE MEMBLOCK FROM MESH";
            const bool Indexed = !Source.Indices.empty();
            const std::size_t VertexCount = Indexed ? Source.Indices.size() : Source.Vertices.size();
            if (VertexCount % 3u != 0u)
            {
                if (Error)
                    *Error = std::string(Command) + " requires triangular Classic geometry";
                return false;
            }
            if (VertexCount > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()) + 1u)
            {
                if (Error)
                    *Error = std::string(Command) + " exceeds the Classic 16-bit face index limit";
                return false;
            }

            const std::size_t FaceCount = VertexCount / 3u;
            const std::size_t VertexOffset = ClassicMeshHeaderSize;
            const std::size_t NormalOffset = VertexOffset + VertexCount * ClassicVectorSize;
            const std::size_t FaceOffset = NormalOffset + VertexCount * ClassicVectorSize;
            const std::size_t FaceDataSize = FaceCount * ClassicMeshFaceSize;
            const std::size_t TextureOffset = FaceOffset + FaceDataSize;
            const std::size_t TotalSize = TextureOffset + VertexCount * ClassicTextureCoordinateSize;
            if (TotalSize > std::numeric_limits<uint32_t>::max())
            {
                if (Error)
                    *Error = std::string(Command) + " Classic payload is too large";
                return false;
            }

            std::vector<uint8_t> Result(TotalSize, 0u);
            WriteScalar<uint32_t>(Result, 0, static_cast<uint32_t>(VertexCount));
            WriteScalar<uint32_t>(Result, 4, static_cast<uint32_t>(VertexOffset));
            WriteScalar<uint32_t>(Result, 8, static_cast<uint32_t>(VertexCount));
            WriteScalar<uint32_t>(Result, 12, static_cast<uint32_t>(NormalOffset));
            WriteScalar<uint32_t>(Result, 16, static_cast<uint32_t>(FaceCount));
            WriteScalar<uint32_t>(Result, 20, static_cast<uint32_t>(FaceOffset));
            WriteScalar<uint32_t>(Result, 24, static_cast<uint32_t>(FaceDataSize));
            WriteScalar<uint32_t>(Result, 28, static_cast<uint32_t>(TextureOffset));
            for (std::size_t Index = 0; Index < VertexCount; ++Index)
            {
                const uint32_t SourceIndex = Indexed ? Source.Indices[Index] : static_cast<uint32_t>(Index);
                if (SourceIndex >= Source.Vertices.size())
                {
                    if (Error)
                        *Error = std::string(Command) + " found an invalid mesh index";
                    return false;
                }
                const Vec3 Position = Source.Vertices[SourceIndex];
                const Vec3 Normal = SourceIndex < Source.Normals.size() ? Source.Normals[SourceIndex] : Vec3{};
                const SDL_FPoint TextureCoordinate =
                    SourceIndex < Source.Uvs.size() ? Source.Uvs[SourceIndex] : SDL_FPoint{};
                if (!RequireFinite(Position, Command, Error))
                    return false;
                if (!RequireFinite(Normal, Command, Error))
                    return false;
                if (!RequireFinite(TextureCoordinate, Command, Error))
                    return false;
                WriteScalar(Result, VertexOffset + Index * ClassicVectorSize, Position);
                WriteScalar(Result, NormalOffset + Index * ClassicVectorSize, Normal);
                WriteScalar(Result, TextureOffset + Index * ClassicTextureCoordinateSize, TextureCoordinate);
            }
            for (std::size_t Face = 0; Face < FaceCount; ++Face)
            {
                const std::size_t Record = FaceOffset + Face * ClassicMeshFaceSize;
                WriteScalar<uint16_t>(Result, Record, 3u);
                for (std::size_t Corner = 0; Corner < 3u; ++Corner)
                {
                    const uint16_t Index = static_cast<uint16_t>(Face * 3u + Corner);
                    WriteScalar<uint16_t>(Result, Record + 2u + Corner * 4u, Index);
                    WriteScalar<uint16_t>(Result, Record + 4u + Corner * 4u, Index);
                }
            }
            {
                Output = std::move(Result);
                return true;
            }
        }

        bool EncodeClassicSound(std::vector<uint8_t>& Output, const std::vector<uint8_t>& Professional,
                                std::string* Error = nullptr)
        {
            constexpr const char* Command = "MAKE MEMBLOCK FROM SOUND";
            if (Professional.size() <= ProfessionalSoundHeaderSize)
            {
                if (Error)
                    *Error = std::string(Command) + " requires decoded sound samples";
                return false;
            }
            const uint32_t Format = ReadScalar<uint32_t>(Professional, 0);
            const uint32_t Channels = ReadScalar<uint32_t>(Professional, 4);
            const uint32_t Frequency = ReadScalar<uint32_t>(Professional, 8);
            const uint32_t BlockAlign = ReadScalar<uint32_t>(Professional, 16);
            const uint32_t BitsPerSample = ReadScalar<uint32_t>(Professional, 20);
            const std::size_t PayloadSize = Professional.size() - ProfessionalSoundHeaderSize;
            if (Format != 1u || Channels == 0u || Channels > 8u || Frequency == 0u || BlockAlign == 0u ||
                BitsPerSample == 0u || BitsPerSample > 32u || BitsPerSample % 8u != 0u ||
                BlockAlign != Channels * (BitsPerSample / 8u) || PayloadSize % BlockAlign != 0u ||
                PayloadSize / BlockAlign > std::numeric_limits<uint32_t>::max())
            {
                if (Error)
                    *Error = std::string(Command) + " cannot represent this sound in the Classic layout";
                return false;
            }

            std::vector<uint8_t> Result(ClassicSoundHeaderSize + PayloadSize, 0u);
            WriteScalar<uint32_t>(Result, 0, BitsPerSample);
            WriteScalar<uint32_t>(Result, 4, Frequency);
            WriteScalar<uint32_t>(Result, 8, static_cast<uint32_t>(PayloadSize / BlockAlign));
            std::memcpy(Result.data() + ClassicSoundHeaderSize, Professional.data() + ProfessionalSoundHeaderSize,
                        PayloadSize);
            {
                Output = std::move(Result);
                return true;
            }
        }

        bool DecodeClassicSound(std::vector<uint8_t>& Output, const std::vector<uint8_t>& Classic,
                                std::string* Error = nullptr)
        {
            constexpr const char* Command = "MAKE SOUND FROM MEMBLOCK";
            if (Classic.size() <= ClassicSoundHeaderSize)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a 12-byte Classic sound header and samples";
                return false;
            }
            const uint32_t BitsPerSample = ReadScalar<uint32_t>(Classic, 0);
            const uint32_t Frequency = ReadScalar<uint32_t>(Classic, 4);
            const uint32_t SampleCount = ReadScalar<uint32_t>(Classic, 8);
            const std::size_t PayloadSize = Classic.size() - ClassicSoundHeaderSize;
            if (BitsPerSample == 0u || BitsPerSample > 32u || BitsPerSample % 8u != 0u || Frequency == 0u)
            {
                if (Error)
                    *Error = std::string(Command) + " has an invalid Classic sound header";
                return false;
            }

            const std::size_t BytesPerChannelSample = BitsPerSample / 8u;
            std::size_t BlockAlign = BytesPerChannelSample;
            if (SampleCount != 0u)
            {
                if (PayloadSize % SampleCount != 0u)
                {
                    if (Error)
                        *Error = std::string(Command) + " has a mismatched Classic sample count";
                    return false;
                }
                BlockAlign = PayloadSize / SampleCount;
            }
            if (BlockAlign == 0u || BlockAlign % BytesPerChannelSample != 0u || PayloadSize % BlockAlign != 0u ||
                BlockAlign / BytesPerChannelSample > 8u)
            {
                if (Error)
                    *Error = std::string(Command) + " has an invalid Classic sample layout";
                return false;
            }
            const std::size_t Channels = BlockAlign / BytesPerChannelSample;
            if (Channels == 0u || Frequency > std::numeric_limits<uint32_t>::max() / BlockAlign)
            {
                if (Error)
                    *Error = std::string(Command) + " Classic sound rate overflows";
                return false;
            }

            std::vector<uint8_t> Result(ProfessionalSoundHeaderSize + PayloadSize, 0u);
            WriteScalar<uint32_t>(Result, 0, 1u);
            WriteScalar<uint32_t>(Result, 4, static_cast<uint32_t>(Channels));
            WriteScalar<uint32_t>(Result, 8, Frequency);
            WriteScalar<uint32_t>(Result, 12, static_cast<uint32_t>(Frequency * BlockAlign));
            WriteScalar<uint32_t>(Result, 16, static_cast<uint32_t>(BlockAlign));
            WriteScalar<uint32_t>(Result, 20, BitsPerSample);
            WriteScalar<uint32_t>(Result, 24, 0u);
            std::memcpy(Result.data() + ProfessionalSoundHeaderSize, Classic.data() + ClassicSoundHeaderSize,
                        PayloadSize);
            {
                Output = std::move(Result);
                return true;
            }
        }

        bool PixelStride(std::size_t& Output, uint32_t Depth, const char* Command, std::string* Error = nullptr)
        {
            if (Depth == 16)
            {
                Output = 2u;
                return true;
            }
            if (Depth == 32)
            {
                Output = 4u;
                return true;
            }
            {
                if (Error)
                    *Error = std::string(Command) + " requires a 16-bit RGB565 or 32-bit RGBA payload";
                return false;
            }
        }

        bool PixelPayloadSize(std::size_t& Output, uint32_t Width, uint32_t Height, uint32_t Depth, const char* Command,
                              std::string* Error = nullptr)
        {
            if (Width == 0 || Height == 0 || Width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                Height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
            {
                if (Error)
                    *Error = std::string(Command) + " requires positive host-sized dimensions";
                return false;
            }
            std::size_t Stride{};
            if (!PixelStride(Stride, Depth, Command, Error))
                return false;
            if (static_cast<std::size_t>(Width) >
                (std::numeric_limits<std::size_t>::max() - 12u) / Stride / static_cast<std::size_t>(Height))
            {
                if (Error)
                    *Error = std::string(Command) + " dimensions overflow the host size";
                return false;
            }
            {
                Output = static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * Stride;
                return true;
            }
        }

        uint16_t PackRgb565(uint8_t Red, uint8_t Green, uint8_t Blue)
        {
            return static_cast<uint16_t>(((Red >> 3u) << 11u) | ((Green >> 2u) << 5u) | (Blue >> 3u));
        }

        uint8_t Expand5(uint16_t Value)
        {
            return static_cast<uint8_t>((Value << 3u) | (Value >> 2u));
        }

        uint8_t Expand6(uint16_t Value)
        {
            return static_cast<uint8_t>((Value << 2u) | (Value >> 4u));
        }

        bool EncodeSurface(std::vector<uint8_t>& Output, SDL_Surface* Surface, uint32_t Depth, const char* Command,
                           std::string* Error = nullptr)
        {
            const auto DestroySurface = [](SDL_Surface* Value) { SDL_DestroySurface(Value); };
            std::unique_ptr<SDL_Surface, decltype(DestroySurface)> Rgba(
                SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32), DestroySurface);
            if (!Rgba)
            {
                if (Error)
                    *Error = std::string(Command) + " conversion failed: " + SDL_GetError();
                return false;
            }

            const uint32_t Width = static_cast<uint32_t>(Rgba->w);
            const uint32_t Height = static_cast<uint32_t>(Rgba->h);
            std::size_t Stride{};
            if (!PixelStride(Stride, Depth, Command, Error))
                return false;
            std::size_t PayloadSize{};
            if (!PixelPayloadSize(PayloadSize, Width, Height, Depth, Command, Error))
                return false;
            std::vector<uint8_t> Data(12u + PayloadSize, 0u);
            std::memcpy(Data.data() + 0, &Width, 4);
            std::memcpy(Data.data() + 4, &Height, 4);
            std::memcpy(Data.data() + 8, &Depth, 4);

            if (!SDL_LockSurface(Rgba.get()))
            {
                if (Error)
                    *Error = std::string(Command) + " lock failed: " + SDL_GetError();
                return false;
            }
            const auto* Pixels = static_cast<const uint8_t*>(Rgba->pixels);
            for (uint32_t Y = 0; Y < Height; ++Y)
            {
                const uint8_t* Source = Pixels + static_cast<std::size_t>(Y) * Rgba->pitch;
                uint8_t* Destination = Data.data() + 12u + static_cast<std::size_t>(Y) * Width * Stride;
                for (uint32_t X = 0; Depth == 32 && X < Width; ++X)
                {
                    // DirectX A8R8G8B8: a little-endian ARGB dword, so blue is the first byte.
                    Destination[X * 4u] = Source[X * 4u + 2u];
                    Destination[X * 4u + 1u] = Source[X * 4u + 1u];
                    Destination[X * 4u + 2u] = Source[X * 4u];
                    Destination[X * 4u + 3u] = Source[X * 4u + 3u];
                }
                if (Depth == 32)
                    continue;
                for (uint32_t X = 0; X < Width; ++X)
                {
                    const uint16_t Pixel = PackRgb565(Source[X * 4u], Source[X * 4u + 1u], Source[X * 4u + 2u]);
                    Destination[X * 2u] = static_cast<uint8_t>(Pixel & 0xffu);
                    Destination[X * 2u + 1u] = static_cast<uint8_t>(Pixel >> 8u);
                }
            }
            SDL_UnlockSurface(Rgba.get());
            {
                Output = std::move(Data);
                return true;
            }
        }

        // KeepAlpha false makes every pixel opaque, as for X8R8G8B8 surfaces whose fourth byte is unused.
        SDL_Surface* DecodeSurface(const std::vector<uint8_t>& Memblock, const char* Command, bool KeepAlpha,
                                   std::string* Error = nullptr)
        {
            if (Memblock.size() < 12u)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a 12-byte image header";
                return nullptr;
            }

            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t Depth = 0;
            std::memcpy(&Width, Memblock.data() + 0, 4);
            std::memcpy(&Height, Memblock.data() + 4, 4);
            std::memcpy(&Depth, Memblock.data() + 8, 4);
            std::size_t Stride{};
            if (!PixelStride(Stride, Depth, Command, Error))
                return nullptr;
            std::size_t PayloadSize{};
            if (!PixelPayloadSize(PayloadSize, Width, Height, Depth, Command, Error))
                return nullptr;
            if (PayloadSize > Memblock.size() - 12u)
            {
                if (Error)
                    *Error = std::string(Command) + " pixel payload is truncated";
                return nullptr;
            }

            SDL_Surface* Surface =
                SDL_CreateSurface(static_cast<int>(Width), static_cast<int>(Height), SDL_PIXELFORMAT_RGBA32);
            if (!Surface)
            {
                if (Error)
                    *Error = std::string(Command) + " allocation failed: " + SDL_GetError();
                return nullptr;
            }
            if (!SDL_LockSurface(Surface))
            {
                SDL_DestroySurface(Surface);
                {
                    if (Error)
                        *Error = std::string(Command) + " lock failed: " + SDL_GetError();
                    return nullptr;
                }
            }
            auto* Pixels = static_cast<uint8_t*>(Surface->pixels);
            for (uint32_t Y = 0; Y < Height; ++Y)
            {
                const uint8_t* Source = Memblock.data() + 12u + static_cast<std::size_t>(Y) * Width * Stride;
                uint8_t* Destination = Pixels + static_cast<std::size_t>(Y) * Surface->pitch;
                for (uint32_t X = 0; Depth == 32 && X < Width; ++X)
                {
                    Destination[X * 4u] = Source[X * 4u + 2u];
                    Destination[X * 4u + 1u] = Source[X * 4u + 1u];
                    Destination[X * 4u + 2u] = Source[X * 4u];
                    Destination[X * 4u + 3u] = KeepAlpha ? Source[X * 4u + 3u] : 255u;
                }
                if (Depth == 32)
                    continue;
                for (uint32_t X = 0; X < Width; ++X)
                {
                    const uint16_t Pixel =
                        static_cast<uint16_t>(Source[X * 2u]) | (static_cast<uint16_t>(Source[X * 2u + 1u]) << 8u);
                    Destination[X * 4u] = Expand5(static_cast<uint16_t>((Pixel >> 11u) & 0x1fu));
                    Destination[X * 4u + 1u] = Expand6(static_cast<uint16_t>((Pixel >> 5u) & 0x3fu));
                    Destination[X * 4u + 2u] = Expand5(static_cast<uint16_t>(Pixel & 0x1fu));
                    Destination[X * 4u + 3u] = 255u;
                }
            }
            SDL_UnlockSurface(Surface);
            return Surface;
        }
    } // namespace

    MemblockSystem::MemblockSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    MemblockSystem::~MemblockSystem()
    {
        Memblocks_.clear();
    }

    std::unordered_map<int, std::vector<uint8_t>>& MemblockSystem::AllMemblocks()
    {
        return Memblocks_;
    }

    const std::unordered_map<int, std::vector<uint8_t>>& MemblockSystem::AllMemblocks() const
    {
        return Memblocks_;
    }

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    bool MemblockSystem::ValidateMemblockNumber(int Num, std::string* Error)
    {
        if (Num < 1 || Num > 255)
        {
            if (Error)
                *Error = "Memblock number must be between 1 and 255";
            return false;
        }
        return true;
    }

    bool MemblockSystem::ValidateRange(const std::vector<uint8_t>& Memblock, int Offset, std::size_t Size,
                                       std::string* Error)
    {
        if (!HasRange(Memblock, Offset, Size))
        {
            if (Error)
                *Error = "Memblock access is outside the allocated range";
            return false;
        }
        return true;
    }

    std::vector<uint8_t>* MemblockSystem::GetMemblock(int Num)
    {
        const auto It = Memblocks_.find(Num);
        return (It != Memblocks_.end()) ? &It->second : nullptr;
    }

    const std::vector<uint8_t>* MemblockSystem::GetMemblock(int Num) const
    {
        const auto It = Memblocks_.find(Num);
        return (It != Memblocks_.end()) ? &It->second : nullptr;
    }

    // ---------------------------------------------------------------------------
    // Allocation / destruction
    // ---------------------------------------------------------------------------

    bool MemblockSystem::MakeMemblock(int Num, int Size, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        if (Size <= 0)
        {
            if (Error)
                *Error = "Memblock size must be greater than zero";
            return false;
        }
        if (Memblocks_.contains(Num))
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " already exists";
            return false;
        }
        Memblocks_[Num].assign(static_cast<size_t>(Size), 0u);
        return true;
    }

    bool MemblockSystem::DeleteMemblock(int Num, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        if (Memblocks_.erase(Num) == 0)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        return true;
    }

    bool MemblockSystem::CopyMemblock(int Src, int Dst, int SrcOff, int DstOff, int Size, std::string* Error)
    {
        if (!ValidateMemblockNumber(Src, Error))
            return false;
        if (!ValidateMemblockNumber(Dst, Error))
            return false;
        const auto* Source = GetMemblock(Src);
        auto* Destination = GetMemblock(Dst);
        if (!Source)
        {
            if (Error)
                *Error = "Source memblock " + std::to_string(Src) + " does not exist";
            return false;
        }
        if (!Destination)
        {
            if (Error)
                *Error = "Destination memblock " + std::to_string(Dst) + " does not exist";
            return false;
        }
        if (Size < 0)
        {
            if (Error)
                *Error = "COPY MEMBLOCK requires a non-negative size";
            return false;
        }
        if (!ValidateRange(*Source, SrcOff, static_cast<std::size_t>(Size), Error))
            return false;
        if (!ValidateRange(*Destination, DstOff, static_cast<std::size_t>(Size), Error))
            return false;
        std::memmove(Destination->data() + DstOff, Source->data() + SrcOff, static_cast<std::size_t>(Size));
        return true;
    }

    bool MemblockSystem::ReplaceMemblock(int Num, std::vector<uint8_t> Data, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        if (Data.empty())
        {
            if (Error)
                *Error = "Memblock size must be greater than zero";
            return false;
        }
        Memblocks_[Num] = std::move(Data);
        return true;
    }

    const std::vector<uint8_t>* MemblockSystem::MemblockData(int Num, std::string* Error) const
    {
        if (!ValidateMemblockNumber(Num, Error))
            return nullptr;
        const auto* Result = GetMemblock(Num);
        if (!Result)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return nullptr;
        }
        return Result;
    }

    // ---------------------------------------------------------------------------
    // Write primitives
    // ---------------------------------------------------------------------------

    bool MemblockSystem::WriteMemblockByte(int Num, int Offset, int Val, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!ValidateRange(*M, Offset, 1, Error))
            return false;
        (*M)[static_cast<std::size_t>(Offset)] = static_cast<uint8_t>(Val);
        return true;
    }

    bool MemblockSystem::WriteMemblockWord(int Num, int Offset, int Val, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!ValidateRange(*M, Offset, 2, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        const uint16_t V = static_cast<uint16_t>(Val);
        std::memcpy(M->data() + O, &V, 2);
        return true;
    }

    bool MemblockSystem::WriteMemblockDword(int Num, int Offset, int Val, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!ValidateRange(*M, Offset, 4, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        const uint32_t V = static_cast<uint32_t>(Val);
        std::memcpy(M->data() + O, &V, 4);
        return true;
    }

    bool MemblockSystem::WriteMemblockFloat(int Num, int Offset, float Val, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!ValidateRange(*M, Offset, 4, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        std::memcpy(M->data() + O, &Val, 4);
        return true;
    }

    bool MemblockSystem::WriteMemblockString(int Num, int Offset, const char* S, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!S)
        {
            if (Error)
                *Error = "Memblock string source is null";
            return false;
        }
        const size_t O = static_cast<size_t>(Offset);
        const size_t Len = std::strlen(S) + 1; // include null terminator
        if (!ValidateRange(*M, Offset, Len, Error))
            return false;
        std::memcpy(M->data() + O, S, Len);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Read primitives
    // ---------------------------------------------------------------------------

    bool MemblockSystem::ReadMemblockByte(int& Output, int Num, int Offset, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        const auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!HasRange(*M, Offset, 1) && Kernel_.RuntimeCompatibility().NeutralOutOfRangeMemblockReads())
        {
            Output = 0;
            return true;
        }
        if (!ValidateRange(*M, Offset, 1, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        {
            Output = static_cast<int>((*M)[O]);
            return true;
        }
    }

    bool MemblockSystem::ReadMemblockWord(int& Output, int Num, int Offset, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        const auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!HasRange(*M, Offset, 2) && Kernel_.RuntimeCompatibility().NeutralOutOfRangeMemblockReads())
        {
            Output = 0;
            return true;
        }
        if (!ValidateRange(*M, Offset, 2, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        uint16_t V = 0;
        std::memcpy(&V, M->data() + O, 2);
        {
            Output = static_cast<int>(V);
            return true;
        }
    }

    bool MemblockSystem::ReadMemblockDword(int& Output, int Num, int Offset, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        const auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!HasRange(*M, Offset, 4) && Kernel_.RuntimeCompatibility().NeutralOutOfRangeMemblockReads())
        {
            Output = 0;
            return true;
        }
        if (!ValidateRange(*M, Offset, 4, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        uint32_t V = 0;
        std::memcpy(&V, M->data() + O, 4);
        {
            Output = static_cast<int>(V);
            return true;
        }
    }

    bool MemblockSystem::ReadMemblockFloat(float& Output, int Num, int Offset, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        const auto* M = GetMemblock(Num);
        if (!M)
        {
            if (Error)
                *Error = "Memblock " + std::to_string(Num) + " does not exist";
            return false;
        }
        if (!HasRange(*M, Offset, 4) && Kernel_.RuntimeCompatibility().NeutralOutOfRangeMemblockReads())
        {
            Output = 0.0f;
            return true;
        }
        if (!ValidateRange(*M, Offset, 4, Error))
            return false;
        const size_t O = static_cast<size_t>(Offset);
        float V = 0.0f;
        std::memcpy(&V, M->data() + O, 4);
        {
            Output = V;
            return true;
        }
    }

    // ---------------------------------------------------------------------------
    // Queries
    // ---------------------------------------------------------------------------

    bool MemblockSystem::MemblockExist(int& Output, int Num, std::string* Error)
    {
        if (!ValidateMemblockNumber(Num, Error))
            return false;
        {
            Output = Memblocks_.count(Num) ? 1 : 0;
            return true;
        }
    }

    bool MemblockSystem::MemblockSize(int& Output, int Num, std::string* Error)
    {
        const auto* Data = MemblockData(Num, Error);
        if (!Data)
            return false;
        if (Data->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            if (Error)
                *Error = "Memblock size exceeds the BASIC integer range";
            return false;
        }
        Output = static_cast<int>(Data->size());
        return true;
    }

    bool MemblockSystem::MakeMeshFromMemblock(int MeshId, int MemblockNum, std::string* Error)
    {
        if (!ValidateMemblockNumber(MemblockNum, Error))
            return false;
        if (MeshId < 1 || MeshId > 65535)
        {
            if (Error)
                *Error = "MAKE MESH FROM MEMBLOCK mesh number must be between 1 and 65535";
            return false;
        }
        const auto* Data = GetMemblock(MemblockNum);
        if (!Data)
        {
            if (Error)
                *Error = "MAKE MESH FROM MEMBLOCK requires existing memblock " + std::to_string(MemblockNum);
            return false;
        }
        if (Kernel_.RuntimeCompatibility().MeshMemblocks() == MeshMemblockLayout::Classic32ByteHeader)
        {
            Mesh Decoded;
            if (!DecodeClassicMesh(Decoded, *Data, Error))
                return false;
            Kernel_.Objects().StoreMesh(MeshId, std::move(Decoded));
            return true;
        }
        if (Data->size() < 12)
        {
            if (Error)
                *Error = "MAKE MESH FROM MEMBLOCK requires a 12-byte FVF header";
            return false;
        }

        uint32_t Fvf = 0;
        uint32_t Stride = 0;
        uint32_t VertexCount = 0;
        std::memcpy(&Fvf, Data->data(), 4);
        std::memcpy(&Stride, Data->data() + 4, 4);
        std::memcpy(&VertexCount, Data->data() + 8, 4);
        if ((Fvf & 0x400Eu) != 0x0002u)
        {
            if (Error)
                *Error = "MAKE MESH FROM MEMBLOCK supports FVF XYZ vertex positions";
            return false;
        }
        if (Stride < 12 || VertexCount > (Data->size() - 12) / Stride ||
            12u + static_cast<std::size_t>(VertexCount) * Stride != Data->size())
        {
            if (Error)
                *Error = "MAKE MESH FROM MEMBLOCK has an invalid FVF stride or vertex count";
            return false;
        }

        std::size_t NormalOffset = 0;
        std::size_t DiffuseOffset = 0;
        std::size_t UvOffset = 0;
        std::size_t MinimumStride = 12;
        if ((Fvf & 0x0010u) != 0)
        {
            NormalOffset = MinimumStride;
            MinimumStride += 12;
        }
        if ((Fvf & 0x0020u) != 0)
            MinimumStride += 4;
        if ((Fvf & 0x0040u) != 0)
        {
            DiffuseOffset = MinimumStride;
            MinimumStride += 4;
        }
        if ((Fvf & 0x0080u) != 0)
            MinimumStride += 4;
        const uint32_t TextureCount = (Fvf >> 8u) & 0xFu;
        if (TextureCount > 0)
        {
            UvOffset = MinimumStride;
            MinimumStride += 8;
        }
        if (MinimumStride > Stride)
        {
            if (Error)
                *Error = "MAKE MESH FROM MEMBLOCK FVF fields exceed the declared stride";
            return false;
        }

        Mesh Result;
        Result.Vertices.reserve(VertexCount);
        Result.Normals.reserve(VertexCount);
        Result.Uvs.reserve(VertexCount);
        Result.DiffuseColors.reserve(VertexCount);
        Result.Indices.reserve(VertexCount);
        for (uint32_t Index = 0; Index < VertexCount; ++Index)
        {
            const uint8_t* Vertex = Data->data() + 12 + static_cast<std::size_t>(Index) * Stride;
            Vec3 Position{};
            std::memcpy(&Position, Vertex, sizeof(Position));
            Result.Vertices.push_back(Position);
            Vec3 Normal{};
            if (NormalOffset != 0)
                std::memcpy(&Normal, Vertex + NormalOffset, sizeof(Normal));
            Result.Normals.push_back(Normal);
            uint32_t Diffuse = 0xFFFFFFFFu;
            if (DiffuseOffset != 0)
                std::memcpy(&Diffuse, Vertex + DiffuseOffset, sizeof(Diffuse));
            Result.DiffuseColors.push_back(Diffuse);
            SDL_FPoint Uv{};
            if (UvOffset != 0)
                std::memcpy(&Uv, Vertex + UvOffset, sizeof(Uv));
            Result.Uvs.push_back(Uv);
            Result.Indices.push_back(Index);
        }
        Kernel_.Objects().StoreMesh(MeshId, std::move(Result));
        return true;
    }

    bool MemblockSystem::ChangeMeshFromMemblock(int MeshId, int MemblockNum, std::string* Error)
    {
        if (Kernel_.Objects().MeshExist(MeshId) == 0)
        {
            if (Error)
                *Error = "CHANGE MESH FROM MEMBLOCK requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        if (!MakeMeshFromMemblock(MeshId, MemblockNum, Error))
            return false;
        return true;
    }

    bool MemblockSystem::MakeMemblockFromMesh(int MemblockNum, int MeshId, std::string* Error)
    {
        if (!ValidateMemblockNumber(MemblockNum, Error))
            return false;
        const std::vector<Mesh>* Meshes = Kernel_.Objects().GetMeshes(MeshId);
        if (!Meshes || Meshes->empty())
        {
            if (Error)
                *Error = "MAKE MEMBLOCK FROM MESH requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        const Mesh& Source = Meshes->front();
        if (Kernel_.RuntimeCompatibility().MeshMemblocks() == MeshMemblockLayout::Classic32ByteHeader)
        {
            std::vector<uint8_t> Encoded;
            if (!EncodeClassicMesh(Encoded, Source, Error))
                return false;
            if (!ReplaceMemblock(MemblockNum, std::move(Encoded), Error))
                return false;
            return true;
        }
        const bool Indexed = !Source.Indices.empty();
        const std::size_t VertexCount = Indexed ? Source.Indices.size() : Source.Vertices.size();
        constexpr uint32_t Fvf = 0x0152u;
        constexpr uint32_t Stride = 36u;
        if (VertexCount > (std::numeric_limits<std::size_t>::max() - 12u) / Stride ||
            VertexCount > std::numeric_limits<uint32_t>::max())
        {
            if (Error)
                *Error = "MAKE MEMBLOCK FROM MESH vertex data is too large";
            return false;
        }
        std::vector<uint8_t> Result(12u + VertexCount * Stride, 0u);
        const uint32_t Count = static_cast<uint32_t>(VertexCount);
        std::memcpy(Result.data(), &Fvf, 4);
        std::memcpy(Result.data() + 4, &Stride, 4);
        std::memcpy(Result.data() + 8, &Count, 4);
        for (std::size_t Index = 0; Index < VertexCount; ++Index)
        {
            const uint32_t SourceIndex = Indexed ? Source.Indices[Index] : static_cast<uint32_t>(Index);
            if (SourceIndex >= Source.Vertices.size())
            {
                if (Error)
                    *Error = "MAKE MEMBLOCK FROM MESH found an invalid mesh index";
                return false;
            }
            uint8_t* Vertex = Result.data() + 12u + Index * Stride;
            std::memcpy(Vertex, &Source.Vertices[SourceIndex], sizeof(Vec3));
            const Vec3 Normal = SourceIndex < Source.Normals.size() ? Source.Normals[SourceIndex] : Vec3{};
            std::memcpy(Vertex + 12, &Normal, sizeof(Vec3));
            const uint32_t Diffuse =
                SourceIndex < Source.DiffuseColors.size() ? Source.DiffuseColors[SourceIndex] : 0xFFFFFFFFu;
            std::memcpy(Vertex + 24, &Diffuse, 4);
            const SDL_FPoint Uv = SourceIndex < Source.Uvs.size() ? Source.Uvs[SourceIndex] : SDL_FPoint{};
            std::memcpy(Vertex + 28, &Uv, sizeof(SDL_FPoint));
        }
        Memblocks_[MemblockNum] = std::move(Result);
        return true;
    }

    // ---------------------------------------------------------------------------
    // ChangeMesh -- replace object limb mesh with mesh from AllMeshes()
    // ---------------------------------------------------------------------------

    bool MemblockSystem::ChangeMesh(int ObjId, int LimbId, int MeshId, std::string* Error)
    {
        if (LimbId < 0 || LimbId > 65535)
        {
            if (Error)
                *Error = "CHANGE MESH limb number must be between 0 and 65535";
            return false;
        }
        Object3D* Obj = Kernel_.Objects().GetObject(ObjId);
        if (!Obj)
            return true;
        const std::vector<Mesh>* Src = Kernel_.Objects().GetMeshes(MeshId);
        if (!Src || Src->empty())
            return true;

        auto& Meshes = Obj->Meshes;
        // Extend meshes vector if needed
        while (static_cast<int>(Meshes.size()) <= LimbId)
            Meshes.emplace_back();
        Meshes[static_cast<size_t>(LimbId)] = (*Src)[0];
        // Mesh replacement invalidates both historical and render bounds.
        Obj->BoundingRadius = -1.0f;
        Obj->RenderBoundingRadius = -1.0f;
        return true;
    }

    // ---------------------------------------------------------------------------
    // MakeMemblockFromBitmap -- 12-byte header (w,h,depth) + RGB565/RGBA pixels
    // ---------------------------------------------------------------------------

    bool MemblockSystem::MakeMemblockFromBitmap(int MemblockNum, int BitmapId, std::string* Error)
    {
        if (!ValidateMemblockNumber(MemblockNum, Error))
            return false;
        SDL_Surface* Surface =
            Kernel_.ImageStoreRef().RequireBitmapSurface(BitmapId, "MAKE MEMBLOCK FROM BITMAP", Error);
        if (!Surface)
            return false;
        const uint32_t Depth = BitmapId == 0 && Kernel_.Core().ScreenDepth() == 16 ? 16u : 32u;
        std::vector<uint8_t> Encoded;
        if (!EncodeSurface(Encoded, Surface, Depth, "MAKE MEMBLOCK FROM BITMAP", Error))
            return false;
        if (!ReplaceMemblock(MemblockNum, std::move(Encoded), Error))
            return false;
        return true;
    }

    // ---------------------------------------------------------------------------
    // MakeBitmapFromMemblock -- inverse: memblock → SDL_Surface bitmap
    // ---------------------------------------------------------------------------

    bool MemblockSystem::MakeBitmapFromMemblock(int BitmapId, int MemblockNum, std::string* Error)
    {
        const auto* Data = MemblockData(MemblockNum, Error);
        if (!Data)
            return false;
        // Bitmaps are opaque pictures: an RGB() dword without alpha still shows.
        SDL_Surface* Surface = DecodeSurface(*Data, "MAKE BITMAP FROM MEMBLOCK", false, Error);
        if (!Surface)
            return false;
        const bool Stored = Kernel_.ImageStoreRef().StoreBitmapFromSurface(BitmapId, Surface, Error);
        SDL_DestroySurface(Surface);
        if (!Stored)
            return false;
        return true;
    }

    // ---------------------------------------------------------------------------
    // MakeMemblockFromImage -- image → memblock (requires surface readback)
    // ---------------------------------------------------------------------------

    bool MemblockSystem::MakeMemblockFromImage(int MemblockNum, int ImageId, std::string* Error)
    {
        if (!ValidateMemblockNumber(MemblockNum, Error))
            return false;
        const Image* Img = Kernel_.ImageStoreRef().GetImage(ImageId);
        if (!Img || !Img->Surface)
        {
            if (Error)
                *Error = "MAKE MEMBLOCK FROM IMAGE requires an existing CPU-readable image";
            return false;
        }

        std::vector<uint8_t> Encoded;
        if (!EncodeSurface(Encoded, Img->Surface, 32u, "MAKE MEMBLOCK FROM IMAGE", Error))
            return false;
        if (!ReplaceMemblock(MemblockNum, std::move(Encoded), Error))
            return false;
        return true;
    }

    // ---------------------------------------------------------------------------
    // MakeImageFromMemblock -- memblock → image (via FillImageFromSurface)
    // ---------------------------------------------------------------------------

    bool MemblockSystem::MakeImageFromMemblock(int ImageId, int MemblockNum, std::string* Error)
    {
        const auto* Data = MemblockData(MemblockNum, Error);
        if (!Data)
            return false;
        SDL_Surface* Surf = DecodeSurface(*Data, "MAKE IMAGE FROM MEMBLOCK",
                                          Kernel_.RuntimeCompatibility().ImageMemblockAlpha(), Error);
        if (!Surf)
            return false;

        // Store directly via ImageStore — creates textures + retains surface copy
        const bool Stored = Kernel_.ImageStoreRef().StoreImageFromSurface(ImageId, Surf, Error);
        SDL_DestroySurface(Surf);
        if (!Stored)
            return false;
        return true;
    }

    bool MemblockSystem::MakeMemblockFromSound(int MemblockNum, int SoundId, std::string* Error)
    {
        std::vector<uint8_t> Data;
        if (!Kernel_.Audio().SoundMemblockData(Data, SoundId, Error))
            return false;
        if (Kernel_.RuntimeCompatibility().SoundMemblocks() == SoundMemblockLayout::Classic12ByteHeader)
        {
            std::vector<uint8_t> Encoded;
            if (!EncodeClassicSound(Encoded, Data, Error))
                return false;
            Data = std::move(Encoded);
        }
        if (!ReplaceMemblock(MemblockNum, std::move(Data), Error))
            return false;
        return true;
    }

    bool MemblockSystem::MakeSoundFromMemblock(int SoundId, int MemblockNum, std::string* Error)
    {
        const auto* DataPointer = MemblockData(MemblockNum, Error);
        if (!DataPointer)
            return false;
        const auto& Data = *DataPointer;
        if (Kernel_.RuntimeCompatibility().SoundMemblocks() == SoundMemblockLayout::Classic12ByteHeader)
        {
            std::vector<uint8_t> Decoded;
            if (!DecodeClassicSound(Decoded, Data, Error))
                return false;
            return Kernel_.Audio().MakeSoundFromMemblock(SoundId, Decoded, Error);
        }
        return Kernel_.Audio().MakeSoundFromMemblock(SoundId, Data, Error);
    }

} // namespace dbc
