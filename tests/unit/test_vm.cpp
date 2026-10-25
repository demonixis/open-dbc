#include <doctest/doctest.h>

#include "language/frontend.hpp"
#include "language/numeric_conversion.hpp"
#include "vm/compiler.hpp"
#include "vm/execution_session.hpp"
#include "vm/verifier.hpp"
#include <cmath>
#include <limits>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    class TestHost final : public dbc::vm::BuiltinHost
    {
      public:
        dbc::vm::BuiltinResult Invoke(const dbc::vm::BuiltinInvocation& Invocation) override
        {
            Calls.push_back(Invocation.Symbol);
            InvocationIds.push_back(Invocation.Id);
            Retries.push_back(Invocation.IsRetry);
            if (FailNext)
            {
                FailNext = false;
                return {dbc::vm::BuiltinState::Failed, {}, "test failure"};
            }
            if (Invocation.ArrayArgument)
            {
                SawArrayArgument = true;
                if (Invocation.ReplacesArray)
                    return {dbc::vm::BuiltinState::Completed, Invocation.ArrayArgument->Clone(), {}};
                return {};
            }
            if (PendingNext)
            {
                PendingNext = false;
                return {dbc::vm::BuiltinState::Pending, {}, {}, {dbc::vm::SuspensionKind::NextTick, {}}};
            }
            if (SuspendAfter)
            {
                SuspendAfter = false;
                return {
                    dbc::vm::BuiltinState::CompletedAndSuspend, {}, {}, {dbc::vm::SuspensionKind::FrameBoundary, {}}};
            }
            if (Invocation.ResultType.Kind != dbc::language::TypeKind::Void)
                return {dbc::vm::BuiltinState::Completed, dbc::vm::Value::Integer(7), {}};
            for (const dbc::vm::Value& Argument : Invocation.Arguments)
                Output.push_back(Argument.AsString());
            return {};
        }

        bool Cancel(std::uint64_t InvocationId, std::string& Error) override
        {
            Cancelled.push_back(InvocationId);
            if (FailCancellation)
            {
                Error = "cleanup rejected";
                return false;
            }
            return true;
        }

        bool FailCancellation = false;
        bool PendingNext = false;
        bool SuspendAfter = false;
        bool FailNext = false;
        bool SawArrayArgument = false;
        std::vector<dbc::language::SymbolId> Calls{};
        std::vector<std::uint64_t> InvocationIds{};
        std::vector<bool> Retries{};
        std::vector<std::uint64_t> Cancelled{};
        std::vector<std::string> Output{};
    };

    std::shared_ptr<const dbc::vm::Module> Compile(std::string Text, dbc::language::LanguageProfile Profile)
    {
        const std::filesystem::path Path = Profile == dbc::language::LanguageProfile::Dbc ? "test.dba" : "test.dba";
        const auto Source = std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{Path, std::move(Text), Profile});
        dbc::language::Program Program = dbc::language::Compile(Source);
        for (const auto& Diagnostic : Program.Diagnostics)
            INFO(dbc::language::FormatDiagnostic(*Source, Diagnostic));
        REQUIRE_FALSE(Program.HasErrors());
        const dbc::vm::CompileResult Result = dbc::vm::Lower(Program);
        for (const auto& Diagnostic : Result.Diagnostics)
            INFO(dbc::language::FormatDiagnostic(*Source, Diagnostic));
        REQUIRE(Result.Success());
        const dbc::vm::VerificationResult Verification = dbc::vm::Verify(*Result.Bytecode);
        for (const auto& Diagnostic : Verification.Diagnostics)
            INFO(Diagnostic.Code << ": " << Diagnostic.Message);
        REQUIRE(Verification.Success());
        return Result.Bytecode;
    }

    std::string StubArgument(dbc::language::BuiltinValueType Type)
    {
        return Type == dbc::language::BuiltinValueType::String || Type == dbc::language::BuiltinValueType::Any
                   ? "\"stub\""
                   : "0";
    }

    void RunToCompletion(dbc::vm::ExecutionSession& Session, std::size_t Budget = 32)
    {
        for (std::size_t Iteration = 0; Iteration < 1000; ++Iteration)
        {
            const dbc::vm::TickResult Result = Session.Tick(Budget);
            if (Result.State == dbc::vm::SessionState::Completed)
                return;
            INFO(Result.Error);
            REQUIRE(Result.State != dbc::vm::SessionState::Failed);
            REQUIRE(Result.State != dbc::vm::SessionState::Cancelled);
        }
        FAIL("Execution did not complete");
    }

    dbc::vm::Instruction Instruction(dbc::vm::OpCode Op)
    {
        dbc::vm::Instruction Result;
        Result.Op = Op;
        return Result;
    }

    const dbc::vm::BuiltinCoverageEntry* CoverageFor(const dbc::vm::ExecutionSession& Session,
                                                     dbc::language::SymbolId Symbol)
    {
        const auto Values = Session.Coverage();
        const auto Iterator = std::find_if(Values.begin(), Values.end(), [&](const dbc::vm::BuiltinCoverageEntry& Value)
                                           { return Value.Symbol == Symbol; });
        return Iterator == Values.end() ? nullptr : &*Iterator;
    }
} // namespace

