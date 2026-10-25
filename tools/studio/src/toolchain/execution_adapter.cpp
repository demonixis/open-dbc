#include "execution_adapter.hpp"

#include <chrono>
#include <string_view>
#include <utility>

#include "engine/systems/core_system.hpp"
#include "engine/systems/debug_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/vr_system.hpp"
#include "interpreter/execution_service.hpp"
#include "interpreter/runtime_startup.hpp"

namespace dbc::studio
{
    namespace toolchain
    {
        namespace
        {
            std::string FormatDiagnostic(const dbc::interpreter::ExecutionDiagnostic& Diagnostic)
            {
                std::string Result;
                if (!Diagnostic.LogicalPath.empty())
                    Result += Diagnostic.LogicalPath.string();
                if (Diagnostic.Begin.Line > 0)
                {
                    if (!Result.empty())
                        Result += ':';
                    Result += std::to_string(Diagnostic.Begin.Line);
                    if (Diagnostic.Begin.Column > 0)
                        Result += ':' + std::to_string(Diagnostic.Begin.Column);
                }
                if (!Result.empty())
                    Result += ": ";
                if (!Diagnostic.Code.empty())
                    Result += Diagnostic.Code + ": ";
                Result += Diagnostic.Message;
                return Result;
            }

            bool IsError(const dbc::interpreter::ExecutionDiagnostic& Diagnostic)
            {
                return Diagnostic.Severity == dbc::language::DiagnosticSeverity::Error;
            }
        } // namespace

        ExecutionAdapter::ExecutionAdapter() = default;

        ExecutionAdapter::~ExecutionAdapter()
        {
            ShutdownRuntime();
        }

        void ExecutionAdapter::SetHandlers(LineHandler OnLine, FinishHandler OnFinish)
        {
            OnLine_ = std::move(OnLine);
            OnFinish_ = std::move(OnFinish);
        }

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

            if (!dbc::language::IsLanguageSource(Document.LogicalPath))
            {
                if (OnLine_)
                    OnLine_("BASIC source must use the .dba extension", true);
                return false;
            }

            std::error_code PathError;
            RootLogicalPath_ = std::filesystem::absolute(Document.LogicalPath, PathError).lexically_normal();
            if (PathError)
            {
                if (OnLine_)
                    OnLine_("Cannot resolve source path: " + PathError.message(), true);
                RootLogicalPath_.clear();
                return false;
            }
            StopRequested_ = false;

            const std::filesystem::path LogDirectory =
                RootLogicalPath_.parent_path().empty() ? std::filesystem::path(".") : RootLogicalPath_.parent_path();
            dbc::log::Init(Options.LogLevel, LogDirectory.string());

            const dbc::interpreter::ProjectStartup Startup =
                Document.Project ? Document.Project->Startup : dbc::interpreter::ProjectStartup{};
            Kernel_ = std::make_unique<dbc::EngineKernel>();
            if (!Kernel_->Initialize(Startup.Title, Startup.Width, Startup.Height))
            {
                if (OnLine_)
                    OnLine_("Failed to create the OpenDBC runtime", true);
                ShutdownRuntime();
                return false;
            }

