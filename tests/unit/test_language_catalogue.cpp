#include <doctest/doctest.h>

#include "language/language_catalogue.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

using dbc::language::LanguageProfile;
using dbc::language::SupportStatus;
using dbc::language::SymbolKind;

namespace
{
    std::string_view CatalogueSymbolName(dbc::language::SymbolId Id)
    {
        switch (Id)
        {
#define DBC_CATALOGUE(Name, ...)                                                                                       \
    case dbc::language::SymbolId::Name:                                                                                \
        return #Name;
#define DBC_SIGNATURE(...)
#include "language/language_catalogue.inc"
#undef DBC_SIGNATURE
#undef DBC_CATALOGUE
            case dbc::language::SymbolId::Count:
                break;
        }
        return {};
    }

    std::string_view NeutralBehaviorName(dbc::language::CompatibilityStubBehavior Behavior)
    {
        using dbc::language::CompatibilityStubBehavior;
        switch (Behavior)
        {
            case CompatibilityStubBehavior::NoOp:
                return "no-op";
            case CompatibilityStubBehavior::AcceptInertResource:
                return "accept-inert-resource-id";
            case CompatibilityStubBehavior::NumericZero:
                return "numeric:0";
            case CompatibilityStubBehavior::NumericOne:
                return "numeric:1";
            case CompatibilityStubBehavior::NumericNegativeOne:
                return "numeric:-1";
            case CompatibilityStubBehavior::EmptyString:
                return "string:empty";
            case CompatibilityStubBehavior::None:
                return {};
        }
        return {};
    }
} // namespace

TEST_CASE("The language catalogue has unique typed spellings and provenance")
{
    std::set<std::tuple<LanguageProfile, SymbolKind, std::string>> Keys;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        CHECK(Descriptor.Evidence != 0);
        for (const LanguageProfile Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
        {
            if (!dbc::language::AvailableIn(Descriptor, Profile))
            {
                continue;
            }
            CHECK(Keys.emplace(Profile, Descriptor.Kind, std::string(Descriptor.CanonicalName)).second);
        }
    }
}

TEST_CASE("Retired symbol ids remain reserved and can never be reused")
{
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        CAPTURE(Descriptor.CanonicalName);
        CHECK((Descriptor.Kind == SymbolKind::Reserved) == (Descriptor.AvailableProfiles == 0));
        if (Descriptor.Kind == SymbolKind::Reserved)
        {
            CHECK(Descriptor.ImplementedProfiles == 0);
            CHECK(Descriptor.CompatibilityStubProfiles == 0);
            CHECK(Descriptor.PermanentStubProfiles == 0);
        }
    }
}

TEST_CASE("DBPro extension surfaces are typed profile-only catalogue data")
{
    using dbc::language::CompatibilitySurface;
    using dbc::language::EvidenceSource;
    using dbc::language::SymbolId;

    std::size_t ExtensionSymbols = 0;
    std::size_t ImplementedExtensionSymbols = 0;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (Descriptor.Surface == CompatibilitySurface::Core)
            continue;
        ++ExtensionSymbols;
        CHECK_FALSE(dbc::language::AvailableIn(Descriptor, LanguageProfile::Dbc));
        CHECK(dbc::language::AvailableIn(Descriptor, LanguageProfile::DbPro));
        if (dbc::language::SupportFor(Descriptor, LanguageProfile::DbPro) == SupportStatus::Implemented)
        {
            ++ImplementedExtensionSymbols;
            CHECK(Descriptor.Surface == CompatibilitySurface::Ode);
        }
        else
        {
            CHECK(dbc::language::SupportFor(Descriptor, LanguageProfile::DbPro) == SupportStatus::Unsupported);
        }
    }
    CHECK(ExtensionSymbols == 1270);
    CHECK(ImplementedExtensionSymbols == 20);

    CHECK(dbc::language::Symbol(SymbolId::Command_OdeStart).Kind == SymbolKind::Reserved);
    const auto& ActiveOde = dbc::language::Symbol(SymbolId::Command_ExtOdeStart);
    CHECK(ActiveOde.Kind == SymbolKind::Command);
    CHECK(ActiveOde.Surface == CompatibilitySurface::Ode);
    CHECK(dbc::language::HasEvidence(ActiveOde, EvidenceSource::OdeExtension));
}

