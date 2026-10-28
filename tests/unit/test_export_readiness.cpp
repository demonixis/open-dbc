#include <doctest/doctest.h>

#include "core/settings.hpp"
#include "toolchain/export_readiness.hpp"
#include "toolchain/toolchain_locator.hpp"
#include "toolchain/windows_resources.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace dbc::studio;

namespace
{
    namespace fs = std::filesystem;

    struct ToolTree
    {
        explicit ToolTree(std::string_view Name)
        {
            Root = fs::temp_directory_path() /
                   (std::string(Name) + "-" + std::to_string(SDL_GetTicksNS()) + "-" + std::to_string(Counter++));
            fs::create_directories(Root);
        }

        ~ToolTree()
        {
            std::error_code Ignored;
            fs::remove_all(Root, Ignored);
        }

        void Write(const fs::path& Relative, std::string_view Text = {}) const
        {
            fs::create_directories((Root / Relative).parent_path());
            std::ofstream(Root / Relative, std::ios::binary) << Text;
        }

        fs::path Jdk(const fs::path& Relative, std::string_view Version) const
        {
#if defined(_WIN32)
            Write(Relative / "bin/java.exe");
            Write(Relative / "bin/keytool.exe");
#else
            Write(Relative / "bin/java");
            Write(Relative / "bin/keytool");
#endif
            Write(Relative / "release", "IMPLEMENTOR=\"Test\"\nJAVA_VERSION=\"" + std::string(Version) + "\"\n");
            return Root / Relative;
        }

        static inline int Counter = 0;
        fs::path Root;
    };

    toolchain::ToolchainSearch Search(std::map<std::string, std::string> Variables)
    {
        toolchain::ToolchainSearch Result;
        Result.Variable = [Variables = std::move(Variables)](std::string_view Name) -> std::optional<std::string>
        {
            const auto Found = Variables.find(std::string(Name));
            if (Found == Variables.end())
                return std::nullopt;
            return Found->second;
        };
        return Result;
    }

    bool HasFailed(const std::vector<toolchain::ExportCheck>& Checks, std::string_view Label)
    {
        return std::any_of(Checks.begin(), Checks.end(), [Label](const toolchain::ExportCheck& Check)
                           { return Check.Label == Label && !Check.Passed; });
    }
} // namespace

TEST_CASE("JDK detection requires java, keytool and Java 17 or newer")
{
    ToolTree Tree("opendbc-jdk");
    const fs::path Old = Tree.Jdk("old", "1.8.0_402");
    const fs::path Current = Tree.Jdk("roots/jdk-21/Contents/Home", "21.0.4");
    CHECK_FALSE(toolchain::IsJavaHome(Old));
    CHECK(toolchain::IsJavaHome(Current));

    // JAVA_HOME pointing at Java 8 is skipped in favour of a detected JDK 21.
    toolchain::ToolchainSearch Paths = Search({{"JAVA_HOME", Old.string()}});
    Paths.JdkRoots = {Tree.Root / "roots"};
    const auto Found = toolchain::LocateJavaHome({}, Paths);
    REQUIRE(Found);
    CHECK(fs::equivalent(*Found, Current));

    // A configured folder is never replaced by a detected one.
    CHECK_FALSE(toolchain::LocateJavaHome(Old, Paths));
    CHECK(toolchain::LocateJavaHome(Current, Paths) == Current);
}

TEST_CASE("Android SDK detection follows ANDROID_HOME, then ANDROID_SDK_ROOT, then known folders")
{
    ToolTree Tree("opendbc-android-sdk");
    Tree.Write("home/platforms/android-36/android.jar");
    Tree.Write("root/build-tools/35.0.0/aapt2");
    Tree.Write("known/platforms/android-35/android.jar");
    toolchain::ToolchainSearch Paths = Search(
        {{"ANDROID_HOME", (Tree.Root / "missing").string()}, {"ANDROID_SDK_ROOT", (Tree.Root / "root").string()}});
    Paths.AndroidSdkRoots = {Tree.Root / "known"};
    const auto Found = toolchain::LocateAndroidSdk({}, Paths);
    REQUIRE(Found);
    CHECK(fs::equivalent(*Found, Tree.Root / "root"));
    CHECK(toolchain::LocateAndroidSdk(Tree.Root / "home", Paths) == Tree.Root / "home");
    CHECK_FALSE(toolchain::LocateAndroidSdk(Tree.Root / "missing", Paths));
}

