#include <doctest/doctest.h>

#include "core/settings.hpp"
#include "toolchain/export_readiness.hpp"
#include "toolchain/toolchain_locator.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
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
