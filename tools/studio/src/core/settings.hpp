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
        enum class DocumentationReader
        {
            Studio,
            Browser,
        };

        // Edit > Editor Settings.
        struct EditorSettings
        {
            static constexpr float KMinSpeed = 0.1f;
            static constexpr float KMaxSpeed = 5.0f;
            static constexpr float KMinTextZoom = 0.5f;
            static constexpr float KMaxTextZoom = 4.0f;
            static constexpr float KMinInterfaceZoom = 0.75f;
            static constexpr float KMaxInterfaceZoom = 2.5f;

            // The viewport's zoom under the wheel or two fingers.
            float ScrollSpeed = 1.0f;
            // The viewport camera's flight.
            float CameraSpeed = 1.0f;
            // The size of the code font, in the editor and the panels that use it.
            float TextZoom = 1.0f;
            // The size of everything else: menus, panels, buttons.
            float InterfaceZoom = 1.0f;
            DocumentationReader DocReader = DocumentationReader::Studio;
        };

        // Preferences use JSON in the per-user directory; ImGui persists window geometry and docking.
        struct Settings
        {
            std::string ProjectFolder;
            std::string LastFile;
            std::string LogLevel = "debug";
            // "auto" follows the system; anything else is a language code.
            std::string Language = "auto";
            // "auto" follows the system, otherwise "dark" or "light".
            std::string Theme = "dark";
            bool DebugOverlay = false;
            EditorSettings Editor;
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
