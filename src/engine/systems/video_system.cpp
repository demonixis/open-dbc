#include "video_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "log.hpp"
#include "sprite_system.hpp"
#include "video_decoder.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <limits>

namespace dbc
{
    namespace
    {
        // The Classic help limits LOAD ANIMATION to numbers 1 to 32.
        constexpr int KMaximumAnimationNumber = 32;
    } // namespace

    VideoSystem::VideoSystem(EngineKernel& Kernel) : Kernel_(Kernel)
    {
    }

    VideoSystem::~VideoSystem() = default;

    bool VideoSystem::ValidateAnimationNumber(int AnimationId, std::string* Error)
    {
        if (AnimationId < 1 || AnimationId > KMaximumAnimationNumber)
        {
            if (Error)
                *Error = "Animation number must be between 1 and 32";
            return false;
        }
        return true;
    }

    bool VideoSystem::ValidateArea(const AnimationArea& Area, const char* Command, std::string* Error)
    {
        const auto Positive = [](int First, int Last)
        {
            const auto Extent = static_cast<std::int64_t>(Last) - First;
            return Extent > 0 && Extent <= std::numeric_limits<int>::max();
        };
        if (!Positive(Area.Left, Area.Right) || !Positive(Area.Top, Area.Bottom))
        {
            if (Error)
                *Error = std::string(Command) + " requires an area with a positive width and height";
            return false;
        }
        return true;
    }

    VideoSystem::Animation* VideoSystem::RequireAnimation(int AnimationId, const char* Command, std::string* Error)
    {
        if (!ValidateAnimationNumber(AnimationId, Error))
            return nullptr;
        const auto It = Animations_.find(AnimationId);
        if (It == Animations_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing animation " + std::to_string(AnimationId);
            return nullptr;
        }
        return &It->second;
    }

    const VideoSystem::Animation* VideoSystem::RequireAnimation(int AnimationId, const char* Command,
                                                                std::string* Error) const
    {
        return const_cast<VideoSystem*>(this)->RequireAnimation(AnimationId, Command, Error);
    }

    bool VideoSystem::LoadAnimation(const std::string& Path, int AnimationId, std::string* Error)
    {
        if (!ValidateAnimationNumber(AnimationId, Error))
            return false;
        if (Animations_.contains(AnimationId))
        {
            if (Error)
                *Error = "LOAD ANIMATION cannot replace existing animation " + std::to_string(AnimationId);
            return false;
        }
        SDL_PathInfo Info{};
        if (Path.empty() || !SDL_GetPathInfo(Path.c_str(), &Info) || Info.type != SDL_PATHTYPE_FILE)
        {
            if (Error)
                *Error = "LOAD ANIMATION cannot find '" + Path + "'";
            return false;
        }

        Animation Value;
        std::string Reason;
        Value.Decoder = VideoDecoder::Open(Path, Reason);
        if (Value.Decoder)
        {
            Value.Width = Value.Decoder->Width();
            Value.Height = Value.Decoder->Height();
        }
        else
        {
            // Period AVI codecs are common in Classic programs; they keep running without the video.
            DBC_LOG_WARN("LOAD ANIMATION: animation {} stays inert, '{}' cannot be played: {}", AnimationId, Path,
                         Reason);
        }
        Animations_.emplace(AnimationId, std::move(Value));
        return true;
    }

    bool VideoSystem::DeleteAnimation(int AnimationId, std::string* Error)
    {
        if (!RequireAnimation(AnimationId, "DELETE ANIMATION", Error))
            return false;
        // The last picture stays in its bitmap, as in DarkBASIC.
        Animations_.erase(AnimationId);
        return true;
    }

    bool VideoSystem::PlayAnimation(int AnimationId, bool Loop, std::optional<int> BitmapId,
                                    std::optional<AnimationArea> Area, std::string* Error)
    {
        const char* Command = Loop ? "LOOP ANIMATION" : "PLAY ANIMATION";
        Animation* Value = RequireAnimation(AnimationId, Command, Error);
        if (!Value)
            return false;
        if (Area && !ValidateArea(*Area, Command, Error))
            return false;
        ImageStore& Store = Kernel_.ImageStoreRef();
        const int Target = BitmapId.value_or(Store.CurrentBitmap());
        if (Target < 0 || Target > 32 || (Target > 0 && !Store.GetBitmapSurface(Target)))
        {
            if (Error)
                *Error = std::string(Command) + " requires existing bitmap " + std::to_string(Target);
            return false;
        }

        Value->BitmapId = Target;
        if (Area)
        {
            Value->X = Area->Left;
            Value->Y = Area->Top;
            Value->Width = Area->Right - Area->Left;
            Value->Height = Area->Bottom - Area->Top;
        }
        Value->Looping = Loop;
        Value->Paused = false;
        Value->Playing = Value->Decoder && Value->Decoder->Start();
        return true;
    }

    bool VideoSystem::PlayAnimationAt(int AnimationId, int X, int Y, std::string* Error)
    {
        Animation* Value = RequireAnimation(AnimationId, "PLAY ANIMATION", Error);
        if (!Value)
            return false;
        if (!PlayAnimation(AnimationId, false, std::nullopt, std::nullopt, Error))
            return false;
        Value->X = X;
        Value->Y = Y;
        return true;
    }

    bool VideoSystem::PlaceAnimation(int AnimationId, const AnimationArea& Area, std::string* Error)
    {
        Animation* Value = RequireAnimation(AnimationId, "PLACE ANIMATION", Error);
        if (!Value || !ValidateArea(Area, "PLACE ANIMATION", Error))
            return false;
        Value->X = Area.Left;
        Value->Y = Area.Top;
        Value->Width = Area.Right - Area.Left;
        Value->Height = Area.Bottom - Area.Top;
        return true;
    }

