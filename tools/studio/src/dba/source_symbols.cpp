#include "source_symbols.hpp"

#include "code_outline.hpp"
#include "language/frontend.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <unordered_set>

namespace dbc::studio
{
    namespace
    {
        namespace lang = dbc::language;

        // Included files are followed this deep at most; the frontend's own limit is far higher, but
        // completion only needs the names.
        constexpr std::size_t KMaxIncludedFiles = 256;

        class SymbolBuilder
        {
          public:
            explicit SymbolBuilder(const lang::SourceDocument& Source) : Source_(Source) {}

            dba::SourceSymbols Build(const lang::Program& Program)
            {
                for (const lang::TypeDeclaration& Type : Program.Types)
                    TypeNames_.emplace_back(Type.Name, Spelling(Type.Span, Type.Name));
                for (const lang::TypeDeclaration& Type : Program.Types)
                {
                    dba::SourceType Result{Spelling(Type.Span, Type.Name), Type.Name, {}};
                    for (const lang::FieldDeclaration& Field : Type.Fields)
                        Result.Fields.push_back(Variable(Field.Name, Field.Span, Field.DeclaredType, true, false));
                    Symbols_.Types.push_back(std::move(Result));
                }
                for (const lang::ConstantDeclaration& Constant : Program.Constants)
                    Symbols_.Constants.push_back(Spelling(Constant.Span, Constant.Name));
                for (const lang::IncludeDeclaration& Include : Program.Includes)
                    Symbols_.Includes.push_back(Include.Path);

                // What every scope sees comes first, so that a function assigning a global does not
                // list it among its locals.
                CollectShared(Program.Statements);
                for (const lang::FunctionDeclaration& Function : Program.Functions)
                    CollectShared(Function.Body);

                CollectLabels(Program.Statements);
                Collect(Program.Statements, Symbols_.Main, MainNames_);
                for (const lang::FunctionDeclaration& Function : Program.Functions)
                {
                    CollectLabels(Function.Body);
                    dba::SourceFunction Result;
                    Result.Name = Spelling(Function.Span, Function.Name);
                    Result.FirstLine = Function.Span.Begin.Line;
                    Result.LastLine = std::max(Function.Span.End.Line, LastLine(Function.Body, Result.FirstLine));
                    if (Function.FinalReturn)
                        Result.LastLine = std::max(Result.LastLine, Function.FinalReturn->Span.End.Line);
                    std::unordered_set<std::string> Names;
                    for (const lang::ParameterDeclaration& Parameter : Function.Parameters)
                    {
                        dba::SourceVariable Local = Variable(Parameter.Name, Parameter.Span, Parameter.DeclaredType,
                                                             Parameter.HasExplicitType, false);
                        Result.Parameters.push_back(Parameter.HasExplicitType ? Local.Name + " as " + Local.Type
                                                                              : Local.Name);
                        Names.insert(Parameter.Name);
                        Result.Locals.push_back(std::move(Local));
                    }
                    Collect(Function.Body, Result.Locals, Names);
                    Symbols_.Functions.push_back(std::move(Result));
                }
                return std::move(Symbols_);
            }

          private:
            std::string Spelling(const lang::SourceSpan& Span, const std::string& Canonical) const
            {
                return dba::DeclaredSpelling(Source_, Span, Canonical);
            }

