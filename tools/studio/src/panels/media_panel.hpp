#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include "core/media_catalogue.hpp"

namespace dbc::studio
{
    namespace panels
    {
        // Media files under the project's asset root, filtered by kind and name.
        class MediaPanel
        {
          public:
            using FileHandler = std::function<void(const std::filesystem::path&)>;

            // Rescans only when the root changes; Refresh() forces it.
            void SetRoot(const std::filesystem::path& Root);
            void Refresh();
            void Draw();

            void SetOnActivate(FileHandler Handler)
            {
                OnActivate_ = std::move(Handler);
            }

          private:
            std::filesystem::path Root_;
            core::MediaScan Scan_;
            std::optional<core::MediaKind> Filter_;
            char Search_[128] = {};
            FileHandler OnActivate_;
        };
    } // namespace panels

} // namespace dbc::studio