TEST_CASE("Extension evidence retains command function and same-arity type overloads")
{
    const auto* Command =
        dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "DYN COOK CONVEX MESH");
    const auto* Function =
        dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Function, "DYN COOK CONVEX MESH");
    REQUIRE(Command != nullptr);
    REQUIRE(Function != nullptr);
    CHECK(Command->Id != Function->Id);

    const auto* Convex =
        dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "PHY MAKE RIGID BODY STATIC CONVEX");
    REQUIRE(Convex != nullptr);
    const auto Calls = dbc::language::Overloads(Convex->Id, LanguageProfile::DbPro, 2);
    REQUIRE(Calls.size() == 2);
    REQUIRE(Calls[0]->Parameters.size() == 2);
    REQUIRE(Calls[1]->Parameters.size() == 2);
    CHECK(Calls[0]->Parameters[1].ValueType != Calls[1]->Parameters[1].ValueType);
}
TEST_CASE("DBPro math value creation and deletion retain command and integer function forms")
{
    for (const std::string_view Name : {"MAKE VECTOR2", "MAKE VECTOR3", "MAKE VECTOR4", "MAKE MATRIX4",
                                        "DELETE VECTOR2", "DELETE VECTOR3", "DELETE VECTOR4", "DELETE MATRIX4"})
    {
        CAPTURE(Name);
        const auto* Command = dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, Name);
        const auto* Function = dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Function, Name);
        REQUIRE(Command != nullptr);
        REQUIRE(Function != nullptr);
        CHECK(Command->Id != Function->Id);
        CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, Name) == nullptr);
        const dbc::language::BuiltinOverload* Overload =
            dbc::language::FindOverload(Function->Id, LanguageProfile::DbPro, 1);
        REQUIRE(Overload != nullptr);
        CHECK(Overload->ReturnType == dbc::language::BuiltinValueType::Integer);
    }
}

TEST_CASE("Classic and Professional availability is explicit and asymmetric")
{
    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "BOB") != nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "BOB") == nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "ADD VECTOR4") == nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "ADD VECTOR4") != nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Statement, "LOCAL") == nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Statement, "LOCAL") != nullptr);
}

TEST_CASE("Profile-specific arities preserve widened and disjoint signatures")
{
    const auto* PositionCamera =
        dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "POSITION CAMERA");
    REQUIRE(PositionCamera != nullptr);
    const auto Classic = dbc::language::ArityFor(*PositionCamera, LanguageProfile::Dbc);
    const auto Professional = dbc::language::ArityFor(*PositionCamera, LanguageProfile::DbPro);
    REQUIRE(Classic);
    REQUIRE(Professional);
    CHECK(Classic->Accepts(3));
    CHECK_FALSE(Classic->Accepts(4));
    CHECK(Professional->Accepts(4));

    const auto* Particles =
        dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "POSITION PARTICLES");
    REQUIRE(Particles != nullptr);
    const auto ParticleArity = dbc::language::ArityFor(*Particles, LanguageProfile::DbPro);
    REQUIRE(ParticleArity);
    CHECK(ParticleArity->Accepts(2));
    CHECK_FALSE(ParticleArity->Accepts(3));
    CHECK(ParticleArity->Accepts(4));

    const auto* LoadSound = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "LOAD SOUND");
    const auto* SetDisplay = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "SET DISPLAY MODE");
    const auto* CollisionX =
        dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, "GET OBJECT COLLISION X");
    REQUIRE(LoadSound != nullptr);
    REQUIRE(SetDisplay != nullptr);
    REQUIRE(CollisionX != nullptr);
    const auto LoadSoundArity = dbc::language::ArityFor(*LoadSound, LanguageProfile::Dbc);
    const auto SetDisplayArity = dbc::language::ArityFor(*SetDisplay, LanguageProfile::Dbc);
    const auto CollisionXArity = dbc::language::ArityFor(*CollisionX, LanguageProfile::Dbc);
    REQUIRE(LoadSoundArity);
    REQUIRE(SetDisplayArity);
    REQUIRE(CollisionXArity);
    CHECK(LoadSoundArity->Accepts(2));
    CHECK(LoadSoundArity->Accepts(3));
    CHECK_FALSE(LoadSoundArity->Accepts(4));
    CHECK(SetDisplayArity->Accepts(2));
    CHECK(SetDisplayArity->Accepts(3));
    CHECK_FALSE(SetDisplayArity->Accepts(4));
    CHECK(CollisionXArity->Accepts(0));
    CHECK(CollisionXArity->Accepts(1));
    CHECK_FALSE(CollisionXArity->Accepts(2));
}

