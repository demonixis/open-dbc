#include "completion.hpp"

#include <algorithm>
#include <span>

#include "command_database.hpp"
#include "language/language_catalogue.hpp"

namespace dbc::studio
{
    namespace
    {
        // Command names are ASCII, so anything else ends a word and the text a
        // suggestion replaces never needs decoding.
        bool IsWordChar(char Character)
        {
            return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') ||
                   (Character >= '0' && Character <= '9') || Character == '_' || Character == '$' || Character == '#';
        }

        bool IsSpace(char Character)
        {
            return Character == ' ' || Character == '\t';
        }

        bool IsUpper(char Character)
        {
            return Character >= 'A' && Character <= 'Z';
        }

        bool IsLower(char Character)
        {
            return Character >= 'a' && Character <= 'z';
        }

        char ToLower(char Character)
        {
            return IsUpper(Character) ? static_cast<char>(Character - 'A' + 'a') : Character;
        }

        char ToUpper(char Character)
        {
            return IsLower(Character) ? static_cast<char>(Character - 'a' + 'A') : Character;
        }

        std::string ToLower(std::string_view Value)
        {
            std::string Result(Value);
            std::transform(Result.begin(), Result.end(), Result.begin(), [](char C) { return ToLower(C); });
            return Result;
        }

        bool EqualsLower(std::string_view Value, std::string_view Lower)
        {
            return std::equal(Value.begin(), Value.end(), Lower.begin(), Lower.end(),
                              [](char Left, char Right) { return ToLower(Left) == Right; });
        }

        enum class Rank
        {
            None,
            Substring,
            WordsInOrder,
            Initials,
            Prefix,
        };

        Rank RankOf(const dba::Command& Entry, std::span<const std::string> Typed, bool AfterSpace)
        {
            const std::vector<std::string>& Words = Entry.Words;

            // A space continues a name only: the words so far are complete and more follow.
            if (AfterSpace)
            {
                return Words.size() > Typed.size() && std::equal(Typed.begin(), Typed.end(), Words.begin())
                           ? Rank::Prefix
                           : Rank::None;
            }

            const auto Starts = [](const std::string& Word, const std::string& Prefix)
            { return Word.starts_with(Prefix); };

            if (Words.size() >= Typed.size() &&
                std::equal(Typed.begin(), Typed.end(), Words.begin(),
                           [&](const std::string& Prefix, const std::string& Word) { return Starts(Word, Prefix); }))
            {
                return Rank::Prefix;
            }

            const bool OneWord = Typed.size() == 1;
            if (OneWord && Entry.Key.starts_with(Typed[0]))
            {
                return Rank::Prefix;
            }
            if (OneWord && Typed[0].size() >= 2 && Entry.Initials.starts_with(Typed[0]))
            {
                return Rank::Initials;
            }

            size_t Next = 0;
            for (const std::string& Word : Words)
            {
                if (Next < Typed.size() && Starts(Word, Typed[Next]))
                {
                    ++Next;
                }
            }
            if (Next == Typed.size())
            {
                return Rank::WordsInOrder;
            }
            return OneWord && Entry.Key.find(Typed[0]) != std::string::npos ? Rank::Substring : Rank::None;
        }

        struct Match
        {
            const dba::Command* Entry;
            Rank Value;
        };

        const char* TypeName(dbc::language::BuiltinValueType Type)
        {
            switch (Type)
            {
                case dbc::language::BuiltinValueType::Any:
                    return "any";
                case dbc::language::BuiltinValueType::Numeric:
                    return "number";
                case dbc::language::BuiltinValueType::Integer:
                    return "integer";
                case dbc::language::BuiltinValueType::Real:
                    return "real";
                case dbc::language::BuiltinValueType::String:
                    return "string";
                case dbc::language::BuiltinValueType::Void:
                    return "void";
            }
            return "any";
        }

