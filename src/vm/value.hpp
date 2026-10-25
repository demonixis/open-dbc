#pragma once

#include "language/frontend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace dbc::vm
{
    struct RecordValue;
    struct ArrayValue;

    class Value
    {
      public:
        using Payload = std::variant<std::monostate, std::int64_t, double, std::string, std::shared_ptr<RecordValue>,
                                     std::shared_ptr<ArrayValue>>;

        Value() = default;
        Value(const Value& Other);
        Value& operator=(const Value& Other);
        Value(Value&&) noexcept = default;
        Value& operator=(Value&&) noexcept = default;

        static Value Integer(std::int64_t Value);
        static Value Real(double Value);
        static Value String(std::string Value);
        static Value Record(std::shared_ptr<RecordValue> Value, language::Type Type);
        static Value Array(std::shared_ptr<ArrayValue> Value, language::Type ElementType);

        const language::Type& Type() const;
        const Payload& Data() const;
        Payload& Data();
        bool IsTruthy() const;
        std::int64_t AsInteger() const;
        bool AsInteger(std::int64_t& Result, std::string& Error) const;
        double AsReal() const;
        std::string AsString() const;
        bool ConvertTo(const language::Type& Target, Value& Result, std::string& Error) const;
        Value Clone() const;

      private:
        language::Type Type_{};
        Payload Data_{};
    };

    struct RecordValue
    {
        std::string TypeName{};
        std::unordered_map<std::string, Value> Fields{};
    };

    struct ArrayValue
    {
        language::Type ElementType{};
        std::vector<std::size_t> Dimensions{};
        std::vector<Value> Elements{};
        std::int64_t CurrentIndex = 0;

        bool FlatIndex(const std::vector<std::int64_t>& Indices, std::size_t& Result, std::string& Error) const;
    };
} // namespace dbc::vm
