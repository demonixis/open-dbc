#include "engine/runtime_compatibility.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "interpreter/engine_builtin_context.hpp"
#include "interpreter/engine_builtin_host.hpp"
#include "interpreter/engine_builtin_registry.hpp"
#include "language/frontend.hpp"
#include "vm/compiler.hpp"
#include "vm/verifier.hpp"

#include <doctest/doctest.h>

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
