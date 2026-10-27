#include "frontend_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>

namespace dbc::language::detail
{
    namespace
    {
        struct Segment
        {
            std::vector<Token> Tokens{};
            SourceSpan Span{};
            bool EndedByColon = false;
            bool StartsLine = false;
        };

        bool IsDelimiter(TokenKind Kind)
        {
            return Kind == TokenKind::Comma || Kind == TokenKind::Semicolon;
        }

        Type ImplicitType(std::string_view Name)
        {
            if (!Name.empty())
            {
                switch (Name.back())
                {
                    case '$':
                        return {TypeKind::String, {}};
                    case '#':
                        return {TypeKind::Real, {}};
                    case '%':
                        return {TypeKind::Integer, {}};
                    default:
                        break;
                }
            }
            return {TypeKind::Integer, {}};
        }

        Type NamedType(std::string Name)
        {
            Name = Canonical(Name);
            if (Name == "BOOLEAN" || Name == "BOOL")
            {
                return {TypeKind::Boolean, {}};
            }
            if (Name == "BYTE" || Name == "WORD" || Name == "DWORD" || Name == "INTEGER" || Name == "DOUBLE INTEGER")
            {
                return {TypeKind::Integer, {}};
            }
            if (Name == "FLOAT" || Name == "DOUBLE FLOAT" || Name == "REAL")
            {
                return {TypeKind::Real, {}};
            }
            if (Name == "STRING")
            {
                return {TypeKind::String, {}};
            }
            return {TypeKind::UserDefined, std::move(Name)};
        }

        class ExpressionReader
        {
          public:
            ExpressionReader(const SourceDocument& Source, std::span<const Token> Tokens,
                             std::vector<Diagnostic>& Diagnostics)
                : Source_(Source), Tokens_(Tokens), Diagnostics_(Diagnostics)
            {
            }

            std::unique_ptr<Expression> Parse()
            {
                if (Tokens_.empty())
                {
                    AddError(Diagnostics_, "DBC1100", "Expected an expression", {});
                    return MakeError({});
                }
                std::unique_ptr<Expression> Result = ParseBinary(0);
                if (!AtEnd())
                {
                    AddError(Diagnostics_, "DBC1101", "Unexpected token after expression", Peek().Span);
                }
                return Result;
            }

          private:
            bool AtEnd() const
            {
                return Position_ >= Tokens_.size();
            }

            const Token& Peek(std::size_t Lookahead = 0) const
            {
                static const Token EndToken{TokenKind::End, {}};
                return Position_ + Lookahead < Tokens_.size() ? Tokens_[Position_ + Lookahead] : EndToken;
            }

            const Token& Advance()
            {
                return Tokens_[Position_++];
            }

            bool Match(TokenKind Kind)
            {
                if (!AtEnd() && Peek().Kind == Kind)
                {
                    ++Position_;
                    return true;
                }
                return false;
            }

            bool MatchWord(std::string_view Word)
            {
                if (!AtEnd() && IsWord(Source_, Peek(), Word))
                {
                    ++Position_;
                    return true;
                }
                return false;
            }

            static std::unique_ptr<Expression> MakeError(SourceSpan Span)
            {
                auto Result = std::make_unique<Expression>();
                Result->Kind = ExpressionKind::Error;
                Result->Span = Span;
                Result->ResolvedType = {TypeKind::Error, {}};
                return Result;
            }

            std::unique_ptr<Expression> ParseBinary(int MinimumPrecedence)
            {
                std::unique_ptr<Expression> Left = ParseUnary();
                while (!AtEnd())
                {
                    const auto Operator = BinaryOperator(Peek());
                    if (!Operator || Operator->Precedence < MinimumPrecedence)
                    {
                        break;
                    }
                    const Token OperatorToken = Advance();
                    const int NextMinimum =
                        Operator->RightAssociative ? Operator->Precedence : Operator->Precedence + 1;
                    std::unique_ptr<Expression> Right = ParseBinary(NextMinimum);
                    auto Combined = std::make_unique<Expression>();
                    Combined->Kind = ExpressionKind::Binary;
                    Combined->Span = Merge(Left->Span, Right->Span);
                    Combined->Text = Operator->Name;
                    Combined->Children.push_back(std::move(Left));
                    Combined->Children.push_back(std::move(Right));
                    Left = std::move(Combined);
                    (void)OperatorToken;
                }
                return Left;
            }

            std::unique_ptr<Expression> ParseUnary()
            {
                if (!AtEnd() && (Peek().Kind == TokenKind::Plus || Peek().Kind == TokenKind::Minus ||
                                 IsWord(Source_, Peek(), "NOT")))
                {
                    const Token Operator = Advance();
                    std::unique_ptr<Expression> Operand = ParseUnary();
                    auto Result = std::make_unique<Expression>();
                    Result->Kind = ExpressionKind::Unary;
                    Result->Span = Merge(Operator.Span, Operand->Span);
                    Result->Text = Canonical(Lexeme(Source_, Operator.Span));
                    if (Result->Text == "NOT" && Source_.Profile == LanguageProfile::DbPro)
                        Result->Text = "BITNOT";
                    Result->Children.push_back(std::move(Operand));
                    return Result;
                }
                return ParsePostfix();
            }

            std::unique_ptr<Expression> ParsePostfix()
            {
                std::unique_ptr<Expression> Result = ParsePrimary();
                while (!AtEnd() && Peek().Kind == TokenKind::Dot)
                {
                    const Token Dot = Advance();
                    if (AtEnd() || Peek().Kind != TokenKind::Word)
                    {
                        AddError(Diagnostics_, "DBC1102", "Expected a field name after '.'", Dot.Span);
                        return Result;
                    }
                    const Token Field = Advance();
                    auto Member = std::make_unique<Expression>();
                    Member->Kind = ExpressionKind::Member;
                    Member->Span = Merge(Result->Span, Field.Span);
                    Member->Text = Canonical(Lexeme(Source_, Field.Span));
                    Member->Children.push_back(std::move(Result));
                    Result = std::move(Member);
                }
                return Result;
            }

