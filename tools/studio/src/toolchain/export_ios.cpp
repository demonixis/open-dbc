#include "export_internal.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>

#include <nlohmann/json.hpp>

namespace dbc::studio
{
    namespace toolchain::internal
    {
#if defined(__APPLE__)
        static bool ValidateIosHost(const ExportContext& Context, const fs::path& App, bool Simulator,
                                    std::string& Error)
        {
            const fs::path Executable = App / "opendbc-runner";
            if (!fs::is_regular_file(Executable))
            {
                Error = "The iOS export host has no opendbc-runner executable";
                return false;
            }
            const std::optional<std::string> Build =
                Context.Capture({"/usr/bin/xcrun", "vtool", "-show-build", Executable.string()}, Error);
            const bool IsSimulator = Build && Build->find("platform IOSSIMULATOR") != std::string::npos;
            const bool IsDevice = Build && Build->find("platform IOS\n") != std::string::npos && !IsSimulator;
            if ((Simulator && !IsSimulator) || (!Simulator && !IsDevice))
            {
                if (Error.empty())
                    Error = Simulator ? "The iOS Simulator export host is not an iphonesimulator binary"
                                      : "The iOS .ipa export host is not an iphoneos device binary";
                return false;
            }
            const std::optional<std::string> Architectures =
                Context.Capture({"/usr/bin/lipo", "-archs", Executable.string()}, Error);
            if (!Architectures || Architectures->find("arm64") == std::string::npos)
            {
                if (Error.empty())
                    Error = "The iOS .ipa export host does not contain arm64";
                return false;
            }
            return true;
        }

        static bool PrepareIosProvisioning(const ExportContext& Context, const fs::path& App,
                                           const core::ProjectSettings& Settings, bool Simulator, bool Sign,
                                           fs::path& Entitlements, std::string& Error)
        {
            Entitlements.clear();
            if (!Sign || Simulator || Settings.IosTeamId.empty())
                return true;
            const char* ProfileValue = std::getenv("OPENDBC_IOS_PROVISIONING_PROFILE");
            if (ProfileValue == nullptr || *ProfileValue == '\0')
            {
                Error = "The configured iOS team requires OPENDBC_IOS_PROVISIONING_PROFILE";
                return false;
            }
            std::error_code FileError;
            const fs::path Profile = fs::weakly_canonical(ProfileValue, FileError);
            const fs::file_status Status = fs::symlink_status(ProfileValue, FileError);
            if (FileError || !fs::is_regular_file(Status) || fs::is_symlink(Status))
            {
                Error = "The iOS provisioning profile must be a regular non-symlink file";
                return false;
            }
            const fs::path Decoded = App.parent_path() / ".opendbc-provisioning.plist";
            Entitlements = App.parent_path() / ".opendbc-entitlements.plist";
            const auto RemoveTemporary = [&]()
            {
                std::error_code Ignored;
                fs::remove(Decoded, Ignored);
                if (!Entitlements.empty())
                    fs::remove(Entitlements, Ignored);
            };
            if (!CopyArtifactFile(Profile, App / "embedded.mobileprovision", Error) ||
                !Context.Run({"/usr/bin/security", "cms", "-D", "-i", Profile.string(), "-o", Decoded.string()}, Error))
            {
                RemoveTemporary();
                return false;
            }
            const std::optional<std::string> TeamOutput = Context.Capture(
                {"/usr/bin/plutil", "-extract", "TeamIdentifier.0", "raw", "-o", "-", Decoded.string()}, Error);
            const std::optional<std::string> ApplicationIdentifierOutput =
                Context.Capture({"/usr/bin/plutil", "-extract", "Entitlements.application-identifier", "raw", "-o",
                                   "-", Decoded.string()},
                                  Error);
            const auto Trimmed = [](std::optional<std::string> Value)
            {
                std::string Result = Value.value_or(std::string{});
                while (!Result.empty() && std::isspace(static_cast<unsigned char>(Result.back())))
                    Result.pop_back();
                return Result;
            };
            const std::string Team = Trimmed(TeamOutput);
            const std::string ApplicationIdentifier = Trimmed(ApplicationIdentifierOutput);
            const std::string BundleId =
                Settings.BundleId.empty() ? "net.demonixis.opendbc.artifact" : Settings.BundleId;
            const std::string RequiredPrefix = Settings.IosTeamId + ".";
            const bool MatchesBundle =
                ApplicationIdentifier.starts_with(RequiredPrefix) &&
                (ApplicationIdentifier.substr(RequiredPrefix.size()) == BundleId ||
                 (ApplicationIdentifier.ends_with(".*") &&
                  BundleId.starts_with(ApplicationIdentifier.substr(
                      RequiredPrefix.size(), ApplicationIdentifier.size() - RequiredPrefix.size() - 1))));
            if (Team != Settings.IosTeamId || !MatchesBundle ||
                !Context.Run({"/usr/bin/plutil", "-extract", "Entitlements", "xml1", "-o", Entitlements.string(),
                             Decoded.string()},
                            Error))
            {
                if (Error.empty())
                    Error = "The iOS provisioning profile does not match the configured team and bundle ID";
                RemoveTemporary();
                return false;
            }
            if (!RemoveRequired(Decoded, Error))
            {
                RemoveTemporary();
                return false;
            }
            return true;
        }

