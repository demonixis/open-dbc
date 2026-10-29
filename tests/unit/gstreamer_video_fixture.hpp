#pragma once

#include <doctest/doctest.h>
#include <gst/gst.h>

#include <chrono>
#include <filesystem>
#include <memory>

namespace dbc::tests
{
    struct GStreamerVideoFixture
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() /
            ("opendbc-video-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        explicit GStreamerVideoFixture(bool Compressed = false)
        {
            REQUIRE(std::filesystem::create_directory(Root));
            REQUIRE(gst_init_check(nullptr, nullptr, nullptr));
            // Raw AVI with PCM audio needs no codec library, including FFmpeg. Odd widths exercise row padding.
            GError* Error = nullptr;
            const std::string Encoding = Compressed ? "videoconvert ! jpegenc ! " : "";
            const std::string Description =
                "concat name=frames ! video/x-raw,format=BGR,width=65,height=48,framerate=10/1 ! " + Encoding +
                "avimux name=mux ! filesink name=output "
                "videotestsrc num-buffers=10 pattern=solid-color foreground-color=0xffff0000 ! "
                "video/x-raw,format=BGR,width=65,height=48,framerate=10/1 ! frames. "
                "videotestsrc num-buffers=10 pattern=solid-color foreground-color=0xff0000ff ! "
                "video/x-raw,format=BGR,width=65,height=48,framerate=10/1 ! frames. "
                "audiotestsrc num-buffers=20 samplesperbuffer=800 wave=silence ! "
                "audio/x-raw,format=S16LE,rate=8000,channels=1 ! mux.";
            GstElement* Raw = gst_parse_launch(Description.c_str(), &Error);
            const std::string Reason = Error ? Error->message : "";
            g_clear_error(&Error);
            const auto Destroy = [](GstElement* Pipeline)
            {
                gst_element_set_state(Pipeline, GST_STATE_NULL);
                gst_object_unref(Pipeline);
            };
            std::unique_ptr<GstElement, decltype(Destroy)> Pipeline(Raw, Destroy);
            REQUIRE_MESSAGE(Pipeline != nullptr, Reason);
            REQUIRE_MESSAGE(Reason.empty(), Reason);
            GstElement* Output = gst_bin_get_by_name(GST_BIN(Raw), "output");
            REQUIRE(Output != nullptr);
            g_object_set(Output, "location", (Root / "red_blue.avi").c_str(), nullptr);
            gst_object_unref(Output);
            REQUIRE(gst_element_set_state(Raw, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
            GstBus* Bus = gst_element_get_bus(Raw);
            GstMessage* Message = gst_bus_timed_pop_filtered(
                Bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            gst_object_unref(Bus);
            REQUIRE(Message != nullptr);
            const bool Finished = GST_MESSAGE_TYPE(Message) == GST_MESSAGE_EOS;
            if (!Finished)
                gst_message_parse_error(Message, &Error, nullptr);
            const std::string Failure = Error ? Error->message : "";
            g_clear_error(&Error);
            gst_message_unref(Message);
            REQUIRE_MESSAGE(Finished, Failure);
        }

        ~GStreamerVideoFixture()
        {
            std::error_code Error;
            std::filesystem::remove_all(Root, Error);
        }
    };
} // namespace dbc::tests