            std::unique_ptr<Expression> ParsePrimary()
            {
                if (AtEnd())
                {
                    AddError(Diagnostics_, "DBC1103", "Expected an expression", {});
                    return MakeError({});
                }

                const Token First = Peek();
                if (First.Kind == TokenKind::Integer || First.Kind == TokenKind::Real)
                {
                    Advance();
                    auto Result = std::make_unique<Expression>();
                    Result->Kind =
                        First.Kind == TokenKind::Integer ? ExpressionKind::IntegerLiteral : ExpressionKind::RealLiteral;
                    Result->Span = First.Span;
                    Result->Text = TokenText(Source_, First);
                    Result->Number = ParseNumber(Result->Text);
                    Result->ResolvedType = {First.Kind == TokenKind::Integer ? TypeKind::Integer : TypeKind::Real, {}};
                    return Result;
                }
                if (First.Kind == TokenKind::String)
                {
                    Advance();
                    auto Result = std::make_unique<Expression>();
                    Result->Kind = ExpressionKind::StringLiteral;
                    Result->Span = First.Span;
                    Result->Text = DecodeString(Lexeme(Source_, First.Span));
                    Result->ResolvedType = {TypeKind::String, {}};
                    return Result;
                }
                if (Match(TokenKind::LeftParen))
                {
                    std::unique_ptr<Expression> Result = ParseBinary(0);
                    if (!Match(TokenKind::RightParen))
                    {
                        AddError(Diagnostics_, "DBC1104", "Expected ')'", First.Span);
                    }
                    return Result;
                }
                if (First.Kind != TokenKind::Word)
                {
                    Advance();
                    AddError(Diagnostics_, "DBC1105", "Expected an expression", First.Span);
                    return MakeError(First.Span);
                }

                const std::size_t NameWords = LongestFunctionName();
                const std::size_t Count = NameWords == 0 ? 1 : NameWords;
                const SourceSpan NameSpan = Merge(Peek().Span, Peek(Count - 1).Span);
                std::string Name;
                for (std::size_t Index = 0; Index < Count; ++Index)
                {
                    if (!Name.empty())
                    {
                        Name.push_back(' ');
                    }
                    Name += TokenText(Source_, Advance());
                }
                Name = Canonical(Name);

                if (!Match(TokenKind::LeftParen))
                {
                    auto Result = std::make_unique<Expression>();
                    Result->Kind = ExpressionKind::Name;
                    Result->Span = NameSpan;
                    Result->Text = std::move(Name);
                    Result->ResolvedType = ImplicitType(Result->Text);
                    return Result;
                }

                auto Result = std::make_unique<Expression>();
                Result->Kind = ExpressionKind::CallOrIndex;
                Result->Text = std::move(Name);
                if (!Match(TokenKind::RightParen))
                {
                    for (;;)
                    {
                        Result->Children.push_back(ParseBinary(0));
                        if (Match(TokenKind::RightParen))
                        {
                            break;
                        }
                        if (!Match(TokenKind::Comma))
                        {
                            AddError(Diagnostics_, "DBC1106", "Expected ',' or ')' in argument list", Peek().Span);
                            while (!AtEnd() && Peek().Kind != TokenKind::RightParen)
                            {
                                Advance();
                            }
                            Match(TokenKind::RightParen);
                            break;
                        }
                    }
                }
                Result->Span = Merge(NameSpan, Position_ == 0 ? NameSpan : Tokens_[Position_ - 1].Span);
                return Result;
            }

            struct OperatorInfo
            {
                int Precedence = 0;
                bool RightAssociative = false;
                std::string Name{};
            };

            std::optional<OperatorInfo> BinaryOperator(const Token& Value) const
            {
                switch (Value.Kind)
                {
                    case TokenKind::Power:
                        return OperatorInfo{70, true, "^"};
                    case TokenKind::Star:
                        return OperatorInfo{60, false, "*"};
                    case TokenKind::Slash:
                        return OperatorInfo{60, false, "/"};
                    case TokenKind::Modulo:
                        return OperatorInfo{60, false, "MOD"};
                    case TokenKind::Plus:
                        return OperatorInfo{50, false, "+"};
                    case TokenKind::Minus:
                        return OperatorInfo{50, false, "-"};
                    case TokenKind::ShiftLeft:
                        return OperatorInfo{45, false, "<<"};
                    case TokenKind::ShiftRight:
                        return OperatorInfo{45, false, ">>"};
                    case TokenKind::BitAnd:
                        return OperatorInfo{44, false, "&&"};
                    case TokenKind::BitXor:
                        return OperatorInfo{43, false, "~~"};
                    case TokenKind::BitOr:
                        return OperatorInfo{42, false, "||"};
                    case TokenKind::UnsupportedOperator:
                        return OperatorInfo{44, false, ".."};
                    case TokenKind::Equal:
                        return OperatorInfo{40, false, "="};
                    case TokenKind::NotEqual:
                        return OperatorInfo{40, false, "<>"};
                    case TokenKind::Less:
                        return OperatorInfo{40, false, "<"};
                    case TokenKind::LessEqual:
                        return OperatorInfo{40, false, "<="};
                    case TokenKind::Greater:
                        return OperatorInfo{40, false, ">"};
                    case TokenKind::GreaterEqual:
                        return OperatorInfo{40, false, ">="};
                    default:
                        break;
                }
                if (Value.Kind == TokenKind::Word)
                {
                    const std::string Word = Canonical(Lexeme(Source_, Value.Span));
                    if (Word == "MOD")
                        return OperatorInfo{60, false, "MOD"};
                    if (Word == "AND")
                        return OperatorInfo{30, false, Source_.Profile == LanguageProfile::DbPro ? "&&" : "AND"};
                    if (Word == "OR")
                        return OperatorInfo{20, false, Source_.Profile == LanguageProfile::DbPro ? "||" : "OR"};
                    if (Word == "XOR" && Source_.Profile == LanguageProfile::DbPro)
                        return OperatorInfo{10, false, "XOR"};
                }
                return std::nullopt;
            }

            static bool IsOperatorWord(std::string_view Word)
            {
                return Word == "AND" || Word == "OR" || Word == "NOT" || Word == "MOD" || Word == "XOR";
            }

            std::size_t LongestFunctionName() const
            {
                std::size_t Words = 0;
                while (Position_ + Words < Tokens_.size() && Tokens_[Position_ + Words].Kind == TokenKind::Word)
                {
                    if (Words > 0 && IsOperatorWord(Canonical(Lexeme(Source_, Tokens_[Position_ + Words].Span))))
                    {
                        break;
                    }
                    ++Words;
                }
                if (Words == 0 || Position_ + Words >= Tokens_.size() ||
                    Tokens_[Position_ + Words].Kind != TokenKind::LeftParen)
                {
                    return 0;
                }

                for (std::size_t Count = Words; Count > 0; --Count)
                {
                    std::string Name;
                    for (std::size_t Index = 0; Index < Count; ++Index)
                    {
                        if (!Name.empty())
                            Name.push_back(' ');
                        Name += TokenText(Source_, Tokens_[Position_ + Index]);
                    }
                    if (Position_ + Count < Tokens_.size() && Tokens_[Position_ + Count].Kind == TokenKind::LeftParen &&
                        FindSymbol(Source_.Profile, SymbolKind::Function, Name) != nullptr)
                    {
                        return Count;
                    }
                }
                return Words;
            }

