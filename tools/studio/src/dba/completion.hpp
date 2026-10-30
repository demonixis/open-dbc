#pragma once

#include "language/language_profile.hpp"

#include <cstddef>
#include <optional>
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

        // One overload of a name, split so that the parameter being typed can stand out.
        struct Signature
        {
            static constexpr size_t NoParameter = static_cast<size_t>(-1);

            // "SET WINDOW TITLE " or "RGB(".
            std::string Prefix;
            // "number", "string output", "[number]" when it may be left out, "any...".
            std::vector<std::string> Parameters;
            // Nothing, or ") -> integer".
            std::string Suffix;
            // The last parameter takes any number of arguments.
            bool Repeats = false;

            // The parameter a 0-based argument fills, NoParameter when this overload stops earlier.
            size_t ParameterFor(size_t Argument) const;
            std::string Text() const;
        };

        // The builtin call around the cursor.
        struct ParameterHint
        {
            std::vector<Signature> Overloads;
            // The 0-based argument the cursor is in.
            size_t Argument = 0;
            // Byte offset in the line of the name.
            size_t NameStart = 0;
        };

        // Looks for the innermost builtin whose arguments hold the end of the line:
        // "rgb(1, " in an expression, "position object 1, " as a statement. Nothing when
        // the name is still being typed, takes no argument, or the line ends in a comment.
        // `InBlockComment` is the state left by the lines above.
        std::optional<ParameterHint> FindParameterHint(const CommandDatabase& Commands,
                                                       dbc::language::LanguageProfile Profile,
                                                       std::string_view LineBeforeCursor, bool InBlockComment);

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
