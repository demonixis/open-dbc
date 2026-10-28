#pragma once

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

// Terrain heightmaps the Scene view writes. MAKE TERRAIN reads a square image whose
// grey levels 0-255 map to heights 0-KTerrainHeightScale.
namespace dbc::studio
{
    namespace scene
    {
        // One grey level per pixel, row by row.
        using Heightmap = std::vector<std::uint8_t>;

        Heightmap FlatHeightmap(int Size, float Height);
        // Rolling hills from 0 up to `Range`: `Hills` bumps across, with finer detail on top.
        Heightmap RandomHeightmap(int Size, float Range, int Hills, std::mt19937& Random);

        // Where a level's generated heightmap for a terrain lives, relative to the asset root.
        std::filesystem::path TerrainHeightmapPath(const std::string& LevelName, int TerrainId);
        bool WriteHeightmap(const std::filesystem::path& Path, int Size, const Heightmap& Levels, std::string& Error);
    } // namespace scene

} // namespace dbc::studio