TEST_CASE("Profile overloads carry stable call shape metadata")
{
    const auto* Pick = dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Function, "PICK OBJECT");
    REQUIRE(Pick != nullptr);
    const auto PickOverloads = dbc::language::Overloads(Pick->Id, LanguageProfile::DbPro);
    REQUIRE(PickOverloads.size() == 1);
    CHECK(PickOverloads[0]->SourceArity.Accepts(4));
    CHECK(PickOverloads[0]->SourceArity.Accepts(5));
    CHECK(PickOverloads[0]->ReturnType == dbc::language::BuiltinValueType::Integer);

    const auto* LoadArray = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "LOAD ARRAY");
    REQUIRE(LoadArray != nullptr);
    const dbc::language::BuiltinOverload* Load = dbc::language::FindOverload(LoadArray->Id, LanguageProfile::Dbc, 2);
    REQUIRE(Load != nullptr);
    REQUIRE(Load->Parameters.size() == 2);
    CHECK(Load->Parameters[0].ValueType == dbc::language::BuiltinValueType::String);
    CHECK(Load->Parameters[1].Mode == dbc::language::BuiltinParameterMode::Array);

    const auto* Text = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "TEXT");
    REQUIRE(Text != nullptr);
    const dbc::language::BuiltinOverload* TextCall = dbc::language::FindOverload(Text->Id, LanguageProfile::Dbc, 3);
    REQUIRE(TextCall != nullptr);
    REQUIRE(TextCall->Parameters.size() == 3);
    CHECK(TextCall->Parameters[0].ValueType == dbc::language::BuiltinValueType::Numeric);
    CHECK(TextCall->Parameters[1].ValueType == dbc::language::BuiltinValueType::Numeric);
    CHECK(TextCall->Parameters[2].ValueType == dbc::language::BuiltinValueType::String);
}

TEST_CASE("Implemented overloads use concrete scalar types outside historical polymorphic forms")
{
    for (const dbc::language::BuiltinOverload& Overload : dbc::language::Overloads())
    {
        const dbc::language::SymbolDescriptor& Descriptor = dbc::language::Symbol(Overload.Symbol);
        if (dbc::language::SupportFor(Descriptor, Overload.Profile) != SupportStatus::Implemented)
            continue;
        const dbc::language::BuiltinConvention Convention = dbc::language::ConventionFor(Overload.Symbol);
        const bool Polymorphic = Convention == dbc::language::BuiltinConvention::Print ||
                                 Convention == dbc::language::BuiltinConvention::DataLiteral ||
                                 Convention == dbc::language::BuiltinConvention::DataRead ||
                                 Convention == dbc::language::BuiltinConvention::DataRestore ||
                                 Convention == dbc::language::BuiltinConvention::Input ||
                                 Convention == dbc::language::BuiltinConvention::LoadArray ||
                                 Convention == dbc::language::BuiltinConvention::SaveArray ||
                                 Convention == dbc::language::BuiltinConvention::Undim ||
                                 Convention == dbc::language::BuiltinConvention::ArrayCollection ||
                                 Overload.Symbol == dbc::language::SymbolId::Function_StrString ||
                                 Overload.Symbol == dbc::language::SymbolId::Command_Printc;
        CAPTURE(Descriptor.CanonicalName);
        for (const dbc::language::BuiltinParameter& Parameter : Overload.Parameters)
        {
            if (Parameter.ValueType == dbc::language::BuiltinValueType::Any)
                CHECK(Polymorphic);
        }
        CHECK(Overload.ReturnType != dbc::language::BuiltinValueType::Any);
        CHECK(Overload.ReturnType != dbc::language::BuiltinValueType::Numeric);
    }
}

