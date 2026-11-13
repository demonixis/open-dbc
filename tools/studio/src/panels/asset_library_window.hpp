#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/asset_library.hpp"
#include "core/gpu_texture.hpp"

namespace dbc::studio
{
    namespace panels
    {
        // Assets > Texture Library: the textures shipped with the Studio as a grid of tiles. A click on
        // one copies it into the project's textures folder.
        class AssetLibraryWindow
        {
          public:
            void Initialize(SDL_GPUDevice* Gpu, std::filesystem::path Folder);
            // Frees the thumbnails; call before the GPU device goes.
            void Shutdown();

            void Open();

            // Returns the texture clicked this frame, which the caller adds to the project.
            std::optional<core::LibraryTexture> Draw(const std::filesystem::path& ProjectRoot);

          private:
            SDL_GPUDevice* Gpu_ = nullptr;
            std::filesystem::path Folder_;
            std::vector<core::LibraryTexture> Textures_;
            // By texture name; a failed load keeps an empty entry, so it is tried once.
            std::unordered_map<std::string, core::GpuImage> Thumbnails_;
            bool Scanned_ = false;
            bool Open_ = false;
        };
    } // namespace panels

} // namespace dbc::studio
