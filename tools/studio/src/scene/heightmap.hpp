#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

// Terrain heightmaps the Scene view writes and edits. MAKE TERRAIN reads a square image and takes
// each pixel's (R + G + B) / 765 as a height from 0 to KTerrainHeightScale.
namespace dbc::studio
{
    namespace scene
    {
        // One height per pixel, row by row, in world units from 0 to KTerrainHeightScale.
        struct Heightmap
        {
            int Size = 0;
            std::vector<float> Heights;

            bool operator==(const Heightmap&) const = default;
        };

        Heightmap FlatHeightmap(int Size, float Height);
        // The height at a pixel position, between the four pixels around it; the edges extend outward.
        float SampleHeightmap(const Heightmap& Map, float X, float Z);

        enum class TerrainPreset
        {
            RollingHills,
            Mountains,
            Plains,
            Islands,
            Desert,
        };

        inline constexpr int KTerrainPresetCount = 5;
        // A landscape from 0 up to `Range`. The same preset, seed and size give the same heights on
        // every platform.
        Heightmap GenerateHeightmap(int Size, TerrainPreset Preset, std::uint32_t Seed, float Range);

        enum class SculptMode
        {
            Raise,   // adds Amount units; a negative Amount lowers
            Smooth,  // moves each height toward its neighbours' mean by the fraction Amount
            Flatten, // moves each height toward Target by the fraction Amount
        };

        // A disc, in pixels: full strength up to Radius * (1 - Falloff), fading to nothing at Radius.
        struct TerrainBrush
        {
            float Radius = 12.0f;
            float Falloff = 0.5f;
        };

        float BrushWeight(const TerrainBrush& Brush, float Distance);
        // Applies one stamp centred on a pixel position. Returns whether a height changed.
        bool SculptHeightmap(Heightmap& Map, SculptMode Mode, const TerrainBrush& Brush, const glm::vec2& Center,
                             float Amount, float Target = 0.0f);

        // Where a level's generated heightmap for a terrain lives, relative to the asset root.
        std::filesystem::path TerrainHeightmapPath(const std::string& LevelName, int TerrainId);
        bool ReadHeightmap(const std::filesystem::path& Path, Heightmap& Out, std::string& Error);
        // A near-grey PNG: the three channels differ by one level at most, which gives 766 height
        // steps where a grey image has 256.
        bool WriteHeightmap(const std::filesystem::path& Path, const Heightmap& Map, std::string& Error);
    } // namespace scene

} // namespace dbc::studio
