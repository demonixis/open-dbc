#include "execution_adapter.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <chrono>
#include <regex>
#include <string_view>
#include <utility>

#include "interpreter/execution_service.hpp"

#if defined(DBC_STUDIO_SANDBOXED)
#include "engine/systems/core_system.hpp"
#include "engine/systems/debug_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/vr_system.hpp"
#include "interpreter/runtime_startup.hpp"
#else
#include "core/paths.hpp"
#include "interpreter/atomic_file.hpp"
#include "interpreter/command_line.hpp"
#include "interpreter/program_image.hpp"
#include "toolchain/process_runner.hpp"
#endif

namespace dbc::studio
{
    namespace toolchain
    {
        namespace
        {
            std::string FormatDiagnostic(const RunDiagnostic& Diagnostic)
            {
                std::string Result;
                if (!Diagnostic.Path.empty())
                    Result += dbc::PathToUtf8(Diagnostic.Path);
                if (Diagnostic.Line > 0)
                {
                    if (!Result.empty())
                        Result += ':';
                    Result += std::to_string(Diagnostic.Line);
                    if (Diagnostic.Column > 0)
                        Result += ':' + std::to_string(Diagnostic.Column);
                }
                if (!Result.empty())
                    Result += ": ";
                if (!Diagnostic.Code.empty())
                    Result += Diagnostic.Code + ": ";
                Result += Diagnostic.Message;
                return Result;
            }

            RunDiagnostic ToRunDiagnostic(const dbc::interpreter::ExecutionDiagnostic& Value,
                                          const std::filesystem::path& Root)
            {
                RunDiagnostic Result;
                Result.Path = Value.LogicalPath.empty() ? Root : Value.LogicalPath.lexically_normal();
                Result.Line = Value.Begin.Line;
                Result.Column = Value.Begin.Column;
                Result.Code = Value.Code;
                Result.Message = Value.Message;
                Result.Error = Value.Severity == dbc::language::DiagnosticSeverity::Error;
                return Result;
            }

            // Fills Request with the program's sources: the unsaved buffers, then the project's files from disk.
            bool MakeRequest(RunDocument& Document, const std::filesystem::path& Root,
                             dbc::interpreter::ExecutionRequest& Request, std::string& Error)
            {
                Request.RootLogicalPath = Root;
                Request.Profile = Document.Profile;
                if (Document.Project)
                {
                    Request.OrderedProjectSources = Document.Project->OrderedSources;
                    Request.InputKind =
                        std::string(dbc::interpreter::ProjectInputKindName(Document.Project->InputKind));
                    Request.DialectSource =
                        std::string(dbc::interpreter::DialectOriginName(Document.Project->ProfileOrigin));
                    Request.ProjectFile = Document.Project->DescriptorPath;
                    Request.Options.CommandLine = Document.Project->Startup.CommandLine;
                }
                else
                {
                    Request.OrderedProjectSources = {Root};
                }
                std::error_code PathError;
                bool HasRoot = false;
                for (MemorySource& Source : Document.Sources)
                {
                    Source.LogicalPath = std::filesystem::absolute(Source.LogicalPath, PathError).lexically_normal();
                    if (PathError)
                    {
                        Error = "Cannot resolve buffered source path: " + PathError.message();
                        return false;
                    }
                    HasRoot = HasRoot || Source.LogicalPath == Root;
                    Request.Sources.push_back({std::move(Source.LogicalPath), std::move(Source.Text)});
                }
                if (!HasRoot)
                {
                    std::optional<dbc::interpreter::SourceUnit> Unit =
                        dbc::interpreter::LoadSourceUnitFromDisk(Root, Document.Profile, Error);
                    if (!Unit)
                        return false;
                    Request.Sources.push_back(std::move(*Unit));
                }
                for (const std::filesystem::path& Ordered : Request.OrderedProjectSources)
                {
                    const std::filesystem::path Absolute =
                        std::filesystem::absolute(Ordered, PathError).lexically_normal();
                    const bool Present = std::any_of(Request.Sources.begin(), Request.Sources.end(),
                                                     [&](const dbc::interpreter::SourceUnit& Unit)
                                                     { return Unit.LogicalPath == Absolute; });
                    if (Present)
                        continue;
                    std::optional<dbc::interpreter::SourceUnit> Unit =
                        dbc::interpreter::LoadSourceUnitFromDisk(Absolute, Document.Profile, Error);
                    if (!Unit)
                        return false;
                    Request.Sources.push_back(std::move(*Unit));
                }
                Request.LoadMissingSource = dbc::interpreter::LoadSourceUnitFromDisk;
                return true;
            }

