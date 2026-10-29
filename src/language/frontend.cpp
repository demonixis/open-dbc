#include "frontend_internal.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>
#include <unordered_set>

namespace dbc::language
{
    namespace
    {
        std::string SourceKey(const SourceDocument& Source)
        {
            return dbc::GenericPathToUtf8(Source.LogicalPath.lexically_normal());
        }

        void IncludeError(Program& Value, const IncludeDeclaration& Include, std::string Code, std::string Message)
        {
            Value.Diagnostics.push_back({std::move(Code),
                                         DiagnosticSeverity::Error,
                                         std::move(Message),
                                         Include.Span,
                                         {},
                                         Include.Span.LogicalPath});
        }

        Program ExpandIncludes(std::shared_ptr<const SourceDocument> Source, const IncludeLoader& Loader,
                               std::unordered_set<std::string>& Active)
        {
            Program Result = LexAndParse(std::move(Source));
            if (!Result.Source || Result.HasErrors())
                return Result;

            const std::string Key = SourceKey(*Result.Source);
            if (!Active.insert(Key).second)
            {
                detail::AddError(Result.Diagnostics, "DBC1225", "Cyclic #INCLUDE detected", {});
                Result.Diagnostics.back().LogicalPath = Result.Source->LogicalPath;
                return Result;
            }

            std::size_t InsertedStatements = 0;
            const std::vector<IncludeDeclaration> DirectIncludes = Result.Includes;
            for (const IncludeDeclaration& Include : DirectIncludes)
            {
                if (!Loader)
                {
                    IncludeError(Result, Include, "DBC1222", "#INCLUDE requires a source loader");
                    continue;
                }
                std::string Error;
                std::shared_ptr<const SourceDocument> Included = Loader(*Result.Source, Include.Path, Error);
                if (!Included)
                {
                    IncludeError(Result, Include, "DBC1223",
                                 Error.empty() ? "Failed to load #INCLUDE '" + dbc::PathToUtf8(Include.Path) + "'"
                                               : std::move(Error));
                    continue;
                }
                if (!IsLanguageSource(Included->LogicalPath) || Included->Profile != Result.Source->Profile)
                {
                    IncludeError(Result, Include, "DBC1224",
                                 IsLanguageSource(Included->LogicalPath)
                                     ? "#INCLUDE changed the immutable language profile"
                                     : "#INCLUDE source must use the .dba extension");
                    continue;
                }

                Program Child = ExpandIncludes(std::move(Included), Loader, Active);
                Result.Diagnostics.insert(Result.Diagnostics.end(), std::make_move_iterator(Child.Diagnostics.begin()),
                                          std::make_move_iterator(Child.Diagnostics.end()));
                const std::size_t Position =
                    std::min(Include.StatementIndex + InsertedStatements, Result.Statements.size());
                const std::size_t ChildStatementCount = Child.Statements.size();
                Result.Statements.insert(Result.Statements.begin() + static_cast<std::ptrdiff_t>(Position),
                                         std::make_move_iterator(Child.Statements.begin()),
                                         std::make_move_iterator(Child.Statements.end()));
                InsertedStatements += ChildStatementCount;
                Result.Functions.insert(Result.Functions.end(), std::make_move_iterator(Child.Functions.begin()),
                                        std::make_move_iterator(Child.Functions.end()));
                Result.Types.insert(Result.Types.end(), std::make_move_iterator(Child.Types.begin()),
                                    std::make_move_iterator(Child.Types.end()));
                Result.Constants.insert(Result.Constants.end(), std::make_move_iterator(Child.Constants.begin()),
                                        std::make_move_iterator(Child.Constants.end()));
                Result.Includes.insert(Result.Includes.end(), std::make_move_iterator(Child.Includes.begin()),
                                       std::make_move_iterator(Child.Includes.end()));
            }
            Active.erase(Key);
            return Result;
        }
    } // namespace

    bool Program::HasErrors() const
    {
        return std::any_of(Diagnostics.begin(), Diagnostics.end(),
                           [](const Diagnostic& Value) { return Value.Severity == DiagnosticSeverity::Error; });
    }

