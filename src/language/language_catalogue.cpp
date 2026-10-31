#include "language_catalogue.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <string>
#include <unordered_map>

namespace
{
    using dbc::language::LanguageProfile;
    using dbc::language::SymbolDescriptor;
    using dbc::language::SymbolKind;

    constexpr std::uint8_t ProfileBit(LanguageProfile Profile)
    {
        return Profile == LanguageProfile::Dbc ? 1 : 2;
    }

    constexpr dbc::language::CompatibilitySurface SurfaceForEvidence(std::uint8_t Evidence)
    {
        using dbc::language::CompatibilitySurface;
        using dbc::language::EvidenceSource;
        if ((Evidence & static_cast<std::uint8_t>(EvidenceSource::OdeExtension)) != 0)
            return CompatibilitySurface::Ode;
        return CompatibilitySurface::Core;
    }

    constexpr bool IsPermanentCompatibilityStub(dbc::language::SymbolId Id)
    {
        using dbc::language::SymbolId;
        switch (Id)
        {
            case SymbolId::Command_DisableSystemkeys:
            case SymbolId::Command_DisableTnl:
            case SymbolId::Command_EnableSystemkeys:
            case SymbolId::Command_EnableTnl:
            case SymbolId::Command_FtpConnect:
            case SymbolId::Command_FtpDeleteFile:
            case SymbolId::Command_FtpDisconnect:
            case SymbolId::Command_FtpFindFirst:
            case SymbolId::Command_FtpFindNext:
            case SymbolId::Command_FtpGetFile:
            case SymbolId::Command_FtpProceed:
            case SymbolId::Command_FtpPutFile:
            case SymbolId::Command_FtpSetDir:
            case SymbolId::Command_FtpTerminate:
            case SymbolId::Command_LoadCdmusic:
            case SymbolId::Command_SetEax:
            case SymbolId::Command_SetEmulationOff:
            case SymbolId::Command_SetEmulationOn:
            case SymbolId::Command_SetGraphicsCard:
            case SymbolId::Function_EmulationMode:
            case SymbolId::Function_GetFtpDirString:
            case SymbolId::Function_GetFtpErrorString:
            case SymbolId::Function_GetFtpFailure:
            case SymbolId::Function_GetFtpFileNameString:
            case SymbolId::Function_GetFtpFileSize:
            case SymbolId::Function_GetFtpFileType:
            case SymbolId::Function_GetFtpProgress:
            case SymbolId::Function_GetFtpStatus:
            case SymbolId::Function_WindirString:
                return true;
            default:
                return false;
        }
    }

    constexpr std::uint8_t CompatibilityStubProfiles(std::uint8_t Profiles, std::uint8_t Implemented)
    {
        constexpr std::uint8_t DbcProfile = 1;
        return static_cast<std::uint8_t>(Profiles & DbcProfile & ~Implemented);
    }

    std::string UpperAscii(std::string_view Value)
    {
        std::string Result(Value);
        std::transform(Result.begin(), Result.end(), Result.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
        return Result;
    }

    std::string LookupKey(LanguageProfile Profile, SymbolKind Kind, std::string_view Spelling)
    {
        std::string Key;
        Key.reserve(Spelling.size() + 4);
        Key.push_back(static_cast<char>('0' + static_cast<int>(Profile)));
        Key.push_back(':');
        Key.push_back(static_cast<char>('0' + static_cast<int>(Kind)));
        Key.push_back(':');
        Key += UpperAscii(Spelling);
        return Key;
    }

    constexpr std::array<SymbolDescriptor, static_cast<std::size_t>(dbc::language::SymbolId::Count)> KSymbols = {{
#define DBC_CATALOGUE(Id, Kind, Name, Profiles, DbcMin, DbcMax, DbcCounts, DbProMin, DbProMax, DbProCounts,            \
                      Implemented, Evidence)                                                                           \
    {dbc::language::SymbolId::Id,                                                                                      \
     dbc::language::SymbolKind::Kind,                                                                                  \
     Name,                                                                                                             \
     Profiles,                                                                                                         \
     {DbcMin, DbcMax, DbcCounts},                                                                                      \
     {DbProMin, DbProMax, DbProCounts},                                                                                \
     Implemented,                                                                                                      \
     CompatibilityStubProfiles(Profiles, Implemented),                                                                 \
     static_cast<std::uint8_t>(CompatibilityStubProfiles(Profiles, Implemented) &                                      \
                               (IsPermanentCompatibilityStub(dbc::language::SymbolId::Id) ? 1 : 0)),                   \
     Evidence,                                                                                                         \
     SurfaceForEvidence(Evidence)},
#define DBC_SIGNATURE(...)
#include "language_catalogue.inc"
#undef DBC_SIGNATURE
#undef DBC_CATALOGUE
    }};