TEST_CASE("Incomplete and host-only behavior is not advertised as implemented")
{
    const auto* Unsupported =
        dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "PERFORM CSG UNION ON VERTEXDATA");
    REQUIRE(Unsupported != nullptr);
    CHECK(dbc::language::SupportFor(*Unsupported, LanguageProfile::DbPro) == SupportStatus::Unsupported);

    // Catalogued from the Classic help and accepted through the explicit non-portable compatibility boundary.
    const auto* Recorded = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "LOAD DLL");
    REQUIRE(Recorded != nullptr);
    CHECK(dbc::language::SupportFor(*Recorded, LanguageProfile::Dbc) == SupportStatus::CompatibilityStub);
    CHECK(dbc::language::StubPolicyFor(*Recorded, LanguageProfile::Dbc) ==
          dbc::language::CompatibilityStubPolicy::Permanent);
    CHECK(dbc::language::SupportFor(*Recorded, LanguageProfile::DbPro) == SupportStatus::Unsupported);

    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "DUMP SCENE") == nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Command, "DUMP SCENE") == nullptr);
}

TEST_CASE("Longest command matching is profile filtered")
{
    const auto* Classic = dbc::language::FindLongestCommandPrefix(LanguageProfile::Dbc, "make object cube 1, 10");
    REQUIRE(Classic != nullptr);
    CHECK(Classic->CanonicalName == "MAKE OBJECT CUBE");

    CHECK(dbc::language::FindLongestCommandPrefix(LanguageProfile::Dbc, "add vector4 1,2,3") == nullptr);
    CHECK(dbc::language::FindLongestCommandPrefix(LanguageProfile::DbPro, "add vector4 1,2,3") != nullptr);
}

TEST_CASE("Non-value call conventions are canonical catalogue metadata")
{
    using dbc::language::BuiltinConvention;
    using dbc::language::ConventionFor;
    using dbc::language::SymbolId;

    CHECK(ConventionFor(SymbolId::Command_Print) == BuiltinConvention::Print);
    CHECK(ConventionFor(SymbolId::Command_Input) == BuiltinConvention::Input);
    CHECK(ConventionFor(SymbolId::Command_ReadByte) == BuiltinConvention::FileRead);
    CHECK(ConventionFor(SymbolId::Command_LoadArray) == BuiltinConvention::LoadArray);
    CHECK(ConventionFor(SymbolId::Command_Undim) == BuiltinConvention::Undim);
    CHECK(ConventionFor(SymbolId::Command_AddToQueue) == BuiltinConvention::ArrayCollection);
    CHECK(ConventionFor(SymbolId::Function_ArrayCount) == BuiltinConvention::ArrayCollection);
    CHECK(ConventionFor(SymbolId::Command_PositionCamera) == BuiltinConvention::Values);
}

TEST_CASE("Compatibility stubs are explicit and remain profile isolated")
{
    const auto* VertexPointer =
        dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Function, "GET VERTEXDATA PTR");
    const auto* FtpStatus = dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Function, "GET FTP STATUS");
    const auto* GetImage = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "GET IMAGE");
    const auto* Undim = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "UNDIM");
    REQUIRE(VertexPointer != nullptr);
    REQUIRE(FtpStatus != nullptr);
    REQUIRE(GetImage != nullptr);
    REQUIRE(Undim != nullptr);
    CHECK(dbc::language::SupportFor(*VertexPointer, LanguageProfile::DbPro) == SupportStatus::Unsupported);
    CHECK(dbc::language::SupportFor(*FtpStatus, LanguageProfile::DbPro) == SupportStatus::Unsupported);
    CHECK(dbc::language::SupportFor(*Undim, LanguageProfile::Dbc) == SupportStatus::Implemented);
    const auto* ClassicFtpStatus =
        dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, "GET FTP STATUS");
    const auto* ForceUp = dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "FORCE UP");
    const auto* ObjectAnimation =
        dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "APPEND OBJECT ANIMATION");
    REQUIRE(ClassicFtpStatus != nullptr);
    REQUIRE(ForceUp != nullptr);
    REQUIRE(ObjectAnimation != nullptr);
    CHECK(dbc::language::StubPolicyFor(*ClassicFtpStatus, LanguageProfile::Dbc) ==
          dbc::language::CompatibilityStubPolicy::Permanent);
    CHECK(dbc::language::StubPolicyFor(*ForceUp, LanguageProfile::Dbc) !=
          dbc::language::CompatibilityStubPolicy::Permanent);
    CHECK(dbc::language::StubPolicyFor(*ObjectAnimation, LanguageProfile::Dbc) !=
          dbc::language::CompatibilityStubPolicy::Permanent);
    // Both documented GET IMAGE forms (bitmap/iget.htm): the texturing flag is the sixth argument.
    const auto Arity = dbc::language::ArityFor(*GetImage, LanguageProfile::Dbc);
    REQUIRE(Arity);
    CHECK(Arity->Accepts(5));
    CHECK(Arity->Accepts(6));
    CHECK_FALSE(Arity->Accepts(7));
}

