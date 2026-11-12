#pragma once

#include "language/language_profile.hpp"

#include <cstddef>
#include <cstdint>
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
        struct SourceSymbols;

        // What Tab and Enter do with the first suggestion. A short or loose match must
        // not replace a name on Enter, nor must a name of the program typed in full.
        enum class CompletionSelection
        {
            None, // Nothing is selected: both keep their usual meaning.
            Soft, // Tab accepts, Enter starts a new line.
            Hard, // Both accept.
        };

        // A builtin of the catalogue or a name the program declares.
        struct CompletionItem
        {
            enum class Kind
            {
                Builtin,
                Function,
                Variable,
                Array,
                Field,
                Label,
                Constant,
                Type,
            };

            // A builtin's canonical spelling, or the name as the program declares it.
            std::string Name;
            Kind What = Kind::Builtin;
            // "integer", "Player", "Move(dx#, who as Player)"; empty for a builtin.
            std::string Detail;
            const Command* Builtin = nullptr;
        };

        struct Completion
        {
            // Byte offset in the line of the first word a suggestion replaces: accepting
            // "SET WINDOW TITLE" after "set window ti" replaces all three words.
            size_t ReplaceStart = 0;
            // The line from ReplaceStart to the cursor.
            std::string Typed;
            CompletionSelection Selection = CompletionSelection::None;
            // The program's names first, then builtins by rank; alphabetical within each.
            std::vector<CompletionItem> Items;
        };

        // Suggests commands and functions for the text left of the cursor. Without
        // `Manual`, a space only continues a command name and numbers suggest nothing;
        // `Manual` (Ctrl+Space) lists every command when nothing matches.
        // With `Symbols`, the names the program declares join in: those the 1-based
        // `CursorLine` sees, labels after GOTO, GOSUB and RESTORE, types after AS, and
        // after a dot the fields of the user type on its left, alone.
        Completion Complete(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                            std::string_view LineBeforeCursor, bool Manual, const SourceSymbols* Symbols = nullptr,
                            std::uint32_t CursorLine = 0);

        // What accepting a suggestion writes: a builtin follows the case of what was typed,
        // a name of the program keeps its own.
        std::string CompletionText(const CompletionItem& Item, std::string_view Typed);

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
        // A function of `Symbols` is found when no builtin has the name.
        std::optional<ParameterHint> FindParameterHint(const CommandDatabase& Commands,
                                                       dbc::language::LanguageProfile Profile,
                                                       std::string_view LineBeforeCursor, bool InBlockComment,
                                                       const SourceSymbols* Symbols = nullptr);

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
        // The same for a suggestion: a name of the program shows what it is and its type or signature.
        CommandHelp DescribeItem(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                                 const CompletionItem& Item);

        // Spells a canonical name the way the first typed word is spelled: "set" gives
        // lower case, "Set" title case, "SET" upper case.
        std::string ApplyTypedCase(std::string_view Name, std::string_view Typed);

        // True when the end of `Text` lies in a comment or a string, where nothing is
        // suggested. Call it on each line in order: `InBlockComment` carries REMSTART
        // blocks from one line to the next.
        bool EndsInCommentOrString(std::string_view Text, bool& InBlockComment);
    } // namespace dba

} // namespace dbc::studio