        static bool PrepareIosIcon(const ExportContext& Context, const ArtifactExportRequest& Request,
                                   const fs::path& App, bool Simulator, std::string& Error)
        {
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            if (!Source)
                return false;
            const fs::path Catalog = App.parent_path() / ".opendbc-AppIcon.xcassets";
            const fs::path IconSet = Catalog / "AppIcon.appiconset";
            if (!WriteScaledPng(Context, *Source, IconSet / "AppIcon.png", 1024, true, Error))
                return false;
            nlohmann::json Contents;
            Contents["images"] = nlohmann::json::array(
                {{{"filename", "AppIcon.png"}, {"idiom", "universal"}, {"platform", "ios"}, {"size", "1024x1024"}}});
            Contents["info"] = {{"author", "OpenDBC"}, {"version", 1}};
            std::ofstream Json(IconSet / "Contents.json", std::ios::binary | std::ios::trunc);
            Json << Contents.dump(2) << '\n';
            Json.close();
            if (!Json)
            {
                Error = "Cannot write the iOS app icon catalogue";
                return false;
            }
            const fs::path PartialPlist = App.parent_path() / ".opendbc-icon-info.plist";
            const std::string Platform = Simulator ? "iphonesimulator" : "iphoneos";
            if (!Context.Run({"/usr/bin/xcrun",
                             "actool",
                             Catalog.string(),
                             "--compile",
                             App.string(),
                             "--app-icon",
                             "AppIcon",
                             "--platform",
                             Platform,
                             "--minimum-deployment-target",
                             Request.Settings.IosDeploymentTarget,
                             "--target-device",
                             "iphone",
                             "--target-device",
                             "ipad",
                             "--output-partial-info-plist",
                             PartialPlist.string(),
                             "--output-format",
                             "human-readable-text",
                             "--compress-pngs"},
                            Error))
                return false;
            if (!fs::is_regular_file(App / "Assets.car") || !fs::is_regular_file(App / "AppIcon60x60@2x.png") ||
                !fs::is_regular_file(App / "AppIcon76x76@2x~ipad.png"))
            {
                Error = "The iOS asset compiler did not produce a complete app icon set";
                return false;
            }
            const fs::path Plist = App / "Info.plist";
            const fs::path PhoneJson = App.parent_path() / ".opendbc-phone-icons.json";
            const fs::path IpadJson = App.parent_path() / ".opendbc-ipad-icons.json";
            if (!Context.Run({"/usr/bin/plutil", "-extract", "CFBundleIcons", "json", "-o", PhoneJson.string(),
                             PartialPlist.string()},
                            Error) ||
                !Context.Run({"/usr/bin/plutil", "-extract", "CFBundleIcons~ipad", "json", "-o", IpadJson.string(),
                             PartialPlist.string()},
                            Error))
                return false;
            const std::optional<std::string> PhoneMetadata = ReadFile(PhoneJson);
            const std::optional<std::string> IpadMetadata = ReadFile(IpadJson);
            if (!PhoneMetadata || !IpadMetadata ||
                !Context.Run({"/usr/bin/plutil", "-replace", "CFBundleIcons", "-json", *PhoneMetadata, Plist.string()},
                            Error) ||
                !Context.Run(
                    {"/usr/bin/plutil", "-replace", "CFBundleIcons~ipad", "-json", *IpadMetadata, Plist.string()},
                    Error) ||
                !RemoveRequired(Catalog, Error) || !RemoveRequired(PartialPlist, Error) ||
                !RemoveRequired(PhoneJson, Error) || !RemoveRequired(IpadJson, Error))
                return false;
            return true;
        }

