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