TEST_CASE("Every available Classic callable is executable with an auditable stub policy")
{
    std::size_t Temporary = 0;
    std::size_t Permanent = 0;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (!dbc::language::AvailableIn(Descriptor, LanguageProfile::Dbc) ||
            (Descriptor.Kind != SymbolKind::Command && Descriptor.Kind != SymbolKind::Function))
        {
            continue;
        }
        CAPTURE(Descriptor.CanonicalName);
        CHECK(dbc::language::ExecutableIn(Descriptor, LanguageProfile::Dbc));
        CHECK((Descriptor.ImplementedProfiles & Descriptor.CompatibilityStubProfiles) == 0);
        CHECK((Descriptor.PermanentStubProfiles & ~Descriptor.CompatibilityStubProfiles) == 0);
        switch (dbc::language::StubPolicyFor(Descriptor, LanguageProfile::Dbc))
        {
            case dbc::language::CompatibilityStubPolicy::Temporary:
                ++Temporary;
                break;
            case dbc::language::CompatibilityStubPolicy::Permanent:
                ++Permanent;
                break;
            case dbc::language::CompatibilityStubPolicy::None:
                break;
        }
    }
    CAPTURE(Temporary);
    CAPTURE(Permanent);
    CHECK(Temporary == 0);
    CHECK(Permanent == 39);
}

TEST_CASE("The permanent Classic stub manifest exactly describes neutral behavior and profile isolation")
{
    using dbc::language::BuiltinValueType;
    using dbc::language::CompatibilityStubBehavior;
    using dbc::language::CompatibilityStubPolicy;
    using dbc::language::SymbolId;

    const std::filesystem::path Manifest =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/unit/compatibility_stub_manifest.tsv";
    std::ifstream Input(Manifest);
    REQUIRE(Input.good());

    std::string Line;
    std::getline(Input, Line);
    CHECK(Line == "profile\tsymbol_id\tkind\tname\tpolicy\tneutral_behavior");

    std::set<SymbolId> Actual;
    while (std::getline(Input, Line))
    {
        if (Line.empty())
            continue;
        std::istringstream Fields(Line);
        std::string Profile;
        std::string SymbolName;
        std::string Kind;
        std::string Name;
        std::string Policy;
        std::string NeutralBehavior;
        std::getline(Fields, Profile, '\t');
        std::getline(Fields, SymbolName, '\t');
        std::getline(Fields, Kind, '\t');
        std::getline(Fields, Name, '\t');
        std::getline(Fields, Policy, '\t');
        std::getline(Fields, NeutralBehavior, '\t');

        CAPTURE(SymbolName);
        CAPTURE(Name);
        CHECK(Profile == "dbc");
        CHECK(Policy == "Permanent");
        const SymbolKind ParsedKind = Kind == "Command" ? SymbolKind::Command : SymbolKind::Function;
        CHECK((Kind == "Command" || Kind == "Function"));
        const auto* Descriptor = dbc::language::FindSymbol(LanguageProfile::Dbc, ParsedKind, Name);
        REQUIRE(Descriptor != nullptr);
        CHECK(CatalogueSymbolName(Descriptor->Id) == SymbolName);
        CHECK(dbc::language::StubPolicyFor(*Descriptor, LanguageProfile::Dbc) == CompatibilityStubPolicy::Permanent);
        CHECK(dbc::language::StubBehaviorFor(*Descriptor, LanguageProfile::Dbc) != CompatibilityStubBehavior::None);
        CHECK(NeutralBehaviorName(dbc::language::StubBehaviorFor(*Descriptor, LanguageProfile::Dbc)) ==
              NeutralBehavior);
        CHECK(dbc::language::StubBehaviorFor(*Descriptor, LanguageProfile::DbPro) == CompatibilityStubBehavior::None);
        CHECK(dbc::language::SupportFor(*Descriptor, LanguageProfile::DbPro) != SupportStatus::CompatibilityStub);
        CHECK(Actual.insert(Descriptor->Id).second);

        for (const dbc::language::BuiltinOverload* Overload :
             dbc::language::Overloads(Descriptor->Id, LanguageProfile::Dbc))
        {
            REQUIRE(Overload != nullptr);
            const CompatibilityStubBehavior Behavior =
                dbc::language::StubBehaviorFor(*Descriptor, LanguageProfile::Dbc);
            if (Descriptor->Kind == SymbolKind::Command)
            {
                CHECK(Overload->ReturnType == BuiltinValueType::Void);
                CHECK((Behavior == CompatibilityStubBehavior::NoOp ||
                       Behavior == CompatibilityStubBehavior::AcceptInertResource));
            }
            else if (Behavior == CompatibilityStubBehavior::EmptyString)
            {
                CHECK(Overload->ReturnType == BuiltinValueType::String);
            }
            else
            {
                CHECK((Overload->ReturnType == BuiltinValueType::Integer ||
                       Overload->ReturnType == BuiltinValueType::Real));
            }
        }
    }

    std::set<SymbolId> Expected;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        CAPTURE(Descriptor.CanonicalName);
        if (dbc::language::StubPolicyFor(Descriptor, LanguageProfile::Dbc) == CompatibilityStubPolicy::Permanent)
            Expected.insert(Descriptor.Id);
        CHECK((Descriptor.CompatibilityStubProfiles & 2U) == 0);
    }
    CHECK(Expected.size() == 39);
    CHECK(Actual == Expected);
}

