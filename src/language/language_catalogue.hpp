#pragma once

#include "language_profile.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace dbc::language
{
    enum class SymbolKind : std::uint8_t
    {
        Reserved,
        Statement,
        Directive,
        Command,
        Function,
        Operator,
    };

    enum class SupportStatus : std::uint8_t
    {
        Implemented,
        CompatibilityStub,
        Unsupported,
    };

    enum class CompatibilityStubPolicy : std::uint8_t
    {
        None,
        Temporary,
        Permanent,
    };

    enum class CompatibilityStubBehavior : std::uint8_t
    {
        None,
        NoOp,
        AcceptInertResource,
        NumericZero,
        NumericOne,
        NumericNegativeOne,
        EmptyString,
    };

    enum class BuiltinConvention : std::uint8_t
    {
        Values,
        Print,
        Sync,
        Wait,
        WaitKey,
        SuspendForMouse,
        Input,
        DataLiteral,
        DataRead,
        DataRestore,
        FileRead,
        LoadArray,
        SaveArray,
        Undim,
        ArrayCollection,
    };

    enum class EvidenceSource : std::uint8_t
    {
        None = 0,
        DbcHelp = 1,
        DbProReference = 2,
        OpenDbcExtension = 4,
        LegacyUnverified = 8,
        OdeExtension = 16,
        ClothParticlesExtension = 32,
        DarkPhysicsExtension = 64,
        DarkDynamixExtension = 128,
    };

    enum class CompatibilitySurface : std::uint8_t
    {
        Core,
        Ode,
        ClothParticles,
        DarkPhysics,
        DarkDynamix,
    };

    enum class SymbolId : std::uint16_t
    {
#define DBC_CATALOGUE(Id, ...) Id,
#define DBC_SIGNATURE(...)
#include "language_catalogue.inc"
#undef DBC_SIGNATURE
#undef DBC_CATALOGUE
        Count,
    };

    struct Arity
    {
        int Min = 0;
        int Max = 0;
        std::uint64_t AcceptedCounts = 0;

        bool Accepts(int Count) const;
        bool IsVariadic() const;
    };

    enum class BuiltinValueType : std::uint8_t
    {
        Any,
        Numeric,
        Integer,
        Real,
        String,
        Void,
    };

    enum class BuiltinParameterMode : std::uint8_t
    {
        Value,
        Output,
        Array,
    };

    struct OverloadId
    {
        static constexpr std::uint32_t Invalid = UINT32_MAX;
        std::uint32_t Value = Invalid;

        explicit operator bool() const
        {
            return Value != Invalid;
        }

        bool operator==(const OverloadId&) const = default;

        bool operator<(const OverloadId& Other) const
        {
            return Value < Other.Value;
        }
    };

    struct BuiltinParameter
    {
        BuiltinValueType ValueType = BuiltinValueType::Any;
        BuiltinParameterMode Mode = BuiltinParameterMode::Value;
    };

    struct BuiltinOverload
    {
        OverloadId Id{};
        SymbolId Symbol{};
        LanguageProfile Profile = LanguageProfile::Dbc;
        Arity SourceArity{};
        std::vector<BuiltinParameter> Parameters{};
        BuiltinValueType ReturnType = BuiltinValueType::Void;
    };

    struct SymbolDescriptor
    {
        SymbolId Id;
        SymbolKind Kind;
        std::string_view CanonicalName;
        std::uint8_t AvailableProfiles;
        Arity DbcArity;
        Arity DbProArity;
        std::uint8_t ImplementedProfiles;
        std::uint8_t CompatibilityStubProfiles;
        std::uint8_t PermanentStubProfiles;
        std::uint8_t Evidence;
        CompatibilitySurface Surface;
    };

    std::span<const SymbolDescriptor> Symbols();
    std::vector<const SymbolDescriptor*> Symbols(LanguageProfile Profile);
    std::vector<const SymbolDescriptor*> Symbols(LanguageProfile Profile, SymbolKind Kind);
    const SymbolDescriptor& Symbol(SymbolId Id);
    const SymbolDescriptor* FindSymbol(LanguageProfile Profile, SymbolKind Kind, std::string_view Spelling);
    const SymbolDescriptor* FindLongestCommandPrefix(LanguageProfile Profile, std::string_view Text);
    bool AvailableIn(const SymbolDescriptor& Descriptor, LanguageProfile Profile);
    SupportStatus SupportFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile);
    CompatibilityStubPolicy StubPolicyFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile);
    CompatibilityStubBehavior StubBehaviorFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile);
    bool ExecutableIn(const SymbolDescriptor& Descriptor, LanguageProfile Profile);
    std::optional<Arity> ArityFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile);
    std::span<const BuiltinOverload> Overloads();
    const BuiltinOverload* Overload(OverloadId Id);
    std::vector<const BuiltinOverload*> Overloads(SymbolId Id, LanguageProfile Profile);
    std::vector<const BuiltinOverload*> Overloads(SymbolId Id, LanguageProfile Profile, int SourceArgumentCount);
    const BuiltinOverload* FindOverload(SymbolId Id, LanguageProfile Profile, int SourceArgumentCount);
    bool HasEvidence(const SymbolDescriptor& Descriptor, EvidenceSource Source);
    BuiltinConvention ConventionFor(SymbolId Id);
} // namespace dbc::language
