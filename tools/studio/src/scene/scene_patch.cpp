#include "scene/scene_patch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "language/frontend.hpp"
#include "path_utf8.hpp"
#include "scene/scene_codegen.hpp"

namespace dbc::studio
{
    namespace scene
    {
        namespace lang = dbc::language;
        using lang::SymbolId;

        namespace
        {
            // The command setting a whole part of a node.
            SymbolId WholeCommand(IdSpace Space, TransformPart Part)
            {
                switch (Space)
                {
                    case IdSpace::Object:
                        return Part == TransformPart::Position   ? SymbolId::Command_PositionObject
                               : Part == TransformPart::Rotation ? SymbolId::Command_RotateObject
                                                                 : SymbolId::Command_ScaleObject;
                    case IdSpace::Light:
                        return Part == TransformPart::Position   ? SymbolId::Command_PositionLight
                               : Part == TransformPart::Rotation ? SymbolId::Command_RotateLight
                                                                 : SymbolId::Count;
                    case IdSpace::Matrix:
                        return Part == TransformPart::Position ? SymbolId::Command_PositionMatrix : SymbolId::Count;
                    case IdSpace::Terrain:
                        return Part == TransformPart::Position ? SymbolId::Command_PositionTerrain : SymbolId::Count;
                    case IdSpace::Sound:
                        return Part == TransformPart::Position ? SymbolId::Command_PositionSound : SymbolId::Count;
                    default:
                        return SymbolId::Count;
                }
            }

            constexpr std::array<SymbolId, 3> KAxisRotations{
                SymbolId::Command_XrotateObject, SymbolId::Command_YrotateObject, SymbolId::Command_ZrotateObject};

            void Collect(const std::vector<lang::Statement>& Statements, const SourceEdit& Edit,
                         std::vector<const lang::Statement*>& Found)
            {
                for (const lang::Statement& Value : Statements)
                {
                    if (Value.Builtin == Edit.Symbol && Value.Span.Begin.Line == Edit.Line)
                        Found.push_back(&Value);
                    Collect(Value.Body, Edit, Found);
                    Collect(Value.ElseBody, Edit, Found);
                    for (const lang::CaseClause& Clause : Value.Cases)
                        Collect(Clause.Body, Edit, Found);
                }
            }

            // The statement an edit came from: nothing when the source no longer has it where the capture saw it.
            const lang::Statement* FindStatement(const lang::Program& Parsed, const SourceEdit& Edit)
            {
                std::vector<const lang::Statement*> Found;
                Collect(Parsed.Statements, Edit, Found);
                for (const lang::FunctionDeclaration& Function : Parsed.Functions)
                    Collect(Function.Body, Edit, Found);
                for (const lang::Statement* Value : Found)
                    if (Value->Span.Begin.Column == Edit.Column)
                        return Value;
                return Found.size() == 1 ? Found.front() : nullptr;
            }

            bool IsNumber(const lang::Expression& Value)
            {
                if (Value.Kind == lang::ExpressionKind::IntegerLiteral ||
                    Value.Kind == lang::ExpressionKind::RealLiteral)
                    return true;
                return Value.Kind == lang::ExpressionKind::Unary && Value.Children.size() == 1 &&
                       Value.Children.front() != nullptr && IsNumber(*Value.Children.front());
            }

            // Whether reading the expression again right away gives the same value: a function may not.
            bool ReadsTheSameTwice(const lang::Expression& Value, const lang::Program& Parsed,
                                   lang::LanguageProfile Profile)
            {
                switch (Value.Kind)
                {
                    case lang::ExpressionKind::IntegerLiteral:
                    case lang::ExpressionKind::RealLiteral:
                    case lang::ExpressionKind::Unary:
                    case lang::ExpressionKind::Binary:
                    case lang::ExpressionKind::Index:
                    case lang::ExpressionKind::Member:
                        break;
                    case lang::ExpressionKind::Name:
                    case lang::ExpressionKind::CallOrIndex:
                    {
                        const bool IsFunction =
                            lang::FindSymbol(Profile, lang::SymbolKind::Function, Value.Text) != nullptr ||
                            std::any_of(Parsed.Functions.begin(), Parsed.Functions.end(),
                                        [&](const lang::FunctionDeclaration& Function)
                                        { return Function.Name == Value.Text; });
                        if (IsFunction)
                            return false;
                        break;
                    }
                    default:
                        return false;
                }
                return std::all_of(Value.Children.begin(), Value.Children.end(),
                                   [&](const std::unique_ptr<lang::Expression>& Child)
                                   { return Child != nullptr && ReadsTheSameTwice(*Child, Parsed, Profile); });
            }