        static bool SetIosMetadata(const ExportContext& Context, const fs::path& App,
                                   const core::ProjectSettings& Settings, const std::string& ProductName,
                                   bool RequireTeam, std::string& Error)
        {
            if (!SetAppleMetadata(Context, App, Settings, ProductName, Error))
                return false;
            const fs::path Plist = App / "Info.plist";
            if (!IsSupportedIosDeployment(Settings.IosDeploymentTarget))
            {
                Error = "The iOS deployment target must be version 16.0 or newer";
                return false;
            }
            if (RequireTeam && !IsAppleTeamIdentifier(Settings.IosTeamId))
            {
                Error = "The iOS development team ID must contain 10 uppercase letters or digits";
                return false;
            }

            static const std::set<std::string> Orientations = {"LandscapeLeft", "LandscapeRight", "Portrait",
                                                               "PortraitUpsideDown"};
            nlohmann::json Values = nlohmann::json::array();
            for (const std::string& Orientation : Settings.IosOrientations)
            {
                if (!Orientations.contains(Orientation))
                {
                    Error = "Unsupported iOS orientation: " + Orientation;
                    return false;
                }
                Values.push_back("UIInterfaceOrientation" + Orientation);
            }
            if (Values.empty())
            {
                Error = "At least one iOS orientation is required";
                return false;
            }
            const std::string Json = Values.dump();
            if (!Context.Run(
                    {"/usr/bin/plutil", "-replace", "UISupportedInterfaceOrientations", "-json", Json, Plist.string()},
                    Error) ||
                !Context.Run({"/usr/bin/plutil", "-replace", "UISupportedInterfaceOrientations~ipad", "-json", Json,
                             Plist.string()},
                            Error) ||
                !Context.Run({"/usr/bin/plutil", "-replace", "MinimumOSVersion", "-string",
                              Settings.IosDeploymentTarget, Plist.string()},
                            Error))
                return false;

            const std::array<std::pair<std::string_view, const std::string*>, 4> Permissions = {{
                {"NSCameraUsageDescription", &Settings.IosCameraDesc},
                {"NSMicrophoneUsageDescription", &Settings.IosMicrophoneDesc},
                {"NSLocationWhenInUseUsageDescription", &Settings.IosLocationDesc},
                {"NSPhotoLibraryUsageDescription", &Settings.IosPhotoDesc},
            }};
            for (const auto& [Key, Value] : Permissions)
            {
                if (!Value->empty() &&
                    !Context.Run({"/usr/bin/plutil", "-insert", std::string(Key), "-string", *Value, Plist.string()},
                                Error))
                    return false;
            }
            return true;
        }

