#pragma once

#include "artifact_export_service.hpp"
#include "core/settings.hpp"
#include "toolchain_locator.hpp"

#include <string>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        struct ExportCheck
        {
            std::string Label;
            bool Passed = false;
            // A failed required check blocks the build; the others only warn.
            bool Required = true;
            // What is wrong and how to fix it, when the check failed.
            std::string Detail;
        };

        // What an export needs from this machine and the project, checked before the build starts so the
        // Studio can say what to install or configure instead of failing halfway through Gradle or Xcode.
        std::vector<ExportCheck> CheckExportReadiness(const ArtifactExportRequest& Request);
        bool ReadyToExport(const std::vector<ExportCheck>& Checks);

        // Applies the Studio's configured tool paths, or detects them when they are empty.
        ExportToolchain ResolveExportToolchain(const core::Settings& Settings, const ToolchainSearch& Search);
    } // namespace toolchain

} // namespace dbc::studio
