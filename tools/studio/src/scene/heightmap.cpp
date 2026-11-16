#include "scene/heightmap.hpp"
#include "core/i18n.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "scene/scene_document.hpp"
#include "scene/scene_geometry.hpp"

namespace dbc::studio
{
    namespace scene
    {
        namespace
        {
            // Height steps of an image: the sum of three 8-bit channels.
            constexpr int KLevels = 765;

            float SmoothStep(float Value)
            {
                return Value * Value * (3.0f - 2.0f * Value);
            }

            // A hash rather than a standard distribution: those differ between standard libraries.
            float Lattice(int X, int Z, std::uint32_t Seed)
            {
                std::uint32_t Hash = static_cast<std::uint32_t>(X) * 0x85ebca6bu ^
                                     static_cast<std::uint32_t>(Z) * 0xc2b2ae35u ^ Seed * 0x27d4eb2fu;
                Hash ^= Hash >> 15;
                Hash *= 0x2c1b3c6du;
                Hash ^= Hash >> 12;
                Hash *= 0x297a2d39u;
                Hash ^= Hash >> 15;
                return static_cast<float>(Hash & 0xffffffu) / static_cast<float>(0xffffffu);
            }

            // Value noise from 0 to 1: random heights on the whole coordinates, smoothly joined.
            float Noise(float X, float Z, std::uint32_t Seed)
            {
                const float FloorX = std::floor(X), FloorZ = std::floor(Z);
                const int Ix = static_cast<int>(FloorX), Iz = static_cast<int>(FloorZ);
                const float Tx = SmoothStep(X - FloorX), Tz = SmoothStep(Z - FloorZ);
                const float Top = std::lerp(Lattice(Ix, Iz, Seed), Lattice(Ix + 1, Iz, Seed), Tx);
                const float Bottom = std::lerp(Lattice(Ix, Iz + 1, Seed), Lattice(Ix + 1, Iz + 1, Seed), Tx);
                return std::lerp(Top, Bottom, Tz);
            }

            // Octaves of doubling frequency and halving amplitude, from 0 to 1. `Ridged` folds each
            // octave around its middle, which turns its slopes into crests.
            float Fractal(float X, float Z, std::uint32_t Seed, int Octaves, bool Ridged = false)
            {
                float Sum = 0.0f, Total = 0.0f, Amplitude = 1.0f;
                for (int Octave = 0; Octave < Octaves; ++Octave, Amplitude *= 0.5f, X *= 2.0f, Z *= 2.0f)
                {
                    float Value = Noise(X, Z, Seed + static_cast<std::uint32_t>(Octave) * 101u);
                    if (Ridged)
                        Value = 1.0f - std::abs(2.0f * Value - 1.0f);
                    Sum += Value * Amplitude;
                    Total += Amplitude;
                }
                return Sum / Total;
            }

            // One preset's shape at (U, V) in 0..1, before it is stretched over the height range.
            float PresetHeight(TerrainPreset Preset, float U, float V, std::uint32_t Seed)
            {
                switch (Preset)
                {
                    case TerrainPreset::RollingHills:
                        return Fractal(U * 4.0f, V * 4.0f, Seed, 3);
                    case TerrainPreset::Mountains:
                    {
                        const float Ridges = Fractal(U * 3.0f, V * 3.0f, Seed, 6, true);
                        return Ridges * Ridges * 0.75f + Fractal(U * 2.0f, V * 2.0f, Seed + 7u, 3) * 0.25f;
                    }
                    case TerrainPreset::Plains:
                    {
                        // Mostly low, with a few gentle rises.
                        const float Base = Fractal(U * 3.0f, V * 3.0f, Seed, 4);
                        return Base * Base * Base;
                    }
                    case TerrainPreset::Islands:
                    {
                        // The land sinks to sea level, 0, before the borders.
                        const float Distance = std::hypot(U - 0.5f, V - 0.5f) * 2.0f;
                        const float Shore = 1.0f - SmoothStep(std::clamp((Distance - 0.35f) / 0.6f, 0.0f, 1.0f));
                        return std::max(0.0f, Fractal(U * 4.0f, V * 4.0f, Seed, 5) * Shore * 1.3f - 0.2f);
                    }
                    case TerrainPreset::Desert:
                    {
                        // Dunes: crests along one direction, bent by low noise.
                        const float Bend = Fractal(U * 2.0f, V * 2.0f, Seed, 2);
                        const float Dune = 1.0f - std::abs(std::sin(3.14159265f * (U * 5.0f + Bend * 2.5f)));
                        const float Swell = Fractal(U * 1.5f, V * 1.5f, Seed + 13u, 2);
                        return Dune * Swell * 0.7f + Swell * 0.3f;
                    }
                }
                return 0.0f;
            }
        } // namespace

