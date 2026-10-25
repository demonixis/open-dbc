#pragma once

#include "bytecode.hpp"

namespace dbc::vm
{
    struct CompileResult
    {
        std::shared_ptr<Module> Bytecode{};
        std::vector<language::Diagnostic> Diagnostics{};

        bool Success() const;
    };

    CompileResult Lower(const language::Program& Program);
} // namespace dbc::vm