    const std::unordered_map<std::string, const SymbolDescriptor*>& SymbolIndex()
    {
        static const std::unordered_map<std::string, const SymbolDescriptor*> Index = []
        {
            std::unordered_map<std::string, const SymbolDescriptor*> Result;
            Result.reserve(KSymbols.size() * 2);
            for (const SymbolDescriptor& Descriptor : KSymbols)
            {
                for (const LanguageProfile Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
                {
                    if ((Descriptor.AvailableProfiles & ProfileBit(Profile)) == 0)
                    {
                        continue;
                    }
                    const std::string Key = LookupKey(Profile, Descriptor.Kind, Descriptor.CanonicalName);
                    const bool Inserted = Result.emplace(Key, &Descriptor).second;
                    assert(Inserted && "Duplicate language catalogue spelling");
                    (void)Inserted;
                }
            }
            return Result;
        }();
        return Index;
    }
} // namespace

namespace dbc::language
{
    namespace
    {
        struct SignatureDescriptor
        {
            SymbolId Symbol;
            LanguageProfile Profile;
            Arity SourceArity;
            BuiltinValueType ReturnType;
            std::string_view Parameters;
        };

        constexpr auto KSignatures = std::to_array<SignatureDescriptor>({
#define DBC_CATALOGUE(...)
#define DBC_SIGNATURE(Symbol, Profile, Min, Max, Counts, Return, Parameters)                                           \
    {SymbolId::Symbol, LanguageProfile::Profile, {Min, Max, Counts}, BuiltinValueType::Return, Parameters},
#include "language_catalogue.inc"
#undef DBC_SIGNATURE
#undef DBC_CATALOGUE
        });

        BuiltinParameter ParseParameter(std::string_view Token)
        {
            BuiltinParameter Result;
            if (Token.ends_with("[]"))
            {
                Result.Mode = BuiltinParameterMode::Array;
                Token.remove_suffix(2);
            }
            else if (Token.ends_with('!'))
            {
                Result.Mode = BuiltinParameterMode::Output;
                Token.remove_suffix(1);
            }
            if (Token == "A")
                Result.ValueType = BuiltinValueType::Any;
            else if (Token == "N")
                Result.ValueType = BuiltinValueType::Numeric;
            else if (Token == "I")
                Result.ValueType = BuiltinValueType::Integer;
            else if (Token == "R")
                Result.ValueType = BuiltinValueType::Real;
            else if (Token == "S")
                Result.ValueType = BuiltinValueType::String;
            else
                assert(false && "Invalid explicit builtin parameter type");
            return Result;
        }

        void ApplySignature(BuiltinOverload& Value, const SignatureDescriptor& Signature)
        {
            Value.ReturnType = Signature.ReturnType;
            Value.Parameters.clear();
            if (Signature.Parameters == "-")
                return;
            std::string_view Parameters = Signature.Parameters;
            if (Parameters.ends_with('*'))
            {
                Parameters.remove_suffix(1);
                const BuiltinParameter Repeated = ParseParameter(Parameters);
                Value.Parameters.assign(static_cast<std::size_t>(Value.SourceArity.Max), Repeated);
                return;
            }
            while (!Parameters.empty())
            {
                const std::size_t Separator = Parameters.find(',');
                const std::string_view Token = Parameters.substr(0, Separator);
                Value.Parameters.push_back(ParseParameter(Token));
                if (Separator == std::string_view::npos)
                    break;
                Parameters.remove_prefix(Separator + 1);
            }
            assert(Value.Parameters.size() == static_cast<std::size_t>(Value.SourceArity.Max));
        }

