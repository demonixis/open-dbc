#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio::core
{
    class ReferenceArchive
    {
      public:
        // Data must remain immutable and alive while this archive is open.
        bool Open(std::string_view Data);
        bool IsOpen() const;
        std::optional<std::string> Read(std::string_view Name) const;

        // Publish the local browser pages and return the path to their index.
        std::optional<std::filesystem::path> Materialize(const std::filesystem::path& CacheDirectory) const;

      private:
        struct Entry
        {
            std::string_view Name;
            std::string_view Compressed;
            std::size_t Size;
        };

        const Entry* Find(std::string_view Name) const;
        static std::optional<std::string> Expand(const Entry& Item);

        std::string_view Data_;
        std::vector<Entry> Entries_;
    };
} // namespace dbc::studio::core
