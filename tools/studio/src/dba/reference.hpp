#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dbc::studio
{
    namespace dba
    {
        // Documentation for DBA commands, parsed out of the markdown tables in
        // docs/darkbasic_reference.md. Feeds the command database, the editor hover
        // tooltips and the autocomplete list.
        class Reference
        {
          public:
            struct Entry
            {
                // Full first cell of the table row, e.g. "LOAD BITMAP filename$, n".
                std::string Signature;

                // Second cell: one line of prose.
                std::string Description;
            };

            bool Load(const std::filesystem::path& Markdown);

            // Accepts the canonical historical spelling, case-insensitively.
            const Entry* Find(std::string_view Command) const;

            // Convenience for callers that only want the prose.
            const std::string* FindDescription(std::string_view Command) const;

            bool Empty() const
            {
                return Entries_.empty();
            }

          private:
            // Keys are lower case canonical spellings.
            std::unordered_map<std::string, Entry> Entries_;
        };
    } // namespace dba

} // namespace dbc::studio
