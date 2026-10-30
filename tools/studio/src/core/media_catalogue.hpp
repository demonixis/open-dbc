#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        enum class MediaKind : std::uint8_t
        {
            Image,
            Audio,
            Model,
            Effect,
            Video,
            Font,
        };

        inline constexpr std::array<MediaKind, 6> KMediaKinds = {MediaKind::Image,  MediaKind::Audio, MediaKind::Model,
                                                                 MediaKind::Effect, MediaKind::Video, MediaKind::Font};

        // Plural label for the Media panel filter ("Images", "Audio", ...).
        const char* MediaKindLabel(MediaKind Kind);

        // "512 B", "1.5 KB", "3.0 MB" in the interface language.
        std::string FormatFileSize(std::uintmax_t Bytes);

        // Classifies by extension, case-insensitively; nullopt for non-media files.
        std::optional<MediaKind> ClassifyMedia(const std::filesystem::path& Path);

        struct MediaFile
        {
            std::filesystem::path Path;
            std::string RelativeName; // generic form, relative to the scanned root
            MediaKind Kind = MediaKind::Image;
            std::uintmax_t Size = 0;
        };

        struct MediaScan
        {
            std::vector<MediaFile> Files; // sorted by RelativeName
            bool Truncated = false;       // stopped at the file limit
        };

        // Walks `Root` recursively, skipping hidden entries, and keeps media files
        // only. Stops after `Limit` files so opening a huge folder stays cheap.
        MediaScan ScanMedia(const std::filesystem::path& Root, std::size_t Limit);
    } // namespace core

} // namespace dbc::studio
