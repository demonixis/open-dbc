#include "core/i18n.hpp"
#include "export_internal.hpp"
#include "path_utf8.hpp"

#include <array>
#include <fstream>
#include <set>
#include <system_error>

#include <nlohmann/json.hpp>

namespace dbc::studio
{
    namespace toolchain::internal
    {
        namespace
        {
            // Where the Studio keeps the iOS runtime and the project template (tools/studio/CMakeLists.txt).
            fs::path IosHostRoot(const ArtifactExportRequest& Request)
            {
                return Request.HostResources / "export-hosts/ios";
            }

            // The xcframework slices xcodebuild -create-xcframework names, with their host manifests.
            constexpr std::array<std::pair<std::string_view, std::string_view>, 2> IosSlices = {{
                {"ios-arm64", "opendbc-host-ios.json"},
                {"ios-arm64-simulator", "opendbc-host-ios-simulator.json"},
            }};
        } // namespace

        bool ValidateIosExportHost(const ArtifactExportRequest& Request, std::string& Error)
        {
            const fs::path Root = IosHostRoot(Request);
            std::error_code FileError;
            if (!fs::is_directory(Root / "template", FileError) ||
                !fs::is_regular_file(Root / "OpenDBCRuntime.xcconfig", FileError))
            {
                Error = Tr("The installed product does not contain the qualified iOS runtime");
                return false;
            }
            for (const auto& [Slice, Manifest] : IosSlices)
            {
                if (!detail::ValidateArtifactHostIdentity(
                        Root / Manifest, Root / "OpenDBCRuntime.xcframework" / Slice / "libOpenDBCRuntime.a",
                        Request.HostResources / "opendbc-runtime.json", Error))
                    return false;
            }
            return true;
        }

#if defined(__APPLE__)
        // Build settings of the exported project; Xcode reads the bundle identity, version and team from here.
        static bool WriteGameConfiguration(const ArtifactExportRequest& Request, const std::string& ProductName,
                                           const fs::path& Project, std::string& Error)
        {
            const std::string BundleId =
                Request.Settings.BundleId.empty() ? "net.demonixis.opendbc.artifact" : Request.Settings.BundleId;
            if (!IsBundleIdentifier(BundleId))
            {
                Error = Tr("iOS bundle ID must contain at least two valid identifier segments");
                return false;
            }
            if (!IsAppleVersion(Request.Settings.VersionName) || Request.Settings.VersionCode < 1)
            {
                Error = Tr("iOS versions require one to three numeric components and a positive build number");
                return false;
            }
            if (!IsSupportedIosDeployment(Request.Settings.IosDeploymentTarget))
            {
                Error = Tr("The iOS deployment target must be version 16.0 or newer");
                return false;
            }
            if (!IsAppleTeamIdentifier(Request.Settings.IosTeamId))
            {
                Error = Tr("The iOS development team ID must contain 10 uppercase letters or digits");
                return false;
            }
            std::ofstream Output(Project / "Game.xcconfig", std::ios::binary | std::ios::trunc);
            Output << "// Written by the OpenDBC export from the project settings.\n"
                      "#include \"OpenDBCRuntime.xcconfig\"\n\n"
                   << "PRODUCT_NAME = " << ProductName << '\n'
                   << "PRODUCT_BUNDLE_IDENTIFIER = " << BundleId << '\n'
                   << "MARKETING_VERSION = " << Request.Settings.VersionName << '\n'
                   << "CURRENT_PROJECT_VERSION = " << Request.Settings.VersionCode << '\n'
                   << "IPHONEOS_DEPLOYMENT_TARGET = " << Request.Settings.IosDeploymentTarget << '\n'
                   << "DEVELOPMENT_TEAM = " << Request.Settings.IosTeamId << '\n'
                   << "CODE_SIGN_STYLE = Automatic\n"
                      "SDKROOT = iphoneos\n"
                      "SUPPORTED_PLATFORMS = iphoneos iphonesimulator\n"
                      "SUPPORTS_MACCATALYST = NO\n"
                      "TARGETED_DEVICE_FAMILY = 1,2\n"
                      "INFOPLIST_FILE = Info.plist\n"
                      "ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon\n"
                      "OTHER_LDFLAGS = $(inherited) $(OPENDBC_RUNTIME_LDFLAGS)\n";
            Output.close();
            if (!Output)
            {
                Error = Tr("Cannot write the Xcode build settings");
                return false;
            }
            return true;
        }

