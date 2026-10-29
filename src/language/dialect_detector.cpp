#include "dialect_detector.hpp"
#include "path_utf8.hpp"

#include "frontend.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <string_view>
#include <tuple>

namespace dbc::language
{
    namespace
    {
        using EvidenceKey = std::tuple<LanguageProfile, DialectEvidenceKind, std::string, std::uint32_t, std::string>;

        void AppendEvidence(std::vector<DialectEvidence>& Evidence, std::set<EvidenceKey>& Seen,
                            LanguageProfile Profile, DialectEvidenceKind Kind, const SourceSpan& Span,
                            std::string Detail, const std::filesystem::path& FallbackPath)
        {
            const std::filesystem::path Path = Span.LogicalPath.empty() ? FallbackPath : Span.LogicalPath;
            EvidenceKey Key{Profile, Kind, dbc::GenericPathToUtf8(Path), Span.Begin.Line, Detail};
            if (Seen.insert(Key).second)
                Evidence.push_back({Profile, Kind, Path, Span.Begin.Line, std::move(Detail)});
        }

        void InspectBuiltin(SymbolId Symbol, std::size_t ArgumentCount, const SourceSpan& Span,
                            const std::filesystem::path& Path, std::vector<DialectEvidence>& Evidence,
                            std::set<EvidenceKey>& Seen)
        {
            const SymbolDescriptor& Descriptor = language::Symbol(Symbol);
            const bool DbcAvailable = AvailableIn(Descriptor, LanguageProfile::Dbc);
            const bool DbProAvailable = AvailableIn(Descriptor, LanguageProfile::DbPro);
            if (DbcAvailable != DbProAvailable)
            {
                const LanguageProfile Profile = DbcAvailable ? LanguageProfile::Dbc : LanguageProfile::DbPro;
                AppendEvidence(Evidence, Seen, Profile, DialectEvidenceKind::Symbol, Span,
                               std::string(Descriptor.CanonicalName), Path);
                return;
            }
            if (!DbcAvailable)
                return;
            const std::optional<Arity> DbcArity = ArityFor(Descriptor, LanguageProfile::Dbc);
            const std::optional<Arity> DbProArity = ArityFor(Descriptor, LanguageProfile::DbPro);
            const bool DbcAccepts = DbcArity && DbcArity->Accepts(static_cast<int>(ArgumentCount));
            const bool DbProAccepts = DbProArity && DbProArity->Accepts(static_cast<int>(ArgumentCount));
            if (DbcAccepts != DbProAccepts)
            {
                const LanguageProfile Profile = DbcAccepts ? LanguageProfile::Dbc : LanguageProfile::DbPro;
                AppendEvidence(Evidence, Seen, Profile, DialectEvidenceKind::Arity, Span,
                               std::string(Descriptor.CanonicalName) + " with " + std::to_string(ArgumentCount) +
                                   " argument(s)",
                               Path);
            }
        }

        void InspectExpression(const Expression& Value, const std::filesystem::path& Path,
                               std::vector<DialectEvidence>& Evidence, std::set<EvidenceKey>& Seen)
        {
            if (Value.Kind == ExpressionKind::Member)
            {
                AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar, Value.Span,
                               "member access", Path);
            }
            if (Value.Builtin)
                InspectBuiltin(*Value.Builtin, Value.Children.size(), Value.Span, Path, Evidence, Seen);
            for (const std::unique_ptr<Expression>& Child : Value.Children)
                InspectExpression(*Child, Path, Evidence, Seen);
        }

