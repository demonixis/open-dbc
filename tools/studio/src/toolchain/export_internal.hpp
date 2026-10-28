#pragma once

// Shared by the per-target export sources; not part of the Studio's toolchain interface.

#include "artifact_export_service.hpp"
#include "interpreter/embedded_artifact.hpp"
#include "process_runner.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace toolchain::internal
    {
        namespace fs = std::filesystem;

        // Carries the caller's log and cancellation into every tool an export runs.
        struct ExportContext
        {
            const ArtifactExportProgress* Progress = nullptr;

            void Note(std::string_view Line) const;
            bool Cancelled(std::string& Error) const;
            // Streams the tool's output into the log.
            bool Run(const ProcessRequest& Request, std::string& Error) const;
            bool Run(const std::vector<std::string>& Arguments, std::string& Error) const;
            // Returns the tool's output instead of logging it.
            std::optional<std::string> Capture(const std::vector<std::string>& Arguments, std::string& Error) const;
            // Logs the tool's output and returns it.
            std::optional<std::string> RunCapturing(const ProcessRequest& Request, std::string& Error) const;
        };

        // Removes a staging directory unless the export publishes it.
        struct StageCleanup
        {
            fs::path Path;
            ~StageCleanup();
        };

        std::string Lower(std::string Value);
        std::optional<std::string> Sha256File(const fs::path& Path);
        bool IsBundleIdentifier(std::string_view Value);
        std::string SafeProductName(const core::ProjectSettings& Settings, const fs::path& Entry);
        std::string PropertyValue(std::string_view Value);
        std::string XmlValue(std::string_view Value);

        bool CreateStageDirectory(const fs::path& OutputDirectory, std::string_view Prefix, fs::path& Stage,
                                  std::string& Error);
        bool PublishNoReplace(const fs::path& Source, const fs::path& Destination, std::string& Error);
        bool PublishAndRemoveStage(const fs::path& Source, const fs::path& Destination, const fs::path& Stage,
                                   std::string& Error);
        // Not CopyFile: <windows.h> defines that name as a macro.
        bool CopyArtifactFile(const fs::path& Source, const fs::path& Destination, std::string& Error);
        bool CopyTree(const fs::path& Source, const fs::path& Destination, std::string& Error);
        bool RemoveRequired(const fs::path& Path, std::string& Error);
        std::optional<std::string> ReadFile(const fs::path& Path);

        bool BuildExportProgram(const ArtifactExportRequest& Request, std::vector<std::uint8_t>& Image,
                                std::string& Error);
        // Writes program.obc, the assets and the manifest into ResourceRoot.
        bool WriteArtifact(const ArtifactExportRequest& Request, const std::vector<std::uint8_t>& Image,
                           const fs::path& ResourceRoot, std::vector<dbc::interpreter::detail::ArtifactFile>& Files,
                           std::string& Error);

        std::optional<fs::path> ResolveArtifactIcon(const ArtifactExportRequest& Request, bool Required,
                                                    std::string& Error);
        bool WriteScaledPng(const ExportContext& Context, const fs::path& Source, const fs::path& Destination,
                            int Size, bool Opaque, std::string& Error);

#if defined(__APPLE__)
        // Development builds: an ad hoc signature runs on this Mac only.
        bool AdHocSignAppleBundle(const ExportContext& Context, const fs::path& Bundle, std::string& Error);
        bool IsAppleVersion(std::string_view Value);
        bool IsSupportedIosDeployment(std::string_view Value);
        bool IsAppleTeamIdentifier(std::string_view Value);
        bool SetAppleMetadata(const ExportContext& Context, const fs::path& App, const core::ProjectSettings& Settings,
                              const std::string& ProductName, std::string& Error);
        // Signs every nested Mach-O, then the bundle, with the hardened runtime and a secure timestamp.
        bool SignMacDistribution(const ExportContext& Context, const fs::path& App, const std::string& Identity,
                                 std::string& Error);
        // Submits the signed bundle to Apple, waits for the verdict and staples the ticket.
        bool NotarizeMacApp(const ExportContext& Context, const fs::path& App, const std::string& Profile,
                            std::string& Error);
#endif

        // Checks that the installed export host was built from this Studio's runtime.
        bool ValidateDesktopHost(const ArtifactExportRequest& Request, std::string& Error);
        bool ValidateAndroidHost(const ArtifactExportRequest& Request, std::string& Error);
        bool ValidateIosExportHost(const ArtifactExportRequest& Request, std::string& Error);

        // Windows and Linux exports are a folder holding the renamed runner, the OpenXR loader, the shaders, the
        // notices and the payload. These steps compile on every host, so their tests run everywhere.
        bool StageDesktopFolder(const ArtifactExportRequest& Request, const std::vector<std::uint8_t>& Image,
                                const fs::path& Stage, const fs::path& Executable, std::string_view XrLoader,
                                std::vector<dbc::interpreter::detail::ArtifactFile>& Files, std::string& Error);
        // Gives the executable the game's icon and version, then signs it in distribution builds.
        bool FinishWindowsFolder(const ExportContext& Context, const ArtifactExportRequest& Request,
                                 const fs::path& Stage, const fs::path& Executable, const std::string& ProductName,
                                 std::string& Error);
        // Adds the game's icon and a desktop entry beside the executable.
        bool FinishLinuxFolder(const ExportContext& Context, const ArtifactExportRequest& Request,
                               const fs::path& Stage, const std::string& ProductName, std::string& Error);
        std::string LinuxDesktopEntry(const std::string& ProductName);
        std::vector<std::string> WindowsSignArguments(const fs::path& Signtool, const ExportToolchain& Toolchain,
                                                      const fs::path& File);
        std::vector<std::string> DesktopArchiveArguments(const fs::path& OutputDirectory,
                                                         const std::string& ProductName, const fs::path& Archive);

        ArtifactExportResult ExportDesktop(const ExportContext& Context, const ArtifactExportRequest& Request,
                                           const std::vector<std::uint8_t>& Image);
        ArtifactExportResult ExportIos(const ExportContext& Context, const ArtifactExportRequest& Request,
                                       const std::vector<std::uint8_t>& Image);
        ArtifactExportResult ExportAndroid(const ExportContext& Context, const ArtifactExportRequest& Request,
                                           const std::vector<std::uint8_t>& Image, bool Bundle);
    } // namespace toolchain::internal

} // namespace dbc::studio