        const std::vector<BuiltinOverload>& BuiltinOverloads()
        {
            static const std::vector<BuiltinOverload> Values = []
            {
                std::vector<BuiltinOverload> Result;
                for (const SymbolDescriptor& Descriptor : KSymbols)
                {
                    if (Descriptor.Kind != SymbolKind::Command && Descriptor.Kind != SymbolKind::Function)
                        continue;
                    for (const LanguageProfile Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
                    {
                        const std::optional<Arity> ProfileArity = ArityFor(Descriptor, Profile);
                        if (!ProfileArity)
                            continue;
                        bool Found = false;
                        for (const SignatureDescriptor& Signature : KSignatures)
                        {
                            if (Signature.Symbol != Descriptor.Id || Signature.Profile != Profile)
                                continue;
                            Found = true;
                            BuiltinOverload Value;
                            Value.Id = {static_cast<std::uint32_t>(Result.size())};
                            Value.Symbol = Descriptor.Id;
                            Value.Profile = Profile;
                            Value.SourceArity = Signature.SourceArity;
                            ApplySignature(Value, Signature);
                            Result.push_back(std::move(Value));
                        }
                        assert(Found && "Missing explicit builtin overload signature");
                        (void)Found;
                    }
                }
                return Result;
            }();
            return Values;
        }
    } // namespace

    bool Arity::Accepts(int Count) const
    {
        if (Count < 0)
        {
            return false;
        }
        if (AcceptedCounts != 0)
        {
            return Count < 64 && (AcceptedCounts & (std::uint64_t{1} << Count)) != 0;
        }
        return Count >= Min && Count <= Max;
    }

    bool Arity::IsVariadic() const
    {
        return AcceptedCounts == 0 && Max > 63;
    }

    std::span<const SymbolDescriptor> Symbols()
    {
        return KSymbols;
    }

    std::vector<const SymbolDescriptor*> Symbols(LanguageProfile Profile)
    {
        std::vector<const SymbolDescriptor*> Result;
        for (const SymbolDescriptor& Descriptor : KSymbols)
        {
            if (AvailableIn(Descriptor, Profile))
            {
                Result.push_back(&Descriptor);
            }
        }
        return Result;
    }

    std::vector<const SymbolDescriptor*> Symbols(LanguageProfile Profile, SymbolKind Kind)
    {
        std::vector<const SymbolDescriptor*> Result;
        for (const SymbolDescriptor& Descriptor : KSymbols)
        {
            if (Descriptor.Kind == Kind && AvailableIn(Descriptor, Profile))
            {
                Result.push_back(&Descriptor);
            }
        }
        return Result;
    }

    const SymbolDescriptor& Symbol(SymbolId Id)
    {
        const std::size_t Index = static_cast<std::size_t>(Id);
        assert(Index < KSymbols.size());
        return KSymbols[Index];
    }

    const SymbolDescriptor* FindSymbol(LanguageProfile Profile, SymbolKind Kind, std::string_view Spelling)
    {
        const auto& Index = SymbolIndex();
        const auto It = Index.find(LookupKey(Profile, Kind, Spelling));
        return It == Index.end() ? nullptr : It->second;
    }

    std::string_view RemovedPhysicsPlugin(std::string_view CanonicalName)
    {
        if (CanonicalName.starts_with("PHY ") || CanonicalName == "PHYSX")
            return "DarkPHYSICS";
        if (CanonicalName.starts_with("DYN "))
            return "Dark Dynamix";
        static constexpr std::string_view ClothForms[] = {
#include "removed_physics_forms.inc"
        };
        for (std::string_view Name = CanonicalName;;)
        {
            if (std::binary_search(std::begin(ClothForms), std::end(ClothForms), Name))
                return "3D Cloth & Particles";
            const std::size_t Space = Name.rfind(' ');
            if (Space == std::string_view::npos)
                return {};
            Name = Name.substr(0, Space);
        }
    }

    std::size_t LongestCommandWords()
    {
        static const std::size_t Longest = []
        {
            std::size_t Result = 0;
            for (const SymbolDescriptor& Descriptor : KSymbols)
            {
                if (Descriptor.Kind == SymbolKind::Command)
                {
                    const auto Spaces =
                        std::count(Descriptor.CanonicalName.begin(), Descriptor.CanonicalName.end(), ' ');
                    Result = std::max(Result, static_cast<std::size_t>(Spaces) + 1);
                }
            }
            return Result;
        }();
        return Longest;
    }