            dbc::interpreter::ExecutionRequest Request;
            Request.RootLogicalPath = RootLogicalPath_;
            Request.Profile = Document.Profile;
            if (Document.Project)
            {
                Request.OrderedProjectSources = Document.Project->OrderedSources;
                Request.InputKind = std::string(dbc::interpreter::ProjectInputKindName(Document.Project->InputKind));
                Request.DialectSource =
                    std::string(dbc::interpreter::DialectOriginName(Document.Project->ProfileOrigin));
                Request.ProjectFile = Document.Project->DescriptorPath;
                Request.Options.CommandLine = Document.Project->Startup.CommandLine;
            }
            else
            {
                Request.OrderedProjectSources = {RootLogicalPath_};
            }
            bool HasRoot = false;
            for (MemorySource& Source : Document.Sources)
            {
                Source.LogicalPath = std::filesystem::absolute(Source.LogicalPath, PathError).lexically_normal();
                if (PathError)
                {
                    if (OnLine_)
                        OnLine_("Cannot resolve buffered source path: " + PathError.message(), true);
                    ShutdownRuntime();
                    return false;
                }
                HasRoot = HasRoot || Source.LogicalPath == RootLogicalPath_;
                Request.Sources.push_back({std::move(Source.LogicalPath), std::move(Source.Text)});
            }
            if (!HasRoot)
            {
                std::string Error;
                std::optional<dbc::interpreter::SourceUnit> Root =
                    dbc::interpreter::LoadSourceUnitFromDisk(RootLogicalPath_, Document.Profile, Error);
                if (!Root)
                {
                    if (OnLine_)
                        OnLine_(Error, true);
                    ShutdownRuntime();
                    return false;
                }
                Request.Sources.push_back(std::move(*Root));
            }
            for (const std::filesystem::path& Ordered : Request.OrderedProjectSources)
            {
                const std::filesystem::path Absolute = std::filesystem::absolute(Ordered, PathError).lexically_normal();
                const bool Present =
                    std::any_of(Request.Sources.begin(), Request.Sources.end(),
                                [&](const dbc::interpreter::SourceUnit& Unit) { return Unit.LogicalPath == Absolute; });
                if (Present)
                    continue;
                std::string Error;
                std::optional<dbc::interpreter::SourceUnit> Unit =
                    dbc::interpreter::LoadSourceUnitFromDisk(Absolute, Document.Profile, Error);
                if (!Unit)
                {
                    if (OnLine_)
                        OnLine_(Error, true);
                    ShutdownRuntime();
                    return false;
                }
                Request.Sources.push_back(std::move(*Unit));
            }
            Request.Options.AssetRoot =
                Options.AssetRoot.empty() ? RootLogicalPath_.parent_path() : std::filesystem::path(Options.AssetRoot);
            Request.Options.Output = [this](std::string_view Text)
            {
                if (OnLine_)
                    OnLine_(std::string(Text), false);
            };
            Request.LoadMissingSource = dbc::interpreter::LoadSourceUnitFromDisk;

            Execution_ = std::make_unique<dbc::interpreter::ExecutionService>(*Kernel_);
            if (!Execution_->Prepare(std::move(Request)))
            {
                Finish();
                return false;
            }

            const dbc::interpreter::RuntimeStartupResult Runtime =
                dbc::interpreter::InitializeProjectRuntime(*Kernel_, Startup);
            if (!Runtime)
            {
                if (OnLine_)
                    OnLine_(Runtime.Diagnostic, true);
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
            if (!IsRunning())
                return;
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
            if (OnLine_)
                OnLine_("Wrote open-dbc-dump-scene.txt and open-dbc-dump-renderer.txt", false);
        }

        void ExecutionAdapter::Poll()
        {
            if (!IsRunning())
                return;

            if (!Kernel_->Core().Running())
            {
                StopRequested_ = true;
                Execution_->RequestStop();
            }

            const dbc::interpreter::ExecutionTick Tick = Execution_->Tick();
            if (Tick.State == dbc::interpreter::ExecutionState::Running && !Kernel_->Core().SyncOn())
                Kernel_->Renderer().RenderScene(true);
            ServiceSuspension(Tick);
            if (Execution_->Result().Finished())
                Finish();
        }

        void ExecutionAdapter::Shutdown()
        {
            ShutdownRuntime();
        }

        std::string ExecutionAdapter::Describe() const
        {
            return "in-process";
        }

        void ExecutionAdapter::ReportDiagnostics(const dbc::interpreter::ExecutionResult& Result,
                                                 std::map<int, std::string>& RootErrors)
        {
            for (const dbc::interpreter::ExecutionDiagnostic& Diagnostic : Result.Diagnostics)
            {
                const std::string Message = FormatDiagnostic(Diagnostic);
                if (OnLine_)
                    OnLine_(Message, IsError(Diagnostic));
                const std::filesystem::path DiagnosticPath =
                    Diagnostic.LogicalPath.empty() ? RootLogicalPath_ : Diagnostic.LogicalPath.lexically_normal();
                if (IsError(Diagnostic) && DiagnosticPath == RootLogicalPath_ && Diagnostic.Begin.Line > 0)
                {
                    RootErrors.emplace(static_cast<int>(Diagnostic.Begin.Line), Diagnostic.Message);
                }
            }
        }

        void ExecutionAdapter::ServiceSuspension(const dbc::interpreter::ExecutionTick& Tick)
        {
            using dbc::interpreter::SuspensionReason;

            switch (Tick.Suspension.Reason)
            {
                case SuspensionReason::Deadline:
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

        void ExecutionAdapter::Finish()
        {
            const dbc::interpreter::ExecutionResult Result = Execution_->Result();
            std::map<int, std::string> Errors;
            ReportDiagnostics(Result, Errors);

            const int ExitCode = Result.Success() ? 0 : (StopRequested_ ? 130 : 1);
            ShutdownRuntime();
            if (OnFinish_)
                OnFinish_(ExitCode, Errors);
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
            RootLogicalPath_.clear();
        }
    } // namespace toolchain

} // namespace dbc::studio
