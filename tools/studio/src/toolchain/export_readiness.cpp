#include "export_readiness.hpp"
#include "export_internal.hpp"

#include <algorithm>
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
            Toolchain.MacSigningIdentity = Settings.MacSigningIdentity;
            Toolchain.NotaryProfile = Settings.NotaryProfile;
            Toolchain.WindowsCertificate = Settings.WindowsCertificate;
            Toolchain.WindowsTimestampUrl = Settings.WindowsTimestampUrl;
            return Toolchain;
        }
    } // namespace toolchain

} // namespace dbc::studio