TEST_CASE("Bound control flow lowers to verified bytecode")
{
    const auto Module = Compile("sum = 0\n"
                                "for i = 1 to 5\n"
                                "  sum = sum + i\n"
                                "next i\n"
                                "if sum = 15\n"
                                "  result = 1\n"
                                "else\n"
                                "  result = 0\n"
                                "endif\n",
                                dbc::language::LanguageProfile::Dbc);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session, 3);
    REQUIRE(Session.Global("result") != nullptr);
    CHECK(Session.Global("result")->AsInteger() == 1);
}

TEST_CASE("Every permanent Classic compatibility stub binds, lowers and verifies with its typed overloads")
{
    using dbc::language::BuiltinParameterMode;
    using dbc::language::CompatibilityStubPolicy;
    using dbc::language::LanguageProfile;
    using dbc::language::SymbolKind;

    std::ostringstream Source;
    std::size_t SymbolCount = 0;
    std::size_t OverloadCount = 0;
    for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
    {
        if (dbc::language::StubPolicyFor(Descriptor, LanguageProfile::Dbc) != CompatibilityStubPolicy::Permanent)
        {
            continue;
        }
        ++SymbolCount;
        const auto Overloads = dbc::language::Overloads(Descriptor.Id, LanguageProfile::Dbc);
        REQUIRE_FALSE(Overloads.empty());
        for (const dbc::language::BuiltinOverload* Overload : Overloads)
        {
            REQUIRE(Overload != nullptr);
            ++OverloadCount;
            if (Descriptor.Kind == SymbolKind::Function)
                Source << "print " << Descriptor.CanonicalName << '(';
            else
                Source << Descriptor.CanonicalName << ' ';
            for (std::size_t Index = 0; Index < Overload->Parameters.size(); ++Index)
            {
                if (Index != 0)
                    Source << ',';
                CHECK(Overload->Parameters[Index].Mode == BuiltinParameterMode::Value);
                Source << StubArgument(Overload->Parameters[Index].ValueType);
            }
            if (Descriptor.Kind == SymbolKind::Function)
                Source << ')';
            Source << '\n';
        }
    }
    CHECK(SymbolCount == 95);
    CHECK(OverloadCount >= SymbolCount);
    const auto Module = Compile(Source.str(), LanguageProfile::Dbc);
    REQUIRE(Module != nullptr);
    CHECK(Module->Profile == LanguageProfile::Dbc);
}

TEST_CASE("User functions use explicit frames")
{
    const auto Module = Compile("function Twice(value as integer)\n"
                                "  local copy as integer = value\n"
                                "  exitfunction copy * 2\n"
                                "endfunction\n"
                                "answer = Twice(21)\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session);
    REQUIRE(Session.Global("answer") != nullptr);
    CHECK(Session.Global("answer")->AsInteger() == 42);
}

TEST_CASE("Bare Classic zero-argument function statements lower and execute")
{
    const auto Module = Compile("HandleExplosion\n"
                                "function HandleExplosion()\n"
                                "  print \"handled\"\n"
                                "endfunction\n",
                                dbc::language::LanguageProfile::Dbc);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session);
    REQUIRE(Host.Output.size() == 1);
    CHECK(Host.Output.front() == "handled");
}