        static bool PrepareIosApplication(const ExportContext& Context, const ArtifactExportRequest& Request,
                                          const std::vector<std::uint8_t>& Image, const fs::path& Host,
                                          const fs::path& App, const std::string& ProductName, bool Simulator,
                                          std::vector<dbc::interpreter::detail::ArtifactFile>& Files,
                                          std::string& Error)
        {
            if (!detail::ValidateArtifactShaders(Host / "shaders", Request.HostResources / "shaders", Error) ||
                !CopyTree(Host, App, Error) || !ValidateIosHost(Context, App, Simulator, Error))
                return false;
            fs::path Entitlements;
            if (!WriteArtifact(Request, Image, App, Files, Error) ||
                !SetIosMetadata(Context, App, Request.Settings, ProductName, Request.Sign && !Simulator, Error) ||
                !PrepareIosIcon(Context, Request, App, Simulator, Error) ||
                !PrepareIosProvisioning(Context, App, Request.Settings, Simulator, Request.Sign, Entitlements, Error))
                return false;
            const bool Signed = SignAppleBundle(Context, 
                App, Request.Sign, Simulator ? std::string_view{} : Request.Settings.IosTeamId, Entitlements, Error);
            if (!Entitlements.empty() && !RemoveRequired(Entitlements, Error))
                return false;
            return Signed;
        }
#endif

        bool ValidateIosExportHost(const ArtifactExportRequest& Request, bool Simulator, std::string& Error)
        {
            const fs::path Host =
                Request.HostResources / (Simulator ? "export-hosts/ios-simulator" : "export-hosts/ios") / "OpenDBC.app";
            std::error_code FileError;
            if (!fs::is_directory(Host, FileError))
            {
                Error = Simulator ? "The installed product does not contain the qualified iOS Simulator host"
                                  : "The installed product does not contain the qualified iOS export host";
                return false;
            }
            return detail::ValidateArtifactHostIdentity(Host.parent_path() / "opendbc-host.json",
                                                        Host / "opendbc-runner",
                                                        Request.HostResources / "opendbc-runtime.json", Error);
        }

        ArtifactExportResult ExportIos(const ExportContext& Context, const ArtifactExportRequest& Request,
                                       const std::vector<std::uint8_t>& Image)
        {
            ArtifactExportResult Result;
#if !defined(__APPLE__)
            (void)Context;
            (void)Request;
            (void)Image;
            Result.Error = "iOS artifacts can only be exported on macOS";
            return Result;
#else
            const fs::path Host = Request.HostResources / "export-hosts/ios/OpenDBC.app";
            std::error_code FileError;
            if (!ValidateIosExportHost(Request, false, Result.Error))
                return Result;
            if (Request.Sign && Request.Settings.IosTeamId.empty())
            {
                Result.Error = "Signed iOS device exports require a development team ID";
                return Result;
            }
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".ipa");
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-ios-stage-", Stage, Result.Error))
                return Result;
            StageCleanup Cleanup{Stage};
            const fs::path App = Stage / "Payload" / (ProductName + ".app");
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!PrepareIosApplication(Context, Request, Image, Host, App, ProductName, false, Files, Result.Error))
                return Result;
            const fs::path Archive = Stage / "artifact.ipa";
            if (!Context.Run({"/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent",
                             (Stage / "Payload").string(), Archive.string()},
                            Result.Error))
                return Result;
            if (!PublishAndRemoveStage(Archive, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported the program image and " + std::to_string(Files.size()) + " asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
#endif
        }

        ArtifactExportResult ExportIosSimulator(const ExportContext& Context, const ArtifactExportRequest& Request,
                                                const std::vector<std::uint8_t>& Image)
        {
            ArtifactExportResult Result;
#if !defined(__APPLE__)
            (void)Context;
            (void)Request;
            (void)Image;
            Result.Error = "iOS Simulator artifacts can only be exported on macOS";
            return Result;
#else
            const fs::path Host = Request.HostResources / "export-hosts/ios-simulator/OpenDBC.app";
            std::error_code FileError;
            if (!ValidateIosExportHost(Request, true, Result.Error))
                return Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".app");
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-ios-simulator-stage-", Stage, Result.Error))
                return Result;
            StageCleanup Cleanup{Stage};
            const fs::path App = Stage / (ProductName + ".app");
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!PrepareIosApplication(Context, Request, Image, Host, App, ProductName, true, Files, Result.Error))
                return Result;
            if (!PublishAndRemoveStage(App, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported the program image and " + std::to_string(Files.size()) + " asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
#endif
        }
    } // namespace toolchain::internal

} // namespace dbc::studio
