#include "execution_service.hpp"
#include "path_utf8.hpp"

#include "basic_path.hpp"
#include "engine/systems/audio_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/vr_system.hpp"
#include "engine_builtin_context.hpp"
#include "engine_builtin_host.hpp"
#include "execution_diagnostics.hpp"
#include "vm/compiler.hpp"
#include "vm/execution_session.hpp"
#include "vm/verifier.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#include <SDL3/SDL.h>

namespace dbc::interpreter
{
    namespace
    {
        struct FileCloser
        {
            void operator()(std::FILE* Value) const
            {
                if (Value)
                    std::fclose(Value);
            }
        };

        using LogFile = std::unique_ptr<std::FILE, FileCloser>;

        std::filesystem::path NormalizePath(const std::filesystem::path& Value)
        {
            return NormalizeBasicPath(Value);
        }

        std::optional<std::vector<std::string>> SplitCommandLine(std::string_view Value, std::string& Error)
        {
            std::vector<std::string> Result;
            std::string Current;
            char Quote = '\0';
            for (std::size_t Index = 0; Index < Value.size(); ++Index)
            {
                const char Character = Value[Index];
                if (Character == '\\' && Quote != '\'' && Index + 1 < Value.size() && Value[Index + 1] == '"')
                {
                    Current.push_back('"');
                    ++Index;
                    continue;
                }
                if (Quote != '\0')
                {
                    if (Character == Quote)
                        Quote = '\0';
                    else
                        Current.push_back(Character);
                    continue;
                }
                if (Character == '\'' || Character == '"')
                {
                    Quote = Character;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(Character)))
                {
                    if (!Current.empty())
                    {
                        Result.push_back(std::move(Current));
                        Current.clear();
                    }
                    continue;
                }
                Current.push_back(Character);
            }
            if (Quote != '\0')
            {
                Error = "EXECUTE FILE command line contains an unterminated quote";
                return std::nullopt;
            }
            if (!Current.empty())
                Result.push_back(std::move(Current));
            return Result;
        }