TEST_CASE("Arrays and UDT assignments preserve value semantics")
{
    const auto Module = Compile("type Point\n"
                                "  x as integer\n"
                                "endtype\n"
                                "dim points(1) as Point\n"
                                "dim original as Point\n"
                                "original.x = 4\n"
                                "points(0) = original\n"
                                "original.x = 9\n"
                                "inc points(0).x, 2\n"
                                "answer = points(0).x\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session);
    REQUIRE(Session.Global("answer") != nullptr);
    CHECK(Session.Global("answer")->AsInteger() == 6);
}

TEST_CASE("DBPro ELSEIF and dynamic array collections use verified VM state")
{
    const auto Module = Compile("type Item\n"
                                "  value as integer\n"
                                "endtype\n"
                                "dim items(0) as Item\n"
                                "items(0).value = 10\n"
                                "array insert at bottom items()\n"
                                "items().value = 20\n"
                                "add to queue items()\n"
                                "items().value = 30\n"
                                "add to stack items()\n"
                                "items().value = 40\n"
                                "count_before = array count(items())\n"
                                "array index to top items()\n"
                                "first = items().value\n"
                                "next array index items()\n"
                                "second = items().value\n"
                                "array index to bottom items()\n"
                                "last = items().value\n"
                                "previous array index items()\n"
                                "previous = items().value\n"
                                "array insert at top items(), 2\n"
                                "items().value = 1\n"
                                "array insert at element items(), 1\n"
                                "items().value = 2\n"
                                "array delete element items()\n"
                                "array delete element items(), 0\n"
                                "remove from queue items()\n"
                                "remove from stack items()\n"
                                "count_after = array count(items())\n"
                                "array index to queue items()\n"
                                "queue_valid = array index valid(items())\n"
                                "array index to stack items()\n"
                                "stack_valid = array index valid(items())\n"
                                "empty array items()\n"
                                "empty_count = array count(items())\n"
                                "empty_valid = array index valid(items())\n"
                                "if count_before = 99 then\n"
                                "  branch = 1\n"
                                "elseif count_after = 2 then\n"
                                "  branch = 2\n"
                                "else\n"
                                "  branch = 3\n"
                                "endif\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session);
    CHECK(Session.Global("count_before")->AsInteger() == 3);
    CHECK(Session.Global("first")->AsInteger() == 10);
    CHECK(Session.Global("second")->AsInteger() == 20);
    CHECK(Session.Global("last")->AsInteger() == 40);
    CHECK(Session.Global("previous")->AsInteger() == 30);
    CHECK(Session.Global("count_after")->AsInteger() == 2);
    CHECK(Session.Global("queue_valid")->AsInteger() == 1);
    CHECK(Session.Global("stack_valid")->AsInteger() == 1);
    CHECK(Session.Global("empty_count")->AsInteger() == -1);
    CHECK(Session.Global("empty_valid")->AsInteger() == 0);
    CHECK(Session.Global("branch")->AsInteger() == 2);
    CHECK(Host.Calls.empty());
}

TEST_CASE("DBPro dynamic array collection faults remain typed runtime failures")
{
    const auto Module = Compile("dim values(1, 1) as integer\n"
                                "array insert at bottom values()\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host, true));
    const dbc::vm::TickResult Result = Session.Tick(32);
    CHECK(Result.State == dbc::vm::SessionState::Failed);
    CHECK(Result.Error.find("one-dimensional array") != std::string::npos);
    CHECK(Host.Calls.empty());
    const dbc::vm::BuiltinCoverageEntry* Coverage =
        CoverageFor(Session, dbc::language::SymbolId::Command_ArrayInsertAtBottom);
    REQUIRE(Coverage != nullptr);
    CHECK(Coverage->Attempts == 1);
    CHECK(Coverage->Completed == 0);
    CHECK(Coverage->Failed == 1);
}

TEST_CASE("Array updates and real FOR variables retain their bound types")
{
    const auto Module = Compile("dim values(1) as integer\n"
                                "values(0) = 1\n"
                                "inc values(0), 2\n"
                                "sum# = 0.0\n"
                                "for i# = 1.0 to 2.0 step 0.5\n"
                                "  sum# = sum# + i#\n"
                                "next i#\n"
                                "answer = values(0)\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session);
    REQUIRE(Session.Global("answer") != nullptr);
    REQUIRE(Session.Global("sum#") != nullptr);
    CHECK(Session.Global("answer")->AsInteger() == 3);
    CHECK(Session.Global("sum#")->AsReal() == doctest::Approx(4.5));
}

TEST_CASE("GOSUB targets use an explicit subroutine stack")
{
    const auto Module = Compile("value = 1\n"
                                "gosub add_two\n"
                                "end\n"
                                "add_two:\n"
                                "value = value + 2\n"
                                "return\n",
                                dbc::language::LanguageProfile::Dbc);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host));
    RunToCompletion(Session);
    REQUIRE(Session.Global("value") != nullptr);
    CHECK(Session.Global("value")->AsInteger() == 3);
}

TEST_CASE("DATA READ and RESTORE use the typed module pool")
{
    const auto Module = Compile("type Holder\n"
                                "  value as integer\n"
                                "endtype\n"
                                "dim item as Holder\n"
                                "data 1, \"alpha\"\n"
                                "again:\n"
                                "data 7\n"
                                "read first, text$\n"
                                "read item.value\n"
                                "restore again\n"
                                "read repeated\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host, true));
    RunToCompletion(Session);
    REQUIRE(Session.Global("first") != nullptr);
    REQUIRE(Session.Global("text$") != nullptr);
    REQUIRE(Session.Global("item") != nullptr);
    REQUIRE(Session.Global("repeated") != nullptr);
    CHECK(Session.Global("first")->AsInteger() == 1);
    CHECK(Session.Global("text$")->AsString() == "alpha");
    const auto Record = std::get<std::shared_ptr<dbc::vm::RecordValue>>(Session.Global("item")->Data());
    CHECK(Record->Fields.at("VALUE").AsInteger() == 7);
    CHECK(Session.Global("repeated")->AsInteger() == 7);
    const dbc::vm::BuiltinCoverageEntry* Read = CoverageFor(Session, dbc::language::SymbolId::Command_Read);
    REQUIRE(Read != nullptr);
    CHECK(Read->Attempts == 3);
    CHECK(Read->Completed == 3);
    CHECK(Read->Failed == 0);
    const dbc::vm::BuiltinCoverageEntry* Restore = CoverageFor(Session, dbc::language::SymbolId::Command_Restore);
    REQUIRE(Restore != nullptr);
    CHECK(Restore->Attempts == 1);
    CHECK(Restore->Completed == 1);
    CHECK(Restore->Failed == 0);
}

TEST_CASE("builtin coverage supports calls nested in VM-handled READ targets")
{
    const auto Module = Compile("dim values(7)\n"
                                "data 7\n"
                                "read values(rnd(0))\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host, true));
    RunToCompletion(Session);

    const dbc::vm::BuiltinCoverageEntry* Read = CoverageFor(Session, dbc::language::SymbolId::Command_Read);
    REQUIRE(Read != nullptr);
    CHECK(Read->Attempts == 1);
    CHECK(Read->Completed == 1);
    CHECK(Read->Failed == 0);
    const dbc::vm::BuiltinCoverageEntry* Random = CoverageFor(Session, dbc::language::SymbolId::Function_Rnd);
    REQUIRE(Random != nullptr);
    CHECK(Random->Attempts == 1);
    CHECK(Random->Completed == 1);
    CHECK(Random->Failed == 0);
}

TEST_CASE("Output commands store typed results and whole-array calls are explicit")
{
    const auto Module = Compile("dim values(1) as integer\n"
                                "read long 1, values(0)\n"
                                "save array \"values.bin\", values(0)\n"
                                "load array \"values.bin\", values(0)\n"
                                "undim values()\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host, true));
    RunToCompletion(Session);
    REQUIRE(Session.Global("values") != nullptr);
    CHECK(std::holds_alternative<std::monostate>(Session.Global("values")->Data()));
    CHECK(Host.SawArrayArgument);
    const dbc::vm::BuiltinCoverageEntry* Undim = CoverageFor(Session, dbc::language::SymbolId::Command_Undim);
    REQUIRE(Undim != nullptr);
    CHECK(Undim->Attempts == 1);
    CHECK(Undim->Completed == 1);
    CHECK(Undim->Failed == 0);
}

TEST_CASE("whole-array validation failures are recorded by builtin coverage")
{
    const auto Module = Compile("dim values(0) as integer\n"
                                "undim values()\n"
                                "save array \"values.bin\", values(0)\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host, true));
    CHECK(Session.Tick(64).State == dbc::vm::SessionState::Failed);
    const dbc::vm::BuiltinCoverageEntry* Save = CoverageFor(Session, dbc::language::SymbolId::Command_SaveArray);
    REQUIRE(Save != nullptr);
    CHECK(Save->Attempts == 1);
    CHECK(Save->Completed == 0);
    CHECK(Save->Failed == 1);
}

TEST_CASE("builtin coverage keeps overload identities distinct")
{
    const auto Module = Compile("color light 1, 255\n"
                                "color light 1, 2, 3, 4\n",
                                dbc::language::LanguageProfile::DbPro);
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Module, Host, true));
    RunToCompletion(Session);

    std::vector<dbc::vm::BuiltinCoverageEntry> Matches;
    for (const dbc::vm::BuiltinCoverageEntry& Value : Session.Coverage())
    {
        if (Value.Symbol == dbc::language::SymbolId::Command_ColorLight)
            Matches.push_back(Value);
    }
    REQUIRE(Matches.size() == 2);
    CHECK(Matches[0].Overload != Matches[1].Overload);
    for (const dbc::vm::BuiltinCoverageEntry& Value : Matches)
    {
        CHECK(Value.Attempts == 1);
        CHECK(Value.Completed == 1);
        CHECK(Value.Failed == 0);
    }
}

TEST_CASE("Tick budgets cancellation and suspended builtins are cooperative")
{
    auto Loop = std::make_shared<dbc::vm::Module>();
    Loop->Functions.push_back({"<entry>", {dbc::language::TypeKind::Void, {}}, 0, {}, {}});
    Loop->Functions.front().Code.push_back(Instruction(dbc::vm::OpCode::Jump));
    TestHost Host;
    dbc::vm::ExecutionSession Session;
    REQUIRE(Session.Prepare(Loop, Host));
    const dbc::vm::TickResult Slice = Session.Tick(3);
    CHECK(Slice.State == dbc::vm::SessionState::Running);
    CHECK(Slice.Instructions == 3);
    Session.RequestStop();
    CHECK(Session.Tick(3).State == dbc::vm::SessionState::Cancelled);

    auto Yield = std::make_shared<dbc::vm::Module>();
    Yield->Functions.push_back({"<entry>", {dbc::language::TypeKind::Void, {}}, 0, {}, {}});
    dbc::vm::Instruction Call = Instruction(dbc::vm::OpCode::CallBuiltin);
    Call.ResultType = {dbc::language::TypeKind::Void, {}};
    Call.A = static_cast<std::uint32_t>(dbc::language::SymbolId::Command_Sync);
    const dbc::language::BuiltinOverload* SyncOverload =
        dbc::language::FindOverload(dbc::language::SymbolId::Command_Sync, dbc::language::LanguageProfile::Dbc, 0);
    REQUIRE(SyncOverload != nullptr);
    Call.E = SyncOverload->Id.Value;
    Yield->Functions.front().Code.push_back(Call);
    Yield->Functions.front().Code.push_back(Instruction(dbc::vm::OpCode::Halt));
    Host.PendingNext = true;
    dbc::vm::ExecutionSession YieldSession;
    REQUIRE(YieldSession.Prepare(Yield, Host, true));
    CHECK(YieldSession.Tick(8).State == dbc::vm::SessionState::Suspended);
    CHECK(YieldSession.Tick(8).State == dbc::vm::SessionState::Completed);
    CHECK(Host.Calls.size() == 2);
    CHECK(Host.InvocationIds[0] == Host.InvocationIds[1]);
    CHECK_FALSE(Host.Retries[0]);
    CHECK(Host.Retries[1]);
    const dbc::vm::BuiltinCoverageEntry* YieldCoverage =
        CoverageFor(YieldSession, dbc::language::SymbolId::Command_Sync);
    REQUIRE(YieldCoverage != nullptr);
    CHECK(YieldCoverage->Attempts == 1);
    CHECK(YieldCoverage->Completed == 1);
    CHECK(YieldCoverage->Failed == 0);

    TestHost CancellingHost;
    CancellingHost.PendingNext = true;
    dbc::vm::ExecutionSession CancellingSession;
    REQUIRE(CancellingSession.Prepare(Yield, CancellingHost, true));
    CHECK(CancellingSession.Tick(8).State == dbc::vm::SessionState::Suspended);
    CancellingSession.RequestStop();
    CHECK(CancellingSession.Tick(8).State == dbc::vm::SessionState::Cancelled);
    REQUIRE(CancellingHost.Cancelled.size() == 1);
    CHECK(CancellingHost.Cancelled.front() == CancellingHost.InvocationIds.front());
    const dbc::vm::BuiltinCoverageEntry* CancelledCoverage =
        CoverageFor(CancellingSession, dbc::language::SymbolId::Command_Sync);
    REQUIRE(CancelledCoverage != nullptr);
    CHECK(CancelledCoverage->Attempts == 1);
    CHECK(CancelledCoverage->Completed == 0);
    CHECK(CancelledCoverage->Failed == 0);

    TestHost CleanupHost;
    CleanupHost.PendingNext = true;
    CleanupHost.FailCancellation = true;
    dbc::vm::ExecutionSession CleanupSession;
    REQUIRE(CleanupSession.Prepare(Yield, CleanupHost));
    REQUIRE(CleanupSession.Tick(8).State == dbc::vm::SessionState::Suspended);
    CleanupSession.RequestStop();
    CHECK(CleanupSession.Tick(8).State == dbc::vm::SessionState::Failed);
    CHECK(CleanupSession.Failure().Message.find("cleanup rejected") != std::string::npos);
    CHECK(CleanupHost.Cancelled.size() == 1);
    CHECK(CleanupSession.Tick(8).State == dbc::vm::SessionState::Failed);
    CHECK(CleanupHost.Cancelled.size() == 1);

    TestHost FailingHost;
    FailingHost.FailNext = true;
    dbc::vm::ExecutionSession FailingSession;
    REQUIRE(FailingSession.Prepare(Yield, FailingHost, true));
    CHECK(FailingSession.Tick(8).State == dbc::vm::SessionState::Failed);
    const dbc::vm::BuiltinCoverageEntry* FailedCoverage =
        CoverageFor(FailingSession, dbc::language::SymbolId::Command_Sync);
    REQUIRE(FailedCoverage != nullptr);
    CHECK(FailedCoverage->Attempts == 1);
    CHECK(FailedCoverage->Completed == 0);
    CHECK(FailedCoverage->Failed == 1);

    TestHost FrameHost;
    FrameHost.SuspendAfter = true;
    dbc::vm::ExecutionSession FrameSession;
    REQUIRE(FrameSession.Prepare(Yield, FrameHost));
    const dbc::vm::TickResult Frame = FrameSession.Tick(8);
    CHECK(Frame.State == dbc::vm::SessionState::Suspended);
    CHECK(Frame.Wait.Kind == dbc::vm::SuspensionKind::FrameBoundary);
    CHECK(FrameSession.Tick(8).State == dbc::vm::SessionState::Completed);
    CHECK(FrameHost.Calls.size() == 1);
}

TEST_CASE("Verifier rejects malformed control flow and stack joins")
{
    dbc::vm::Module InvalidJump;
    InvalidJump.Functions.push_back({"<entry>", {dbc::language::TypeKind::Void, {}}, 0, {}, {}});
    dbc::vm::Instruction Jump = Instruction(dbc::vm::OpCode::Jump);
    Jump.A = 9;
    InvalidJump.Functions.front().Code.push_back(Jump);
    CHECK_FALSE(dbc::vm::Verify(InvalidJump).Success());

    dbc::vm::Module InvalidJoin;
    InvalidJoin.Functions.push_back({"<entry>", {dbc::language::TypeKind::Void, {}}, 0, {}, {}});
    dbc::vm::Instruction Push = Instruction(dbc::vm::OpCode::PushInteger);
    Push.ResultType = {dbc::language::TypeKind::Integer, {}};
    InvalidJoin.Functions.front().Code.push_back(Push);
    dbc::vm::Instruction Branch = Instruction(dbc::vm::OpCode::JumpIfFalse);
    Branch.A = 3;
    InvalidJoin.Functions.front().Code.push_back(Branch);
    InvalidJoin.Functions.front().Code.push_back(Push);
    InvalidJoin.Functions.front().Code.push_back(Instruction(dbc::vm::OpCode::Halt));
    CHECK_FALSE(dbc::vm::Verify(InvalidJoin).Success());

    const auto ValidBuiltin = Compile("sync\n", dbc::language::LanguageProfile::Dbc);
    dbc::vm::Module InvalidOverload = *ValidBuiltin;
    auto Call =
        std::find_if(InvalidOverload.Functions.front().Code.begin(), InvalidOverload.Functions.front().Code.end(),
                     [](const dbc::vm::Instruction& Value) { return Value.Op == dbc::vm::OpCode::CallBuiltin; });
    REQUIRE(Call != InvalidOverload.Functions.front().Code.end());
    Call->E = dbc::language::OverloadId::Invalid;
    CHECK_FALSE(dbc::vm::Verify(InvalidOverload).Success());

    const auto Collection = Compile("dim values(0) as integer\n"
                                    "count = array count(values())\n",
                                    dbc::language::LanguageProfile::DbPro);
    dbc::vm::Module InvalidCollection = *Collection;
    auto ArrayCall =
        std::find_if(InvalidCollection.Functions.front().Code.begin(), InvalidCollection.Functions.front().Code.end(),
                     [](const dbc::vm::Instruction& Value) { return Value.Op == dbc::vm::OpCode::CallBuiltinArray; });
    REQUIRE(ArrayCall != InvalidCollection.Functions.front().Code.end());
    ArrayCall->D = 0;
    CHECK_FALSE(dbc::vm::Verify(InvalidCollection).Success());

    const auto MarkedRead = Compile("data 1\nread value\n", dbc::language::LanguageProfile::Dbc);
    dbc::vm::Module MissingBegin = *MarkedRead;
    auto BeginMarker =
        std::find_if(MissingBegin.Functions.front().Code.begin(), MissingBegin.Functions.front().Code.end(),
                     [](const dbc::vm::Instruction& Value) { return Value.Op == dbc::vm::OpCode::BeginBuiltin; });
    REQUIRE(BeginMarker != MissingBegin.Functions.front().Code.end());
    MissingBegin.Functions.front().Code.erase(BeginMarker);
    CHECK_FALSE(dbc::vm::Verify(MissingBegin).Success());

    dbc::vm::Module MissingComplete = *MarkedRead;
    auto CompleteMarker =
        std::find_if(MissingComplete.Functions.front().Code.begin(), MissingComplete.Functions.front().Code.end(),
                     [](const dbc::vm::Instruction& Value) { return Value.Op == dbc::vm::OpCode::CompleteBuiltin; });
    REQUIRE(CompleteMarker != MissingComplete.Functions.front().Code.end());
    MissingComplete.Functions.front().Code.erase(CompleteMarker);
    CHECK_FALSE(dbc::vm::Verify(MissingComplete).Success());
}

TEST_CASE("Every DBA golden source passes the typed frontend and verified lowering")
{
    const std::filesystem::path Directory = std::filesystem::path(TEST_SOURCE_DIR) / "tests" / "dba";
    std::map<std::string, dbc::language::LanguageProfile> Profiles;
    {
        std::ifstream Input(Directory / "test_profiles.tsv");
        for (std::string Line; std::getline(Input, Line);)
        {
            if (Line.empty() || Line.front() == '#')
                continue;
            const std::size_t Separator = Line.find('\t');
            REQUIRE(Separator != std::string::npos);
            Profiles.emplace(Line.substr(0, Separator), Line.substr(Separator + 1) == "dbc"
                                                            ? dbc::language::LanguageProfile::Dbc
                                                            : dbc::language::LanguageProfile::DbPro);
        }
    }
    std::vector<std::filesystem::path> Sources;
    std::size_t Goldens = 0;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Directory))
    {
        const std::filesystem::path& Path = Entry.path();
        if (Entry.is_regular_file() && Path.extension() == ".expected")
        {
            ++Goldens;
        }
        if (!Entry.is_regular_file() || Path.stem().string().rfind("test_", 0) != 0 || Path.extension() != ".dba")
        {
            continue;
        }
        Sources.push_back(Path);
    }
    std::sort(Sources.begin(), Sources.end());
    REQUIRE(Goldens > 0);
    REQUIRE(Sources.size() == Goldens);

    std::ostringstream Failures;
    std::size_t VerifiedCount = 0;
    for (const std::filesystem::path& Path : Sources)
    {
        std::filesystem::path Expected = Path;
        Expected.replace_extension(".expected");
        if (!std::filesystem::is_regular_file(Expected))
        {
            Failures << Path.filename().string() << ": missing golden output\n";
            continue;
        }

        const auto Profile = Profiles.find(Path.filename().string());
        if (Profile == Profiles.end())
        {
            Failures << Path.filename().string() << ": missing explicit profile\n";
            continue;
        }

        std::ifstream Input(Path, std::ios::binary);
        std::ostringstream Text;
        Text << Input.rdbuf();
        if (!Input.good() && !Input.eof())
        {
            Failures << Path.filename().string() << ": failed to read source\n";
            continue;
        }

        const auto Source = std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{Path, Text.str(), Profile->second});
        const dbc::language::IncludeLoader LoadInclude =
            [](const dbc::language::SourceDocument& Including, const std::filesystem::path& Include,
               std::string& Error) -> std::shared_ptr<const dbc::language::SourceDocument>
        {
            const std::filesystem::path Resolved =
                Include.is_absolute() ? Include : Including.LogicalPath.parent_path() / Include;
            std::ifstream Stream(Resolved, std::ios::binary);
            if (!Stream)
            {
                Error = "#INCLUDE not found: " + Resolved.string();
                return {};
            }
            std::ostringstream Body;
            Body << Stream.rdbuf();
            return std::make_shared<const dbc::language::SourceDocument>(
                dbc::language::SourceDocument{Resolved, Body.str(), Including.Profile});
        };
        const dbc::language::Program Program = dbc::language::Compile(Source, {}, LoadInclude);
        if (Program.HasErrors())
        {
            Failures << Path.filename().string() << ": frontend failed\n";
            for (const dbc::language::Diagnostic& Diagnostic : Program.Diagnostics)
            {
                if (Diagnostic.Severity == dbc::language::DiagnosticSeverity::Error)
                    Failures << "  " << dbc::language::FormatDiagnostic(*Source, Diagnostic) << '\n';
            }
            continue;
        }

        const dbc::vm::CompileResult Lowered = dbc::vm::Lower(Program);
        if (!Lowered.Success())
        {
            Failures << Path.filename().string() << ": lowering failed\n";
            for (const dbc::language::Diagnostic& Diagnostic : Lowered.Diagnostics)
                Failures << "  " << dbc::language::FormatDiagnostic(*Source, Diagnostic) << '\n';
            continue;
        }

        const dbc::vm::VerificationResult Verification = dbc::vm::Verify(*Lowered.Bytecode);
        if (!Verification.Success())
        {
            Failures << Path.filename().string() << ": verification failed\n";
            for (const dbc::language::Diagnostic& Diagnostic : Verification.Diagnostics)
                Failures << "  " << Diagnostic.Code << ": " << Diagnostic.Message << '\n';
            continue;
        }
        ++VerifiedCount;
    }

    INFO(Failures.str());
    CHECK(VerifiedCount == Sources.size());
}

TEST_CASE("Numeric conversion rejects non-finite values and the exact exclusive integer bound")
{
    std::string Error;
    std::int64_t Integer = 0;
    const double Bound = std::ldexp(1.0, 63);
    for (const double Number : {Bound, -std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        CAPTURE(Number);
        CHECK_FALSE(dbc::language::ConvertInteger(Number, Integer, &Error));
        CHECK_FALSE(Error.empty());
    }
    REQUIRE(dbc::language::ConvertInteger(-Bound, Integer, &Error));
    CHECK(Integer == std::numeric_limits<std::int64_t>::min());
    REQUIRE(dbc::language::ConvertInteger(std::nextafter(Bound, 0.0), Integer, &Error));
    REQUIRE(dbc::language::ConvertInteger(-12.9, Integer, &Error));
    CHECK(Integer == -12);
    int Narrow = 0;
    CHECK_FALSE(dbc::language::ConvertInteger(std::ldexp(1.0, 31), Narrow, &Error));
    CHECK_FALSE(dbc::language::ConvertInteger(std::ldexp(1.0, 31), Narrow));
    CHECK(Narrow == 0);
    double Parsed = 0.0;
    CHECK_FALSE(dbc::language::ParseNumber("1e9999", Parsed, &Error));
    CHECK_FALSE(dbc::language::ParseNumber("nan", Parsed, &Error));
    CHECK_FALSE(dbc::language::ParseNumber("invalid", Parsed, &Error));
    CHECK_FALSE(dbc::language::ParseNumber("invalid", Parsed));
    CHECK(Parsed == 0.0);
    REQUIRE(dbc::language::ParseNumber("  -12.5 suffix", Parsed, &Error));
    CHECK(Parsed == -12.5);
    dbc::vm::Value Converted;
    CHECK_FALSE(dbc::vm::Value::Real(Bound).ConvertTo({dbc::language::TypeKind::Integer, {}}, Converted, Error));
    auto NullRecord = dbc::vm::Value::Record({}, {dbc::language::TypeKind::UserDefined, "EMPTY"});
    CHECK_FALSE(NullRecord.ConvertTo(NullRecord.Type(), Converted, Error));
    CHECK_NOTHROW(NullRecord.Clone());
}
