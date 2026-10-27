#pragma once

#include "host_services.hpp"
#include "language/frontend.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::vm
{
    struct Module;
}

namespace dbc
{
    class EngineKernel;

    namespace interpreter
    {
        struct SourceUnit
        {
            std::filesystem::path LogicalPath{};
            std::string Text{};
        };

        using MissingSourceLoader = std::function<std::optional<SourceUnit>(
            const std::filesystem::path& LogicalPath, language::LanguageProfile Profile, std::string& Error)>;

        struct ExecutionOptions
        {
            std::filesystem::path AssetRoot{};
            std::filesystem::path LogOutputPath{};
            std::filesystem::path BuiltinCoverageOutputPath{};
            std::filesystem::path BenchmarkOutputPath{};
            std::string CommandLine{};
            std::size_t InstructionBudget = 200000;
            int MaxFrames = 0;
            bool Benchmark = false;
            std::function<void(std::string_view Text)> Output{};
            HostFileLauncher LaunchFile{};
            ExitPromptPresenter PresentExitPrompt{};
        };

        struct ExecutionRequest
        {
            std::filesystem::path RootLogicalPath{};
            language::LanguageProfile Profile = language::LanguageProfile::Dbc;
            std::vector<SourceUnit> Sources{};
            std::vector<std::filesystem::path> OrderedProjectSources{};
            std::string InputKind = "source";
            std::string DialectSource = "forced";
            std::optional<std::filesystem::path> ProjectFile{};
            ExecutionOptions Options{};
            MissingSourceLoader LoadMissingSource{};
            std::optional<std::filesystem::path> ConfinedSourceRoot{};
            // Bytecode from a program image replaces Sources; Prepare still verifies it.
            std::shared_ptr<const vm::Module> Bytecode{};
        };

        enum class DiagnosticStage : std::uint8_t
        {
            Input,
            Frontend,
            Compiler,
            Verifier,
            Runtime,
        };

        struct ExecutionDiagnostic
        {
            DiagnosticStage Stage = DiagnosticStage::Input;
            language::DiagnosticSeverity Severity = language::DiagnosticSeverity::Error;
            std::string Code{};
            std::string Message{};
            std::filesystem::path LogicalPath{};
            language::SourcePosition Begin{};
            language::SourcePosition End{};
        };

        enum class ExecutionState : std::uint8_t
        {
            Empty,
            Ready,
            Running,
            Suspended,
            Completed,
            Failed,
            Cancelled,
        };

        enum class SuspensionReason : std::uint8_t
        {
            None,
            NextTick,
            FrameBoundary,
            Deadline,
            Key,
            Mouse,
            TextInput,
            Io,
        };

        struct ExecutionSuspension
        {
            SuspensionReason Reason = SuspensionReason::None;
            std::optional<std::chrono::steady_clock::time_point> ResumeAt{};
        };

        struct BenchmarkResult
        {
            double PrepareMilliseconds = 0.0;
            double TotalMilliseconds = 0.0;
            std::size_t FrameCount = 0;
            double AverageFrameMilliseconds = 0.0;
            double MinimumFrameMilliseconds = 0.0;
            double P50FrameMilliseconds = 0.0;
            double P95FrameMilliseconds = 0.0;
            double P99FrameMilliseconds = 0.0;
            double MaximumFrameMilliseconds = 0.0;
        };

        struct BuiltinCoverageEntry
        {
            language::SymbolId Symbol = language::SymbolId::Count;
            language::OverloadId Overload{};
            std::uint64_t Attempts = 0;
            std::uint64_t Completed = 0;
            std::uint64_t Failed = 0;
        };

        struct ExecutionResult
        {
            ExecutionState State = ExecutionState::Empty;
            std::vector<ExecutionDiagnostic> Diagnostics{};
            std::size_t FrameCount = 0;
            std::vector<BuiltinCoverageEntry> BuiltinCoverage{};
            BenchmarkResult Benchmark{};

            bool Success() const;
            bool Finished() const;
        };

        struct ExecutionTick
        {
            ExecutionState State = ExecutionState::Empty;
            std::size_t Instructions = 0;
            ExecutionSuspension Suspension{};
        };

        struct SourceInspection
        {
            struct BuiltinUse
            {
                language::SymbolId Symbol{};
                language::OverloadId Overload{};
                bool operator==(const BuiltinUse&) const = default;
            };

            std::filesystem::path RootLogicalPath{};
            language::LanguageProfile Profile = language::LanguageProfile::Dbc;
            std::string InputKind = "source";
            std::string DialectSource = "forced";
            std::optional<std::filesystem::path> ProjectFile{};
            std::vector<std::filesystem::path> OrderedSources{};
            std::size_t SourceCount = 0;
            std::size_t ByteCount = 0;
            std::size_t LineCount = 0;
            std::vector<std::string> Builtins{};
            std::vector<BuiltinUse> BuiltinOverloads{};
        };

        struct SourceAnalysis
        {
            SourceInspection Inspection{};
            std::vector<ExecutionDiagnostic> Diagnostics{};

            bool Success() const;
        };

        struct ProgramBuild
        {
            std::shared_ptr<vm::Module> Bytecode{};
            std::vector<ExecutionDiagnostic> Diagnostics{};
        };

        class ExecutionService
        {
          public:
            explicit ExecutionService(EngineKernel& Kernel);
            ~ExecutionService();

            ExecutionService(const ExecutionService&) = delete;
            ExecutionService& operator=(const ExecutionService&) = delete;

            bool Prepare(ExecutionRequest Request);
            void StartFrameMeasurements();
            ExecutionTick Tick(std::size_t InstructionBudget = 0);
            // A frame presented while the program waits for input counts toward MaxFrames like a SYNC does,
            // so a frame-bounded run cannot hang on WAIT KEY, SUSPEND FOR MOUSE or INPUT.
            void NotifyPresentedFrame();
            void RequestStop();

            ExecutionState State() const;
            const ExecutionResult& Result() const;

          private:
            struct Impl;
            std::unique_ptr<Impl> Impl_;
        };

        std::optional<SourceUnit> LoadSourceUnitFromDisk(const std::filesystem::path& LogicalPath,
                                                         language::LanguageProfile Profile, std::string& Error);
        SourceAnalysis AnalyzeSources(ExecutionRequest Request);
        // Compiles and verifies the sources of Request; Bytecode is null when a diagnostic is an error.
        ProgramBuild BuildProgram(ExecutionRequest Request);
        std::vector<ExecutionDiagnostic> VerifyBytecode(const vm::Module& Bytecode);
    } // namespace interpreter
} // namespace dbc