TEST_CASE("the newest Android build tools are chosen by version, not by name")
{
    ToolTree Tree("opendbc-build-tools");
    for (const char* Version : {"9.0.0", "35.0.1", "36.0.0", "34.0.0"})
        Tree.Write(fs::path("build-tools") / Version / "apksigner");
    const auto Latest = toolchain::LatestAndroidBuildTools(Tree.Root);
    REQUIRE(Latest);
    CHECK(Latest->filename() == "36.0.0");
    CHECK(toolchain::AndroidSdkInstallCommand(36) == "sdkmanager \"platforms;android-36\" \"build-tools;36.0.0\"");
}

TEST_CASE("Android readiness names what is missing before a signed build")
{
    ToolTree Tree("opendbc-android-readiness");
    toolchain::ArtifactExportRequest Request;
    Request.Target = toolchain::ArtifactTarget::AndroidBundle;
    Request.ProjectRoot = Tree.Root / "project";
    Request.HostResources = Tree.Root / "product";
    Request.Sign = true;
    std::vector<toolchain::ExportCheck> Checks = toolchain::CheckExportReadiness(Request);
    CHECK_FALSE(toolchain::ReadyToExport(Checks));
    CHECK(HasFailed(Checks, "Export host matches this Studio"));
    CHECK(HasFailed(Checks, "JDK 17 or newer"));
    CHECK(HasFailed(Checks, "Android SDK"));
    CHECK(HasFailed(Checks, "Release keystore"));
    CHECK(HasFailed(Checks, "Keystore passwords"));

    Tree.Write("sdk/platforms/android-35/android.jar");
    Tree.Write("project/release.keystore", "keystore");
    Request.Toolchain.JavaHome = Tree.Jdk("jdk", "17.0.2");
    Request.Toolchain.AndroidSdk = Tree.Root / "sdk";
    Request.Settings.AndroidKeystorePath = "release.keystore";
    Request.Secrets.AndroidKeystorePassword = "store";
    Request.Secrets.AndroidKeyPassword = "key";
    Checks = toolchain::CheckExportReadiness(Request);
    CHECK_FALSE(HasFailed(Checks, "JDK 17 or newer"));
    CHECK_FALSE(HasFailed(Checks, "Release keystore"));
    CHECK_FALSE(HasFailed(Checks, "Keystore passwords"));
    // A missing SDK platform only warns: Gradle installs it once its licence is accepted.
    const auto Platform = std::find_if(Checks.begin(), Checks.end(), [](const toolchain::ExportCheck& Check)
                                       { return Check.Label.starts_with("Android platform"); });
    REQUIRE(Platform != Checks.end());
    CHECK_FALSE(Platform->Passed);
    CHECK_FALSE(Platform->Required);
    CHECK(Platform->Detail.find("sdkmanager") != std::string::npos);

    Request.Sign = false;
    Checks = toolchain::CheckExportReadiness(Request);
    CHECK(std::none_of(Checks.begin(), Checks.end(),
                       [](const toolchain::ExportCheck& Check) { return Check.Label == "Keystore passwords"; }));
}

TEST_CASE("the export toolchain keeps configured tools and detects the others")
{
    ToolTree Tree("opendbc-export-toolchain");
    const fs::path Jdk = Tree.Jdk("jdk", "21");
    Tree.Write("sdk/platforms/android-36/android.jar");
    core::Settings Settings;
    Settings.JavaHome = Jdk.string();
    Settings.NotaryProfile = "opendbc-notary";
    toolchain::ToolchainSearch Paths = Search({{"ANDROID_HOME", (Tree.Root / "sdk").string()}});
    const toolchain::ExportToolchain Toolchain = toolchain::ResolveExportToolchain(Settings, Paths);
    CHECK(Toolchain.JavaHome == Jdk);
    CHECK(fs::equivalent(Toolchain.AndroidSdk, Tree.Root / "sdk"));
    CHECK(Toolchain.NotaryProfile == "opendbc-notary");
    CHECK(Toolchain.WindowsTimestampUrl == "http://timestamp.digicert.com");

    // Build machines override the preferences through the environment.
    Paths = Search({{"OPENDBC_NOTARY_PROFILE", "ci-notary"}});
    CHECK(toolchain::ResolveExportToolchain(Settings, Paths).NotaryProfile == "ci-notary");
}

