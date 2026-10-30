#pragma once

#include "language/language_profile.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace dba
    {
        class CommandDatabase;
        struct Command;

        // What Tab and Enter do with the first suggestion. Variables are not known
        // yet, so a short or loose match must not replace a name on Enter.
        enum class CompletionSelection
        {
            None, // Nothing is selected: both keep their usual meaning.
            Soft, // Tab accepts, Enter starts a new line.
            Hard, // Both accept.
        };

        struct Completion
        {
            // Byte offset in the line of the first word a suggestion replaces: accepting
            // "SET WINDOW TITLE" after "set window ti" replaces all three words.
            size_t ReplaceStart = 0;
            // The line from ReplaceStart to the cursor.
            std::string Typed;
            CompletionSelection Selection = CompletionSelection::None;
            // Best match first, alphabetical within a rank.
            std::vector<const Command*> Items;
        };

        // Suggests commands and functions for the text left of the cursor. Without
        // `Manual`, a space only continues a command name and numbers suggest nothing;
        // `Manual` (Ctrl+Space) lists every command when nothing matches.
        Completion Complete(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                            std::string_view LineBeforeCursor, bool Manual);

        // The end of the word the cursor is inside, which accepting also replaces.
        size_t CompletionWordEnd(std::string_view Line, size_t Cursor);

        // What the Studio shows about a name, for one profile.
        struct CommandHelp
        {
            // One line per overload: "SET WINDOW TITLE string", "OBJECT POSITION X(number) -> real".
            std::vector<std::string> Signatures;
            // Set when the name is not implemented in this profile.
            std::string Warning;
            std::string Description;
        };

        CommandHelp DescribeCommand(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                                    std::string_view Name);

        // Spells a canonical name the way the first typed word is spelled: "set" gives
        // lower case, "Set" title case, "SET" upper case.
        std::string ApplyTypedCase(std::string_view Name, std::string_view Typed);

        // True when the end of `Text` lies in a comment or a string, where nothing is
        // suggested. Call it on each line in order: `InBlockComment` carries REMSTART
        // blocks from one line to the next.
        bool EndsInCommentOrString(std::string_view Text, bool& InBlockComment);
    } // namespace dba

} // namespace dbc::studio