    const SymbolDescriptor* FindLongestCommandPrefix(LanguageProfile Profile, std::string_view Text)
    {
        const std::string Upper = UpperAscii(Text);
        const SymbolDescriptor* Best = nullptr;
        for (const SymbolDescriptor& Entry : KSymbols)
        {
            const SymbolDescriptor* Descriptor = &Entry;
            if (Descriptor->Kind != SymbolKind::Command || !AvailableIn(*Descriptor, Profile))
            {
                continue;
            }
            const std::string_view Name = Descriptor->CanonicalName;
            if (Upper.size() < Name.size() || Upper.compare(0, Name.size(), Name) != 0)
            {
                continue;
            }
            if (Upper.size() > Name.size())
            {
                const unsigned char Next = static_cast<unsigned char>(Upper[Name.size()]);
                if (std::isalnum(Next) != 0 || Next == '_' || Next == '$' || Next == '#')
                {
                    continue;
                }
            }
            if (!Best || Name.size() > Best->CanonicalName.size())
            {
                Best = Descriptor;
            }
        }
        return Best;
    }

    bool AvailableIn(const SymbolDescriptor& Descriptor, LanguageProfile Profile)
    {
        return (Descriptor.AvailableProfiles & ProfileBit(Profile)) != 0;
    }

    SupportStatus SupportFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile)
    {
        const std::uint8_t Bit = ProfileBit(Profile);
        if ((Descriptor.ImplementedProfiles & Bit) != 0)
            return SupportStatus::Implemented;
        if ((Descriptor.CompatibilityStubProfiles & Bit) != 0)
            return SupportStatus::CompatibilityStub;
        return SupportStatus::Unsupported;
    }

    CompatibilityStubPolicy StubPolicyFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile)
    {
        const std::uint8_t Bit = ProfileBit(Profile);
        if ((Descriptor.CompatibilityStubProfiles & Bit) == 0)
            return CompatibilityStubPolicy::None;
        return (Descriptor.PermanentStubProfiles & Bit) != 0 ? CompatibilityStubPolicy::Permanent
                                                             : CompatibilityStubPolicy::Temporary;
    }

