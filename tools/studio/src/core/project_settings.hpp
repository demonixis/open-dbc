#pragma once

#include "language/language_profile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        // Per-project settings persisted as .opendbc-project.json in the project root.
        struct ProjectSettings
        {
            // General
            std::string AppName;
            std::string BundleId;
            std::string VersionName = "1.0.0";
            int VersionCode = 1;

            // Icons
            std::string IconSource;

            // iOS
            std::string IosTeamId;
            std::string IosDeploymentTarget = "16.0";
            std::vector<std::string> IosOrientations = {"LandscapeLeft", "LandscapeRight"};
            std::string IosCameraDesc;
            std::string IosMicrophoneDesc;
            std::string IosLocationDesc;
            std::string IosPhotoDesc;

            // Android
            int AndroidMinSdk = 29;
            int AndroidTargetSdk = 36;
            std::string AndroidOrientation = "sensorLandscape";
            std::vector<std::string> AndroidPermissions;
            std::string AndroidKeystorePath;
            std::string AndroidKeyAlias = "release";

            // Language selection. Automatic detection never writes these fields.
            std::optional<dbc::language::LanguageProfile> LanguageDialect;
            std::string LanguageProjectFile;
            std::string LanguageEntry;

            // Original JSON is retained so fields owned by newer versions or plugins survive.
            std::string PreservedJson;
            std::string LoadError;

            static std::filesystem::path FilePath(const std::filesystem::path& ProjectRoot);
            static ProjectSettings Parse(const std::string& Text);
            static ProjectSettings Load(const std::filesystem::path& ProjectRoot);
            bool Save(const std::filesystem::path& ProjectRoot) const;
        };
    } // namespace core

} // namespace dbc::studio