            bool ResolveRoot(const RunDocument& Document, std::filesystem::path& Root, std::string& Error)
            {
                if (!dbc::language::IsLanguageSource(Document.LogicalPath))
                {
                    Error = "BASIC source must use the .dba extension";
                    return false;
                }
                std::error_code PathError;
                Root = std::filesystem::absolute(Document.LogicalPath, PathError).lexically_normal();
                if (PathError)
                {
                    Error = "Cannot resolve source path: " + PathError.message();
                    Root.clear();
                    return false;
                }
                return true;
            }
        } // namespace

        std::optional<RunDiagnostic> ParseRunDiagnostic(std::string_view Line, const std::filesystem::path& SourceRoot)
        {
            // The lazy path stops at the first ":line:column: " or ": DBC", which a Windows drive letter is not.
            static const std::regex Pattern(R"(^(?:(.+?):(\d+):(\d+): |(.+?): )?(DBC\d{4}): (.*)$)");
            std::match_results<std::string_view::const_iterator> Match;
            if (!std::regex_match(Line.begin(), Line.end(), Match, Pattern))
                return std::nullopt;
            RunDiagnostic Result;
            const std::string Path = Match[1].matched ? Match[1].str() : Match[4].str();
            if (!Path.empty())
            {
                const std::filesystem::path Value = dbc::PathFromUtf8(Path);
                Result.Path = (Value.is_absolute() ? Value : SourceRoot / Value).lexically_normal();
            }
            if (Match[2].matched)
            {
                Result.Line = static_cast<std::uint32_t>(std::stoul(Match[2].str()));
                Result.Column = static_cast<std::uint32_t>(std::stoul(Match[3].str()));
            }
            Result.Code = Match[5].str();
            Result.Message = Match[6].str();
            return Result;
        }

        ExecutionAdapter::ExecutionAdapter() = default;

        ExecutionAdapter::~ExecutionAdapter()
        {
            Shutdown();
        }

        void ExecutionAdapter::SetHandlers(LineHandler OnLine, FinishHandler OnFinish)
        {
            OnLine_ = std::move(OnLine);
            OnFinish_ = std::move(OnFinish);
        }

        std::vector<RunDiagnostic> ExecutionAdapter::Check(RunDocument Document) const
        {
            std::vector<RunDiagnostic> Result;
            std::filesystem::path Root;
            std::string Error;
            dbc::interpreter::ExecutionRequest Request;
            if (!ResolveRoot(Document, Root, Error) || !MakeRequest(Document, Root, Request, Error))
            {
                RunDiagnostic Failure;
                Failure.Path = Root;
                Failure.Message = Error;
                Result.push_back(std::move(Failure));
                return Result;
            }
            for (const dbc::interpreter::ExecutionDiagnostic& Value :
                 dbc::interpreter::BuildProgram(std::move(Request)).Diagnostics)
                Result.push_back(ToRunDiagnostic(Value, Root));
            return Result;
        }

        void ExecutionAdapter::Report(const std::string& Line, bool IsError)
        {
            if (OnLine_)
                OnLine_(Line, IsError);
        }

        void ExecutionAdapter::Finish(int ExitCode)
        {
            const std::vector<RunDiagnostic> Diagnostics = std::move(Diagnostics_);
            Diagnostics_.clear();
            RootLogicalPath_.clear();
            if (OnFinish_)
                OnFinish_(ExitCode, Diagnostics);
        }

#if defined(DBC_STUDIO_SANDBOXED)
        void ExecutionAdapter::SetRunner(std::filesystem::path) {}

        bool ExecutionAdapter::IsRunning() const
        {
            return Execution_ && !Execution_->Result().Finished();
        }

        bool ExecutionAdapter::IsVrEnabled() const
        {
            return IsRunning() && Kernel_->Vr().Enabled();
        }