    CompatibilityStubBehavior StubBehaviorFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile)
    {
        if (SupportFor(Descriptor, Profile) != SupportStatus::CompatibilityStub)
            return CompatibilityStubBehavior::None;

        switch (Descriptor.Id)
        {
            case SymbolId::Command_LoadCdmusic:
                return CompatibilityStubBehavior::AcceptInertResource;
            case SymbolId::Function_GetFtpFailure:
                return CompatibilityStubBehavior::NumericOne;
            case SymbolId::Function_GetFtpFileType:
            case SymbolId::Function_GetFtpProgress:
                return CompatibilityStubBehavior::NumericNegativeOne;
            case SymbolId::Function_GetFtpDirString:
            case SymbolId::Function_GetFtpErrorString:
            case SymbolId::Function_GetFtpFileNameString:
            case SymbolId::Function_WindirString:
                return CompatibilityStubBehavior::EmptyString;
            default:
                return Descriptor.Kind == SymbolKind::Command ? CompatibilityStubBehavior::NoOp
                                                              : CompatibilityStubBehavior::NumericZero;
        }
    }

    bool ExecutableIn(const SymbolDescriptor& Descriptor, LanguageProfile Profile)
    {
        return AvailableIn(Descriptor, Profile) && SupportFor(Descriptor, Profile) != SupportStatus::Unsupported;
    }

    std::optional<Arity> ArityFor(const SymbolDescriptor& Descriptor, LanguageProfile Profile)
    {
        if (!AvailableIn(Descriptor, Profile) ||
            (Descriptor.Kind != SymbolKind::Command && Descriptor.Kind != SymbolKind::Function))
        {
            return std::nullopt;
        }
        return Profile == LanguageProfile::Dbc ? Descriptor.DbcArity : Descriptor.DbProArity;
    }

    std::span<const BuiltinOverload> Overloads()
    {
        return BuiltinOverloads();
    }

    const BuiltinOverload* Overload(OverloadId Id)
    {
        const auto& Values = BuiltinOverloads();
        return Id.Value < Values.size() ? &Values[Id.Value] : nullptr;
    }

    std::vector<const BuiltinOverload*> Overloads(SymbolId Id, LanguageProfile Profile)
    {
        std::vector<const BuiltinOverload*> Result;
        for (const BuiltinOverload& Value : BuiltinOverloads())
        {
            if (Value.Symbol == Id && Value.Profile == Profile)
                Result.push_back(&Value);
        }
        return Result;
    }

    std::vector<const BuiltinOverload*> Overloads(SymbolId Id, LanguageProfile Profile, int SourceArgumentCount)
    {
        std::vector<const BuiltinOverload*> Result;
        for (const BuiltinOverload& Value : BuiltinOverloads())
        {
            if (Value.Symbol == Id && Value.Profile == Profile && Value.SourceArity.Accepts(SourceArgumentCount))
                Result.push_back(&Value);
        }
        return Result;
    }

    const BuiltinOverload* FindOverload(SymbolId Id, LanguageProfile Profile, int SourceArgumentCount)
    {
        for (const BuiltinOverload& Value : BuiltinOverloads())
        {
            if (Value.Symbol == Id && Value.Profile == Profile && Value.SourceArity.Accepts(SourceArgumentCount))
                return &Value;
        }
        return nullptr;
    }

    bool HasEvidence(const SymbolDescriptor& Descriptor, EvidenceSource Source)
    {
        return (Descriptor.Evidence & static_cast<std::uint8_t>(Source)) != 0;
    }

    BuiltinConvention ConventionFor(SymbolId Id)
    {
        switch (Id)
        {
            case SymbolId::Command_Print:
                return BuiltinConvention::Print;
            case SymbolId::Command_Sync:
                return BuiltinConvention::Sync;
            case SymbolId::Command_Sleep:
            case SymbolId::Command_Wait:
                return BuiltinConvention::Wait;
            case SymbolId::Command_SuspendForKey:
            case SymbolId::Command_WaitKey:
                return BuiltinConvention::WaitKey;
            case SymbolId::Command_SuspendForMouse:
            case SymbolId::Command_WaitMouse:
                return BuiltinConvention::SuspendForMouse;
            case SymbolId::Command_Input:
                return BuiltinConvention::Input;
            case SymbolId::Command_Data:
                return BuiltinConvention::DataLiteral;
            case SymbolId::Command_Read:
            case SymbolId::Statement_Read:
                return BuiltinConvention::DataRead;
            case SymbolId::Command_Restore:
            case SymbolId::Statement_Restore:
                return BuiltinConvention::DataRestore;
            case SymbolId::Command_ReadByte:
            case SymbolId::Command_ReadWord:
            case SymbolId::Command_ReadLong:
            case SymbolId::Command_ReadFloat:
            case SymbolId::Command_ReadString:
            case SymbolId::Command_ReadFile:
                return BuiltinConvention::FileRead;
            case SymbolId::Command_LoadArray:
                return BuiltinConvention::LoadArray;
            case SymbolId::Command_SaveArray:
                return BuiltinConvention::SaveArray;
            case SymbolId::Command_Undim:
                return BuiltinConvention::Undim;
            case SymbolId::Command_AddToQueue:
            case SymbolId::Command_AddToStack:
            case SymbolId::Function_ArrayCount:
            case SymbolId::Command_ArrayDeleteElement:
            case SymbolId::Command_ArrayIndexToBottom:
            case SymbolId::Command_ArrayIndexToQueue:
            case SymbolId::Command_ArrayIndexToStack:
            case SymbolId::Command_ArrayIndexToTop:
            case SymbolId::Function_ArrayIndexValid:
            case SymbolId::Command_ArrayInsertAtBottom:
            case SymbolId::Command_ArrayInsertAtElement:
            case SymbolId::Command_ArrayInsertAtTop:
            case SymbolId::Command_EmptyArray:
            case SymbolId::Command_NextArrayIndex:
            case SymbolId::Command_PreviousArrayIndex:
            case SymbolId::Command_RemoveFromQueue:
            case SymbolId::Command_RemoveFromStack:
                return BuiltinConvention::ArrayCollection;
            default:
                return BuiltinConvention::Values;
        }
    }
} // namespace dbc::language
