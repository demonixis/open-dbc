#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "execution_adapter.hpp"

namespace dbc::studio
{
    namespace toolchain
    {
        struct ActiveRunPlan
        {
            std::optional<RunDocument> Document;
            std::filesystem::path AssetRoot;
            std::vector<std::string> Blockers;
            std::string Error;

            explicit operator bool() const
            {
                return Document.has_value() && Error.empty();
            }
        };

        ActiveRunPlan PlanActiveRun(const std::filesystem::path& ActivePath,
                                    const std::optional<dbc::interpreter::ResolvedProject>& Project,
                                    std::vector<MemorySource> Sources);
    } // namespace toolchain

} // namespace dbc::studio
