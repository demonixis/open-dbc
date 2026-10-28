#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        // Where the locator looks besides the configured paths. Tests replace the environment and the roots.
        struct ToolchainSearch
        {
            std::function<std::optional<std::string>(std::string_view Name)> Variable;
            // Folders that may hold JDKs or Android SDKs: the home directory, application folders,
            // package manager prefixes.
            std::vector<std::filesystem::path> JdkRoots;
            std::vector<std::filesystem::path> AndroidSdkRoots;

            // The current machine: real environment, platform install locations.
            static ToolchainSearch Host();
        };

        // A JDK 17 or newer with java and keytool. An explicit path wins; an invalid one is not replaced
        // by a detected JDK, so the Studio reports the mistake instead of silently building with another.
        std::optional<std::filesystem::path> LocateJavaHome(const std::filesystem::path& Configured,
                                                            const ToolchainSearch& Search);
        std::optional<std::filesystem::path> LocateAndroidSdk(const std::filesystem::path& Configured,
                                                              const ToolchainSearch& Search);
        bool IsJavaHome(const std::filesystem::path& Path);
        bool IsAndroidSdk(const std::filesystem::path& Path);

        // The newest installed build-tools folder of an Android SDK.
        std::optional<std::filesystem::path> LatestAndroidBuildTools(const std::filesystem::path& Sdk);
        // The command that installs what an Android build needs from the SDK.
        std::string AndroidSdkInstallCommand(int CompileSdk);

        struct SigningIdentity
        {
            std::string Sha1;
            std::string Name;
        };
        // Parses `security find-identity -v -p codesigning`.
        std::vector<SigningIdentity> ParseSigningIdentities(std::string_view Output);
        // The valid code signing identities of the user's keychains; empty outside macOS.
        std::vector<SigningIdentity> ListSigningIdentities();
        // The lowest macOS version a Mach-O binary runs on, from its LC_BUILD_VERSION.
        std::optional<std::string> MacMinimumSystem(const std::filesystem::path& Binary);
    } // namespace toolchain

} // namespace dbc::studio
