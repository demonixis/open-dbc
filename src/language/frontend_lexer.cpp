#include "frontend_internal.hpp"

#include <cctype>
#include <cstdlib>

namespace dbc::language::detail
{
    namespace
    {
        class Lexer
        {
          public:
            Lexer(const SourceDocument& Source, std::vector<Diagnostic>& Diagnostics)
                : Source_(Source), Diagnostics_(Diagnostics)
            {
            }

            std::vector<Token> Run()
            {
                while (!AtEnd())
                {
                    const char Character = Peek();
                    if (Character == ' ' || Character == '\t' || Character == '\f' || Character == '\v')
                    {
                        Advance();
                        continue;
                    }
                    if (Character == '\r' || Character == '\n')
                    {
                        LexNewLine();
                        CanStartComment_ = true;
                        continue;
                    }
                    if (Character == '`' || Character == '\'')
                    {
                        SkipComment();
                        continue;
                    }
                    if (Character == '"')
                    {
                        LexString();
                        CanStartComment_ = false;
                        continue;
                    }
                    if (std::isalpha(static_cast<unsigned char>(Character)) != 0 || Character == '_' ||
                        Character == '#')
                    {
                        LexWord();
                        continue;
                    }
                    if (std::isdigit(static_cast<unsigned char>(Character)) != 0 ||
                        (Character == '.' && std::isdigit(static_cast<unsigned char>(Peek(1))) != 0) ||
                        (Character == '%' && (Peek(1) == '0' || Peek(1) == '1')))
                    {
                        LexNumber();
                        CanStartComment_ = false;
                        continue;
                    }
                    LexPunctuation();
                }

                const SourcePosition End = Position();
                Tokens_.push_back({TokenKind::End, MakeSpan(End, End)});
                return std::move(Tokens_);
            }

          private:
            bool AtEnd(std::size_t Lookahead = 0) const
            {
                return Offset_ + Lookahead >= Source_.Text.size();
            }

            char Peek(std::size_t Lookahead = 0) const
            {
                return AtEnd(Lookahead) ? '\0' : Source_.Text[Offset_ + Lookahead];
            }

            SourcePosition Position() const
            {
                return {Offset_, Line_, Column_};
            }

            SourceSpan MakeSpan(SourcePosition Begin, SourcePosition End) const
            {
                return {Begin, End, Source_.LogicalPath};
            }

            char Advance()
            {
                if (AtEnd())
                {
                    return '\0';
                }
                const char Result = Source_.Text[Offset_++];
                if (Result == '\n')
                {
                    ++Line_;
                    Column_ = 1;
                }
                else
                {
                    ++Column_;
                }
                return Result;
            }

            void Push(TokenKind Kind, SourcePosition Begin)
            {
                Tokens_.push_back({Kind, MakeSpan(Begin, Position())});
            }

            void LexNewLine()
            {
                const SourcePosition Begin = Position();
                if (Peek() == '\r')
                {
                    ++Offset_;
                    if (Peek() == '\n')
                    {
                        ++Offset_;
                    }
                    ++Line_;
                    Column_ = 1;
                }
                else
                {
                    Advance();
                }
                Push(TokenKind::NewLine, Begin);
            }

            void SkipComment()
            {
                while (!AtEnd() && Peek() != '\r' && Peek() != '\n')
                {
                    Advance();
                }
            }

            static bool IsWordBody(char Character)
            {
                const unsigned char Value = static_cast<unsigned char>(Character);
                return std::isalnum(Value) != 0 || Character == '_' || Character == '$' || Character == '#' ||
                       Character == '%' || Character == '&';
            }

            void LexWord()
            {
                const SourcePosition Begin = Position();
                if (Peek() == '#')
                {
                    Advance();
                }
                while (IsWordBody(Peek()))
                {
                    Advance();
                }

                const SourceSpan Span = MakeSpan(Begin, Position());
                if (CanStartComment_)
                {
                    const std::string Word = Canonical(Lexeme(Source_, Span));
                    if (Word == "REM")
                    {
                        SkipComment();
                        return;
                    }
                    if (Word == "REMSTART")
                    {
                        SkipBlockComment(Begin);
                        return;
                    }
                }
                Tokens_.push_back({TokenKind::Word, Span});
                CanStartComment_ = false;
            }