        bool IsExecutableFile(const std::filesystem::path& Path)
        {
#if defined(_WIN32)
            std::string Extension = dbc::PathToUtf8(Path.extension());
            std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                           [](unsigned char Value) { return static_cast<char>(std::tolower(Value)); });
            return Extension == ".exe" || Extension == ".com" || Extension == ".bat" || Extension == ".cmd";
#else
            std::error_code Error;
            const std::filesystem::perms Permissions = std::filesystem::status(Path, Error).permissions();
            if (Error)
                return false;
            constexpr std::filesystem::perms Execute = std::filesystem::perms::owner_exec |
                                                       std::filesystem::perms::group_exec |
                                                       std::filesystem::perms::others_exec;
            return (Permissions & Execute) != std::filesystem::perms::none;
#endif
        }

        std::string FileUrl(const std::filesystem::path& Path)
        {
            static constexpr char Hex[] = "0123456789ABCDEF";
            const std::string Source = dbc::GenericPathToUtf8(Path);
            std::string Result = "file://";
            Result.reserve(Result.size() + Source.size());
            for (const unsigned char Character : Source)
            {
                if (std::isalnum(Character) || Character == '/' || Character == '-' || Character == '_' ||
                    Character == '.' || Character == '~' || Character == ':')
                {
                    Result.push_back(static_cast<char>(Character));
                }
                else
                {
                    Result.push_back('%');
                    Result.push_back(Hex[Character >> 4]);
                    Result.push_back(Hex[Character & 0x0f]);
                }
            }
            return Result;
        }

        bool LaunchFileWithSdl(const HostFileLaunchRequest& Request, std::string& Error)
        {
            std::error_code PathError;
            if (!std::filesystem::exists(Request.FileName, PathError) || PathError)
            {
                Error = "EXECUTE FILE cannot find '" + dbc::PathToUtf8(Request.FileName) + "'";
                return false;
            }
            if (!IsExecutableFile(Request.FileName))
            {
                if (!SDL_OpenURL(FileUrl(Request.FileName).c_str()))
                {
                    Error = "EXECUTE FILE cannot open '" + dbc::PathToUtf8(Request.FileName) + "': " + SDL_GetError();
                    return false;
                }
                return true;
            }

            std::optional<std::vector<std::string>> Parsed = SplitCommandLine(Request.CommandLine, Error);
            if (!Parsed)
                return false;
            std::vector<std::string> OwnedArguments;
            OwnedArguments.reserve(Parsed->size() + 1);
            OwnedArguments.push_back(dbc::PathToUtf8(Request.FileName));
            OwnedArguments.insert(OwnedArguments.end(), std::make_move_iterator(Parsed->begin()),
                                  std::make_move_iterator(Parsed->end()));
            std::vector<const char*> Arguments;
            Arguments.reserve(OwnedArguments.size() + 1);
            for (const std::string& Argument : OwnedArguments)
                Arguments.push_back(Argument.c_str());
            Arguments.push_back(nullptr);

            const SDL_PropertiesID Properties = SDL_CreateProperties();
            if (Properties == 0)
            {
                Error = "EXECUTE FILE cannot create process properties: " + std::string(SDL_GetError());
                return false;
            }
            SDL_SetPointerProperty(Properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, Arguments.data());
            if (!Request.WorkingDirectory.empty())
            {
                SDL_SetStringProperty(Properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                                      dbc::PathToUtf8(Request.WorkingDirectory).c_str());
            }
            SDL_SetBooleanProperty(Properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, !Request.WaitForTermination);
            SDL_Process* Process = SDL_CreateProcessWithProperties(Properties);
            SDL_DestroyProperties(Properties);
            if (!Process)
            {
                Error = "EXECUTE FILE cannot launch '" + dbc::PathToUtf8(Request.FileName) + "': " + SDL_GetError();
                return false;
            }
            if (Request.WaitForTermination)
            {
                int ExitCode = 0;
                if (!SDL_WaitProcess(Process, true, &ExitCode))
                {
                    Error =
                        "EXECUTE FILE cannot wait for '" + dbc::PathToUtf8(Request.FileName) + "': " + SDL_GetError();
                    SDL_DestroyProcess(Process);
                    return false;
                }
            }
            SDL_DestroyProcess(Process);
            return true;
        }

        void PresentExitPromptWithSdl(std::string_view Title, std::string_view Description)
        {
            const std::string StableTitle(Title);
            const std::string StableDescription(Description);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, StableTitle.c_str(), StableDescription.c_str(),
                                     nullptr);
        }

        ExecutionState MapState(vm::SessionState Value)
        {
            switch (Value)
            {
                case vm::SessionState::Empty:
                    return ExecutionState::Empty;
                case vm::SessionState::Ready:
                    return ExecutionState::Ready;
                case vm::SessionState::Running:
                    return ExecutionState::Running;
                case vm::SessionState::Suspended:
                    return ExecutionState::Suspended;
                case vm::SessionState::Completed:
                    return ExecutionState::Completed;
                case vm::SessionState::Failed:
                    return ExecutionState::Failed;
                case vm::SessionState::Cancelled:
                    return ExecutionState::Cancelled;
            }
            return ExecutionState::Failed;
        }

        ExecutionSuspension MapSuspension(const vm::Suspension& Value)
        {
            SuspensionReason Reason = SuspensionReason::None;
            switch (Value.Kind)
            {
                case vm::SuspensionKind::None:
                    Reason = SuspensionReason::None;
                    break;
                case vm::SuspensionKind::NextTick:
                    Reason = SuspensionReason::NextTick;
                    break;
                case vm::SuspensionKind::FrameBoundary:
                    Reason = SuspensionReason::FrameBoundary;
                    break;
                case vm::SuspensionKind::Deadline:
                    Reason = SuspensionReason::Deadline;
                    break;
                case vm::SuspensionKind::Key:
                    Reason = SuspensionReason::Key;
                    break;
                case vm::SuspensionKind::Mouse:
                    Reason = SuspensionReason::Mouse;
                    break;
                case vm::SuspensionKind::TextInput:
                    Reason = SuspensionReason::TextInput;
                    break;
                case vm::SuspensionKind::Io:
                    Reason = SuspensionReason::Io;
                    break;
            }
            return {Reason, Value.Deadline};
        }

        ExecutionDiagnostic RuntimeDiagnostic(const vm::SessionFailure& Failure)
        {
            ExecutionDiagnostic Result{DiagnosticStage::Runtime, language::DiagnosticSeverity::Error,
                                       Failure.Code.empty() ? std::string("DBC5000") : Failure.Code, Failure.Message};
            Result.LogicalPath = Failure.Span.LogicalPath;
            Result.Begin = Failure.Span.Begin;
            Result.End = Failure.Span.End;
            return Result;
        }
    } // namespace

    struct ExecutionService::Impl
    {
        explicit Impl(EngineKernel& Value) : Kernel(Value) {}

        EngineKernel& Kernel;
        ExecutionRequest Request{};
        detail::BuiltinContext Context{};
        std::unique_ptr<detail::EngineBuiltinHost> Host{};
        vm::ExecutionSession Session{};
        ExecutionResult Result{};
        LogFile Log{};
        FrameMeter Frames{};
        std::atomic_bool StopRequested{false};

        void PresentExitPrompt()
        {
            if (Context.ExitPromptPresented || !Context.PendingExitPrompt || Context.MaxFrames > 0 ||
                !Context.PresentExitPrompt)
                return;
            Context.ExitPromptPresented = true;
            Context.PresentExitPrompt(Context.PendingExitPrompt->first, Context.PendingExitPrompt->second);
        }

        void FinishBenchmark()
        {
            if (Request.Options.Benchmark)
                Frames.Finish(Result.Benchmark);
        }

        void SyncBuiltinCoverage()
        {
            Result.BuiltinCoverage.clear();
            const std::span<const vm::BuiltinCoverageEntry> Values = Session.Coverage();
            Result.BuiltinCoverage.reserve(Values.size());
            for (const vm::BuiltinCoverageEntry& Value : Values)
            {
                Result.BuiltinCoverage.push_back(
                    {Value.Symbol, Value.Overload, Value.Attempts, Value.Completed, Value.Failed});
            }
        }

        void AccountPresentedFrame()
        {
            ++Result.FrameCount;
            if (Request.Options.Benchmark)
                Frames.RecordFrame();
            ++Context.SyncCount;
            if (Context.MaxFrames > 0 && Context.SyncCount >= Context.MaxFrames)
                Context.ShouldExit = true;
        }
    };

    namespace
    {
        bool UsesVr(const vm::Module& Bytecode)
        {
            for (const vm::Function& Function : Bytecode.Functions)
                for (const vm::Instruction& Instruction : Function.Code)
                {
                    if (Instruction.Op != vm::OpCode::CallBuiltin && Instruction.Op != vm::OpCode::CallBuiltinArray)
                        continue;
                    switch (static_cast<language::SymbolId>(Instruction.A))
                    {
                        case language::SymbolId::Command_SetVrEnabled:
                        case language::SymbolId::Command_SetTrackingSpace:
                        case language::SymbolId::Command_RecenterVrCamera:
                        case language::SymbolId::Command_GetVrControllerPosition:
                        case language::SymbolId::Command_GetVrControllerRotation:
                        case language::SymbolId::Command_GetVrHeadPosition:
                        case language::SymbolId::Command_GetVrRotation:
                        case language::SymbolId::Function_IsVrControllerTracked:
                        case language::SymbolId::Function_IsVrHeadTracked:
                        case language::SymbolId::Function_IsVrSupported:
                        case language::SymbolId::Function_IsVrEnabled:
                        case language::SymbolId::Command_SetVrPreviewMode:
                        case language::SymbolId::Command_SetVrCameraHeight:
                        case language::SymbolId::Function_GetVrCameraHeight:
                            return true;
                        default:
                            break;
                    }
                }
            return false;
        }
    } // namespace

    bool ExecutionResult::Success() const
    {
        return State == ExecutionState::Completed;
    }

    bool ExecutionResult::Finished() const
    {
        return State == ExecutionState::Completed || State == ExecutionState::Failed ||
               State == ExecutionState::Cancelled;
    }

    bool SourceAnalysis::Success() const
    {
        return std::none_of(Diagnostics.begin(), Diagnostics.end(), [](const ExecutionDiagnostic& Value)
                            { return Value.Severity == language::DiagnosticSeverity::Error; });
    }

    ExecutionService::ExecutionService(EngineKernel& Kernel) : Impl_(std::make_unique<Impl>(Kernel)) {}

    ExecutionService::~ExecutionService() = default;

    bool ExecutionService::PrepareBytecode(ExecutionRequest Request)
    {
        const auto Started = std::chrono::steady_clock::now();
        if (!Request.Bytecode)
            return Start(std::move(Request),
                         {detail::InputDiagnostic("DBC1007", "No bytecode to prepare", Request.RootLogicalPath)},
                         Started);
        return Start(std::move(Request), {}, Started);
    }

    bool ExecutionService::Start(ExecutionRequest Request, std::vector<ExecutionDiagnostic> Diagnostics,
                                 std::chrono::steady_clock::time_point Started)
    {
        Impl_->Session.RequestStop();
        Impl_->Session.Tick(1);
        Impl_->Request = std::move(Request);
        Impl_->Kernel.ConfigureRuntimeCompatibility(Impl_->Request.Profile == language::LanguageProfile::Dbc
                                                        ? RuntimeCompatibilityPolicy::Classic()
                                                        : RuntimeCompatibilityPolicy::Professional());
        Impl_->Result = {};
        Impl_->Log.reset();
        Impl_->Host.reset();
        Impl_->Context = {};
        Impl_->Frames.Start(Started);
        Impl_->StopRequested.store(false, std::memory_order_release);

        // Bytecode is verified here whatever its origin, so no caller can bypass the verifier.
        std::shared_ptr<const vm::Module> Bytecode = std::move(Impl_->Request.Bytecode);
        Impl_->Result.Diagnostics = std::move(Diagnostics);
        if (Bytecode && !detail::HasErrors(Impl_->Result.Diagnostics))
        {
            std::vector<ExecutionDiagnostic> Verification = VerifyBytecode(*Bytecode);
            Impl_->Result.Diagnostics.insert(Impl_->Result.Diagnostics.end(),
                                             std::make_move_iterator(Verification.begin()),
                                             std::make_move_iterator(Verification.end()));
        }
        if (!Bytecode || detail::HasErrors(Impl_->Result.Diagnostics))
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->FinishBenchmark();
            return false;
        }

        const bool RequiresVr = UsesVr(*Bytecode);
        std::string CapabilityError;
        if (!Impl_->Kernel.Vr().Configure(RequiresVr, &CapabilityError))
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->Result.Diagnostics.push_back(
                detail::InputDiagnostic("DBC5000", CapabilityError, Impl_->Request.RootLogicalPath));
            Impl_->FinishBenchmark();
            return false;
        }

        if (!Impl_->Request.Options.LogOutputPath.empty())
        {
// Binary mode: Windows text mode would turn each '\n' into "\r\n" and break the golden outputs.
#if defined(_WIN32)
            Impl_->Log.reset(_wfopen(Impl_->Request.Options.LogOutputPath.c_str(), L"wb"));
#else
            Impl_->Log.reset(std::fopen(Impl_->Request.Options.LogOutputPath.c_str(), "wb"));
#endif
            if (!Impl_->Log)
            {
                Impl_->Result.State = ExecutionState::Failed;
                Impl_->Result.Diagnostics.push_back(detail::InputDiagnostic(
                    "DBC1004",
                    "Failed to open log output '" + dbc::PathToUtf8(Impl_->Request.Options.LogOutputPath) + "'",
                    Impl_->Request.Options.LogOutputPath));
                Impl_->FinishBenchmark();
                return false;
            }
        }

        Impl_->Context.Kernel = &Impl_->Kernel;
        Impl_->Context.AssetRoot = Impl_->Request.Options.AssetRoot;
        if (Impl_->Context.AssetRoot.empty())
            Impl_->Context.AssetRoot = NormalizePath(Impl_->Request.RootLogicalPath).parent_path();
        Impl_->Context.Benchmark = Impl_->Request.Options.Benchmark;
        Impl_->Context.MaxFrames = Impl_->Request.Options.MaxFrames;
        Impl_->Context.Profile = Impl_->Request.Profile;
        Impl_->Context.CommandLine = Impl_->Request.Options.CommandLine;
        Impl_->Context.LogFile = Impl_->Log.get();
        Impl_->Context.Output = Impl_->Request.Options.Output;
        Impl_->Context.LaunchFile =
            Impl_->Request.Options.LaunchFile ? Impl_->Request.Options.LaunchFile : HostFileLauncher(LaunchFileWithSdl);
        Impl_->Context.PresentExitPrompt = Impl_->Request.Options.PresentExitPrompt
                                               ? Impl_->Request.Options.PresentExitPrompt
                                               : ExitPromptPresenter(PresentExitPromptWithSdl);
        Impl_->Host = std::make_unique<detail::EngineBuiltinHost>(Impl_->Context);
        if (!Impl_->Session.Prepare(std::move(Bytecode), *Impl_->Host,
                                    !Impl_->Request.Options.BuiltinCoverageOutputPath.empty()))
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->Result.Diagnostics.push_back(RuntimeDiagnostic(Impl_->Session.Failure()));
            Impl_->FinishBenchmark();
            return false;
        }

        Impl_->Result.State = ExecutionState::Ready;
        Impl_->Result.Benchmark.PrepareMilliseconds = Impl_->Frames.MillisecondsSinceStart();
        Impl_->Frames.RestartFrame();
        return true;
    }

    void ExecutionService::NotifyPresentedFrame()
    {
        if (Impl_->Result.Finished())
            return;
        Impl_->AccountPresentedFrame();
    }

    void ExecutionService::StartFrameMeasurements()
    {
        Impl_->Frames.RestartFrame();
    }

    ExecutionTick ExecutionService::Tick(std::size_t InstructionBudget)
    {
        Impl_->Kernel.Audio().PumpRecording();
        if (Impl_->Result.Finished())
            return {Impl_->Result.State, 0, {}};
        if (Impl_->Context.ShouldExit)
        {
            Impl_->Session.RequestStop();
            Impl_->Session.Tick(1);
            Impl_->SyncBuiltinCoverage();
            Impl_->Result.State = ExecutionState::Completed;
            Impl_->PresentExitPrompt();
            Impl_->FinishBenchmark();
            return {ExecutionState::Completed, 0, {}};
        }
        const std::size_t Budget =
            InstructionBudget == 0 ? Impl_->Request.Options.InstructionBudget : InstructionBudget;
        if (Budget == 0)
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->Result.Diagnostics.push_back(
                detail::InputDiagnostic("DBC1005", "Instruction budget must be greater than zero"));
            Impl_->FinishBenchmark();
            return {Impl_->Result.State, 0, {}};
        }

        if (Impl_->Kernel.Core().Suspended() && !Impl_->Kernel.Vr().Enabled() &&
            !Impl_->StopRequested.load(std::memory_order_acquire))
        {
            Impl_->Result.State = ExecutionState::Suspended;
            return {ExecutionState::Suspended, 0, {SuspensionReason::NextTick, {}}};
        }

        const vm::TickResult Tick = Impl_->Session.Tick(Budget);
        Impl_->SyncBuiltinCoverage();
        Impl_->Kernel.Audio().PumpRecording();
        ExecutionTick Result{MapState(Tick.State), Tick.Instructions, MapSuspension(Tick.Wait)};
        if (Tick.State == vm::SessionState::Suspended && Tick.Wait.Kind == vm::SuspensionKind::FrameBoundary)
        {
            Impl_->AccountPresentedFrame();
        }
        if (Impl_->Context.ShouldExit)
        {
            Impl_->Session.RequestStop();
            Impl_->Session.Tick(1);
            Impl_->SyncBuiltinCoverage();
            Result.State = ExecutionState::Completed;
            Result.Suspension = {};
        }

        Impl_->Result.State = Result.State;
        if (Result.State == ExecutionState::Failed)
        {
            Impl_->Result.Diagnostics.push_back(RuntimeDiagnostic(Impl_->Session.Failure()));
        }
        if (Result.State == ExecutionState::Completed)
            Impl_->PresentExitPrompt();
        if (Impl_->Result.Finished())
            Impl_->FinishBenchmark();
        return Result;
    }

    void ExecutionService::RequestStop()
    {
        Impl_->StopRequested.store(true, std::memory_order_release);
        Impl_->Session.RequestStop();
    }

    ExecutionState ExecutionService::State() const
    {
        return Impl_->Result.State;
    }

    const ExecutionResult& ExecutionService::Result() const
    {
        return Impl_->Result;
    }

    std::vector<ExecutionDiagnostic> VerifyBytecode(const vm::Module& Bytecode)
    {
        return detail::ConvertDiagnostics(vm::Verify(Bytecode).Diagnostics, DiagnosticStage::Verifier);
    }
} // namespace dbc::interpreter