            struct TextRange
            {
                std::size_t Begin = 0;
                std::size_t End = 0;
            };

            // An expression's span stops at its operands, so in `(a + b) / 2` it starts after the parenthesis.
            // Nothing when the parentheses it leaves out are not next to it.
            std::optional<TextRange> WholeExpression(std::string_view Text, const lang::Expression& Value)
            {
                TextRange Range{Value.Span.Begin.Offset, Value.Span.End.Offset};
                if (Range.Begin > Range.End || Range.End > Text.size())
                    return std::nullopt;
                int Unclosed = 0;
                int Unopened = 0;
                bool InString = false;
                for (std::size_t Index = Range.Begin; Index < Range.End; ++Index)
                {
                    const char Character = Text[Index];
                    if (Character == '"')
                        InString = !InString;
                    else if (!InString && Character == '(')
                        ++Unclosed;
                    else if (!InString && Character == ')' && Unclosed > 0)
                        --Unclosed;
                    else if (!InString && Character == ')')
                        ++Unopened;
                }
                const auto IsBlank = [](char Character) { return Character == ' ' || Character == '\t'; };
                for (; Unopened > 0; --Unopened)
                {
                    std::size_t At = Range.Begin;
                    while (At > 0 && IsBlank(Text[At - 1]))
                        --At;
                    if (At == 0 || Text[At - 1] != '(')
                        return std::nullopt;
                    Range.Begin = At - 1;
                }
                for (; Unclosed > 0; --Unclosed)
                {
                    std::size_t At = Range.End;
                    while (At < Text.size() && IsBlank(Text[At]))
                        ++At;
                    if (At == Text.size() || Text[At] != ')')
                        return std::nullopt;
                    Range.End = At + 1;
                }
                return Range;
            }

            // How many statements name a known command, and with how many arguments.
            struct CommandCount
            {
                std::size_t Known = 0;
                std::size_t Unknown = 0;
                std::size_t Arguments = 0;

                void Add(const std::vector<lang::Statement>& Statements)
                {
                    for (const lang::Statement& Value : Statements)
                    {
                        if (Value.Kind == lang::StatementKind::Command)
                        {
                            ++(Value.Builtin ? Known : Unknown);
                            Arguments += Value.Expressions.size();
                        }
                        Add(Value.Body);
                        Add(Value.ElseBody);
                        for (const lang::CaseClause& Clause : Value.Cases)
                            Add(Clause.Body);
                    }
                }

                void Add(const lang::Program& Parsed)
                {
                    Add(Parsed.Statements);
                    for (const lang::FunctionDeclaration& Function : Parsed.Functions)
                        Add(Function.Body);
                }
            };

            std::size_t ErrorCount(const lang::Program& Parsed)
            {
                return static_cast<std::size_t>(std::count_if(
                    Parsed.Diagnostics.begin(), Parsed.Diagnostics.end(),
                    [](const lang::Diagnostic& Value) { return Value.Severity == lang::DiagnosticSeverity::Error; }));
            }

            lang::Program Parse(const std::string& Text, const std::string& LogicalPath, lang::LanguageProfile Profile)
            {
                auto Source = std::make_shared<lang::SourceDocument>();
                Source->LogicalPath = dbc::PathFromUtf8(LogicalPath);
                Source->Text = Text;
                Source->Profile = Profile;
                return lang::LexAndParse(std::move(Source));
            }

