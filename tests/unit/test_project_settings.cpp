#include <doctest/doctest.h>

#include "core/project_settings.hpp"
#include "core/settings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace dbc::studio;

namespace
{
    struct TemporaryProject
    {
        TemporaryProject()
        {
            Root = std::filesystem::temp_directory_path() /
                   ("opendbc-project-settings-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(Root);
        }

        ~TemporaryProject()
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Root, Ignored);
        }

        std::filesystem::path Root{};
    };
} // namespace

TEST_CASE("project settings preserve product metadata and unknown sections")
{
    TemporaryProject Project;
    {
        std::ofstream Output(core::ProjectSettings::FilePath(Project.Root));
        Output << R"({
            "general": {
                "appName": "Legacy Project",
                "bundleId": "net.demonixis.legacy",
                "versionName": "2.1.0",
                "versionCode": 21
            },
            "targets": {
                "desktop": true,
                "ios": true,
                "android": true,
                "cpp": true,
                "cppOutputDir": "generated"
            },
            "ios": {
                "teamId": "RETIREDTEAM"
            }
        })";
    }

    core::ProjectSettings Settings = core::ProjectSettings::Load(Project.Root);
    CHECK(Settings.AppName == "Legacy Project");
    CHECK(Settings.BundleId == "net.demonixis.legacy");
    CHECK(Settings.VersionName == "2.1.0");
    CHECK(Settings.VersionCode == 21);
    REQUIRE(Settings.Save(Project.Root));

    std::ifstream Input(core::ProjectSettings::FilePath(Project.Root));
    const std::string Saved((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    CHECK(Saved.find("\"targets\"") != std::string::npos);
    CHECK(Saved.find("cppOutputDir") != std::string::npos);
    CHECK(Saved.find("RETIREDTEAM") != std::string::npos);
}

TEST_CASE("project settings persist explicit language selection without discarding unknown keys")
{
    TemporaryProject Project;
    {
        std::ofstream Output(core::ProjectSettings::FilePath(Project.Root));
        Output
            << R"({"plugin":{"keep":42},"general":{"future":1},"ios":{"future":2,"permissions":{"future":"keep"}},"android":{"future":3},"language":{"dialect":"dbpro","projectFile":"game.dbpro","entry":"main.dba"}})";
    }
    core::ProjectSettings Settings = core::ProjectSettings::Load(Project.Root);
    CHECK(Settings.LanguageDialect == dbc::language::LanguageProfile::DbPro);
    CHECK(Settings.LanguageProjectFile == "game.dbpro");
    CHECK(Settings.LanguageEntry == "main.dba");
    Settings.LanguageDialect = dbc::language::LanguageProfile::Dbc;
    Settings.LanguageProjectFile.clear();
    REQUIRE(Settings.Save(Project.Root));

    std::ifstream Input(core::ProjectSettings::FilePath(Project.Root));
    const std::string Saved{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
    CHECK(Saved.find("\"keep\": 42") != std::string::npos);
    CHECK(Saved.find("\"future\": 1") != std::string::npos);
    CHECK(Saved.find("\"future\": 2") != std::string::npos);
    CHECK(Saved.find("\"future\": \"keep\"") != std::string::npos);
    CHECK(Saved.find("\"future\": 3") != std::string::npos);
    CHECK(Saved.find("\"dialect\": \"dbc\"") != std::string::npos);
    CHECK(Saved.find("projectFile") == std::string::npos);
}

TEST_CASE("project settings scrub legacy Android passwords")
{
    TemporaryProject Project;
    {
        std::ofstream Output(core::ProjectSettings::FilePath(Project.Root));
        Output << R"({
            "android": {
                "keystorePath": "release.jks",
                "keystorePassword": "must-not-survive",
                "keyAlias": "production",
                "keyPassword": "must-not-survive-either"
            }
        })";
    }

    core::ProjectSettings Settings = core::ProjectSettings::Load(Project.Root);
    CHECK(Settings.AndroidKeystorePath == "release.jks");
    CHECK(Settings.AndroidKeyAlias == "production");
    REQUIRE(Settings.Save(Project.Root));

    std::ifstream Input(core::ProjectSettings::FilePath(Project.Root));
    const std::string Saved((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    CHECK(Saved.find("keystorePassword") == std::string::npos);
    CHECK(Saved.find("keyPassword") == std::string::npos);
    CHECK(Saved.find("must-not-survive") == std::string::npos);
}

TEST_CASE("Studio preferences reject wrong JSON types without throwing")
{
    std::string Error;
    const auto Parsed = core::Settings::Parse(
        R"({"folder":false,"lastFile":17,"logLevel":[],"debugOverlay":"false","projectPathRemappings":{"a":2}})",
        Error);
    CHECK_FALSE(Error.empty());
    CHECK(Parsed.ProjectFolder.empty());
    CHECK(Parsed.LastFile.empty());
    CHECK(Parsed.LogLevel == "debug");
    CHECK_FALSE(Parsed.DebugOverlay);
    CHECK(Parsed.ProjectPathRemappings.empty());
    const auto Valid = core::Settings::Parse(R"({"debugOverlay":true,"folder":"project"})", Error);
    CHECK(Error.empty());
    CHECK(Valid.DebugOverlay);
    CHECK(Valid.ProjectFolder == "project");
    core::Settings::Parse("[", Error);
    CHECK_FALSE(Error.empty());
    core::Settings::Parse("[]", Error);
    CHECK_FALSE(Error.empty());
}

TEST_CASE("Invalid project settings cannot replace the original descriptor")
{
    TemporaryProject Project;
    const std::string Invalid = R"({"general":{"versionCode":18446744073709551615}})";
    {
        std::ofstream Output(core::ProjectSettings::FilePath(Project.Root));
        Output << Invalid;
    }
    const auto Settings = core::ProjectSettings::Load(Project.Root);
    CHECK_FALSE(Settings.LoadError.empty());
    CHECK_FALSE(Settings.Save(Project.Root));
    std::ifstream Input(core::ProjectSettings::FilePath(Project.Root));
    CHECK(std::string(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()) == Invalid);
    for (const std::string Text : {"[]", "[", R"({"ios":false})", R"({"android":{"permissions":[true]}})",
                                   R"({"language":{"dialect":"modern"}})", R"({"general":{"versionCode":1.5}})"})
        CHECK_FALSE(core::ProjectSettings::Parse(Text).LoadError.empty());
}
