#pragma once

#include "language_catalogue.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dbc::language
{
    // Statement blocks and expression operands share this nesting budget. The parser, binder, compiler and AST
    // destructors recurse once per level, so the limit keeps them within a 512 KB thread stack.
    inline constexpr std::size_t KMaxNestingDepth = 100;

    struct SourcePosition
    {
        std::size_t Offset = 0;
        std::uint32_t Line = 1;
        std::uint32_t Column = 1;
    };

    struct SourceSpan
    {
        SourcePosition Begin{};
        SourcePosition End{};
        std::filesystem::path LogicalPath{};
    };

    struct SourceDocument
    {
        std::filesystem::path LogicalPath{};
        std::string Text{};
        LanguageProfile Profile = LanguageProfile::Dbc;
    };

    enum class DiagnosticSeverity : std::uint8_t
    {
        Error,
        Warning,
    };

    struct DiagnosticNote
    {
        std::string Message{};
        SourceSpan Span{};
    };

    struct Diagnostic
    {
        std::string Code{};
        DiagnosticSeverity Severity = DiagnosticSeverity::Error;
        std::string Message{};
        SourceSpan Span{};
        std::vector<DiagnosticNote> Notes{};
        std::filesystem::path LogicalPath{};
    };

    enum class TokenKind : std::uint8_t
    {
        Word,
        Integer,
        Real,
        String,
        NewLine,
        Colon,
        Comma,
        Semicolon,
        LeftParen,
        RightParen,
        Dot,
        Plus,
        Minus,
        Star,
        Slash,
        Modulo,
        Power,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        ShiftLeft,
        ShiftRight,
        BitAnd,
        BitOr,
        BitXor,
        UnsupportedOperator,
        End,
        Invalid,
    };

    struct Token
    {
        TokenKind Kind = TokenKind::Invalid;
        SourceSpan Span{};
    };

    enum class TypeKind : std::uint8_t
    {
        Error,
        Unresolved,
        Void,
        Boolean,
        Integer,
        Real,
        String,
        UserDefined,
    };

    struct Type
    {
        TypeKind Kind = TypeKind::Unresolved;
        std::string Name{};

        bool IsNumeric() const;
        bool operator==(const Type& Other) const;
    };

    enum class ExpressionKind : std::uint8_t
    {
        Error,
        IntegerLiteral,
        RealLiteral,
        StringLiteral,
        Name,
        Unary,
        Binary,
        CallOrIndex,
        Call,
        Index,
        Member,
    };

    enum class ReferenceKind : std::uint8_t
    {
        None,
        Variable,
        Constant,
        UserFunction,
        BuiltinFunction,
        Array,
        Field,
    };

    struct Expression
    {
        ExpressionKind Kind = ExpressionKind::Error;
        SourceSpan Span{};
        Type ResolvedType{};
        std::string Text{};
        double Number = 0.0;
        std::size_t ArrayRank = 0;
        ReferenceKind Reference = ReferenceKind::None;
        std::optional<SymbolId> Builtin{};
        std::optional<OverloadId> Overload{};
        std::vector<std::unique_ptr<Expression>> Children{};
    };

    enum class StorageClass : std::uint8_t
    {
        Implicit,
        Dim,
        Local,
        Global,
        Parameter,
    };

    enum class StatementKind : std::uint8_t
    {
        Error,
        Command,
        Expression,
        Assignment,
        Declaration,
        For,
        While,
        Repeat,
        Do,
        If,
        Select,
        Goto,
        Gosub,
        Return,
        Exit,
        ExitFunction,
        End,
        Data,
        Label,
    };

    struct CaseClause;

    struct Statement
    {
        StatementKind Kind = StatementKind::Error;
        SourceSpan Span{};
        std::string Name{};
        StorageClass Storage = StorageClass::Implicit;
        Type DeclaredType{};
        std::size_t ArrayRank = 0;
        bool HasExplicitType = false;
        bool ContinuesPrint = false;
        std::optional<SymbolId> Builtin{};
        std::optional<OverloadId> Overload{};
        std::vector<std::unique_ptr<Expression>> Expressions{};
        std::vector<Statement> Body{};
        std::vector<Statement> ElseBody{};
        std::vector<CaseClause> Cases{};
    };

    struct CaseClause
    {
        SourceSpan Span{};
        bool IsDefault = false;
        std::vector<std::unique_ptr<Expression>> Values{};
        std::vector<Statement> Body{};
    };

    struct ParameterDeclaration
    {
        SourceSpan Span{};
        std::string Name{};
        Type DeclaredType{};
        bool HasExplicitType = false;
    };

    struct FunctionDeclaration
    {
        SourceSpan Span{};
        std::string Name{};
        Type ReturnType{};
        std::vector<ParameterDeclaration> Parameters{};
        std::vector<Statement> Body{};
        std::unique_ptr<Expression> FinalReturn{};
        // Scalars first used inside the body without a declaration; they are local to the function.
        std::vector<std::pair<std::string, Type>> ImplicitLocals{};
    };

    struct FieldDeclaration
    {
        SourceSpan Span{};
        std::string Name{};
        Type DeclaredType{};
    };

    struct TypeDeclaration
    {
        SourceSpan Span{};
        std::string Name{};
        std::vector<FieldDeclaration> Fields{};
    };

    struct ConstantDeclaration
    {
        SourceSpan Span{};
        std::string Name{};
        std::unique_ptr<Expression> Value{};
    };

    struct IncludeDeclaration
    {
        SourceSpan Span{};
        std::filesystem::path Path{};
        std::size_t StatementIndex = 0;
    };

    struct Program
    {
        std::shared_ptr<const SourceDocument> Source{};
        std::vector<Token> Tokens{};
        std::vector<Statement> Statements{};
        std::vector<FunctionDeclaration> Functions{};
        std::vector<TypeDeclaration> Types{};
        std::vector<ConstantDeclaration> Constants{};
        std::vector<IncludeDeclaration> Includes{};
        std::vector<Diagnostic> Diagnostics{};
        bool IsBound = false;

        bool HasErrors() const;
    };

    struct FrontendOptions
    {
        bool RejectUnsupported = true;
    };

    using IncludeLoader = std::function<std::shared_ptr<const SourceDocument>(
        const SourceDocument&, const std::filesystem::path&, std::string&)>;

    std::string_view Lexeme(const SourceDocument& Source, const SourceSpan& Span);
    std::string FormatDiagnostic(const SourceDocument& Source, const Diagnostic& Value);
    Program LexAndParse(std::shared_ptr<const SourceDocument> Source);
    void Bind(Program& Value, const FrontendOptions& Options = {});
    Program Compile(std::shared_ptr<const SourceDocument> Source, const FrontendOptions& Options = {},
                    const IncludeLoader& Loader = {});
} // namespace dbc::language