        bool ExecutionAdapter::IsVrPacing() const
        {
            return IsRunning() && Kernel_->Vr().Pacing();
        }

        bool ExecutionAdapter::Run(RunDocument Document, const RunOptions& Options)
        {
            if (IsRunning() || Document.LogicalPath.empty())
                return false;
            std::string Error;
            if (!ResolveRoot(Document, RootLogicalPath_, Error))
            {
                Report(Error, true);
                return false;
            }
            StopRequested_ = false;

            const std::filesystem::path LogDirectory =
                RootLogicalPath_.parent_path().empty() ? dbc::PathFromUtf8(".") : RootLogicalPath_.parent_path();
            dbc::log::Init(Options.LogLevel, dbc::PathToUtf8(LogDirectory));

            const dbc::interpreter::ProjectStartup Startup =
                Document.Project ? Document.Project->Startup : dbc::interpreter::ProjectStartup{};
            Kernel_ = std::make_unique<dbc::EngineKernel>();
            if (!Kernel_->Initialize(Startup.Title, Startup.Width, Startup.Height))
            {
                Report("Failed to create the OpenDBC runtime", true);
                ShutdownRuntime();
                return false;
            }

            dbc::interpreter::ExecutionRequest Request;
            if (!MakeRequest(Document, RootLogicalPath_, Request, Error))
            {
                Report(Error, true);
                ShutdownRuntime();
                return false;
            }
            Request.Options.AssetRoot =
                Options.AssetRoot.empty() ? RootLogicalPath_.parent_path() : dbc::PathFromUtf8(Options.AssetRoot);
            Request.Options.Output = [this](std::string_view Text) { Report(std::string(Text), false); };

            Execution_ = std::make_unique<dbc::interpreter::ExecutionService>(*Kernel_);
            if (!Execution_->Prepare(std::move(Request)))
            {
                Complete();
                return false;
            }

            const dbc::interpreter::RuntimeStartupResult Runtime =
                dbc::interpreter::InitializeProjectRuntime(*Kernel_, Startup);
            if (!Runtime)
            {
                Report(Runtime.Diagnostic, true);
                ShutdownRuntime();
                return false;
            }
            Kernel_->Core().HideMouse();
            if (Options.DebugOverlay)
                Kernel_->Debug().SetOverlayEnabled(true);
            return true;
        }

        void ExecutionAdapter::BeginEventFrame()
        {
            if (IsRunning())
                Kernel_->Input().BeginEventFrame();
        }

        void ExecutionAdapter::HandleEvent(const SDL_Event& Event)
        {
            if (IsRunning())
                Kernel_->Input().HandleEvent(Event);
        }

        void ExecutionAdapter::Stop()
        {
            if (!IsRunning())
                return;
            StopRequested_ = true;
            Execution_->RequestStop();
        }

        void ExecutionAdapter::DumpDiagnostics()
        {
            if (!IsRunning())
                return;
            Kernel_->Debug().DumpDiagnostics();
            Report("Wrote open-dbc-dump-scene.txt and open-dbc-dump-renderer.txt", false);
        }

        bool ExecutionAdapter::CanDumpDiagnostics() const
        {
            return IsRunning();
        }

        void ExecutionAdapter::Poll()
        {
            if (!Execution_)
                return;
            if (IsRunning())
            {
                if (!Kernel_->Core().Running())
                {
                    StopRequested_ = true;
                    Execution_->RequestStop();
                }
                const dbc::interpreter::ExecutionTick Tick = Execution_->Tick();
                if (Tick.State == dbc::interpreter::ExecutionState::Running && !Kernel_->Core().SyncOn())
                    Kernel_->Renderer().RenderScene(true);
                ServiceSuspension(Tick);
                if (!Execution_->Result().Finished())
                    return;
            }
            Complete();
        }

        void ExecutionAdapter::Complete()
        {
            const dbc::interpreter::ExecutionResult Result = Execution_->Result();
            for (const dbc::interpreter::ExecutionDiagnostic& Value : Result.Diagnostics)
            {
                Diagnostics_.push_back(ToRunDiagnostic(Value, RootLogicalPath_));
                Report(FormatDiagnostic(Diagnostics_.back()), Diagnostics_.back().Error);
            }
            const int ExitCode = Result.Success() ? 0 : (StopRequested_ ? 130 : 1);
            ShutdownRuntime();
            Finish(ExitCode);
        }