TEST_CASE("Corrected Classic signatures retain DBPro arities")
{
    using dbc::language::BuiltinValueType;
    const auto RequireArity = [](std::string_view Name, SymbolKind Kind, LanguageProfile Profile, int Count)
    {
        const auto* Descriptor = dbc::language::FindSymbol(Profile, Kind, Name);
        REQUIRE(Descriptor != nullptr);
        const auto Arity = dbc::language::ArityFor(*Descriptor, Profile);
        REQUIRE(Arity);
        CHECK(Arity->Accepts(Count));
        return Descriptor;
    };

    const auto* Execute = RequireArity("EXECUTE FILE", SymbolKind::Command, LanguageProfile::Dbc, 3);
    CHECK(dbc::language::ArityFor(*Execute, LanguageProfile::Dbc)->Accepts(4));
    const auto* Ftp = RequireArity("FTP GET FILE", SymbolKind::Command, LanguageProfile::Dbc, 2);
    CHECK(dbc::language::ArityFor(*Ftp, LanguageProfile::Dbc)->Accepts(3));
    RequireArity("SAVE SOUND", SymbolKind::Command, LanguageProfile::Dbc, 2);
    RequireArity("GET STATIC COLLISION X", SymbolKind::Function, LanguageProfile::Dbc, 0);
    RequireArity("LISTENER POSITION X", SymbolKind::Function, LanguageProfile::Dbc, 0);

    const auto* ClassicLimb = RequireArity("LIMB TEXTURE NAME$", SymbolKind::Function, LanguageProfile::Dbc, 2);
    const auto* ClassicLimbOverload = dbc::language::FindOverload(ClassicLimb->Id, LanguageProfile::Dbc, 2);
    REQUIRE(ClassicLimbOverload != nullptr);
    CHECK(ClassicLimbOverload->ReturnType == BuiltinValueType::String);
    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, "LIMB TEXTURE NAME") == nullptr);
    const auto* ProfessionalLimb = RequireArity("LIMB TEXTURE NAME", SymbolKind::Function, LanguageProfile::DbPro, 2);
    CHECK(dbc::language::ArityFor(*ProfessionalLimb, LanguageProfile::DbPro)->Accepts(3));
    const auto* ProfessionalLimbOverload = dbc::language::FindOverload(ProfessionalLimb->Id, LanguageProfile::DbPro, 3);
    REQUIRE(ProfessionalLimbOverload != nullptr);
    CHECK(ProfessionalLimbOverload->ReturnType == BuiltinValueType::String);
    CHECK(dbc::language::FindSymbol(LanguageProfile::DbPro, SymbolKind::Function, "LIMB TEXTURE NAME$") == nullptr);

    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, "APPNAME$") != nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, "STATISTIC") != nullptr);
    CHECK(dbc::language::FindSymbol(LanguageProfile::Dbc, SymbolKind::Function, "ANIMATION LOOPING") != nullptr);
    const auto* GraphicsCard = RequireArity("SET GRAPHICS CARD", SymbolKind::Command, LanguageProfile::Dbc, 1);
    const auto* ClassicGraphicsCard = dbc::language::FindOverload(GraphicsCard->Id, LanguageProfile::Dbc, 1);
    const auto* ProfessionalGraphicsCard = dbc::language::FindOverload(GraphicsCard->Id, LanguageProfile::DbPro, 1);
    REQUIRE(ClassicGraphicsCard != nullptr);
    REQUIRE(ProfessionalGraphicsCard != nullptr);
    REQUIRE(ClassicGraphicsCard->Parameters.size() == 1);
    REQUIRE(ProfessionalGraphicsCard->Parameters.size() == 1);
    CHECK(ClassicGraphicsCard->Parameters.front().ValueType == BuiltinValueType::String);
    CHECK(ProfessionalGraphicsCard->Parameters.front().ValueType == BuiltinValueType::Numeric);
    CHECK(dbc::language::ArityFor(*Execute, LanguageProfile::DbPro)->Accepts(3));
    CHECK(dbc::language::ArityFor(*Execute, LanguageProfile::DbPro)->Accepts(4));
}

