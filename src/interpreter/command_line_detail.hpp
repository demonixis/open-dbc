#pragma once

#include "command_line.hpp"

#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Shared by command_line.cpp, which compiles BASIC sources, and command_line_run.cpp, which only
// runs prepared bytecode and is all that opendbc-runner links.
namespace dbc::interpreter::detail
{
    struct DiskRequestResult
    {
        std::optional<ExecutionRequest> Request{};
        std::optional<ResolvedProject> Project{};
        std::string Error{};
    };

    using PrepareMethod = bool (ExecutionService::*)(ExecutionRequest);

    void WriteStandard(std::FILE* File, std::string_view Text);
    void Write(const std::function<void(std::string_view)>& Sink, std::FILE* Default, std::string_view Text);
    bool ParseNonNegative(std::string_view Text, std::size_t& Value);
    int Exit(CommandLineExitCode Value);
    std::string JsonEscape(std::string_view Text);
    void WriteDiagnostics(const std::vector<ExecutionDiagnostic>& Diagnostics,
                          const CommandLineEnvironment& Environment);
    DiskRequestResult MakeImageRequest(const CommandLine& Command);
    int RunRequest(const CommandLine& Command, const CommandLineEnvironment& Environment, DiskRequestResult Loaded,
                   PrepareMethod Prepare);
} // namespace dbc::interpreter::detail
