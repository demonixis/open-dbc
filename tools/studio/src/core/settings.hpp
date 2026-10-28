#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dbc::studio
{
    namespace core
    {
        // Preferences use JSON in the per-user directory; ImGui persists window geometry and docking.
        struct Settings
        {
            std::string ProjectFolder;
            std::string LastFile;
            std::string LogLevel = "debug";
            bool DebugOverlay = false;
            std::map<std::string, std::string> ProjectPathRemappings;

            // External tools for exports. Empty JDK and SDK paths mean automatic detection.
            std::string JavaHome;
            std::string AndroidSdk;
            std::string MacSigningIdentity;
            std::string NotaryProfile;
            std::string WindowsCertificate;
            std::string WindowsTimestampUrl = "http://timestamp.digicert.com";
            // Last export folder per project root; absolute local paths belong to this machine, not the project.
            std::map<std::string, std::string> ExportFolders;

            std::optional<std::filesystem::path> ProjectPathRemapping(const std::filesystem::path& Descriptor,
                                                                      std::string_view RawPath) const;
            void SetProjectPathRemapping(const std::filesystem::path& Descriptor, std::string_view RawPath,
                                         const std::filesystem::path& LocalPath);
            void ClearProjectPathRemappings();

            static Settings Parse(std::string_view Text, std::string& Error);
            static Settings Load();
            bool Save() const;
        };
    } // namespace core

} // namespace dbc::studio