            dba::SourceVariable Variable(const std::string& Canonical, const lang::SourceSpan& Span,
                                         const lang::Type& Declared, bool Explicit, bool Array) const
            {
                dba::SourceVariable Result;
                Result.Name = Spelling(Span, Canonical);
                Result.Array = Array;
                lang::TypeKind Kind = Declared.Kind;
                if (!Explicit)
                {
                    // An undeclared name takes its type from its suffix.
                    Kind = Canonical.ends_with('$')   ? lang::TypeKind::String
                           : Canonical.ends_with('#') ? lang::TypeKind::Real
                                                      : lang::TypeKind::Integer;
                }
                switch (Kind)
                {
                    case lang::TypeKind::String:
                        Result.Type = "string";
                        break;
                    case lang::TypeKind::Real:
                        Result.Type = "real";
                        break;
                    case lang::TypeKind::Boolean:
                        Result.Type = "boolean";
                        break;
                    case lang::TypeKind::UserDefined:
                    {
                        Result.UserType = Declared.Name;
                        const auto Known = std::find_if(TypeNames_.begin(), TypeNames_.end(),
                                                        [&](const auto& Pair) { return Pair.first == Declared.Name; });
                        Result.Type = Known != TypeNames_.end() ? Known->second : Declared.Name;
                        break;
                    }
                    default:
                        Result.Type = "integer";
                        break;
                }
                return Result;
            }

            static std::uint32_t LastLine(const std::vector<lang::Statement>& Statements, std::uint32_t Line)
            {
                for (const lang::Statement& Value : Statements)
                {
                    Line = std::max({Line, Value.Span.End.Line, LastLine(Value.Body, Line),
                                     LastLine(Value.ElseBody, Line)});
                    for (const lang::CaseClause& Clause : Value.Cases)
                        Line = std::max(Line, LastLine(Clause.Body, Line));
                }
                return Line;
            }

            template <typename Visitor> static void ForEach(const std::vector<lang::Statement>& Statements, Visitor&& Visit)
            {
                for (const lang::Statement& Value : Statements)
                {
                    Visit(Value);
                    ForEach(Value.Body, Visit);
                    ForEach(Value.ElseBody, Visit);
                    for (const lang::CaseClause& Clause : Value.Cases)
                        ForEach(Clause.Body, Visit);
                }
            }

            // GLOBAL declarations and arrays: a DIM is global wherever it is written.
            void CollectShared(const std::vector<lang::Statement>& Statements)
            {
                ForEach(Statements, [this](const lang::Statement& Value)
                        {
                            if (Value.Kind != lang::StatementKind::Declaration)
                                return;
                            const bool Array = Value.ArrayRank > 0;
                            const bool Shared = Value.Storage == lang::StorageClass::Global ||
                                                (Array && Value.Storage != lang::StorageClass::Local);
                            if (Shared && SharedNames_.insert(Key(Value.Name, Array)).second)
                                Symbols_.Shared.push_back(
                                    Variable(Value.Name, Value.Span, Value.DeclaredType, Value.HasExplicitType, Array));
                        });
            }

            void CollectLabels(const std::vector<lang::Statement>& Statements)
            {
                ForEach(Statements, [this](const lang::Statement& Value)
                        {
                            if (Value.Kind == lang::StatementKind::Label && LabelNames_.insert(Value.Name).second)
                                Symbols_.Labels.push_back(Spelling(Value.Span, Value.Name));
                        });
            }

            // Arrays and scalars are distinct names.
            static std::string Key(const std::string& Canonical, bool Array)
            {
                return Array ? Canonical + "()" : Canonical;
            }

            // The variables of one scope: what it declares, first assigns or counts with.
            void Collect(const std::vector<lang::Statement>& Statements, std::vector<dba::SourceVariable>& Into,
                         std::unordered_set<std::string>& Names)
            {
                const auto Add = [&](const std::string& Canonical, const lang::SourceSpan& Span,
                                     const lang::Type& Declared, bool Explicit, bool Array)
                {
                    const std::string Name = Key(Canonical, Array);
                    if (Canonical.empty() || SharedNames_.contains(Name) || !Names.insert(Name).second)
                        return;
                    Into.push_back(Variable(Canonical, Span, Declared, Explicit, Array));
                };
                ForEach(Statements, [&](const lang::Statement& Value)
                        {
                            switch (Value.Kind)
                            {
                                case lang::StatementKind::Declaration:
                                    Add(Value.Name, Value.Span, Value.DeclaredType, Value.HasExplicitType,
                                        Value.ArrayRank > 0);
                                    break;
                                case lang::StatementKind::For:
                                    Add(Value.Name, Value.Span, {}, false, false);
                                    break;
                                case lang::StatementKind::Assignment:
                                {
                                    // `a = 1` declares a; `a.b = 1` and `a(i) = 1` need a declared a.
                                    const lang::Expression* Target =
                                        Value.Expressions.empty() ? nullptr : Value.Expressions.front().get();
                                    if (Target != nullptr && Target->Kind == lang::ExpressionKind::Name)
                                        Add(Target->Text, Target->Span, {}, false, false);
                                    break;
                                }
                                default:
                                    break;
                            }
                        });
            }