            static double ParseNumber(const std::string& Text)
            {
                if (!Text.empty() && Text.front() == '%')
                {
                    return static_cast<double>(std::strtoull(Text.c_str() + 1, nullptr, 2));
                }
                if (Text.size() > 2 && Text[0] == '0' && (Text[1] == 'c' || Text[1] == 'C'))
                {
                    return static_cast<double>(std::strtoull(Text.c_str() + 2, nullptr, 8));
                }
                if (Text.size() > 2 && Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X'))
                {
                    return static_cast<double>(std::strtoull(Text.c_str() + 2, nullptr, 16));
                }
                return std::strtod(Text.c_str(), nullptr);
            }

            static std::string DecodeString(std::string_view Text)
            {
                if (Text.size() >= 2 && Text.front() == '"' && Text.back() == '"')
                {
                    Text.remove_prefix(1);
                    Text.remove_suffix(1);
                }
                std::string Result;
                Result.reserve(Text.size());
                for (std::size_t Index = 0; Index < Text.size(); ++Index)
                {
                    if (Text[Index] == '"' && Index + 1 < Text.size() && Text[Index + 1] == '"')
                    {
                        Result.push_back('"');
                        ++Index;
                    }
                    else
                    {
                        Result.push_back(Text[Index]);
                    }
                }
                return Result;
            }

            const SourceDocument& Source_;
            std::span<const Token> Tokens_;
            std::vector<Diagnostic>& Diagnostics_;
            std::size_t Position_ = 0;
        };

        class Parser
        {
          public:
            explicit Parser(Program& Value) : Program_(Value), Source_(*Value.Source), Segments_(BuildSegments()) {}

            void Run()
            {
                while (!AtEnd())
                {
                    if (FirstIs(Peek(), "FUNCTION"))
                    {
                        ParseFunction();
                    }
                    else if (IsTypeDeclaration(Peek()))
                    {
                        ParseType();
                    }
                    else if (FirstIs(Peek(), "#CONSTANT"))
                    {
                        ParseConstant();
                    }
                    else if (FirstIs(Peek(), "#INCLUDE"))
                    {
                        ParseInclude();
                    }
                    else if (IsTerminator(Peek()))
                    {
                        AddError(Program_.Diagnostics, "DBC1200", "Unexpected block terminator", Peek().Span);
                        ++Position_;
                    }
                    else
                    {
                        Program_.Statements.push_back(ParseStatement());
                    }
                }
            }

          private:
            bool AtEnd() const
            {
                return Position_ >= Segments_.size();
            }

            Segment& Peek(std::size_t Lookahead = 0)
            {
                return Segments_[std::min(Position_ + Lookahead, Segments_.size() - 1)];
            }

            Segment Take()
            {
                return std::move(Segments_[Position_++]);
            }

            bool FirstIs(const Segment& Value, std::string_view Word) const
            {
                return !Value.Tokens.empty() && IsWord(Source_, Value.Tokens.front(), Word);
            }

            bool IsTypeDeclaration(const Segment& Value) const
            {
                return FirstIs(Value, "TYPE") && Value.Tokens.size() > 1 && Value.Tokens[1].Kind == TokenKind::Word;
            }

            bool ContainsWord(const std::vector<Token>& Tokens, std::string_view Word) const
            {
                int Depth = 0;
                for (const Token& Value : Tokens)
                {
                    if (Value.Kind == TokenKind::LeftParen)
                        ++Depth;
                    if (Value.Kind == TokenKind::RightParen)
                        --Depth;
                    if (Depth == 0 && IsWord(Source_, Value, Word))
                        return true;
                }
                return false;
            }

            std::vector<Segment> BuildSegments()
            {
                std::vector<Segment> Result;
                std::vector<Token> Line;
                for (const Token& Value : Program_.Tokens)
                {
                    if (Value.Kind != TokenKind::NewLine && Value.Kind != TokenKind::End)
                    {
                        Line.push_back(Value);
                        continue;
                    }
                    SplitLine(Line, Result);
                    Line.clear();
                }
                return Result;
            }

            void SplitLine(const std::vector<Token>& Line, std::vector<Segment>& Result)
            {
                if (Line.empty())
                    return;
                const bool InlineIf = IsWord(Source_, Line.front(), "IF") && ContainsWord(Line, "THEN");
                if (InlineIf)
                {
                    Result.push_back({Line, Merge(Line.front().Span, Line.back().Span), false, true});
                    return;
                }

                std::size_t Begin = 0;
                int Depth = 0;
                for (std::size_t Index = 0; Index <= Line.size(); ++Index)
                {
                    const bool End = Index == Line.size();
                    if (!End)
                    {
                        if (Line[Index].Kind == TokenKind::LeftParen)
                            ++Depth;
                        if (Line[Index].Kind == TokenKind::RightParen)
                            --Depth;
                    }
                    const bool Split = !End && Depth == 0 && Line[Index].Kind == TokenKind::Colon;
                    if (!End && !Split)
                        continue;
                    if (Index > Begin)
                    {
                        std::vector<Token> Tokens(Line.begin() + static_cast<std::ptrdiff_t>(Begin),
                                                  Line.begin() + static_cast<std::ptrdiff_t>(Index));
                        AppendDeclarationList(
                            {std::move(Tokens), Merge(Line[Begin].Span, Line[Index - 1].Span), Split, Begin == 0},
                            Result);
                    }
                    Begin = Index + 1;
                }
            }

            // DBPro's GLOBAL and LOCAL declare every comma-separated name, each with its own optional
            // AS type; an '=' initializer takes the rest of the statement. Each name becomes its own
            // declaration segment so an empty item still reports a missing name.
            void AppendDeclarationList(Segment Value, std::vector<Segment>& Result) const
            {
                if (!FirstIs(Value, "GLOBAL") && !FirstIs(Value, "LOCAL"))
                {
                    Result.push_back(std::move(Value));
                    return;
                }
                const Token Keyword = Value.Tokens.front();
                std::size_t Begin = 1;
                int Depth = 0;
                for (std::size_t Index = 1; Index < Value.Tokens.size(); ++Index)
                {
                    const TokenKind Kind = Value.Tokens[Index].Kind;
                    if (Kind == TokenKind::LeftParen)
                        ++Depth;
                    if (Kind == TokenKind::RightParen)
                        --Depth;
                    if (Depth == 0 && Kind == TokenKind::Equal)
                        break;
                    if (Depth != 0 || Kind != TokenKind::Comma)
                        continue;
                    std::vector<Token> Tokens{Keyword};
                    Tokens.insert(Tokens.end(), Value.Tokens.begin() + static_cast<std::ptrdiff_t>(Begin),
                                  Value.Tokens.begin() + static_cast<std::ptrdiff_t>(Index));
                    const SourceSpan Span = Merge(Begin == 1 ? Keyword.Span : Value.Tokens[Begin].Span,
                                                  Value.Tokens[Index].Span);
                    Result.push_back({std::move(Tokens), Span, false, Begin == 1 && Value.StartsLine});
                    Begin = Index + 1;
                }
                if (Begin == 1)
                {
                    Result.push_back(std::move(Value));
                    return;
                }
                std::vector<Token> Tokens{Keyword};
                Tokens.insert(Tokens.end(), Value.Tokens.begin() + static_cast<std::ptrdiff_t>(Begin),
                              Value.Tokens.end());
                const SourceSpan Span =
                    Merge(Begin < Value.Tokens.size() ? Value.Tokens[Begin].Span : Value.Tokens.back().Span,
                          Value.Tokens.back().Span);
                Result.push_back({std::move(Tokens), Span, Value.EndedByColon, false});
            }

