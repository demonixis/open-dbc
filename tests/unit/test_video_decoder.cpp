#include "engine/systems/video_decoder.hpp"
#include "gstreamer_video_fixture.hpp"

#include <array>
#include <fstream>
#include <thread>

namespace
{
    void DisableLibav()
    {
        REQUIRE(gst_init_check(nullptr, nullptr, nullptr));
        // Disable every libav factory, even when the optional system plugin is installed.
        GList* Features = gst_registry_get_feature_list_by_plugin(gst_registry_get(), "libav");
        for (GList* Entry = Features; Entry; Entry = Entry->next)
            gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(Entry->data), GST_RANK_NONE);
        gst_plugin_feature_list_free(Features);
    }

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
        REQUIRE(Frame.Pixels != nullptr);
        REQUIRE(Frame.Width == 65);
        REQUIRE(Frame.Height == 48);
        REQUIRE(Frame.Pitch >= Frame.Width * 4);
        // Read the last row and column as well as checking the negotiated dimensions and pitch.
        const auto* Value = Frame.Pixels + 47 * Frame.Pitch + 64 * 4;
        return {Value[0], Value[1], Value[2], Value[3]};
    }
} // namespace

TEST_CASE("GStreamer plays, pauses, restarts and finishes video with PCM audio without libav")
{
    DisableLibav();
    const dbc::tests::GStreamerVideoFixture Fixture;

    std::string Reason;
    auto Decoder = dbc::VideoDecoder::Open((Fixture.Root / "red_blue.avi").string(), Reason);
    REQUIRE_MESSAGE(Decoder != nullptr, Reason);
    CHECK(Reason.empty());
    CHECK(Decoder->Width() == 65);
    CHECK(Decoder->Height() == 48);
    CHECK_FALSE(Decoder->Finished());
    const auto Red = [&] { return Pixel(*Decoder) == std::array<std::uint8_t, 4>{0, 0, 255, 255}; };
    const auto Blue = [&] { return Pixel(*Decoder) == std::array<std::uint8_t, 4>{255, 0, 0, 255}; };
    CHECK(Red());

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
    CHECK_FALSE(Decoder->Finished());
    REQUIRE(Await(Red));
    // A slow renderer must not stall the pipeline or audio waiting for an appsink consumer.
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    CHECK(Decoder->Finished());
    CHECK(Blue());

    REQUIRE(Decoder->Start());
    REQUIRE(Await(Red));
    Decoder.reset();
    // Destruction during playback must release the pipeline and permit reopening it.
    Decoder = dbc::VideoDecoder::Open((Fixture.Root / "red_blue.avi").string(), Reason);
    REQUIRE_MESSAGE(Decoder != nullptr, Reason);
}

TEST_CASE("GStreamer decodes Motion JPEG video without libav")
{
    DisableLibav();
    const dbc::tests::GStreamerVideoFixture Fixture(true);
    std::string Reason;
    auto Decoder = dbc::VideoDecoder::Open((Fixture.Root / "red_blue.avi").string(), Reason);
    REQUIRE_MESSAGE(Decoder != nullptr, Reason);
    REQUIRE(Decoder->Start());
    CHECK(Await([&] { return Pixel(*Decoder)[2] > 240 && Pixel(*Decoder)[0] < 15; }));
    CHECK(Await([&] { return Pixel(*Decoder)[0] > 240 && Pixel(*Decoder)[2] < 15; }));
}

TEST_CASE("GStreamer rejects invalid files, audio-only files and unknown video codecs with a reason")
{
    const dbc::tests::GStreamerVideoFixture Fixture;
    std::string Reason;
    CHECK_FALSE(dbc::VideoDecoder::Open("", Reason));
    CHECK_FALSE(Reason.empty());
    CHECK_FALSE(dbc::VideoDecoder::Open(std::string("bad\0path", 8), Reason));
    CHECK_FALSE(Reason.empty());
    CHECK_FALSE(dbc::VideoDecoder::Open((Fixture.Root / "missing.avi").string(), Reason));
    CHECK_FALSE(Reason.empty());
    CHECK_FALSE(dbc::VideoDecoder::Open(std::string(TEST_SOURCE_DIR) + "/README.md", Reason));
    CHECK_FALSE(Reason.empty());
    CHECK_FALSE(
        dbc::VideoDecoder::Open(std::string(TEST_SOURCE_DIR) + "/tests/fixtures/classic/sound/chime.wav", Reason));
    CHECK_FALSE(Reason.empty());

    std::ifstream Input(std::string(TEST_SOURCE_DIR) + "/tests/fixtures/video/cinepak.avi", std::ios::binary);
    REQUIRE(Input.good());
    std::string Bytes{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
    std::size_t Replaced = 0;
    for (std::size_t Position = Bytes.find("cvid"); Position != std::string::npos; Position = Bytes.find("cvid"))
    {
        Bytes.replace(Position, 4, "DBCV");
        ++Replaced;
    }
    REQUIRE(Replaced > 0);
    const auto Unknown = Fixture.Root / "unknown.avi";
    {
        std::ofstream Output(Unknown, std::ios::binary);
        Output.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
        REQUIRE(Output.good());
    }
    CHECK_FALSE(dbc::VideoDecoder::Open(Unknown.string(), Reason));
    CHECK_FALSE(Reason.empty());
}

// Run separately with an empty plugin search path; it must not inherit another case's initialized registry.
TEST_CASE("GStreamer reports missing playback plugins" * doctest::skip())
{
    std::string Reason;
    CHECK_FALSE(dbc::VideoDecoder::Open(std::string(TEST_SOURCE_DIR) + "/tests/fixtures/video/red_blue.mp4", Reason));
    CHECK(Reason == "GStreamer requires the playback and app plugins");
}