TEST_CASE("Every implemented profile overload has an observable behavior witness")
{
    using Key = std::tuple<LanguageProfile, std::uint32_t, std::uint32_t>;
    const std::filesystem::path Manifest =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests/unit/builtin_behavior_manifest.tsv";
    std::ifstream Input(Manifest);
    REQUIRE(Input.good());

    std::set<Key> Actual;
    std::string Line;
    std::getline(Input, Line);
    CHECK(Line == "profile\tsymbol_id\toverload_id\tkind\tname\toracle\ttest\toracle_sha256");
    while (std::getline(Input, Line))
    {
        if (Line.empty())
            continue;
        std::istringstream Fields(Line);
        std::string ProfileText;
        std::string SymbolText;
        std::string OverloadText;
        std::string Kind;
        std::string Name;
        std::string Oracle;
        std::string Test;
        std::string OracleSha256;
        std::getline(Fields, ProfileText, '\t');
        std::getline(Fields, SymbolText, '\t');
        std::getline(Fields, OverloadText, '\t');
        std::getline(Fields, Kind, '\t');
        std::getline(Fields, Name, '\t');
        std::getline(Fields, Oracle, '\t');
        std::getline(Fields, Test, '\t');
        std::getline(Fields, OracleSha256, '\t');
        const LanguageProfile Profile = ProfileText == "dbc" ? LanguageProfile::Dbc : LanguageProfile::DbPro;
        const auto Symbol = static_cast<dbc::language::SymbolId>(std::stoul(SymbolText));
        const dbc::language::OverloadId Overload{static_cast<std::uint32_t>(std::stoul(OverloadText))};
        const dbc::language::BuiltinOverload* Descriptor = dbc::language::Overload(Overload);
        REQUIRE(Descriptor != nullptr);
        CHECK(Descriptor->Profile == Profile);
        CHECK(Descriptor->Symbol == Symbol);
        CHECK(dbc::language::Symbol(Symbol).CanonicalName == Name);
        CHECK(Oracle != "smoke");
        CHECK(Oracle != "output");
        CHECK_FALSE(Test.empty());
        if (Oracle == "golden-output")
        {
            CHECK(OracleSha256.size() == 64);
            CHECK(Test.ends_with(".dba"));
        }
        else
        {
            CHECK(OracleSha256 == "-");
            CHECK(Test.find(".cpp::") != std::string::npos);
        }
        CHECK(Actual.emplace(Profile, static_cast<std::uint32_t>(Symbol), Overload.Value).second);
    }

    std::set<Key> Expected;
    std::ostringstream Missing;
    for (const dbc::language::BuiltinOverload& Overload : dbc::language::Overloads())
    {
        const dbc::language::SymbolDescriptor& Descriptor = dbc::language::Symbol(Overload.Symbol);
        if (dbc::language::SupportFor(Descriptor, Overload.Profile) != SupportStatus::Implemented)
            continue;
        const Key Value{Overload.Profile, static_cast<std::uint32_t>(Overload.Symbol), Overload.Id.Value};
        Expected.insert(Value);
        if (!Actual.contains(Value))
            Missing << '\n'
                    << (Overload.Profile == LanguageProfile::Dbc ? "dbc " : "dbpro ") << Descriptor.CanonicalName
                    << " overload " << Overload.Id.Value;
    }
    INFO("Missing behavior witnesses:" << Missing.str());
    CHECK(Actual == Expected);
}

