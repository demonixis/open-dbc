#include "engine/runtime_compatibility.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "interpreter/engine_builtin_context.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"
#include "language/frontend.hpp"
#include "vm/compiler.hpp"
#include "vm/verifier.hpp"

#include <SDL3/SDL_platform.h>
#include <doctest/doctest.h>

#include <algorithm>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using dbc::interpreter::detail::BuiltinContext;
    using dbc::interpreter::detail::BuiltinValue;
    using dbc::language::LanguageProfile;
    using dbc::language::SymbolId;
    using dbc::language::SymbolKind;

    // A DBPro program's engine, driven one builtin at a time.
    class Application
    {
      public:
        explicit Application(const char* Title)
        {
            REQUIRE(Kernel_.Initialize(Title, 64, 64));
            Kernel_.ConfigureRuntimeCompatibility(dbc::RuntimeCompatibilityPolicy::Professional());
            Context_.Kernel = &Kernel_;
            Context_.Profile = LanguageProfile::DbPro;
        }

        bool Command(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return dbc::interpreter::detail::InvokeCommand(Context_, Symbol, Arguments, Error);
        }

        void Require(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            REQUIRE_MESSAGE(Command(Symbol, std::move(Arguments)), Error);
        }

        BuiltinValue Function(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            BuiltinValue Result;
            REQUIRE_MESSAGE(dbc::interpreter::detail::InvokeFunction(Context_, Symbol, Arguments, Result, Error),
                            Error);
            return Result;
        }

        double Number(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return Function(Symbol, std::move(Arguments)).AsNumber();
        }

        std::string Text(SymbolId Symbol, std::vector<BuiltinValue> Arguments = {})
        {
            return Function(Symbol, std::move(Arguments)).AsString();
        }

        bool Fails(SymbolId Symbol, std::vector<BuiltinValue> Arguments)
        {
            BuiltinValue Result;
            Error.clear();
            return !dbc::interpreter::detail::InvokeFunction(Context_, Symbol, Arguments, Result, Error) &&
                   !Error.empty();
        }

        dbc::EngineKernel& Kernel()
        {
            return Kernel_;
        }

        BuiltinContext& Context()
        {
            return Context_;
        }

        std::string Error;

      private:
        dbc::EngineKernel Kernel_;
        BuiltinContext Context_;
    };

    BuiltinValue Int(int Value)
    {
        return BuiltinValue::FromNumber(static_cast<double>(Value));
    }

    std::vector<std::string> ChecklistNames(const BuiltinContext& Context)
    {
        std::vector<std::string> Names;
        for (const auto& Item : Context.Checklist)
            Names.push_back(Item.Name);
        return Names;
    }

    bool CompilesInDbPro(std::string Text)
    {
        const auto Source = std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{"test.dba", std::move(Text), LanguageProfile::DbPro});
        const dbc::language::Program Program = dbc::language::Compile(Source);
        for (const auto& Diagnostic : Program.Diagnostics)
            INFO(dbc::language::FormatDiagnostic(*Source, Diagnostic));
        if (Program.HasErrors())
            return false;
        const dbc::vm::CompileResult Result = dbc::vm::Lower(Program);
        return Result.Success() && dbc::vm::Verify(*Result.Bytecode).Success();
    }
} // namespace

