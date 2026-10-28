#include "export_internal.hpp"
#include "toolchain_locator.hpp"
#include "windows_resources.hpp"

#include <array>
#include <fstream>
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

        bool StageDesktopFolder(const ArtifactExportRequest& Request, const std::vector<std::uint8_t>& Image,
                                const fs::path& Stage, const fs::path& Executable, std::string_view XrLoader,
                                std::vector<dbc::interpreter::detail::ArtifactFile>& Files, std::string& Error)
        {
            return CopyArtifactFile(Request.HostProduct, Executable, Error) &&
                   CopyArtifactFile(Request.HostResources / XrLoader, Stage / XrLoader, Error) &&
                   CopyTree(Request.HostResources / "shaders", Stage / "shaders", Error) &&
                   CopyArtifactFile(Request.HostResources / "NOTICE.md", Stage / "NOTICE.md", Error) &&
                   CopyTree(Request.HostResources / "licenses", Stage / "licenses", Error) &&
                   WriteArtifact(Request, Image, Stage, Files, Error);
        }

        std::vector<std::string> WindowsSignArguments(const fs::path& Signtool, const ExportToolchain& Toolchain,
                                                      const fs::path& File)
        {
            const std::string Thumbprint =
                NormalizeCertificateThumbprint(Toolchain.WindowsCertificate).value_or(std::string{});
            return {Signtool.string(), "sign", "/fd",       "SHA256",   "/td",        "SHA256",
                    "/tr",             Toolchain.WindowsTimestampUrl, "/sha1", Thumbprint, File.string()};
        }

        bool FinishWindowsFolder(const ExportContext& Context, const ArtifactExportRequest& Request,
                                 const fs::path& Stage, const fs::path& Executable, const std::string& ProductName,
                                 std::string& Error)
        {
            const std::optional<fs::path> Icon = ResolveArtifactIcon(Request, true, Error);
            if (!Icon)
                return false;
            std::vector<WindowsIconImage> Icons;
            for (const int Size : {256, 64, 48, 32, 16})
            {
                const fs::path Png = Stage / (".opendbc-icon-" + std::to_string(Size) + ".png");
                const StageCleanup PngCleanup{Png};
                if (!WriteScaledPng(Context, *Icon, Png, Size, false, Error))
                    return false;
                const std::optional<std::string> Bytes = ReadFile(Png);
                if (!Bytes)
                {
                    Error = "Cannot read the generated Windows icon";
                    return false;
                }
                Icons.push_back({Size, std::vector<std::uint8_t>(Bytes->begin(), Bytes->end())});
            }
            WindowsVersionInfo Version;
            Version.ProductName = ProductName;
            Version.CompanyName = Request.Metadata.Company;
            Version.Description = Request.Metadata.Description;
            Version.Copyright = Request.Metadata.Copyright;
            Version.Version = Request.Settings.VersionName;
            Version.Build = Request.Settings.VersionCode;
            Version.OriginalFilename = Executable.filename().string();
            if (!UpdateWindowsExecutableResources(Executable, Icons, Version, Error))
                return false;
            if (!Request.Sign)
                return true;
            const std::optional<fs::path> Signtool = LocateSigntool(ToolchainSearch::Host());
            if (!Signtool)
            {
                Error = "Signing Windows executables needs signtool from the Windows SDK";
                return false;
            }
            if (!NormalizeCertificateThumbprint(Request.Toolchain.WindowsCertificate))
            {
                Error = "Signing Windows executables needs the SHA-1 thumbprint of a code signing certificate";
                return false;
            }
            Context.Note("Signing with certificate " + Request.Toolchain.WindowsCertificate);
            for (const fs::path& File : {Executable, Stage / "openxr_loader.dll"})
            {
                if (!Context.Run(WindowsSignArguments(*Signtool, Request.Toolchain, File), Error) ||
                    !Context.Run({Signtool->string(), "verify", "/pa", File.string()}, Error))
                    return false;
            }
            return true;
        }

        std::string LinuxDesktopEntry(const std::string& ProductName)
        {
            // Exec and Icon name the files in this folder; installers rewrite them to the installed paths.
            return "[Desktop Entry]\nType=Application\nName=" + ProductName + "\nExec=\"" + ProductName +
                   "\"\nIcon=" + ProductName + "\nTerminal=false\nCategories=Game;\n";
        }

        bool FinishLinuxFolder(const ExportContext& Context, const ArtifactExportRequest& Request,
                               const fs::path& Stage, const std::string& ProductName, std::string& Error)
        {
            const std::optional<fs::path> Icon = ResolveArtifactIcon(Request, true, Error);
            if (!Icon || !WriteScaledPng(Context, *Icon, Stage / (ProductName + ".png"), 256, false, Error))
                return false;
            std::ofstream Entry(Stage / (ProductName + ".desktop"), std::ios::binary | std::ios::trunc);
            Entry << LinuxDesktopEntry(ProductName);
            Entry.close();
            if (!Entry)
            {
                Error = "Cannot write the Linux desktop entry";
                return false;
            }
            return true;
        }

        std::vector<std::string> DesktopArchiveArguments(const fs::path& OutputDirectory,
                                                         const std::string& ProductName, const fs::path& Archive)
        {
            // bsdtar on Windows 10+ and GNU tar both pick the format from the archive name.
            return {"tar", "-a", "-cf", Archive.string(), "-C", OutputDirectory.string(), ProductName};
        }

        ArtifactExportResult ExportDesktop(const ExportContext& Context, const ArtifactExportRequest& Request,
                                           const std::vector<std::uint8_t>& Image)
        {
            ArtifactExportResult Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
#if defined(__APPLE__)
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".app");
            const fs::path Archive = Request.OutputDirectory / (ProductName + ".zip");
