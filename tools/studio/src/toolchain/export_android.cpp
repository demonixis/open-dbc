#include "export_internal.hpp"
#include "path_utf8.hpp"
#include "toolchain_locator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <system_error>

namespace dbc::studio
{
    namespace toolchain::internal
    {
        constexpr std::uint64_t AndroidPageAlignment = 16384;

        bool ValidateAndroidHostLibrary(const fs::path& Path, std::string& Error)
        {
            std::error_code FileError;
            const fs::file_status Status = fs::symlink_status(Path, FileError);
            if (FileError || !fs::is_regular_file(Status) || fs::is_symlink(Status))
            {
                Error = "The installed Android export host has no regular non-symlink libmain.so";
                return false;
            }
            std::array<unsigned char, 64> Header{};
            std::ifstream Input(Path, std::ios::binary);
            Input.read(reinterpret_cast<char*>(Header.data()), static_cast<std::streamsize>(Header.size()));
            if (Input.gcount() < 20 || Header[0] != 0x7f || Header[1] != 'E' || Header[2] != 'L' ||
                Header[3] != 'F' || Header[4] != 2 || Header[5] != 1 || Header[6] != 1 || Header[18] != 0xb7 ||
                Header[19] != 0)
            {
                Error = "The installed Android export host libmain.so is not ELF64 little-endian AArch64";
                return false;
            }
            const auto Little = [](const unsigned char* Bytes, int Count)
            {
                std::uint64_t Value = 0;
                for (int Index = Count - 1; Index >= 0; --Index)
                    Value = (Value << 8) | Bytes[Index];
                return Value;
            };
            // Every loadable segment must be aligned on 16 KB pages.
            const std::uint64_t ProgramHeaders = Little(Header.data() + 32, 8);
            const std::uint64_t EntrySize = Little(Header.data() + 54, 2);
            const std::uint64_t EntryCount = Little(Header.data() + 56, 2);
            bool Loadable = false;
            bool Aligned = Input.gcount() == static_cast<std::streamsize>(Header.size()) && EntrySize >= 56;
            for (std::uint64_t Index = 0; Aligned && Index < EntryCount; ++Index)
            {
                std::array<unsigned char, 56> Entry{};
                Input.clear();
                Input.seekg(static_cast<std::streamoff>(ProgramHeaders + Index * EntrySize));
                Input.read(reinterpret_cast<char*>(Entry.data()), static_cast<std::streamsize>(Entry.size()));
                if (Input.gcount() != static_cast<std::streamsize>(Entry.size()))
                    Aligned = false;
                else if (Little(Entry.data(), 4) == 1)
                {
                    Loadable = true;
                    Aligned = Little(Entry.data() + 48, 8) >= AndroidPageAlignment;
                }
            }
            if (!Loadable || !Aligned)
            {
                Error = "The installed Android export host libmain.so is not aligned on 16 KB pages, which Google "
                        "Play requires";
                return false;
            }
            return true;
        }

        static bool PrepareAndroidIcon(const ExportContext& Context, const ArtifactExportRequest& Request,
                                       const fs::path& Project, std::string& Error)
        {
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            if (!Source)
                return false;
            static const std::array<std::pair<std::string_view, int>, 5> Icons = {{
                {"mipmap-mdpi", 48},
                {"mipmap-hdpi", 72},
                {"mipmap-xhdpi", 96},
                {"mipmap-xxhdpi", 144},
                {"mipmap-xxxhdpi", 192},
            }};
            for (const auto& [Directory, Size] : Icons)
            {
                if (!WriteScaledPng(Context, *Source, Project / "app/src/main/res" / Directory / "ic_launcher.png",
                                    Size, false, Error))
                    return false;
            }
            return true;
        }