    std::string_view Lexeme(const SourceDocument& Source, const SourceSpan& Span)
    {
        if (Span.Begin.Offset > Span.End.Offset || Span.End.Offset > Source.Text.size())
        {
            return {};
        }
        return std::string_view(Source.Text).substr(Span.Begin.Offset, Span.End.Offset - Span.Begin.Offset);
    }

    std::string FormatDiagnostic(const SourceDocument& Source, const Diagnostic& Value)
    {
        std::ostringstream Output;
        const std::filesystem::path& Path = Value.LogicalPath.empty() ? Source.LogicalPath : Value.LogicalPath;
        Output << dbc::PathToUtf8(Path) << ':' << Value.Span.Begin.Line << ':' << Value.Span.Begin.Column << ": "
               << (Value.Severity == DiagnosticSeverity::Error ? "error" : "warning") << ' ' << Value.Code << ": "
               << Value.Message;
        return Output.str();
    }

    Program LexAndParse(std::shared_ptr<const SourceDocument> Source)
    {
        Program Result;
        Result.Source = std::move(Source);
        if (!Result.Source)
        {
            detail::AddError(Result.Diagnostics, "DBC1000", "A source document is required", {});
            return Result;
        }
        Result.Tokens = detail::Lex(*Result.Source, Result.Diagnostics);
        detail::Parse(Result);
        for (Diagnostic& Value : Result.Diagnostics)
        {
            if (Value.LogicalPath.empty())
                Value.LogicalPath = Result.Source->LogicalPath;
        }
        return Result;
    }

    void Bind(Program& Value, const FrontendOptions& Options)
    {
        if (!Value.Source)
        {
            detail::AddError(Value.Diagnostics, "DBC2000", "A parsed source document is required", {});
            return;
        }
        const std::size_t DiagnosticBegin = Value.Diagnostics.size();
        detail::BindProgram(Value, Options);
        for (std::size_t Index = DiagnosticBegin; Index < Value.Diagnostics.size(); ++Index)
        {
            if (Value.Diagnostics[Index].LogicalPath.empty())
                Value.Diagnostics[Index].LogicalPath = Value.Diagnostics[Index].Span.LogicalPath.empty()
                                                           ? Value.Source->LogicalPath
                                                           : Value.Diagnostics[Index].Span.LogicalPath;
        }
        Value.IsBound = true;
    }

    Program Compile(std::shared_ptr<const SourceDocument> Source, const FrontendOptions& Options,
                    const IncludeLoader& Loader)
    {
        std::unordered_set<std::string> Active;
        Program Result = ExpandIncludes(std::move(Source), Loader, Active);
        Bind(Result, Options);
        std::stable_sort(Result.Diagnostics.begin(), Result.Diagnostics.end(),
                         [](const Diagnostic& Left, const Diagnostic& Right)
                         {
                             if (Left.LogicalPath != Right.LogicalPath)
                                 return dbc::GenericPathToUtf8(Left.LogicalPath) <
                                        dbc::GenericPathToUtf8(Right.LogicalPath);
                             if (Left.Span.Begin.Offset != Right.Span.Begin.Offset)
                             {
                                 return Left.Span.Begin.Offset < Right.Span.Begin.Offset;
                             }
                             return Left.Code < Right.Code;
                         });
        return Result;
    }
} // namespace dbc::language

namespace dbc::language::detail
{

    std::string TokenText(const SourceDocument& Source, const Token& Value)
    {
        return std::string(Lexeme(Source, Value.Span));
    }

    bool IsWord(const SourceDocument& Source, const Token& Value, std::string_view Expected)
    {
        return Value.Kind == TokenKind::Word && Canonical(Lexeme(Source, Value.Span)) == Expected;
    }

    SourceSpan Merge(SourceSpan First, SourceSpan Last)
    {
        return {First.Begin, Last.End, First.LogicalPath.empty() ? Last.LogicalPath : First.LogicalPath};
    }

    void AddError(std::vector<Diagnostic>& Diagnostics, std::string Code, std::string Message, SourceSpan Span)
    {
        Diagnostics.push_back({std::move(Code), DiagnosticSeverity::Error, std::move(Message), Span, {}});
    }
} // namespace dbc::language::detail