            static bool IsTerminatorWord(std::string_view Word)
            {
                static const std::set<std::string_view> Words = {
                    "NEXT",  "ENDWHILE",  "UNTIL", "LOOP",    "ELSEIF",      "ELSE",
                    "ENDIF", "ENDSELECT", "CASE",  "ENDCASE", "ENDFUNCTION", "ENDTYPE",
                };
                return Words.contains(Word);
            }

            // A lone word before ':' is a label only when it cannot be a statement or command.
            bool IsReservedWord(const std::string& Word) const
            {
                if (IsTerminatorWord(Word))
                    return true;
                return FindSymbol(Source_.Profile, SymbolKind::Statement, Word) != nullptr ||
                       FindSymbol(Source_.Profile, SymbolKind::Command, Word) != nullptr;
            }

            bool IsTerminator(const Segment& Value) const
            {
                if (Value.Tokens.empty() || Value.Tokens.front().Kind != TokenKind::Word)
                {
                    return false;
                }
                const std::string Word = Canonical(Lexeme(Source_, Value.Tokens.front().Span));
                if (!IsTerminatorWord(Word))
                {
                    return false;
                }
                if (Word == "LOOP" && Value.Tokens.size() != 1)
                    return false;
                if (Word == "NEXT" && FindLongestCommandPrefix(Source_.Profile, Lexeme(Source_, Value.Span)))
                    return false;
                return true;
            }

            std::vector<Statement> ParseBlock(std::initializer_list<std::string_view> Terminators)
            {
                std::vector<Statement> Result;
                while (!AtEnd())
                {
                    bool Stop = false;
                    for (const std::string_view Terminator : Terminators)
                    {
                        Stop |= FirstIs(Peek(), Terminator) && (Terminator != "LOOP" || Peek().Tokens.size() == 1) &&
                                (Terminator != "NEXT" || IsTerminator(Peek()));
                    }
                    if (Stop)
                        break;
                    if (FirstIs(Peek(), "FUNCTION") || IsTypeDeclaration(Peek()))
                    {
                        AddError(Program_.Diagnostics, "DBC1201", "Declarations cannot be nested in a statement block",
                                 Peek().Span);
                        ++Position_;
                        continue;
                    }
                    Result.push_back(ParseStatement());
                }
                return Result;
            }

            Statement ParseStatement()
            {
                if (AtEnd())
                    return {};
                if (FirstIs(Peek(), "FOR"))
                    return ParseFor();
                if (FirstIs(Peek(), "WHILE"))
                    return ParseWhile();
                if (FirstIs(Peek(), "REPEAT"))
                    return ParseRepeat();
                if (FirstIs(Peek(), "DO"))
                    return ParseDo();
                if (FirstIs(Peek(), "IF"))
                    return ParseIf();
                if (FirstIs(Peek(), "SELECT"))
                    return ParseSelect();
                return ParseSimple(Take());
            }

            Statement ParseFor()
            {
                Segment Header = Take();
                Statement Result;
                Result.Kind = StatementKind::For;
                Result.Span = Header.Span;
                const std::size_t Equal = FindTopLevel(Header.Tokens, TokenKind::Equal, 1);
                const std::size_t To = FindTopLevelWord(Header.Tokens, "TO", Equal == Npos ? 1 : Equal + 1);
                const std::size_t Step = FindTopLevelWord(Header.Tokens, "STEP", To == Npos ? 1 : To + 1);
                if (Equal == Npos || To == Npos || Equal != 2 || Header.Tokens[1].Kind != TokenKind::Word)
                {
                    AddError(Program_.Diagnostics, "DBC1202", "FOR requires 'name = value TO value [STEP value]'",
                             Header.Span);
                }
                else
                {
                    Result.Name = Canonical(Lexeme(Source_, Header.Tokens[1].Span));
                    Result.Expressions.push_back(ParseExpression(Header.Tokens, Equal + 1, To));
                    Result.Expressions.push_back(
                        ParseExpression(Header.Tokens, To + 1, Step == Npos ? Header.Tokens.size() : Step));
                    if (Step == Npos)
                    {
                        Result.Expressions.push_back(MakeInteger(Header.Span, 1));
                    }
                    else
                    {
                        Result.Expressions.push_back(ParseExpression(Header.Tokens, Step + 1, Header.Tokens.size()));
                    }
                }
                Result.Body = ParseBlock({"NEXT"});
                if (AtEnd())
                {
                    AddError(Program_.Diagnostics, "DBC1203", "FOR has no matching NEXT", Header.Span);
                }
                else
                {
                    Segment End = Take();
                    Result.Span = Merge(Result.Span, End.Span);
                }
                return Result;
            }

            Statement ParseWhile()
            {
                Segment Header = Take();
                Statement Result;
                Result.Kind = StatementKind::While;
                Result.Span = Header.Span;
                Result.Expressions.push_back(ParseExpression(Header.Tokens, 1, Header.Tokens.size()));
                Result.Body = ParseBlock({"ENDWHILE"});
                CloseBlock(Result, "ENDWHILE", "WHILE has no matching ENDWHILE");
                return Result;
            }

            Statement ParseRepeat()
            {
                Segment Header = Take();
                Statement Result;
                Result.Kind = StatementKind::Repeat;
                Result.Span = Header.Span;
                Result.Body = ParseBlock({"UNTIL"});
                if (AtEnd())
                {
                    AddError(Program_.Diagnostics, "DBC1205", "REPEAT has no matching UNTIL", Header.Span);
                }
                else
                {
                    Segment End = Take();
                    Result.Expressions.push_back(ParseExpression(End.Tokens, 1, End.Tokens.size()));
                    Result.Span = Merge(Result.Span, End.Span);
                }
                return Result;
            }

            Statement ParseDo()
            {
                Segment Header = Take();
                Statement Result;
                Result.Kind = StatementKind::Do;
                Result.Span = Header.Span;
                Result.Body = ParseBlock({"LOOP"});
                CloseBlock(Result, "LOOP", "DO has no matching LOOP");
                return Result;
            }

