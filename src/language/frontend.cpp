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

        Program ExpandIncludes(std::shared_ptr<const SourceDocument> Source, const IncludeLoader& Loader)
        {
            Program Root = LexAndParse(std::move(Source));
            // A parse error must not hide the includes: every name they declare would look unknown.
            if (!Root.Source)
                return Root;

            struct IncludeFrame
            {
                Program Value;
                std::size_t DirectIncludeCount = 0;
                std::size_t NextInclude = 0;
                std::size_t InsertedStatements = 0;
                bool IsActive = true;
            };

            // Include chains must not consume the Studio worker's native stack while a child is parsed.
            std::unordered_set<std::string> Active{SourceKey(*Root.Source)};
            std::vector<IncludeFrame> Frames;
            const std::size_t RootIncludeCount = Root.Includes.size();
            Frames.push_back({std::move(Root), RootIncludeCount});
            while (true)
            {
                IncludeFrame& Frame = Frames.back();
                Program& Result = Frame.Value;
                if (Frame.NextInclude == Frame.DirectIncludeCount)
                {
                    if (Frame.IsActive)
                        Active.erase(SourceKey(*Result.Source));
                    Program Child = std::move(Result);
                    Frames.pop_back();
                    if (Frames.empty())
                        return Child;

                    IncludeFrame& Parent = Frames.back();
                    Program& Destination = Parent.Value;
                    const IncludeDeclaration& Include = Destination.Includes[Parent.NextInclude - 1];
                    Destination.Diagnostics.insert(Destination.Diagnostics.end(),
                                                   std::make_move_iterator(Child.Diagnostics.begin()),
                                                   std::make_move_iterator(Child.Diagnostics.end()));
                    const std::size_t Position =
                        std::min(Include.StatementIndex + Parent.InsertedStatements, Destination.Statements.size());
                    const std::size_t ChildStatementCount = Child.Statements.size();
                    Destination.Statements.insert(Destination.Statements.begin() +
                                                      static_cast<std::ptrdiff_t>(Position),
                                                  std::make_move_iterator(Child.Statements.begin()),
                                                  std::make_move_iterator(Child.Statements.end()));
                    Parent.InsertedStatements += ChildStatementCount;
                    Destination.Functions.insert(Destination.Functions.end(),
                                                 std::make_move_iterator(Child.Functions.begin()),
                                                 std::make_move_iterator(Child.Functions.end()));
                    Destination.Types.insert(Destination.Types.end(), std::make_move_iterator(Child.Types.begin()),
                                             std::make_move_iterator(Child.Types.end()));
                    Destination.Constants.insert(Destination.Constants.end(),
                                                 std::make_move_iterator(Child.Constants.begin()),
                                                 std::make_move_iterator(Child.Constants.end()));
                    Destination.Includes.insert(Destination.Includes.end(),
                                                std::make_move_iterator(Child.Includes.begin()),
                                                std::make_move_iterator(Child.Includes.end()));
                    continue;
                }

                const IncludeDeclaration& Include = Result.Includes[Frame.NextInclude++];
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

                Program Child = LexAndParse(std::move(Included));
                const bool IsActive = Active.insert(SourceKey(*Child.Source)).second;
                if (!IsActive)
                {
                    detail::AddError(Child.Diagnostics, "DBC1225", "Cyclic #INCLUDE detected", {});
                    Child.Diagnostics.back().LogicalPath = Child.Source->LogicalPath;
                }
                const std::size_t ChildIncludeCount = IsActive ? Child.Includes.size() : 0;
                Frames.push_back({std::move(Child), ChildIncludeCount, 0, 0, IsActive});
            }
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
        Program Result = ExpandIncludes(std::move(Source), Loader);
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

    void AddWarning(std::vector<Diagnostic>& Diagnostics, std::string Code, std::string Message, SourceSpan Span)
    {
        Diagnostics.push_back({std::move(Code), DiagnosticSeverity::Warning, std::move(Message), Span, {}});
    }
} // namespace dbc::language::detail
