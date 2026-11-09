#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ImHTML
{
    class Document;
}

namespace dbc::studio::core
{
    class ReferenceArchive;
}

namespace dbc::studio::panels
{
    class ReferencePanel
    {
      public:
        ReferencePanel();
        ~ReferencePanel();

        void Open();
        void Draw(const core::ReferenceArchive& Archive);

      private:
        struct Location
        {
            std::string Page;
            std::string Anchor;
            bool operator==(const Location&) const = default;
        };

        struct SearchEntry
        {
            Location Destination;
            std::string Name;
            std::string Category;
            std::string Kind;
            std::string Profile;
        };

        struct HistoryEntry
        {
            Location Destination;
            float ScrollY;
        };

        static std::optional<Location> Resolve(std::string_view Url, std::string_view CurrentPage);
        void LoadIndex(const core::ReferenceArchive& Archive);
        void UpdateMatches();
        void Navigate(const Location& Destination);
        void DrawMatches();
        void DrawPage(const core::ReferenceArchive& Archive);

        bool Visible_ = false;
        bool FocusRequested_ = false;
        bool IndexLoaded_ = false;
        bool ScrollRequested_ = false;
        std::array<char, 128> Filter_{};
        std::vector<SearchEntry> Entries_;
        std::vector<std::size_t> Matches_;
        std::vector<HistoryEntry> History_;
        Location Current_{"index.html", {}};
        float CurrentScrollY_ = 0.0f;
        std::optional<float> RestoreScrollY_;
        std::string LoadedPage_;
        std::optional<std::string> Html_;
        std::unique_ptr<ImHTML::Document> Document_;
        unsigned int ThemeTextColor_ = 0;
        float FontSize_ = 0.0f;
    };
} // namespace dbc::studio::panels
