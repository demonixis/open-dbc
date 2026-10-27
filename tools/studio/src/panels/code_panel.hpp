#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "dba/code_outline.hpp"
#include "language/language_profile.hpp"

class TextEditor;

namespace dbc::studio
{
    namespace panels
    {
        // Outline of the active editor tab: functions with their call counts,
        // labels with their jumps, types and variables. Selecting an entry goes
        // to its line.
        class CodePanel
        {
          public:
            using LineHandler = std::function<void(std::uint32_t Line)>;

            // Called every frame with the active tab (nullptr when none). Edits are
            // re-parsed once typing pauses, a tab switch at once.
            void Update(const TextEditor* Editor, const std::filesystem::path& Path,
                        dbc::language::LanguageProfile Profile, double Now);
            void Draw();

            void SetOnSelect(LineHandler Handler)
            {
                OnSelect_ = std::move(Handler);
            }

          private:
            void DrawSection(const char* Title, const char* Id, const std::vector<dba::OutlineEntry>& Entries,
                             const char* CountTooltip);

            const TextEditor* Editor_ = nullptr;
            std::size_t UndoIndex_ = 0;
            dbc::language::LanguageProfile Profile_ = dbc::language::LanguageProfile::Dbc;
            bool Pending_ = false;
            double ChangedAt_ = 0.0;

            dba::CodeOutline Outline_;
            bool SortByName_ = false;
            LineHandler OnSelect_;
        };
    } // namespace panels

} // namespace dbc::studio