        bool ValidateAndroidHost(const ArtifactExportRequest& Request, std::string& Error)
        {
            const fs::path Host = Request.HostResources / "export-hosts/android/template";
            std::error_code FileError;
            if (!fs::is_directory(Host, FileError))
            {
                Error = "The installed product does not contain the qualified Android export host";
                return false;
            }
            const fs::path Library = Host / "app/src/main/jniLibs/arm64-v8a/libmain.so";
            const fs::path Manifest = Host / "app/src/main/assets/opendbc-host.json";
            return detail::ValidateArtifactShaders(Host / "app/src/main/assets/shaders",
                                                   Request.HostResources / "shaders", Error) &&
                   ValidateAndroidHostLibrary(Library, Error) &&
                   detail::ValidateArtifactHostIdentity(Manifest, Library,
                                                        Request.HostResources / "opendbc-runtime.json", Error) &&
                   detail::ValidateAndroidSystemDependencies(Manifest, Error);
        }

        // Confirms that Gradle signed the package with the release key before it is published.
        static bool VerifyAndroidSignature(const ExportContext& Context, const ExportToolchain& Toolchain,
                                           const fs::path& Package, bool Bundle, std::string& Error)
        {
            Context.Note("Verifying the signature");
            if (Bundle)
            {
                // jarsigner exits successfully on an unsigned bundle, so its verdict is read from the output.
#if defined(_WIN32)
                const fs::path Jarsigner = Toolchain.JavaHome / "bin/jarsigner.exe";
#else
                const fs::path Jarsigner = Toolchain.JavaHome / "bin/jarsigner";
#endif
                const std::optional<std::string> Output = Context.RunCapturing(
                    {{dbc::PathToUtf8(Jarsigner), "-verify", dbc::PathToUtf8(Package)}, {}, {}}, Error);
                if (!Output)
                    return false;
                if (Output->find("jar verified.") == std::string::npos)
                {
                    Error = "The Android App Bundle is not signed";
                    return false;
                }
                return true;
            }
            const std::optional<fs::path> BuildTools = LatestAndroidBuildTools(Toolchain.AndroidSdk);
            if (!BuildTools)
            {
                Error = "Verifying the APK needs the Android SDK build tools";
                return false;
            }
#if defined(_WIN32)
            const fs::path Apksigner = *BuildTools / "apksigner.bat";
#else
            const fs::path Apksigner = *BuildTools / "apksigner";
#endif
            return Context.Run({{dbc::PathToUtf8(Apksigner), "verify", "--verbose", dbc::PathToUtf8(Package)},
                                {{"JAVA_HOME", dbc::PathToUtf8(Toolchain.JavaHome)}},
                                {}},
                               Error);
        }

        ArtifactExportResult ExportAndroid(const ExportContext& Context, const ArtifactExportRequest& Request,
                                           const std::vector<std::uint8_t>& Image, bool Bundle)
        {
            ArtifactExportResult Result;
            const fs::path Host = Request.HostResources / "export-hosts/android/template";
            std::error_code FileError;
            if (!ValidateAndroidHost(Request, Result.Error))
                return Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + (Bundle ? ".aab" : ".apk"));
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + dbc::PathToUtf8(Result.OutputPath);
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-android-stage-", Stage, Result.Error))
                return Result;
            StageCleanup Cleanup{Stage};
            const fs::path Project = Stage / "project";
            if (!CopyTree(Host, Project, Result.Error))
                return Result;
            const fs::path Injection = Project / "app/src/main/assets";
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!WriteArtifact(Request, Image, Injection, Files, Result.Error))
                return Result;

