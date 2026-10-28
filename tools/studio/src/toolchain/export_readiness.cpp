#include "export_readiness.hpp"
#include "export_internal.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <system_error>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        // The export template compiles against this Android platform (examples/android/export-template).
        constexpr int AndroidCompileSdk = 36;

        // docs/usage.md describes how to provide each export host.
        void AddHostCheck(std::vector<toolchain::ExportCheck>& Checks, bool Valid, const std::string& Error,
                          std::string_view Remedy)
        {
            Checks.push_back({"Export host matches this Studio", Valid, true,
                              Valid ? std::string{} : Error + ". " + std::string(Remedy)});
        }

        void AddAndroidChecks(const toolchain::ArtifactExportRequest& Request,
                              std::vector<toolchain::ExportCheck>& Checks)
        {
            std::string Error;
            AddHostCheck(Checks, toolchain::internal::ValidateAndroidHost(Request, Error), Error,
                         "Build the android-host preset and configure OpenDBC with "
                         "OPENDBC_ANDROID_EXPORT_HOST_LIBRARY.");
            const bool HasJava = !Request.Toolchain.JavaHome.empty();
            Checks.push_back({"JDK 17 or newer", HasJava, true,
                              HasJava ? std::string{}
                                      : "Install Android Studio or a JDK 17+, or set its folder in External Tools."});
            const fs::path& Sdk = Request.Toolchain.AndroidSdk;
            Checks.push_back({"Android SDK", !Sdk.empty(), true,
                              !Sdk.empty() ? std::string{}
                                           : "Install Android Studio or the Android command-line tools, or set the "
                                             "SDK folder in External Tools."});
            if (!Sdk.empty())
            {
                std::error_code FileError;
                const bool HasPlatform =
                    fs::is_directory(Sdk / "platforms" / ("android-" + std::to_string(AndroidCompileSdk)), FileError);
                const bool HasBuildTools = toolchain::LatestAndroidBuildTools(Sdk).has_value();
                // Gradle can download missing SDK packages once their licences are accepted, so this only warns.
                Checks.push_back({"Android platform " + std::to_string(AndroidCompileSdk) + " and build tools",
                                  HasPlatform && HasBuildTools, false,
                                  HasPlatform && HasBuildTools
                                      ? std::string{}
                                      : "Run: " + toolchain::AndroidSdkInstallCommand(AndroidCompileSdk)});
            }
            if (!Request.Sign)
                return;
            fs::path Keystore = Request.Settings.AndroidKeystorePath;
            if (!Keystore.empty() && Keystore.is_relative())
                Keystore = Request.ProjectRoot / Keystore;
            std::error_code FileError;
            const bool HasKeystore = !Keystore.empty() && fs::is_regular_file(Keystore, FileError) &&
                                     !Request.Settings.AndroidKeyAlias.empty();
            Checks.push_back({"Release keystore", HasKeystore, true,
                              HasKeystore ? std::string{}
                                          : "Create or choose a keystore and its key alias in Project Settings > "
                                            "Android."});
            const bool HasPasswords =
                !Request.Secrets.AndroidKeystorePassword.empty() && !Request.Secrets.AndroidKeyPassword.empty();
            Checks.push_back({"Keystore passwords", HasPasswords, true,
                              HasPasswords ? std::string{}
                                           : "Enter them in the Build window, or set OPENDBC_ANDROID_KEYSTORE_PASSWORD "
                                             "and OPENDBC_ANDROID_KEY_PASSWORD."});
        }

