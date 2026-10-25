#include "engine_builtin_host.hpp"
#include "atomic_file.hpp"

#include "basic_path.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/file_io_system.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/vr_system.hpp"
#include "engine_builtin_api/text.hpp"
#include "engine_builtin_registry.hpp"
#include "language/numeric_conversion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace dbc::interpreter::detail
{
    namespace
    {
        vm::BuiltinResult Failed(std::string Message)
        {
            return {vm::BuiltinState::Failed, {}, std::move(Message)};
        }

        vm::BuiltinResult Suspend(vm::SuspensionKind Kind,
                                  std::optional<std::chrono::steady_clock::time_point> Deadline = {})
        {
            return {vm::BuiltinState::Pending, {}, {}, {Kind, Deadline}};
        }

        bool EngineInteger(const vm::Value& Source, int& Result, std::string& Error)
        {
            std::int64_t Wide = 0;
            if (!Source.AsInteger(Wide, Error))
                return false;
            if (Wide < std::numeric_limits<int>::min() || Wide > std::numeric_limits<int>::max())
            {
                Error = "Numeric value is out of engine integer range";
                return false;
            }
            Result = static_cast<int>(Wide);
            return true;
        }

        std::optional<BuiltinValue> ToLegacy(const vm::Value& Source, std::string& Error)
        {
            if (Source.Type().Kind == language::TypeKind::String)
                return BuiltinValue::FromString(Source.AsString());
            if (!Source.Type().IsNumeric())
            {
                Error = "Builtin arguments must be scalar values";
                return std::nullopt;
            }
            double Number = 0.0;
            if (const auto* Integer = std::get_if<std::int64_t>(&Source.Data()))
            {
                constexpr std::int64_t MaxExact = std::int64_t{1} << 53;
                if (*Integer < -MaxExact || *Integer > MaxExact)
                {
                    Error = "Integer argument cannot be represented exactly by the engine bridge";
                    return std::nullopt;
                }
                Number = static_cast<double>(*Integer);
            }
            else
            {
                Number = Source.AsReal();
            }
            if (!std::isfinite(Number))
            {
                Error = "Numeric builtin argument is not finite";
                return std::nullopt;
            }
            return BuiltinValue::FromNumber(Number);
        }

        std::optional<vm::Value> FromLegacy(const BuiltinValue& Source, const language::Type& Target,
                                            std::string& Error)
        {
            if (Target.Kind == language::TypeKind::String)
            {
                if (!Source.IsString)
                {
                    Error = "Builtin returned a number where a string was required";
                    return std::nullopt;
                }
                return vm::Value::String(Source.Text);
            }
            if (Source.IsString || !std::isfinite(Source.Number))
            {
                Error = "Builtin returned a non-numeric value";
                return std::nullopt;
            }
            if (Target.Kind == language::TypeKind::Real)
                return vm::Value::Real(Source.Number);
            if (Source.Number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                Source.Number >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            {
                Error = "Builtin integer result is out of range";
                return std::nullopt;
            }
            return vm::Value::Integer(static_cast<std::int64_t>(Source.Number));
        }

    } // namespace

    EngineBuiltinHost::EngineBuiltinHost(BuiltinContext& Context) : Context_(Context) {}

    vm::BuiltinResult EngineBuiltinHost::Invoke(const vm::BuiltinInvocation& Invocation)
    {
        if (Invocation.Symbol >= language::SymbolId::Count)
            return Failed("Builtin symbol is outside the language catalogue");
        const language::SymbolDescriptor& Descriptor = language::Symbol(Invocation.Symbol);
        if (!language::ExecutableIn(Descriptor, Context_.Profile))
        {
            const bool IsCommand = Descriptor.Kind == language::SymbolKind::Command;
            if (!language::AvailableIn(Descriptor, Context_.Profile))
            {
                return Failed(std::string(IsCommand ? "Command is not available in this profile: "
                                                    : "Function is not available in this profile: ") +
                              std::string(Descriptor.CanonicalName));
            }
            return Failed(std::string(IsCommand ? "Unsupported command: " : "Unsupported function: ") +
                          std::string(Descriptor.CanonicalName));
        }
        if (language::SupportFor(Descriptor, Context_.Profile) == language::SupportStatus::CompatibilityStub)
            return InvokeCompatibilityStub(Invocation);
        if (std::optional<vm::BuiltinResult> Inert = InvokeInertResourceContinuation(Invocation))
            return std::move(*Inert);
        if (!Context_.Kernel)
            return Failed("Engine builtin host has no kernel");
        if (Invocation.Symbol == language::SymbolId::Command_Fastsync)
        {
            Context_.Kernel->Core().FastSync();
            return {vm::BuiltinState::CompletedAndSuspend, {}, {}, {vm::SuspensionKind::FrameBoundary, {}}};
        }
        const language::BuiltinConvention Convention = language::ConventionFor(Invocation.Symbol);
        switch (Convention)
        {
            case language::BuiltinConvention::Sync:
                return InvokeSync(Invocation);
            case language::BuiltinConvention::Wait:
                return InvokeWait(Invocation);
            case language::BuiltinConvention::WaitKey:
                return InvokeWaitKey(Invocation);
            case language::BuiltinConvention::SuspendForMouse:
                return InvokeWaitMouse(Invocation);
            case language::BuiltinConvention::Input:
                return InvokeInput(Invocation);
            case language::BuiltinConvention::FileRead:
                return InvokeFileRead(Invocation);
            case language::BuiltinConvention::LoadArray:
            case language::BuiltinConvention::SaveArray:
                return InvokeArray(Invocation);
            case language::BuiltinConvention::DataLiteral:
            case language::BuiltinConvention::DataRead:
            case language::BuiltinConvention::DataRestore:
            case language::BuiltinConvention::Undim:
            case language::BuiltinConvention::ArrayCollection:
                return Failed("VM-owned builtin escaped bytecode lowering");
            case language::BuiltinConvention::Print:
                return InvokePrint(Invocation);
            case language::BuiltinConvention::Values:
                return InvokeValue(Invocation);
        }
        return Failed("Unknown builtin convention");
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeCompatibilityStub(const vm::BuiltinInvocation& Invocation)
    {
        const language::SymbolDescriptor& Descriptor = language::Symbol(Invocation.Symbol);
        if (LoggedCompatibilityStubs_.insert(Invocation.Symbol).second)
            DBC_LOG_DEBUG("Compatibility stub: {}", Descriptor.CanonicalName);

        const language::CompatibilityStubBehavior Behavior = language::StubBehaviorFor(Descriptor, Context_.Profile);
        switch (Behavior)
        {
            case language::CompatibilityStubBehavior::NoOp:
                return Invocation.ResultType.Kind == language::TypeKind::Void
                           ? vm::BuiltinResult{}
                           : Failed("Compatibility command stub has a non-void result type");
            case language::CompatibilityStubBehavior::AcceptInertResource:
                if (Invocation.Symbol == language::SymbolId::Command_LoadCdmusic && Invocation.Arguments.size() >= 2)
                {
                    int Id = 0;
                    std::string Error;
                    if (!EngineInteger(Invocation.Arguments[1], Id, Error))
                        return Failed(Error);
                    Context_.InertMusicIds.insert(Id);
                }
                return Invocation.ResultType.Kind == language::TypeKind::Void
                           ? vm::BuiltinResult{}
                           : Failed("Compatibility resource stub has a non-void result type");
            case language::CompatibilityStubBehavior::ClearChecklist:
                Context_.Checklist.clear();
                return Invocation.ResultType.Kind == language::TypeKind::Void
                           ? vm::BuiltinResult{}
                           : Failed("Compatibility checklist stub has a non-void result type");
            case language::CompatibilityStubBehavior::EmptyString:
                return Invocation.ResultType.Kind == language::TypeKind::String
                           ? vm::BuiltinResult{vm::BuiltinState::Completed, vm::Value::String({}), {}}
                           : Failed("Compatibility string stub has a non-string result type");
            case language::CompatibilityStubBehavior::NumericZero:
            case language::CompatibilityStubBehavior::NumericOne:
            case language::CompatibilityStubBehavior::NumericNegativeOne:
                break;
            case language::CompatibilityStubBehavior::None:
                return Failed("Compatibility stub has no neutral behavior");
        }

        const double Numeric = Behavior == language::CompatibilityStubBehavior::NumericOne
                                   ? 1.0
                                   : (Behavior == language::CompatibilityStubBehavior::NumericNegativeOne ? -1.0 : 0.0);
        switch (Invocation.ResultType.Kind)
        {
            case language::TypeKind::Boolean:
            case language::TypeKind::Integer:
                return {vm::BuiltinState::Completed, vm::Value::Integer(static_cast<std::int64_t>(Numeric)), {}};
            case language::TypeKind::Real:
                return {vm::BuiltinState::Completed, vm::Value::Real(Numeric), {}};
            case language::TypeKind::String:
            case language::TypeKind::Error:
            case language::TypeKind::Unresolved:
            case language::TypeKind::Void:
            case language::TypeKind::UserDefined:
                return Failed("Compatibility stub has an invalid result type");
        }
        return Failed("Compatibility stub has an unknown result type");
    }

    std::optional<vm::BuiltinResult> EngineBuiltinHost::InvokeInertResourceContinuation(
        const vm::BuiltinInvocation& Invocation)
    {
        if (Context_.Profile != language::LanguageProfile::Dbc || Invocation.Arguments.empty())
            return std::nullopt;
        if (Invocation.Symbol == language::SymbolId::Command_LoadMusic)
        {
            if (Invocation.Arguments.size() >= 2)
            {
                int Id = 0;
                std::string Error;
                if (!EngineInteger(Invocation.Arguments[1], Id, Error))
                    return Failed(Error);
                Context_.InertMusicIds.erase(Id);
            }
            return std::nullopt;
        }
        switch (Invocation.Symbol)
        {
            case language::SymbolId::Command_PlayMusic:
            case language::SymbolId::Command_LoopMusic:
            case language::SymbolId::Command_StopMusic:
            case language::SymbolId::Command_PauseMusic:
            case language::SymbolId::Command_ResumeMusic:
            case language::SymbolId::Command_DeleteMusic:
            case language::SymbolId::Command_SetMusicVolume:
            case language::SymbolId::Command_SetMusicSpeed:
            case language::SymbolId::Function_MusicExist:
            case language::SymbolId::Function_MusicPlaying:
            case language::SymbolId::Function_MusicLooping:
            case language::SymbolId::Function_MusicPaused:
            case language::SymbolId::Function_MusicVolume:
            case language::SymbolId::Function_MusicSpeed:
                break;
            default:
                return std::nullopt;
        }
        int ResourceId = 0;
        std::string Error;
        if (!EngineInteger(Invocation.Arguments.front(), ResourceId, Error))
            return Failed(Error);
        if (!Context_.InertMusicIds.contains(ResourceId))
            return std::nullopt;

        switch (Invocation.Symbol)
        {
            case language::SymbolId::Command_PlayMusic:
            case language::SymbolId::Command_LoopMusic:
            case language::SymbolId::Command_StopMusic:
            case language::SymbolId::Command_PauseMusic:
            case language::SymbolId::Command_ResumeMusic:
            case language::SymbolId::Command_SetMusicVolume:
            case language::SymbolId::Command_SetMusicSpeed:
                return vm::BuiltinResult{};
            case language::SymbolId::Command_DeleteMusic:
                Context_.InertMusicIds.erase(ResourceId);
                return vm::BuiltinResult{};
            case language::SymbolId::Function_MusicExist:
            case language::SymbolId::Function_MusicPlaying:
            case language::SymbolId::Function_MusicLooping:
            case language::SymbolId::Function_MusicPaused:
            case language::SymbolId::Function_MusicVolume:
            case language::SymbolId::Function_MusicSpeed:
                if (Invocation.ResultType.Kind == language::TypeKind::Integer ||
                    Invocation.ResultType.Kind == language::TypeKind::Boolean)
                {
                    return vm::BuiltinResult{vm::BuiltinState::Completed, vm::Value::Integer(0), {}};
                }
                if (Invocation.ResultType.Kind == language::TypeKind::Real)
                    return vm::BuiltinResult{vm::BuiltinState::Completed, vm::Value::Real(0.0), {}};
                return Failed("Inert music query has a non-numeric result type");
            default:
                return std::nullopt;
        }
    }

    vm::BuiltinResult EngineBuiltinHost::InvokePrint(const vm::BuiltinInvocation& Invocation)
    {
        std::string Text;
        std::string Error;
        for (const vm::Value& Argument : Invocation.Arguments)
        {
            std::optional<BuiltinValue> Converted = ToLegacy(Argument, Error);
            if (!Converted)
                return Failed(std::move(Error));
            Text += Converted->AsString();
        }

        if (Invocation.ContinuesPrint)
            Printc(*Context_.Kernel, Text);
        else
            Print(*Context_.Kernel, Text);

        Context_.PendingPrint += Text;
        if (Invocation.ContinuesPrint)
            return {};

        const std::string Line = std::move(Context_.PendingPrint);
        Context_.PendingPrint.clear();
        if (Context_.Output)
        {
            Context_.Output(Line);
        }
        else
        {
            std::fprintf(stdout, "%s\n", Line.c_str());
            std::fflush(stdout);
        }
        if (Context_.LogFile)
        {
            std::fprintf(Context_.LogFile, "%s\n", Line.c_str());
            std::fflush(Context_.LogFile);
        }
        return {};
    }

    bool EngineBuiltinHost::Cancel(std::uint64_t InvocationId, std::string& Error)
    {
        const auto Iterator = Pending_.find(InvocationId);
        bool Cancelled = true;
        if (Iterator != Pending_.end() && Iterator->second.CurrentPhase == Phase::TextInput && Context_.Kernel)
            Cancelled = Context_.Kernel->Input().EndTextInput(&Error);
        Pending_.erase(InvocationId);
        return Cancelled;
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeValue(const vm::BuiltinInvocation& Invocation)
    {
        std::vector<BuiltinValue> Arguments;
        Arguments.reserve(Invocation.Arguments.size());
        std::string Error;
        for (const vm::Value& Argument : Invocation.Arguments)
        {
            std::optional<BuiltinValue> Converted = ToLegacy(Argument, Error);
            if (!Converted)
                return Failed(std::move(Error));
            Arguments.push_back(std::move(*Converted));
        }
        const language::SymbolDescriptor& Descriptor = language::Symbol(Invocation.Symbol);
        if (Descriptor.Kind == language::SymbolKind::Command)
        {
            if (!InvokeCommand(Context_, Invocation.Symbol, Arguments, Error))
                return Failed(std::move(Error));
            if (Invocation.Symbol == language::SymbolId::Command_Cls ||
                Invocation.Symbol == language::SymbolId::Command_SetCursor)
                Context_.PendingPrint.clear();
            return {};
        }
        BuiltinValue Result;
        if (!InvokeFunction(Context_, Invocation.Symbol, Arguments, Result, Error))
            return Failed(std::move(Error));
        std::optional<vm::Value> Converted = FromLegacy(Result, Invocation.ResultType, Error);
        if (!Converted)
            return Failed(std::move(Error));
        return {vm::BuiltinState::Completed, std::move(*Converted), {}};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeSync(const vm::BuiltinInvocation& Invocation)
    {
        auto Iterator = Pending_.find(Invocation.Id);
        if (!Invocation.IsRetry)
        {
            const std::optional<std::chrono::steady_clock::time_point> Deadline =
                Context_.Kernel->Core().NextSyncDeadline();
            if (Deadline && std::chrono::steady_clock::now() < *Deadline)
            {
                Pending_.emplace(Invocation.Id, Pending{Phase::SyncDeadline, *Deadline, {}});
                return Suspend(vm::SuspensionKind::Deadline, *Deadline);
            }
        }
        else
        {
            if (Iterator == Pending_.end() || Iterator->second.CurrentPhase != Phase::SyncDeadline)
                return Failed("SYNC retry state is missing");
            // XR may have started while the host serviced this pending flat SYNC deadline.
            if (!Context_.Kernel->Vr().Pacing() && std::chrono::steady_clock::now() < Iterator->second.Deadline)
                return Suspend(vm::SuspensionKind::Deadline, Iterator->second.Deadline);
            Pending_.erase(Iterator);
        }

        Context_.Kernel->Core().Sync();
        return {vm::BuiltinState::CompletedAndSuspend, {}, {}, {vm::SuspensionKind::FrameBoundary, {}}};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeWait(const vm::BuiltinInvocation& Invocation)
    {
        if (Invocation.Arguments.size() != 1)
            return Failed("WAIT requires one duration");
        auto Iterator = Pending_.find(Invocation.Id);
        if (Invocation.IsRetry && Iterator != Pending_.end() && Iterator->second.CurrentPhase == Phase::SyncDeadline)
        {
            return InvokeSync(Invocation);
        }
        if (!Invocation.IsRetry)
        {
            std::int64_t Milliseconds = 0;
            std::string Error;
            if (!Invocation.Arguments.front().AsInteger(Milliseconds, Error))
                return Failed(Error);
            if (Milliseconds <= 0)
            {
                if (Invocation.Symbol == language::SymbolId::Command_Sleep)
                    return {};
                return InvokeSync(Invocation);
            }
            const auto Now = std::chrono::steady_clock::now();
            const auto Available = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::time_point::max() - Now);
            if (Milliseconds > Available.count())
                return Failed("WAIT duration exceeds the clock range");
            const auto Deadline = Now + std::chrono::milliseconds(Milliseconds);
            Iterator = Pending_.emplace(Invocation.Id, Pending{Phase::Deadline, Deadline, {}}).first;
        }
        if (Iterator == Pending_.end())
            return Failed("WAIT retry state is missing");
        if (std::chrono::steady_clock::now() < Iterator->second.Deadline)
            return Suspend(vm::SuspensionKind::Deadline, Iterator->second.Deadline);
        Pending_.erase(Iterator);
        return {};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeWaitKey(const vm::BuiltinInvocation& Invocation)
    {
        if (!Invocation.IsRetry)
        {
            Context_.Kernel->Input().AnyKeyPressed() = false;
            Pending_.emplace(Invocation.Id, Pending{Phase::Key, {}, {}});
            return Suspend(vm::SuspensionKind::Key);
        }
        if (!Pending_.contains(Invocation.Id))
            return Failed("WAIT KEY retry state is missing");
        if (!Context_.Kernel->Input().AnyKeyPressed())
            return Suspend(vm::SuspensionKind::Key);
        Context_.Kernel->Input().AnyKeyPressed() = false;
        Pending_.erase(Invocation.Id);
        return {};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeWaitMouse(const vm::BuiltinInvocation& Invocation)
    {
        auto Iterator = Pending_.find(Invocation.Id);
        if (!Invocation.IsRetry)
        {
            Iterator = Pending_.emplace(Invocation.Id, Pending{Phase::MouseRelease, {}, {}}).first;
        }
        if (Iterator == Pending_.end())
            return Failed("SUSPEND FOR MOUSE retry state is missing");
        if (Iterator->second.CurrentPhase == Phase::MouseRelease)
        {
            if (Context_.Kernel->Input().Mouseclick() != 0)
                return Suspend(vm::SuspensionKind::Mouse);
            Iterator->second.CurrentPhase = Phase::MousePress;
        }
        if (Context_.Kernel->Input().Mouseclick() == 0)
            return Suspend(vm::SuspensionKind::Mouse);
        Pending_.erase(Iterator);
        return {};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeInput(const vm::BuiltinInvocation& Invocation)
    {
        auto Iterator = Pending_.find(Invocation.Id);
        if (!Invocation.IsRetry)
        {
            if (!Invocation.Arguments.empty())
            {
                std::string Error;
                const std::optional<BuiltinValue> Prompt = ToLegacy(Invocation.Arguments.front(), Error);
                if (!Prompt || !InvokeCommand(Context_, language::SymbolId::Command_Print, {*Prompt}, Error))
                    return Failed(Error.empty() ? "INPUT prompt failed" : std::move(Error));
            }
            if (!Context_.Kernel->Input().BeginTextInput())
                return Failed("INPUT could not start text input");
            Iterator = Pending_.emplace(Invocation.Id, Pending{Phase::TextInput, {}, {}}).first;
            return Suspend(vm::SuspensionKind::TextInput);
        }
        if (Iterator == Pending_.end())
            return Failed("INPUT retry state is missing");
        Iterator->second.Text += Context_.Kernel->Input().TakeTextInput();
        if (!Context_.Kernel->Input().TextInputSubmitted())
            return Suspend(vm::SuspensionKind::TextInput);
        std::string StopError;
        if (!Context_.Kernel->Input().EndTextInput(&StopError))
        {
            Pending_.erase(Iterator);
            return Failed(StopError);
        }
        std::string Text = std::move(Iterator->second.Text);
        Pending_.erase(Iterator);
        if (Invocation.ResultType.Kind == language::TypeKind::String)
            return {vm::BuiltinState::Completed, vm::Value::String(std::move(Text)), {}};
        double Number = 0.0;
        std::string Error;
        if (!language::ParseNumber(Text, Number, &Error))
            return Failed("INPUT requires a finite numeric value");
        if (Invocation.ResultType.Kind == language::TypeKind::Real)
            return {vm::BuiltinState::Completed, vm::Value::Real(Number), {}};
        std::int64_t Integer = 0;
        if (!language::ConvertInteger(Number, Integer, &Error))
            return Failed("INPUT " + Error);
        return {vm::BuiltinState::Completed, vm::Value::Integer(Integer), {}};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeFileRead(const vm::BuiltinInvocation& Invocation)
    {
        if (Invocation.Arguments.size() != 1)
            return Failed("File read command requires one file number");
        int FileNumber = 0;
        std::string Error;
        if (!EngineInteger(Invocation.Arguments.front(), FileNumber, Error))
            return Failed(Error);
        FileIOSystem& Files = Context_.Kernel->FileIO();
        int Open = 0;
        int End = 0;
        if (!Files.FileIsOpen(Open, FileNumber, &Error))
            return Failed(Error);
        if (!Open)
            return Failed("File read command cannot read from file " + std::to_string(FileNumber));
        if (!Files.FileEnd(End, FileNumber, &Error))
            return Failed(Error);
        if (End)
            return Failed("File read command cannot read from file " + std::to_string(FileNumber));
        int Integer = 0;
        float Real = 0;
        std::string Text;
        switch (Invocation.Symbol)
        {
            case language::SymbolId::Command_ReadByte:
                if (!Files.FileReadByte(Integer, FileNumber, &Error))
                    return Failed(Error);
                break;
            case language::SymbolId::Command_ReadWord:
                if (!Files.FileReadWord(Integer, FileNumber, &Error))
                    return Failed(Error);
                break;
            case language::SymbolId::Command_ReadLong:
            case language::SymbolId::Command_ReadFile:
                if (!Files.FileReadLong(Integer, FileNumber, &Error))
                    return Failed(Error);
                break;
            case language::SymbolId::Command_ReadFloat:
                if (!Files.FileReadFloat(Real, FileNumber, &Error))
                    return Failed(Error);
                return {vm::BuiltinState::Completed, vm::Value::Real(Real), {}};
            case language::SymbolId::Command_ReadString:
                if (Invocation.ResultType.Kind != language::TypeKind::String)
                    return Failed("READ STRING requires a string destination");
                if (!Files.FileReadString(Text, FileNumber, &Error))
                    return Failed(Error);
                return {vm::BuiltinState::Completed, vm::Value::String(std::move(Text)), {}};
            default:
                return Failed("Unknown file read builtin");
        }
        return {vm::BuiltinState::Completed, vm::Value::Integer(Integer), {}};
    }

    vm::BuiltinResult EngineBuiltinHost::InvokeArray(const vm::BuiltinInvocation& Invocation)
    {
        if (Invocation.Arguments.size() != 1 || !Invocation.ArrayArgument)
            return Failed("Whole-array builtin requires a path and array");
        const auto* Array = std::get_if<std::shared_ptr<vm::ArrayValue>>(&Invocation.ArrayArgument->Data());
        if (!Array || !*Array)
            return Failed("Whole-array builtin received an undimensioned array");
        if ((*Array)->ElementType.Kind != language::TypeKind::Boolean &&
            (*Array)->ElementType.Kind != language::TypeKind::Integer &&
            (*Array)->ElementType.Kind != language::TypeKind::Real)
            return Failed("LOAD ARRAY and SAVE ARRAY support numeric arrays only");
        const std::filesystem::path Path =
            ResolveBasicPath(Context_.AssetRoot, Invocation.Arguments.front().AsString());
        if (Invocation.ReplacesArray)
        {
            std::ifstream Input(Path, std::ios::binary);
            if (!Input)
                return Failed("LOAD ARRAY cannot open '" + Path.string() + "'");
            // The original only recommends that the saved and loaded arrays match: the official swarm
            // demo ships a hiscore file shorter than its array, so a short file fills what it holds.
            vm::Value Replacement = Invocation.ArrayArgument->Clone();
            auto& Target = **std::get_if<std::shared_ptr<vm::ArrayValue>>(&Replacement.Data());
            for (vm::Value& Element : Target.Elements)
            {
                std::int32_t Number = 0;
                Input.read(reinterpret_cast<char*>(&Number), sizeof(Number));
                if (Input.gcount() == 0 && Input.eof())
                    break;
                if (Input.gcount() != static_cast<std::streamsize>(sizeof(Number)))
                    return Failed("LOAD ARRAY found a truncated record in '" + Path.string() + "'");
                if (Input.bad())
                    return Failed("LOAD ARRAY failed while reading '" + Path.string() + "'");
                Element = Target.ElementType.Kind == language::TypeKind::Real
                              ? vm::Value::Real(static_cast<double>(Number))
                              : vm::Value::Integer(Number);
            }
            return {vm::BuiltinState::Completed, std::move(Replacement), {}};
        }

        std::string Bytes;
        if ((*Array)->Elements.size() > Bytes.max_size() / sizeof(std::int32_t))
            return Failed("SAVE ARRAY output is too large");
        Bytes.reserve((*Array)->Elements.size() * sizeof(std::int32_t));
        for (const vm::Value& Element : (*Array)->Elements)
        {
            std::int64_t Wide = 0;
            std::string ConversionError;
            if (!Element.AsInteger(Wide, ConversionError) || Wide < std::numeric_limits<std::int32_t>::min() ||
                Wide > std::numeric_limits<std::int32_t>::max())
                return Failed("SAVE ARRAY element is outside the Classic 32-bit file format");
            const std::int32_t Number = static_cast<std::int32_t>(Wide);
            Bytes.append(reinterpret_cast<const char*>(&Number), sizeof(Number));
        }
        std::string Error;
        if (!AtomicWriteText(Path, Bytes, &Error))
            return Failed("SAVE ARRAY " + Error);
        return {};
    }
} // namespace dbc::interpreter::detail
