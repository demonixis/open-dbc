#pragma once

#include "execution_service.hpp"
#include "project_resolver.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dbc::interpreter
{
    enum class CommandLineExitCode : int
    {
        Success = 0,
        Usage = 2,
        Target = 3,
        Source = 4,
        Runtime = 5,
        Interrupted = 130,
    };

    enum class CommandLineAction
    {
        Studio,
        Run,
        Check,
        Inspect,
        Export,
    };

    struct CommandLine
    {
        CommandLineAction Action = CommandLineAction::Studio;
        std::optional<std::filesystem::path> Path{};
        std::optional<language::LanguageProfile> RequestedProfile{};
        std::optional<std::string> CommandLineOverride{};
        ExecutionOptions Execution{};
        std::string LogLevel{};
        std::string ExportTarget{};
        std::filesystem::path ExportOutput{};
        bool ExportSign = true;
        bool DebugOverlay = false;
        bool Headless = false;
        std::filesystem::path CaptureOutput{};
        bool Json = false;
    };

    struct CommandLineParseResult
    {
        std::optional<CommandLine> Command{};
        std::string Error{};
        bool HelpRequested = false;

        explicit operator bool() const;
    };

    struct CommandLineEnvironment
    {
        std::function<void(std::string_view Text)> StandardOutput{};
        std::function<void(std::string_view Text)> StandardError{};
        std::function<int(const CommandLine& Command)> LaunchStudio{};
        std::function<int(const CommandLine& Command, const ResolvedProject& Project)> ExportArtifact{};
        DialectSelector SelectDialect{};
        ProjectSelector SelectProject{};
        language::ProjectPathRemapper RemapProjectPath{};
        std::optional<std::filesystem::path> ConfinedSourceRoot{};
        std::vector<std::filesystem::path> OrderedProjectSources{};
    };

    std::string_view CommandLineHelp();
    CommandLineParseResult ParseCommandLine(std::span<const std::string_view> Arguments);
    int DispatchCommandLine(const CommandLine& Command, const CommandLineEnvironment& Environment = {});
    int DispatchCommandLine(std::span<const std::string_view> Arguments,
                            const CommandLineEnvironment& Environment = {});
} // namespace dbc::interpreter
