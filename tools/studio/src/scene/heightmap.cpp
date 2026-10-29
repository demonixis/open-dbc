#include "scene/heightmap.hpp"
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
            std::uint8_t Level(float Height)
            {
                return static_cast<std::uint8_t>(
                    std::clamp(std::lround(Height / KTerrainHeightScale * 255.0f), 0L, 255L));
            }

            float SmoothStep(float Value)
            {
                return Value * Value * (3.0f - 2.0f * Value);
            }
        } // namespace

        Heightmap FlatHeightmap(int Size, float Height)
        {
            return Heightmap(static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size), Level(Height));
        }

        Heightmap RandomHeightmap(int Size, float Range, int Hills, std::mt19937& Random)
        {
            // Value noise: random heights on a coarse grid, smoothly interpolated, with
            // three octaves of halving amplitude.
            std::vector<float> Sum(static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size), 0.0f);
            std::uniform_real_distribution<float> Unit(0.0f, 1.0f);
            float Amplitude = 1.0f;
            int Cells = std::max(1, Hills);
            for (int Octave = 0; Octave < 3; ++Octave, Amplitude *= 0.5f, Cells *= 2)
            {
                std::vector<float> Grid(static_cast<std::size_t>(Cells + 1) * static_cast<std::size_t>(Cells + 1));
                for (float& Value : Grid)
                    Value = Unit(Random);
                const auto At = [&](int X, int Z) { return Grid[static_cast<std::size_t>(Z) * (Cells + 1) + X]; };
                for (int Z = 0; Z < Size; ++Z)
                    for (int X = 0; X < Size; ++X)
                    {
                        const float Fx = static_cast<float>(X) / std::max(1, Size - 1) * Cells;
                        const float Fz = static_cast<float>(Z) / std::max(1, Size - 1) * Cells;
                        const int Cx = std::min(static_cast<int>(Fx), Cells - 1);
                        const int Cz = std::min(static_cast<int>(Fz), Cells - 1);
                        const float Tx = SmoothStep(Fx - Cx), Tz = SmoothStep(Fz - Cz);
                        const float Top = At(Cx, Cz) + (At(Cx + 1, Cz) - At(Cx, Cz)) * Tx;
                        const float Bottom = At(Cx, Cz + 1) + (At(Cx + 1, Cz + 1) - At(Cx, Cz + 1)) * Tx;
                        Sum[static_cast<std::size_t>(Z) * Size + X] += (Top + (Bottom - Top) * Tz) * Amplitude;
                    }
            }
            // Stretch the result over the whole range.
            const auto [Low, High] = std::minmax_element(Sum.begin(), Sum.end());
            const float Span = *High - *Low;
            Heightmap Result(Sum.size());
            for (std::size_t Index = 0; Index < Sum.size(); ++Index)
                Result[Index] = Level(Span > 0.0f ? (Sum[Index] - *Low) / Span * Range : 0.0f);
            return Result;
        }

        std::filesystem::path TerrainHeightmapPath(const std::string& LevelName, int TerrainId)
        {
            return dbc::PathFromUtf8("heightmaps") /
                   (SanitizeLevelName(LevelName) + "_terrain" + std::to_string(TerrainId) + ".png");
        }

        bool WriteHeightmap(const std::filesystem::path& Path, int Size, const Heightmap& Levels, std::string& Error)
        {
            if (Size < 2 || Levels.size() != static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size))
            {
                Error = "A heightmap must be square and at least 2x2";
                return false;
            }
            std::error_code FileError;
            std::filesystem::create_directories(Path.parent_path(), FileError);
            if (FileError)
            {
                Error = "Cannot create " + dbc::PathToUtf8(Path.parent_path()) + ": " + FileError.message();
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
                    std::fill_n(Row + X * 3, 3, Levels[static_cast<std::size_t>(Z) * Size + X]);
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
                Error = "Cannot write " + dbc::PathToUtf8(Path) + ": " + FileError.message();
                std::filesystem::remove(Temporary, FileError);
                return false;
            }
            return true;
        }
    } // namespace scene

} // namespace dbc::studio
