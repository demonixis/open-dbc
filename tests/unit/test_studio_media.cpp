#include <doctest/doctest.h>

#include "core/media_catalogue.hpp"

#include <filesystem>
#include <fstream>

using namespace dbc::studio;

TEST_CASE("Studio media are classified by extension")
{
    CHECK(core::ClassifyMedia("a/Logo.PNG") == core::MediaKind::Image);
    CHECK(core::ClassifyMedia("theme.mid") == core::MediaKind::Audio);
    CHECK(core::ClassifyMedia("mech.X") == core::MediaKind::Model);
    CHECK(core::ClassifyMedia("bump.fx") == core::MediaKind::Effect);
    CHECK(core::ClassifyMedia("intro.avi") == core::MediaKind::Video);
    CHECK(core::ClassifyMedia("hud.ttf") == core::MediaKind::Font);
    CHECK_FALSE(core::ClassifyMedia("game.dba"));
    CHECK_FALSE(core::ClassifyMedia("readme"));
}

TEST_CASE("Studio media scan skips hidden entries and honours the limit")
{
    const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-media";
    std::error_code Error;
    std::filesystem::remove_all(Root, Error);
    std::filesystem::create_directories(Root / "media" / "sounds", Error);
    std::filesystem::create_directories(Root / ".git", Error);
    REQUIRE_FALSE(Error);
    for (const char* Name : {"media/sounds/shot.wav", "media/ship.x", "main.dba", ".git/icon.png", ".hidden.png"})
        std::ofstream(Root / Name) << "data";

    const core::MediaScan Scan = core::ScanMedia(Root, 10);
    REQUIRE(Scan.Files.size() == 2);
    CHECK_FALSE(Scan.Truncated);
    CHECK(Scan.Files[0].RelativeName == "media/ship.x");
    CHECK(Scan.Files[0].Kind == core::MediaKind::Model);
    CHECK(Scan.Files[0].Size == 4);
    CHECK(Scan.Files[1].RelativeName == "media/sounds/shot.wav");

    const core::MediaScan Limited = core::ScanMedia(Root, 1);
    CHECK(Limited.Files.size() == 1);
    CHECK(Limited.Truncated);

    std::filesystem::remove_all(Root, Error);
}
