#include "export_internal.hpp"

#include <array>
#include <system_error>

namespace dbc::studio
{
    namespace toolchain::internal
    {
#if defined(__APPLE__)
        static bool PrepareMacIcon(const ExportContext& Context, const ArtifactExportRequest& Request,
                                   const fs::path& App, std::string& Error)
        {
            if (Request.Settings.IconSource.empty())
                return true;
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            if (!Source)
                return false;
            const fs::path IconSet = App / "Contents/Resources/.opendbc.iconset";
            static const std::array<std::pair<std::string_view, int>, 10> Icons = {{
                {"icon_16x16.png", 16},
                {"icon_16x16@2x.png", 32},
                {"icon_32x32.png", 32},
                {"icon_32x32@2x.png", 64},
                {"icon_128x128.png", 128},
                {"icon_128x128@2x.png", 256},
                {"icon_256x256.png", 256},
                {"icon_256x256@2x.png", 512},
                {"icon_512x512.png", 512},
                {"icon_512x512@2x.png", 1024},
            }};
            for (const auto& [Name, Size] : Icons)
            {
                if (!WriteScaledPng(Context, *Source, IconSet / Name, Size, false, Error))
                    return false;
            }
            const fs::path Icon = App / "Contents/Resources/artifact.icns";
            if (!Context.Run({"/usr/bin/iconutil", "-c", "icns", "-o", Icon.string(), IconSet.string()}, Error) ||
                !RemoveRequired(IconSet, Error))
                return false;
            const fs::path Plist = App / "Contents/Info.plist";
            return Context.Run(
                {"/usr/bin/plutil", "-replace", "CFBundleIconFile", "-string", "artifact.icns", Plist.string()}, Error);
        }
#endif

        // The host is opendbc-runner, built with this Studio; its manifest binds it to the runtime the program
        // image is compiled for.
        bool ValidateDesktopHost(const ArtifactExportRequest& Request, std::string& Error)
        {
#if defined(__APPLE__)
            const fs::path HostResources = Request.HostProduct / "Contents/Resources";
            const fs::path HostBinary = Request.HostProduct / "Contents/MacOS/opendbc-runner";
            const fs::path HostManifest = HostResources / "opendbc-host.json";
#else
            const fs::path HostResources = Request.HostResources;
            const fs::path HostBinary = Request.HostProduct;
            const fs::path HostManifest = Request.HostProduct.parent_path() / "opendbc-runner-host.json";
#endif
            return detail::ValidateArtifactHostIdentity(HostManifest, HostBinary,
                                                        Request.HostResources / "opendbc-runtime.json", Error) &&
                   detail::ValidateArtifactShaders(HostResources / "shaders", Request.HostResources / "shaders", Error);
        }

        ArtifactExportResult ExportDesktop(const ExportContext& Context, const ArtifactExportRequest& Request,
                                           const std::vector<std::uint8_t>& Image)
        {
            ArtifactExportResult Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
#if defined(__APPLE__)
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".app");
#else
            Result.OutputPath = Request.OutputDirectory / ProductName;
#endif
            std::error_code FileError;
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            if (!ValidateDesktopHost(Request, Result.Error))
                return Result;
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-stage-", Stage, Result.Error))
                return Result;
            StageCleanup Cleanup{Stage};
#if defined(__APPLE__)
            if (!CopyTree(Request.HostProduct, Stage, Result.Error))
                return Result;
            const fs::path Resources = Stage / "Contents/Resources";
#else
            if (!Request.Settings.IconSource.empty())
            {
                Result.Error = "Custom Desktop artifact icons are currently supported only on macOS";
                return Result;
            }
            if (!CopyArtifactFile(Request.HostProduct, Stage / Request.HostProduct.filename(), Result.Error))
                return Result;
            const fs::path Resources = Stage;
#if defined(_WIN32)
            constexpr auto XrLoader = "openxr_loader.dll";
#else
            constexpr auto XrLoader = "libopenxr_loader.so.1";
#endif
            if (!CopyArtifactFile(Request.HostResources / XrLoader, Resources / XrLoader, Result.Error) ||
                !CopyTree(Request.HostResources / "shaders", Resources / "shaders", Result.Error))
                return Result;
#endif
            if (!CopyArtifactFile(Request.HostResources / "NOTICE.md", Resources / "NOTICE.md", Result.Error) ||
                !CopyTree(Request.HostResources / "licenses", Resources / "licenses", Result.Error))
                return Result;
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!WriteArtifact(Request, Image, Resources, Files, Result.Error))
                return Result;
#if defined(__APPLE__)
            if (!PrepareMacIcon(Context, Request, Stage, Result.Error))
                return Result;
            if (!SetAppleMetadata(Context, Stage, Request.Settings, ProductName, Result.Error))
                return Result;
#endif
            if (!SignAppleBundle(Context, Stage, Request.Sign, {}, {}, Result.Error))
                return Result;
            if (!PublishNoReplace(Stage, Result.OutputPath, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported the program image and " + std::to_string(Files.size()) + " asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
        }
    } // namespace toolchain::internal

} // namespace dbc::studio