            // REMSTART hides everything up to and including the line that starts with REMEND.
            void SkipBlockComment(SourcePosition Begin)
            {
                SkipComment();
                while (!AtEnd())
                {
                    LexNewLine();
                    while (Peek() == ' ' || Peek() == '\t' || Peek() == '\f' || Peek() == '\v')
                    {
                        Advance();
                    }
                    std::size_t Length = 0;
                    while (IsWordBody(Peek(Length)))
                    {
                        ++Length;
                    }
                    const bool Closed =
                        Length == 6 && Canonical(std::string_view(Source_.Text).substr(Offset_, Length)) == "REMEND";
                    SkipComment();
                    if (Closed)
                    {
                        return;
                    }
                }
                AddError(Diagnostics_, "DBC1008", "REMSTART has no matching REMEND", MakeSpan(Begin, Position()));
            }

            void LexString()
            {
                const SourcePosition Begin = Position();
                Advance();
                bool Closed = false;
                while (!AtEnd() && Peek() != '\r' && Peek() != '\n')
                {
                    if (Peek() != '"')
                    {
                        Advance();
                        continue;
                    }
                    Advance();
                    if (Peek() == '"')
                    {
                        Advance();
                        continue;
                    }
                    Closed = true;
                    break;
                }
                Push(TokenKind::String, Begin);
                if (!Closed)
                {
                    AddError(Diagnostics_, "DBC1001", "Unterminated string literal", {Begin, Position()});
                }
            }

            void LexNumber()
            {
                const SourcePosition Begin = Position();
                if (Peek() == '%')
                {
                    Advance();
                    while (Peek() == '0' || Peek() == '1')
                    {
                        Advance();
                    }
                    Push(TokenKind::Integer, Begin);
                    if (Source_.Profile != LanguageProfile::DbPro)
                    {
                        AddError(Diagnostics_, "DBC1002", "Binary literals require the DBPro profile",
                                 {Begin, Position()});
                    }
                    return;
                }

                if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X' || Peek(1) == 'c' || Peek(1) == 'C'))
                {
                    const bool Hex = Peek(1) == 'x' || Peek(1) == 'X';
                    Advance();
                    Advance();
                    while (Hex ? std::isxdigit(static_cast<unsigned char>(Peek())) != 0
                               : (Peek() >= '0' && Peek() <= '7'))
                    {
                        Advance();
                    }
                    Push(TokenKind::Integer, Begin);
                    if (Source_.Profile != LanguageProfile::DbPro)
                    {
                        AddError(Diagnostics_, "DBC1003", "Radix literals require the DBPro profile",
                                 {Begin, Position()});
                    }
                    return;
                }

                while (std::isdigit(static_cast<unsigned char>(Peek())) != 0)
                {
                    Advance();
                }
                if (std::isalpha(static_cast<unsigned char>(Peek())) != 0 || Peek() == '_')
                {
                    while (IsWordBody(Peek()))
                    {
                        Advance();
                    }
                    Push(TokenKind::Word, Begin);
                    return;
                }

                bool Real = false;
                if (Peek() == '.')
                {
                    Real = true;
                    Advance();
                    while (std::isdigit(static_cast<unsigned char>(Peek())) != 0)
                    {
                        Advance();
                    }
                }
                if (Peek() == 'e' || Peek() == 'E')
                {
                    Real = true;
                    Advance();
                    if (Peek() == '+' || Peek() == '-')
                    {
                        Advance();
                    }
                    while (std::isdigit(static_cast<unsigned char>(Peek())) != 0)
                    {
                        Advance();
                    }
                }
                Push(Real ? TokenKind::Real : TokenKind::Integer, Begin);
            }