        std::string SignatureOf(const dba::Command& Entry, const dbc::language::BuiltinOverload& Overload)
        {
            // The catalogue spells a repeated parameter once per accepted argument.
            const bool Repeated = Overload.SourceArity.IsVariadic();
            std::string Parameters;
            for (const dbc::language::BuiltinParameter& Parameter :
                 std::span(Overload.Parameters).first(Repeated ? 1 : Overload.Parameters.size()))
            {
                Parameters += Parameters.empty() ? "" : ", ";
                Parameters += TypeName(Parameter.ValueType);
                if (Parameter.Mode == dbc::language::BuiltinParameterMode::Output)
                {
                    Parameters += " output";
                }
                else if (Parameter.Mode == dbc::language::BuiltinParameterMode::Array)
                {
                    Parameters += " array";
                }
            }
            Parameters += Repeated ? "..." : "";

            if (Entry.Descriptor->Kind == dbc::language::SymbolKind::Function)
            {
                return Entry.Classic + "(" + Parameters + ") -> " + TypeName(Overload.ReturnType);
            }
            return Parameters.empty() ? Entry.Classic : Entry.Classic + " " + Parameters;
        }
    } // namespace

    namespace dba
    {
        Completion Complete(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                            std::string_view LineBeforeCursor, bool Manual)
        {
            const std::string_view Line = LineBeforeCursor;

            // The words right before the cursor, up to anything a command name cannot hold.
            std::vector<std::string> Words;
            std::vector<size_t> Starts;
            for (size_t Index = Line.size();;)
            {
                while (Index > 0 && IsSpace(Line[Index - 1]))
                {
                    --Index;
                }
                const size_t End = Index;
                while (Index > 0 && IsWordChar(Line[Index - 1]))
                {
                    --Index;
                }
                if (Index == End)
                {
                    break;
                }
                Words.push_back(ToLower(Line.substr(Index, End - Index)));
                Starts.push_back(Index);
            }
            std::reverse(Words.begin(), Words.end());
            std::reverse(Starts.begin(), Starts.end());

            const bool AfterSpace = !Line.empty() && IsSpace(Line.back());
            const bool InNumber =
                !AfterSpace && !Words.empty() &&
                std::all_of(Words.back().begin(), Words.back().end(), [](char C) { return C >= '0' && C <= '9'; });
            // "player.x" reads a field, not a command.
            const bool AfterDot = !Starts.empty() && Starts[0] > 0 && Line[Starts[0] - 1] == '.';

            Completion Result;
            std::vector<Match> Matches;

            // The longest run of words that fits a name wins, so "print set w" replaces "set w".
            for (size_t First = AfterDot ? 1 : 0; !InNumber && First < Words.size() && Matches.empty(); ++First)
            {
                const std::span<const std::string> Typed(Words.begin() + static_cast<long>(First), Words.end());
                for (const Command& Entry : Commands.All())
                {
                    if (!dbc::language::AvailableIn(*Entry.Descriptor, Profile))
                    {
                        continue;
                    }
                    if (const Rank Value = RankOf(Entry, Typed, AfterSpace); Value != Rank::None)
                    {
                        Matches.push_back({&Entry, Value});
                    }
                }
                Result.ReplaceStart = Starts[First];
            }

            if (Matches.empty())
            {
                if (!Manual)
                {
                    return Result;
                }
                for (const Command& Entry : Commands.All())
                {
                    if (dbc::language::AvailableIn(*Entry.Descriptor, Profile))
                    {
                        Matches.push_back({&Entry, Rank::None});
                    }
                }
                Result.ReplaceStart = !AfterSpace && !Starts.empty() ? Starts.back() : Line.size();
            }

            // Commands() is sorted by name, which the stable sort keeps within a rank.
            std::stable_sort(Matches.begin(), Matches.end(),
                             [](const Match& Left, const Match& Right) { return Left.Value > Right.Value; });

            Result.Items.reserve(Matches.size());
            for (const Match& Value : Matches)
            {
                // A command and a function may share a name.
                if (Result.Items.empty() || Result.Items.back()->Classic != Value.Entry->Classic)
                {
                    Result.Items.push_back(Value.Entry);
                }
            }

            Result.Typed = std::string(Line.substr(Result.ReplaceStart));
            if (!AfterSpace && !Matches.empty() && Matches.front().Value != Rank::None)
            {
                Result.Selection = Matches.front().Value == Rank::Prefix && Result.Typed.size() >= 2
                                       ? CompletionSelection::Hard
                                       : CompletionSelection::Soft;
            }
            return Result;
        }