    bool VideoSystem::StopAnimation(int AnimationId, std::string* Error)
    {
        Animation* Value = RequireAnimation(AnimationId, "STOP ANIMATION", Error);
        if (!Value)
            return false;
        if (Value->Playing)
            Value->Decoder->Pause();
        Value->Playing = false;
        Value->Paused = false;
        Value->Looping = false;
        return true;
    }

    bool VideoSystem::PauseAnimation(int AnimationId, std::string* Error)
    {
        Animation* Value = RequireAnimation(AnimationId, "PAUSE ANIMATION", Error);
        if (!Value)
            return false;
        if (Value->Playing && !Value->Paused)
        {
            Value->Decoder->Pause();
            Value->Paused = true;
        }
        return true;
    }

    bool VideoSystem::ResumeAnimation(int AnimationId, std::string* Error)
    {
        Animation* Value = RequireAnimation(AnimationId, "RESUME ANIMATION", Error);
        if (!Value)
            return false;
        if (Value->Paused)
        {
            Value->Decoder->Resume();
            Value->Paused = false;
        }
        return true;
    }

    bool VideoSystem::AnimationExist(int& Output, int AnimationId, std::string* Error) const
    {
        if (!ValidateAnimationNumber(AnimationId, Error))
            return false;
        Output = Animations_.contains(AnimationId) ? 1 : 0;
        return true;
    }

    bool VideoSystem::AnimationPlaying(int& Output, int AnimationId, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, "ANIMATION PLAYING", Error);
        if (!Value)
            return false;
        Output = Value->Playing && !Value->Paused ? 1 : 0;
        return true;
    }

    bool VideoSystem::AnimationPaused(int& Output, int AnimationId, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, "ANIMATION PAUSED", Error);
        if (!Value)
            return false;
        Output = Value->Paused ? 1 : 0;
        return true;
    }

    bool VideoSystem::AnimationLooping(int& Output, int AnimationId, const char* Command, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, Command, Error);
        if (!Value)
            return false;
        Output = Value->Looping ? 1 : 0;
        return true;
    }

    bool VideoSystem::AnimationPositionX(int& Output, int AnimationId, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, "ANIMATION POSITION X", Error);
        if (!Value)
            return false;
        Output = Value->X;
        return true;
    }

    bool VideoSystem::AnimationPositionY(int& Output, int AnimationId, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, "ANIMATION POSITION Y", Error);
        if (!Value)
            return false;
        Output = Value->Y;
        return true;
    }

    bool VideoSystem::AnimationWidth(int& Output, int AnimationId, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, "ANIMATION WIDTH", Error);
        if (!Value)
            return false;
        Output = Value->Width;
        return true;
    }

    bool VideoSystem::AnimationHeight(int& Output, int AnimationId, std::string* Error) const
    {
        const Animation* Value = RequireAnimation(AnimationId, "ANIMATION HEIGHT", Error);
        if (!Value)
            return false;
        Output = Value->Height;
        return true;
    }

    void VideoSystem::PaintFrames()
    {
        for (auto& [AnimationId, Value] : Animations_)
        {
            if (!Value.Playing)
                continue;
            // Repainted while paused too: a 3D frame clears the screen bitmap.
            if (!Paint(AnimationId, Value))
            {
                Value.Decoder->Pause();
                Value.Playing = false;
                Value.Paused = false;
                continue;
            }
            if (!Value.Paused && Value.Decoder->Finished())
            {
                if (Value.Looping)
                    Value.Playing = Value.Decoder->Start();
                else
                    Value.Playing = false;
            }
        }
    }

    bool VideoSystem::Paint(int AnimationId, Animation& Value)
    {
        const VideoFrame Frame = Value.Decoder->CurrentFrame();
        if (!Frame.Pixels || Value.Width <= 0 || Value.Height <= 0)
            return true;

        ImageStore& Store = Kernel_.ImageStoreRef();
        std::string Error;
        SDL_Surface* Target = nullptr;
        if (Value.BitmapId == 0)
        {
            const int Current = Store.CurrentBitmap();
            if (Kernel_.Sprites().MaterializePresentedBobTrails(&Error))
                Target = Store.EnsureScreenBitmap(&Error);
            // Allocating the screen bitmap selects it; the program's current bitmap must not change.
            if (Current != Store.CurrentBitmap())
                (void)Store.SetCurrentBitmap(Current);
        }
        else
        {
            Target = Store.GetBitmapSurface(Value.BitmapId);
            if (!Target)
                Error = "bitmap " + std::to_string(Value.BitmapId) + " no longer exists";
        }
        if (!Target)
        {
            DBC_LOG_WARN("Animation {} stopped: {}", AnimationId, Error);
            return false;
        }

        SDL_Surface* Source = SDL_CreateSurfaceFrom(Frame.Width, Frame.Height, SDL_PIXELFORMAT_BGRA32,
                                                    const_cast<std::uint8_t*>(Frame.Pixels), Frame.Pitch);
        const SDL_Rect Destination{Value.X, Value.Y, Value.Width, Value.Height};
        const bool Painted = Source && SDL_SetSurfaceBlendMode(Source, SDL_BLENDMODE_NONE) &&
                             SDL_BlitSurfaceScaled(Source, nullptr, Target, &Destination, SDL_SCALEMODE_LINEAR);
        if (!Painted)
            DBC_LOG_WARN("Animation {} stopped: painting failed: {}", AnimationId, SDL_GetError());
        SDL_DestroySurface(Source);
        Store.InvalidateBitmapGpuTexture(Value.BitmapId);
        return Painted;
    }

} // namespace dbc