        void InspectStatement(const Statement& Value, const std::filesystem::path& Path,
                              std::vector<DialectEvidence>& Evidence, std::set<EvidenceKey>& Seen)
        {
            if (Value.Kind == StatementKind::Declaration &&
                (Value.Storage == StorageClass::Local || Value.Storage == StorageClass::Global ||
                 Value.HasExplicitType))
            {
                AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar, Value.Span,
                               "typed or scoped declaration", Path);
            }
            if (Value.Builtin)
                InspectBuiltin(*Value.Builtin, Value.Expressions.size(), Value.Span, Path, Evidence, Seen);
            for (const std::unique_ptr<Expression>& Expression : Value.Expressions)
                InspectExpression(*Expression, Path, Evidence, Seen);
            for (const Statement& Child : Value.Body)
                InspectStatement(Child, Path, Evidence, Seen);
            for (const Statement& Child : Value.ElseBody)
                InspectStatement(Child, Path, Evidence, Seen);
            for (const CaseClause& Clause : Value.Cases)
            {
                for (const std::unique_ptr<Expression>& Expression : Clause.Values)
                    InspectExpression(*Expression, Path, Evidence, Seen);
                for (const Statement& Child : Clause.Body)
                    InspectStatement(Child, Path, Evidence, Seen);
            }
        }

        bool IsProfessionalGrammarDiagnostic(const Diagnostic& Value)
        {
            return Value.Message.find("requires the DBPro profile") != std::string::npos;
        }

        void InspectProgram(const Program& ProgramValue, LanguageProfile CompiledProfile,
                            std::vector<DialectEvidence>& Evidence, std::set<EvidenceKey>& Seen)
        {
            const std::filesystem::path Path =
                ProgramValue.Source ? ProgramValue.Source->LogicalPath : std::filesystem::path{};
            for (const Diagnostic& Diagnostic : ProgramValue.Diagnostics)
            {
                if (CompiledProfile == LanguageProfile::Dbc && IsProfessionalGrammarDiagnostic(Diagnostic))
                {
                    AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar,
                                   Diagnostic.Span, Diagnostic.Message, Path);
                }
                else if ((Diagnostic.Code == "DBC2014" || Diagnostic.Code == "DBC2036") &&
                         CompiledProfile == LanguageProfile::DbPro)
                {
                    AppendEvidence(Evidence, Seen, LanguageProfile::Dbc, DialectEvidenceKind::Symbol, Diagnostic.Span,
                                   Diagnostic.Message, Path);
                }
                else if ((Diagnostic.Code == "DBC2014" || Diagnostic.Code == "DBC2036") &&
                         CompiledProfile == LanguageProfile::Dbc)
                {
                    AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Symbol, Diagnostic.Span,
                                   Diagnostic.Message, Path);
                }
            }
            for (const Statement& StatementValue : ProgramValue.Statements)
                InspectStatement(StatementValue, Path, Evidence, Seen);
            if (CompiledProfile == LanguageProfile::DbPro)
            {
                for (const TypeDeclaration& Declaration : ProgramValue.Types)
                {
                    AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar,
                                   Declaration.Span, "TYPE declaration", Path);
                }
                for (const ConstantDeclaration& Declaration : ProgramValue.Constants)
                {
                    AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar,
                                   Declaration.Span, "#CONSTANT declaration", Path);
                }
                for (const Token& TokenValue : ProgramValue.Tokens)
                {
                    if (TokenValue.Kind == TokenKind::ShiftLeft || TokenValue.Kind == TokenKind::ShiftRight ||
                        TokenValue.Kind == TokenKind::BitAnd || TokenValue.Kind == TokenKind::BitOr ||
                        TokenValue.Kind == TokenKind::BitXor)
                    {
                        AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar,
                                       TokenValue.Span, "DBPro operator", Path);
                    }
                }
            }
            for (const FunctionDeclaration& Function : ProgramValue.Functions)
            {
                if (CompiledProfile == LanguageProfile::DbPro)
                {
                    for (const ParameterDeclaration& Parameter : Function.Parameters)
                    {
                        if (Parameter.HasExplicitType)
                        {
                            AppendEvidence(Evidence, Seen, LanguageProfile::DbPro, DialectEvidenceKind::Grammar,
                                           Parameter.Span, "typed FUNCTION parameter", Path);
                        }
                    }
                }
                for (const Statement& StatementValue : Function.Body)
                    InspectStatement(StatementValue, Path, Evidence, Seen);
                if (Function.FinalReturn)
                    InspectExpression(*Function.FinalReturn, Path, Evidence, Seen);
            }
        }

        DialectDetection Finish(std::vector<DialectEvidence> Evidence)
        {
            const bool HasDbc = std::any_of(Evidence.begin(), Evidence.end(), [](const DialectEvidence& Value)
                                            { return Value.Profile == LanguageProfile::Dbc; });
            const bool HasDbPro = std::any_of(Evidence.begin(), Evidence.end(), [](const DialectEvidence& Value)
                                              { return Value.Profile == LanguageProfile::DbPro; });
            DialectDetectionKind Kind = DialectDetectionKind::Ambiguous;
            if (HasDbc && HasDbPro)
                Kind = DialectDetectionKind::Conflict;
            else if (HasDbc)
                Kind = DialectDetectionKind::Dbc;
            else if (HasDbPro)
                Kind = DialectDetectionKind::DbPro;
            return {Kind, std::move(Evidence)};
        }
    } // namespace

    std::optional<LanguageProfile> DialectDetection::Profile() const
    {
        if (Kind == DialectDetectionKind::Dbc)
            return LanguageProfile::Dbc;
        if (Kind == DialectDetectionKind::DbPro)
            return LanguageProfile::DbPro;
        return std::nullopt;
    }

    DialectDetection DetectDialect(const DialectSource& Source)
    {
        std::vector<DialectEvidence> Evidence;
        std::set<EvidenceKey> Seen;
        for (const LanguageProfile Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
        {
            const auto Document =
                std::make_shared<const SourceDocument>(SourceDocument{Source.LogicalPath, Source.Text, Profile});
            Program ProgramValue = Compile(Document, {.RejectUnsupported = false});
            InspectProgram(ProgramValue, Profile, Evidence, Seen);
        }
        return Finish(std::move(Evidence));
    }

    DialectDetection DetectDialect(std::span<const DialectSource> Sources)
    {
        std::vector<DialectEvidence> Evidence;
        std::set<EvidenceKey> Seen;
        for (const DialectSource& Source : Sources)
        {
            DialectDetection Current = DetectDialect(Source);
            for (DialectEvidence& Value : Current.Evidence)
            {
                EvidenceKey Key{Value.Profile, Value.Kind, dbc::GenericPathToUtf8(Value.LogicalPath), Value.Line,
                                Value.Detail};
                if (Seen.insert(Key).second)
                    Evidence.push_back(std::move(Value));
            }
        }
        return Finish(std::move(Evidence));
    }
} // namespace dbc::language
