#include "code_outline.hpp"

#include "language/frontend.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_map>

namespace dbc::studio
{
    namespace
    {
        namespace lang = dbc::language;

        std::string Upper(std::string_view Value)
        {
            std::string Result(Value);
            std::transform(Result.begin(), Result.end(), Result.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
            return Result;
        }

        // The parser stores canonical upper-case names; the outline shows the
        // spelling found in the declaration itself.
        std::string Spelling(const lang::SourceDocument& Source, const lang::SourceSpan& Span,
                             const std::string& Canonical)
        {
            const auto IsNameCharacter = [](char Character)
            {
                return std::isalnum(static_cast<unsigned char>(Character)) != 0 || Character == '_' ||
                       Character == '#' || Character == '$';
            };
            const std::string_view Text = lang::Lexeme(Source, Span);
            const std::string UpperText = Upper(Text);
            // Whole words only: `a` must not match the A of `local a`.
            for (std::size_t At = UpperText.find(Canonical); At != std::string::npos;
                 At = UpperText.find(Canonical, At + 1))
            {
                const std::size_t End = At + Canonical.size();
                if ((At == 0 || !IsNameCharacter(Text[At - 1])) && (End == Text.size() || !IsNameCharacter(Text[End])))
                    return std::string(Text.substr(At, Canonical.size()));
            }
            return Canonical;
        }

        class OutlineBuilder
        {
          public:
            explicit OutlineBuilder(const lang::SourceDocument& Source) : Source_(Source) {}

            void Collect(const lang::Program& Program)
            {
                for (const lang::FunctionDeclaration& Function : Program.Functions)
                {
                    Functions_.emplace(Function.Name, Outline_.Functions.size());
                    Outline_.Functions.push_back(
                        {Spelling(Source_, Function.Span, Function.Name), Function.Span.Begin.Line});
                }
                for (const lang::TypeDeclaration& Type : Program.Types)
                {
                    Outline_.Types.push_back({Spelling(Source_, Type.Span, Type.Name), Type.Span.Begin.Line,
                                              static_cast<int>(Type.Fields.size())});
                }
                // Labels first, so jumps written before their label still count.
                CollectLabels(Program.Statements);
                for (const lang::FunctionDeclaration& Function : Program.Functions)
                    CollectLabels(Function.Body);

                Walk(Program.Statements, true);
                for (const lang::FunctionDeclaration& Function : Program.Functions)
                {
                    Walk(Function.Body, false);
                    Visit(Function.FinalReturn.get());
                }
            }

            dba::CodeOutline Take()
            {
                return std::move(Outline_);
            }

          private:
            void CollectLabels(const std::vector<lang::Statement>& Statements)
            {
                for (const lang::Statement& Value : Statements)
                {
                    if (Value.Kind == lang::StatementKind::Label && !Labels_.contains(Value.Name))
                    {
                        Labels_.emplace(Value.Name, Outline_.Labels.size());
                        Outline_.Labels.push_back({Spelling(Source_, Value.Span, Value.Name), Value.Span.Begin.Line});
                    }
                    CollectLabels(Value.Body);
                    CollectLabels(Value.ElseBody);
                    for (const lang::CaseClause& Clause : Value.Cases)
                        CollectLabels(Clause.Body);
                }
            }

            void AddVariable(const std::string& Canonical, const lang::SourceSpan& Span, bool Array)
            {
                if (Canonical.empty() || Variables_.contains(Canonical))
                    return;
                Variables_.emplace(Canonical, Outline_.Variables.size());
                Outline_.Variables.push_back({Spelling(Source_, Span, Canonical), Span.Begin.Line, 0, Array});
            }

            // The variable an assignment writes: `a`, `a(i)` or the root of `a.b.c`.
            void AddAssignedVariable(const lang::Expression* Target)
            {
                while (Target != nullptr && Target->Kind == lang::ExpressionKind::Member && !Target->Children.empty())
                    Target = Target->Children.front().get();
                if (Target == nullptr)
                    return;
                if (Target->Kind == lang::ExpressionKind::Name)
                    AddVariable(Target->Text, Target->Span, false);
                else if (Target->Kind == lang::ExpressionKind::CallOrIndex ||
                         Target->Kind == lang::ExpressionKind::Index)
                    AddVariable(Target->Text, Target->Span, true);
            }

            void Walk(const std::vector<lang::Statement>& Statements, bool MainProgram)
            {
                for (const lang::Statement& Value : Statements)
                {
                    switch (Value.Kind)
                    {
                        case lang::StatementKind::Declaration:
                            if (MainProgram || Value.Storage == lang::StorageClass::Global)
                                AddVariable(Value.Name, Value.Span, Value.ArrayRank > 0);
                            break;
                        case lang::StatementKind::Assignment:
                            if (MainProgram && !Value.Expressions.empty())
                                AddAssignedVariable(Value.Expressions.front().get());
                            break;
                        case lang::StatementKind::For:
                            if (MainProgram)
                                AddVariable(Value.Name, Value.Span, false);
                            break;
                        case lang::StatementKind::Goto:
                        case lang::StatementKind::Gosub:
                            if (const auto It = Labels_.find(Value.Name); It != Labels_.end())
                                ++Outline_.Labels[It->second].Count;
                            break;
                        default:
                            break;
                    }
                    for (const std::unique_ptr<lang::Expression>& Child : Value.Expressions)
                        Visit(Child.get());
                    Walk(Value.Body, MainProgram);
                    Walk(Value.ElseBody, MainProgram);
                    for (const lang::CaseClause& Clause : Value.Cases)
                    {
                        for (const std::unique_ptr<lang::Expression>& Child : Clause.Values)
                            Visit(Child.get());
                        Walk(Clause.Body, MainProgram);
                    }
                }
            }

            void Visit(const lang::Expression* Value)
            {
                if (Value == nullptr)
                    return;
                if (Value->Kind == lang::ExpressionKind::CallOrIndex || Value->Kind == lang::ExpressionKind::Call)
                {
                    if (const auto It = Functions_.find(Value->Text); It != Functions_.end())
                        ++Outline_.Functions[It->second].Count;
                }
                for (const std::unique_ptr<lang::Expression>& Child : Value->Children)
                    Visit(Child.get());
            }

            const lang::SourceDocument& Source_;
            dba::CodeOutline Outline_;
            std::unordered_map<std::string, std::size_t> Functions_;
            std::unordered_map<std::string, std::size_t> Labels_;
            std::unordered_map<std::string, std::size_t> Variables_;
        };
    } // namespace

    namespace dba
    {
        CodeOutline BuildCodeOutline(const std::filesystem::path& LogicalPath, std::string Text,
                                     dbc::language::LanguageProfile Profile)
        {
            auto Source = std::make_shared<lang::SourceDocument>();
            Source->LogicalPath = LogicalPath;
            Source->Text = std::move(Text);
            Source->Profile = Profile;
            const lang::Program Program = lang::LexAndParse(Source);

            OutlineBuilder Builder(*Source);
            Builder.Collect(Program);
            return Builder.Take();
        }
    } // namespace dba

} // namespace dbc::studio
