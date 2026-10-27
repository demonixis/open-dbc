#pragma once

#include "execution_service.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Diagnostic helpers shared by source compilation and execution, which live in separate
// translation units so that a bytecode-only runner never links the BASIC frontend.
namespace dbc::interpreter::detail
{
    inline ExecutionDiagnostic InputDiagnostic(std::string Code, std::string Message,
                                               const std::filesystem::path& Path = {})
    {
        return {DiagnosticStage::Input, language::DiagnosticSeverity::Error, std::move(Code), std::move(Message),
                Path};
    }

    inline ExecutionDiagnostic ConvertDiagnostic(const language::Diagnostic& Value, DiagnosticStage Stage)
    {
        const std::filesystem::path& Path = Value.LogicalPath.empty() ? Value.Span.LogicalPath : Value.LogicalPath;
        return {Stage, Value.Severity, Value.Code, Value.Message, Path, Value.Span.Begin, Value.Span.End};
    }

    inline std::vector<ExecutionDiagnostic> ConvertDiagnostics(const std::vector<language::Diagnostic>& Values,
                                                               DiagnosticStage Stage)
    {
        std::vector<ExecutionDiagnostic> Result;
        Result.reserve(Values.size());
        for (const language::Diagnostic& Value : Values)
            Result.push_back(ConvertDiagnostic(Value, Stage));
        return Result;
    }

    inline bool HasErrors(const std::vector<ExecutionDiagnostic>& Values)
    {
        return std::any_of(Values.begin(), Values.end(), [](const ExecutionDiagnostic& Value)
                           { return Value.Severity == language::DiagnosticSeverity::Error; });
    }
} // namespace dbc::interpreter::detail