            Statement ParseIf()
            {
                Segment Header = Take();
                const std::size_t Then = FindTopLevelWord(Header.Tokens, "THEN", 1);
                Statement Result;
                Result.Kind = StatementKind::If;
                Result.Span = Header.Span;
                Result.Expressions.push_back(
                    ParseExpression(Header.Tokens, 1, Then == Npos ? Header.Tokens.size() : Then));
                if (Then != Npos && Then + 1 < Header.Tokens.size())
                {
                    const std::size_t Else = FindTopLevelWord(Header.Tokens, "ELSE", Then + 1);
                    Result.Body = ParseInlineBlock(Header.Tokens, Then + 1, Else == Npos ? Header.Tokens.size() : Else);
                    if (Else != Npos)
                    {
                        Result.ElseBody = ParseInlineBlock(Header.Tokens, Else + 1, Header.Tokens.size());
                    }
                    return Result;
                }

                Result.Body = ParseBlock({"ELSEIF", "ELSE", "ENDIF"});
                Statement* Branch = &Result;
                while (!AtEnd() && FirstIs(Peek(), "ELSEIF"))
                {
                    Segment ElseIfHeader = Take();
                    const std::size_t ElseIfThen = FindTopLevelWord(ElseIfHeader.Tokens, "THEN", 1);
                    Statement ElseIf;
                    ElseIf.Kind = StatementKind::If;
                    ElseIf.Span = ElseIfHeader.Span;
                    ElseIf.Builtin = SymbolId::Statement_Elseif;
                    ElseIf.Expressions.push_back(ParseExpression(
                        ElseIfHeader.Tokens, 1, ElseIfThen == Npos ? ElseIfHeader.Tokens.size() : ElseIfThen));
                    ElseIf.Body = ParseBlock({"ELSEIF", "ELSE", "ENDIF"});
                    Branch->ElseBody.push_back(std::move(ElseIf));
                    Branch = &Branch->ElseBody.back();
                }
                if (!AtEnd() && FirstIs(Peek(), "ELSE"))
                {
                    Take();
                    Branch->ElseBody = ParseBlock({"ENDIF"});
                }
                CloseBlock(Result, "ENDIF", "IF has no matching ENDIF");
                return Result;
            }

            Statement ParseSelect()
            {
                Segment Header = Take();
                Statement Result;
                Result.Kind = StatementKind::Select;
                Result.Span = Header.Span;
                Result.Expressions.push_back(ParseExpression(Header.Tokens, 1, Header.Tokens.size()));
                while (!AtEnd() && !FirstIs(Peek(), "ENDSELECT"))
                {
                    if (!FirstIs(Peek(), "CASE"))
                    {
                        AddError(Program_.Diagnostics, "DBC1206", "SELECT body must begin with CASE", Peek().Span);
                        ++Position_;
                        continue;
                    }
                    Segment CaseHeader = Take();
                    CaseClause Clause;
                    Clause.Span = CaseHeader.Span;
                    Clause.IsDefault = CaseHeader.Tokens.size() > 1 && IsWord(Source_, CaseHeader.Tokens[1], "DEFAULT");
                    if (!Clause.IsDefault)
                    {
                        Clause.Values = ParseExpressionList(CaseHeader.Tokens, 1, CaseHeader.Tokens.size());
                    }
                    Clause.Body = ParseBlock({"ENDCASE", "CASE", "ENDSELECT"});
                    if (!AtEnd() && FirstIs(Peek(), "ENDCASE"))
                    {
                        Clause.Span = Merge(Clause.Span, Take().Span);
                    }
                    Result.Cases.push_back(std::move(Clause));
                }
                CloseBlock(Result, "ENDSELECT", "SELECT has no matching ENDSELECT");
                return Result;
            }

            void CloseBlock(Statement& Value, std::string_view Terminator, std::string Message)
            {
                if (AtEnd() || !FirstIs(Peek(), Terminator))
                {
                    AddError(Program_.Diagnostics, "DBC1207", std::move(Message), Value.Span);
                    return;
                }
                Value.Span = Merge(Value.Span, Take().Span);
            }