            bool IsComment(std::string_view Rest)
            {
                if (Rest.empty() || Rest.front() == '`' || Rest.front() == '\'' || Rest.starts_with("//"))
                    return true;
                if (Rest.size() < 3 || std::toupper(static_cast<unsigned char>(Rest[0])) != 'R' ||
                    std::toupper(static_cast<unsigned char>(Rest[1])) != 'E' ||
                    std::toupper(static_cast<unsigned char>(Rest[2])) != 'M')
                    return false;
                return Rest.size() == 3 || std::isspace(static_cast<unsigned char>(Rest[3])) != 0;
            }

            struct Change
            {
                std::size_t Begin = 0;
                std::size_t End = 0;
                std::string Text;
            };

            std::string_view NewlineOf(std::string_view Text)
            {
                const std::size_t First = Text.find('\n');
                return First != std::string_view::npos && First > 0 && Text[First - 1] == '\r' ? "\r\n" : "\n";
            }

            // On a line of its own when the statement has one, so a block's indentation carries over. Anything else
            // on the line, like the THEN of a one-line IF, keeps the new statement in the same flow.
            Change InsertAfter(std::string_view Text, const lang::Statement& Value, const std::string& Added)
            {
                const std::size_t Begin = Value.Span.Begin.Offset;
                const std::size_t End = Value.Span.End.Offset;
                const std::size_t Before = Begin == 0 ? std::string_view::npos : Text.rfind('\n', Begin - 1);
                const std::size_t LineBegin = Before == std::string_view::npos ? 0 : Before + 1;
                const std::size_t Indent = std::min(Text.find_first_not_of(" \t", LineBegin), Begin);

                std::size_t LineEnd = std::min(Text.find('\n', End), Text.size());
                if (LineEnd > End && Text[LineEnd - 1] == '\r')
                    --LineEnd;
                std::string_view Rest = Text.substr(End, LineEnd - End);
                Rest.remove_prefix(std::min(Rest.find_first_not_of(" \t"), Rest.size()));

                if (Indent == Begin && IsComment(Rest))
                    return {LineEnd, LineEnd,
                            std::string(NewlineOf(Text)) + std::string(Text.substr(LineBegin, Begin - LineBegin)) +
                                Added};
                return {End, End, " : " + Added};
            }

