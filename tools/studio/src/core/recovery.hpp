#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dbc::studio::core
{
    // An editor tab whose changes are not on disk.
    struct UnsavedText
    {
        // Empty for an untitled tab.
        std::filesystem::path Path;
        std::string Title;
        // UTF-8, as the editor holds it.
        std::string Text;
    };

    struct RecoveryCopy
    {
        std::filesystem::path File;
        UnsavedText Content;
    };

    // Copies of unsaved tabs, one JSON file each. The Studio refreshes them before each run and every
    // KRecoveryInterval, and removes them when it quits normally: the copies found at startup are what a
    // crash would have lost. Source files themselves are only written when the user saves.
    class Recovery
    {
      public:
        static constexpr std::uint64_t KRecoveryInterval = 30'000; // milliseconds

        explicit Recovery(std::filesystem::path Directory = {}) : Directory_(std::move(Directory)) {}

        void SetDirectory(std::filesystem::path Directory)
        {
            Directory_ = std::move(Directory);
        }

        // Writes the copies whose text changed and removes those of tabs that are saved or closed.
        bool Update(const std::vector<UnsavedText>& Texts, std::string& Error);
        // Removes every copy this Studio wrote.
        void Clear();
        // Copies in the directory that this Studio did not write.
        std::vector<RecoveryCopy> Pending() const;
        void Remove(const std::filesystem::path& File);

      private:
        std::filesystem::path Directory_;
        // File name of each copy this Studio wrote, with a hash of its text.
        std::map<std::string, std::size_t> Written_;
    };
} // namespace dbc::studio::core