            Statement ParseSimple(Segment Value)
            {
                Statement Result;
                Result.Span = Value.Span;
                if (Value.Tokens.empty())
                    return Result;

                if (Value.EndedByColon && Value.StartsLine && Value.Tokens.size() == 1 &&
                    Value.Tokens.front().Kind == TokenKind::Word &&
                    !IsReservedWord(Canonical(Lexeme(Source_, Value.Tokens.front().Span))))
                {
                    Result.Kind = StatementKind::Label;
                    Result.Name = Canonical(Lexeme(Source_, Value.Tokens.front().Span));
                    return Result;
                }
                if (FirstIs(Value, "DIM"))
                    return ParseDeclaration(Value, StorageClass::Dim, 1);
                if (FirstIs(Value, "LOCAL"))
                    return ParseDeclaration(Value, StorageClass::Local, 1);
                if (FirstIs(Value, "GLOBAL"))
                    return ParseDeclaration(Value, StorageClass::Global, 1);
                if (FirstIs(Value, "INC") || FirstIs(Value, "DEC"))
                    return ParseIncDec(Value);
                if (FirstIs(Value, "GOTO") || FirstIs(Value, "GOSUB"))
                {
                    Result.Kind = FirstIs(Value, "GOTO") ? StatementKind::Goto : StatementKind::Gosub;
                    if (Value.Tokens.size() < 2)
                    {
                        AddError(Program_.Diagnostics, "DBC1208", "A jump target is required", Value.Span);
                    }
                    else
                    {
                        Result.Name = Canonical(Lexeme(Source_, Value.Tokens[1].Span));
                    }
                    return Result;
                }
                if (FirstIs(Value, "RETURN"))
                {
                    Result.Kind = StatementKind::Return;
                    if (Value.Tokens.size() > 1)
                        Result.Expressions.push_back(ParseExpression(Value.Tokens, 1, Value.Tokens.size()));
                    return Result;
                }
                if (FirstIs(Value, "EXITFUNCTION"))
                {
                    Result.Kind = StatementKind::ExitFunction;
                    if (Value.Tokens.size() > 1)
                        Result.Expressions.push_back(ParseExpression(Value.Tokens, 1, Value.Tokens.size()));
                    return Result;
                }
                if (FirstIs(Value, "EXIT"))
                {
                    if (FindLongestCommandPrefix(Source_.Profile, Lexeme(Source_, Value.Span)) != nullptr)
                        return ParseCommand(Value);
                    Result.Kind = StatementKind::Exit;
                    return Result;
                }
                if (FirstIs(Value, "END"))
                {
                    Result.Kind = StatementKind::End;
                    return Result;
                }
                if (FirstIs(Value, "DATA"))
                {
                    Result.Kind = StatementKind::Data;
                    Result.Name = "DATA";
                    if (const SymbolDescriptor* Descriptor = FindSymbol(Source_.Profile, SymbolKind::Command, "DATA"))
                        Result.Builtin = Descriptor->Id;
                    Result.Expressions = ParseExpressionList(Value.Tokens, 1, Value.Tokens.size());
                    return Result;
                }

                if (FindLongestCommandPrefix(Source_.Profile, Lexeme(Source_, Value.Span)) != nullptr)
                {
                    return ParseCommand(Value);
                }

                const std::size_t As = FindTopLevelWord(Value.Tokens, "AS", 1);
                if (As != Npos && Value.Tokens.front().Kind == TokenKind::Word)
                {
                    return ParseDeclaration(Value, StorageClass::Dim, 0);
                }

                const std::size_t Equal = FindTopLevel(Value.Tokens, TokenKind::Equal, 0);
                if (Equal != Npos)
                {
                    Result.Kind = StatementKind::Assignment;
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, 0, Equal));
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, Equal + 1, Value.Tokens.size()));
                    return Result;
                }
                if (Value.Tokens.size() > 1 && Value.Tokens.front().Kind == TokenKind::Word &&
                    Value.Tokens[1].Kind == TokenKind::LeftParen)
                {
                    Result.Kind = StatementKind::Expression;
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, 0, Value.Tokens.size()));
                    return Result;
                }
                if (Value.Tokens.size() > 2 && Value.Tokens.front().Kind == TokenKind::Word &&
                    (Value.Tokens[1].Kind == TokenKind::Plus || Value.Tokens[1].Kind == TokenKind::Minus))
                {
                    Result.Kind = StatementKind::Assignment;
                    Result.Name = "SELF";
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, 0, 1));
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, 0, Value.Tokens.size()));
                    return Result;
                }
                return ParseCommand(Value);
            }

            Statement ParseDeclaration(const Segment& Value, StorageClass Storage, std::size_t NameIndex)
            {
                Statement Result;
                Result.Kind = StatementKind::Declaration;
                Result.Span = Value.Span;
                Result.Storage = Storage;
                if (NameIndex >= Value.Tokens.size() || Value.Tokens[NameIndex].Kind != TokenKind::Word)
                {
                    AddError(Program_.Diagnostics, "DBC1209", "A declaration requires a variable name", Value.Span);
                    return Result;
                }
                Result.Name = Canonical(Lexeme(Source_, Value.Tokens[NameIndex].Span));
                Result.DeclaredType = ImplicitType(Result.Name);
                std::size_t Position = NameIndex + 1;
                if (Position < Value.Tokens.size() && Value.Tokens[Position].Kind == TokenKind::LeftParen)
                {
                    const std::size_t Close = MatchingRightParen(Value.Tokens, Position);
                    if (Close == Npos)
                    {
                        AddError(Program_.Diagnostics, "DBC1210", "Array dimensions require ')'", Value.Span);
                        return Result;
                    }
                    Result.Expressions = ParseExpressionList(Value.Tokens, Position + 1, Close);
                    Result.ArrayRank = Result.Expressions.size();
                    Position = Close + 1;
                }
                if (Position < Value.Tokens.size() && IsWord(Source_, Value.Tokens[Position], "AS"))
                {
                    std::size_t End = FindTopLevel(Value.Tokens, TokenKind::Equal, Position + 1);
                    if (End == Npos)
                        End = Value.Tokens.size();
                    Result.DeclaredType = NamedType(JoinWords(Value.Tokens, Position + 1, End));
                    Result.HasExplicitType = true;
                    Position = End;
                }
                if (Position < Value.Tokens.size() && Value.Tokens[Position].Kind == TokenKind::Equal)
                {
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, Position + 1, Value.Tokens.size()));
                }
                return Result;
            }

            Statement ParseIncDec(const Segment& Value)
            {
                Statement Result;
                Result.Kind = StatementKind::Assignment;
                Result.Name = FirstIs(Value, "INC") ? "INC" : "DEC";
                Result.Span = Value.Span;
                const std::size_t Comma = FindTopLevel(Value.Tokens, TokenKind::Comma, 1);
                Result.Expressions.push_back(
                    ParseExpression(Value.Tokens, 1, Comma == Npos ? Value.Tokens.size() : Comma));
                if (Comma == Npos)
                    Result.Expressions.push_back(MakeInteger(Value.Span, 1));
                else
                    Result.Expressions.push_back(ParseExpression(Value.Tokens, Comma + 1, Value.Tokens.size()));
                return Result;
            }

            Statement ParseCommand(const Segment& Value)
            {
                Statement Result;
                Result.Kind = StatementKind::Command;
                Result.Span = Value.Span;
                std::size_t Words = 0;
                while (Words < Value.Tokens.size() && Value.Tokens[Words].Kind == TokenKind::Word)
                    ++Words;
                const SymbolDescriptor* Descriptor = nullptr;
                std::size_t NameWords = 0;
                for (std::size_t Count = Words; Count > 0; --Count)
                {
                    const std::string Name = JoinWords(Value.Tokens, 0, Count);
                    Descriptor = FindSymbol(Source_.Profile, SymbolKind::Command, Name);
                    if (Descriptor)
                    {
                        NameWords = Count;
                        break;
                    }
                }
                if (!Descriptor)
                {
                    Result.Name = Words == 0 ? std::string{} : JoinWords(Value.Tokens, 0, Words);
                    return Result;
                }
                else
                {
                    Result.Builtin = Descriptor->Id;
                    Result.Name = std::string(Descriptor->CanonicalName);
                }
                Result.ContinuesPrint = Descriptor->Id == SymbolId::Command_Print && !Value.Tokens.empty() &&
                                        Value.Tokens.back().Kind == TokenKind::Semicolon;
                const std::size_t DiagnosticCount = Program_.Diagnostics.size();
                Result.Expressions = ParseExpressionList(Value.Tokens, NameWords, Value.Tokens.size());
                if (Descriptor && Words > NameWords && Program_.Diagnostics.size() > DiagnosticCount)
                {
                    Program_.Diagnostics.resize(DiagnosticCount);
                    Result.Builtin.reset();
                    Result.Name = JoinWords(Value.Tokens, 0, Words);
                    Result.Expressions.clear();
                }
                return Result;
            }

            void ParseFunction()
            {
                Segment Header = Take();
                FunctionDeclaration Result;
                Result.Span = Header.Span;
                if (Header.Tokens.size() < 2 || Header.Tokens[1].Kind != TokenKind::Word)
                {
                    AddError(Program_.Diagnostics, "DBC1211", "FUNCTION requires a name", Header.Span);
                    return;
                }
                Result.Name = Canonical(Lexeme(Source_, Header.Tokens[1].Span));
                Result.ReturnType = ImplicitType(Result.Name);
                if (Header.Tokens.size() > 2)
                {
                    if (Header.Tokens[2].Kind != TokenKind::LeftParen)
                    {
                        AddError(Program_.Diagnostics, "DBC1212", "Expected '(' after function name",
                                 Header.Tokens[2].Span);
                    }
                    else
                    {
                        const std::size_t Close = MatchingRightParen(Header.Tokens, 2);
                        if (Close == Npos)
                        {
                            AddError(Program_.Diagnostics, "DBC1213", "Function parameter list requires ')'",
                                     Header.Span);
                        }
                        else
                        {
                            ParseParameters(Header.Tokens, 3, Close, Result.Parameters);
                        }
                    }
                }
                Result.Body = ParseBlock({"ENDFUNCTION"});
                if (AtEnd())
                {
                    AddError(Program_.Diagnostics, "DBC1214", "FUNCTION has no matching ENDFUNCTION", Header.Span);
                }
                else
                {
                    Segment End = Take();
                    Result.Span = Merge(Result.Span, End.Span);
                    if (End.Tokens.size() > 1)
                    {
                        Result.FinalReturn = ParseExpression(End.Tokens, 1, End.Tokens.size());
                    }
                }
                Program_.Functions.push_back(std::move(Result));
            }

            void ParseParameters(const std::vector<Token>& Tokens, std::size_t Begin, std::size_t End,
                                 std::vector<ParameterDeclaration>& Output)
            {
                if (Begin >= End)
                {
                    return;
                }
                for (const auto& [PartBegin, PartEnd] : SplitList(Tokens, Begin, End))
                {
                    if (PartBegin == PartEnd || Tokens[PartBegin].Kind != TokenKind::Word)
                    {
                        AddError(Program_.Diagnostics, "DBC1215", "Invalid function parameter", Tokens[PartBegin].Span);
                        continue;
                    }
                    ParameterDeclaration Parameter;
                    Parameter.Span = Merge(Tokens[PartBegin].Span, Tokens[PartEnd - 1].Span);
                    Parameter.Name = Canonical(Lexeme(Source_, Tokens[PartBegin].Span));
                    Parameter.DeclaredType = ImplicitType(Parameter.Name);
                    if (PartBegin + 1 < PartEnd && IsWord(Source_, Tokens[PartBegin + 1], "AS"))
                    {
                        Parameter.DeclaredType = NamedType(JoinWords(Tokens, PartBegin + 2, PartEnd));
                        Parameter.HasExplicitType = true;
                    }
                    Output.push_back(std::move(Parameter));
                }
            }

            void ParseType()
            {
                Segment Header = Take();
                TypeDeclaration Result;
                Result.Span = Header.Span;
                if (Header.Tokens.size() < 2 || Header.Tokens[1].Kind != TokenKind::Word)
                {
                    AddError(Program_.Diagnostics, "DBC1216", "TYPE requires a name", Header.Span);
                    return;
                }
                Result.Name = Canonical(Lexeme(Source_, Header.Tokens[1].Span));

                const std::size_t InlineEnd = FindTopLevelWord(Header.Tokens, "ENDTYPE", 2);
                if (InlineEnd != Npos)
                {
                    ParseInlineFields(Header.Tokens, 2, InlineEnd, Result.Fields);
                    Program_.Types.push_back(std::move(Result));
                    return;
                }
                while (!AtEnd() && !FirstIs(Peek(), "ENDTYPE"))
                {
                    Segment Field = Take();
                    if (Field.Tokens.empty() || Field.Tokens.front().Kind != TokenKind::Word)
                    {
                        AddError(Program_.Diagnostics, "DBC1217", "Invalid TYPE field", Field.Span);
                        continue;
                    }
                    FieldDeclaration Declaration;
                    Declaration.Span = Field.Span;
                    Declaration.Name = Canonical(Lexeme(Source_, Field.Tokens.front().Span));
                    Declaration.DeclaredType = ImplicitType(Declaration.Name);
                    if (Field.Tokens.size() > 2 && IsWord(Source_, Field.Tokens[1], "AS"))
                    {
                        Declaration.DeclaredType = NamedType(JoinWords(Field.Tokens, 2, Field.Tokens.size()));
                    }
                    Result.Fields.push_back(std::move(Declaration));
                }
                if (AtEnd())
                {
                    AddError(Program_.Diagnostics, "DBC1218", "TYPE has no matching ENDTYPE", Header.Span);
                }
                else
                {
                    Result.Span = Merge(Result.Span, Take().Span);
                }
                Program_.Types.push_back(std::move(Result));
            }

            void ParseInlineFields(const std::vector<Token>& Tokens, std::size_t Begin, std::size_t End,
                                   std::vector<FieldDeclaration>& Output)
            {
                std::size_t Position = Begin;
                while (Position < End)
                {
                    if (Tokens[Position].Kind != TokenKind::Word)
                    {
                        AddError(Program_.Diagnostics, "DBC1219", "Invalid inline TYPE field", Tokens[Position].Span);
                        ++Position;
                        continue;
                    }
                    FieldDeclaration Field;
                    Field.Name = Canonical(Lexeme(Source_, Tokens[Position].Span));
                    Field.Span = Tokens[Position].Span;
                    Field.DeclaredType = ImplicitType(Field.Name);
                    ++Position;
                    if (Position < End && IsWord(Source_, Tokens[Position], "AS"))
                    {
                        ++Position;
                        if (Position < End && Tokens[Position].Kind == TokenKind::Word)
                        {
                            Field.DeclaredType = NamedType(TokenText(Source_, Tokens[Position]));
                            Field.Span = Merge(Field.Span, Tokens[Position].Span);
                            ++Position;
                        }
                    }
                    Output.push_back(std::move(Field));
                }
            }

            void ParseConstant()
            {
                Segment Value = Take();
                ConstantDeclaration Result;
                Result.Span = Value.Span;
                if (Value.Tokens.size() < 3 || Value.Tokens[1].Kind != TokenKind::Word)
                {
                    AddError(Program_.Diagnostics, "DBC1220", "#CONSTANT requires a name and value", Value.Span);
                    return;
                }
                Result.Name = Canonical(Lexeme(Source_, Value.Tokens[1].Span));
                Result.Value = ParseExpression(Value.Tokens, 2, Value.Tokens.size());
                Program_.Constants.push_back(std::move(Result));
            }

            void ParseInclude()
            {
                Segment Value = Take();
                if (Value.Tokens.size() != 2 || Value.Tokens[1].Kind != TokenKind::String)
                {
                    AddError(Program_.Diagnostics, "DBC1221", "#INCLUDE requires one quoted path", Value.Span);
                    return;
                }
                std::string Path = TokenText(Source_, Value.Tokens[1]);
                if (Path.size() >= 2)
                {
                    Path = Path.substr(1, Path.size() - 2);
                }
                Program_.Includes.push_back({Value.Span, std::filesystem::path(Path), Program_.Statements.size()});
            }

            std::vector<Statement> ParseInlineBlock(const std::vector<Token>& Tokens, std::size_t Begin,
                                                    std::size_t End)
            {
                std::vector<Statement> Result;
                std::size_t PartBegin = Begin;
                int Depth = 0;
                for (std::size_t Index = Begin; Index <= End; ++Index)
                {
                    const bool AtRangeEnd = Index == End;
                    if (!AtRangeEnd)
                    {
                        if (Tokens[Index].Kind == TokenKind::LeftParen)
                            ++Depth;
                        if (Tokens[Index].Kind == TokenKind::RightParen)
                            --Depth;
                    }
                    if (!AtRangeEnd && !(Depth == 0 && Tokens[Index].Kind == TokenKind::Colon))
                        continue;
                    if (Index > PartBegin)
                    {
                        std::vector<Token> Part(Tokens.begin() + static_cast<std::ptrdiff_t>(PartBegin),
                                                Tokens.begin() + static_cast<std::ptrdiff_t>(Index));
                        Segment Value{std::move(Part), Merge(Tokens[PartBegin].Span, Tokens[Index - 1].Span), false};
                        if (FirstIs(Value, "IF"))
                        {
                            const std::size_t Then = FindTopLevelWord(Value.Tokens, "THEN", 1);
                            if (Then != Npos)
                            {
                                Statement Nested;
                                Nested.Kind = StatementKind::If;
                                Nested.Span = Value.Span;
                                Nested.Expressions.push_back(ParseExpression(Value.Tokens, 1, Then));
                                Nested.Body = ParseInlineBlock(Value.Tokens, Then + 1, Value.Tokens.size());
                                Result.push_back(std::move(Nested));
                            }
                            else
                            {
                                Result.push_back(ParseSimple(std::move(Value)));
                            }
                        }
                        else
                        {
                            std::vector<Segment> Declarations;
                            AppendDeclarationList(std::move(Value), Declarations);
                            for (Segment& Declaration : Declarations)
                                Result.push_back(ParseSimple(std::move(Declaration)));
                        }
                    }
                    PartBegin = Index + 1;
                }
                return Result;
            }

            std::unique_ptr<Expression> ParseExpression(const std::vector<Token>& Tokens, std::size_t Begin,
                                                        std::size_t End)
            {
                if (Begin >= End || End > Tokens.size())
                {
                    const SourceSpan Span = Tokens.empty() ? SourceSpan{} : Tokens.front().Span;
                    AddError(Program_.Diagnostics, "DBC1222", "Expected an expression", Span);
                    auto Error = std::make_unique<Expression>();
                    Error->Kind = ExpressionKind::Error;
                    Error->ResolvedType = {TypeKind::Error, {}};
                    Error->Span = Span;
                    return Error;
                }
                return ExpressionReader(Source_, std::span<const Token>(Tokens).subspan(Begin, End - Begin),
                                        Program_.Diagnostics)
                    .Parse();
            }

            std::vector<std::unique_ptr<Expression>> ParseExpressionList(const std::vector<Token>& Tokens,
                                                                         std::size_t Begin, std::size_t End)
            {
                std::vector<std::unique_ptr<Expression>> Result;
                if (Begin >= End)
                    return Result;
                for (const auto& [PartBegin, PartEnd] : SplitList(Tokens, Begin, End))
                {
                    if (PartBegin < PartEnd)
                    {
                        Result.push_back(ParseExpression(Tokens, PartBegin, PartEnd));
                    }
                }
                return Result;
            }

            std::vector<std::pair<std::size_t, std::size_t>> SplitList(const std::vector<Token>& Tokens,
                                                                       std::size_t Begin, std::size_t End) const
            {
                std::vector<std::pair<std::size_t, std::size_t>> Result;
                std::size_t PartBegin = Begin;
                int Depth = 0;
                for (std::size_t Index = Begin; Index <= End; ++Index)
                {
                    const bool AtRangeEnd = Index == End;
                    if (!AtRangeEnd)
                    {
                        if (Tokens[Index].Kind == TokenKind::LeftParen)
                            ++Depth;
                        if (Tokens[Index].Kind == TokenKind::RightParen)
                            --Depth;
                    }
                    if (!AtRangeEnd && !(Depth == 0 && IsDelimiter(Tokens[Index].Kind)))
                        continue;
                    Result.emplace_back(PartBegin, Index);
                    PartBegin = Index + 1;
                }
                return Result;
            }

            std::unique_ptr<Expression> MakeInteger(SourceSpan Span, int Value)
            {
                auto Result = std::make_unique<Expression>();
                Result->Kind = ExpressionKind::IntegerLiteral;
                Result->Span = Span;
                Result->ResolvedType = {TypeKind::Integer, {}};
                Result->Number = static_cast<double>(Value);
                Result->Text = std::to_string(Value);
                return Result;
            }

            std::string JoinWords(const std::vector<Token>& Tokens, std::size_t Begin, std::size_t End) const
            {
                std::string Result;
                for (std::size_t Index = Begin; Index < End; ++Index)
                {
                    if (!Result.empty())
                        Result.push_back(' ');
                    Result += TokenText(Source_, Tokens[Index]);
                }
                return Canonical(Result);
            }

            std::size_t FindTopLevel(const std::vector<Token>& Tokens, TokenKind Kind, std::size_t Begin) const
            {
                int Depth = 0;
                for (std::size_t Index = Begin; Index < Tokens.size(); ++Index)
                {
                    if (Tokens[Index].Kind == TokenKind::LeftParen)
                        ++Depth;
                    if (Tokens[Index].Kind == TokenKind::RightParen)
                        --Depth;
                    if (Depth == 0 && Tokens[Index].Kind == Kind)
                        return Index;
                }
                return Npos;
            }

            std::size_t FindTopLevelWord(const std::vector<Token>& Tokens, std::string_view Word,
                                         std::size_t Begin) const
            {
                int Depth = 0;
                for (std::size_t Index = Begin; Index < Tokens.size(); ++Index)
                {
                    if (Tokens[Index].Kind == TokenKind::LeftParen)
                        ++Depth;
                    if (Tokens[Index].Kind == TokenKind::RightParen)
                        --Depth;
                    if (Depth == 0 && IsWord(Source_, Tokens[Index], Word))
                        return Index;
                }
                return Npos;
            }

            static std::size_t MatchingRightParen(const std::vector<Token>& Tokens, std::size_t Begin)
            {
                int Depth = 0;
                for (std::size_t Index = Begin; Index < Tokens.size(); ++Index)
                {
                    if (Tokens[Index].Kind == TokenKind::LeftParen)
                        ++Depth;
                    if (Tokens[Index].Kind == TokenKind::RightParen && --Depth == 0)
                        return Index;
                }
                return Npos;
            }

            static constexpr std::size_t Npos = std::numeric_limits<std::size_t>::max();

            Program& Program_;
            const SourceDocument& Source_;
            std::vector<Segment> Segments_;
            std::size_t Position_ = 0;
        };
    } // namespace

    void Parse(Program& Value)
    {
        Parser(Value).Run();
    }
} // namespace dbc::language::detail