            std::string SpelledLike(std::string_view Name, std::string_view Example)
            {
                std::string Result(Name);
                if (!Example.empty() && std::islower(static_cast<unsigned char>(Example.front())) != 0)
                    std::transform(Result.begin(), Result.end(), Result.begin(),
                                   [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
                return Result;
            }

            const char* AxisName(int Axis)
            {
                constexpr std::array<const char*, 3> Names{"X", "Y", "Z"};
                return Names[static_cast<std::size_t>(Axis)];
            }

            const char* PartName(TransformPart Part)
            {
                return Part == TransformPart::Position   ? "position"
                       : Part == TransformPart::Rotation ? "rotation"
                                                         : "scale";
            }
        } // namespace

        const glm::vec3& TransformValue(const SceneNode& Node, TransformPart Part)
        {
            return Part == TransformPart::Position   ? Node.Position
                   : Part == TransformPart::Rotation ? Node.Rotation
                                                     : Node.Scale;
        }

        glm::vec3& TransformValue(SceneNode& Node, TransformPart Part)
        {
            return const_cast<glm::vec3&>(TransformValue(static_cast<const SceneNode&>(Node), Part));
        }

        PatchPlan PlanPatches(const SceneDocument& Before, const SceneDocument& After,
                              const std::map<NodeKey, NodeOrigin>& Origins)
        {
            // Below what a drag or a typed value means: a gizmo that only recomputes a value leaves the source alone.
            constexpr float KUnchanged = 1e-4f;
            PatchPlan Plan;
            for (const SceneNode& Node : After.Nodes)
            {
                const NodeKey Key{IdSpaceOf(Node.Kind), Node.Id};
                const SceneNode* Old = Before.Find(Key.first, Key.second);
                const auto Found = Origins.find(Key);
                if (Old == nullptr || Found == Origins.end())
                    continue;
                const NodeOrigin& Origin = Found->second;
                for (std::size_t PartIndex = 0; PartIndex < KTransformPartCount; ++PartIndex)
                {
                    const auto Part = static_cast<TransformPart>(PartIndex);
                    // A matrix has no turn or scale for BASIC to set, whatever a group edit did to its fields.
                    const SymbolId Command = WholeCommand(Key.first, Part);
                    if (Command == SymbolId::Count)
                        continue;
                    const glm::vec3& Was = TransformValue(*Old, Part);
                    const glm::vec3& Now = TransformValue(Node, Part);
                    const auto& Axes = Origin.Axes[PartIndex];
                    std::array<bool, 3> Adds{};
                    bool AnyWriter = false;
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        const AxisOrigin& Value = Axes[static_cast<std::size_t>(Axis)];
                        AnyWriter = AnyWriter || Value.Argument >= 0;
                        if (Value.Access == AxisAccess::Locked || std::abs(Now[Axis] - Was[Axis]) <= KUnchanged)
                            continue;
                        Plan.Axes.push_back({Key, Part, Axis, Now[Axis]});
                        if (Value.Access == AxisAccess::Replace)
                        {
                            SourceEdit Edit;
                            Edit.Line = Value.Writer.Line;
                            Edit.Column = Value.Writer.Column;
                            Edit.Symbol = Value.WriterSymbol;
                            Edit.Argument = Value.Argument;
                            Edit.Values = {Now[Axis]};
                            Plan.Files[Value.Writer.File].push_back(std::move(Edit));
                        }
                        else
                        {
                            Adds[static_cast<std::size_t>(Axis)] = true;
                        }
                    }
                    if (std::none_of(Adds.begin(), Adds.end(), [](bool Value) { return Value; }))
                        continue;

                    const auto Add = [&](SymbolId Command, std::vector<float> Values)
                    {
                        SourceEdit Edit;
                        Edit.Line = Origin.Creator.Line;
                        Edit.Column = Origin.Creator.Column;
                        Edit.Symbol = Origin.CreatorSymbol;
                        Edit.Added = Command;
                        Edit.IdArgument = Origin.CreatorIdArgument;
                        Edit.Values = std::move(Values);
                        Plan.Files[Origin.Creator.File].push_back(std::move(Edit));
                    };
                    if (AnyWriter)
                    {
                        // Only an object's turns are set axis by axis, so only they get here.
                        for (std::size_t Axis = 0; Axis < 3; ++Axis)
                            if (Adds[Axis])
                                Add(KAxisRotations[Axis], {Now[static_cast<int>(Axis)]});
                        continue;
                    }
                    // Right after the creation, an axis nothing has set still holds its default.
                    const float Default = Part == TransformPart::Scale ? 100.0f : 0.0f;
                    Add(Command, {Adds[0] ? Now.x : Default, Adds[1] ? Now.y : Default, Adds[2] ? Now.z : Default});
                }
            }
            return Plan;
        }

