#include "verifier.hpp"
#include "path_utf8.hpp"

#include "language/language_catalogue.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dbc::vm
{
    namespace
    {
        using Stack = std::vector<language::Type>;
        using BuiltinMarker = std::pair<language::SymbolId, language::OverloadId>;
        using BuiltinMarkerStack = std::vector<BuiltinMarker>;

        std::string Canonical(std::string_view Value)
        {
            std::string Result(Value);
            std::transform(Result.begin(), Result.end(), Result.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
            return Result;
        }

        std::string TypeName(const language::Type& Value)
        {
            switch (Value.Kind)
            {
                case language::TypeKind::Error:
                    return "<error>";
                case language::TypeKind::Unresolved:
                    return "<unresolved>";
                case language::TypeKind::Void:
                    return "void";
                case language::TypeKind::Boolean:
                    return "boolean";
                case language::TypeKind::Integer:
                    return "integer";
                case language::TypeKind::Real:
                    return "real";
                case language::TypeKind::String:
                    return "string";
                case language::TypeKind::UserDefined:
                    return Value.Name.empty() ? "<unnamed TYPE>" : Value.Name;
            }
            return "<invalid>";
        }

        bool CanConvert(const language::Type& Source, const language::Type& Target)
        {
            return Source == Target || (Source.IsNumeric() && Target.IsNumeric());
        }

        bool MatchesBuiltinType(language::BuiltinValueType Expected, const language::Type& Actual)
        {
            switch (Expected)
            {
                case language::BuiltinValueType::Any:
                    return Actual.Kind != language::TypeKind::Void;
                case language::BuiltinValueType::Numeric:
                case language::BuiltinValueType::Real:
                    return Actual.IsNumeric();
                case language::BuiltinValueType::Integer:
                    return Actual.Kind == language::TypeKind::Integer || Actual.Kind == language::TypeKind::Boolean;
                case language::BuiltinValueType::String:
                    return Actual.Kind == language::TypeKind::String;
                case language::BuiltinValueType::Void:
                    return Actual.Kind == language::TypeKind::Void;
            }
            return false;
        }

        bool IsComparison(BinaryOperation Value)
        {
            return Value == BinaryOperation::Equal || Value == BinaryOperation::NotEqual ||
                   Value == BinaryOperation::Less || Value == BinaryOperation::LessEqual ||
                   Value == BinaryOperation::Greater || Value == BinaryOperation::GreaterEqual;
        }

        bool IsLogical(BinaryOperation Value)
        {
            return Value == BinaryOperation::LogicalAnd || Value == BinaryOperation::LogicalOr;
        }

        class Verifier
        {
          public:
            explicit Verifier(const Module& Value) : Module_(Value) {}

            VerificationResult Run()
            {
                ValidateModule();
                for (std::size_t Index = 0; Index < Module_.Functions.size(); ++Index)
                {
                    ValidateFunction(Index);
                }
                std::stable_sort(Result_.Diagnostics.begin(), Result_.Diagnostics.end(),
                                 [](const language::Diagnostic& Left, const language::Diagnostic& Right)
                                 {
                                     if (Left.LogicalPath != Right.LogicalPath)
                                         return dbc::GenericPathToUtf8(Left.LogicalPath) <
                                                dbc::GenericPathToUtf8(Right.LogicalPath);
                                     if (Left.Span.Begin.Offset != Right.Span.Begin.Offset)
                                         return Left.Span.Begin.Offset < Right.Span.Begin.Offset;
                                     if (Left.Code != Right.Code)
                                         return Left.Code < Right.Code;
                                     return Left.Message < Right.Message;
                                 });
                return std::move(Result_);
            }

          private:
            struct FunctionState
            {
                const Function* Value = nullptr;
                std::size_t Index = 0;
                std::vector<bool> StructurallyValid{};
                std::vector<std::optional<Stack>> Inputs{};
                std::vector<std::optional<BuiltinMarkerStack>> MarkerInputs{};
                std::vector<bool> JoinReported{};
            };

            void Error(std::string Code, std::string Message, language::SourceSpan Span = {})
            {
                Result_.Diagnostics.push_back({std::move(Code),
                                               language::DiagnosticSeverity::Error,
                                               std::move(Message),
                                               Span,
                                               {},
                                               Span.LogicalPath.empty() ? Module_.LogicalPath : Span.LogicalPath});
            }

            std::string At(const FunctionState& State, std::size_t InstructionIndex) const
            {
                return "Function '" + State.Value->Name + "', instruction " + std::to_string(InstructionIndex) + " (" +
                       std::string(OpCodeName(State.Value->Code[InstructionIndex].Op)) + ")";
            }

            bool ValidProfile() const
            {
                return Module_.Profile == language::LanguageProfile::Dbc ||
                       Module_.Profile == language::LanguageProfile::DbPro;
            }

            bool TypeKindValid(language::TypeKind Kind, bool AllowVoid) const
            {
                switch (Kind)
                {
                    case language::TypeKind::Boolean:
                    case language::TypeKind::Integer:
                    case language::TypeKind::Real:
                    case language::TypeKind::String:
                    case language::TypeKind::UserDefined:
                        return true;
                    case language::TypeKind::Void:
                        return AllowVoid;
                    case language::TypeKind::Error:
                    case language::TypeKind::Unresolved:
                        return false;
                }
                return false;
            }

            bool ValidateType(const language::Type& Value, bool AllowVoid, std::string_view Context,
                              language::SourceSpan Span = {})
            {
                if (!TypeKindValid(Value.Kind, AllowVoid))
                {
                    Error("DBC4001", std::string(Context) + " has invalid type " + TypeName(Value), Span);
                    return false;
                }
                if (Value.Kind == language::TypeKind::UserDefined)
                {
                    if (Value.Name.empty())
                    {
                        Error("DBC4001", std::string(Context) + " refers to an unnamed TYPE", Span);
                        return false;
                    }
                    if (!TypeIndices_.contains(Canonical(Value.Name)))
                    {
                        Error("DBC4001", std::string(Context) + " refers to unknown TYPE '" + Value.Name + "'", Span);
                        return false;
                    }
                }
                return true;
            }

            void ValidateModule()
            {
                if (!ValidProfile())
                {
                    Error("DBC4000", "Bytecode module has an invalid language profile");
                }

                for (std::size_t Index = 0; Index < Module_.Types.size(); ++Index)
                {
                    const TypeLayout& Layout = Module_.Types[Index];
                    const std::string Name = Canonical(Layout.Name);
                    if (Name.empty())
                    {
                        Error("DBC4001", "TYPE layout " + std::to_string(Index) + " has no name");
                        continue;
                    }
                    if (!TypeIndices_.emplace(Name, Index).second)
                    {
                        Error("DBC4001", "Duplicate TYPE layout '" + Name + "'");
                    }
                }

                for (const TypeLayout& Layout : Module_.Types)
                {
                    std::unordered_set<std::string> Fields;
                    for (const TypeField& Field : Layout.Fields)
                    {
                        const std::string Name = Canonical(Field.Name);
                        if (Name.empty())
                            Error("DBC4001", "TYPE '" + Layout.Name + "' has an unnamed field");
                        else if (!Fields.insert(Name).second)
                            Error("DBC4001", "TYPE '" + Layout.Name + "' has duplicate field '" + Name + "'");
                        ValidateType(Field.ValueType, false,
                                     "Field '" + Layout.Name + "." + (Name.empty() ? std::string("<unnamed>") : Name) +
                                         "'");
                    }
                }

                ValidateSlots(Module_.Globals, "global");
                for (std::size_t Index = 0; Index < Module_.Data.size(); ++Index)
                {
                    const DataValue& Value = Module_.Data[Index];
                    if (!ValidateType(Value.ValueType, false, "DATA item " + std::to_string(Index)))
                        continue;
                    if (Value.ValueType.Kind != language::TypeKind::Integer &&
                        Value.ValueType.Kind != language::TypeKind::Real &&
                        Value.ValueType.Kind != language::TypeKind::String)
                    {
                        Error("DBC4001", "DATA item " + std::to_string(Index) + " is not scalar");
                    }
                    if (Value.ValueType.Kind == language::TypeKind::String &&
                        Value.StringIndex >= Module_.Strings.size())
                    {
                        Error("DBC4002", "DATA item " + std::to_string(Index) + " references string " +
                                             std::to_string(Value.StringIndex) + " outside the string table");
                    }
                }
                std::unordered_set<std::string> DataLabels;
                for (const auto& [RawName, Index] : Module_.DataLabels)
                {
                    const std::string Name = Canonical(RawName);
                    if (Name.empty())
                        Error("DBC4001", "DATA label has no name");
                    else if (!DataLabels.insert(Name).second)
                        Error("DBC4001", "Duplicate DATA label '" + Name + "'");
                    if (Index > Module_.Data.size())
                        Error("DBC4002", "DATA label '" + Name + "' points outside the DATA pool");
                }

                if (Module_.Functions.empty())
                {
                    Error("DBC4000", "Bytecode module contains no functions");
                    return;
                }
                if (Module_.EntryFunction >= Module_.Functions.size())
                {
                    Error("DBC4000", "Entry function index " + std::to_string(Module_.EntryFunction) +
                                         " is outside the function table");
                }

                std::unordered_set<std::string> FunctionNames;
                for (std::size_t Index = 0; Index < Module_.Functions.size(); ++Index)
                {
                    const Function& Value = Module_.Functions[Index];
                    const std::string Name = Canonical(Value.Name);
                    if (Name.empty())
                        Error("DBC4001", "Function " + std::to_string(Index) + " has no name");
                    else if (!FunctionNames.insert(Name).second)
                        Error("DBC4001", "Duplicate function '" + Name + "'");

                    ValidateType(Value.ReturnType, true, "Return type of function '" + Value.Name + "'");
                    if (Value.ParameterCount > Value.Locals.size())
                    {
                        Error("DBC4000", "Function '" + Value.Name + "' declares " +
                                             std::to_string(Value.ParameterCount) + " parameters but has only " +
                                             std::to_string(Value.Locals.size()) + " local slots");
                    }
                    ValidateSlots(Value.Locals, "local in function '" + Value.Name + "'");
                }

                if (Module_.EntryFunction < Module_.Functions.size())
                {
                    const Function& Entry = Module_.Functions[Module_.EntryFunction];
                    if (Entry.ParameterCount != 0 || Entry.ReturnType.Kind != language::TypeKind::Void)
                    {
                        Error("DBC4000", "Entry function must take no parameters and return void");
                    }
                }
            }

            void ValidateSlots(const std::vector<Slot>& Slots, std::string_view Description)
            {
                std::unordered_set<std::string> Names;
                for (std::size_t Index = 0; Index < Slots.size(); ++Index)
                {
                    const Slot& Value = Slots[Index];
                    const std::string Name = Canonical(Value.Name);
                    if (Name.empty())
                    {
                        Error("DBC4001", std::string(Description) + " slot " + std::to_string(Index) + " has no name");
                    }
                    else if (!Names.insert(Name).second)
                    {
                        Error("DBC4001", "Duplicate " + std::string(Description) + " slot '" + Name + "'");
                    }
                    ValidateType(Value.ValueType, false,
                                 std::string(Description) + " slot '" +
                                     (Name.empty() ? std::string("<unnamed>") : Name) + "'");
                }
            }

            void ValidateFunction(std::size_t FunctionIndex)
            {
                FunctionState State;
                State.Value = &Module_.Functions[FunctionIndex];
                State.Index = FunctionIndex;
                State.StructurallyValid.assign(State.Value->Code.size(), true);
                State.Inputs.resize(State.Value->Code.size());
                State.MarkerInputs.resize(State.Value->Code.size());
                State.JoinReported.assign(State.Value->Code.size(), false);

                if (State.Value->Code.empty())
                {
                    Error("DBC4000", "Function '" + State.Value->Name + "' has no bytecode");
                    return;
                }

                for (std::size_t Index = 0; Index < State.Value->Code.size(); ++Index)
                {
                    State.StructurallyValid[Index] = ValidateInstruction(State, Index);
                }

                State.Inputs.front() = Stack{};
                State.MarkerInputs.front() = BuiltinMarkerStack{};
                std::deque<std::size_t> Worklist{0};
                while (!Worklist.empty())
                {
                    const std::size_t Index = Worklist.front();
                    Worklist.pop_front();
                    if (!State.Inputs[Index] || !State.StructurallyValid[Index])
                        continue;
                    Transfer(State, Index, *State.Inputs[Index], *State.MarkerInputs[Index], Worklist);
                }
            }

            bool ValidateInstruction(const FunctionState& State, std::size_t Index)
            {
                const Instruction& Value = State.Value->Code[Index];
                const std::string Prefix = At(State, Index);
                switch (Value.Op)
                {
                    case OpCode::PushInteger:
                        return RequireResultType(Value, {language::TypeKind::Integer, {}}, Prefix);
                    case OpCode::PushReal:
                        return RequireResultType(Value, {language::TypeKind::Real, {}}, Prefix);
                    case OpCode::PushString:
                        return RequireResultType(Value, {language::TypeKind::String, {}}, Prefix) &&
                               ValidateStringIndex(Value.A, Prefix, Value.Span);
                    case OpCode::LoadGlobal:
                    case OpCode::StoreGlobal:
                        return ValidateDirectSlot(State, Value, SlotScope::Global, Prefix);
                    case OpCode::LoadLocal:
                    case OpCode::StoreLocal:
                        return ValidateDirectSlot(State, Value, SlotScope::Local, Prefix);
                    case OpCode::CreateArray:
                    case OpCode::LoadArray:
                    case OpCode::StoreArray:
                    case OpCode::DestroyArray:
                        return ValidateArrayInstruction(State, Value, Prefix);
                    case OpCode::LoadField:
                    case OpCode::StoreField:
                        return ValidateStringIndex(Value.A, Prefix, Value.Span) &&
                               ValidateType(Value.ResultType, false, Prefix + " result", Value.Span);
                    case OpCode::BeginBuiltin:
                    case OpCode::CompleteBuiltin:
                        return ValidateVmBuiltinMarker(Value, Prefix);
                    case OpCode::ReadData:
                        return ValidateType(Value.ResultType, false, Prefix + " result", Value.Span);
                    case OpCode::RestoreData:
                        if (Value.A > Module_.Data.size())
                        {
                            Error("DBC4002", Prefix + " points outside the DATA pool", Value.Span);
                            return false;
                        }
                        return RequireResultType(Value, {language::TypeKind::Void, {}}, Prefix);
                    case OpCode::Unary:
                        if (Value.A > static_cast<std::uint32_t>(UnaryOperation::BitNot))
                        {
                            Error("DBC4002", Prefix + " has invalid unary operation " + std::to_string(Value.A),
                                  Value.Span);
                            return false;
                        }
                        return ValidateType(Value.ResultType, false, Prefix + " result", Value.Span);
                    case OpCode::Binary:
                        if (Value.A > static_cast<std::uint32_t>(BinaryOperation::BitXor))
                        {
                            Error("DBC4002", Prefix + " has invalid binary operation " + std::to_string(Value.A),
                                  Value.Span);
                            return false;
                        }
                        return ValidateType(Value.ResultType, false, Prefix + " result", Value.Span);
                    case OpCode::CallBuiltin:
                        return ValidateBuiltin(Value, Prefix);
                    case OpCode::CallBuiltinArray:
                        return ValidateBuiltinArray(State, Value, Prefix);
                    case OpCode::CallFunction:
                        return ValidateFunctionCall(Value, Prefix);
                    case OpCode::Duplicate:
                        if (Value.A == 0)
                        {
                            Error("DBC4002", Prefix + " must duplicate at least one stack value", Value.Span);
                            return false;
                        }
                        return true;
                    case OpCode::Pop:
                        return true;
                    case OpCode::Jump:
                    case OpCode::JumpIfFalse:
                    case OpCode::CallLabel:
                        return ValidateJump(State, Index, Value, Prefix);
                    case OpCode::ReturnSubroutine:
                    case OpCode::Halt:
                        return true;
                    case OpCode::Return:
                        return ValidateReturnInstruction(State, Value, Prefix);
                }
                Error("DBC4009",
                      "Function '" + State.Value->Name + "', instruction " + std::to_string(Index) +
                          " has invalid opcode " + std::to_string(static_cast<unsigned int>(Value.Op)),
                      Value.Span);
                return false;
            }

            bool RequireResultType(const Instruction& Value, const language::Type& Expected, const std::string& Prefix)
            {
                if (Value.ResultType == Expected)
                    return true;
                Error("DBC4005",
                      Prefix + " declares result type " + TypeName(Value.ResultType) + ", expected " +
                          TypeName(Expected),
                      Value.Span);
                return false;
            }

            bool ValidateStringIndex(std::uint32_t Index, const std::string& Prefix, language::SourceSpan Span)
            {
                if (Index < Module_.Strings.size())
                    return true;
                Error("DBC4002", Prefix + " references string " + std::to_string(Index) + " outside the string table",
                      Span);
                return false;
            }

            const Slot* ResolveSlot(const FunctionState& State, SlotScope Scope, std::uint32_t Index,
                                    const std::string& Prefix, language::SourceSpan Span)
            {
                const std::vector<Slot>& Slots = Scope == SlotScope::Global ? Module_.Globals : State.Value->Locals;
                if (Index < Slots.size())
                    return &Slots[Index];
                Error("DBC4002",
                      Prefix + " references " + std::string(Scope == SlotScope::Global ? "global" : "local") +
                          " slot " + std::to_string(Index) + " outside its slot table",
                      Span);
                return nullptr;
            }

            bool DecodeScope(std::uint32_t Encoded, SlotScope& Scope, const std::string& Prefix,
                             language::SourceSpan Span)
            {
                if (Encoded == static_cast<std::uint32_t>(SlotScope::Global))
                {
                    Scope = SlotScope::Global;
                    return true;
                }
                if (Encoded == static_cast<std::uint32_t>(SlotScope::Local))
                {
                    Scope = SlotScope::Local;
                    return true;
                }
                Error("DBC4002", Prefix + " has invalid slot scope " + std::to_string(Encoded), Span);
                return false;
            }

            bool ValidateDirectSlot(const FunctionState& State, const Instruction& Value, SlotScope Scope,
                                    const std::string& Prefix)
            {
                const Slot* Target = ResolveSlot(State, Scope, Value.A, Prefix, Value.Span);
                if (!Target)
                    return false;
                if (Target->ArrayRank != 0)
                {
                    Error("DBC4002", Prefix + " addresses array slot '" + Target->Name + "' as a scalar", Value.Span);
                    return false;
                }
                return RequireResultType(Value, Target->ValueType, Prefix);
            }

            bool ValidateArrayInstruction(const FunctionState& State, const Instruction& Value,
                                          const std::string& Prefix)
            {
                SlotScope Scope{};
                if (!DecodeScope(Value.C, Scope, Prefix, Value.Span))
                    return false;
                const Slot* Target = ResolveSlot(State, Scope, Value.A, Prefix, Value.Span);
                if (!Target)
                    return false;
                bool Valid = true;
                if (Value.D > 1 || (Value.D != 0 && Target->ArrayRank != 1))
                {
                    Error("DBC4002", Prefix + " has an invalid current-index array access", Value.Span);
                    Valid = false;
                }
                if (Value.B == 0 || Target->ArrayRank == 0 || Value.B != Target->ArrayRank)
                {
                    Error("DBC4002",
                          Prefix + " uses rank " + std::to_string(Value.B) + " for array slot '" + Target->Name +
                              "' with rank " + std::to_string(Target->ArrayRank),
                          Value.Span);
                    Valid = false;
                }
                if (!(Value.ResultType == Target->ValueType))
                {
                    Error("DBC4005",
                          Prefix + " declares element type " + TypeName(Value.ResultType) + ", expected " +
                              TypeName(Target->ValueType),
                          Value.Span);
                    Valid = false;
                }
                return Valid;
            }

            bool ValidateJump(const FunctionState& State, std::size_t Index, const Instruction& Value,
                              const std::string& Prefix)
            {
                bool Valid = true;
                if (Value.A >= State.Value->Code.size())
                {
                    Error("DBC4003",
                          Prefix + " targets instruction " + std::to_string(Value.A) + " outside the function",
                          Value.Span);
                    Valid = false;
                }
                if ((Value.Op == OpCode::JumpIfFalse || Value.Op == OpCode::CallLabel) &&
                    Index + 1 >= State.Value->Code.size())
                {
                    Error("DBC4003", Prefix + " has no fallthrough instruction", Value.Span);
                    Valid = false;
                }
                return Valid;
            }

            bool ValidateVmBuiltinMarker(const Instruction& Value, const std::string& Prefix)
            {
                if (Value.A >= static_cast<std::uint32_t>(language::SymbolId::Count))
                {
                    Error("DBC4007", Prefix + " references a symbol outside the language catalogue", Value.Span);
                    return false;
                }
                const auto Id = static_cast<language::SymbolId>(Value.A);
                const language::SymbolDescriptor& Descriptor = language::Symbol(Id);
                const language::BuiltinOverload* Selected = language::Overload({Value.E});
                const language::BuiltinConvention Convention = language::ConventionFor(Id);
                bool Valid = true;
                if (Descriptor.Kind != language::SymbolKind::Command ||
                    (Convention != language::BuiltinConvention::DataRead &&
                     Convention != language::BuiltinConvention::DataRestore &&
                     Convention != language::BuiltinConvention::Undim))
                {
                    Error("DBC4007", Prefix + " does not reference a VM-handled command", Value.Span);
                    Valid = false;
                }
                if (!Selected || Selected->Symbol != Id || Selected->Profile != Module_.Profile ||
                    !Selected->SourceArity.Accepts(static_cast<int>(Value.D)))
                {
                    Error("DBC4007", Prefix + " references an invalid VM-handled builtin overload", Value.Span);
                    Valid = false;
                }
                if (!language::ExecutableIn(Descriptor, Module_.Profile))
                {
                    Error("DBC4007", Prefix + " uses an unavailable or unsupported VM-handled command", Value.Span);
                    Valid = false;
                }
                if (!RequireResultType(Value, {language::TypeKind::Void, {}}, Prefix))
                    Valid = false;
                return Valid;
            }

            bool ValidateBuiltin(const Instruction& Value, const std::string& Prefix)
            {
                if (Value.A >= static_cast<std::uint32_t>(language::SymbolId::Count))
                {
                    Error("DBC4007",
                          Prefix + " references symbol " + std::to_string(Value.A) + " outside the language catalogue",
                          Value.Span);
                    return false;
                }
                if (Value.C > 1)
                {
                    Error("DBC4007", Prefix + " has invalid builtin return flag " + std::to_string(Value.C),
                          Value.Span);
                    return false;
                }

                const auto Id = static_cast<language::SymbolId>(Value.A);
                const language::SymbolDescriptor& Descriptor = language::Symbol(Id);
                const language::BuiltinOverload* Selected = language::Overload({Value.E});
                const language::BuiltinConvention Convention = language::ConventionFor(Id);
                const bool OutputCommand = Convention == language::BuiltinConvention::Input ||
                                           Convention == language::BuiltinConvention::FileRead;
                bool Valid = true;
                if (!Selected || Selected->Symbol != Id || Selected->Profile != Module_.Profile ||
                    !Selected->SourceArity.Accepts(static_cast<int>(Value.D)))
                {
                    Error("DBC4007", Prefix + " references an invalid builtin overload", Value.Span);
                    Valid = false;
                }
                if (Descriptor.Kind != language::SymbolKind::Command &&
                    Descriptor.Kind != language::SymbolKind::Function)
                {
                    Error("DBC4007",
                          Prefix + " uses " + std::string(Descriptor.CanonicalName) + " with the wrong symbol kind",
                          Value.Span);
                    Valid = false;
                }
                if ((Descriptor.Kind == language::SymbolKind::Function && Value.C != 1) ||
                    (Descriptor.Kind == language::SymbolKind::Command && Value.C != (OutputCommand ? 1U : 0U)))
                {
                    Error("DBC4007",
                          Prefix + " has an invalid return convention for '" + std::string(Descriptor.CanonicalName) +
                              "'",
                          Value.Span);
                    Valid = false;
                }
                if (!language::AvailableIn(Descriptor, Module_.Profile))
                {
                    Error("DBC4007",
                          Prefix + " uses symbol '" + std::string(Descriptor.CanonicalName) +
                              "' outside the module profile",
                          Value.Span);
                    Valid = false;
                }
                else if (!language::ExecutableIn(Descriptor, Module_.Profile))
                {
                    Error("DBC4007",
                          Prefix + " uses unsupported symbol '" + std::string(Descriptor.CanonicalName) + "'",
                          Value.Span);
                    Valid = false;
                }
                const std::optional<language::Arity> Arity = language::ArityFor(Descriptor, Module_.Profile);
                if (!Arity || !Arity->Accepts(static_cast<int>(Value.D)))
                {
                    Error("DBC4007",
                          Prefix + " calls '" + std::string(Descriptor.CanonicalName) + "' with " +
                              std::to_string(Value.D) + " source arguments",
                          Value.Span);
                    Valid = false;
                }
                const std::uint32_t ExpectedInputs = OutputCommand && Value.D != 0 ? Value.D - 1U : Value.D;
                if (Value.D == 0 && OutputCommand)
                {
                    Error("DBC4007", Prefix + " output command has no destination", Value.Span);
                    Valid = false;
                }
                else if (Value.B != ExpectedInputs)
                {
                    Error("DBC4007",
                          Prefix + " consumes " + std::to_string(Value.B) + " stack arguments, expected " +
                              std::to_string(ExpectedInputs),
                          Value.Span);
                    Valid = false;
                }
                if (Value.C == 0)
                {
                    if (Value.ResultType.Kind != language::TypeKind::Void)
                    {
                        Error("DBC4005", Prefix + " command call must have void result type", Value.Span);
                        Valid = false;
                    }
                }
                else if (!ValidateType(Value.ResultType, false, Prefix + " result", Value.Span))
                {
                    Valid = false;
                }
                else if (!OutputCommand && Selected && !MatchesBuiltinType(Selected->ReturnType, Value.ResultType))
                {
                    Error("DBC4005",
                          Prefix + " result type " + TypeName(Value.ResultType) +
                              " does not match the selected builtin overload",
                          Value.Span);
                    Valid = false;
                }
                return Valid;
            }

            bool ValidateBuiltinArray(const FunctionState& State, const Instruction& Value, const std::string& Prefix)
            {
                if (Value.A >= static_cast<std::uint32_t>(language::SymbolId::Count))
                {
                    Error("DBC4007", Prefix + " references a symbol outside the language catalogue", Value.Span);
                    return false;
                }
                const auto Id = static_cast<language::SymbolId>(Value.A);
                const language::SymbolDescriptor& Descriptor = language::Symbol(Id);
                const language::BuiltinOverload* Selected = language::Overload({Value.E});
                const language::BuiltinConvention Convention = language::ConventionFor(Id);
                const bool IsLoad = Convention == language::BuiltinConvention::LoadArray;
                const bool IsCollection = Convention == language::BuiltinConvention::ArrayCollection;
                bool Valid = true;
                const int SourceArity = IsCollection ? static_cast<int>(Value.D) : 2;
                if (!Selected || Selected->Symbol != Id || Selected->Profile != Module_.Profile ||
                    !Selected->SourceArity.Accepts(SourceArity))
                {
                    Error("DBC4007", Prefix + " references an invalid whole-array overload", Value.Span);
                    Valid = false;
                }
                if (IsCollection)
                {
                    if ((Descriptor.Kind != language::SymbolKind::Command &&
                         Descriptor.Kind != language::SymbolKind::Function) ||
                        Value.D < 1 || Value.D > 2 || !Selected || Selected->Parameters.empty() ||
                        Selected->Parameters.front().Mode != language::BuiltinParameterMode::Array)
                    {
                        Error("DBC4007", Prefix + " has an invalid dynamic-array builtin convention", Value.Span);
                        Valid = false;
                    }
                }
                else if (Descriptor.Kind != language::SymbolKind::Command ||
                         (!IsLoad && Convention != language::BuiltinConvention::SaveArray) ||
                         Value.D != (IsLoad ? 1U : 0U))
                {
                    Error("DBC4007", Prefix + " has an invalid whole-array builtin convention", Value.Span);
                    Valid = false;
                }
                if (!language::ExecutableIn(Descriptor, Module_.Profile))
                {
                    Error("DBC4007",
                          Prefix + " uses unavailable or unsupported symbol '" + std::string(Descriptor.CanonicalName) +
                              "'",
                          Value.Span);
                    Valid = false;
                }
                const std::optional<language::Arity> Arity = language::ArityFor(Descriptor, Module_.Profile);
                if (!Arity || !Arity->Accepts(SourceArity))
                {
                    Error("DBC4007", Prefix + " whole-array builtin has an invalid source arity", Value.Span);
                    Valid = false;
                }
                SlotScope Scope{};
                if (!DecodeScope(Value.C, Scope, Prefix, Value.Span))
                    return false;
                const Slot* Target = ResolveSlot(State, Scope, Value.B, Prefix, Value.Span);
                if (!Target)
                    return false;
                if (Target->ArrayRank == 0)
                {
                    Error("DBC4002", Prefix + " requires an array slot", Value.Span);
                    Valid = false;
                }
                if (!IsCollection && !(Value.ResultType == Target->ValueType))
                {
                    Error("DBC4005",
                          Prefix + " declares element type " + TypeName(Value.ResultType) + ", expected " +
                              TypeName(Target->ValueType),
                          Value.Span);
                    Valid = false;
                }
                if (IsCollection)
                {
                    const language::BuiltinValueType Expected = Descriptor.Kind == language::SymbolKind::Command
                                                                    ? language::BuiltinValueType::Void
                                                                : Selected ? Selected->ReturnType
                                                                           : language::BuiltinValueType::Void;
                    if (!MatchesBuiltinType(Expected, Value.ResultType))
                    {
                        Error("DBC4005", Prefix + " has an invalid dynamic-array result type", Value.Span);
                        Valid = false;
                    }
                }
                return Valid;
            }

            bool ValidateFunctionCall(const Instruction& Value, const std::string& Prefix)
            {
                if (Value.A >= Module_.Functions.size())
                {
                    Error("DBC4007",
                          Prefix + " references function " + std::to_string(Value.A) + " outside the function table",
                          Value.Span);
                    return false;
                }
                const Function& Target = Module_.Functions[Value.A];
                bool Valid = true;
                if (Value.A == Module_.EntryFunction)
                {
                    Error("DBC4007", Prefix + " cannot call the module entry function", Value.Span);
                    Valid = false;
                }
                if (Value.B != Target.ParameterCount)
                {
                    Error("DBC4007",
                          Prefix + " calls function '" + Target.Name + "' with " + std::to_string(Value.B) +
                              " arguments, expected " + std::to_string(Target.ParameterCount),
                          Value.Span);
                    Valid = false;
                }
                if (!(Value.ResultType == Target.ReturnType))
                {
                    Error("DBC4005",
                          Prefix + " declares result type " + TypeName(Value.ResultType) + ", function '" +
                              Target.Name + "' returns " + TypeName(Target.ReturnType),
                          Value.Span);
                    Valid = false;
                }
                return Valid;
            }

            bool ValidateReturnInstruction(const FunctionState& State, const Instruction& Value,
                                           const std::string& Prefix)
            {
                if (Value.ResultType == State.Value->ReturnType)
                    return true;
                Error("DBC4008",
                      Prefix + " declares return type " + TypeName(Value.ResultType) + ", function returns " +
                          TypeName(State.Value->ReturnType),
                      Value.Span);
                return false;
            }

            bool Pop(Stack& Values, std::size_t Count, const FunctionState& State, std::size_t Index)
            {
                if (Values.size() >= Count)
                {
                    Values.resize(Values.size() - Count);
                    return true;
                }
                Error("DBC4004",
                      At(State, Index) + " needs " + std::to_string(Count) + " stack value(s), found " +
                          std::to_string(Values.size()),
                      State.Value->Code[Index].Span);
                return false;
            }

            std::optional<language::Type> PopOne(Stack& Values, const FunctionState& State, std::size_t Index)
            {
                if (Values.empty())
                {
                    Error("DBC4004", At(State, Index) + " needs one stack value, found none",
                          State.Value->Code[Index].Span);
                    return std::nullopt;
                }
                language::Type Result = Values.back();
                Values.pop_back();
                return Result;
            }

            bool RequireNumeric(const language::Type& Value, const FunctionState& State, std::size_t Index,
                                std::string_view Role)
            {
                if (Value.IsNumeric())
                    return true;
                Error("DBC4005",
                      At(State, Index) + " requires a numeric " + std::string(Role) + ", found " + TypeName(Value),
                      State.Value->Code[Index].Span);
                return false;
            }

            bool RequireExact(const language::Type& Value, const language::Type& Expected, const FunctionState& State,
                              std::size_t Index, std::string_view Role)
            {
                if (Value == Expected)
                    return true;
                Error("DBC4005",
                      At(State, Index) + " has " + std::string(Role) + " type " + TypeName(Value) + ", expected " +
                          TypeName(Expected),
                      State.Value->Code[Index].Span);
                return false;
            }

            const TypeLayout* FindLayout(const language::Type& Type) const
            {
                if (Type.Kind != language::TypeKind::UserDefined)
                    return nullptr;
                const auto Iterator = TypeIndices_.find(Canonical(Type.Name));
                return Iterator == TypeIndices_.end() ? nullptr : &Module_.Types[Iterator->second];
            }

            const TypeField* FindField(const TypeLayout& Layout, std::string_view Name) const
            {
                const std::string Expected = Canonical(Name);
                const auto Iterator =
                    std::find_if(Layout.Fields.begin(), Layout.Fields.end(),
                                 [&](const TypeField& Value) { return Canonical(Value.Name) == Expected; });
                return Iterator == Layout.Fields.end() ? nullptr : &*Iterator;
            }

            void Transfer(FunctionState& State, std::size_t Index, const Stack& Input,
                          const BuiltinMarkerStack& MarkerInput, std::deque<std::size_t>& Worklist)
            {
                const Instruction& Value = State.Value->Code[Index];
                Stack Output = Input;
                BuiltinMarkerStack Markers = MarkerInput;
                bool Continue = true;

                switch (Value.Op)
                {
                    case OpCode::PushInteger:
                    case OpCode::PushReal:
                    case OpCode::PushString:
                        Output.push_back(Value.ResultType);
                        break;
                    case OpCode::LoadGlobal:
                    case OpCode::LoadLocal:
                        Output.push_back(Value.ResultType);
                        break;
                    case OpCode::StoreGlobal:
                    case OpCode::StoreLocal:
                    {
                        const std::optional<language::Type> Stored = PopOne(Output, State, Index);
                        if (!Stored)
                            return;
                        const SlotScope Scope = Value.Op == OpCode::StoreGlobal ? SlotScope::Global : SlotScope::Local;
                        const Slot* Target = ResolveSlot(State, Scope, Value.A, At(State, Index), Value.Span);
                        if (!Target)
                            return;
                        if (!CanConvert(*Stored, Target->ValueType))
                        {
                            Error("DBC4005",
                                  At(State, Index) + " cannot store " + TypeName(*Stored) + " in slot '" +
                                      Target->Name + "' of type " + TypeName(Target->ValueType),
                                  Value.Span);
                            return;
                        }
                        break;
                    }
                    case OpCode::CreateArray:
                    {
                        const std::size_t Rank = Value.B;
                        if (Output.size() < Rank)
                        {
                            Pop(Output, Rank, State, Index);
                            return;
                        }
                        for (std::size_t Offset = 0; Offset < Rank; ++Offset)
                        {
                            if (!RequireNumeric(Output[Output.size() - Rank + Offset], State, Index, "array dimension"))
                                return;
                        }
                        Output.resize(Output.size() - Rank);
                        break;
                    }
                    case OpCode::DestroyArray:
                        break;
                    case OpCode::LoadArray:
                    case OpCode::StoreArray:
                    {
                        const std::size_t IndexCount = Value.D != 0 ? 0U : static_cast<std::size_t>(Value.B);
                        const std::size_t Required = IndexCount + (Value.Op == OpCode::StoreArray ? 1U : 0U);
                        if (Output.size() < Required)
                        {
                            Pop(Output, Required, State, Index);
                            return;
                        }
                        const std::size_t IndexBegin = Output.size() - Required;
                        for (std::size_t Offset = 0; Offset < IndexCount; ++Offset)
                        {
                            if (!RequireNumeric(Output[IndexBegin + Offset], State, Index, "array index"))
                                return;
                        }
                        if (Value.Op == OpCode::StoreArray)
                        {
                            const language::Type& Stored = Output.back();
                            if (!CanConvert(Stored, Value.ResultType))
                            {
                                Error("DBC4005",
                                      At(State, Index) + " cannot store " + TypeName(Stored) + " in an array of " +
                                          TypeName(Value.ResultType),
                                      Value.Span);
                                return;
                            }
                        }
                        Output.resize(IndexBegin);
                        if (Value.Op == OpCode::LoadArray)
                            Output.push_back(Value.ResultType);
                        break;
                    }
                    case OpCode::LoadField:
                    case OpCode::StoreField:
                    {
                        std::optional<language::Type> Stored;
                        if (Value.Op == OpCode::StoreField)
                        {
                            Stored = PopOne(Output, State, Index);
                            if (!Stored)
                                return;
                        }
                        const std::optional<language::Type> Base = PopOne(Output, State, Index);
                        if (!Base)
                            return;
                        const TypeLayout* Layout = FindLayout(*Base);
                        if (!Layout)
                        {
                            Error("DBC4005",
                                  At(State, Index) + " requires a user-defined value, found " + TypeName(*Base),
                                  Value.Span);
                            return;
                        }
                        const std::string& FieldName = Module_.Strings[Value.A];
                        const TypeField* Field = FindField(*Layout, FieldName);
                        if (!Field)
                        {
                            Error("DBC4002",
                                  At(State, Index) + " references unknown field '" + FieldName + "' of TYPE '" +
                                      Layout->Name + "'",
                                  Value.Span);
                            return;
                        }
                        if (!RequireExact(Value.ResultType, Field->ValueType, State, Index, "field result"))
                            return;
                        if (Stored && !CanConvert(*Stored, Field->ValueType))
                        {
                            Error("DBC4005",
                                  At(State, Index) + " cannot store " + TypeName(*Stored) + " in field '" + FieldName +
                                      "' of type " + TypeName(Field->ValueType),
                                  Value.Span);
                            return;
                        }
                        Output.push_back(Value.Op == OpCode::LoadField ? Field->ValueType : *Base);
                        break;
                    }
                    case OpCode::BeginBuiltin:
                        Markers.emplace_back(static_cast<language::SymbolId>(Value.A), language::OverloadId{Value.E});
                        break;
                    case OpCode::CompleteBuiltin:
                    {
                        const BuiltinMarker Expected{static_cast<language::SymbolId>(Value.A),
                                                     language::OverloadId{Value.E}};
                        if (Markers.empty() || Markers.back() != Expected)
                        {
                            Error("DBC4007", At(State, Index) + " does not close the active VM builtin marker",
                                  Value.Span);
                            return;
                        }
                        Markers.pop_back();
                        break;
                    }
                    case OpCode::ReadData:
                        Output.push_back(Value.ResultType);
                        break;
                    case OpCode::RestoreData:
                        break;
                    case OpCode::Unary:
                    {
                        const std::optional<language::Type> Operand = PopOne(Output, State, Index);
                        if (!Operand)
                            return;
                        const auto Operation = static_cast<UnaryOperation>(Value.A);
                        if (!RequireNumeric(*Operand, State, Index, "unary operand"))
                            return;
                        const language::Type Expected =
                            Operation == UnaryOperation::LogicalNot ? language::Type{language::TypeKind::Boolean, {}}
                            : Operation == UnaryOperation::BitNot   ? language::Type{language::TypeKind::Integer, {}}
                                                                    : *Operand;
                        if (!RequireExact(Value.ResultType, Expected, State, Index, "unary result"))
                            return;
                        Output.push_back(Value.ResultType);
                        break;
                    }
                    case OpCode::Binary:
                    {
                        const std::optional<language::Type> Right = PopOne(Output, State, Index);
                        const std::optional<language::Type> Left = PopOne(Output, State, Index);
                        if (!Right || !Left)
                            return;
                        const auto Operation = static_cast<BinaryOperation>(Value.A);
                        language::Type Expected{};
                        if (IsComparison(Operation))
                        {
                            const bool Numeric = Left->IsNumeric() && Right->IsNumeric();
                            const bool Strings =
                                Left->Kind == language::TypeKind::String && Right->Kind == language::TypeKind::String;
                            if (!Numeric && !Strings)
                            {
                                Error("DBC4005",
                                      At(State, Index) + " compares incompatible types " + TypeName(*Left) + " and " +
                                          TypeName(*Right),
                                      Value.Span);
                                return;
                            }
                            Expected = {language::TypeKind::Boolean, {}};
                        }
                        else if (IsLogical(Operation))
                        {
                            if (!RequireNumeric(*Left, State, Index, "left operand") ||
                                !RequireNumeric(*Right, State, Index, "right operand"))
                                return;
                            Expected = {language::TypeKind::Boolean, {}};
                        }
                        else if (Operation == BinaryOperation::Add && Left->Kind == language::TypeKind::String &&
                                 Right->Kind == language::TypeKind::String)
                        {
                            Expected = {language::TypeKind::String, {}};
                        }
                        else
                        {
                            if (!RequireNumeric(*Left, State, Index, "left operand") ||
                                !RequireNumeric(*Right, State, Index, "right operand"))
                                return;
                            Expected = {Left->Kind == language::TypeKind::Real ||
                                                Right->Kind == language::TypeKind::Real
                                            ? language::TypeKind::Real
                                            : language::TypeKind::Integer,
                                        {}};
                        }
                        if (!RequireExact(Value.ResultType, Expected, State, Index, "binary result"))
                            return;
                        Output.push_back(Value.ResultType);
                        break;
                    }
                    case OpCode::CallBuiltin:
                    {
                        const language::BuiltinOverload* Selected = language::Overload({Value.E});
                        if (!Selected || Output.size() < Value.B)
                        {
                            if (!Pop(Output, Value.B, State, Index))
                                return;
                            break;
                        }
                        const std::size_t Begin = Output.size() - Value.B;
                        for (std::size_t Argument = 0; Argument < Value.B && Argument < Selected->Parameters.size();
                             ++Argument)
                        {
                            const language::BuiltinParameter& Parameter = Selected->Parameters[Argument];
                            if (!MatchesBuiltinType(Parameter.ValueType, Output[Begin + Argument]))
                            {
                                Error("DBC4005",
                                      At(State, Index) + " argument " + std::to_string(Argument + 1) +
                                          " does not match the selected builtin overload",
                                      Value.Span);
                                return;
                            }
                        }
                        if (!Pop(Output, Value.B, State, Index))
                            return;
                        if (Value.C == 1)
                            Output.push_back(Value.ResultType);
                        break;
                    }
                    case OpCode::CallBuiltinArray:
                    {
                        const auto Id = static_cast<language::SymbolId>(Value.A);
                        if (language::ConventionFor(Id) == language::BuiltinConvention::ArrayCollection)
                        {
                            const language::BuiltinOverload* Selected = language::Overload({Value.E});
                            const std::size_t ArgumentCount = Value.D - 1U;
                            if (Output.size() < ArgumentCount)
                            {
                                Pop(Output, ArgumentCount, State, Index);
                                return;
                            }
                            const std::size_t Begin = Output.size() - ArgumentCount;
                            for (std::size_t Argument = 0; Argument < ArgumentCount; ++Argument)
                            {
                                const language::BuiltinParameter& Parameter = Selected->Parameters[Argument + 1];
                                if (!MatchesBuiltinType(Parameter.ValueType, Output[Begin + Argument]))
                                {
                                    Error("DBC4005", At(State, Index) + " dynamic-array argument type mismatch",
                                          Value.Span);
                                    return;
                                }
                            }
                            Output.resize(Begin);
                            if (language::Symbol(Id).Kind == language::SymbolKind::Function)
                                Output.push_back(Value.ResultType);
                        }
                        else
                        {
                            const std::optional<language::Type> Path = PopOne(Output, State, Index);
                            if (!Path)
                                return;
                            if (Path->Kind != language::TypeKind::String)
                            {
                                Error("DBC4005", At(State, Index) + " requires a string path", Value.Span);
                                return;
                            }
                        }
                        break;
                    }
                    case OpCode::CallFunction:
                    {
                        const Function& Target = Module_.Functions[Value.A];
                        if (Output.size() < Value.B)
                        {
                            Pop(Output, Value.B, State, Index);
                            return;
                        }
                        const std::size_t Begin = Output.size() - Value.B;
                        for (std::size_t Argument = 0; Argument < Value.B; ++Argument)
                        {
                            if (Argument >= Target.Locals.size())
                                return;
                            const language::Type& Actual = Output[Begin + Argument];
                            const language::Type& Expected = Target.Locals[Argument].ValueType;
                            if (!CanConvert(Actual, Expected))
                            {
                                Error("DBC4005",
                                      At(State, Index) + " passes " + TypeName(Actual) + " to parameter " +
                                          std::to_string(Argument) + " of function '" + Target.Name + "', expected " +
                                          TypeName(Expected),
                                      Value.Span);
                                return;
                            }
                        }
                        Output.resize(Begin);
                        if (Target.ReturnType.Kind != language::TypeKind::Void)
                            Output.push_back(Target.ReturnType);
                        break;
                    }
                    case OpCode::Duplicate:
                    {
                        if (Output.size() < Value.A)
                        {
                            Pop(Output, Value.A, State, Index);
                            return;
                        }
                        const auto Begin = Output.end() - static_cast<std::ptrdiff_t>(Value.A);
                        const Stack Copy(Begin, Output.end());
                        Output.insert(Output.end(), Copy.begin(), Copy.end());
                        break;
                    }
                    case OpCode::Pop:
                        if (!Pop(Output, 1, State, Index))
                            return;
                        break;
                    case OpCode::Jump:
                        Merge(State, Value.A, Output, Markers, Worklist);
                        Continue = false;
                        break;
                    case OpCode::JumpIfFalse:
                    {
                        const std::optional<language::Type> Condition = PopOne(Output, State, Index);
                        if (!Condition || !RequireNumeric(*Condition, State, Index, "condition"))
                            return;
                        Merge(State, Value.A, Output, Markers, Worklist);
                        break;
                    }
                    case OpCode::CallLabel:
                        if (!Output.empty())
                        {
                            Error("DBC4008", At(State, Index) + " requires an empty value stack before GOSUB",
                                  Value.Span);
                            return;
                        }
                        Merge(State, Value.A, Output, Markers, Worklist);
                        break;
                    case OpCode::ReturnSubroutine:
                        if (!Output.empty())
                        {
                            Error("DBC4008", At(State, Index) + " requires an empty value stack at RETURN", Value.Span);
                        }
                        if (!Markers.empty())
                            Error("DBC4007", At(State, Index) + " returns with an active VM builtin marker",
                                  Value.Span);
                        Continue = false;
                        break;
                    case OpCode::Return:
                        ValidateReturnStack(State, Index, Output);
                        if (!Markers.empty())
                            Error("DBC4007", At(State, Index) + " returns with an active VM builtin marker",
                                  Value.Span);
                        Continue = false;
                        break;
                    case OpCode::Halt:
                        if (!Output.empty())
                            Error("DBC4008",
                                  At(State, Index) + " halts with " + std::to_string(Output.size()) +
                                      " value(s) on the stack",
                                  Value.Span);
                        if (!Markers.empty())
                            Error("DBC4007", At(State, Index) + " halts with an active VM builtin marker", Value.Span);
                        Continue = false;
                        break;
                }

                if (!Continue)
                    return;
                if (Index + 1 >= State.Value->Code.size())
                {
                    Error("DBC4003", At(State, Index) + " falls through the end of the function", Value.Span);
                    return;
                }
                Merge(State, Index + 1, Output, Markers, Worklist);
            }

            void ValidateReturnStack(const FunctionState& State, std::size_t Index, Stack Values)
            {
                const Instruction& Instruction = State.Value->Code[Index];
                if (State.Value->ReturnType.Kind == language::TypeKind::Void)
                {
                    if (!Values.empty())
                    {
                        Error("DBC4008",
                              At(State, Index) + " returns void with " + std::to_string(Values.size()) +
                                  " value(s) on the stack",
                              Instruction.Span);
                    }
                    return;
                }

                const std::optional<language::Type> Returned = PopOne(Values, State, Index);
                if (!Returned)
                    return;
                if (!CanConvert(*Returned, State.Value->ReturnType))
                {
                    Error("DBC4008",
                          At(State, Index) + " returns " + TypeName(*Returned) + ", expected " +
                              TypeName(State.Value->ReturnType),
                          Instruction.Span);
                    return;
                }
                if (!Values.empty())
                {
                    Error("DBC4008",
                          At(State, Index) + " leaves " + std::to_string(Values.size()) +
                              " extra value(s) on the stack",
                          Instruction.Span);
                }
            }

            void Merge(FunctionState& State, std::size_t Target, const Stack& Values, const BuiltinMarkerStack& Markers,
                       std::deque<std::size_t>& Worklist)
            {
                if (Target >= State.Value->Code.size())
                    return;
                if (!State.Inputs[Target])
                {
                    State.Inputs[Target] = Values;
                    State.MarkerInputs[Target] = Markers;
                    Worklist.push_back(Target);
                    return;
                }
                if (*State.Inputs[Target] == Values && *State.MarkerInputs[Target] == Markers)
                    return;
                if (!State.JoinReported[Target])
                {
                    Error("DBC4006",
                          "Function '" + State.Value->Name + "', instruction " + std::to_string(Target) +
                              " receives incompatible value or VM builtin marker states at a join",
                          State.Value->Code[Target].Span);
                    State.JoinReported[Target] = true;
                }
            }

            const Module& Module_;
            VerificationResult Result_{};
            std::unordered_map<std::string, std::size_t> TypeIndices_{};
        };
    } // namespace

    bool VerificationResult::Success() const
    {
        return std::none_of(Diagnostics.begin(), Diagnostics.end(), [](const language::Diagnostic& Value)
                            { return Value.Severity == language::DiagnosticSeverity::Error; });
    }

    VerificationResult Verify(const Module& Value)
    {
        return Verifier(Value).Run();
    }
} // namespace dbc::vm
