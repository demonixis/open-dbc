#include "value.hpp"
#include "language/numeric_conversion.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace dbc::vm
{
    Value::Value(const Value& Other) : Type_(Other.Type_), Data_(Other.Clone().Data_) {}

    Value& Value::operator=(const Value& Other)
    {
        if (this != &Other)
        {
            Type_ = Other.Type_;
            Data_ = Other.Clone().Data_;
        }
        return *this;
    }

    Value Value::Integer(std::int64_t Number)
    {
        Value Result;
        Result.Type_ = {language::TypeKind::Integer, {}};
        Result.Data_ = Number;
        return Result;
    }

    Value Value::Real(double Number)
    {
        Value Result;
        Result.Type_ = {language::TypeKind::Real, {}};
        Result.Data_ = Number;
        return Result;
    }

    Value Value::String(std::string Text)
    {
        Value Result;
        Result.Type_ = {language::TypeKind::String, {}};
        Result.Data_ = std::move(Text);
        return Result;
    }

    Value Value::Record(std::shared_ptr<RecordValue> Record, language::Type Type)
    {
        Value Result;
        Result.Type_ = std::move(Type);
        Result.Data_ = std::move(Record);
        return Result;
    }

    Value Value::Array(std::shared_ptr<ArrayValue> Array, language::Type ElementType)
    {
        Value Result;
        Result.Type_ = std::move(ElementType);
        Result.Data_ = std::move(Array);
        return Result;
    }

    const language::Type& Value::Type() const
    {
        return Type_;
    }

    const Value::Payload& Value::Data() const
    {
        return Data_;
    }

    Value::Payload& Value::Data()
    {
        return Data_;
    }

    bool Value::IsTruthy() const
    {
        if (std::holds_alternative<std::int64_t>(Data_))
            return std::get<std::int64_t>(Data_) != 0;
        if (std::holds_alternative<double>(Data_))
            return std::get<double>(Data_) != 0.0;
        if (std::holds_alternative<std::string>(Data_))
            return !std::get<std::string>(Data_).empty();
        return !std::holds_alternative<std::monostate>(Data_);
    }

    std::int64_t Value::AsInteger() const
    {
        std::int64_t Result = 0;
        std::string Error;
        const bool Valid = AsInteger(Result, Error);
        assert(Valid);
        (void)Valid;
        return Result;
    }

    bool Value::AsInteger(std::int64_t& Result, std::string& Error) const
    {
        if (std::holds_alternative<std::int64_t>(Data_))
        {
            Result = std::get<std::int64_t>(Data_);
            return true;
        }
        if (std::holds_alternative<double>(Data_))
            return language::ConvertInteger(std::get<double>(Data_), Result, &Error);
        Error = "Value is not numeric";
        return false;
    }

    double Value::AsReal() const
    {
        if (std::holds_alternative<double>(Data_))
            return std::get<double>(Data_);
        if (std::holds_alternative<std::int64_t>(Data_))
            return static_cast<double>(std::get<std::int64_t>(Data_));
        assert(false && "Numeric access requires a validated numeric value");
        return 0.0;
    }

    std::string Value::AsString() const
    {
        if (std::holds_alternative<std::string>(Data_))
            return std::get<std::string>(Data_);
        if (std::holds_alternative<std::int64_t>(Data_))
            return std::to_string(std::get<std::int64_t>(Data_));
        if (std::holds_alternative<double>(Data_))
        {
            char Buffer[32];
            const int Count = std::snprintf(Buffer, sizeof(Buffer), "%.12g", std::get<double>(Data_));
            if (Count <= 0 || Count >= static_cast<int>(sizeof(Buffer)))
                return {};
            return std::string(Buffer, static_cast<std::size_t>(Count));
        }
        assert(false && "String formatting requires a scalar value");
        return {};
    }

    bool Value::ConvertTo(const language::Type& Target, Value& Result, std::string& Error) const
    {
        if (Target.Kind == language::TypeKind::Boolean || Target.Kind == language::TypeKind::Integer)
        {
            std::int64_t Number = 0;
            if (!AsInteger(Number, Error))
                return false;
            Result = Integer(Number);
            return true;
        }
        if (Target.Kind == language::TypeKind::Real &&
            (std::holds_alternative<double>(Data_) || std::holds_alternative<std::int64_t>(Data_)))
        {
            Result = Real(AsReal());
            return true;
        }
        if (Target.Kind == language::TypeKind::String && std::holds_alternative<std::string>(Data_))
        {
            Result = Clone();
            return true;
        }
        if (Target.Kind == language::TypeKind::UserDefined && Target == Type_)
        {
            const auto* Record = std::get_if<std::shared_ptr<RecordValue>>(&Data_);
            if (Record && *Record)
            {
                Result = Clone();
                return true;
            }
        }
        Error = "Invalid value conversion";
        return false;
    }

    Value Value::Clone() const
    {
        Value Result;
        Result.Type_ = Type_;
        if (const auto* Record = std::get_if<std::shared_ptr<RecordValue>>(&Data_))
        {
            if (!*Record)
            {
                Result.Data_ = *Record;
                return Result;
            }
            auto Copy = std::make_shared<RecordValue>();
            Copy->TypeName = (*Record)->TypeName;
            for (const auto& [Name, Field] : (*Record)->Fields)
                Copy->Fields.emplace(Name, Field.Clone());
            Result.Data_ = std::move(Copy);
        }
        else if (const auto* Array = std::get_if<std::shared_ptr<ArrayValue>>(&Data_))
        {
            if (!*Array)
            {
                Result.Data_ = *Array;
                return Result;
            }
            auto Copy = std::make_shared<ArrayValue>();
            Copy->ElementType = (*Array)->ElementType;
            Copy->Dimensions = (*Array)->Dimensions;
            Copy->CurrentIndex = (*Array)->CurrentIndex;
            Copy->Elements.reserve((*Array)->Elements.size());
            for (const Value& Element : (*Array)->Elements)
                Copy->Elements.push_back(Element.Clone());
            Result.Data_ = std::move(Copy);
        }
        else
        {
            Result.Data_ = Data_;
        }
        return Result;
    }

    bool ArrayValue::FlatIndex(const std::vector<std::int64_t>& Indices, std::size_t& Result, std::string& Error) const
    {
        if (Indices.size() != Dimensions.size())
        {
            Error = "Array rank mismatch";
            return false;
        }
        Result = 0;
        for (std::size_t Dimension = 0; Dimension < Dimensions.size(); ++Dimension)
        {
            const auto Index = static_cast<std::uint64_t>(Indices[Dimension]);
            if (Indices[Dimension] < 0 || Index >= Dimensions[Dimension])
            {
                Error = "Array index is out of bounds";
                return false;
            }
            if (Result > (std::numeric_limits<std::size_t>::max() - Index) / Dimensions[Dimension])
            {
                Error = "Array size is too large";
                return false;
            }
            Result = Result * Dimensions[Dimension] + static_cast<std::size_t>(Index);
        }
        if (Result >= Elements.size())
        {
            Error = "Array storage does not match its dimensions";
            return false;
        }
        return true;
    }
} // namespace dbc::vm
