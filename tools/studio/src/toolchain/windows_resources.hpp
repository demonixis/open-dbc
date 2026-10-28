#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        // The VERSIONINFO an exported Windows game shows in Explorer's properties and in SmartScreen.
        struct WindowsVersionInfo
        {
            std::string ProductName;
            std::string CompanyName;
            std::string Description;
            std::string Copyright;
            // One to three numeric components, completed by the build number.
            std::string Version;
            int Build = 1;
            std::string OriginalFilename;
        };

        struct WindowsIconImage
        {
            int Size = 0;
            // PNG-encoded images, which Windows Vista and later read inside icon resources.
            std::vector<std::uint8_t> Png;
        };

        // The RT_VERSION resource: a VS_VERSIONINFO tree in UTF-16.
        std::vector<std::uint8_t> BuildVersionResource(const WindowsVersionInfo& Info);
        // The RT_GROUP_ICON resource naming RT_ICON resources FirstId, FirstId + 1, ...
        std::vector<std::uint8_t> BuildIconGroupResource(const std::vector<WindowsIconImage>& Images,
                                                         std::uint16_t FirstId);

        // Replaces the icon and the version of a copied executable, invalidating any signature. Windows only.
        bool UpdateWindowsExecutableResources(const std::filesystem::path& Executable,
                                              const std::vector<WindowsIconImage>& Icons,
                                              const WindowsVersionInfo& Version, std::string& Error);
    } // namespace toolchain

} // namespace dbc::studio