            const std::string BundleId =
                Request.Settings.BundleId.empty() ? "net.demonixis.opendbc.artifact" : Request.Settings.BundleId;
            if (!IsBundleIdentifier(BundleId))
            {
                Result.Error = "Android bundle ID must contain at least two valid Java identifier segments";
                return Result;
            }
            static const std::set<std::string> AndroidOrientations = {"sensorLandscape", "landscape", "sensorPortrait",
                                                                      "portrait", "fullSensor"};
            if (!AndroidOrientations.contains(Request.Settings.AndroidOrientation))
            {
                Result.Error = "Unsupported Android screen orientation";
                return Result;
            }
            static const std::set<std::string> AndroidPermissions = {"INTERNET",
                                                                     "CAMERA",
                                                                     "RECORD_AUDIO",
                                                                     "ACCESS_FINE_LOCATION",
                                                                     "READ_EXTERNAL_STORAGE",
                                                                     "WRITE_EXTERNAL_STORAGE",
                                                                     "VIBRATE"};
            if (std::any_of(Request.Settings.AndroidPermissions.begin(), Request.Settings.AndroidPermissions.end(),
                            [](const std::string& Permission) { return !AndroidPermissions.contains(Permission); }))
            {
                Result.Error = "Unsupported Android permission";
                return Result;
            }
            constexpr int AndroidHostMinSdk = 29;
            constexpr int AndroidCompileSdk = 36;
            if (Request.Settings.VersionName.empty() || Request.Settings.VersionName.size() > 64 ||
                Request.Settings.VersionName.find_first_of("\r\n") != std::string::npos ||
                Request.Settings.VersionCode < 1 || Request.Settings.AndroidMinSdk < AndroidHostMinSdk ||
                Request.Settings.AndroidTargetSdk < Request.Settings.AndroidMinSdk ||
                Request.Settings.AndroidTargetSdk > AndroidCompileSdk)
            {
                Result.Error = "Android settings require minSdk 29 or newer and targetSdk no newer than 36";
                return Result;
            }
            fs::path Keystore;
            const bool UseSigning = Request.Sign && !Request.Settings.AndroidKeystorePath.empty();
            if (Request.Sign && !UseSigning)
            {
                Result.Error = "Android release export requires a configured keystore";
                return Result;
            }
            if (UseSigning)
            {
                Keystore = dbc::PathFromUtf8(Request.Settings.AndroidKeystorePath);
                if (Keystore.is_relative())
                    Keystore = Request.ProjectRoot / Keystore;
                const fs::file_status KeystoreStatus = fs::symlink_status(Keystore, FileError);
                if (FileError || !fs::is_regular_file(KeystoreStatus) || fs::is_symlink(KeystoreStatus) ||
                    Request.Settings.AndroidKeyAlias.empty())
                {
                    Result.Error = "Android signing requires a regular non-symlink keystore and a key alias";
                    return Result;
                }
                Keystore = fs::weakly_canonical(Keystore, FileError);
                if (FileError)
                {
                    Result.Error = "Cannot resolve the Android keystore";
                    return Result;
                }
            }
            if (!PrepareAndroidIcon(Context, Request, Project, Result.Error))
                return Result;
            std::ofstream Properties(Project / "artifact.properties", std::ios::binary | std::ios::trunc);
            if (!Properties)
            {
                Result.Error = "Cannot write Android artifact properties";
                return Result;
            }
            Properties << "applicationId=" << PropertyValue(BundleId) << '\n'
                       << "versionName=" << PropertyValue(Request.Settings.VersionName) << '\n'
                       << "versionCode=" << Request.Settings.VersionCode << '\n'
                       << "minSdk=" << Request.Settings.AndroidMinSdk << '\n'
                       << "targetSdk=" << Request.Settings.AndroidTargetSdk << '\n'
                       << "keystorePath=" << PropertyValue(UseSigning ? dbc::PathToUtf8(Keystore) : std::string{})
                       << '\n'
                       << "keyAlias=" << PropertyValue(UseSigning ? Request.Settings.AndroidKeyAlias : std::string{})
                       << '\n';
            if (!Properties.good())
            {
                Result.Error = "Cannot finish Android artifact properties";
                return Result;
            }
            Properties.close();

