#include "core_system.hpp"
#include "log.hpp"
#include "video_decoder.hpp"

#include <SDL3/SDL_hints.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <limits>
#include <utility>

namespace dbc
{
    namespace
    {
        constexpr GstClockTime KLoadTimeout = 10 * GST_SECOND;

        std::string Describe(GError* Error, const char* Fallback)
        {
            std::string Reason = Error ? Error->message : Fallback;
            g_clear_error(&Error);
            return Reason;
        }

        const std::string& InitializationFailure()
        {
            // GStreamer is process-wide; do not deinitialize it when an animation is deleted.
            static const std::string Failure = []
            {
                GError* Error = nullptr;
                if (!gst_init_check(nullptr, nullptr, &Error))
                    return Describe(Error, "GStreamer initialization failed");
                return std::string{};
            }();
            return Failure;
        }
    } // namespace

    struct VideoDecoder::Impl
    {
        GstElement* Player = nullptr;
        GstElement* Sink = nullptr;
        GstBus* Bus = nullptr;
        GstSample* Sample = nullptr;
        GstVideoFrame Frame{};
        bool Mapped = false;
        bool Ended = false;
        bool FailureLogged = false;
        bool WarningLogged = false;
        std::string Failure;
        int Width = 0;
        int Height = 0;

        ~Impl()
        {
            ClearFrame();
            if (Player)
                gst_element_set_state(Player, GST_STATE_NULL);
            if (Bus)
                gst_object_unref(Bus);
            if (Player)
                gst_object_unref(Player);
            if (Sink)
                gst_object_unref(Sink);
        }

        void ClearFrame()
        {
            if (Mapped)
                gst_video_frame_unmap(&Frame);
            Mapped = false;
            if (Sample)
                gst_sample_unref(Sample);
            Sample = nullptr;
        }

        bool SetFrame(GstSample* Next)
        {
            ClearFrame();
            Sample = Next;
            GstCaps* Caps = gst_sample_get_caps(Sample);
            GstBuffer* Buffer = gst_sample_get_buffer(Sample);
            GstVideoInfo Info{};
            if (!Caps || !Buffer || !gst_video_info_from_caps(&Info, Caps) ||
                GST_VIDEO_INFO_FORMAT(&Info) != GST_VIDEO_FORMAT_BGRA || GST_VIDEO_INFO_WIDTH(&Info) <= 0 ||
                GST_VIDEO_INFO_WIDTH(&Info) > std::numeric_limits<int>::max() / 4 || GST_VIDEO_INFO_HEIGHT(&Info) <= 0)
                return false;
            Mapped = gst_video_frame_map(&Frame, &Info, Buffer, GST_MAP_READ);
            return Mapped && GST_VIDEO_FRAME_PLANE_DATA(&Frame, 0) &&
                   GST_VIDEO_FRAME_PLANE_STRIDE(&Frame, 0) >= GST_VIDEO_INFO_WIDTH(&Info) * 4;
        }

        void ReadMessages()
        {
            // Polling avoids a GLib main loop alongside SDL's event loop.
            while (GstMessage* Message = gst_bus_pop(Bus))
            {
                if (GST_MESSAGE_TYPE(Message) == GST_MESSAGE_ERROR && Failure.empty())
                {
                    GError* Error = nullptr;
                    gst_message_parse_error(Message, &Error, nullptr);
                    Failure = Describe(Error, "GStreamer playback failed");
                }
                else if (GST_MESSAGE_TYPE(Message) == GST_MESSAGE_EOS)
                    Ended = true;
                else if (GST_MESSAGE_TYPE(Message) == GST_MESSAGE_WARNING && !WarningLogged)
                {
                    GError* Error = nullptr;
                    gst_message_parse_warning(Message, &Error, nullptr);
                    DBC_LOG_WARN("Video playback warning: {}",
                                 Describe(Error, "GStreamer reported a playback warning"));
                    WarningLogged = true;
                }
                gst_message_unref(Message);
            }
        }

        void CheckPlayback()
        {
            ReadMessages();
            if (!Failure.empty() && !FailureLogged)
            {
                DBC_LOG_WARN("Video playback stopped: {}", Failure);
                FailureLogged = true;
                Ended = true;
                ClearFrame();
                gst_element_set_state(Player, GST_STATE_NULL);
            }
        }

        void SetState(GstState State)
        {
            CheckPlayback();
            if (Failure.empty() && gst_element_set_state(Player, State) == GST_STATE_CHANGE_FAILURE)
            {
                ReadMessages();
                if (Failure.empty())
                    Failure = "GStreamer could not change the playback state";
            }
            CheckPlayback();
        }
    };

    VideoDecoder::VideoDecoder(std::unique_ptr<Impl> Value) : Impl_(std::move(Value)) {}

    VideoDecoder::~VideoDecoder() = default;

