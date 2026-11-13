#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        // One entry of the textures shipped with the Studio: a texture, or the six faces of a cube map.
        struct LibraryTexture
        {
            // Shown under the tile: the file name without its extension, or a cube map's without its faces.
            std::string Name;
            // What the name begins with, up to its variant number: "grass", "day_cube".
            std::string Category;
            std::vector<std::filesystem::path> Files;
            // The file shown on the tile.
            std::filesystem::path Preview;
            bool CubeMap = false;
        };

        // The textures in `Folder`, sorted by name. The six files `<name>_cube_{px,nx,py,ny,pz,nz}` are one
        // cube map.
        std::vector<LibraryTexture> ScanTextureLibrary(const std::filesystem::path& Folder);

        // The folder of a project that library textures are copied to.
        std::filesystem::path LibraryTextureFolder(const std::filesystem::path& ProjectRoot);

        // True when every file of the texture is in the project already.
        bool HasLibraryTexture(const LibraryTexture& Texture, const std::filesystem::path& ProjectRoot);

        // Copies the texture's files into the project. A file already there under that name is kept: it
        // may be the user's own. False with `Error` set when a file cannot be written.
        bool AddLibraryTexture(const LibraryTexture& Texture, const std::filesystem::path& ProjectRoot,
                               std::string& Error);
    } // namespace core

} // namespace dbc::studio