#elif defined(_WIN32)
            Result.OutputPath = Request.OutputDirectory / ProductName;
            const fs::path Archive = Request.OutputDirectory / (ProductName + ".zip");
#else
            Result.OutputPath = Request.OutputDirectory / ProductName;
            const fs::path Archive = Request.OutputDirectory / (ProductName + ".tar.gz");
#endif
            std::error_code FileError;
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            if (Request.Sign && fs::exists(Archive, FileError))
            {
                Result.Error = "Export destination already exists: " + Archive.string();
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
            const fs::path App = Stage / (ProductName + ".app");
            const fs::path Resources = App / "Contents/Resources";
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!CopyTree(Request.HostProduct, App, Result.Error) ||
                !CopyArtifactFile(Request.HostResources / "NOTICE.md", Resources / "NOTICE.md", Result.Error) ||
                !CopyTree(Request.HostResources / "licenses", Resources / "licenses", Result.Error) ||
                !WriteArtifact(Request, Image, Resources, Files, Result.Error))
                return Result;
            if (!PrepareMacIcon(Context, Request, App, Result.Error) ||
                !SetAppleMetadata(Context, App, Request.Settings, ProductName, Result.Error))
                return Result;
            const bool Notarize = Request.Sign && !Request.Toolchain.NotaryProfile.empty();
            if (Request.Sign)
            {
                Context.Note("Signing with " + Request.Toolchain.MacSigningIdentity);
                if (!SignMacDistribution(Context, App, Request.Toolchain.MacSigningIdentity, Result.Error) ||
                    (Notarize && !NotarizeMacApp(Context, App, Request.Toolchain.NotaryProfile, Result.Error)))
                    return Result;
            }
            else if (!AdHocSignAppleBundle(Context, App, Result.Error))
                return Result;
            if (!PublishAndRemoveStage(App, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported the program image and " + std::to_string(Files.size()) + " asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            if (Request.Sign)
            {
                // A zip keeps the signature and the stapled ticket for direct downloads.
                if (!Context.Run({"/usr/bin/ditto", "-c", "-k", "--keepParent", Result.OutputPath.string(),
                                  Archive.string()},
                                 Result.Error))
                    return Result;
                Result.Messages.push_back("Published " + Archive.string());
                if (!Notarize)
                    Result.Messages.push_back("Signed but not notarized: other Macs will block it until it is "
                                              "notarized with a notary profile");
            }
#else
#if defined(_WIN32)
            const fs::path Executable = Stage / (ProductName + ".exe");
            constexpr std::string_view XrLoader = "openxr_loader.dll";
#else
            const fs::path Executable = Stage / ProductName;
            constexpr std::string_view XrLoader = "libopenxr_loader.so.1";
#endif
            std::vector<dbc::interpreter::detail::ArtifactFile> Files;
            if (!StageDesktopFolder(Request, Image, Stage, Executable, XrLoader, Files, Result.Error))
                return Result;
#if defined(_WIN32)
            if (!FinishWindowsFolder(Context, Request, Stage, Executable, ProductName, Result.Error))
                return Result;
#else
            if (!FinishLinuxFolder(Context, Request, Stage, ProductName, Result.Error))
                return Result;
#endif
            if (!PublishNoReplace(Stage, Result.OutputPath, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported the program image and " + std::to_string(Files.size()) + " asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            if (Request.Sign)
            {
                if (!Context.Run(DesktopArchiveArguments(Request.OutputDirectory, ProductName, Archive), Result.Error))
                    return Result;
                Result.Messages.push_back("Published " + Archive.string());
            }
#endif
            return Result;
        }
    } // namespace toolchain::internal

} // namespace dbc::studio