            void LexPunctuation()
            {
                const SourcePosition Begin = Position();
                const char Character = Advance();
                TokenKind Kind = TokenKind::Invalid;
                bool Diagnosed = false;
                switch (Character)
                {
                    case ':':
                        Kind = TokenKind::Colon;
                        CanStartComment_ = true;
                        break;
                    case ',':
                        Kind = TokenKind::Comma;
                        break;
                    case ';':
                        Kind = TokenKind::Semicolon;
                        break;
                    case '(':
                        Kind = TokenKind::LeftParen;
                        break;
                    case ')':
                        Kind = TokenKind::RightParen;
                        break;
                    case '+':
                        Kind = TokenKind::Plus;
                        break;
                    case '-':
                        Kind = TokenKind::Minus;
                        break;
                    case '*':
                        Kind = TokenKind::Star;
                        break;
                    case '/':
                        Kind = TokenKind::Slash;
                        break;
                    case '%':
                        if (Peek() == '%')
                        {
                            Advance();
                            Kind = TokenKind::Modulo;
                            RequireProfessional(Begin, "The '%%' operator requires the DBPro profile");
                        }
                        break;
                    case '^':
                        Kind = TokenKind::Power;
                        break;
                    case '=':
                        Kind = TokenKind::Equal;
                        break;
                    case '.':
                        if (Peek() == '.')
                        {
                            Advance();
                            AddError(Diagnostics_, "DBC1004", "The undocumented '..' operator is not supported",
                                     {Begin, Position()});
                            Kind = TokenKind::UnsupportedOperator;
                            Diagnosed = true;
                        }
                        else
                        {
                            Kind = TokenKind::Dot;
                            if (Source_.Profile != LanguageProfile::DbPro)
                            {
                                AddError(Diagnostics_, "DBC1005", "Member access requires the DBPro profile",
                                         {Begin, Position()});
                            }
                        }
                        break;
                    case '<':
                        if (Peek() == '=')
                        {
                            Advance();
                            Kind = TokenKind::LessEqual;
                        }
                        else if (Peek() == '>')
                        {
                            Advance();
                            Kind = TokenKind::NotEqual;
                        }
                        else if (Peek() == '<')
                        {
                            Advance();
                            Kind = TokenKind::ShiftLeft;
                            RequireProfessional(Begin, "Shift operators require the DBPro profile");
                        }
                        else
                        {
                            Kind = TokenKind::Less;
                        }
                        break;
                    case '>':
                        if (Peek() == '=')
                        {
                            Advance();
                            Kind = TokenKind::GreaterEqual;
                        }
                        else if (Peek() == '>')
                        {
                            Advance();
                            Kind = TokenKind::ShiftRight;
                            RequireProfessional(Begin, "Shift operators require the DBPro profile");
                        }
                        else
                        {
                            Kind = TokenKind::Greater;
                        }
                        break;
                    case '&':
                        if (Peek() == '&')
                        {
                            Advance();
                            Kind = TokenKind::BitAnd;
                            RequireProfessional(Begin, "Bitwise operators require the DBPro profile");
                        }
                        break;
                    case '|':
                        if (Peek() == '|')
                        {
                            Advance();
                            Kind = TokenKind::BitOr;
                            RequireProfessional(Begin, "Bitwise operators require the DBPro profile");
                        }
                        break;
                    case '~':
                        if (Peek() == '~')
                        {
                            Advance();
                            Kind = TokenKind::BitXor;
                            RequireProfessional(Begin, "Bitwise operators require the DBPro profile");
                        }
                        break;
                    default:
                        break;
                }

                Push(Kind, Begin);
                if (Kind == TokenKind::Invalid && !Diagnosed)
                {
                    AddError(Diagnostics_, "DBC1006", "Unexpected character in source", {Begin, Position()});
                }
                else if (Kind != TokenKind::Colon)
                {
                    CanStartComment_ = false;
                }
            }

            void RequireProfessional(SourcePosition Begin, std::string Message)
            {
                if (Source_.Profile != LanguageProfile::DbPro)
                {
                    AddError(Diagnostics_, "DBC1007", std::move(Message), {Begin, Position()});
                }
            }

            const SourceDocument& Source_;
            std::vector<Diagnostic>& Diagnostics_;
            std::vector<Token> Tokens_{};
            std::size_t Offset_ = 0;
            std::uint32_t Line_ = 1;
            std::uint32_t Column_ = 1;
            bool CanStartComment_ = true;
        };
    } // namespace

    std::vector<Token> Lex(const SourceDocument& Source, std::vector<Diagnostic>& Diagnostics)
    {
        return Lexer(Source, Diagnostics).Run();
    }
} // namespace dbc::language::detail
