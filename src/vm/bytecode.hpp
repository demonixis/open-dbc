#pragma once

#include "language/frontend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dbc::vm
{
    enum class OpCode : std::uint8_t
    {
        PushInteger,
        PushReal,
        PushString,
        LoadGlobal,
        StoreGlobal,
        LoadLocal,
        StoreLocal,
        CreateArray,
        LoadArray,
        StoreArray,
        DestroyArray,
        LoadField,
        StoreField,
        BeginBuiltin,
        CompleteBuiltin,
        ReadData,
        RestoreData,
        Unary,
        Binary,
        CallBuiltin,
        CallBuiltinArray,
        CallFunction,
        Duplicate,
        Pop,
        Jump,
        JumpIfFalse,
        CallLabel,
        ReturnSubroutine,
        Return,
        Halt,
    };

    enum class UnaryOperation : std::uint8_t
    {
        Positive,
        Negative,
        LogicalNot,
        BitNot,
    };

    enum class BinaryOperation : std::uint8_t
    {
        Add,
        Subtract,
        Multiply,
        Divide,
        Modulo,
        Power,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        LogicalAnd,
        LogicalOr,
        ShiftLeft,
        ShiftRight,
        BitAnd,
        BitOr,
        BitXor,
    };

    enum class SlotScope : std::uint8_t
    {
        Global,
        Local,
    };

    struct Instruction
    {
        OpCode Op = OpCode::Halt;
        language::SourceSpan Span{};
        language::Type ResultType{};
        std::uint32_t A = 0;
        std::uint32_t B = 0;
        std::uint32_t C = 0;
        std::uint32_t D = 0;
        std::uint32_t E = language::OverloadId::Invalid;
        std::int64_t Integer = 0;
        double Real = 0.0;
    };

    struct Slot
    {
        std::string Name{};
        language::Type ValueType{};
        std::size_t ArrayRank = 0;
    };

    struct TypeField
    {
        std::string Name{};
        language::Type ValueType{};
    };

    struct TypeLayout
    {
        std::string Name{};
        std::vector<TypeField> Fields{};
    };

    struct DataValue
    {
        language::Type ValueType{};
        std::int64_t Integer = 0;
        double Real = 0.0;
        std::uint32_t StringIndex = 0;
    };

    struct Function
    {
        std::string Name{};
        language::Type ReturnType{language::TypeKind::Void, {}};
        std::size_t ParameterCount = 0;
        std::vector<Slot> Locals{};
        std::vector<Instruction> Code{};
    };

    struct Module
    {
        language::LanguageProfile Profile = language::LanguageProfile::Dbc;
        std::filesystem::path LogicalPath{};
        std::vector<Slot> Globals{};
        std::vector<TypeLayout> Types{};
        std::vector<DataValue> Data{};
        std::vector<std::pair<std::string, std::size_t>> DataLabels{};
        std::vector<Function> Functions{};
        std::vector<std::string> Strings{};
        std::size_t EntryFunction = 0;
    };

    std::string_view OpCodeName(OpCode Value);
} // namespace dbc::vm