            const lang::SourceDocument& Source_;
            dba::SourceSymbols Symbols_;
            std::vector<std::pair<std::string, std::string>> TypeNames_;
            std::unordered_set<std::string> SharedNames_;
            std::unordered_set<std::string> MainNames_;
            std::unordered_set<std::string> LabelNames_;
        };

        template <typename T> void Append(std::vector<T>& Into, std::vector<T>&& From)
        {
            Into.insert(Into.end(), std::make_move_iterator(From.begin()), std::make_move_iterator(From.end()));
        }
    } // namespace

    namespace dba
    {
        const SourceFunction* SourceSymbols::FunctionAt(std::uint32_t Line) const
        {
            for (const SourceFunction& Function : Functions)
                if (Function.FirstLine != 0 && Line >= Function.FirstLine && Line <= Function.LastLine)
                    return &Function;
            return nullptr;
        }

        const SourceType* SourceSymbols::FindType(std::string_view Key) const
        {
            const auto Found =
                std::find_if(Types.begin(), Types.end(), [&](const SourceType& Type) { return Type.Key == Key; });
            return Found == Types.end() ? nullptr : &*Found;
        }

        SourceSymbols BuildSourceSymbols(const std::filesystem::path& LogicalPath, std::string Text,
                                         dbc::language::LanguageProfile Profile)
        {
            auto Source = std::make_shared<lang::SourceDocument>();
            Source->LogicalPath = LogicalPath;
            Source->Text = std::move(Text);
            Source->Profile = Profile;
            const lang::Program Program = lang::LexAndParse(Source);
            return SymbolBuilder(*Source).Build(Program);
        }

        SourceSymbols BuildProgramSymbols(const std::filesystem::path& Path, std::string Text,
                                          dbc::language::LanguageProfile Profile, const SourceReader& Read)
        {
            SourceSymbols Result = BuildSourceSymbols(Path, std::move(Text), Profile);
            if (!Read)
                return Result;
            std::set<std::filesystem::path> Seen{Path.lexically_normal()};
            // Each pending include with the folder of the file that names it.
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> Pending;
            for (const std::filesystem::path& Include : Result.Includes)
                Pending.emplace_back(Path.parent_path(), Include);
            for (std::size_t Index = 0; Index < Pending.size() && Seen.size() < KMaxIncludedFiles; ++Index)
            {
                // BASIC sources write folders with either slash.
                std::string Written = Pending[Index].second.generic_string();
                std::replace(Written.begin(), Written.end(), '\\', '/');
                const std::filesystem::path File = (Pending[Index].first / Written).lexically_normal();
                if (!Seen.insert(File).second)
                    continue;
                std::optional<std::string> Included = Read(File);
                if (!Included)
                    continue;
                SourceSymbols Other = BuildSourceSymbols(File, std::move(*Included), Profile);
                for (const std::filesystem::path& Include : Other.Includes)
                    Pending.emplace_back(File.parent_path(), Include);
                for (SourceFunction& Function : Other.Functions)
                    Function.FirstLine = Function.LastLine = 0;
                Append(Result.Functions, std::move(Other.Functions));
                Append(Result.Types, std::move(Other.Types));
                Append(Result.Shared, std::move(Other.Shared));
                Append(Result.Main, std::move(Other.Main));
                Append(Result.Labels, std::move(Other.Labels));
                Append(Result.Constants, std::move(Other.Constants));
            }
            return Result;
        }
    } // namespace dba

} // namespace dbc::studio