        void ExecutionAdapter::Shutdown()
        {
            ShutdownRuntime();
        }

        std::string ExecutionAdapter::Describe() const
        {
            return "in-process";
        }

        void ExecutionAdapter::ServiceSuspension(const dbc::interpreter::ExecutionTick& Tick)
        {
            using dbc::interpreter::SuspensionReason;

            switch (Tick.Suspension.Reason)
            {
                case SuspensionReason::Deadline:
                    if (Kernel_->Core().RefreshesWhileWaiting())
                        Kernel_->Renderer().RenderScene(true);
                    break;
                case SuspensionReason::Key:
                case SuspensionReason::Mouse:
                case SuspensionReason::TextInput:
                    Kernel_->Renderer().RenderScene(true);
                    break;
                case SuspensionReason::None:
                case SuspensionReason::NextTick:
                case SuspensionReason::FrameBoundary:
                case SuspensionReason::Io:
                    break;
            }
        }

        void ExecutionAdapter::ShutdownRuntime()
        {
            Execution_.reset();
            if (Kernel_)
            {
                Kernel_->Shutdown();
                Kernel_.reset();
            }
            dbc::log::Shutdown();
        }
#else
        namespace
        {
            std::filesystem::path InstalledRunner()
            {
#if defined(__APPLE__)
                return core::paths::RunnerProduct() / "Contents/MacOS/opendbc-runner";
#else
                return core::paths::RunnerProduct();
#endif
            }
        } // namespace

        void ExecutionAdapter::SetRunner(std::filesystem::path Runner)
        {
            Runner_ = std::move(Runner);
        }

        bool ExecutionAdapter::IsRunning() const
        {
            return Worker_.joinable();
        }

        // VR programs pace themselves in their own process.
        bool ExecutionAdapter::IsVrEnabled() const
        {
            return false;
        }

        bool ExecutionAdapter::IsVrPacing() const
        {
            return false;
        }