        PatchResult PatchSource(const std::string& Text, const std::string& LogicalPath, lang::LanguageProfile Profile,
                                std::span<const SourceEdit> Edits)
        {
            PatchResult Result;
            const lang::Program Parsed = Parse(Text, LogicalPath, Profile);
            std::vector<Change> Changes;
            Changes.reserve(Edits.size());
            CommandCount Expected;
            Expected.Add(Parsed);
            for (const SourceEdit& Edit : Edits)
            {
                const std::string Where = "line " + std::to_string(Edit.Line) + ": ";
                const lang::Statement* Value = FindStatement(Parsed, Edit);
                if (Value == nullptr)
                {
                    Result.Error = Where + "the statement is no longer there. Refresh the scene and try again.";
                    return Result;
                }
                if (Edit.Argument >= 0)
                {
                    const auto Argument = static_cast<std::size_t>(Edit.Argument);
                    if (Argument >= Value->Expressions.size() || Value->Expressions[Argument] == nullptr ||
                        Edit.Values.empty())
                    {
                        Result.Error = Where + "the statement no longer has this argument.";
                        return Result;
                    }
                    const lang::Expression& Old = *Value->Expressions[Argument];
                    const std::optional<TextRange> Range = WholeExpression(Text, Old);
                    if (!Range)
                    {
                        Result.Error = Where + "the argument's parentheses could not be told apart.";
                        return Result;
                    }
                    if (!IsNumber(Old))
                        Result.Replaced.push_back(std::to_string(Old.Span.Begin.Line) + ": " +
                                                  Text.substr(Range->Begin, Range->End - Range->Begin));
                    Changes.push_back({Range->Begin, Range->End, NumberLiteral(Edit.Values.front())});
                    continue;
                }

                const auto IdArgument = static_cast<std::size_t>(Edit.IdArgument);
                if (IdArgument >= Value->Expressions.size() || Value->Expressions[IdArgument] == nullptr)
                {
                    Result.Error = Where + "the statement no longer names its number.";
                    return Result;
                }
                const lang::Expression& Id = *Value->Expressions[IdArgument];
                const std::optional<TextRange> IdRange = WholeExpression(Text, Id);
                if (!IdRange)
                {
                    Result.Error = Where + "the number's parentheses could not be told apart.";
                    return Result;
                }
                const std::string IdText = Text.substr(IdRange->Begin, IdRange->End - IdRange->Begin);
                if (!ReadsTheSameTwice(Id, Parsed, Profile))
                {
                    Result.Error = Where + "a statement cannot follow it, because " + IdText +
                                   " may give another number when read again.";
                    return Result;
                }
                std::string Added = SpelledLike(lang::Symbol(Edit.Added).CanonicalName,
                                                std::string_view(Text).substr(Value->Span.Begin.Offset, 1)) +
                                    " " + IdText;
                for (const float Number : Edit.Values)
                    Added += ", " + NumberLiteral(Number);
                Changes.push_back(InsertAfter(Text, *Value, Added));
                ++Expected.Known;
                Expected.Arguments += 1 + Edit.Values.size();
            }

            // Stable, then applied from the end: earlier offsets stay valid, and insertions at one place keep their
            // order.
            std::stable_sort(Changes.begin(), Changes.end(),
                             [](const Change& Left, const Change& Right) { return Left.Begin < Right.Begin; });
            Result.Text = Text;
            for (std::size_t Index = Changes.size(); Index > 0; --Index)
            {
                const Change& Value = Changes[Index - 1];
                if (Index < Changes.size() && Value.End > Changes[Index].Begin)
                {
                    Result.Error = "Two edits touch the same text.";
                    return Result;
                }
                Result.Text.replace(Value.Begin, Value.End - Value.Begin, Value.Text);
            }
            // A command whose arguments do not parse is no error at this stage, only an unknown command, so count them.
            const lang::Program Edited = Parse(Result.Text, LogicalPath, Profile);
            CommandCount Found;
            Found.Add(Edited);
            if (ErrorCount(Edited) > ErrorCount(Parsed) || Found.Known != Expected.Known ||
                Found.Unknown != Expected.Unknown || Found.Arguments != Expected.Arguments)
                Result.Error = "The edited source would not parse, so it was left as it is.";
            return Result;
        }

        std::vector<std::string> UnappliedEdits(std::span<const AxisEdit> Edits, const CaptureReadResult& Read)
        {
            std::vector<std::string> Messages;
            for (const AxisEdit& Edit : Edits)
            {
                const SceneNode* Node = Read.Document.Find(Edit.Node.first, Edit.Node.second);
                if (Node == nullptr ||
                    SameTransformValue(Edit.Part, TransformValue(*Node, Edit.Part)[Edit.Axis], Edit.Value))
                    continue;
                std::string Message = Node->Name + ": the program does not keep the new " + AxisName(Edit.Axis) + " " +
                                      PartName(Edit.Part) + " " + NumberLiteral(Edit.Value);
                const auto Found = Read.Origins.find(Edit.Node);
                if (Found != Read.Origins.end())
                {
                    const AxisOrigin& Origin =
                        Found->second.Axes[static_cast<std::size_t>(Edit.Part)][static_cast<std::size_t>(Edit.Axis)];
                    if (!Origin.Reason.empty())
                        Message += ". " + Origin.Reason;
                    if (Origin.Argument >= 0)
                        Message += " See line " + std::to_string(Origin.Writer.Line) + " of " +
                                   dbc::PathToUtf8(dbc::PathFromUtf8(Origin.Writer.File).filename()) + ".";
                }
                Messages.push_back(std::move(Message));
            }
            return Messages;
        }
    } // namespace scene
} // namespace dbc::studio