    std::unique_ptr<VideoDecoder> VideoDecoder::Open(const std::string& Path, std::string& Reason)
    {
        Reason = InitializationFailure();
        if (!Reason.empty())
            return nullptr;
        if (Path.empty() || Path.find('\0') != std::string::npos)
        {
            Reason = "the video path is empty or contains a null byte";
            return nullptr;
        }

        GError* Error = nullptr;
        gchar* Uri = gst_filename_to_uri(Path.c_str(), &Error);
        if (!Uri)
        {
            Reason = Describe(Error, "GStreamer could not resolve the video path");
            return nullptr;
        }

        auto Value = std::make_unique<Impl>();
        Value->Player = gst_element_factory_make("playbin", nullptr);
        Value->Sink = gst_element_factory_make("appsink", nullptr);
        if (Value->Player)
            gst_object_ref_sink(Value->Player);
        if (Value->Sink)
            gst_object_ref_sink(Value->Sink);
        if (!Value->Player || !Value->Sink)
        {
            g_free(Uri);
            Reason = "GStreamer requires the playback and app plugins";
            return nullptr;
        }
        Value->Bus = gst_element_get_bus(Value->Player);

        GstCaps* Caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", nullptr);
        gst_app_sink_set_caps(GST_APP_SINK(Value->Sink), Caps);
        gst_caps_unref(Caps);
        // Keep the newest due picture without blocking audio when BASIC renders slowly.
        g_object_set(Value->Sink, "sync", TRUE, "max-buffers", 1U, "drop", TRUE, "enable-last-sample", FALSE,
                     "wait-on-eos", FALSE, nullptr);
        // GstPlayFlags: video and audio only; no subtitles or visualization.
        constexpr guint KPlaybackFlags = (1U << 0) | (1U << 1);
        g_object_set(Value->Player, "uri", Uri, "video-sink", Value->Sink, "flags", KPlaybackFlags, "current-video", 0,
                     "current-audio", 0, nullptr);
        g_free(Uri);

        if (SDL_GetHintBoolean(KHeadlessHint, false))
        {
            GstElement* Audio = gst_element_factory_make("fakesink", nullptr);
            if (!Audio)
            {
                Reason = "GStreamer requires the coreelements plugin for headless playback";
                return nullptr;
            }
            gst_object_ref_sink(Audio);
            g_object_set(Audio, "sync", TRUE, nullptr);
            g_object_set(Value->Player, "audio-sink", Audio, nullptr);
            gst_object_unref(Audio);
        }

        // LOAD ANIMATION needs dimensions immediately. Only opening waits; frame reads never block.
        const bool Requested = gst_element_set_state(Value->Player, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE;
        const auto State =
            Requested ? gst_element_get_state(Value->Player, nullptr, nullptr, KLoadTimeout) : GST_STATE_CHANGE_FAILURE;
        Value->ReadMessages();
        if (!Value->Failure.empty() || State != GST_STATE_CHANGE_SUCCESS)
        {
            Reason = !Value->Failure.empty() ? Value->Failure : "GStreamer did not open the video in time";
            return nullptr;
        }
        GstSample* Preroll = gst_app_sink_try_pull_preroll(GST_APP_SINK(Value->Sink), 0);
        if (!Preroll || !Value->SetFrame(Preroll))
        {
            Reason = "GStreamer found no decodable BGRA video track";
            return nullptr;
        }
        Value->Width = GST_VIDEO_FRAME_WIDTH(&Value->Frame);
        Value->Height = GST_VIDEO_FRAME_HEIGHT(&Value->Frame);
        return std::unique_ptr<VideoDecoder>(new VideoDecoder(std::move(Value)));
    }

    int VideoDecoder::Width() const
    {
        return Impl_->Width;
    }

    int VideoDecoder::Height() const
    {
        return Impl_->Height;
    }

    bool VideoDecoder::Start()
    {
        Impl_->CheckPlayback();
        if (!Impl_->Failure.empty())
            return false;
        Impl_->Ended = false;
        if (!gst_element_seek_simple(Impl_->Player, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0))
        {
            Impl_->Failure = "GStreamer could not restart the video";
            Impl_->CheckPlayback();
            return false;
        }
        Impl_->SetState(GST_STATE_PLAYING);
        return Impl_->Failure.empty();
    }

    void VideoDecoder::Pause()
    {
        Impl_->SetState(GST_STATE_PAUSED);
    }

    void VideoDecoder::Resume()
    {
        Impl_->SetState(GST_STATE_PLAYING);
    }

    bool VideoDecoder::Finished() const
    {
        Impl_->CheckPlayback();
        return Impl_->Ended;
    }

    VideoFrame VideoDecoder::CurrentFrame()
    {
        if (Impl_->Failure.empty())
        {
            GstSample* Next = gst_app_sink_try_pull_sample(GST_APP_SINK(Impl_->Sink), 0);
            if (Next && !Impl_->SetFrame(Next))
                Impl_->Failure = "GStreamer could not map a BGRA video frame";
        }
        Impl_->CheckPlayback();
        if (!Impl_->Mapped || !Impl_->Failure.empty())
            return {};
        return {static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&Impl_->Frame, 0)),
                GST_VIDEO_FRAME_WIDTH(&Impl_->Frame), GST_VIDEO_FRAME_HEIGHT(&Impl_->Frame),
                GST_VIDEO_FRAME_PLANE_STRIDE(&Impl_->Frame, 0)};
    }

} // namespace dbc