        static bool SetIosInfo(const ExportContext& Context, const core::ProjectSettings& Settings,
                               const fs::path& Plist, std::string& Error)
        {
            static const std::set<std::string> Orientations = {"LandscapeLeft", "LandscapeRight", "Portrait",
                                                               "PortraitUpsideDown"};
            nlohmann::json Values = nlohmann::json::array();
            for (const std::string& Orientation : Settings.IosOrientations)
            {
                if (!Orientations.contains(Orientation))
                {
                    Error = TrFmt("Unsupported iOS orientation: {0}", {Orientation});
                    return false;
                }
                Values.push_back("UIInterfaceOrientation" + Orientation);
            }
            if (Values.empty())
            {
                Error = Tr("At least one iOS orientation is required");
                return false;
            }
            const std::string Json = Values.dump();
            if (!Context.Run({"/usr/bin/plutil", "-replace", "UISupportedInterfaceOrientations", "-json", Json,
                              dbc::PathToUtf8(Plist)},
                             Error) ||
                !Context.Run({"/usr/bin/plutil", "-replace", "UISupportedInterfaceOrientations~ipad", "-json", Json,
                              dbc::PathToUtf8(Plist)},
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
                if (!Value->empty() && !Context.Run({"/usr/bin/plutil", "-insert", std::string(Key), "-string", *Value,
                                                     dbc::PathToUtf8(Plist)},
                                                    Error))
                    return false;
            }
            return true;
        }

        // The App Store takes a single opaque 1024-pixel icon; Xcode derives the other sizes.
        static bool WriteIosIcon(const ExportContext& Context, const ArtifactExportRequest& Request,
                                 const fs::path& Project, std::string& Error)
        {
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            const fs::path IconSet = Project / "Assets.xcassets/AppIcon.appiconset";
            if (!Source || !WriteScaledPng(Context, *Source, IconSet / "AppIcon.png", 1024, true, Error))
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
                Error = Tr("Cannot write the iOS app icon catalogue");
                return false;
            }
            return true;
        }
#endif

        ArtifactExportResult ExportIos(const ExportContext& Context, const ArtifactExportRequest& Request,
                                       const std::vector<std::uint8_t>& Image)
        {
            ArtifactExportResult Result;
#if !defined(__APPLE__)
            (void)Context;
            (void)Request;
            (void)Image;
            Result.Error = Tr("iOS Xcode projects can only be exported on macOS");
            return Result;
#else
            if (!ValidateIosExportHost(Request, Result.Error))
                return Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + "-Xcode");
            std::error_code FileError;
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = TrFmt("Export destination already exists: {0}", {dbc::PathToUtf8(Result.OutputPath)});
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-ios-stage-", Stage, Result.Error))
                return Result;
            StageCleanup Cleanup{Stage};
            const fs::path Host = IosHostRoot(Request);
            const fs::path Project = Stage / "project";
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!CopyTree(Host / "template", Project, Result.Error) ||
                !CopyTree(Host / "OpenDBCRuntime.xcframework", Project / "OpenDBCRuntime.xcframework", Result.Error) ||
                !CopyArtifactFile(Host / "OpenDBCRuntime.xcconfig", Project / "OpenDBCRuntime.xcconfig",
                                  Result.Error) ||
                !CopyTree(Request.HostResources / "shaders", Project / "shaders", Result.Error) ||
                !CopyArtifactFile(Request.HostResources / "NOTICE.md", Project / "NOTICE.md", Result.Error) ||
                !CopyTree(Request.HostResources / "licenses", Project / "licenses", Result.Error) ||
                !WriteArtifact(Request, Image, Project, Files, Result.Error) ||
                !WriteGameConfiguration(Request, ProductName, Project, Result.Error) ||
                !SetIosInfo(Context, Request.Settings, Project / "Info.plist", Result.Error) ||
                !WriteIosIcon(Context, Request, Project, Result.Error))
                return Result;
            fs::rename(Project / "OpenDBCGame.xcodeproj", Project / (ProductName + ".xcodeproj"), FileError);
            if (FileError)
            {
                Result.Error = TrFmt("Cannot name the Xcode project: {0}", {FileError.message()});
                return Result;
            }
            if (!PublishAndRemoveStage(Project, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back(
                TrFmt("Exported the program image and {0} asset files", {std::to_string(Files.size())}));
            Result.Messages.push_back(TrFmt("Published {0}", {dbc::PathToUtf8(Result.OutputPath)}));
            Result.Messages.push_back(
                TrFmt("Open {0}.xcodeproj in Xcode, choose your team under Signing & Capabilities, "
                      "then use Product > Archive to upload to App Store Connect",
                      {ProductName}));
            return Result;
#endif
        }
    } // namespace toolchain::internal

} // namespace dbc::studio
