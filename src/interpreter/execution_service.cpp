#include "execution_service.hpp"

#include "basic_path.hpp"
#include "engine/systems/audio_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/vr_system.hpp"
#include "engine_builtin_context.hpp"
#include "engine_builtin_host.hpp"
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

        std::string PathKey(const std::filesystem::path& Value)
        {
            return NormalizePath(Value).generic_string();
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
            std::string Extension = Path.extension().string();
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
            const std::string Source = Path.generic_string();
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
                Error = "EXECUTE FILE cannot find '" + Request.FileName.string() + "'";
                return false;
            }
            if (!IsExecutableFile(Request.FileName))
            {
                if (!SDL_OpenURL(FileUrl(Request.FileName).c_str()))
                {
                    Error = "EXECUTE FILE cannot open '" + Request.FileName.string() + "': " + SDL_GetError();
                    return false;
                }
                return true;
            }

            std::optional<std::vector<std::string>> Parsed = SplitCommandLine(Request.CommandLine, Error);
            if (!Parsed)
                return false;
            std::vector<std::string> OwnedArguments;
            OwnedArguments.reserve(Parsed->size() + 1);
            OwnedArguments.push_back(Request.FileName.string());
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
                                      Request.WorkingDirectory.string().c_str());
            }
            SDL_SetBooleanProperty(Properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, !Request.WaitForTermination);
            SDL_Process* Process = SDL_CreateProcessWithProperties(Properties);
            SDL_DestroyProperties(Properties);
            if (!Process)
            {
                Error = "EXECUTE FILE cannot launch '" + Request.FileName.string() + "': " + SDL_GetError();
                return false;
            }
            if (Request.WaitForTermination)
            {
                int ExitCode = 0;
                if (!SDL_WaitProcess(Process, true, &ExitCode))
                {
                    Error = "EXECUTE FILE cannot wait for '" + Request.FileName.string() + "': " + SDL_GetError();
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

        ExecutionDiagnostic InputDiagnostic(std::string Code, std::string Message,
                                            const std::filesystem::path& Path = {})
        {
            return {DiagnosticStage::Input, language::DiagnosticSeverity::Error, std::move(Code), std::move(Message),
                    Path};
        }

        ExecutionDiagnostic ConvertDiagnostic(const language::Diagnostic& Value, DiagnosticStage Stage)
        {
            const std::filesystem::path& Path = Value.LogicalPath.empty() ? Value.Span.LogicalPath : Value.LogicalPath;
            return {Stage, Value.Severity, Value.Code, Value.Message, Path, Value.Span.Begin, Value.Span.End};
        }

        std::vector<ExecutionDiagnostic> ConvertDiagnostics(const std::vector<language::Diagnostic>& Values,
                                                            DiagnosticStage Stage)
        {
            std::vector<ExecutionDiagnostic> Result;
            Result.reserve(Values.size());
            for (const language::Diagnostic& Value : Values)
                Result.push_back(ConvertDiagnostic(Value, Stage));
            return Result;
        }

        bool ValidateLogicalProfile(const std::filesystem::path& Path, language::LanguageProfile Profile,
                                    std::string& Error)
        {
            if (Path.empty())
            {
                Error = "Source logical path cannot be empty";
                return false;
            }
            if (Path.extension().empty())
                return true;
            if (!language::IsLanguageSource(Path))
            {
                Error = "BASIC source must use the .dba extension: " + Path.string();
                return false;
            }
            (void)Profile;
            return true;
        }

        std::size_t LineCount(std::string_view Text)
        {
            if (Text.empty())
                return 0;
            return static_cast<std::size_t>(std::count(Text.begin(), Text.end(), '\n')) +
                   static_cast<std::size_t>(Text.back() != '\n');
        }

        using BuiltinUseKey = std::pair<language::SymbolId, language::OverloadId>;

        void CollectExpressionBuiltins(const language::Expression& Expression, std::set<std::string>& Names,
                                       std::set<BuiltinUseKey>& Uses)
        {
            if (Expression.Builtin)
                Names.emplace(language::Symbol(*Expression.Builtin).CanonicalName);
            if (Expression.Builtin && Expression.Overload)
                Uses.emplace(*Expression.Builtin, *Expression.Overload);
            for (const std::unique_ptr<language::Expression>& Child : Expression.Children)
                CollectExpressionBuiltins(*Child, Names, Uses);
        }

        void CollectStatementBuiltins(const language::Statement& Statement, std::set<std::string>& Names,
                                      std::set<BuiltinUseKey>& Uses)
        {
            if (Statement.Builtin)
                Names.emplace(language::Symbol(*Statement.Builtin).CanonicalName);
            if (Statement.Builtin && Statement.Overload)
                Uses.emplace(*Statement.Builtin, *Statement.Overload);
            for (const std::unique_ptr<language::Expression>& Expression : Statement.Expressions)
                CollectExpressionBuiltins(*Expression, Names, Uses);
            for (const language::Statement& Child : Statement.Body)
                CollectStatementBuiltins(Child, Names, Uses);
            for (const language::Statement& Child : Statement.ElseBody)
                CollectStatementBuiltins(Child, Names, Uses);
            for (const language::CaseClause& Clause : Statement.Cases)
            {
                for (const std::unique_ptr<language::Expression>& Value : Clause.Values)
                    CollectExpressionBuiltins(*Value, Names, Uses);
                for (const language::Statement& Child : Clause.Body)
                    CollectStatementBuiltins(Child, Names, Uses);
            }
        }

        struct FrontendResult
        {
            language::Program Program{};
            SourceInspection Inspection{};
            std::vector<ExecutionDiagnostic> Diagnostics{};
        };

        FrontendResult CompileFrontend(ExecutionRequest& Request)
        {
            FrontendResult Result;
            Result.Inspection.RootLogicalPath = NormalizePath(Request.RootLogicalPath);
            Result.Inspection.Profile = Request.Profile;
            Result.Inspection.InputKind = Request.InputKind;
            Result.Inspection.DialectSource = Request.DialectSource;
            Result.Inspection.ProjectFile = Request.ProjectFile;
            Result.Inspection.OrderedSources =
                Request.OrderedProjectSources.empty()
                    ? std::vector<std::filesystem::path>{Result.Inspection.RootLogicalPath}
                    : Request.OrderedProjectSources;

            std::string Error;
            if (!ValidateLogicalProfile(Result.Inspection.RootLogicalPath, Request.Profile, Error))
            {
                Result.Diagnostics.push_back(
                    InputDiagnostic("DBC1000", std::move(Error), Result.Inspection.RootLogicalPath));
                return Result;
            }

            std::unordered_map<std::string, SourceUnit> Sources;
            Sources.reserve(Request.Sources.size());
            for (SourceUnit& Unit : Request.Sources)
            {
                Unit.LogicalPath = NormalizePath(Unit.LogicalPath);
                if (!ValidateLogicalProfile(Unit.LogicalPath, Request.Profile, Error))
                {
                    Result.Diagnostics.push_back(InputDiagnostic("DBC1001", std::move(Error), Unit.LogicalPath));
                    return Result;
                }
                const std::string Key = PathKey(Unit.LogicalPath);
                if (!Sources.emplace(Key, std::move(Unit)).second)
                {
                    Result.Diagnostics.push_back(
                        InputDiagnostic("DBC1002", "Duplicate source logical path '" + Key + "'", Key));
                    return Result;
                }
            }

            const std::string RootKey = PathKey(Result.Inspection.RootLogicalPath);
            const auto Root = Sources.find(RootKey);
            if (Root == Sources.end())
            {
                Result.Diagnostics.push_back(InputDiagnostic("DBC1003",
                                                             "Root source is not present in the in-memory source units",
                                                             Result.Inspection.RootLogicalPath));
                return Result;
            }

            std::set<std::string> Used{RootKey};
            std::string RootText = Root->second.Text;
            for (std::size_t Index = 1; Index < Request.OrderedProjectSources.size(); ++Index)
            {
                const std::filesystem::path UnitPath = NormalizePath(Request.OrderedProjectSources[Index]);
                if (UnitPath.generic_string().find('"') != std::string::npos)
                {
                    Result.Diagnostics.push_back(
                        InputDiagnostic("DBC1006", "Project source path cannot contain a quote", UnitPath));
                    return Result;
                }
                std::filesystem::path IncludePath =
                    UnitPath.lexically_relative(Result.Inspection.RootLogicalPath.parent_path());
                if (IncludePath.empty())
                    IncludePath = UnitPath;
                if (!RootText.empty() && RootText.back() != '\n')
                    RootText.push_back('\n');
                RootText += "#include \"" + IncludePath.generic_string() + "\"\n";
            }
            const auto RootDocument = std::make_shared<const language::SourceDocument>(
                language::SourceDocument{Root->second.LogicalPath, std::move(RootText), Request.Profile});
            const language::IncludeLoader IncludeLoader =
                [&Request, &Sources, &Used](const language::SourceDocument& Including,
                                            const std::filesystem::path& Include,
                                            std::string& LoadError) -> std::shared_ptr<const language::SourceDocument>
            {
                const std::filesystem::path NormalizedInclude = NormalizeBasicPath(Include);
                const bool IncludeIsAbsolute = NormalizedInclude.is_absolute();
                const std::filesystem::path LogicalPath = NormalizePath(
                    IncludeIsAbsolute ? NormalizedInclude : Including.LogicalPath.parent_path() / NormalizedInclude);
                if (Request.ConfinedSourceRoot)
                {
                    if (IncludeIsAbsolute)
                    {
                        LoadError = "#INCLUDE paths in an embedded artifact must be relative";
                        return {};
                    }
                    std::filesystem::path ConfinedPath;
                    if (!ResolveConfinedBasicPath(*Request.ConfinedSourceRoot, LogicalPath, ConfinedPath, &LoadError))
                    {
                        LoadError = "#INCLUDE in an embedded artifact must remain inside its payload: " + LoadError;
                        return {};
                    }
                }
                if (!ValidateLogicalProfile(LogicalPath, Request.Profile, LoadError))
                    return {};
                const std::string Key = PathKey(LogicalPath);
                auto Unit = Sources.find(Key);
                if (Unit == Sources.end() && Request.LoadMissingSource)
                {
                    std::optional<SourceUnit> Loaded =
                        Request.LoadMissingSource(LogicalPath, Request.Profile, LoadError);
                    if (!Loaded)
                    {
                        if (LoadError.empty())
                            LoadError = "Unable to load #INCLUDE '" + LogicalPath.string() + "'";
                        return {};
                    }
                    Loaded->LogicalPath = LogicalPath;
                    Unit = Sources.emplace(Key, std::move(*Loaded)).first;
                }
                if (Unit == Sources.end())
                {
                    LoadError = "#INCLUDE source is not present in memory: " + LogicalPath.string();
                    return {};
                }
                Used.insert(Key);
                return std::make_shared<const language::SourceDocument>(
                    language::SourceDocument{LogicalPath, Unit->second.Text, Request.Profile});
            };

            Result.Program = language::Compile(RootDocument, {.RejectUnsupported = true}, IncludeLoader);
            Result.Diagnostics = ConvertDiagnostics(Result.Program.Diagnostics, DiagnosticStage::Frontend);

            Result.Inspection.SourceCount = Used.size();
            for (const std::string& Key : Used)
            {
                const SourceUnit& Unit = Sources.at(Key);
                Result.Inspection.ByteCount += Unit.Text.size();
                Result.Inspection.LineCount += LineCount(Unit.Text);
            }
            if (!Result.Program.HasErrors())
            {
                std::set<std::string> Names;
                std::set<BuiltinUseKey> Uses;
                for (const language::Statement& Statement : Result.Program.Statements)
                    CollectStatementBuiltins(Statement, Names, Uses);
                for (const language::FunctionDeclaration& Function : Result.Program.Functions)
                {
                    for (const language::Statement& Statement : Function.Body)
                        CollectStatementBuiltins(Statement, Names, Uses);
                    if (Function.FinalReturn)
                        CollectExpressionBuiltins(*Function.FinalReturn, Names, Uses);
                }
                for (const auto& Constant : Result.Program.Constants)
                    if (Constant.Value)
                        CollectExpressionBuiltins(*Constant.Value, Names, Uses);
                Result.Inspection.Builtins.assign(Names.begin(), Names.end());
                for (const auto& [Symbol, Overload] : Uses)
                    Result.Inspection.BuiltinOverloads.push_back({Symbol, Overload});
            }
            return Result;
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
        std::chrono::steady_clock::time_point Started{};
        std::chrono::steady_clock::time_point FrameStarted{};
        double FrameSumMilliseconds = 0.0;
        double FrameMinimumMilliseconds = 1e18;
        double FrameMaximumMilliseconds = 0.0;
        std::vector<double> FrameSamples{};
        std::size_t FrameCount = 0;
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
            if (!Request.Options.Benchmark || Started == std::chrono::steady_clock::time_point{})
                return;
            Result.Benchmark.TotalMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
            Result.Benchmark.FrameCount = FrameCount;
            if (FrameCount > 0)
            {
                Result.Benchmark.AverageFrameMilliseconds = FrameSumMilliseconds / static_cast<double>(FrameCount);
                Result.Benchmark.MinimumFrameMilliseconds = FrameMinimumMilliseconds;
                Result.Benchmark.MaximumFrameMilliseconds = FrameMaximumMilliseconds;
                std::vector<double> Sorted = FrameSamples;
                std::sort(Sorted.begin(), Sorted.end());
                const auto Percentile = [&](double Fraction)
                {
                    const std::size_t Index = std::min(
                        Sorted.size() - 1,
                        static_cast<std::size_t>(std::ceil(Fraction * static_cast<double>(Sorted.size()))) - 1);
                    return Sorted[Index];
                };
                Result.Benchmark.P50FrameMilliseconds = Percentile(0.50);
                Result.Benchmark.P95FrameMilliseconds = Percentile(0.95);
                Result.Benchmark.P99FrameMilliseconds = Percentile(0.99);
            }
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

        void RecordFrame()
        {
            const auto Now = std::chrono::steady_clock::now();
            const double Milliseconds = std::chrono::duration<double, std::milli>(Now - FrameStarted).count();
            FrameSumMilliseconds += Milliseconds;
            FrameMinimumMilliseconds = std::min(FrameMinimumMilliseconds, Milliseconds);
            FrameMaximumMilliseconds = std::max(FrameMaximumMilliseconds, Milliseconds);
            FrameSamples.push_back(Milliseconds);
            ++FrameCount;
            FrameStarted = Now;
        }

        void AccountPresentedFrame()
        {
            ++Result.FrameCount;
            if (Request.Options.Benchmark)
                RecordFrame();
            ++Context.SyncCount;
            if (Context.MaxFrames > 0 && Context.SyncCount >= Context.MaxFrames)
                Context.ShouldExit = true;
        }
    };

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

    bool ExecutionService::Prepare(ExecutionRequest Request)
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
        Impl_->Started = std::chrono::steady_clock::now();
        Impl_->FrameStarted = Impl_->Started;
        Impl_->FrameSumMilliseconds = 0.0;
        Impl_->FrameMinimumMilliseconds = 1e18;
        Impl_->FrameMaximumMilliseconds = 0.0;
        Impl_->FrameSamples.clear();
        Impl_->FrameCount = 0;
        Impl_->StopRequested.store(false, std::memory_order_release);

        FrontendResult Frontend = CompileFrontend(Impl_->Request);
        const bool FrontendFailed =
            std::any_of(Frontend.Diagnostics.begin(), Frontend.Diagnostics.end(), [](const ExecutionDiagnostic& Value)
                        { return Value.Severity == language::DiagnosticSeverity::Error; });
        Impl_->Result.Diagnostics = std::move(Frontend.Diagnostics);
        if (FrontendFailed)
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->FinishBenchmark();
            return false;
        }

        vm::CompileResult Compiled = vm::Lower(Frontend.Program);
        if (!Compiled.Success())
        {
            Impl_->Result.State = ExecutionState::Failed;
            std::vector<ExecutionDiagnostic> Diagnostics =
                ConvertDiagnostics(Compiled.Diagnostics, DiagnosticStage::Compiler);
            Impl_->Result.Diagnostics.insert(Impl_->Result.Diagnostics.end(),
                                             std::make_move_iterator(Diagnostics.begin()),
                                             std::make_move_iterator(Diagnostics.end()));
            Impl_->FinishBenchmark();
            return false;
        }
        const vm::VerificationResult Verification = vm::Verify(*Compiled.Bytecode);
        if (!Verification.Success())
        {
            Impl_->Result.State = ExecutionState::Failed;
            std::vector<ExecutionDiagnostic> Diagnostics =
                ConvertDiagnostics(Verification.Diagnostics, DiagnosticStage::Verifier);
            Impl_->Result.Diagnostics.insert(Impl_->Result.Diagnostics.end(),
                                             std::make_move_iterator(Diagnostics.begin()),
                                             std::make_move_iterator(Diagnostics.end()));
            Impl_->FinishBenchmark();
            return false;
        }

        const bool RequiresVr =
            std::any_of(Frontend.Inspection.BuiltinOverloads.begin(), Frontend.Inspection.BuiltinOverloads.end(),
                        [](const SourceInspection::BuiltinUse& Use)
                        {
                            switch (Use.Symbol)
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
                                    return false;
                            }
                        });
        std::string CapabilityError;
        if (!Impl_->Kernel.Vr().Configure(RequiresVr, &CapabilityError))
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->Result.Diagnostics.push_back(
                InputDiagnostic("DBC5000", CapabilityError, Impl_->Request.RootLogicalPath));
            Impl_->FinishBenchmark();
            return false;
        }

        if (!Impl_->Request.Options.LogOutputPath.empty())
        {
            // Binary mode: Windows text mode would turn each '\n' into "\r\n" and break the golden outputs.
            Impl_->Log.reset(std::fopen(Impl_->Request.Options.LogOutputPath.string().c_str(), "wb"));
            if (!Impl_->Log)
            {
                Impl_->Result.State = ExecutionState::Failed;
                Impl_->Result.Diagnostics.push_back(InputDiagnostic(
                    "DBC1004", "Failed to open log output '" + Impl_->Request.Options.LogOutputPath.string() + "'",
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
        if (!Impl_->Session.Prepare(std::move(Compiled.Bytecode), *Impl_->Host,
                                    !Impl_->Request.Options.BuiltinCoverageOutputPath.empty()))
        {
            Impl_->Result.State = ExecutionState::Failed;
            Impl_->Result.Diagnostics.push_back(RuntimeDiagnostic(Impl_->Session.Failure()));
            Impl_->FinishBenchmark();
            return false;
        }

        Impl_->Result.State = ExecutionState::Ready;
        Impl_->Result.Benchmark.PrepareMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Impl_->Started).count();
        Impl_->FrameStarted = std::chrono::steady_clock::now();
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
        Impl_->FrameStarted = std::chrono::steady_clock::now();
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
                InputDiagnostic("DBC1005", "Instruction budget must be greater than zero"));
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

    std::optional<SourceUnit> LoadSourceUnitFromDisk(const std::filesystem::path& LogicalPath,
                                                     language::LanguageProfile Profile, std::string& Error)
    {
        const std::filesystem::path NormalizedPath = NormalizeBasicPath(LogicalPath);
        if (!language::IsLanguageSource(NormalizedPath))
        {
            Error = "BASIC source must use the .dba extension: " + NormalizedPath.string();
            return std::nullopt;
        }
        (void)Profile;
        std::size_t Size = 0;
        void* Bytes = SDL_LoadFile(NormalizedPath.string().c_str(), &Size);
        if (Bytes == nullptr)
        {
            Error = "Failed to read source file '" + NormalizedPath.string() + "'";
            return std::nullopt;
        }
        const std::string Text(static_cast<const char*>(Bytes), Size);
        SDL_free(Bytes);
        return SourceUnit{NormalizePath(NormalizedPath), Text};
    }

    SourceAnalysis AnalyzeSources(ExecutionRequest Request)
    {
        FrontendResult Frontend = CompileFrontend(Request);
        SourceAnalysis Result{std::move(Frontend.Inspection), std::move(Frontend.Diagnostics)};
        if (!Result.Success())
            return Result;

        vm::CompileResult Compiled = vm::Lower(Frontend.Program);
        if (!Compiled.Success())
        {
            std::vector<ExecutionDiagnostic> Diagnostics =
                ConvertDiagnostics(Compiled.Diagnostics, DiagnosticStage::Compiler);
            Result.Diagnostics.insert(Result.Diagnostics.end(), std::make_move_iterator(Diagnostics.begin()),
                                      std::make_move_iterator(Diagnostics.end()));
            return Result;
        }

        const vm::VerificationResult Verification = vm::Verify(*Compiled.Bytecode);
        if (!Verification.Success())
        {
            std::vector<ExecutionDiagnostic> Diagnostics =
                ConvertDiagnostics(Verification.Diagnostics, DiagnosticStage::Verifier);
            Result.Diagnostics.insert(Result.Diagnostics.end(), std::make_move_iterator(Diagnostics.begin()),
                                      std::make_move_iterator(Diagnostics.end()));
        }
        return Result;
    }
} // namespace dbc::interpreter