TEST_CASE("DBPro EAX forms bind and run as permanent stubs, as on a card without EAX")
{
    using dbc::language::BuiltinValueType;
    using dbc::language::CompatibilityStubPolicy;
    using dbc::language::Type;
    using dbc::language::TypeKind;

    Application App("enhancement-eax-stubs");
    dbc::interpreter::detail::EngineBuiltinHost Host(App.Context());
    std::ostringstream Program;
    std::size_t Stubs = 0;
    std::uint64_t InvocationId = 1;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (dbc::language::StubPolicyFor(Descriptor, LanguageProfile::DbPro) != CompatibilityStubPolicy::Permanent)
            continue;
        CAPTURE(Descriptor.CanonicalName);
        CHECK(Descriptor.CanonicalName.find("EAX") != std::string_view::npos);
        ++Stubs;
        for (const dbc::language::BuiltinOverload* Overload :
             dbc::language::Overloads(Descriptor.Id, LanguageProfile::DbPro))
        {
            std::vector<dbc::vm::Value> Arguments;
            Program << (Descriptor.Kind == SymbolKind::Function ? "print " : "") << Descriptor.CanonicalName
                    << (Descriptor.Kind == SymbolKind::Function ? "(" : " ");
            for (std::size_t Index = 0; Index < Overload->Parameters.size(); ++Index)
            {
                Program << (Index == 0 ? "" : ", ") << "1";
                Arguments.push_back(Overload->Parameters[Index].ValueType == BuiltinValueType::Real
                                        ? dbc::vm::Value::Real(1.0)
                                        : dbc::vm::Value::Integer(1));
            }
            Program << (Descriptor.Kind == SymbolKind::Function ? ")\n" : "\n");

            const Type Result = Overload->ReturnType == BuiltinValueType::Void
                                    ? Type{TypeKind::Void, {}}
                                    : (Overload->ReturnType == BuiltinValueType::Real ? Type{TypeKind::Real, {}}
                                                                                      : Type{TypeKind::Integer, {}});
            const dbc::vm::BuiltinResult Invoked =
                Host.Invoke({InvocationId++, Descriptor.Id, Arguments, Result, false});
            REQUIRE(Invoked.State == dbc::vm::BuiltinState::Completed);
            if (Descriptor.Kind == SymbolKind::Command)
            {
                CHECK_FALSE(Invoked.ReturnValue.has_value());
                continue;
            }
            REQUIRE(Invoked.ReturnValue.has_value());
            CHECK(Invoked.ReturnValue->AsReal() == 0.0);
        }
    }
    CHECK(Stubs == 80);
    CHECK(CompilesInDbPro(Program.str()));
}

TEST_CASE("PERFORM CHECKLIST FOR EAX EFFECTS lists the presets in the Enhancement Pack's order")
{
    Application App("enhancement-eax-checklist");
    App.Context().Checklist = {{"stale"}, {"entries"}};
    App.Require(SymbolId::Command_ExtEnhancementPerformChecklistForEaxEffects);
    const auto& Checklist = App.Context().Checklist;
    REQUIRE(Checklist.size() == 26);
    CHECK(Checklist.front().Name == "Generic");
    CHECK(Checklist[7].Name == "Concert Hall");
    CHECK(Checklist.back().Name == "Psychotic");
}

TEST_CASE("Enhancement Pack processor queries describe the host processor")
{
    Application App("enhancement-processor");
    const double Count = App.Number(SymbolId::Function_ExtEnhancementGetProcessorCount);
    CHECK(Count >= 1.0);
    CHECK(App.Text(SymbolId::Function_ExtEnhancementGetProcessorPlatform, {Int(0)}) == SDL_GetPlatform());
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetProcessorFrequency, {Int(0)}) >= 0.0);
    for (int Sample = 0; Sample < 2; ++Sample)
    {
        const double Usage = App.Number(SymbolId::Function_ExtEnhancementGetProcessorUsage, {Int(0)});
        CHECK(Usage >= 0.0);
        CHECK(Usage <= 100.0);
    }

    App.Context().Checklist = {{"stale"}};
    App.Require(SymbolId::Command_ExtEnhancementGetProcessorSupportedFeatures, {Int(0)});
    const std::vector<std::string> Features = ChecklistNames(App.Context());
    CHECK(std::find(Features.begin(), Features.end(), "stale") == Features.end());
    CHECK((std::find(Features.begin(), Features.end(), "MP") != Features.end()) == (Count > 1.0));

    App.Require(SymbolId::Command_ExtEnhancementGetProcessorOnChipFeatures, {Int(0)});
    for (const std::string& Name : ChecklistNames(App.Context()))
    {
        CAPTURE(Name);
        CHECK(((Name.rfind("L", 0) == 0 && Name.find(" cache = ") == 2) || Name == "APIC present"));
    }
    App.Require(SymbolId::Command_ExtEnhancementGetProcessorPowerManagement, {Int(0)});
    for (const std::string& Name : ChecklistNames(App.Context()))
    {
        CAPTURE(Name);
        CHECK((Name == "Thermal monitor" || Name == "Temperature sensor" || Name == "Frequency ID" ||
               Name == "Voltage ID"));
    }
}