        Heightmap FlatHeightmap(int Size, float Height)
        {
            Heightmap Map;
            Map.Size = Size;
            Map.Heights.assign(static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size),
                               std::clamp(Height, 0.0f, KTerrainHeightScale));
            return Map;
        }

        float SampleHeightmap(const Heightmap& Map, float X, float Z)
        {
            if (Map.Size < 2)
                return Map.Heights.empty() ? 0.0f : Map.Heights.front();
            const float Limit = static_cast<float>(Map.Size - 1);
            X = std::clamp(X, 0.0f, Limit);
            Z = std::clamp(Z, 0.0f, Limit);
            const int Ix = std::min(static_cast<int>(X), Map.Size - 2);
            const int Iz = std::min(static_cast<int>(Z), Map.Size - 2);
            const auto At = [&](int Column, int Row)
            { return Map.Heights[static_cast<std::size_t>(Row) * Map.Size + Column]; };
            const float Tx = X - static_cast<float>(Ix), Tz = Z - static_cast<float>(Iz);
            return std::lerp(std::lerp(At(Ix, Iz), At(Ix + 1, Iz), Tx),
                             std::lerp(At(Ix, Iz + 1), At(Ix + 1, Iz + 1), Tx), Tz);
        }

        Heightmap GenerateHeightmap(int Size, TerrainPreset Preset, std::uint32_t Seed, float Range)
        {
            Heightmap Map;
            Map.Size = Size;
            Map.Heights.resize(static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size));
            const float Last = static_cast<float>(std::max(1, Size - 1));
            for (int Z = 0; Z < Size; ++Z)
                for (int X = 0; X < Size; ++X)
                    Map.Heights[static_cast<std::size_t>(Z) * Size + X] =
                        PresetHeight(Preset, static_cast<float>(X) / Last, static_cast<float>(Z) / Last, Seed);
            if (Map.Heights.empty())
                return Map;
            // Stretched over the whole range, whatever the seed drew.
            const auto [Low, High] = std::minmax_element(Map.Heights.begin(), Map.Heights.end());
            const float Base = *Low, Span = *High - *Low;
            const float Top = std::clamp(Range, 0.0f, KTerrainHeightScale);
            for (float& Height : Map.Heights)
                Height = Span > 0.0f ? (Height - Base) / Span * Top : 0.0f;
            return Map;
        }

        float BrushWeight(const TerrainBrush& Brush, float Distance)
        {
            if (Distance >= Brush.Radius)
                return 0.0f;
            const float Core = Brush.Radius * (1.0f - std::clamp(Brush.Falloff, 0.0f, 1.0f));
            if (Distance <= Core)
                return 1.0f;
            return 1.0f - SmoothStep((Distance - Core) / (Brush.Radius - Core));
        }

        bool SculptHeightmap(Heightmap& Map, SculptMode Mode, const TerrainBrush& Brush, const glm::vec2& Center,
                             float Amount, float Target)
        {
            if (Map.Size < 2 || Brush.Radius <= 0.0f)
                return false;
            const int X0 = std::max(0, static_cast<int>(std::floor(Center.x - Brush.Radius)));
            const int X1 = std::min(Map.Size - 1, static_cast<int>(std::ceil(Center.x + Brush.Radius)));
            const int Z0 = std::max(0, static_cast<int>(std::floor(Center.y - Brush.Radius)));
            const int Z1 = std::min(Map.Size - 1, static_cast<int>(std::ceil(Center.y + Brush.Radius)));
            if (X0 > X1 || Z0 > Z1)
                return false;
            // Smoothing reads the heights as they were before this stamp.
            const std::vector<float> Before = Mode == SculptMode::Smooth ? Map.Heights : std::vector<float>{};
            const auto Mean = [&](int X, int Z)
            {
                float Sum = 0.0f;
                int Count = 0;
                for (int Row = std::max(0, Z - 1); Row <= std::min(Map.Size - 1, Z + 1); ++Row)
                    for (int Column = std::max(0, X - 1); Column <= std::min(Map.Size - 1, X + 1); ++Column, ++Count)
                        Sum += Before[static_cast<std::size_t>(Row) * Map.Size + Column];
                return Sum / static_cast<float>(Count);
            };
            bool Changed = false;
            for (int Z = Z0; Z <= Z1; ++Z)
                for (int X = X0; X <= X1; ++X)
                {
                    const float Weight = BrushWeight(
                        Brush, std::hypot(static_cast<float>(X) - Center.x, static_cast<float>(Z) - Center.y));
                    if (Weight <= 0.0f)
                        continue;
                    float& Height = Map.Heights[static_cast<std::size_t>(Z) * Map.Size + X];
                    float Next = Height;
                    switch (Mode)
                    {
                        case SculptMode::Raise:
                            Next += Amount * Weight;
                            break;
                        case SculptMode::Smooth:
                            Next += (Mean(X, Z) - Height) * std::clamp(Amount * Weight, 0.0f, 1.0f);
                            break;
                        case SculptMode::Flatten:
                            Next += (Target - Height) * std::clamp(Amount * Weight, 0.0f, 1.0f);
                            break;
                    }
                    Next = std::clamp(Next, 0.0f, KTerrainHeightScale);
                    Changed = Changed || Next != Height;
                    Height = Next;
                }
            return Changed;
        }

        std::filesystem::path TerrainHeightmapPath(const std::string& LevelName, int TerrainId)
        {
            return dbc::PathFromUtf8("heightmaps") /
                   (SanitizeLevelName(LevelName) + "_terrain" + std::to_string(TerrainId) + ".png");
        }

        bool ReadHeightmap(const std::filesystem::path& Path, Heightmap& Out, std::string& Error)
        {
            SDL_Surface* Loaded = IMG_Load(dbc::PathToUtf8(Path).c_str());
            if (Loaded == nullptr)
            {
                Error = SDL_GetError();
                return false;
            }
            SDL_Surface* Pixels = SDL_ConvertSurface(Loaded, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(Loaded);
            if (Pixels == nullptr)
            {
                Error = SDL_GetError();
                return false;
            }
            const int Size = Pixels->w;
            if (Size < 2 || Pixels->h != Size)
            {
                SDL_DestroySurface(Pixels);
                Error = Tr("A terrain heightmap must be square and at least 2x2");
                return false;
            }
            Out.Size = Size;
            Out.Heights.resize(static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size));
            for (int Z = 0; Z < Size; ++Z)
            {
                const auto* Row =
                    static_cast<const Uint8*>(Pixels->pixels) + static_cast<std::size_t>(Z) * Pixels->pitch;
                for (int X = 0; X < Size; ++X)
                {
                    const Uint8* Pixel = Row + static_cast<std::size_t>(X) * 4;
                    Out.Heights[static_cast<std::size_t>(Z) * Size + X] =
                        static_cast<float>(Pixel[0] + Pixel[1] + Pixel[2]) / static_cast<float>(KLevels) *
                        KTerrainHeightScale;
                }
            }
            SDL_DestroySurface(Pixels);
            return true;
        }

        bool WriteHeightmap(const std::filesystem::path& Path, const Heightmap& Map, std::string& Error)
        {
            const int Size = Map.Size;
            if (Size < 2 || Map.Heights.size() != static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size))
            {
                Error = Tr("A heightmap must be square and at least 2x2");
                return false;
            }
            std::error_code FileError;
            std::filesystem::create_directories(Path.parent_path(), FileError);
            if (FileError)
            {
                Error = TrFmt("Cannot create {0}: {1}", {dbc::PathToUtf8(Path.parent_path()), FileError.message()});
                return false;
            }
            SDL_Surface* Surface = SDL_CreateSurface(Size, Size, SDL_PIXELFORMAT_RGB24);
            if (Surface == nullptr)
            {
                Error = SDL_GetError();
                return false;
            }
            for (int Z = 0; Z < Size; ++Z)
            {
                auto* Row = static_cast<std::uint8_t*>(Surface->pixels) + static_cast<std::size_t>(Z) * Surface->pitch;
                for (int X = 0; X < Size; ++X)
                {
                    const float Height = Map.Heights[static_cast<std::size_t>(Z) * Size + X];
                    const int Level = static_cast<int>(
                        std::clamp(std::lround(Height / KTerrainHeightScale * static_cast<float>(KLevels)), 0L, 765L));
                    // The remainder of the division by three goes to red, then green.
                    Row[X * 3 + 0] = static_cast<std::uint8_t>((Level + 2) / 3);
                    Row[X * 3 + 1] = static_cast<std::uint8_t>((Level + 1) / 3);
                    Row[X * 3 + 2] = static_cast<std::uint8_t>(Level / 3);
                }
            }
            // Written beside the target first, so a failed save leaves the old image intact.
            std::filesystem::path Temporary = Path;
            Temporary += ".tmp";
            const bool Saved = IMG_SavePNG(Surface, dbc::PathToUtf8(Temporary).c_str());
            SDL_DestroySurface(Surface);
            if (!Saved)
            {
                Error = SDL_GetError();
                std::filesystem::remove(Temporary, FileError);
                return false;
            }
            std::filesystem::rename(Temporary, Path, FileError);
            if (FileError)
            {
                Error = TrFmt("Cannot write {0}: {1}", {dbc::PathToUtf8(Path), FileError.message()});
                std::filesystem::remove(Temporary, FileError);
                return false;
            }
            return true;
        }
    } // namespace scene

} // namespace dbc::studio
