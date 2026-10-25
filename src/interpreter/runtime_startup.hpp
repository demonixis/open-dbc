#pragma once

#include "project_resolver.hpp"

#include <string>

namespace dbc
{
    class EngineKernel;
}

namespace dbc::interpreter
{
    struct RuntimeStartupResult
    {
        std::string Diagnostic{};

        explicit operator bool() const
        {
            return Diagnostic.empty();
        }
    };

    RuntimeStartupResult InitializeProjectRuntime(EngineKernel& Kernel, const ProjectStartup& Startup);
} // namespace dbc::interpreter
