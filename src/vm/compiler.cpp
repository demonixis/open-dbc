#include "compiler.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>

namespace dbc::vm
{
    namespace
    {
        std::string Canonical(std::string_view Text)
        {
            std::string Result(Text);
            std::transform(Result.begin(), Result.end(), Result.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
            return Result;
        }

        class Compiler
        {
          public:
            explicit Compiler(const language::Program& Program) : Program_(Program)
            {
                Module_ = std::make_shared<Module>();
                if (Program.Source)
                {
                    Module_->Profile = Program.Source->Profile;
                    Module_->LogicalPath = Program.Source->LogicalPath;
                }
            }

            CompileResult Run()
            {
                if (!Program_.Source || !Program_.IsBound || Program_.HasErrors())
                {
                    Error("DBC3000", "A successfully bound program is required before lowering", {});
                    return {nullptr, std::move(Diagnostics_)};
                }

                IndexDeclarations();
                BuildDataPool();
                CompileEntry();
                for (std::size_t Index = 0; Index < Program_.Functions.size(); ++Index)
                {
                    CompileFunction(Program_.Functions[Index], Index + 1);
                }
                if (!Diagnostics_.empty())
                    return {nullptr, std::move(Diagnostics_)};
                return {std::move(Module_), {}};
            }

          private:
            struct Context
            {
                Function* Output = nullptr;
                std::size_t FunctionIndex = 0;
                std::unordered_map<std::string, std::size_t> Locals{};
                std::unordered_map<std::string, std::size_t> Labels{};
                std::vector<std::pair<std::size_t, std::string>> Jumps{};
                std::vector<std::vector<std::size_t>> LoopExits{};
            };

            struct SlotReference
            {
                SlotScope Scope = SlotScope::Global;
                std::size_t Index = 0;
            };

            struct MemberPath
            {
                const language::Expression* Root = nullptr;
                std::vector<const language::Expression*> Fields{};
            };

            struct DataEvent
            {
                const language::Statement* Statement = nullptr;
            };

            void IndexDeclarations()
            {
                for (const language::TypeDeclaration& Declaration : Program_.Types)
                {
                    TypeLayout Layout;
                    Layout.Name = Canonical(Declaration.Name);
                    for (const language::FieldDeclaration& Field : Declaration.Fields)
                    {
                        Layout.Fields.push_back({Canonical(Field.Name), Field.DeclaredType});
                    }
                    Module_->Types.push_back(std::move(Layout));
                }
                for (const language::ConstantDeclaration& Declaration : Program_.Constants)
                {
                    Constants_.emplace(Canonical(Declaration.Name), &Declaration);
                }

                Module_->Functions.push_back({"<entry>", {language::TypeKind::Void, {}}, 0, {}, {}});
                FunctionIndices_.emplace("<ENTRY>", 0);
                for (const language::FunctionDeclaration& Declaration : Program_.Functions)
                {
                    const std::size_t Index = Module_->Functions.size();
                    FunctionIndices_.emplace(Canonical(Declaration.Name), Index);
                    Function Output;
                    Output.Name = Canonical(Declaration.Name);
                    Output.ReturnType = Declaration.ReturnType;
                    Output.ParameterCount = Declaration.Parameters.size();
                    Module_->Functions.push_back(std::move(Output));
                }

                ScanGlobalDeclarations(Program_.Statements);
                for (const language::FunctionDeclaration& Function : Program_.Functions)
                    ScanGlobalDeclarations(Function.Body);
            }

            void ScanGlobalDeclarations(const std::vector<language::Statement>& Statements)
            {
                for (const language::Statement& Value : Statements)
                {
                    if (Value.Kind == language::StatementKind::Declaration &&
                        Value.Storage != language::StorageClass::Local)
                    {
                        AddGlobal(Value.Name, Value.DeclaredType, Value.ArrayRank);
                    }
                    ScanGlobalDeclarations(Value.Body);
                    ScanGlobalDeclarations(Value.ElseBody);
                    for (const language::CaseClause& Clause : Value.Cases)
                        ScanGlobalDeclarations(Clause.Body);
                }
            }

            void CollectDataEvents(const std::vector<language::Statement>& Statements, std::vector<DataEvent>& Events)
            {
                for (const language::Statement& Value : Statements)
                {
                    if (Value.Kind == language::StatementKind::Data || Value.Kind == language::StatementKind::Label)
                        Events.push_back({&Value});
                    CollectDataEvents(Value.Body, Events);
                    CollectDataEvents(Value.ElseBody, Events);
                    for (const language::CaseClause& Clause : Value.Cases)
                        CollectDataEvents(Clause.Body, Events);
                }
            }

            void BuildDataPool()
            {
                std::vector<DataEvent> Events;
                CollectDataEvents(Program_.Statements, Events);
                for (const language::FunctionDeclaration& Function : Program_.Functions)
                    CollectDataEvents(Function.Body, Events);
                std::stable_sort(Events.begin(), Events.end(), [](const DataEvent& Left, const DataEvent& Right)
                                 { return Left.Statement->Span.Begin.Offset < Right.Statement->Span.Begin.Offset; });

                for (const DataEvent& Event : Events)
                {
                    const language::Statement& Value = *Event.Statement;
                    if (Value.Kind == language::StatementKind::Label)
                    {
                        const std::string Name = Canonical(Value.Name);
                        DataLabelIndices_.emplace(Name, Module_->Data.size());
                        Module_->DataLabels.emplace_back(Name, Module_->Data.size());
                        continue;
                    }
                    for (const auto& Expression : Value.Expressions)
                    {
                        if (const std::optional<DataValue> Item = EvaluateData(*Expression))
                            Module_->Data.push_back(*Item);
                    }
                }
            }

            std::optional<DataValue> EvaluateData(const language::Expression& Value)
            {
                switch (Value.Kind)
                {
                    case language::ExpressionKind::IntegerLiteral:
                        return DataValue{{language::TypeKind::Integer, {}}, static_cast<std::int64_t>(Value.Number)};
                    case language::ExpressionKind::RealLiteral:
                    {
                        DataValue Result;
                        Result.ValueType = {language::TypeKind::Real, {}};
                        Result.Real = Value.Number;
                        return Result;
                    }
                    case language::ExpressionKind::StringLiteral:
                    {
                        DataValue Result;
                        Result.ValueType = {language::TypeKind::String, {}};
                        Result.StringIndex = static_cast<std::uint32_t>(InternString(Value.Text));
                        return Result;
                    }
                    case language::ExpressionKind::Name:
                    {
                        const auto Iterator = Constants_.find(Canonical(Value.Text));
                        if (Value.Reference != language::ReferenceKind::Constant || Iterator == Constants_.end())
                            break;
                        return EvaluateData(*Iterator->second->Value);
                    }
                    case language::ExpressionKind::Unary:
                    {
                        if (Value.Children.size() != 1)
                            break;
                        std::optional<DataValue> Operand = EvaluateData(*Value.Children.front());
                        if (!Operand || !Operand->ValueType.IsNumeric())
                            break;
                        if (Value.Text == "+")
                            return Operand;
                        if (Value.Text == "-")
                        {
                            if (Operand->ValueType.Kind == language::TypeKind::Real)
                                Operand->Real = -Operand->Real;
                            else if (Operand->Integer == std::numeric_limits<std::int64_t>::min())
                            {
                                Error("DBC3011", "DATA integer constant overflows", Value.Span);
                                return std::nullopt;
                            }
                            else
                                Operand->Integer = -Operand->Integer;
                            return Operand;
                        }
                        break;
                    }
                    default:
                        break;
                }
                Error("DBC3011", "DATA values must be literal or named constants", Value.Span);
                return std::nullopt;
            }

            void CompileEntry()
            {
                Context Value;
                Value.Output = &Module_->Functions.front();
                Value.FunctionIndex = 0;
                CompileStatements(Program_.Statements, Value);
                Emit(Value, OpCode::Halt, {}, {language::TypeKind::Void, {}});
                PatchJumps(Value);
            }

            void CompileFunction(const language::FunctionDeclaration& Declaration, std::size_t FunctionIndex)
            {
                Context Value;
                Value.Output = &Module_->Functions[FunctionIndex];
                Value.FunctionIndex = FunctionIndex;
                for (const language::ParameterDeclaration& Parameter : Declaration.Parameters)
                    AddLocal(Value, Parameter.Name, Parameter.DeclaredType, 0);
                ScanLocalDeclarations(Declaration.Body, Value);
                for (const auto& [Name, Type] : Declaration.ImplicitLocals)
                    AddLocal(Value, Name, Type, 0);
                CompileStatements(Declaration.Body, Value);
                if (Declaration.FinalReturn)
                    CompileExpression(*Declaration.FinalReturn, Value);
                else
                    EmitDefault(Declaration.ReturnType, Declaration.Span, Value);
                Emit(Value, OpCode::Return, Declaration.Span, Declaration.ReturnType);
                PatchJumps(Value);
            }

            void ScanLocalDeclarations(const std::vector<language::Statement>& Statements, Context& Context)
            {
                for (const language::Statement& Value : Statements)
                {
                    if (Value.Kind == language::StatementKind::Declaration &&
                        Value.Storage == language::StorageClass::Local)
                    {
                        AddLocal(Context, Value.Name, Value.DeclaredType, Value.ArrayRank);
                    }
                    ScanLocalDeclarations(Value.Body, Context);
                    ScanLocalDeclarations(Value.ElseBody, Context);
                    for (const language::CaseClause& Clause : Value.Cases)
                        ScanLocalDeclarations(Clause.Body, Context);
                }
            }

            void CompileStatements(const std::vector<language::Statement>& Statements, Context& Context)
            {
                for (const language::Statement& Value : Statements)
                    CompileStatement(Value, Context);
            }

            void CompileStatement(const language::Statement& Value, Context& Context)
            {
                switch (Value.Kind)
                {
                    case language::StatementKind::Error:
                        return;
                    case language::StatementKind::Command:
                        CompileCommand(Value, Context);
                        return;
                    case language::StatementKind::Expression:
                        if (!Value.Expressions.empty())
                        {
                            CompileExpression(*Value.Expressions.front(), Context);
                            Emit(Context, OpCode::Pop, Value.Span, {language::TypeKind::Void, {}});
                        }
                        return;
                    case language::StatementKind::Assignment:
                        CompileAssignment(Value, Context);
                        return;
                    case language::StatementKind::Declaration:
                        CompileDeclaration(Value, Context);
                        return;
                    case language::StatementKind::For:
                        CompileFor(Value, Context);
                        return;
                    case language::StatementKind::While:
                        CompileWhile(Value, Context);
                        return;
                    case language::StatementKind::Repeat:
                        CompileRepeat(Value, Context);
                        return;
                    case language::StatementKind::Do:
                        CompileDo(Value, Context);
                        return;
                    case language::StatementKind::If:
                        CompileIf(Value, Context);
                        return;
                    case language::StatementKind::Select:
                        CompileSelect(Value, Context);
                        return;
                    case language::StatementKind::Goto:
                    case language::StatementKind::Gosub:
                    {
                        const OpCode Op =
                            Value.Kind == language::StatementKind::Goto ? OpCode::Jump : OpCode::CallLabel;
                        const std::size_t Instruction = Emit(Context, Op, Value.Span, {language::TypeKind::Void, {}});
                        Context.Jumps.emplace_back(Instruction, Canonical(Value.Name));
                        return;
                    }
                    case language::StatementKind::Return:
                        Emit(Context, OpCode::ReturnSubroutine, Value.Span, {language::TypeKind::Void, {}});
                        return;
                    case language::StatementKind::Exit:
                        CompileExit(Value, Context);
                        return;
                    case language::StatementKind::ExitFunction:
                        if (!Value.Expressions.empty())
                            CompileExpression(*Value.Expressions.front(), Context);
                        else
                            EmitDefault(Context.Output->ReturnType, Value.Span, Context);
                        Emit(Context, OpCode::Return, Value.Span, Context.Output->ReturnType);
                        return;
                    case language::StatementKind::End:
                        Emit(Context, OpCode::Halt, Value.Span, {language::TypeKind::Void, {}});
                        return;
                    case language::StatementKind::Data:
                        return;
                    case language::StatementKind::Label:
                        Context.Labels.emplace(Canonical(Value.Name), Context.Output->Code.size());
                        return;
                }
            }

            void CompileCommand(const language::Statement& Value, Context& Context)
            {
                if (!Value.Builtin)
                {
                    Error("DBC3002", "Cannot lower an unresolved command", Value.Span);
                    return;
                }
                const language::BuiltinConvention Convention = language::ConventionFor(*Value.Builtin);
                if (Convention == language::BuiltinConvention::DataRead)
                {
                    CompileDataRead(Value, Context);
                    return;
                }
                if (Convention == language::BuiltinConvention::DataRestore)
                {
                    CompileDataRestore(Value, Context);
                    return;
                }
                if (Convention == language::BuiltinConvention::Input ||
                    Convention == language::BuiltinConvention::FileRead)
                {
                    CompileOutputCommand(Value, Context);
                    return;
                }
                if (Convention == language::BuiltinConvention::LoadArray ||
                    Convention == language::BuiltinConvention::SaveArray)
                {
                    CompileArrayCommand(Value, Convention, Context);
                    return;
                }
                if (Convention == language::BuiltinConvention::Undim)
                {
                    CompileUndim(Value, Context);
                    return;
                }
                if (Convention == language::BuiltinConvention::ArrayCollection)
                {
                    CompileArrayCollection(Value, Context);
                    return;
                }
                if (!Value.Overload)
                {
                    Error("DBC3002", "Cannot lower a command without a selected overload", Value.Span);
                    return;
                }
                for (const auto& Argument : Value.Expressions)
                    CompileExpression(*Argument, Context);
                Instruction Call;
                Call.Op = OpCode::CallBuiltin;
                Call.Span = Value.Span;
                Call.ResultType = {language::TypeKind::Void, {}};
                Call.A = static_cast<std::uint32_t>(*Value.Builtin);
                Call.B = static_cast<std::uint32_t>(Value.Expressions.size());
                Call.C = 0;
                Call.D = Call.B;
                Call.E = Value.Overload->Value;
                Call.Integer = Value.ContinuesPrint ? 1 : 0;
                Context.Output->Code.push_back(Call);
            }

            template <typename Producer>
            void CompileProducedStore(const language::Expression& Target, Context& Context, Producer Produce)
            {
                if (Target.Kind == language::ExpressionKind::Member)
                {
                    const MemberPath Path = DecomposeMember(Target);
                    if (!PrepareMemberStore(Path, Context, Target.Span))
                        return;
                    Produce();
                    EmitField(Context, OpCode::StoreField, *Path.Fields.back());
                    FinishMemberStore(Path, Context);
                    return;
                }
                if (Target.Kind == language::ExpressionKind::Index)
                {
                    CompileArrayIndices(Target, Context);
                    Produce();
                    EmitArray(Context, OpCode::StoreArray, Target);
                    return;
                }
                Produce();
                CompileStore(Target, Context);
            }

            bool EmitVmBuiltinMarker(OpCode Op, const language::Statement& Value, Context& Context)
            {
                if (!Value.Builtin || !Value.Overload)
                {
                    Error("DBC3002", "Cannot lower a VM builtin without a selected overload", Value.Span);
                    return false;
                }
                Instruction Marker;
                Marker.Op = Op;
                Marker.Span = Value.Span;
                Marker.ResultType = {language::TypeKind::Void, {}};
                Marker.A = static_cast<std::uint32_t>(*Value.Builtin);
                Marker.D = static_cast<std::uint32_t>(Value.Expressions.size());
                Marker.E = Value.Overload->Value;
                Context.Output->Code.push_back(Marker);
                return true;
            }

            void CompileDataRead(const language::Statement& Value, Context& Context)
            {
                if (!EmitVmBuiltinMarker(OpCode::BeginBuiltin, Value, Context))
                    return;
                for (const auto& Target : Value.Expressions)
                {
                    CompileProducedStore(*Target, Context,
                                         [&]
                                         {
                                             Instruction Read;
                                             Read.Op = OpCode::ReadData;
                                             Read.Span = Target->Span;
                                             Read.ResultType = Target->ResolvedType;
                                             Context.Output->Code.push_back(Read);
                                         });
                }
                EmitVmBuiltinMarker(OpCode::CompleteBuiltin, Value, Context);
            }

            void CompileDataRestore(const language::Statement& Value, Context& Context)
            {
                std::size_t Index = 0;
                if (!Value.Expressions.empty())
                {
                    const language::Expression& Target = *Value.Expressions.front();
                    if (Target.Kind == language::ExpressionKind::IntegerLiteral && Target.Number >= 0.0 &&
                        Target.Number <= static_cast<double>(Module_->Data.size()))
                    {
                        Index = static_cast<std::size_t>(Target.Number);
                    }
                    else if (Target.Kind == language::ExpressionKind::Name)
                    {
                        const auto Iterator = DataLabelIndices_.find(Canonical(Target.Text));
                        if (Iterator == DataLabelIndices_.end())
                        {
                            Error("DBC3012", "RESTORE references unknown DATA label '" + Canonical(Target.Text) + "'",
                                  Target.Span);
                            return;
                        }
                        Index = Iterator->second;
                    }
                    else
                    {
                        Error("DBC3012", "RESTORE requires a DATA label or non-negative constant index", Target.Span);
                        return;
                    }
                }
                Instruction Restore;
                Restore.Op = OpCode::RestoreData;
                Restore.Span = Value.Span;
                Restore.ResultType = {language::TypeKind::Void, {}};
                Restore.A = static_cast<std::uint32_t>(Index);
                if (!EmitVmBuiltinMarker(OpCode::BeginBuiltin, Value, Context))
                    return;
                Context.Output->Code.push_back(Restore);
                EmitVmBuiltinMarker(OpCode::CompleteBuiltin, Value, Context);
            }

            void CompileOutputCommand(const language::Statement& Value, Context& Context)
            {
                if (Value.Expressions.empty() || !Value.Overload)
                {
                    Error("DBC3013",
                          Value.Expressions.empty() ? "Output command has no destination"
                                                    : "Output command has no selected overload",
                          Value.Span);
                    return;
                }
                const language::Expression& Target = *Value.Expressions.back();
                CompileProducedStore(Target, Context,
                                     [&]
                                     {
                                         for (std::size_t Index = 0; Index + 1 < Value.Expressions.size(); ++Index)
                                             CompileExpression(*Value.Expressions[Index], Context);
                                         Instruction Call;
                                         Call.Op = OpCode::CallBuiltin;
                                         Call.Span = Value.Span;
                                         Call.ResultType = Target.ResolvedType;
                                         Call.A = static_cast<std::uint32_t>(*Value.Builtin);
                                         Call.B = static_cast<std::uint32_t>(Value.Expressions.size() - 1);
                                         Call.C = 1;
                                         Call.D = static_cast<std::uint32_t>(Value.Expressions.size());
                                         Call.E = Value.Overload->Value;
                                         Context.Output->Code.push_back(Call);
                                     });
            }

            void CompileArrayCommand(const language::Statement& Value, language::BuiltinConvention Convention,
                                     Context& Context)
            {
                if (Value.Expressions.size() != 2 || Value.Expressions[1]->Kind != language::ExpressionKind::Index ||
                    !Value.Overload)
                {
                    Error("DBC3014", "LOAD ARRAY and SAVE ARRAY require an array destination", Value.Span);
                    return;
                }
                const language::Expression& Array = *Value.Expressions[1];
                const SlotReference Slot = ResolveArraySlot(Array, Context);
                CompileExpression(*Value.Expressions.front(), Context);
                Instruction Call;
                Call.Op = OpCode::CallBuiltinArray;
                Call.Span = Value.Span;
                Call.ResultType = Array.ResolvedType;
                Call.A = static_cast<std::uint32_t>(*Value.Builtin);
                Call.B = static_cast<std::uint32_t>(Slot.Index);
                Call.C = static_cast<std::uint32_t>(Slot.Scope);
                Call.D = Convention == language::BuiltinConvention::LoadArray ? 1U : 0U;
                Call.E = Value.Overload->Value;
                Context.Output->Code.push_back(Call);
            }

            void CompileUndim(const language::Statement& Value, Context& Context)
            {
                if (Value.Expressions.size() != 1 || Value.Expressions.front()->Kind != language::ExpressionKind::Index)
                {
                    Error("DBC3014", "UNDIM requires an array name followed by parentheses", Value.Span);
                    return;
                }
                const language::Expression& Array = *Value.Expressions.front();
                const SlotReference Slot = ResolveArraySlot(Array, Context);
                Instruction Destroy;
                Destroy.Op = OpCode::DestroyArray;
                Destroy.Span = Value.Span;
                Destroy.ResultType = Array.ResolvedType;
                Destroy.A = static_cast<std::uint32_t>(Slot.Index);
                Destroy.B = static_cast<std::uint32_t>(SlotRank(Slot, Context));
                Destroy.C = static_cast<std::uint32_t>(Slot.Scope);
                if (!EmitVmBuiltinMarker(OpCode::BeginBuiltin, Value, Context))
                    return;
                Context.Output->Code.push_back(Destroy);
                EmitVmBuiltinMarker(OpCode::CompleteBuiltin, Value, Context);
            }

            void CompileArrayCollection(const language::Statement& Value, Context& Context)
            {
                if (Value.Expressions.empty() || Value.Expressions.front()->Kind != language::ExpressionKind::Index ||
                    !Value.Overload)
                {
                    Error("DBC3014", "Array collection command requires an array", Value.Span);
                    return;
                }
                const language::Expression& Array = *Value.Expressions.front();
                const SlotReference Slot = ResolveArraySlot(Array, Context);
                for (std::size_t Index = 1; Index < Value.Expressions.size(); ++Index)
                    CompileExpression(*Value.Expressions[Index], Context);
                Instruction Call;
                Call.Op = OpCode::CallBuiltinArray;
                Call.Span = Value.Span;
                Call.ResultType = {language::TypeKind::Void, {}};
                Call.A = static_cast<std::uint32_t>(*Value.Builtin);
                Call.B = static_cast<std::uint32_t>(Slot.Index);
                Call.C = static_cast<std::uint32_t>(Slot.Scope);
                Call.D = static_cast<std::uint32_t>(Value.Expressions.size());
                Call.E = Value.Overload->Value;
                Context.Output->Code.push_back(Call);
            }

            void CompileAssignment(const language::Statement& Value, Context& Context)
            {
                if (Value.Expressions.empty())
                    return;
                const language::Expression& Target = *Value.Expressions.front();
                if (Value.Name == "INC" || Value.Name == "DEC")
                {
                    CompileIncrement(Value, Context);
                    return;
                }
                if (Value.Expressions.size() < 2)
                    return;

                if (Target.Kind == language::ExpressionKind::Member)
                {
                    CompileMemberStore(Target, *Value.Expressions[1], Context);
                    return;
                }
                if (Target.Kind == language::ExpressionKind::Index)
                {
                    CompileArrayIndices(Target, Context);
                    CompileExpression(*Value.Expressions[1], Context);
                    EmitArray(Context, OpCode::StoreArray, Target);
                    return;
                }
                CompileExpression(*Value.Expressions[1], Context);
                CompileStore(Target, Context);
            }

            void CompileIncrement(const language::Statement& Value, Context& Context)
            {
                const language::Expression& Target = *Value.Expressions.front();
                const BinaryOperation Operation =
                    Value.Name == "INC" ? BinaryOperation::Add : BinaryOperation::Subtract;
                if (Target.Kind == language::ExpressionKind::Member)
                {
                    CompileMemberIncrement(Target, *Value.Expressions[1], Operation, Context);
                    return;
                }
                if (Target.Kind == language::ExpressionKind::Index)
                {
                    CompileArrayIndices(Target, Context);
                    if (!Target.Children.empty())
                        EmitDuplicate(Context, Target.Children.size(), Target.Span);
                    EmitArray(Context, OpCode::LoadArray, Target);
                    CompileExpression(*Value.Expressions[1], Context);
                    EmitBinary(Operation, Target.ResolvedType, Value.Span, Context);
                    EmitArray(Context, OpCode::StoreArray, Target);
                    return;
                }
                CompileLoad(Target, Context);
                CompileExpression(*Value.Expressions[1], Context);
                EmitBinary(Operation, Target.ResolvedType, Value.Span, Context);
                CompileStore(Target, Context);
            }

            void CompileMemberStore(const language::Expression& Member, const language::Expression& Source,
                                    Context& Context)
            {
                const MemberPath Path = DecomposeMember(Member);
                if (!PrepareMemberStore(Path, Context, Member.Span))
                    return;
                CompileExpression(Source, Context);
                EmitField(Context, OpCode::StoreField, *Path.Fields.back());
                FinishMemberStore(Path, Context);
            }

            void CompileMemberIncrement(const language::Expression& Member, const language::Expression& Amount,
                                        BinaryOperation Operation, Context& Context)
            {
                const MemberPath Path = DecomposeMember(Member);
                if (!PrepareMemberStore(Path, Context, Member.Span))
                    return;
                EmitDuplicate(Context, 1, Path.Fields.back()->Span);
                EmitField(Context, OpCode::LoadField, *Path.Fields.back());
                CompileExpression(Amount, Context);
                EmitBinary(Operation, Member.ResolvedType, Member.Span, Context);
                EmitField(Context, OpCode::StoreField, *Path.Fields.back());
                FinishMemberStore(Path, Context);
            }

            MemberPath DecomposeMember(const language::Expression& Member)
            {
                MemberPath Result;
                const language::Expression* Current = &Member;
                while (Current->Kind == language::ExpressionKind::Member && !Current->Children.empty())
                {
                    Result.Fields.push_back(Current);
                    Current = Current->Children.front().get();
                }
                Result.Root = Current;
                std::reverse(Result.Fields.begin(), Result.Fields.end());
                return Result;
            }

            bool PrepareMemberStore(const MemberPath& Path, Context& Context, language::SourceSpan Span)
            {
                if (!Path.Root || Path.Fields.empty())
                {
                    Error("DBC3008", "Member expression is not storable", Span);
                    return false;
                }
                if (Path.Root->Kind == language::ExpressionKind::Index)
                {
                    CompileArrayIndices(*Path.Root, Context);
                    if (!Path.Root->Children.empty())
                        EmitDuplicate(Context, Path.Root->Children.size(), Path.Root->Span);
                    EmitArray(Context, OpCode::LoadArray, *Path.Root);
                }
                else if (Path.Root->Kind == language::ExpressionKind::Name)
                {
                    CompileLoad(*Path.Root, Context);
                }
                else
                {
                    Error("DBC3008", "Member root is not storable", Path.Root->Span);
                    return false;
                }
                for (std::size_t Index = 0; Index + 1 < Path.Fields.size(); ++Index)
                {
                    EmitDuplicate(Context, 1, Path.Fields[Index]->Span);
                    EmitField(Context, OpCode::LoadField, *Path.Fields[Index]);
                }
                return true;
            }

            void FinishMemberStore(const MemberPath& Path, Context& Context)
            {
                for (std::size_t Index = Path.Fields.size() - 1; Index > 0; --Index)
                    EmitField(Context, OpCode::StoreField, *Path.Fields[Index - 1]);
                if (Path.Root->Kind == language::ExpressionKind::Index)
                    EmitArray(Context, OpCode::StoreArray, *Path.Root);
                else
                    CompileStore(*Path.Root, Context);
            }

            void CompileDeclaration(const language::Statement& Value, Context& Context)
            {
                const SlotReference Slot = ResolveSlot(Value.Name, Value.DeclaredType, Value.ArrayRank, Context);
                if (Value.ArrayRank != 0)
                {
                    for (std::size_t Index = 0; Index < Value.ArrayRank && Index < Value.Expressions.size(); ++Index)
                        CompileExpression(*Value.Expressions[Index], Context);
                    Instruction Create;
                    Create.Op = OpCode::CreateArray;
                    Create.Span = Value.Span;
                    Create.ResultType = Value.DeclaredType;
                    Create.A = static_cast<std::uint32_t>(Slot.Index);
                    Create.B = static_cast<std::uint32_t>(Value.ArrayRank);
                    Create.C = static_cast<std::uint32_t>(Slot.Scope);
                    Context.Output->Code.push_back(Create);
                }
                if (Value.Expressions.size() > Value.ArrayRank)
                {
                    CompileExpression(*Value.Expressions.back(), Context);
                    EmitStore(Context, Slot, Value.Span, Value.DeclaredType);
                }
            }

            void CompileFor(const language::Statement& Value, Context& Context)
            {
                if (Value.Expressions.size() != 3)
                    return;
                const SlotReference Variable = ResolveSlot(Value.Name, ImplicitType(Value.Name), 0, Context);
                if (SlotRank(Variable, Context) != 0)
                {
                    Error("DBC3010", "FOR control variable cannot be an array", Value.Span);
                    return;
                }
                const language::Type VariableType = SlotType(Variable, Context);
                CompileExpression(*Value.Expressions[0], Context);
                EmitStore(Context, Variable, Value.Expressions[0]->Span, VariableType);

                const std::size_t EndSlot = AddTemporary(Context, Value.Expressions[1]->ResolvedType);
                const std::size_t StepSlot = AddTemporary(Context, Value.Expressions[2]->ResolvedType);
                CompileExpression(*Value.Expressions[1], Context);
                EmitStore(Context, {SlotScope::Local, EndSlot}, Value.Expressions[1]->Span,
                          Value.Expressions[1]->ResolvedType);
                CompileExpression(*Value.Expressions[2], Context);
                EmitStore(Context, {SlotScope::Local, StepSlot}, Value.Expressions[2]->Span,
                          Value.Expressions[2]->ResolvedType);

                const std::size_t Begin = Context.Output->Code.size();
                EmitLoad(Context, {SlotScope::Local, StepSlot}, Value.Span, Value.Expressions[2]->ResolvedType);
                EmitInteger(Context, 0, Value.Span);
                EmitBinary(BinaryOperation::GreaterEqual, {language::TypeKind::Boolean, {}}, Value.Span, Context);
                EmitLoad(Context, Variable, Value.Span, VariableType);
                EmitLoad(Context, {SlotScope::Local, EndSlot}, Value.Span, Value.Expressions[1]->ResolvedType);
                EmitBinary(BinaryOperation::LessEqual, {language::TypeKind::Boolean, {}}, Value.Span, Context);
                EmitBinary(BinaryOperation::LogicalAnd, {language::TypeKind::Boolean, {}}, Value.Span, Context);

                EmitLoad(Context, {SlotScope::Local, StepSlot}, Value.Span, Value.Expressions[2]->ResolvedType);
                EmitInteger(Context, 0, Value.Span);
                EmitBinary(BinaryOperation::Less, {language::TypeKind::Boolean, {}}, Value.Span, Context);
                EmitLoad(Context, Variable, Value.Span, VariableType);
                EmitLoad(Context, {SlotScope::Local, EndSlot}, Value.Span, Value.Expressions[1]->ResolvedType);
                EmitBinary(BinaryOperation::GreaterEqual, {language::TypeKind::Boolean, {}}, Value.Span, Context);
                EmitBinary(BinaryOperation::LogicalAnd, {language::TypeKind::Boolean, {}}, Value.Span, Context);
                EmitBinary(BinaryOperation::LogicalOr, {language::TypeKind::Boolean, {}}, Value.Span, Context);
                const std::size_t Exit = EmitJump(Context, OpCode::JumpIfFalse, Value.Span);

                Context.LoopExits.emplace_back();
                CompileStatements(Value.Body, Context);
                EmitLoad(Context, Variable, Value.Span, VariableType);
                EmitLoad(Context, {SlotScope::Local, StepSlot}, Value.Span, Value.Expressions[2]->ResolvedType);
                EmitBinary(BinaryOperation::Add, VariableType, Value.Span, Context);
                EmitStore(Context, Variable, Value.Span, VariableType);
                EmitJumpTo(Context, Begin, Value.Span);
                PatchLoop(Context, Exit);
            }

            void CompileWhile(const language::Statement& Value, Context& Context)
            {
                const std::size_t Begin = Context.Output->Code.size();
                CompileExpression(*Value.Expressions.front(), Context);
                const std::size_t Exit = EmitJump(Context, OpCode::JumpIfFalse, Value.Span);
                Context.LoopExits.emplace_back();
                CompileStatements(Value.Body, Context);
                EmitJumpTo(Context, Begin, Value.Span);
                PatchLoop(Context, Exit);
            }

            void CompileRepeat(const language::Statement& Value, Context& Context)
            {
                const std::size_t Begin = Context.Output->Code.size();
                Context.LoopExits.emplace_back();
                CompileStatements(Value.Body, Context);
                if (!Value.Expressions.empty())
                {
                    CompileExpression(*Value.Expressions.front(), Context);
                    Instruction Jump;
                    Jump.Op = OpCode::JumpIfFalse;
                    Jump.Span = Value.Span;
                    Jump.A = static_cast<std::uint32_t>(Begin);
                    Context.Output->Code.push_back(Jump);
                }
                PatchLoop(Context, std::nullopt);
            }

            void CompileDo(const language::Statement& Value, Context& Context)
            {
                const std::size_t Begin = Context.Output->Code.size();
                Context.LoopExits.emplace_back();
                CompileStatements(Value.Body, Context);
                EmitJumpTo(Context, Begin, Value.Span);
                PatchLoop(Context, std::nullopt);
            }

            void CompileIf(const language::Statement& Value, Context& Context)
            {
                CompileExpression(*Value.Expressions.front(), Context);
                const std::size_t FalseJump = EmitJump(Context, OpCode::JumpIfFalse, Value.Span);
                CompileStatements(Value.Body, Context);
                if (Value.ElseBody.empty())
                {
                    Patch(Context, FalseJump, Context.Output->Code.size());
                    return;
                }
                const std::size_t EndJump = EmitJump(Context, OpCode::Jump, Value.Span);
                Patch(Context, FalseJump, Context.Output->Code.size());
                CompileStatements(Value.ElseBody, Context);
                Patch(Context, EndJump, Context.Output->Code.size());
            }

            void CompileSelect(const language::Statement& Value, Context& Context)
            {
                if (Value.Expressions.empty())
                    return;
                const std::size_t Selector = AddTemporary(Context, Value.Expressions.front()->ResolvedType);
                CompileExpression(*Value.Expressions.front(), Context);
                EmitStore(Context, {SlotScope::Local, Selector}, Value.Span, Value.Expressions.front()->ResolvedType);
                std::vector<std::size_t> EndJumps;
                const language::CaseClause* Default = nullptr;
                for (const language::CaseClause& Clause : Value.Cases)
                {
                    if (Clause.IsDefault)
                    {
                        Default = &Clause;
                        continue;
                    }
                    bool First = true;
                    for (const auto& CaseValue : Clause.Values)
                    {
                        EmitLoad(Context, {SlotScope::Local, Selector}, CaseValue->Span,
                                 Value.Expressions.front()->ResolvedType);
                        CompileExpression(*CaseValue, Context);
                        EmitBinary(BinaryOperation::Equal, {language::TypeKind::Boolean, {}}, CaseValue->Span, Context);
                        if (!First)
                            EmitBinary(BinaryOperation::LogicalOr, {language::TypeKind::Boolean, {}}, CaseValue->Span,
                                       Context);
                        First = false;
                    }
                    const std::size_t Next = EmitJump(Context, OpCode::JumpIfFalse, Clause.Span);
                    CompileStatements(Clause.Body, Context);
                    EndJumps.push_back(EmitJump(Context, OpCode::Jump, Clause.Span));
                    Patch(Context, Next, Context.Output->Code.size());
                }
                if (Default)
                    CompileStatements(Default->Body, Context);
                for (const std::size_t Jump : EndJumps)
                    Patch(Context, Jump, Context.Output->Code.size());
            }

            void CompileExit(const language::Statement& Value, Context& Context)
            {
                if (Context.LoopExits.empty())
                {
                    Error("DBC3003", "EXIT has no enclosing loop", Value.Span);
                    return;
                }
                Context.LoopExits.back().push_back(EmitJump(Context, OpCode::Jump, Value.Span));
            }

            void PatchLoop(Context& Context, std::optional<std::size_t> ConditionExit)
            {
                const std::size_t End = Context.Output->Code.size();
                if (ConditionExit)
                    Patch(Context, *ConditionExit, End);
                for (const std::size_t Jump : Context.LoopExits.back())
                    Patch(Context, Jump, End);
                Context.LoopExits.pop_back();
            }

            void CompileExpression(const language::Expression& Value, Context& Context)
            {
                switch (Value.Kind)
                {
                    case language::ExpressionKind::Error:
                        return;
                    case language::ExpressionKind::IntegerLiteral:
                        EmitInteger(Context, static_cast<std::int64_t>(Value.Number), Value.Span);
                        return;
                    case language::ExpressionKind::RealLiteral:
                    {
                        Instruction Push;
                        Push.Op = OpCode::PushReal;
                        Push.Span = Value.Span;
                        Push.ResultType = Value.ResolvedType;
                        Push.Real = Value.Number;
                        Context.Output->Code.push_back(Push);
                        return;
                    }
                    case language::ExpressionKind::StringLiteral:
                    {
                        Instruction Push;
                        Push.Op = OpCode::PushString;
                        Push.Span = Value.Span;
                        Push.ResultType = Value.ResolvedType;
                        Push.A = static_cast<std::uint32_t>(InternString(Value.Text));
                        Context.Output->Code.push_back(Push);
                        return;
                    }
                    case language::ExpressionKind::Name:
                        if (Value.Reference == language::ReferenceKind::Constant)
                        {
                            const auto Iterator = Constants_.find(Canonical(Value.Text));
                            if (Iterator == Constants_.end())
                                Error("DBC3004", "Constant disappeared after binding", Value.Span);
                            else
                                CompileExpression(*Iterator->second->Value, Context);
                        }
                        else
                        {
                            CompileLoad(Value, Context);
                        }
                        return;
                    case language::ExpressionKind::Unary:
                        CompileExpression(*Value.Children.front(), Context);
                        EmitUnary(Value, Context);
                        return;
                    case language::ExpressionKind::Binary:
                        CompileExpression(*Value.Children[0], Context);
                        CompileExpression(*Value.Children[1], Context);
                        EmitBinary(BinaryFromText(Value.Text), Value.ResolvedType, Value.Span, Context);
                        return;
                    case language::ExpressionKind::CallOrIndex:
                        Error("DBC3005", "Unresolved call or array index reached lowering", Value.Span);
                        return;
                    case language::ExpressionKind::Call:
                        CompileCall(Value, Context);
                        return;
                    case language::ExpressionKind::Index:
                        CompileArrayIndices(Value, Context);
                        EmitArray(Context, OpCode::LoadArray, Value);
                        return;
                    case language::ExpressionKind::Member:
                        CompileExpression(*Value.Children.front(), Context);
                        EmitField(Context, OpCode::LoadField, Value);
                        return;
                }
            }

            void CompileCall(const language::Expression& Value, Context& Context)
            {
                if (Value.Reference == language::ReferenceKind::BuiltinFunction && Value.Builtin &&
                    language::ConventionFor(*Value.Builtin) == language::BuiltinConvention::ArrayCollection)
                {
                    if (Value.Children.empty() || Value.Children.front()->Kind != language::ExpressionKind::Index ||
                        !Value.Overload)
                    {
                        Error("DBC3006", "Array collection function requires an array", Value.Span);
                        return;
                    }
                    const language::Expression& Array = *Value.Children.front();
                    const SlotReference Slot = ResolveArraySlot(Array, Context);
                    for (std::size_t Index = 1; Index < Value.Children.size(); ++Index)
                        CompileExpression(*Value.Children[Index], Context);
                    Instruction Call;
                    Call.Op = OpCode::CallBuiltinArray;
                    Call.Span = Value.Span;
                    Call.ResultType = Value.ResolvedType;
                    Call.A = static_cast<std::uint32_t>(*Value.Builtin);
                    Call.B = static_cast<std::uint32_t>(Slot.Index);
                    Call.C = static_cast<std::uint32_t>(Slot.Scope);
                    Call.D = static_cast<std::uint32_t>(Value.Children.size());
                    Call.E = Value.Overload->Value;
                    Context.Output->Code.push_back(Call);
                    return;
                }
                for (const auto& Argument : Value.Children)
                    CompileExpression(*Argument, Context);
                Instruction Call;
                Call.Span = Value.Span;
                Call.ResultType = Value.ResolvedType;
                Call.B = static_cast<std::uint32_t>(Value.Children.size());
                Call.D = Call.B;
                if (Value.Reference == language::ReferenceKind::BuiltinFunction && Value.Builtin)
                {
                    if (!Value.Overload)
                    {
                        Error("DBC3006", "Builtin function has no selected overload", Value.Span);
                        return;
                    }
                    Call.Op = OpCode::CallBuiltin;
                    Call.A = static_cast<std::uint32_t>(*Value.Builtin);
                    Call.C = 1;
                    Call.E = Value.Overload->Value;
                }
                else
                {
                    Call.Op = OpCode::CallFunction;
                    const auto Iterator = FunctionIndices_.find(Canonical(Value.Text));
                    if (Iterator == FunctionIndices_.end())
                    {
                        Error("DBC3006", "Unknown bound function during lowering", Value.Span);
                        return;
                    }
                    Call.A = static_cast<std::uint32_t>(Iterator->second);
                }
                Context.Output->Code.push_back(Call);
            }

            void CompileLoad(const language::Expression& Value, Context& Context)
            {
                if (Value.Kind == language::ExpressionKind::Name)
                {
                    const SlotReference Slot = ResolveSlot(Value.Text, Value.ResolvedType, 0, Context);
                    EmitLoad(Context, Slot, Value.Span, Value.ResolvedType);
                    return;
                }
                if (Value.Kind == language::ExpressionKind::Index)
                {
                    CompileArrayIndices(Value, Context);
                    EmitArray(Context, OpCode::LoadArray, Value);
                    return;
                }
                if (Value.Kind == language::ExpressionKind::Member)
                {
                    CompileExpression(Value, Context);
                    return;
                }
                Error("DBC3007", "Expression is not loadable", Value.Span);
            }

            void CompileStore(const language::Expression& Value, Context& Context)
            {
                if (Value.Kind == language::ExpressionKind::Name)
                {
                    EmitStore(Context, ResolveSlot(Value.Text, Value.ResolvedType, 0, Context), Value.Span,
                              Value.ResolvedType);
                    return;
                }
                if (Value.Kind == language::ExpressionKind::Index)
                {
                    EmitArray(Context, OpCode::StoreArray, Value);
                    return;
                }
                Error("DBC3008", "Expression is not directly storable", Value.Span);
            }

            void CompileArrayIndices(const language::Expression& Value, Context& Context)
            {
                for (const auto& Index : Value.Children)
                    CompileExpression(*Index, Context);
            }

            void EmitArray(Context& Context, OpCode Op, const language::Expression& Value)
            {
                const SlotReference Slot = ResolveSlot(Value.Text, Value.ResolvedType, Value.ArrayRank, Context);
                Instruction Result;
                Result.Op = Op;
                Result.Span = Value.Span;
                Result.ResultType = Value.ResolvedType;
                Result.A = static_cast<std::uint32_t>(Slot.Index);
                Result.B = static_cast<std::uint32_t>(Value.ArrayRank);
                Result.C = static_cast<std::uint32_t>(Slot.Scope);
                Result.D = Value.Children.empty() ? 1U : 0U;
                Context.Output->Code.push_back(Result);
            }

            void EmitDuplicate(Context& Context, std::size_t Count, language::SourceSpan Span)
            {
                Instruction Result;
                Result.Op = OpCode::Duplicate;
                Result.Span = Span;
                Result.A = static_cast<std::uint32_t>(Count);
                Context.Output->Code.push_back(Result);
            }

            void EmitField(Context& Context, OpCode Op, const language::Expression& Value)
            {
                Instruction Result;
                Result.Op = Op;
                Result.Span = Value.Span;
                Result.ResultType = Value.ResolvedType;
                Result.A = static_cast<std::uint32_t>(InternString(Canonical(Value.Text)));
                Context.Output->Code.push_back(Result);
            }

            void EmitUnary(const language::Expression& Value, Context& Context)
            {
                Instruction Result;
                Result.Op = OpCode::Unary;
                Result.Span = Value.Span;
                Result.ResultType = Value.ResolvedType;
                Result.A = static_cast<std::uint32_t>(Value.Text == "-"        ? UnaryOperation::Negative
                                                      : Value.Text == "NOT"    ? UnaryOperation::LogicalNot
                                                      : Value.Text == "BITNOT" ? UnaryOperation::BitNot
                                                                               : UnaryOperation::Positive);
                Context.Output->Code.push_back(Result);
            }

            void EmitBinary(BinaryOperation Operation, language::Type Type, language::SourceSpan Span, Context& Context)
            {
                Instruction Result;
                Result.Op = OpCode::Binary;
                Result.Span = Span;
                Result.ResultType = std::move(Type);
                Result.A = static_cast<std::uint32_t>(Operation);
                Context.Output->Code.push_back(Result);
            }

            static BinaryOperation BinaryFromText(std::string_view Text)
            {
                if (Text == "+")
                    return BinaryOperation::Add;
                if (Text == "-")
                    return BinaryOperation::Subtract;
                if (Text == "*")
                    return BinaryOperation::Multiply;
                if (Text == "/")
                    return BinaryOperation::Divide;
                if (Text == "MOD")
                    return BinaryOperation::Modulo;
                if (Text == "^")
                    return BinaryOperation::Power;
                if (Text == "=")
                    return BinaryOperation::Equal;
                if (Text == "<>")
                    return BinaryOperation::NotEqual;
                if (Text == "<")
                    return BinaryOperation::Less;
                if (Text == "<=")
                    return BinaryOperation::LessEqual;
                if (Text == ">")
                    return BinaryOperation::Greater;
                if (Text == ">=")
                    return BinaryOperation::GreaterEqual;
                if (Text == "AND")
                    return BinaryOperation::LogicalAnd;
                if (Text == "OR")
                    return BinaryOperation::LogicalOr;
                if (Text == "<<")
                    return BinaryOperation::ShiftLeft;
                if (Text == ">>")
                    return BinaryOperation::ShiftRight;
                if (Text == "&&")
                    return BinaryOperation::BitAnd;
                if (Text == "||")
                    return BinaryOperation::BitOr;
                if (Text == "~~" || Text == "XOR")
                    return BinaryOperation::BitXor;
                assert(false && "Unknown bound binary operator");
                return BinaryOperation::Add;
            }

            // Arrays and scalars are distinct variables even when spelled the same; arrays are keyed `NAME()`.
            static std::string SlotKey(const std::string& Name, std::size_t Rank)
            {
                return Rank != 0 ? Name + "()" : Name;
            }

            SlotReference ResolveSlot(std::string Name, const language::Type& Type, std::size_t Rank, Context& Context)
            {
                Name = Canonical(Name);
                const std::string Key = SlotKey(Name, Rank);
                if (const auto Local = Context.Locals.find(Key); Local != Context.Locals.end())
                    return {SlotScope::Local, Local->second};
                return {SlotScope::Global, AddGlobal(Name, Type, Rank)};
            }

            // Array commands name the array with free marker indices, so the rank comes from the declaration.
            SlotReference ResolveArraySlot(const language::Expression& Array, Context& Context)
            {
                const std::string Name = Canonical(Array.Text);
                const std::string Key = SlotKey(Name, 1);
                if (const auto Local = Context.Locals.find(Key); Local != Context.Locals.end())
                    return {SlotScope::Local, Local->second};
                if (const auto Global = Globals_.find(Key); Global != Globals_.end())
                    return {SlotScope::Global, Global->second};
                return {SlotScope::Global,
                        AddGlobal(Name, Array.ResolvedType, std::max<std::size_t>(1, Array.Children.size()))};
            }

            static language::Type ImplicitType(std::string_view Name)
            {
                if (!Name.empty() && Name.back() == '$')
                    return {language::TypeKind::String, {}};
                if (!Name.empty() && Name.back() == '#')
                    return {language::TypeKind::Real, {}};
                return {language::TypeKind::Integer, {}};
            }

            const language::Type& SlotType(SlotReference Slot, const Context& Context) const
            {
                return Slot.Scope == SlotScope::Global ? Module_->Globals[Slot.Index].ValueType
                                                       : Context.Output->Locals[Slot.Index].ValueType;
            }

            std::size_t SlotRank(SlotReference Slot, const Context& Context) const
            {
                return Slot.Scope == SlotScope::Global ? Module_->Globals[Slot.Index].ArrayRank
                                                       : Context.Output->Locals[Slot.Index].ArrayRank;
            }

            std::size_t AddGlobal(std::string Name, const language::Type& Type, std::size_t Rank)
            {
                Name = Canonical(Name);
                const std::string Key = SlotKey(Name, Rank);
                if (const auto Iterator = Globals_.find(Key); Iterator != Globals_.end())
                    return Iterator->second;
                const std::size_t Index = Module_->Globals.size();
                Module_->Globals.push_back({Key, Type, Rank});
                Globals_.emplace(Key, Index);
                return Index;
            }

            std::size_t AddLocal(Context& Context, std::string Name, const language::Type& Type, std::size_t Rank)
            {
                Name = Canonical(Name);
                const std::string Key = SlotKey(Name, Rank);
                if (const auto Iterator = Context.Locals.find(Key); Iterator != Context.Locals.end())
                    return Iterator->second;
                const std::size_t Index = Context.Output->Locals.size();
                Context.Output->Locals.push_back({Key, Type, Rank});
                Context.Locals.emplace(Key, Index);
                return Index;
            }

            std::size_t AddTemporary(Context& Context, const language::Type& Type)
            {
                const std::size_t Index = Context.Output->Locals.size();
                Context.Output->Locals.push_back({"$TEMP" + std::to_string(Index), Type, 0});
                return Index;
            }

            std::size_t InternString(const std::string& Text)
            {
                const auto Iterator = std::find(Module_->Strings.begin(), Module_->Strings.end(), Text);
                if (Iterator != Module_->Strings.end())
                    return static_cast<std::size_t>(std::distance(Module_->Strings.begin(), Iterator));
                Module_->Strings.push_back(Text);
                return Module_->Strings.size() - 1;
            }

            std::size_t Emit(Context& Context, OpCode Op, language::SourceSpan Span, language::Type Type)
            {
                Instruction Result;
                Result.Op = Op;
                Result.Span = Span;
                Result.ResultType = std::move(Type);
                Context.Output->Code.push_back(Result);
                return Context.Output->Code.size() - 1;
            }

            void EmitInteger(Context& Context, std::int64_t Value, language::SourceSpan Span)
            {
                Instruction Result;
                Result.Op = OpCode::PushInteger;
                Result.Span = Span;
                Result.ResultType = {language::TypeKind::Integer, {}};
                Result.Integer = Value;
                Context.Output->Code.push_back(Result);
            }

            void EmitDefault(const language::Type& Type, language::SourceSpan Span, Context& Context)
            {
                if (Type.Kind == language::TypeKind::String)
                {
                    Instruction Result;
                    Result.Op = OpCode::PushString;
                    Result.Span = Span;
                    Result.ResultType = Type;
                    Result.A = static_cast<std::uint32_t>(InternString(""));
                    Context.Output->Code.push_back(Result);
                }
                else if (Type.Kind == language::TypeKind::Real)
                {
                    Instruction Result;
                    Result.Op = OpCode::PushReal;
                    Result.Span = Span;
                    Result.ResultType = Type;
                    Context.Output->Code.push_back(Result);
                }
                else if (Type.Kind == language::TypeKind::UserDefined)
                {
                    const std::size_t Temporary = AddTemporary(Context, Type);
                    EmitLoad(Context, {SlotScope::Local, Temporary}, Span, Type);
                }
                else
                {
                    EmitInteger(Context, 0, Span);
                }
            }

            void EmitLoad(Context& Context, SlotReference Slot, language::SourceSpan Span, const language::Type& Type)
            {
                Instruction Result;
                Result.Op = Slot.Scope == SlotScope::Global ? OpCode::LoadGlobal : OpCode::LoadLocal;
                Result.Span = Span;
                Result.ResultType = Type;
                Result.A = static_cast<std::uint32_t>(Slot.Index);
                Context.Output->Code.push_back(Result);
            }

            void EmitStore(Context& Context, SlotReference Slot, language::SourceSpan Span, const language::Type& Type)
            {
                Instruction Result;
                Result.Op = Slot.Scope == SlotScope::Global ? OpCode::StoreGlobal : OpCode::StoreLocal;
                Result.Span = Span;
                Result.ResultType = Type;
                Result.A = static_cast<std::uint32_t>(Slot.Index);
                Context.Output->Code.push_back(Result);
            }

            std::size_t EmitJump(Context& Context, OpCode Op, language::SourceSpan Span)
            {
                return Emit(Context, Op, Span, {language::TypeKind::Void, {}});
            }

            void EmitJumpTo(Context& Context, std::size_t Target, language::SourceSpan Span)
            {
                const std::size_t Index = EmitJump(Context, OpCode::Jump, Span);
                Patch(Context, Index, Target);
            }

            static void Patch(Context& Context, std::size_t Instruction, std::size_t Target)
            {
                Context.Output->Code[Instruction].A = static_cast<std::uint32_t>(Target);
            }

            void PatchJumps(Context& Context)
            {
                for (const auto& [Instruction, Name] : Context.Jumps)
                {
                    const auto Iterator = Context.Labels.find(Name);
                    if (Iterator == Context.Labels.end())
                        Error("DBC3009", "Label disappeared after binding", Context.Output->Code[Instruction].Span);
                    else
                        Patch(Context, Instruction, Iterator->second);
                }
            }

            void Error(std::string Code, std::string Message, language::SourceSpan Span)
            {
                Diagnostics_.push_back({std::move(Code),
                                        language::DiagnosticSeverity::Error,
                                        std::move(Message),
                                        Span,
                                        {},
                                        !Span.LogicalPath.empty() ? Span.LogicalPath
                                                                  : (Program_.Source ? Program_.Source->LogicalPath
                                                                                     : std::filesystem::path{})});
            }

            const language::Program& Program_;
            std::shared_ptr<Module> Module_{};
            std::vector<language::Diagnostic> Diagnostics_{};
            std::unordered_map<std::string, std::size_t> Globals_{};
            std::unordered_map<std::string, std::size_t> FunctionIndices_{};
            std::unordered_map<std::string, const language::ConstantDeclaration*> Constants_{};
            std::unordered_map<std::string, std::size_t> DataLabelIndices_{};
        };
    } // namespace

    bool CompileResult::Success() const
    {
        return Bytecode != nullptr && Diagnostics.empty();
    }

    CompileResult Lower(const language::Program& Program)
    {
        return Compiler(Program).Run();
    }
} // namespace dbc::vm
