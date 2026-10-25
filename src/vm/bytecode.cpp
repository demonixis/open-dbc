#include "bytecode.hpp"

namespace dbc::vm
{
    std::string_view OpCodeName(OpCode Value)
    {
        switch (Value)
        {
            case OpCode::PushInteger:
                return "PushInteger";
            case OpCode::PushReal:
                return "PushReal";
            case OpCode::PushString:
                return "PushString";
            case OpCode::LoadGlobal:
                return "LoadGlobal";
            case OpCode::StoreGlobal:
                return "StoreGlobal";
            case OpCode::LoadLocal:
                return "LoadLocal";
            case OpCode::StoreLocal:
                return "StoreLocal";
            case OpCode::CreateArray:
                return "CreateArray";
            case OpCode::LoadArray:
                return "LoadArray";
            case OpCode::StoreArray:
                return "StoreArray";
            case OpCode::DestroyArray:
                return "DestroyArray";
            case OpCode::LoadField:
                return "LoadField";
            case OpCode::StoreField:
                return "StoreField";
            case OpCode::BeginBuiltin:
                return "BeginBuiltin";
            case OpCode::CompleteBuiltin:
                return "CompleteBuiltin";
            case OpCode::ReadData:
                return "ReadData";
            case OpCode::RestoreData:
                return "RestoreData";
            case OpCode::Unary:
                return "Unary";
            case OpCode::Binary:
                return "Binary";
            case OpCode::CallBuiltin:
                return "CallBuiltin";
            case OpCode::CallBuiltinArray:
                return "CallBuiltinArray";
            case OpCode::CallFunction:
                return "CallFunction";
            case OpCode::Duplicate:
                return "Duplicate";
            case OpCode::Pop:
                return "Pop";
            case OpCode::Jump:
                return "Jump";
            case OpCode::JumpIfFalse:
                return "JumpIfFalse";
            case OpCode::CallLabel:
                return "CallLabel";
            case OpCode::ReturnSubroutine:
                return "ReturnSubroutine";
            case OpCode::Return:
                return "Return";
            case OpCode::Halt:
                return "Halt";
        }
        return "Unknown";
    }
} // namespace dbc::vm