TEST_CASE("Shared VR forms have exact signatures without widening other Classic vector operations")
{
    using namespace dbc::language;
    struct Expected
    {
        SymbolId Id;
        const char* Name;
        SymbolKind Kind;
        int Count;
        BuiltinValueType Result;
    };
    const Expected Forms[]{
        {SymbolId::Command_SetVrEnabled, "SET VR ENABLED", SymbolKind::Command, 1, BuiltinValueType::Void},
        {SymbolId::Command_SetTrackingSpace, "SET TRACKING SPACE", SymbolKind::Command, 1, BuiltinValueType::Void},
        {SymbolId::Command_RecenterVrCamera, "RECENTER VR CAMERA", SymbolKind::Command, 0, BuiltinValueType::Void},
        {SymbolId::Command_GetVrControllerPosition, "GET VR CONTROLLER POSITION", SymbolKind::Command, 2,
         BuiltinValueType::Void},
        {SymbolId::Command_GetVrControllerRotation, "GET VR CONTROLLER ROTATION", SymbolKind::Command, 2,
         BuiltinValueType::Void},
        {SymbolId::Command_GetVrHeadPosition, "GET VR HEAD POSITION", SymbolKind::Command, 1, BuiltinValueType::Void},
        {SymbolId::Command_GetVrRotation, "GET VR ROTATION", SymbolKind::Command, 1, BuiltinValueType::Void},
        {SymbolId::Function_IsVrControllerTracked, "IS VR CONTROLLER TRACKED", SymbolKind::Function, 1,
         BuiltinValueType::Integer},
        {SymbolId::Function_IsVrHeadTracked, "IS VR HEAD TRACKED", SymbolKind::Function, 0, BuiltinValueType::Integer},
        {SymbolId::Function_IsVrSupported, "IS VR SUPPORTED", SymbolKind::Function, 0, BuiltinValueType::Integer},
        {SymbolId::Function_IsVrEnabled, "IS VR ENABLED", SymbolKind::Function, 0, BuiltinValueType::Integer},
        {SymbolId::Command_SetVrPreviewMode, "SET VR PREVIEW MODE", SymbolKind::Command, 1, BuiltinValueType::Void},
        {SymbolId::Command_SetVrCameraHeight, "SET VR CAMERA HEIGHT", SymbolKind::Command, 1, BuiltinValueType::Void},
        {SymbolId::Function_GetVrCameraHeight, "GET VR CAMERA HEIGHT", SymbolKind::Function, 0, BuiltinValueType::Real},
    };
    for (const auto Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
    {
        for (const auto& Form : Forms)
        {
            CAPTURE(Form.Name);
            const auto* Descriptor = FindSymbol(Profile, Form.Kind, Form.Name);
            REQUIRE(Descriptor != nullptr);
            CHECK(Descriptor->Id == Form.Id);
            CHECK(SupportFor(*Descriptor, Profile) == SupportStatus::Implemented);
            const auto Calls = Overloads(Form.Id, Profile);
            REQUIRE(Calls.size() == 1);
            CHECK(Calls[0]->SourceArity.Accepts(Form.Count));
            CHECK_FALSE(Calls[0]->SourceArity.Accepts(Form.Count + 1));
            CHECK(Calls[0]->ReturnType == Form.Result);
            for (const auto& Parameter : Calls[0]->Parameters)
                CHECK(Parameter.ValueType == (Form.Id == SymbolId::Command_SetVrCameraHeight
                                                  ? BuiltinValueType::Real
                                                  : BuiltinValueType::Integer));
        }
        for (const auto Id : {SymbolId::Function_XVector3, SymbolId::Function_YVector3, SymbolId::Function_ZVector3})
        {
            CHECK(SupportFor(Symbol(Id), Profile) == SupportStatus::Implemented);
            const auto* Call = FindOverload(Id, Profile, 1);
            REQUIRE(Call != nullptr);
            CHECK(Call->ReturnType == BuiltinValueType::Real);
            CHECK(Call->Parameters[0].ValueType == BuiltinValueType::Numeric);
        }
    }
    CHECK(FindSymbol(LanguageProfile::Dbc, SymbolKind::Command, "MAKE VECTOR3") == nullptr);
}