TEST_CASE("Studio settings keep export tools and per-project export folders")
{
    std::string Error;
    const core::Settings Parsed = core::Settings::Parse(
        R"({"javaHome":"/jdk","androidSdk":"/sdk","notaryProfile":"notary","exportFolders":{"/p":"/p/exports"}})",
        Error);
    CHECK(Error.empty());
    CHECK(Parsed.JavaHome == "/jdk");
    CHECK(Parsed.AndroidSdk == "/sdk");
    CHECK(Parsed.NotaryProfile == "notary");
    REQUIRE(Parsed.ExportFolders.contains("/p"));
    CHECK(Parsed.ExportFolders.at("/p") == "/p/exports");

    core::Settings::Parse(R"({"exportFolders":[]})", Error);
    CHECK_FALSE(Error.empty());
}

TEST_CASE("code signing identities are read from security find-identity")
{
    const std::vector<toolchain::SigningIdentity> Identities = toolchain::ParseSigningIdentities(
        "  1) 1111111111111111111111111111111111111111 \"Apple Development: Jane Doe (AAAAAAAAAA)\"\n"
        "  2) 2222222222222222222222222222222222222222 \"Developer ID Application: Jane Doe (BBBBBBBBBB)\"\n"
        "  3) 2222222222222222222222222222222222222222 \"Developer ID Application: Jane Doe (BBBBBBBBBB)\"\n"
        "     2 valid identities found\n");
    REQUIRE(Identities.size() == 2);
    CHECK(Identities[0].Sha1 == "1111111111111111111111111111111111111111");
    CHECK(Identities[1].Name == "Developer ID Application: Jane Doe (BBBBBBBBBB)");
}

#if defined(__APPLE__)
TEST_CASE("signed macOS builds need an installed Developer ID identity and warn without notarization")
{
    toolchain::ArtifactExportRequest Request;
    Request.Target = toolchain::ArtifactTarget::Desktop;
    Request.Sign = true;
    Request.Toolchain.MacSigningIdentity = "Developer ID Application: Nobody (0000000000)";
    std::vector<toolchain::ExportCheck> Checks = toolchain::CheckExportReadiness(Request);
    CHECK(HasFailed(Checks, "Developer ID identity"));
    const auto Notary = std::find_if(Checks.begin(), Checks.end(), [](const toolchain::ExportCheck& Check)
                                     { return Check.Label == "Notarization profile"; });
    REQUIRE(Notary != Checks.end());
    CHECK_FALSE(Notary->Passed);
    CHECK_FALSE(Notary->Required);

    Request.Sign = false;
    Checks = toolchain::CheckExportReadiness(Request);
    CHECK(std::none_of(Checks.begin(), Checks.end(),
                       [](const toolchain::ExportCheck& Check) { return Check.Label == "Developer ID identity"; }));
}
#endif

TEST_CASE("certificate thumbprints are normalized and malformed ones rejected")
{
    CHECK(toolchain::NormalizeCertificateThumbprint("0a1b2c3d4e5f6a7b8c9d0a1b2c3d4e5f6a7b8c9d") ==
          "0A1B2C3D4E5F6A7B8C9D0A1B2C3D4E5F6A7B8C9D");
    CHECK_FALSE(toolchain::NormalizeCertificateThumbprint("certificate.pfx"));
    CHECK_FALSE(toolchain::NormalizeCertificateThumbprint("0a1b"));
}

