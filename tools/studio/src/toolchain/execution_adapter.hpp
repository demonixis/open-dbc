#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_events.h>

#include "interpreter/project_resolver.hpp"
#include "language/language_profile.hpp"

namespace dbc
{
    class EngineKernel;

    namespace interpreter
    {
        class ExecutionService;
        struct ExecutionDiagnostic;
        struct ExecutionResult;
        struct ExecutionTick;
    } // namespace interpreter
} // namespace dbc

namespace dbc::studio
{
    namespace toolchain
    {
        struct RunOptions
        {
            std::string AssetRoot;
            std::string LogLevel = "debug";
            bool DebugOverlay = false;
        };

        struct MemorySource
        {
            std::filesystem::path LogicalPath;
            std::string Text;
        };

        struct RunDocument
        {
            std::filesystem::path LogicalPath;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
            std::vector<MemorySource> Sources;
            std::optional<dbc::interpreter::ResolvedProject> Project;
        };

        class ExecutionAdapter
        {
          public:
            using LineHandler = std::function<void(const std::string& Line, bool IsError)>;
            using FinishHandler = std::function<void(int ExitCode, const std::map<int, std::string>& Errors)>;

            ~ExecutionAdapter();

            ExecutionAdapter(const ExecutionAdapter&) = delete;
            ExecutionAdapter& operator=(const ExecutionAdapter&) = delete;
            ExecutionAdapter();

            void SetHandlers(LineHandler OnLine, FinishHandler OnFinish);

            bool IsRunning() const;
            bool IsVrEnabled() const;
            bool IsVrPacing() const;
            bool Run(RunDocument Document, const RunOptions& Options);
            void BeginEventFrame();
            void HandleEvent(const SDL_Event& Event);
            void Stop();
            void Poll();
            void Shutdown();
            // Writes the scene and renderer dumps of the running program (the same as F4 in its window).
            void DumpDiagnostics();

            std::string Describe() const;

          private:
            void ReportDiagnostics(const dbc::interpreter::ExecutionResult& Result,
                                   std::map<int, std::string>& RootErrors);
            void ServiceSuspension(const dbc::interpreter::ExecutionTick& Tick);
            void Finish();
            void ShutdownRuntime();

            std::unique_ptr<dbc::EngineKernel> Kernel_;
            std::unique_ptr<dbc::interpreter::ExecutionService> Execution_;
            std::filesystem::path RootLogicalPath_;
            LineHandler OnLine_;
            FinishHandler OnFinish_;
            bool StopRequested_ = false;
        };
    } // namespace toolchain

} // namespace dbc::studio