        bool ExecutionAdapter::Run(RunDocument Document, const RunOptions& Options)
        {
            if (IsRunning() || Document.LogicalPath.empty())
                return false;
            std::string Error;
            if (!ResolveRoot(Document, RootLogicalPath_, Error))
            {
                Report(Error, true);
                return false;
            }
            StopRequested_ = false;
            const std::filesystem::path Runner = Runner_.empty() ? InstalledRunner() : Runner_;
            std::error_code FileError;
            if (!std::filesystem::is_regular_file(Runner, FileError))
            {
                Report("Cannot run the program: opendbc-runner is missing from " +
                           dbc::PathToUtf8(Runner.parent_path()),
                       true);
                RootLogicalPath_.clear();
                return false;
            }

            dbc::interpreter::ExecutionRequest Request;
            if (!MakeRequest(Document, RootLogicalPath_, Request, Error))
            {
                Report(Error, true);
                RootLogicalPath_.clear();
                return false;
            }
            const dbc::interpreter::ProjectStartup Startup =
                Document.Project ? Document.Project->Startup : dbc::interpreter::ProjectStartup{};
            // Images store source paths relative to this root; run-time diagnostics resolve against it.
            SourceRoot_ = Document.Project ? Document.Project->ProjectRoot : RootLogicalPath_.parent_path();
            const dbc::interpreter::ProgramBuild Build = dbc::interpreter::BuildProgram(std::move(Request));
            for (const dbc::interpreter::ExecutionDiagnostic& Value : Build.Diagnostics)
            {
                Diagnostics_.push_back(ToRunDiagnostic(Value, RootLogicalPath_));
                Report(FormatDiagnostic(Diagnostics_.back()), Diagnostics_.back().Error);
            }
            if (!Build.Bytecode)
            {
                Finish(static_cast<int>(dbc::interpreter::CommandLineExitCode::Source));
                return false;
            }

            if (RunDirectory_.empty())
            {
                const std::filesystem::path Temporary = std::filesystem::temp_directory_path(FileError);
                if (FileError)
                {
                    Report("Cannot find a temporary folder for the program image: " + FileError.message(), true);
                    Finish(static_cast<int>(dbc::interpreter::CommandLineExitCode::Target));
                    return false;
                }
                const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                RunDirectory_ = Temporary / ("opendbc-studio-" + std::to_string(static_cast<long long>(Stamp)));
            }
            Image_ = RunDirectory_ / "program.obc";
            const std::vector<std::uint8_t> Image =
                dbc::interpreter::WriteProgramImage(*Build.Bytecode, Startup, SourceRoot_);
            if (!dbc::interpreter::AtomicWriteText(
                    Image_, std::string_view(reinterpret_cast<const char*>(Image.data()), Image.size()), &Error))
            {
                Report("Cannot write the program image: " + Error, true);
                Finish(static_cast<int>(dbc::interpreter::CommandLineExitCode::Target));
                return false;
            }

            const std::filesystem::path AssetRoot =
                Options.AssetRoot.empty() ? RootLogicalPath_.parent_path() : dbc::PathFromUtf8(Options.AssetRoot);
            ProcessRequest Process;
            Process.Arguments = {dbc::PathToUtf8(Runner), dbc::PathToUtf8(Image_),
                                 "--asset-root",          dbc::PathToUtf8(AssetRoot),
                                 "--log-level",           Options.LogLevel,
                                 "--log-directory",       dbc::PathToUtf8(RootLogicalPath_.parent_path())};
            if (Options.DebugOverlay)
                Process.Arguments.push_back("--debug-overlay");
            Process.WorkingDirectory = AssetRoot;

            Cancel_ = false;
            Done_ = false;
            Lines_.clear();
            LaunchError_.clear();
            Worker_ = std::thread(
                [this, Process = std::move(Process)]
                {
                    const ProcessResult Result = RunProcess(
                        Process,
                        [this](std::string_view Line)
                        {
                            const std::lock_guard Lock(Mutex_);
                            Lines_.emplace_back(Line);
                        },
                        &Cancel_, false);
                    const std::lock_guard Lock(Mutex_);
                    ExitCode_ = Result.Cancelled ? static_cast<int>(dbc::interpreter::CommandLineExitCode::Interrupted)
                                                 : Result.ExitCode;
                    // A started program reports its own failures; only a launch failure needs explaining.
                    if (!Result.Cancelled && Result.ExitCode < 0)
                        LaunchError_ = Result.Error;
                    Done_ = true;
                });
            return true;
        }

        void ExecutionAdapter::BeginEventFrame() {}

        void ExecutionAdapter::HandleEvent(const SDL_Event&) {}

        void ExecutionAdapter::Stop()
        {
            if (!IsRunning())
                return;
            StopRequested_ = true;
            Cancel_ = true;
        }

        void ExecutionAdapter::DumpDiagnostics() {}

        bool ExecutionAdapter::CanDumpDiagnostics() const
        {
            return false;
        }

        void ExecutionAdapter::Poll()
        {
            if (!IsRunning())
                return;
            std::vector<std::string> Lines;
            bool Done = false;
            {
                const std::lock_guard Lock(Mutex_);
                Lines.swap(Lines_);
                Done = Done_;
            }
            for (const std::string& Line : Lines)
            {
                const std::optional<RunDiagnostic> Diagnostic = ParseRunDiagnostic(Line, SourceRoot_);
                if (Diagnostic)
                    Diagnostics_.push_back(*Diagnostic);
                Report(Line, Diagnostic.has_value());
            }
            if (!Done)
                return;
            Worker_.join();
            if (!LaunchError_.empty())
                Report(LaunchError_, true);
            std::error_code Ignored;
            std::filesystem::remove(Image_, Ignored);
            Finish(ExitCode_);
        }

        void ExecutionAdapter::StopChild()
        {
            if (!Worker_.joinable())
                return;
            Cancel_ = true;
            Worker_.join();
        }

        void ExecutionAdapter::Shutdown()
        {
            StopChild();
            if (!RunDirectory_.empty())
            {
                std::error_code Ignored;
                std::filesystem::remove_all(RunDirectory_, Ignored);
                RunDirectory_.clear();
            }
        }

        std::string ExecutionAdapter::Describe() const
        {
            return "opendbc-runner";
        }
#endif
    } // namespace toolchain

} // namespace dbc::studio
