#include <doctest/doctest.h>

#include "engine/systems/video_decoder.hpp"
#include "path_utf8.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>

namespace
{
    template <typename Predicate> bool Await(Predicate&& Reached)
    {
        const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!Reached() && std::chrono::steady_clock::now() < Deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return Reached();
    }

    std::array<std::uint8_t, 4> Pixel(dbc::VideoDecoder& Decoder)
    {
        const dbc::VideoFrame Frame = Decoder.CurrentFrame();
        if (!Frame.Pixels)
            return {};
        REQUIRE(Frame.Width == 64);
        REQUIRE(Frame.Height == 48);
        REQUIRE(Frame.Pitch >= Frame.Width * 4);
        const auto* Value = Frame.Pixels + 47 * Frame.Pitch + 63 * 4;
        return {Value[0], Value[1], Value[2], Value[3]};
    }
} // namespace

TEST_CASE("Media Foundation plays pauses restarts and finishes video from a Unicode path")
{
    const auto Root = std::filesystem::temp_directory_path() /
                      ("opendbc-video-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    struct Cleanup
    {
        std::filesystem::path Path;

        ~Cleanup()
        {
            std::error_code Error;
            std::filesystem::remove_all(Path, Error);
        }
    } Owner{Root};

    std::filesystem::create_directories(Root);
    const auto Path = Root / std::filesystem::path(u8"vid\u00e9o-\u65e5\u672c-\U0001f3ae.mp4");
    std::filesystem::copy_file(std::filesystem::path(TEST_SOURCE_DIR) / "tests/fixtures/video/red_blue.mp4", Path);
    std::string Reason;
    auto Decoder = dbc::VideoDecoder::Open(dbc::PathToUtf8(Path), Reason);
    REQUIRE_MESSAGE(Decoder, Reason);
    CHECK(Reason.empty());
    CHECK(Decoder->Width() == 64);
    CHECK(Decoder->Height() == 48);
    const auto Red = [&]
    {
        const auto P = Pixel(*Decoder);
        return P[2] > 200 && P[0] < 60;
    };
    const auto Blue = [&]
    {
        const auto P = Pixel(*Decoder);
        return P[0] > 200 && P[2] < 60;
    };
    REQUIRE(Decoder->Start());
    REQUIRE(Await(Red));
    Decoder->Pause();
    const auto Paused = Pixel(*Decoder);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    CHECK(Pixel(*Decoder) == Paused);
    CHECK_FALSE(Decoder->Finished());
    Decoder->Resume();
    REQUIRE(Await(Blue));
    REQUIRE(Await([&] { return Decoder->Finished(); }));
    CHECK(Blue());
    REQUIRE(Decoder->Start());
    REQUIRE(Await(Red));
    // Playback and its audio clock must continue while rendering is idle.
    std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    CHECK(Decoder->Finished());
    CHECK(Blue());
    REQUIRE(Decoder->Start());
    REQUIRE(Await(Red));
    Decoder.reset();
    Decoder = dbc::VideoDecoder::Open(dbc::PathToUtf8(Path), Reason);
    REQUIRE_MESSAGE(Decoder, Reason);
}

TEST_CASE("Media Foundation rejects invalid paths non-video files and audio-only files")
{
    for (const std::string Path :
         {std::string{}, std::string("bad\0path", 8), std::string("\xff"),
          std::string(TEST_SOURCE_DIR) + "/missing.mp4", std::string(TEST_SOURCE_DIR) + "/README.md",
          std::string(TEST_SOURCE_DIR) + "/tests/fixtures/classic/sound/chime.wav"})
    {
        std::string Reason;
        CHECK_FALSE(dbc::VideoDecoder::Open(Path, Reason));
        CHECK_FALSE(Reason.empty());
    }
}
