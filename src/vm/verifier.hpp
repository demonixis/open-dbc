#pragma once

#include "bytecode.hpp"

namespace dbc::vm
{
    struct VerificationResult
    {
        std::vector<language::Diagnostic> Diagnostics{};

        bool Success() const;
    };

    VerificationResult Verify(const Module& Value);
} // namespace dbc::vm