TEST_CASE("signtool comes from OPENDBC_SIGNTOOL or the newest Windows SDK")
{
    ToolTree Tree("opendbc-windows-sdk");
    Tree.Write("bin/10.0.19041.0/x64/signtool.exe");
    Tree.Write("bin/10.0.26100.0/x64/signtool.exe");
    Tree.Write("bin/10.0.30000.0/x86/signtool.exe");
    toolchain::ToolchainSearch Paths = Search({});
    Paths.WindowsSdkBinRoots = {Tree.Root / "bin"};
    CHECK(toolchain::LocateSigntool(Paths) == Tree.Root / "bin/10.0.26100.0/x64/signtool.exe");
    Paths = Search({{"OPENDBC_SIGNTOOL", "C:/Tools/signtool.exe"}});
    CHECK(toolchain::LocateSigntool(Paths) == fs::path("C:/Tools/signtool.exe"));
}

namespace
{
    std::uint32_t ReadDword(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
    {
        return std::uint32_t{Bytes[Offset]} | (std::uint32_t{Bytes[Offset + 1]} << 8) |
               (std::uint32_t{Bytes[Offset + 2]} << 16) | (std::uint32_t{Bytes[Offset + 3]} << 24);
    }

    std::vector<std::uint8_t> Utf16Bytes(std::string_view Ascii)
    {
        std::vector<std::uint8_t> Bytes;
        for (const char Character : Ascii)
        {
            Bytes.push_back(static_cast<std::uint8_t>(Character));
            Bytes.push_back(0);
        }
        return Bytes;
    }

    bool Contains(const std::vector<std::uint8_t>& Bytes, const std::vector<std::uint8_t>& Part)
    {
        return std::search(Bytes.begin(), Bytes.end(), Part.begin(), Part.end()) != Bytes.end();
    }
} // namespace

TEST_CASE("the Windows version resource is a VS_VERSIONINFO tree with the game's identity")
{
    toolchain::WindowsVersionInfo Info;
    Info.ProductName = "My Game";
    Info.CompanyName = "Studio";
    Info.Version = "1.2.3";
    Info.Build = 7;
    Info.OriginalFilename = "My Game.exe";
    const std::vector<std::uint8_t> Bytes = toolchain::BuildVersionResource(Info);
    REQUIRE(Bytes.size() > 100);
    CHECK((Bytes[0] | (Bytes[1] << 8)) == static_cast<int>(Bytes.size()));
    CHECK((Bytes[2] | (Bytes[3] << 8)) == 52);
    CHECK(Contains(Bytes, Utf16Bytes("VS_VERSION_INFO")));
    // The fixed file info follows the key, aligned on 32 bits: 6 header bytes + 16 UTF-16 characters.
    CHECK(ReadDword(Bytes, 40) == 0xfeef04bdu);
    CHECK(ReadDword(Bytes, 48) == 0x00010002u);
    CHECK(ReadDword(Bytes, 52) == 0x00030007u);
    CHECK(Contains(Bytes, Utf16Bytes("ProductName")));
    CHECK(Contains(Bytes, Utf16Bytes("My Game.exe")));
    CHECK(Contains(Bytes, Utf16Bytes("1.2.3.7")));
    CHECK(Contains(Bytes, Utf16Bytes("Translation")));
}

TEST_CASE("the Windows icon group lists each PNG image with its resource ID")
{
    const std::vector<toolchain::WindowsIconImage> Images = {{256, std::vector<std::uint8_t>(300)},
                                                             {32, std::vector<std::uint8_t>(40)}};
    const std::vector<std::uint8_t> Group = toolchain::BuildIconGroupResource(Images, 5);
    REQUIRE(Group.size() == 6 + 2 * 14);
    CHECK(Group[2] == 1);
    CHECK(Group[4] == 2);
    CHECK(Group[6] == 0);
    CHECK(Group[6 + 14] == 32);
    CHECK(ReadDword(Group, 6 + 8) == 300);
    CHECK((Group[6 + 12] | (Group[6 + 13] << 8)) == 5);
    CHECK((Group[6 + 14 + 12] | (Group[6 + 14 + 13] << 8)) == 6);
}
