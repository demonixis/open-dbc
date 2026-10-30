#include "completion.hpp"
#include "core/i18n.hpp"

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

        size_t WordEnd(std::string_view Text, size_t Index)
        {
            while (Index < Text.size() && IsWordChar(Text[Index]))
            {
                ++Index;
            }
            return Index;
        }

        // The words right before the end of the text, up to anything a name cannot hold.
        struct WordRun
        {
            std::vector<std::string> Words; // Lower case.
            std::vector<size_t> Starts;
            // "player.x" reads a field: its first word cannot start a name.
            bool AfterDot = false;
        };

        WordRun ReadWordsBefore(std::string_view Text)
        {
            WordRun Run;
            for (size_t Index = Text.size();;)
            {
                while (Index > 0 && IsSpace(Text[Index - 1]))
                {
                    --Index;
                }
                const size_t End = Index;
                while (Index > 0 && IsWordChar(Text[Index - 1]))
                {
                    --Index;
                }
                if (Index == End)
                {
                    break;
                }
                Run.Words.push_back(ToLower(Text.substr(Index, End - Index)));
                Run.Starts.push_back(Index);
            }
            std::reverse(Run.Words.begin(), Run.Words.end());
            std::reverse(Run.Starts.begin(), Run.Starts.end());
            Run.AfterDot = !Run.Starts.empty() && Run.Starts[0] > 0 && Text[Run.Starts[0] - 1] == '.';
            return Run;
        }

        enum class LineEnd
        {
            Code,
            String,
            Comment,
        };

        // Walks a line as the editor colorizes it, showing the visitors what lies outside
        // comments and strings: OnWord(Start, End) and OnMark(Index) for anything else.
        template <typename WordVisitor, typename MarkVisitor>
        LineEnd ScanCode(std::string_view Text, bool& InBlockComment, WordVisitor&& OnWord, MarkVisitor&& OnMark)
        {
            for (size_t Index = 0; Index < Text.size();)
            {
                const char Character = Text[Index];
                if (IsWordChar(Character))
                {
                    const size_t End = WordEnd(Text, Index);
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
                        return LineEnd::Comment;
                    }
                    else
                    {
                        OnWord(Index, End);
                    }
                    Index = End;
                    continue;
                }
                if (!InBlockComment)
                {
                    if (Character == '`')
                    {
                        return LineEnd::Comment;
                    }
                    if (Character == '"')
                    {
                        const size_t Close = Text.find('"', Index + 1);
                        if (Close == std::string_view::npos)
                        {
                            return LineEnd::String;
                        }
                        Index = Close + 1;
                        continue;
                    }
                    OnMark(Index);
                }
                ++Index;
            }
            return InBlockComment ? LineEnd::Comment : LineEnd::Code;
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

        dba::Signature SignatureOf(const dba::Command& Entry, const dbc::language::BuiltinOverload& Overload)
        {
            dba::Signature Result;
            // The catalogue spells a repeated parameter once per accepted argument.
            Result.Repeats = Overload.SourceArity.IsVariadic();
            const std::span<const dbc::language::BuiltinParameter> Parameters =
                std::span(Overload.Parameters).first(Result.Repeats ? 1 : Overload.Parameters.size());
            for (size_t Index = 0; Index < Parameters.size(); ++Index)
            {
                std::string Text = TypeName(Parameters[Index].ValueType);
                if (Parameters[Index].Mode == dbc::language::BuiltinParameterMode::Output)
                {
                    Text += " output";
                }
                else if (Parameters[Index].Mode == dbc::language::BuiltinParameterMode::Array)
                {
                    Text += " array";
                }
                const bool Optional = !Result.Repeats && Index >= static_cast<size_t>(Overload.SourceArity.Min);
                Result.Parameters.push_back(Optional ? "[" + Text + "]" : std::move(Text));
            }
            if (Result.Repeats)
            {
                Result.Parameters.back() += "...";
            }

            if (Entry.Descriptor->Kind == dbc::language::SymbolKind::Function)
            {
                Result.Prefix = Entry.Classic + "(";
                Result.Suffix = std::string(") -> ") + TypeName(Overload.ReturnType);
            }
            else
            {
                Result.Prefix = Result.Parameters.empty() ? Entry.Classic : Entry.Classic + " ";
            }
            return Result;
        }

        // Every entry of a name: a command and a function may share one.
        std::span<const dba::Command> EntriesNamed(const dba::CommandDatabase& Commands, std::string_view Name)
        {
            const std::vector<dba::Command>& All = Commands.All();
            const auto First =
                std::lower_bound(All.begin(), All.end(), Name, [](const dba::Command& Entry, std::string_view Value)
                                 { return Entry.Classic < Value; });
            auto Last = First;
            while (Last != All.end() && Last->Classic == Name)
            {
                ++Last;
            }
            return {First, Last};
        }

        std::vector<dba::Signature> OverloadsOf(const dba::CommandDatabase& Commands,
                                                dbc::language::LanguageProfile Profile, std::string_view Name,
                                                dbc::language::SymbolKind Kind)
        {
            std::vector<dba::Signature> Result;
            for (const dba::Command& Entry : EntriesNamed(Commands, Name))
            {
                if (Entry.Descriptor->Kind != Kind || !dbc::language::AvailableIn(*Entry.Descriptor, Profile))
                {
                    continue;
                }
                for (const dbc::language::BuiltinOverload* Overload :
                     dbc::language::Overloads(Entry.Descriptor->Id, Profile))
                {
                    Result.push_back(SignatureOf(Entry, *Overload));
                }
            }
            return Result;
        }

        bool TakesArguments(const std::vector<dba::Signature>& Overloads)
        {
            return std::any_of(Overloads.begin(), Overloads.end(),
                               [](const dba::Signature& Value) { return !Value.Parameters.empty(); });
        }

        // The builtin whose name ends the text, which an opening parenthesis follows. A
        // call in an expression is a function, but a command may also take parentheses.
        std::optional<dba::ParameterHint> CallBefore(const dba::CommandDatabase& Commands,
                                                     dbc::language::LanguageProfile Profile, std::string_view Text)
        {
            const WordRun Run = ReadWordsBefore(Text);
            for (size_t First = Run.AfterDot ? 1 : 0; First < Run.Words.size(); ++First)
            {
                std::string Name;
                for (size_t Index = First; Index < Run.Words.size(); ++Index)
                {
                    Name += (Index > First ? " " : "") + Run.Words[Index];
                }
                std::transform(Name.begin(), Name.end(), Name.begin(), [](char C) { return ToUpper(C); });

                std::vector<dba::Signature> Overloads =
                    OverloadsOf(Commands, Profile, Name, dbc::language::SymbolKind::Function);
                if (Overloads.empty())
                {
                    Overloads = OverloadsOf(Commands, Profile, Name, dbc::language::SymbolKind::Command);
                    if (!TakesArguments(Overloads))
                    {
                        continue;
                    }
                }
                if (!Overloads.empty())
                {
                    dba::ParameterHint Hint;
                    Hint.Overloads = std::move(Overloads);
                    Hint.NameStart = Run.Starts[First];
                    return Hint;
                }
            }
            return std::nullopt;
        }
    } // namespace

    namespace dba
    {
        Completion Complete(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                            std::string_view LineBeforeCursor, bool Manual)
        {
            const std::string_view Line = LineBeforeCursor;
            const WordRun Run = ReadWordsBefore(Line);
            const std::vector<std::string>& Words = Run.Words;
            const std::vector<size_t>& Starts = Run.Starts;

            const bool AfterSpace = !Line.empty() && IsSpace(Line.back());
            const bool InNumber =
                !AfterSpace && !Words.empty() &&
                std::all_of(Words.back().begin(), Words.back().end(), [](char C) { return C >= '0' && C <= '9'; });

            Completion Result;
            std::vector<Match> Matches;

            // The longest run of words that fits a name wins, so "print set w" replaces "set w".
            for (size_t First = Run.AfterDot ? 1 : 0; !InNumber && First < Words.size() && Matches.empty(); ++First)
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
            return WordEnd(Line, Cursor);
        }

        size_t Signature::ParameterFor(size_t Argument) const
        {
            if (Argument < Parameters.size())
            {
                return Argument;
            }
            return Repeats ? Parameters.size() - 1 : NoParameter;
        }

        std::string Signature::Text() const
        {
            std::string Result = Prefix;
            for (size_t Index = 0; Index < Parameters.size(); ++Index)
            {
                Result += (Index > 0 ? ", " : "") + Parameters[Index];
            }
            return Result + Suffix;
        }

        std::optional<ParameterHint> FindParameterHint(const CommandDatabase& Commands,
                                                       dbc::language::LanguageProfile Profile,
                                                       std::string_view LineBeforeCursor, bool InBlockComment)
        {
            const std::string_view Line = LineBeforeCursor;

            struct Group
            {
                size_t Open;
                size_t Commas = 0;
            };

            std::vector<Group> Groups;
            size_t StatementStart = 0;
            size_t StatementCommas = 0;
            const auto StartStatement = [&](size_t Start)
            {
                Groups.clear();
                StatementStart = Start;
                StatementCommas = 0;
            };

            const LineEnd End = ScanCode(
                Line, InBlockComment,
                [&](size_t Start, size_t Stop)
                {
                    const std::string_view Word = Line.substr(Start, Stop - Start);
                    if (EqualsLower(Word, "then") || EqualsLower(Word, "else"))
                    {
                        StartStatement(Stop);
                    }
                },
                [&](size_t Index)
                {
                    const char Mark = Line[Index];
                    if (Mark == ':')
                    {
                        StartStatement(Index + 1);
                    }
                    else if (Mark == '(')
                    {
                        Groups.push_back({Index});
                    }
                    else if (Mark == ')' && !Groups.empty())
                    {
                        Groups.pop_back();
                    }
                    else if (Mark == ',')
                    {
                        ++(Groups.empty() ? StatementCommas : Groups.back().Commas);
                    }
                });
            if (End == LineEnd::Comment)
            {
                return std::nullopt;
            }

            // Parentheses that name no builtin (grouping, arrays, user functions) belong
            // to the argument of whatever holds them.
            for (auto Value = Groups.rbegin(); Value != Groups.rend(); ++Value)
            {
                if (std::optional<ParameterHint> Hint = CallBefore(Commands, Profile, Line.substr(0, Value->Open)))
                {
                    Hint->Argument = Value->Commas;
                    return Hint;
                }
            }

            const size_t Start = Line.find_first_not_of(" \t", StatementStart);
            if (Start == std::string_view::npos)
            {
                return std::nullopt;
            }
            const dbc::language::SymbolDescriptor* Descriptor =
                dbc::language::FindLongestCommandPrefix(Profile, Line.substr(Start));
            if (Descriptor == nullptr || Start + Descriptor->CanonicalName.size() >= Line.size())
            {
                return std::nullopt;
            }

            ParameterHint Hint;
            Hint.Overloads =
                OverloadsOf(Commands, Profile, Descriptor->CanonicalName, dbc::language::SymbolKind::Command);
            if (!TakesArguments(Hint.Overloads))
            {
                return std::nullopt;
            }
            Hint.Argument = StatementCommas;
            Hint.NameStart = Start;
            return Hint;
        }

        CommandHelp DescribeCommand(const CommandDatabase& Commands, dbc::language::LanguageProfile Profile,
                                    std::string_view Name)
        {
            CommandHelp Help;
            for (const Command& Value : EntriesNamed(Commands, Name))
            {
                const Command* Entry = &Value;
                if (!dbc::language::AvailableIn(*Entry->Descriptor, Profile))
                {
                    continue;
                }
                for (const dbc::language::BuiltinOverload* Overload :
                     dbc::language::Overloads(Entry->Descriptor->Id, Profile))
                {
                    std::string Text = SignatureOf(*Entry, *Overload).Text();
                    if (std::find(Help.Signatures.begin(), Help.Signatures.end(), Text) == Help.Signatures.end())
                    {
                        Help.Signatures.push_back(std::move(Text));
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
                        Help.Warning = Tr("Compatibility stub: accepted, but does not do what it historically did.");
                        break;
                    case dbc::language::SupportStatus::Unsupported:
                        Help.Warning = Tr("Not supported by OpenDBC: using it is a compile error.");
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
            return ScanCode(Text, InBlockComment, [](size_t, size_t) {}, [](size_t) {}) != LineEnd::Code;
        }
    } // namespace dba

} // namespace dbc::studio
