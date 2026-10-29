#include "execution_session.hpp"
#include "path_utf8.hpp"

#include "verifier.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

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

        std::string FormatFailure(const SessionFailure& Failure)
        {
            std::ostringstream Result;
            Result << Failure.Code << ": " << Failure.Message;
            if (Failure.Span.Begin.Line > 0)
            {
                Result << " at line " << Failure.Span.Begin.Line << ", column " << Failure.Span.Begin.Column;
                if (!Failure.Span.LogicalPath.empty())
                    Result << " in " << dbc::PathToUtf8(Failure.Span.LogicalPath);
            }
            return Result.str();
        }

        const TypeLayout* FindType(const Module& Bytecode, std::string_view Name)
        {
            const std::string Key = Canonical(Name);
            const auto Iterator = std::find_if(Bytecode.Types.begin(), Bytecode.Types.end(),
                                               [&](const TypeLayout& Value) { return Canonical(Value.Name) == Key; });
            return Iterator == Bytecode.Types.end() ? nullptr : &*Iterator;
        }

        bool RuntimeError(std::string& Error, std::string Message)
        {
            Error = std::move(Message);
            return false;
        }

        bool DefaultValue(const language::Type& Type, const Module& Bytecode, Value& Result, std::string& Error,
                          std::vector<std::string>& ActiveTypes)
        {
            switch (Type.Kind)
            {
                case language::TypeKind::Boolean:
                case language::TypeKind::Integer:
                    Result = Value::Integer(0);
                    return true;
                case language::TypeKind::Real:
                    Result = Value::Real(0.0);
                    return true;
                case language::TypeKind::String:
                    Result = Value::String({});
                    return true;
                case language::TypeKind::UserDefined:
                {
                    const std::string Name = Canonical(Type.Name);
                    if (std::find(ActiveTypes.begin(), ActiveTypes.end(), Name) != ActiveTypes.end())
                        return RuntimeError(Error, "Recursive user-defined type initialization is not supported");
                    const TypeLayout* Layout = FindType(Bytecode, Name);
                    if (!Layout)
                        return RuntimeError(Error, "Unknown user-defined type '" + Type.Name + "'");
                    ActiveTypes.push_back(Name);
                    auto Record = std::make_shared<RecordValue>();
                    Record->TypeName = Name;
                    for (const TypeField& Field : Layout->Fields)
                    {
                        Value Initial;
                        if (!DefaultValue(Field.ValueType, Bytecode, Initial, Error, ActiveTypes))
                        {
                            ActiveTypes.pop_back();
                            return false;
                        }
                        Record->Fields.emplace(Canonical(Field.Name), std::move(Initial));
                    }
                    ActiveTypes.pop_back();
                    Result = Value::Record(std::move(Record), {language::TypeKind::UserDefined, Name});
                    return true;
                }
                case language::TypeKind::Error:
                case language::TypeKind::Unresolved:
                case language::TypeKind::Void:
                    Result = {};
                    return true;
            }
            return RuntimeError(Error, "Unknown value type");
        }

        bool DefaultValue(const language::Type& Type, const Module& Bytecode, Value& Result, std::string& Error)
        {
            std::vector<std::string> ActiveTypes;
            return DefaultValue(Type, Bytecode, Result, Error, ActiveTypes);
        }

        bool NumericResult(const language::Type& Type)
        {
            return Type.Kind == language::TypeKind::Boolean || Type.Kind == language::TypeKind::Integer;
        }

        bool EvaluateUnary(UnaryOperation Operation, const language::Type& ResultType, const Value& Operand,
                           Value& Result, std::string& Error)
        {
            if (Operation == UnaryOperation::LogicalNot)
            {
                Result = Value::Integer(Operand.IsTruthy() ? 0 : 1);
                return true;
            }
            if (NumericResult(ResultType) || Operation == UnaryOperation::BitNot)
            {
                std::int64_t Number = 0;
                if (!Operand.AsInteger(Number, Error))
                    return false;
                if (Operation == UnaryOperation::Negative)
                {
                    if (Number == std::numeric_limits<std::int64_t>::min())
                        return RuntimeError(Error, "Integer overflow");
                    Number = -Number;
                }
                else if (Operation == UnaryOperation::BitNot)
                    Number = std::bit_cast<std::int64_t>(~std::bit_cast<std::uint64_t>(Number));
                else if (Operation != UnaryOperation::Positive)
                    return RuntimeError(Error, "Unknown unary operation");
                Result = Value::Integer(Number);
                return true;
            }
            if (!Operand.Type().IsNumeric())
                return RuntimeError(Error, "Value is not numeric");
            if (Operation != UnaryOperation::Positive && Operation != UnaryOperation::Negative)
                return RuntimeError(Error, "Unknown unary operation");
            Result = Value::Real(Operation == UnaryOperation::Negative ? -Operand.AsReal() : Operand.AsReal());
            return true;
        }

        template <typename T> bool Compare(const T& Left, const T& Right, BinaryOperation Operation)
        {
            switch (Operation)
            {
                case BinaryOperation::Equal:
                    return Left == Right;
                case BinaryOperation::NotEqual:
                    return Left != Right;
                case BinaryOperation::Less:
                    return Left < Right;
                case BinaryOperation::LessEqual:
                    return Left <= Right;
                case BinaryOperation::Greater:
                    return Left > Right;
                case BinaryOperation::GreaterEqual:
                    return Left >= Right;
                default:
                    assert(false && "Expected a comparison operation");
                    return false;
            }
        }

        bool EvaluateBinary(BinaryOperation Operation, const language::Type& ResultType, const Value& Left,
                            const Value& Right, Value& Result, std::string& Error)
        {
            switch (Operation)
            {
                case BinaryOperation::Equal:
                case BinaryOperation::NotEqual:
                case BinaryOperation::Less:
                case BinaryOperation::LessEqual:
                case BinaryOperation::Greater:
                case BinaryOperation::GreaterEqual:
                {
                    bool Comparison = false;
                    if (Left.Type().IsNumeric() && Right.Type().IsNumeric())
                    {
                        const auto* A = std::get_if<std::int64_t>(&Left.Data());
                        const auto* B = std::get_if<std::int64_t>(&Right.Data());
                        Comparison =
                            A && B ? Compare(*A, *B, Operation) : Compare(Left.AsReal(), Right.AsReal(), Operation);
                    }
                    else if (Left.Type().Kind == language::TypeKind::String &&
                             Right.Type().Kind == language::TypeKind::String)
                        Comparison = Compare(Left.AsString(), Right.AsString(), Operation);
                    else
                        return RuntimeError(Error, "Values cannot be compared");
                    Result = Value::Integer(Comparison ? 1 : 0);
                    return true;
                }
                case BinaryOperation::LogicalAnd:
                    Result = Value::Integer(Left.IsTruthy() && Right.IsTruthy() ? 1 : 0);
                    return true;
                case BinaryOperation::LogicalOr:
                    Result = Value::Integer(Left.IsTruthy() || Right.IsTruthy() ? 1 : 0);
                    return true;
                default:
                    break;
            }
            if (Operation == BinaryOperation::Add && ResultType.Kind == language::TypeKind::String)
            {
                if (Left.Type().Kind != language::TypeKind::String || Right.Type().Kind != language::TypeKind::String)
                    return RuntimeError(Error, "Values cannot be concatenated");
                Result = Value::String(Left.AsString() + Right.AsString());
                return true;
            }
            const bool BitOperation = Operation == BinaryOperation::ShiftLeft ||
                                      Operation == BinaryOperation::ShiftRight ||
                                      Operation == BinaryOperation::BitAnd || Operation == BinaryOperation::BitOr ||
                                      Operation == BinaryOperation::BitXor;
            if ((NumericResult(ResultType) && Operation != BinaryOperation::Power) || BitOperation)
            {
                std::int64_t A = 0, B = 0, Number = 0;
                if (!Left.AsInteger(A, Error) || !Right.AsInteger(B, Error))
                    return false;
                const auto Min = std::numeric_limits<std::int64_t>::min();
                const auto Max = std::numeric_limits<std::int64_t>::max();
                switch (Operation)
                {
                    case BinaryOperation::Add:
                        if ((B > 0 && A > Max - B) || (B < 0 && A < Min - B))
                            return RuntimeError(Error, "Integer overflow");
                        Number = A + B;
                        break;
                    case BinaryOperation::Subtract:
                        if ((B < 0 && A > Max + B) || (B > 0 && A < Min + B))
                            return RuntimeError(Error, "Integer overflow");
                        Number = A - B;
                        break;
                    case BinaryOperation::Multiply:
                        if (A != 0 && B != 0)
                        {
                            if ((A == -1 && B == Min) || (B == -1 && A == Min) ||
                                (A > 0 && ((B > 0 && A > Max / B) || (B < 0 && B < Min / A))) ||
                                (A < 0 && ((B > 0 && A < Min / B) || (B < 0 && A < Max / B))))
                                return RuntimeError(Error, "Integer overflow");
                            Number = A * B;
                        }
                        break;
                    case BinaryOperation::Divide:
                        if (B == 0)
                            return RuntimeError(Error, "Division by zero");
                        if (A == Min && B == -1)
                            return RuntimeError(Error, "Integer overflow");
                        Number = A / B;
                        break;
                    case BinaryOperation::Modulo:
                        if (B == 0)
                            return RuntimeError(Error, "Modulo by zero");
                        Number = A == Min && B == -1 ? 0 : A % B;
                        break;
                    case BinaryOperation::ShiftLeft:
                    case BinaryOperation::ShiftRight:
                    {
                        if (B < 0 || B >= 64)
                            return RuntimeError(Error, "Shift count is out of range");
                        auto Bits = std::bit_cast<std::uint64_t>(A);
                        if (Operation == BinaryOperation::ShiftLeft)
                            Bits <<= static_cast<unsigned>(B);
                        else if (B != 0)
                        {
                            Bits >>= static_cast<unsigned>(B);
                            if (A < 0)
                                Bits |= ~std::uint64_t{0} << (64U - static_cast<unsigned>(B));
                        }
                        Number = std::bit_cast<std::int64_t>(Bits);
                        break;
                    }
                    case BinaryOperation::BitAnd:
                        Number = A & B;
                        break;
                    case BinaryOperation::BitOr:
                        Number = A | B;
                        break;
                    case BinaryOperation::BitXor:
                        Number = A ^ B;
                        break;
                    default:
                        return RuntimeError(Error, "Unknown binary operation");
                }
                Result = Value::Integer(Number);
                return true;
            }
            if (!Left.Type().IsNumeric() || !Right.Type().IsNumeric())
                return RuntimeError(Error, "Value is not numeric");
            const double A = Left.AsReal(), B = Right.AsReal();
            double Number = 0.0;
            switch (Operation)
            {
                case BinaryOperation::Add:
                    Number = A + B;
                    break;
                case BinaryOperation::Subtract:
                    Number = A - B;
                    break;
                case BinaryOperation::Multiply:
                    Number = A * B;
                    break;
                case BinaryOperation::Divide:
                    if (B == 0.0)
                        return RuntimeError(Error, "Division by zero");
                    Number = A / B;
                    break;
                case BinaryOperation::Modulo:
                    if (B == 0.0)
                        return RuntimeError(Error, "Modulo by zero");
                    Number = std::fmod(A, B);
                    break;
                case BinaryOperation::Power:
                    Number = std::pow(A, B);
                    if (!std::isfinite(Number))
                        return RuntimeError(Error, "Power result is not finite");
                    break;
                default:
                    return RuntimeError(Error, "Unknown binary operation");
            }
            return Value::Real(Number).ConvertTo(ResultType, Result, Error);
        }

        bool TakeIndices(std::vector<Value>& Stack, std::size_t Count, std::vector<std::int64_t>& Result,
                         std::string& Error)
        {
            if (Stack.size() < Count)
                return RuntimeError(Error, "Operand stack underflow");
            const std::size_t Begin = Stack.size() - Count;
            Result.clear();
            Result.reserve(Count);
            for (std::size_t Index = Begin; Index < Stack.size(); ++Index)
            {
                std::int64_t Number = 0;
                if (!Stack[Index].AsInteger(Number, Error))
                    return false;
                Result.push_back(Number);
            }
            Stack.resize(Begin);
            return true;
        }

        BuiltinResult InvokeHost(BuiltinHost& Host, const BuiltinInvocation& Invocation)
        {
            try
            {
                return Host.Invoke(Invocation);
            }
            catch (const std::exception& Exception)
            {
                return {BuiltinState::Failed, {}, Exception.what()};
            }
            catch (...)
            {
                return {BuiltinState::Failed, {}, "Unknown builtin host error"};
            }
        }

        ArrayValue* FindArray(Value& Slot)
        {
            auto* Array = std::get_if<std::shared_ptr<ArrayValue>>(&Slot.Data());
            return Array && *Array ? Array->get() : nullptr;
        }

        template <typename T> auto FindRecord(T& Value)
        {
            auto* Record = std::get_if<std::shared_ptr<RecordValue>>(&Value.Data());
            return Record && *Record ? Record->get() : nullptr;
        }
    } // namespace

    bool ExecutionSession::Prepare(std::shared_ptr<const Module> Bytecode, BuiltinHost& Host, bool TrackBuiltinCoverage)
    {
        std::string CancellationError;
        if (!CancelActiveBuiltin(CancellationError))
        {
            Fail("DBC4000", "Builtin cancellation failed: " + CancellationError);
            return false;
        }
        Bytecode_.reset();
        Host_ = nullptr;
        Globals_.clear();
        Frames_.clear();
        DataCursor_ = 0;
        StopRequested_.store(false, std::memory_order_release);
        NextInvocationId_ = 1;
        ActiveCoverage_.clear();
        Coverage_.clear();
        CoverageIndices_.clear();
        TrackBuiltinCoverage_ = TrackBuiltinCoverage;
        Suspension_ = {};
        State_ = SessionState::Empty;
        Error_.clear();
        Failure_ = {};

        if (!Bytecode)
        {
            Fail("DBC4000", "Cannot prepare a null bytecode module");
            return false;
        }

        const VerificationResult Verification = Verify(*Bytecode);
        if (!Verification.Success())
        {
            std::ostringstream Message;
            Message << "Bytecode verification failed";
            for (const language::Diagnostic& Diagnostic : Verification.Diagnostics)
                Message << '\n' << Diagnostic.Code << ": " << Diagnostic.Message;
            Fail("DBC4000", Message.str());
            return false;
        }
        if (Bytecode->EntryFunction >= Bytecode->Functions.size())
        {
            Fail("DBC4000", "Bytecode entry function is out of range");
            return false;
        }

        Bytecode_ = std::move(Bytecode);
        Host_ = &Host;
        std::string Error;
        Globals_.reserve(Bytecode_->Globals.size());
        for (const Slot& Slot : Bytecode_->Globals)
        {
            Value Initial;
            if (Slot.ArrayRank == 0 && !DefaultValue(Slot.ValueType, *Bytecode_, Initial, Error))
            {
                Fail("DBC4000", "Failed to prepare bytecode: " + Error);
                return false;
            }
            Globals_.push_back(std::move(Initial));
        }

        Frame Entry;
        Entry.FunctionIndex = Bytecode_->EntryFunction;
        const Function& Function = Bytecode_->Functions[Entry.FunctionIndex];
        Entry.Locals.reserve(Function.Locals.size());
        for (const Slot& Slot : Function.Locals)
        {
            Value Initial;
            if (Slot.ArrayRank == 0 && !DefaultValue(Slot.ValueType, *Bytecode_, Initial, Error))
            {
                Fail("DBC4000", "Failed to prepare bytecode: " + Error);
                return false;
            }
            Entry.Locals.push_back(std::move(Initial));
        }
        Frames_.push_back(std::move(Entry));
        State_ = SessionState::Ready;
        return true;
    }

    TickResult ExecutionSession::Tick(std::size_t InstructionBudget)
    {
        if (State_ != SessionState::Ready && State_ != SessionState::Running && State_ != SessionState::Suspended)
            return {State_, 0, Error_, Suspension_};
        if (StopRequested_.load(std::memory_order_acquire))
        {
            std::string CancellationError;
            if (CancelActiveBuiltin(CancellationError))
                State_ = SessionState::Cancelled;
            else
                Fail("DBC5000", "Builtin cancellation failed: " + CancellationError);
            Suspension_ = {};
            return {State_, 0, Error_, Suspension_};
        }
        if (InstructionBudget == 0)
            return {State_, 0, Error_, Suspension_};

        State_ = SessionState::Running;
        Suspension_ = {};
        std::size_t Instructions = 0;
        while (Instructions < InstructionBudget && State_ == SessionState::Running)
        {
            if (StopRequested_.load(std::memory_order_acquire))
            {
                std::string CancellationError;
                if (CancelActiveBuiltin(CancellationError))
                    State_ = SessionState::Cancelled;
                else
                    Fail("DBC5000", "Builtin cancellation failed: " + CancellationError);
                break;
            }
            if (!Bytecode_ || Frames_.empty())
            {
                Fail("DBC5000", "Execution has no active frame");
                break;
            }

            Frame& Current = Frames_.back();
            if (Current.FunctionIndex >= Bytecode_->Functions.size())
            {
                Fail("DBC5000", "Active function is out of range");
                break;
            }
            const Function& Function = Bytecode_->Functions[Current.FunctionIndex];
            if (Current.ProgramCounter >= Function.Code.size())
            {
                Fail("DBC5000", "Program counter is out of range");
                break;
            }

            const Instruction& Instruction = Function.Code[Current.ProgramCounter];
            ++Instructions;
            Execute(Current, Instruction);
        }
        return {State_, Instructions, Error_, Suspension_};
    }

    void ExecutionSession::RequestStop()
    {
        StopRequested_.store(true, std::memory_order_release);
    }

    SessionState ExecutionSession::State() const
    {
        return State_;
    }

    const std::string& ExecutionSession::Error() const
    {
        return Error_;
    }

    const Value* ExecutionSession::Global(std::string_view Name) const
    {
        if (!Bytecode_)
            return nullptr;
        // Arrays are stored under `NAME()`; accept the bare name when no scalar of that name exists.
        const std::string Key = Canonical(Name);
        for (const std::string& Candidate : {Key, Key + "()"})
        {
            for (std::size_t Index = 0; Index < Bytecode_->Globals.size() && Index < Globals_.size(); ++Index)
            {
                if (Canonical(Bytecode_->Globals[Index].Name) == Candidate)
                    return &Globals_[Index];
            }
        }
        return nullptr;
    }

    bool ExecutionSession::Execute(Frame& Current, const Instruction& Instruction)
    {
        std::string Error;
        const auto Reject = [&](std::string Message)
        {
            Fail("DBC5000", std::move(Message), Instruction.Span);
            return false;
        };
        if (!Bytecode_ || !Host_)
            return Reject("Execution session is not prepared");
        const Function& CurrentFunction = Bytecode_->Functions[Current.FunctionIndex];

        const auto Pop = [&]()
        {
            assert(!(Current.Stack.empty()));
            Value Result = std::move(Current.Stack.back());
            Current.Stack.pop_back();
            return Result;
        };
        const auto GlobalSlot = [&](std::size_t Index) -> Value&
        {
            assert(!(Index >= Globals_.size()));
            return Globals_[Index];
        };
        const auto LocalSlot = [&](std::size_t Index) -> Value&
        {
            assert(!(Index >= Current.Locals.size()));
            return Current.Locals[Index];
        };
        const auto ScopedSlot = [&](std::size_t Index, std::uint32_t Scope) -> Value&
        {
            if (Scope == static_cast<std::uint32_t>(SlotScope::Global))
                return GlobalSlot(Index);
            if (Scope == static_cast<std::uint32_t>(SlotScope::Local))
                return LocalSlot(Index);
            assert(false && "Verified slot scope is invalid");
            return GlobalSlot(Index);
        };
        const auto SlotDefinition = [&](std::size_t Index, std::uint32_t Scope) -> const Slot&
        {
            if (Scope == static_cast<std::uint32_t>(SlotScope::Global))
            {
                assert(!(Index >= Bytecode_->Globals.size()));
                return Bytecode_->Globals[Index];
            }
            if (Scope == static_cast<std::uint32_t>(SlotScope::Local))
            {
                assert(!(Index >= CurrentFunction.Locals.size()));
                return CurrentFunction.Locals[Index];
            }
            assert(false && "Verified slot scope is invalid");
            return Bytecode_->Globals[Index];
        };
        const auto Advance = [&]() { ++Current.ProgramCounter; };

        switch (Instruction.Op)
        {
            case OpCode::PushInteger:
                Current.Stack.push_back(Value::Integer(Instruction.Integer));
                Advance();
                return true;
            case OpCode::PushReal:
                Current.Stack.push_back(Value::Real(Instruction.Real));
                Advance();
                return true;
            case OpCode::PushString:
                if (Instruction.A >= Bytecode_->Strings.size())
                    return Reject("String index is out of range");
                Current.Stack.push_back(Value::String(Bytecode_->Strings[Instruction.A]));
                Advance();
                return true;
            case OpCode::LoadGlobal:
                Current.Stack.push_back(GlobalSlot(Instruction.A));
                Advance();
                return true;
            case OpCode::StoreGlobal:
            {
                if (Instruction.A >= Globals_.size() || Instruction.A >= Bytecode_->Globals.size())
                    return Reject("Global slot is out of range");
                Value Result;
                if (!Pop().ConvertTo(Bytecode_->Globals[Instruction.A].ValueType, Result, Error))
                    return Reject(Error);
                GlobalSlot(Instruction.A) = std::move(Result);
                Advance();
                return true;
            }
            case OpCode::LoadLocal:
                Current.Stack.push_back(LocalSlot(Instruction.A));
                Advance();
                return true;
            case OpCode::StoreLocal:
            {
                if (Instruction.A >= Current.Locals.size() || Instruction.A >= CurrentFunction.Locals.size())
                    return Reject("Local slot is out of range");
                Value Result;
                if (!Pop().ConvertTo(CurrentFunction.Locals[Instruction.A].ValueType, Result, Error))
                    return Reject(Error);
                LocalSlot(Instruction.A) = std::move(Result);
                Advance();
                return true;
            }
            case OpCode::CreateArray:
            {
                std::vector<std::int64_t> Bounds;
                if (!TakeIndices(Current.Stack, Instruction.B, Bounds, Error))
                    return Reject(Error);
                auto Array = std::make_shared<ArrayValue>();
                const Slot& Definition = SlotDefinition(Instruction.A, Instruction.C);
                Array->ElementType = Definition.ValueType;
                std::size_t ElementCount = 1;
                Array->Dimensions.reserve(Bounds.size());
                for (const std::int64_t Bound : Bounds)
                {
                    if (Bound < 0)
                        return Reject("Array dimension cannot be negative");
                    const auto UnsignedBound = static_cast<std::uint64_t>(Bound);
                    if (UnsignedBound >= std::numeric_limits<std::size_t>::max())
                        return Reject("Array dimension is too large");
                    const std::size_t Extent = static_cast<std::size_t>(UnsignedBound) + 1;
                    if (Extent != 0 && ElementCount > std::numeric_limits<std::size_t>::max() / Extent)
                        return Reject("Array size is too large");
                    ElementCount *= Extent;
                    Array->Dimensions.push_back(Extent);
                }
                if (ElementCount > Array->Elements.max_size())
                    return Reject("Array size is too large");
                Array->Elements.reserve(ElementCount);
                Value Initial;
                if (!DefaultValue(Array->ElementType, *Bytecode_, Initial, Error))
                    return Reject(Error);
                Array->Elements.resize(ElementCount, Initial);
                ScopedSlot(Instruction.A, Instruction.C) = Value::Array(std::move(Array), Definition.ValueType);
                Advance();
                return true;
            }
            case OpCode::LoadArray:
            {
                ArrayValue* Found = FindArray(ScopedSlot(Instruction.A, Instruction.C));
                if (!Found)
                    return Reject("Array has not been dimensioned");
                ArrayValue& Array = *Found;
                std::vector<std::int64_t> Indices{Array.CurrentIndex};
                if (Instruction.D == 0 && !TakeIndices(Current.Stack, Instruction.B, Indices, Error))
                    return Reject(Error);
                std::size_t Index = 0;
                if (!Array.FlatIndex(Indices, Index, Error))
                    return Reject(Error);
                Current.Stack.push_back(Array.Elements[Index]);
                Advance();
                return true;
            }
            case OpCode::StoreArray:
            {
                Value Result = Pop();
                ArrayValue* Found = FindArray(ScopedSlot(Instruction.A, Instruction.C));
                if (!Found)
                    return Reject("Array has not been dimensioned");
                ArrayValue& Array = *Found;
                std::vector<std::int64_t> Indices{Array.CurrentIndex};
                if (Instruction.D == 0 && !TakeIndices(Current.Stack, Instruction.B, Indices, Error))
                    return Reject(Error);
                std::size_t Index = 0;
                if (!Array.FlatIndex(Indices, Index, Error))
                    return Reject(Error);
                if (!Result.ConvertTo(Array.ElementType, Array.Elements[Index], Error))
                    return Reject(Error);
                Advance();
                return true;
            }
            case OpCode::DestroyArray:
                ScopedSlot(Instruction.A, Instruction.C) = {};
                Advance();
                return true;
            case OpCode::LoadField:
            {
                if (Instruction.A >= Bytecode_->Strings.size())
                    return Reject("Field name is out of range");
                const Value Record = Pop();
                const auto* Found = FindRecord(Record);
                if (!Found)
                    return Reject("Value is not a user-defined type");
                const auto& Fields = Found->Fields;
                const auto Iterator = Fields.find(Canonical(Bytecode_->Strings[Instruction.A]));
                if (Iterator == Fields.end())
                    return Reject("Unknown user-defined type field");
                Current.Stack.push_back(Iterator->second);
                Advance();
                return true;
            }
            case OpCode::StoreField:
            {
                if (Instruction.A >= Bytecode_->Strings.size())
                    return Reject("Field name is out of range");
                Value Field = Pop();
                Value Record = Pop();
                auto* Found = FindRecord(Record);
                if (!Found)
                    return Reject("Value is not a user-defined type");
                auto& Fields = Found->Fields;
                const auto Iterator = Fields.find(Canonical(Bytecode_->Strings[Instruction.A]));
                if (Iterator == Fields.end())
                    return Reject("Unknown user-defined type field");
                if (!Field.ConvertTo(Iterator->second.Type(), Iterator->second, Error))
                    return Reject(Error);
                Current.Stack.push_back(std::move(Record));
                Advance();
                return true;
            }
            case OpCode::BeginBuiltin:
                BeginBuiltinCoverage(static_cast<language::SymbolId>(Instruction.A), {Instruction.E});
                Advance();
                return true;
            case OpCode::CompleteBuiltin:
                CompleteBuiltinCoverage(static_cast<language::SymbolId>(Instruction.A), {Instruction.E});
                Advance();
                return true;
            case OpCode::ReadData:
            {
                if (DataCursor_ >= Bytecode_->Data.size())
                    return Reject("READ has passed the end of DATA");
                const DataValue& Item = Bytecode_->Data[DataCursor_];
                Value Result;
                if (Item.ValueType.Kind == language::TypeKind::Integer)
                    Result = Value::Integer(Item.Integer);
                else if (Item.ValueType.Kind == language::TypeKind::Real)
                    Result = Value::Real(Item.Real);
                else if (Item.ValueType.Kind == language::TypeKind::String &&
                         Item.StringIndex < Bytecode_->Strings.size())
                    Result = Value::String(Bytecode_->Strings[Item.StringIndex]);
                else
                    return Reject("DATA item has an invalid type");
                Value Converted;
                if (!Result.ConvertTo(Instruction.ResultType, Converted, Error))
                    return Reject(Error);
                Current.Stack.push_back(std::move(Converted));
                ++DataCursor_;
                Advance();
                return true;
            }
            case OpCode::RestoreData:
                if (Instruction.A > Bytecode_->Data.size())
                    return Reject("RESTORE target is outside DATA");
                DataCursor_ = Instruction.A;
                Advance();
                return true;
            case OpCode::Unary:
            {
                const Value Operand = Pop();
                Value Result;
                if (!EvaluateUnary(static_cast<UnaryOperation>(Instruction.A), Instruction.ResultType, Operand, Result,
                                   Error))
                    return Reject(Error);
                Current.Stack.push_back(std::move(Result));
                Advance();
                return true;
            }
            case OpCode::Binary:
            {
                const Value Right = Pop();
                const Value Left = Pop();
                Value Result;
                if (!EvaluateBinary(static_cast<BinaryOperation>(Instruction.A), Instruction.ResultType, Left, Right,
                                    Result, Error))
                    return Reject(Error);
                Current.Stack.push_back(std::move(Result));
                Advance();
                return true;
            }
            case OpCode::CallBuiltin:
            {
                if (Instruction.A >= static_cast<std::uint32_t>(language::SymbolId::Count))
                    return Reject("Builtin symbol is out of range");
                if (Current.Stack.size() < Instruction.B)
                    return Reject("Operand stack underflow");
                const std::size_t Begin = Current.Stack.size() - Instruction.B;
                const std::span<const Value> Arguments =
                    Instruction.B == 0 ? std::span<const Value>{}
                                       : std::span<const Value>{Current.Stack.data() + Begin, Instruction.B};
                const bool IsRetry = ActiveInvocation_.has_value();
                const std::uint64_t InvocationId = IsRetry ? *ActiveInvocation_ : NextInvocationId_++;
                if (!IsRetry)
                    BeginBuiltinCoverage(static_cast<language::SymbolId>(Instruction.A), {Instruction.E});
                BuiltinResult Result =
                    InvokeHost(*Host_, {InvocationId, static_cast<language::SymbolId>(Instruction.A), Arguments,
                                        Instruction.ResultType, IsRetry, Instruction.Integer != 0});
                if (Result.State == BuiltinState::Pending)
                {
                    ActiveInvocation_ = InvocationId;
                    Suspension_ = Result.Wait;
                    if (Suspension_.Kind == SuspensionKind::None)
                        Suspension_.Kind = SuspensionKind::NextTick;
                    State_ = SessionState::Suspended;
                    return false;
                }
                if (Result.State == BuiltinState::Failed)
                {
                    ActiveInvocation_.reset();
                    Fail("DBC5000", Result.Error.empty() ? "Builtin call failed" : Result.Error, Instruction.Span);
                    return false;
                }
                const bool SuspendAfterCompletion = Result.State == BuiltinState::CompletedAndSuspend;
                if (Result.State != BuiltinState::Completed && !SuspendAfterCompletion)
                    return Reject("Builtin returned an invalid state");

                ActiveInvocation_.reset();
                Current.Stack.resize(Begin);
                if (Instruction.C != 0)
                {
                    if (!Result.ReturnValue)
                        return Reject("Builtin function returned no value");
                    Value Converted;
                    if (!Result.ReturnValue->ConvertTo(Instruction.ResultType, Converted, Error))
                        return Reject(Error);
                    Current.Stack.push_back(std::move(Converted));
                }
                Advance();
                CompleteBuiltinCoverage(static_cast<language::SymbolId>(Instruction.A), {Instruction.E});
                if (SuspendAfterCompletion)
                {
                    Suspension_ = Result.Wait;
                    if (Suspension_.Kind == SuspensionKind::None)
                        Suspension_.Kind = SuspensionKind::NextTick;
                    State_ = SessionState::Suspended;
                    return false;
                }
                return true;
            }
            case OpCode::CallBuiltinArray:
            {
                if (Instruction.A >= static_cast<std::uint32_t>(language::SymbolId::Count))
                    return Reject("Builtin symbol is out of range");
                const auto Id = static_cast<language::SymbolId>(Instruction.A);
                if (language::ConventionFor(Id) == language::BuiltinConvention::ArrayCollection)
                {
                    BeginBuiltinCoverage(Id, {Instruction.E});
                    if (Instruction.D < 1 || Instruction.D > 2 || Current.Stack.size() < Instruction.D - 1U)
                        return Reject("Dynamic array argument count is invalid");
                    std::int64_t Argument = 0;
                    if (Instruction.D == 2 && !Pop().AsInteger(Argument, Error))
                        return Reject(Error);
                    ArrayValue* Found = FindArray(ScopedSlot(Instruction.B, Instruction.C));
                    if (!Found)
                        return Reject("Array has not been dimensioned");
                    ArrayValue& Array = *Found;
                    if (Argument > 0 && static_cast<std::uint64_t>(Argument) > std::numeric_limits<std::size_t>::max())
                        return Reject("Dynamic array quantity is too large");
                    const auto Insert = [&](std::size_t Position, std::size_t Quantity)
                    {
                        if (Array.Dimensions.size() != 1)
                            return Reject("Dynamic array operations require a one-dimensional array");
                        if (Position > Array.Elements.size())
                            return Reject("Array index is out of bounds");
                        if (Quantity > Array.Elements.max_size() - Array.Elements.size())
                            return Reject("Dynamic array is too large");
                        Value Initial;
                        if (!DefaultValue(Array.ElementType, *Bytecode_, Initial, Error))
                            return Reject(Error);
                        Array.Elements.insert(Array.Elements.begin() + static_cast<std::ptrdiff_t>(Position), Quantity,
                                              Initial);
                        Array.Dimensions[0] = Array.Elements.size();
                        return true;
                    };
                    const auto Delete = [&](std::int64_t Index)
                    {
                        if (Array.Dimensions.size() != 1)
                            return Reject("Dynamic array operations require a one-dimensional array");
                        if (Array.Elements.empty())
                            return true;
                        if (Index < 0 || static_cast<std::uint64_t>(Index) >= Array.Elements.size())
                            return Reject("Array index is out of bounds");
                        Array.Elements.erase(Array.Elements.begin() + static_cast<std::ptrdiff_t>(Index));
                        Array.Dimensions[0] = Array.Elements.size();
                        if (Array.CurrentIndex >= static_cast<std::int64_t>(Array.Elements.size()))
                            Array.CurrentIndex = static_cast<std::int64_t>(Array.Elements.size()) - 1;
                        return true;
                    };

                    switch (Id)
                    {
                        case language::SymbolId::Function_ArrayCount:
                            Current.Stack.push_back(
                                Value::Integer(static_cast<std::int64_t>(Array.Elements.size()) - 1));
                            break;
                        case language::SymbolId::Function_ArrayIndexValid:
                            Current.Stack.push_back(Value::Integer(
                                Array.CurrentIndex >= 0 &&
                                        static_cast<std::uint64_t>(Array.CurrentIndex) < Array.Elements.size()
                                    ? 1
                                    : 0));
                            break;
                        case language::SymbolId::Command_ArrayIndexToBottom:
                        case language::SymbolId::Command_ArrayIndexToStack:
                            Array.CurrentIndex = static_cast<std::int64_t>(Array.Elements.size()) - 1;
                            break;
                        case language::SymbolId::Command_ArrayIndexToTop:
                        case language::SymbolId::Command_ArrayIndexToQueue:
                            Array.CurrentIndex = 0;
                            break;
                        case language::SymbolId::Command_NextArrayIndex:
                            Array.CurrentIndex =
                                std::min(Array.CurrentIndex + 1, static_cast<std::int64_t>(Array.Elements.size()));
                            break;
                        case language::SymbolId::Command_PreviousArrayIndex:
                            Array.CurrentIndex = Array.CurrentIndex > 0 ? Array.CurrentIndex - 1 : -1;
                            break;
                        case language::SymbolId::Command_ArrayInsertAtBottom:
                        case language::SymbolId::Command_AddToQueue:
                        case language::SymbolId::Command_AddToStack:
                        {
                            const std::size_t Position = Array.Elements.size();
                            const std::size_t Quantity =
                                Instruction.D == 2 ? static_cast<std::size_t>(std::max<std::int64_t>(1, Argument)) : 1;
                            if (!Insert(Position, Quantity))
                                return false;
                            Array.CurrentIndex = static_cast<std::int64_t>(Position);
                            break;
                        }
                        case language::SymbolId::Command_ArrayInsertAtTop:
                        {
                            if (!Insert(0, Instruction.D == 2
                                               ? static_cast<std::size_t>(std::max<std::int64_t>(1, Argument))
                                               : 1))
                                return false;
                            Array.CurrentIndex = 0;
                            break;
                        }
                        case language::SymbolId::Command_ArrayInsertAtElement:
                            if (Array.Dimensions.size() != 1)
                                return Reject("Dynamic array operations require a one-dimensional array");
                            if (Argument < 0 || static_cast<std::uint64_t>(Argument) >= Array.Elements.size())
                                return Reject("Array index is out of bounds");
                            if (!Insert(static_cast<std::size_t>(Argument), 1))
                                return false;
                            Array.CurrentIndex = Argument;
                            break;
                        case language::SymbolId::Command_ArrayDeleteElement:
                            if (!Delete(Instruction.D == 2 ? Argument : Array.CurrentIndex))
                                return false;
                            break;
                        case language::SymbolId::Command_EmptyArray:
                            Array.Elements.clear();
                            if (Array.Dimensions.size() == 1)
                                Array.Dimensions[0] = 0;
                            else
                                std::fill(Array.Dimensions.begin(), Array.Dimensions.end(), 0);
                            Array.CurrentIndex = 0;
                            break;
                        case language::SymbolId::Command_RemoveFromQueue:
                            if (!Delete(0))
                                return false;
                            Array.CurrentIndex = 0;
                            break;
                        case language::SymbolId::Command_RemoveFromStack:
                            if (!Delete(static_cast<std::int64_t>(Array.Elements.size()) - 1))
                                return false;
                            Array.CurrentIndex = static_cast<std::int64_t>(Array.Elements.size()) - 1;
                            break;
                        default:
                            return Reject("Unknown dynamic array operation");
                    }
                    Advance();
                    CompleteBuiltinCoverage(Id, {Instruction.E});
                    return true;
                }
                if (Current.Stack.empty())
                    return Reject("Operand stack underflow");
                const bool IsRetry = ActiveInvocation_.has_value();
                if (!IsRetry)
                    BeginBuiltinCoverage(Id, {Instruction.E});
                Value& ArraySlot = ScopedSlot(Instruction.B, Instruction.C);
                const Slot& Definition = SlotDefinition(Instruction.B, Instruction.C);
                if (!FindArray(ArraySlot))
                    return Reject("Array has not been dimensioned");
                const std::span<const Value> Arguments{&Current.Stack.back(), 1};
                const std::uint64_t InvocationId = IsRetry ? *ActiveInvocation_ : NextInvocationId_++;
                BuiltinResult Result = InvokeHost(*Host_, {InvocationId, Id, Arguments, Instruction.ResultType, IsRetry,
                                                           false, &ArraySlot, Instruction.D != 0});
                if (Result.State == BuiltinState::Pending)
                {
                    ActiveInvocation_ = InvocationId;
                    Suspension_ = Result.Wait;
                    if (Suspension_.Kind == SuspensionKind::None)
                        Suspension_.Kind = SuspensionKind::NextTick;
                    State_ = SessionState::Suspended;
                    return false;
                }
                if (Result.State == BuiltinState::Failed)
                {
                    ActiveInvocation_.reset();
                    Fail("DBC5000", Result.Error.empty() ? "Whole-array builtin failed" : Result.Error,
                         Instruction.Span);
                    return false;
                }
                const bool SuspendAfterCompletion = Result.State == BuiltinState::CompletedAndSuspend;
                if (Result.State != BuiltinState::Completed && !SuspendAfterCompletion)
                    return Reject("Builtin returned an invalid state");
                ActiveInvocation_.reset();
                Current.Stack.pop_back();
                if (Instruction.D != 0)
                {
                    if (!Result.ReturnValue)
                        return Reject("LOAD ARRAY returned no array");
                    const auto* Replacement = std::get_if<std::shared_ptr<ArrayValue>>(&Result.ReturnValue->Data());
                    if (!Replacement || !*Replacement)
                        return Reject("LOAD ARRAY returned a non-array value");
                    if ((*Replacement)->ElementType != Definition.ValueType ||
                        (*Replacement)->Dimensions.size() != Definition.ArrayRank)
                        return Reject("LOAD ARRAY returned an incompatible array");
                    ArraySlot = Result.ReturnValue->Clone();
                }
                else if (Result.ReturnValue)
                {
                    return Reject("SAVE ARRAY returned an unexpected value");
                }
                Advance();
                CompleteBuiltinCoverage(Id, {Instruction.E});
                if (SuspendAfterCompletion)
                {
                    Suspension_ = Result.Wait;
                    if (Suspension_.Kind == SuspensionKind::None)
                        Suspension_.Kind = SuspensionKind::NextTick;
                    State_ = SessionState::Suspended;
                    return false;
                }
                return true;
            }
            case OpCode::CallFunction:
            {
                if (Instruction.A >= Bytecode_->Functions.size())
                    return Reject("Function index is out of range");
                const Function& Called = Bytecode_->Functions[Instruction.A];
                if (Instruction.B != Called.ParameterCount)
                    return Reject("Function argument count mismatch");
                if (Called.ParameterCount > Called.Locals.size())
                    return Reject("Function parameters exceed local slots");
                if (Current.Stack.size() < Instruction.B)
                    return Reject("Operand stack underflow");
                const std::size_t Begin = Current.Stack.size() - Instruction.B;

                Frame Next;
                Next.FunctionIndex = Instruction.A;
                Next.Locals.reserve(Called.Locals.size());
                for (const Slot& Slot : Called.Locals)
                {
                    Value Initial;
                    if (Slot.ArrayRank == 0 && !DefaultValue(Slot.ValueType, *Bytecode_, Initial, Error))
                        return Reject(Error);
                    Next.Locals.push_back(std::move(Initial));
                }
                for (std::size_t Index = 0; Index < Called.ParameterCount; ++Index)
                {
                    if (!Current.Stack[Begin + Index].ConvertTo(Called.Locals[Index].ValueType, Next.Locals[Index],
                                                                Error))
                        return Reject(Error);
                }

                Current.Stack.resize(Begin);
                Advance();
                Frames_.push_back(std::move(Next));
                return true;
            }
            case OpCode::Duplicate:
            {
                if (Instruction.A > Current.Stack.size())
                    return Reject("Operand stack underflow");
                const std::size_t Begin = Current.Stack.size() - Instruction.A;
                std::vector<Value> Copies;
                Copies.reserve(Instruction.A);
                for (std::size_t Index = Begin; Index < Current.Stack.size(); ++Index)
                    Copies.push_back(Current.Stack[Index]);
                for (Value& Copy : Copies)
                    Current.Stack.push_back(std::move(Copy));
                Advance();
                return true;
            }
            case OpCode::Pop:
                Pop();
                Advance();
                return true;
            case OpCode::Jump:
                if (Instruction.A >= CurrentFunction.Code.size())
                    return Reject("Jump target is out of range");
                Current.ProgramCounter = Instruction.A;
                return true;
            case OpCode::JumpIfFalse:
            {
                const Value Condition = Pop();
                if (!Condition.IsTruthy())
                {
                    if (Instruction.A >= CurrentFunction.Code.size())
                        return Reject("Jump target is out of range");
                    Current.ProgramCounter = Instruction.A;
                }
                else
                {
                    Advance();
                }
                return true;
            }
            case OpCode::CallLabel:
                if (Instruction.A >= CurrentFunction.Code.size())
                    return Reject("Label target is out of range");
                Current.Subroutines.push_back(Current.ProgramCounter + 1);
                Current.ProgramCounter = Instruction.A;
                return true;
            case OpCode::ReturnSubroutine:
                if (Current.Subroutines.empty())
                    return Reject("RETURN has no matching GOSUB");
                Current.ProgramCounter = Current.Subroutines.back();
                Current.Subroutines.pop_back();
                return true;
            case OpCode::Return:
            {
                std::optional<Value> Result;
                if (CurrentFunction.ReturnType.Kind != language::TypeKind::Void)
                {
                    Value Converted;
                    if (!Pop().ConvertTo(CurrentFunction.ReturnType, Converted, Error))
                        return Reject(Error);
                    Result = std::move(Converted);
                }
                Frames_.pop_back();
                if (Frames_.empty())
                {
                    State_ = SessionState::Completed;
                    return false;
                }
                if (Result)
                    Frames_.back().Stack.push_back(std::move(*Result));
                return true;
            }
            case OpCode::Halt:
                Frames_.clear();
                State_ = SessionState::Completed;
                return false;
        }
        return Reject("Unknown opcode");
    }

    const SessionFailure& ExecutionSession::Failure() const
    {
        return Failure_;
    }

    std::span<const BuiltinCoverageEntry> ExecutionSession::Coverage() const
    {
        return Coverage_;
    }

    void ExecutionSession::BeginBuiltinCoverage(language::SymbolId Symbol, language::OverloadId Overload)
    {
        if (!TrackBuiltinCoverage_)
            return;
        ActiveCoverage_.emplace_back(Symbol, Overload);
        const std::uint64_t Key = (static_cast<std::uint64_t>(Symbol) << 32U) | Overload.Value;
        const auto [Iterator, Inserted] = CoverageIndices_.emplace(Key, Coverage_.size());
        if (Inserted)
            Coverage_.push_back({Symbol, Overload, 1, 0, 0});
        else
            ++Coverage_[Iterator->second].Attempts;
    }

    void ExecutionSession::CompleteBuiltinCoverage(language::SymbolId Symbol, language::OverloadId Overload)
    {
        if (!TrackBuiltinCoverage_)
            return;
        assert(!(ActiveCoverage_.empty()));
        assert(!(ActiveCoverage_.back() != std::pair{Symbol, Overload}));
        const std::uint64_t Key = (static_cast<std::uint64_t>(Symbol) << 32U) | Overload.Value;
        const auto Iterator = CoverageIndices_.find(Key);
        assert(!(Iterator == CoverageIndices_.end()));
        ++Coverage_[Iterator->second].Completed;
        ActiveCoverage_.pop_back();
    }

    void ExecutionSession::FailBuiltinCoverage()
    {
        if (ActiveCoverage_.empty())
            return;
        for (const auto& [Symbol, Overload] : ActiveCoverage_)
        {
            const std::uint64_t Key = (static_cast<std::uint64_t>(Symbol) << 32U) | Overload.Value;
            const auto Iterator = CoverageIndices_.find(Key);
            if (Iterator != CoverageIndices_.end())
                ++Coverage_[Iterator->second].Failed;
        }
        ActiveCoverage_.clear();
    }

    void ExecutionSession::Fail(std::string Code, std::string Message, language::SourceSpan Span)
    {
        FailBuiltinCoverage();
        std::string CancellationError;
        if (!CancelActiveBuiltin(CancellationError))
            Message += "; builtin cancellation failed: " + CancellationError;
        Failure_ = SessionFailure{std::move(Code), std::move(Message), std::move(Span)};
        Error_ = FormatFailure(Failure_);
        State_ = SessionState::Failed;
    }

    bool ExecutionSession::CancelActiveBuiltin(std::string& Error)
    {
        if (!ActiveInvocation_ || Host_ == nullptr)
            return true;
        const auto Invocation = *ActiveInvocation_;
        ActiveInvocation_.reset();
        // The host is an integration boundary; its cleanup failure must remain observable.
        try
        {
            if (Host_->Cancel(Invocation, Error))
                return true;
            if (Error.empty())
                Error = "Host rejected cancellation";
        }
        catch (const std::exception& Exception)
        {
            Error = Exception.what();
        }
        catch (...)
        {
            Error = "Unknown host cancellation failure";
        }
        return false;
    }
} // namespace dbc::vm
