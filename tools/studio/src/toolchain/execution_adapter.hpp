#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
            // Set to run headless and write the scene there, stopping one frame after `CaptureFrames`:
            // that frame shows which statements belong to the game loop.
            std::filesystem::path CaptureScene;
            int CaptureFrames = 1;
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

        // A compile-time or run-time diagnostic and where it points.
        struct RunDiagnostic
        {
            std::filesystem::path Path;
            std::uint32_t Line = 0;
            std::uint32_t Column = 0;
            std::string Code;
            std::string Message;
            bool Error = true;
        };

        // Reads a diagnostic line as `opendbc run` prints it: "path:line:column: DBCnnnn: message", the position
        // being optional. A relative path resolves against SourceRoot.
        std::optional<RunDiagnostic> ParseRunDiagnostic(std::string_view Line, const std::filesystem::path& SourceRoot);

        // Runs the active program for the Studio. Desktop Studios compile it, then run the program image in
        // opendbc-runner, a child process: a crash or a hang in the program leaves the Studio running and Stop
        // always ends it. The iOS and Android Studios cannot start processes and run it in-process.
        class ExecutionAdapter
        {
          public:
            using LineHandler = std::function<void(const std::string& Line, bool IsError)>;
            using FinishHandler = std::function<void(int ExitCode, const std::vector<RunDiagnostic>& Diagnostics)>;

            ~ExecutionAdapter();

            ExecutionAdapter(const ExecutionAdapter&) = delete;
            ExecutionAdapter& operator=(const ExecutionAdapter&) = delete;
            ExecutionAdapter();

            void SetHandlers(LineHandler OnLine, FinishHandler OnFinish);
            // The opendbc-runner executable; the one installed beside the Studio by default.
            void SetRunner(std::filesystem::path Runner);

            bool IsRunning() const;
            bool IsVrEnabled() const;
            bool IsVrPacing() const;
            // False when the program cannot start; compile errors still reach the finish handler.
            bool Run(RunDocument Document, const RunOptions& Options);
            // Whether Run honours RunOptions::CaptureScene: an in-process program has no separate process to end.
            bool CanCaptureScene() const;
            // Compiles Document without running it.
            std::vector<RunDiagnostic> Check(RunDocument Document) const;
            void BeginEventFrame();
            void HandleEvent(const SDL_Event& Event);
            void Stop();
            // Forwards the program's output and reports its end; called once per Studio frame.
            void Poll();
            void Shutdown();
            // Writes the scene and renderer dumps of an in-process program (F4 in a child program's window).
            void DumpDiagnostics();
            bool CanDumpDiagnostics() const;

            std::string Describe() const;

          private:
            void Report(const std::string& Line, bool IsError);
            void Finish(int ExitCode);

            std::filesystem::path RootLogicalPath_;
            std::vector<RunDiagnostic> Diagnostics_;
            LineHandler OnLine_;
            FinishHandler OnFinish_;
            bool StopRequested_ = false;

#if defined(DBC_STUDIO_SANDBOXED)
            // Reports the finished program's diagnostics and releases its runtime.
            void Complete();
            void ServiceSuspension(const dbc::interpreter::ExecutionTick& Tick);
            void ShutdownRuntime();

            std::unique_ptr<dbc::EngineKernel> Kernel_;
            std::unique_ptr<dbc::interpreter::ExecutionService> Execution_;
#else
            void StopChild();

            std::filesystem::path Runner_;
            // Program images go in a folder of their own, removed at shutdown.
            std::filesystem::path RunDirectory_;
            std::filesystem::path Image_;
            std::filesystem::path SourceRoot_;
            std::thread Worker_;
            std::atomic<bool> Cancel_ = false;
            // Filled by the worker, drained by Poll.
            std::mutex Mutex_;
            std::vector<std::string> Lines_;
            bool Done_ = false;
            int ExitCode_ = 0;
            std::string LaunchError_;
#endif
        };
    } // namespace toolchain

} // namespace dbc::studio
