#pragma once

#include "core/project_settings.hpp"
#include "language/language_profile.hpp"

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

        struct ArtifactExportRequest
        {
            ArtifactTarget Target = ArtifactTarget::Desktop;
            std::filesystem::path ProjectRoot;
            std::filesystem::path EntrySource;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
            std::vector<std::filesystem::path> OrderedSources;
            std::optional<std::filesystem::path> ProjectFile;
            std::filesystem::path AssetRoot;
            std::filesystem::path OutputDirectory;
            std::filesystem::path HostProduct;
            std::filesystem::path HostResources;
            core::ProjectSettings Settings;
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

        class ArtifactExportService
        {
          public:
            ArtifactExportResult Export(const ArtifactExportRequest& Request) const;
        };

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