TEST_CASE("Enhancement Pack memory queries round up and reject unknown units with zero")
{
    Application App("enhancement-memory");
    const double Kilobytes = App.Number(SymbolId::Function_ExtEnhancementGetInstalledMemory, {Int(0)});
    const double Megabytes = App.Number(SymbolId::Function_ExtEnhancementGetInstalledMemory, {Int(1)});
    CHECK(Kilobytes > 0.0);
    CHECK(Megabytes >= 1.0);
    CHECK(Megabytes * 1024.0 >= Kilobytes);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetInstalledMemory, {Int(3)}) == 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetMemoryAvailable, {Int(1)}) <= Megabytes);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetMemoryAvailable, {Int(-1)}) == 0.0);
    const double Used = App.Number(SymbolId::Function_ExtEnhancementGetMemoryPercentUsed);
    CHECK(Used >= 0.0);
    CHECK(Used <= 100.0);
    CHECK(Used + App.Number(SymbolId::Function_ExtEnhancementGetMemoryPercentFree) ==
          doctest::Approx(100.0).epsilon(0.02));
}

TEST_CASE("Enhancement Pack drive queries read the system volume and answer absent drives as DBPro did")
{
    Application App("enhancement-drives");
    REQUIRE(App.Number(SymbolId::Function_ExtEnhancementGetDriveCount) >= 1.0);
    const double Total = App.Number(SymbolId::Function_ExtEnhancementGetDriveTotalSize, {Int(0), Int(1)});
    const double Free = App.Number(SymbolId::Function_ExtEnhancementGetDriveFreeSpace, {Int(0), Int(1)});
    const double Used = App.Number(SymbolId::Function_ExtEnhancementGetDriveUsedSpace, {Int(0), Int(1)});
    CHECK(Total > 0.0);
    CHECK(Free >= 0.0);
    CHECK(Free + Used <= Total + 1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveFileLengthSupport, {Int(0)}) > 0.0);
    CHECK_FALSE(App.Text(SymbolId::Function_ExtEnhancementGetDriveFileSystem, {Int(0)}).empty());

    // Drive 24 exists on no machine the tests run on.
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveTotalSize, {Int(24), Int(0)}) == 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveFreeSpace, {Int(24), Int(0)}) == -1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveFileLengthSupport, {Int(24)}) == -1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDriveCylinderCount, {Int(24)}) == 0.0);
    CHECK(App.Text(SymbolId::Function_ExtEnhancementGetDriveSerial, {Int(24)}).empty());
    CHECK(App.Fails(SymbolId::Function_ExtEnhancementGetDriveSerial, {Int(25)}));
    CHECK(App.Fails(SymbolId::Function_ExtEnhancementGetDriveTotalSize, {Int(-1), Int(0)}));

    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetCdCount) >= 0.0);
    CHECK(App.Text(SymbolId::Function_ExtEnhancementGetCdLetter, {Int(0)}).empty());
    CHECK(App.Fails(SymbolId::Function_ExtEnhancementGetCdLetter, {Int(25)}));
}

TEST_CASE("Enhancement Pack names, address and display queries answer without a display")
{
    Application App("enhancement-names");
    CHECK_FALSE(App.Text(SymbolId::Function_ExtEnhancementGetComputerName).empty());
    CHECK_FALSE(App.Text(SymbolId::Function_ExtEnhancementGetIpAddress).empty());
    App.Text(SymbolId::Function_ExtEnhancementGetUserName);
    App.Text(SymbolId::Function_ExtEnhancementGetWebClient);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayWidth) >= 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayHeight) >= 0.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayDepth) >= -1.0);
    CHECK(App.Number(SymbolId::Function_ExtEnhancementGetDisplayRefreshRate) >= -1.0);
}
