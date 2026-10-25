#include "language.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

#include "language/language_catalogue.hpp"

namespace dbc::studio
{
    namespace
    {
        using Iterator = TextEditor::Iterator;

        char LowerAscii(ImWchar C)
        {
            return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : static_cast<char>(C);
        }

        bool IsWordChar(ImWchar C)
        {
            return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
        }

        // Case-insensitive literal match that also requires a word boundary after
        // the word, so REMSTART and "remaining" are not mistaken for REM.
        bool MatchesWord(Iterator Start, Iterator End, std::string_view Word)
        {
            Iterator I = Start;
            for (const char Expected : Word)
            {
                if (I == End || LowerAscii(*I) != Expected)
                {
                    return false;
                }
                ++I;
            }
            return I == End || !IsWordChar(*I);
        }

        // REM comments run to the end of the line. Handled here rather than through
        // Language::singleLineComment because that one is matched literally and is
        // therefore case-sensitive, which does not suit DBA.
        Iterator Tokenize(Iterator Start, Iterator End, TextEditor::Color& ColorValue)
        {
            if (MatchesWord(Start, End, "rem"))
            {
                ColorValue = TextEditor::Color::comment;
                return End;
            }
            return Start;
        }

        // REMSTART ... REMEND block comments. Only one level exists in DBA.
        Iterator CommentBlockStart(Iterator Start, Iterator End, size_t& Level)
        {
            if (!MatchesWord(Start, End, "remstart"))
            {
                return Start;
            }

            Level = 0;
            Iterator I = Start;
            for (size_t N = 0; N < 8; ++N)
            {
                ++I;
            }
            return I;
        }

        Iterator CommentBlockEnd(Iterator Start, Iterator End, size_t& Level)
        {
            if (!MatchesWord(Start, End, "remend"))
            {
                return Start;
            }

            Level = 0;
            Iterator I = Start;
            for (size_t N = 0; N < 6; ++N)
            {
                ++I;
            }
            return I;
        }

        // Identifiers may carry a DBA type suffix: value# (float), name$ (string).
        Iterator GetIdentifier(Iterator Start, Iterator End)
        {
            if (Start == End)
            {
                return Start;
            }

            const ImWchar First = *Start;
            const bool StartsWord = (First >= 'a' && First <= 'z') || (First >= 'A' && First <= 'Z') || First == '_';
            if (!StartsWord)
            {
                return Start;
            }

            Iterator I = Start;
            while (I != End && IsWordChar(*I))
            {
                ++I;
            }
            if (I != End && (*I == '#' || *I == '$'))
            {
                ++I;
            }
            return I;
        }

        // Integers, floats and the # float suffix: 42, 3.14, 10#
        Iterator GetNumber(Iterator Start, Iterator End)
        {
            if (Start == End || *Start < '0' || *Start > '9')
            {
                return Start;
            }

            Iterator I = Start;
            while (I != End && *I >= '0' && *I <= '9')
            {
                ++I;
            }

            if (I != End && *I == '.')
            {
                Iterator Fraction = I;
                ++Fraction;
                if (Fraction != End && *Fraction >= '0' && *Fraction <= '9')
                {
                    I = Fraction;
                    while (I != End && *I >= '0' && *I <= '9')
                    {
                        ++I;
                    }
                }
            }

            if (I != End && *I == '#')
            {
                ++I;
            }
            return I;
        }

        bool IsPunctuation(ImWchar C)
        {
            static const std::string_view KPunctuation = "!%&()*+,-./:;<=>[]^{|}~";
            return C < 128 && KPunctuation.find(static_cast<char>(C)) != std::string_view::npos;
        }

        std::string ToLower(std::string_view S)
        {
            std::string Out;
            Out.reserve(S.size());
            for (const char C : S)
            {
                Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
            }
            return Out;
        }
    } // namespace

    namespace dba
    {
        const TextEditor::Language* Language(dbc::language::LanguageProfile Profile)
        {
            static TextEditor::Language Languages[2];
            static bool Initialized[2] = {false, false};
            const std::size_t Index = Profile == dbc::language::LanguageProfile::Dbc ? 0 : 1;
            TextEditor::Language& LanguageValue = Languages[Index];
            if (Initialized[Index])
                return &LanguageValue;

            LanguageValue.name = "DBA";
            LanguageValue.caseSensitive = false;

            // Tick comments are a single symbol, so the literal matcher handles them.
            LanguageValue.singleLineCommentAlt = "`";
            LanguageValue.commentLevelStart = CommentBlockStart;
            LanguageValue.commentLevelEnd = CommentBlockEnd;

            LanguageValue.hasDoubleQuotedStrings = true;
            LanguageValue.stringEscape = '\\';

            LanguageValue.customTokenizer = Tokenize;
            LanguageValue.getIdentifier = GetIdentifier;
            LanguageValue.getNumber = GetNumber;
            LanguageValue.isPunctuation = IsPunctuation;

            for (const dbc::language::SymbolDescriptor* Descriptor : dbc::language::Symbols(Profile))
            {
                if (Descriptor->Kind == dbc::language::SymbolKind::Statement ||
                    Descriptor->Kind == dbc::language::SymbolKind::Directive)
                {
                    LanguageValue.keywords.insert(ToLower(Descriptor->CanonicalName));
                    continue;
                }
                if (Descriptor->Kind != dbc::language::SymbolKind::Command &&
                    Descriptor->Kind != dbc::language::SymbolKind::Function)
                {
                    continue;
                }
                std::istringstream Words{std::string(Descriptor->CanonicalName)};
                std::string Word;
                while (Words >> Word)
                {
                    LanguageValue.identifiers.insert(ToLower(Word));
                }
            }

            // Flow control wins over the command list when a word is in both.
            for (const std::string& Keyword : LanguageValue.keywords)
            {
                LanguageValue.identifiers.erase(Keyword);
            }

            Initialized[Index] = true;
            return &LanguageValue;
        }
    } // namespace dba

} // namespace dbc::studio