#if defined(__APPLE__)
        // The oldest macOS release still supported by Apple when this was written.
        constexpr int RecommendedMacMinimum = 13;

        void AddMacChecks(const toolchain::ArtifactExportRequest& Request, std::vector<toolchain::ExportCheck>& Checks)
        {
            const std::optional<std::string> Minimum =
                toolchain::MacMinimumSystem(Request.HostProduct / "Contents/MacOS/opendbc-runner");
            int Major = 0;
            if (Minimum)
                std::from_chars(Minimum->data(), Minimum->data() + Minimum->size(), Major);
            const bool Reach = Major > 0 && Major <= RecommendedMacMinimum;
            Checks.push_back({"Runs on macOS " + std::to_string(RecommendedMacMinimum) + " and later", Reach, false,
                              Reach ? std::string{}
                                    : "This OpenDBC build needs macOS " + Minimum.value_or("of the build machine") +
                                          ". Build OpenDBC with -DCMAKE_OSX_DEPLOYMENT_TARGET=" +
                                          std::to_string(RecommendedMacMinimum) + ".0 to reach older Macs."});
            if (!Request.Sign)
                return;
            const std::string& Wanted = Request.Toolchain.MacSigningIdentity;
            const std::vector<toolchain::SigningIdentity> Identities = toolchain::ListSigningIdentities();
            const bool Installed =
                !Wanted.empty() && std::any_of(Identities.begin(), Identities.end(),
                                               [&Wanted](const toolchain::SigningIdentity& Identity)
                                               { return Identity.Name == Wanted || Identity.Sha1 == Wanted; });
            Checks.push_back(
                {"Developer ID identity", Installed, true,
                 Installed ? std::string{}
                 : Wanted.empty()
                     ? "Choose a \"Developer ID Application\" identity in External tools. Xcode > Settings > "
                       "Accounts creates one for a paid Apple Developer account."
                     : "\"" + Wanted + "\" is not in your keychains."});
            const bool Profile = !Request.Toolchain.NotaryProfile.empty();
            Checks.push_back({"Notarization profile", Profile, false,
                              Profile ? std::string{}
                                      : "Without notarization, Gatekeeper blocks the app on other Macs. Run: xcrun "
                                        "notarytool store-credentials <name>, then enter the name in External tools."});
        }
#endif

        void AddIosChecks(const toolchain::ArtifactExportRequest& Request, bool Simulator,
                          std::vector<toolchain::ExportCheck>& Checks)
        {
            std::string Error;
            AddHostCheck(Checks, toolchain::internal::ValidateIosExportHost(Request, Simulator, Error), Error,
                         "Build the ios presets and configure OpenDBC with their export hosts.");
            if (Simulator || !Request.Sign)
                return;
            const bool HasTeam = !Request.Settings.IosTeamId.empty();
            Checks.push_back({"Apple development team", HasTeam, true,
                              HasTeam ? std::string{} : "Set the development team ID in Project Settings > iOS."});
        }
    } // namespace

    namespace toolchain
    {
        std::vector<ExportCheck> CheckExportReadiness(const ArtifactExportRequest& Request)
        {
            std::vector<ExportCheck> Checks;
            std::string Error;
            switch (Request.Target)
            {
                case ArtifactTarget::Desktop:
                    AddHostCheck(Checks, internal::ValidateDesktopHost(Request, Error), Error,
                                 "Rebuild OpenDBC so opendbc-runner matches this Studio.");
#if defined(__APPLE__)
                    AddMacChecks(Request, Checks);
#endif
                    break;
                case ArtifactTarget::AndroidApk:
                case ArtifactTarget::AndroidBundle:
                    AddAndroidChecks(Request, Checks);
                    break;
                case ArtifactTarget::Ios:
                    AddIosChecks(Request, false, Checks);
                    break;
                case ArtifactTarget::IosSimulator:
                    AddIosChecks(Request, true, Checks);
                    break;
            }
            return Checks;
        }

        bool ReadyToExport(const std::vector<ExportCheck>& Checks)
        {
            return std::all_of(Checks.begin(), Checks.end(),
                               [](const ExportCheck& Check) { return Check.Passed || !Check.Required; });
        }

        ExportToolchain ResolveExportToolchain(const core::Settings& Settings, const ToolchainSearch& Search)
        {
            ExportToolchain Toolchain;
            Toolchain.JavaHome = LocateJavaHome(Settings.JavaHome, Search).value_or(fs::path{});
            Toolchain.AndroidSdk = LocateAndroidSdk(Settings.AndroidSdk, Search).value_or(fs::path{});
            // Build machines configure signing through the environment instead of the Studio preferences.
            const auto Choose = [&Search](const char* Variable, const std::string& Configured)
            {
                const std::optional<std::string> Value = Search.Variable ? Search.Variable(Variable) : std::nullopt;
                return Value ? *Value : Configured;
            };
            Toolchain.MacSigningIdentity = Choose("OPENDBC_MAC_SIGNING_IDENTITY", Settings.MacSigningIdentity);
            Toolchain.NotaryProfile = Choose("OPENDBC_NOTARY_PROFILE", Settings.NotaryProfile);
            Toolchain.WindowsCertificate = Choose("OPENDBC_WINDOWS_CERTIFICATE", Settings.WindowsCertificate);
            Toolchain.WindowsTimestampUrl = Choose("OPENDBC_WINDOWS_TIMESTAMP_URL", Settings.WindowsTimestampUrl);
            return Toolchain;
        }
    } // namespace toolchain

} // namespace dbc::studio
