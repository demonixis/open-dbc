#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace dbc
{
    class EngineKernel;
    class VideoDecoder;

    // Area of an animation in its bitmap, right and bottom exclusive as in the Classic commands.
    struct AnimationArea
    {
        int Left = 0;
        int Top = 0;
        int Right = 0;
        int Bottom = 0;
    };

    // DarkBASIC Classic animations: video files played into the screen or a bitmap. Each rendered
    // frame paints the current picture into the target, so the last one stays there once stopped.
    class VideoSystem
    {
      public:
        explicit VideoSystem(EngineKernel& Kernel);
        ~VideoSystem();

        VideoSystem(const VideoSystem&) = delete;
        VideoSystem& operator=(const VideoSystem&) = delete;

        // A file the platform cannot decode loads as an inert animation that never plays.
        bool LoadAnimation(const std::string& Path, int AnimationId, std::string* Error = nullptr);
        bool DeleteAnimation(int AnimationId, std::string* Error = nullptr);
        // Plays from the first frame. Without a bitmap the animation plays into the current one; a
        // position moves the area and keeps its size.
        bool PlayAnimation(int AnimationId, bool Loop, std::optional<int> BitmapId = std::nullopt,
                           std::optional<AnimationArea> Area = std::nullopt, std::string* Error = nullptr);
        bool PlayAnimationAt(int AnimationId, int X, int Y, std::string* Error = nullptr);
        bool PlaceAnimation(int AnimationId, const AnimationArea& Area, std::string* Error = nullptr);
        bool StopAnimation(int AnimationId, std::string* Error = nullptr);
        bool PauseAnimation(int AnimationId, std::string* Error = nullptr);
        bool ResumeAnimation(int AnimationId, std::string* Error = nullptr);

        bool AnimationExist(int& Output, int AnimationId, std::string* Error = nullptr) const;
        bool AnimationPlaying(int& Output, int AnimationId, std::string* Error = nullptr) const;
        bool AnimationPaused(int& Output, int AnimationId, std::string* Error = nullptr) const;
        bool AnimationLooping(int& Output, int AnimationId, const char* Command, std::string* Error = nullptr) const;
        bool AnimationPositionX(int& Output, int AnimationId, std::string* Error = nullptr) const;
        bool AnimationPositionY(int& Output, int AnimationId, std::string* Error = nullptr) const;
        bool AnimationWidth(int& Output, int AnimationId, std::string* Error = nullptr) const;
        bool AnimationHeight(int& Output, int AnimationId, std::string* Error = nullptr) const;

        // Advances playback and paints every playing animation; called once per rendered frame.
        void PaintFrames();

      private:
        struct Animation
        {
            // Null when the platform cannot decode the file.
            std::unique_ptr<VideoDecoder> Decoder;
            int BitmapId = 0;
            int X = 0;
            int Y = 0;
            int Width = 0;
            int Height = 0;
            // Started and not yet stopped or finished; a paused animation is still playing.
            bool Playing = false;
            bool Paused = false;
            bool Looping = false;
        };

        static bool ValidateAnimationNumber(int AnimationId, std::string* Error);
        static bool ValidateArea(const AnimationArea& Area, const char* Command, std::string* Error);
        Animation* RequireAnimation(int AnimationId, const char* Command, std::string* Error);
        const Animation* RequireAnimation(int AnimationId, const char* Command, std::string* Error) const;
        bool Paint(int AnimationId, Animation& Value);

        EngineKernel& Kernel_;
        std::unordered_map<int, Animation> Animations_;
    };

} // namespace dbc
