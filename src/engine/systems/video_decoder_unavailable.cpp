#include "video_decoder.hpp"

#include <utility>

// Platforms whose media framework is not integrated yet: every animation stays inert.

namespace dbc
{
    struct VideoDecoder::Impl
    {
    };

    VideoDecoder::VideoDecoder(std::unique_ptr<Impl> Value) : Impl_(std::move(Value))
    {
    }

    VideoDecoder::~VideoDecoder() = default;

    std::unique_ptr<VideoDecoder> VideoDecoder::Open(const std::string&, std::string& Reason)
    {
        Reason = "video playback is not available on this platform yet";
        return nullptr;
    }

    int VideoDecoder::Width() const
    {
        return 0;
    }

    int VideoDecoder::Height() const
    {
        return 0;
    }

    bool VideoDecoder::Start()
    {
        return false;
    }

    void VideoDecoder::Pause()
    {
    }

    void VideoDecoder::Resume()
    {
    }

    bool VideoDecoder::Finished() const
    {
        return true;
    }

    VideoFrame VideoDecoder::CurrentFrame()
    {
        return {};
    }

} // namespace dbc