        size_t CompletionWordEnd(std::string_view Line, size_t Cursor)
        {
            while (Cursor < Line.size() && IsWordChar(Line[Cursor]))
            {
                ++Cursor;
            }
            return Cursor;
        }

        CommandHelp DescribeCommand(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                                    std::string_view Name)
        {
            CommandHelp Help;
            const std::vector<Command>& All = Commands.All();
            const auto First =
                std::lower_bound(All.begin(), All.end(), Name,
                                 [](const Command& Entry, std::string_view Value) { return Entry.Classic < Value; });
            for (auto Entry = First; Entry != All.end() && Entry->Classic == Name; ++Entry)
            {
                if (!dbc::language::AvailableIn(*Entry->Descriptor, Profile))
                {
                    continue;
                }
                for (const dbc::language::BuiltinOverload* Overload :
                     dbc::language::Overloads(Entry->Descriptor->Id, Profile))
                {
                    std::string Signature = SignatureOf(*Entry, *Overload);
                    if (std::find(Help.Signatures.begin(), Help.Signatures.end(), Signature) == Help.Signatures.end())
                    {
                        Help.Signatures.push_back(std::move(Signature));
                    }
                }
                // Some names take a form the grammar defines rather than typed arguments.
                if (Help.Signatures.empty())
                {
                    Help.Signatures.push_back(Entry->Classic);
                }

                switch (dbc::language::SupportFor(*Entry->Descriptor, Profile))
                {
                    case dbc::language::SupportStatus::Implemented:
                        break;
                    case dbc::language::SupportStatus::CompatibilityStub:
                        Help.Warning = "Compatibility stub: accepted, but does not do what it historically did.";
                        break;
                    case dbc::language::SupportStatus::Unsupported:
                        Help.Warning = "Not supported by OpenDBC: using it is a compile error.";
                        break;
                }
                if (Help.Description.empty())
                {
                    Help.Description = Entry->Description;
                }
            }
            return Help;
        }

        std::string ApplyTypedCase(std::string_view Name, std::string_view Typed)
        {
            const std::string_view Word = Typed.substr(0, Typed.find_first_of(" \t"));
            if (Word.empty())
            {
                return std::string(Name);
            }

            const std::string_view Rest = Word.substr(1);
            const bool RestHasUpper = std::any_of(Rest.begin(), Rest.end(), IsUpper);
            const bool RestHasLower = std::any_of(Rest.begin(), Rest.end(), IsLower);

            std::string Result(Name);
            if (!IsUpper(Word[0]) && !RestHasUpper)
            {
                return ToLower(Name);
            }
            if (IsUpper(Word[0]) && !RestHasLower)
            {
                std::transform(Result.begin(), Result.end(), Result.begin(), [](char C) { return ToUpper(C); });
                return Result;
            }
            if (IsUpper(Word[0]) && !RestHasUpper)
            {
                bool WordStart = true;
                for (char& Character : Result)
                {
                    Character = WordStart ? ToUpper(Character) : ToLower(Character);
                    WordStart = IsSpace(Character);
                }
            }
            return Result;
        }

        bool EndsInCommentOrString(std::string_view Text, bool& InBlockComment)
        {
            for (size_t Index = 0; Index < Text.size();)
            {
                const char Character = Text[Index];
                if (IsWordChar(Character))
                {
                    const size_t End = CompletionWordEnd(Text, Index);
                    const std::string_view Word = Text.substr(Index, End - Index);
                    if (InBlockComment)
                    {
                        InBlockComment = !EqualsLower(Word, "remend");
                    }
                    else if (EqualsLower(Word, "remstart"))
                    {
                        InBlockComment = true;
                    }
                    else if (EqualsLower(Word, "rem"))
                    {
                        return true;
                    }
                    Index = End;
                    continue;
                }
                if (!InBlockComment && Character == '`')
                {
                    return true;
                }
                if (!InBlockComment && Character == '"')
                {
                    const size_t Close = Text.find('"', Index + 1);
                    if (Close == std::string_view::npos)
                    {
                        return true;
                    }
                    Index = Close;
                }
                ++Index;
            }
            return InBlockComment;
        }
    } // namespace dba

} // namespace dbc::studio