            std::ofstream Manifest(Project / "app/src/main/AndroidManifest.xml", std::ios::binary | std::ios::trunc);
            if (!Manifest)
            {
                Result.Error = "Cannot write the Android artifact manifest";
                return Result;
            }
            Manifest << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                     << "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n";
            for (const std::string& Permission : Request.Settings.AndroidPermissions)
                Manifest << "  <uses-permission android:name=\"android.permission." << Permission << "\"/>\n";
            Manifest << "  <uses-feature android:name=\"android.hardware.vulkan.level\" android:version=\"0\" "
                        "android:required=\"true\"/>\n"
                     << "  <application android:label=\"" << XmlValue(ProductName)
                     << "\" android:hasCode=\"true\" "
                        "android:allowBackup=\"false\" android:icon=\"@mipmap/ic_launcher\">\n"
                     << "    <activity android:name=\"net.demonixis.opendbc.host.OpenDbcActivity\" "
                        "android:configChanges=\"orientation|screenSize|screenLayout|keyboard|keyboardHidden\" "
                     << "android:screenOrientation=\"" << Request.Settings.AndroidOrientation
                     << "\" android:exported=\"true\">\n"
                     << "      <intent-filter><action android:name=\"android.intent.action.MAIN\"/>"
                        "<category android:name=\"android.intent.category.LAUNCHER\"/></intent-filter>\n"
                     << "    </activity>\n  </application>\n</manifest>\n";
            if (!Manifest.good())
            {
                Result.Error = "Cannot finish the Android artifact manifest";
                return Result;
            }
            Manifest.close();

            if (UseSigning &&
                (Request.Secrets.AndroidKeystorePassword.empty() || Request.Secrets.AndroidKeyPassword.empty()))
            {
                Result.Error = "Android signing requires the keystore and key passwords";
                return Result;
            }
            if (Request.Toolchain.JavaHome.empty() || Request.Toolchain.AndroidSdk.empty())
            {
                Result.Error = "Android packaging requires a JDK 17+ and the Android SDK";
                return Result;
            }
            std::ofstream LocalProperties(Project / "local.properties", std::ios::binary | std::ios::trunc);
            LocalProperties << "sdk.dir=" << PropertyValue(dbc::GenericPathToUtf8(Request.Toolchain.AndroidSdk))
                            << '\n';
            LocalProperties.close();
            if (!LocalProperties)
            {
                Result.Error = "Cannot write the Android SDK location";
                return Result;
            }

#if defined(_WIN32)
            const fs::path GradleScript = Project / "gradlew.bat";
#else
            const fs::path GradleScript = Project / "gradlew";
#endif
            const std::string Task = Bundle ? ":app:bundleRelease" : ":app:assembleRelease";
            ProcessRequest Gradle{
                {dbc::PathToUtf8(GradleScript), "-p", dbc::PathToUtf8(Project), "--no-daemon", "--max-workers=2", Task},
                {{"JAVA_HOME", dbc::PathToUtf8(Request.Toolchain.JavaHome)},
                 {"ANDROID_HOME", dbc::PathToUtf8(Request.Toolchain.AndroidSdk)}},
                Project};
            if (UseSigning)
            {
                Gradle.Environment.emplace_back("OPENDBC_ANDROID_KEYSTORE_PASSWORD",
                                                Request.Secrets.AndroidKeystorePassword);
                Gradle.Environment.emplace_back("OPENDBC_ANDROID_KEY_PASSWORD", Request.Secrets.AndroidKeyPassword);
            }
            Context.Note(std::string("Running Gradle ") + Task);
            if (!Context.Run(Gradle, Result.Error))
                return Result;
            const fs::path PublishSource = Project / (Bundle ? "app/build/outputs/bundle/release/app-release.aab"
                                                             : "app/build/outputs/apk/release/app-release.apk");
            if (!fs::is_regular_file(PublishSource, FileError))
            {
                Result.Error = "Android packaging completed without producing the expected artifact";
                return Result;
            }
            if (UseSigning && !VerifyAndroidSignature(Context, Request.Toolchain, PublishSource, Bundle, Result.Error))
                return Result;
            if (!UseSigning)
                Result.Messages.push_back("Development build signed with the Android debug key: it installs on a "
                                          "device, but Google Play needs a Distribution build");
            if (!PublishAndRemoveStage(PublishSource, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported the program image and " + std::to_string(Files.size()) +
                                      " asset files");
            Result.Messages.push_back("Published " + dbc::PathToUtf8(Result.OutputPath));
            return Result;
        }
    } // namespace toolchain::internal

} // namespace dbc::studio
