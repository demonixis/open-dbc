#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace dbc
{
    // Latest decoded picture, 8-bit BGRA rows. Pixels is null until the first frame arrives and
    // stays valid until the next VideoDecoder call.
    struct VideoFrame
    {
        const std::uint8_t* Pixels = nullptr;
        int Width = 0;
        int Height = 0;
        int Pitch = 0;
    };

    // Plays the first video track of a file with the operating system's media framework, sound
    // included. Each platform provides its own implementation; a platform without one opens nothing.
    class VideoDecoder
    {
      public:
        ~VideoDecoder();

        VideoDecoder(const VideoDecoder&) = delete;
        VideoDecoder& operator=(const VideoDecoder&) = delete;

        // Returns null with a reason when the platform cannot decode the file.
        static std::unique_ptr<VideoDecoder> Open(const std::string& Path, std::string& Reason);

        int Width() const;
        int Height() const;

        // Starts playback from the first frame.
        void Start();
        void Pause();
        void Resume();
        // True once playback reached the last frame since the latest Start.
        bool Finished() const;
        // Fetches the picture due at the current playback time.
        VideoFrame CurrentFrame();

      private:
        struct Impl;
        explicit VideoDecoder(std::unique_ptr<Impl> Value);

        std::unique_ptr<Impl> Impl_;
    };

} // namespace dbc
