#pragma once

#include "core/project_settings.hpp"
#include "interpreter/project_resolver.hpp"
#include "language/language_profile.hpp"
#include "process_runner.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        enum class ArtifactTarget
        {
            Desktop,
            Ios,
            IosSimulator,
            AndroidApk,
            AndroidBundle,
        };

        // Tools and signing identities of this machine, never stored in the project. Empty paths mean the
        // tool was not found.
        struct ExportToolchain
        {
            std::filesystem::path JavaHome;
            std::filesystem::path AndroidSdk;
            // A "Developer ID Application" identity name or its SHA-1.
            std::string MacSigningIdentity;
            // A notarytool keychain profile (xcrun notarytool store-credentials).
            std::string NotaryProfile;
            // The SHA-1 thumbprint of a code signing certificate in the user's Windows certificate store.
            std::string WindowsCertificate;
            std::string WindowsTimestampUrl;
        };

        // Kept in memory for the Studio session, or read from the environment by the CLI, and handed to
        // tools through their environment only.
        struct ExportSecrets
        {
            std::string AndroidKeystorePassword;
            std::string AndroidKeyPassword;

            static ExportSecrets FromEnvironment();
        };

        struct ArtifactExportRequest
        {
            ArtifactTarget Target = ArtifactTarget::Desktop;
            std::filesystem::path ProjectRoot;
            std::filesystem::path EntrySource;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
            std::vector<std::filesystem::path> OrderedSources;
            dbc::interpreter::ProjectStartup Startup;
            std::optional<std::filesystem::path> ProjectFile;
            std::filesystem::path AssetRoot;
            std::filesystem::path OutputDirectory;
            std::filesystem::path HostProduct;
            std::filesystem::path HostResources;
            core::ProjectSettings Settings;
            // Company, description and copyright from the DBPro project, for platform metadata.
            dbc::interpreter::ProjectMetadata Metadata;
            ExportToolchain Toolchain;
            ExportSecrets Secrets;
            bool Sign = true;
        };

        struct ArtifactExportResult
        {
            std::filesystem::path OutputPath;
            std::vector<std::string> Messages;
            std::string Error;

            explicit operator bool() const
            {
                return Error.empty() && !OutputPath.empty();
            }
        };

        struct ArtifactExportProgress
        {
            // Progress notes and tool output, called on the exporting thread.
            ProcessLineSink Log;
            const std::atomic<bool>* Cancel = nullptr;
        };

        class ArtifactExportService
        {
          public:
            ArtifactExportResult Export(const ArtifactExportRequest& Request,
                                        const ArtifactExportProgress& Progress = {}) const;
        };

        // The CLI and the Studio menu both build their request here, so every export starts from the
        // installed opendbc-runner with the project's startup and the same metadata fallbacks.
        ArtifactExportRequest MakeArtifactExportRequest(const dbc::interpreter::ResolvedProject& Project,
                                                        core::ProjectSettings Settings, ArtifactTarget Target,
                                                        const std::filesystem::path& OutputDirectory, bool Sign,
                                                        ExportToolchain Toolchain, ExportSecrets Secrets);

        namespace detail
        {
            bool ClaimArtifactStageDirectory(const std::filesystem::path& Path, std::string& Error);
            bool WriteArtifactIconPng(const std::filesystem::path& Source, const std::filesystem::path& Destination,
                                      int Size, bool Opaque, std::string& Error);
            std::optional<std::string> ArtifactSha256File(const std::filesystem::path& Path);
            bool ValidateArtifactHostIdentity(const std::filesystem::path& HostManifest,
                                              const std::filesystem::path& HostBinary,
                                              const std::filesystem::path& ProductRuntimeManifest, std::string& Error);
            bool ValidateArtifactShaders(const std::filesystem::path& HostShaderRoot,
                                         const std::filesystem::path& ProductShaderRoot, std::string& Error);
            bool ValidateAndroidSystemDependencies(const std::filesystem::path& HostManifest, std::string& Error);
        } // namespace detail
    } // namespace toolchain

} // namespace dbc::studio
