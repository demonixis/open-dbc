#include "asset_preview.hpp"
#include "core/i18n.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>

#include "core/gpu_texture.hpp"

namespace dbc::studio
{
    namespace
    {
        std::string ToLower(std::string S)
        {
            for (char& C : S)
            {
                C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
            }
            return S;
        }

        void FormatTime(Sint64 Milliseconds, char* Out, size_t Size)
        {
            if (Milliseconds < 0)
            {
                SDL_snprintf(Out, Size, "--:--");
                return;
            }
            const Sint64 TotalSeconds = Milliseconds / 1000;
            SDL_snprintf(Out, Size, "%02lld:%02lld", static_cast<long long>(TotalSeconds / 60),
                         static_cast<long long>(TotalSeconds % 60));
        }
    } // namespace

    namespace panels
    {
        AssetPreview::Kind AssetPreview::KindOf(const std::filesystem::path& Path)
        {
            const std::string Ext = ToLower(dbc::PathToUtf8(Path.extension()));

            if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".bmp" || Ext == ".tga" || Ext == ".gif" ||
                Ext == ".webp")
            {
                return Kind::Image;
            }
            if (Ext == ".wav" || Ext == ".ogg" || Ext == ".mp3" || Ext == ".flac" || Ext == ".mod" || Ext == ".xm")
            {
                return Kind::Audio;
            }
            return Kind::None;
        }

        void AssetPreview::Initialize(SDL_GPUDevice* Gpu)
        {
            Gpu_ = Gpu;
        }

        void AssetPreview::Shutdown()
        {
            Close();

            if (Mixer_ != nullptr)
            {
                MIX_DestroyMixer(Mixer_);
                Mixer_ = nullptr;
                MIX_Quit();
            }
        }

        void AssetPreview::Close()
        {
            ReleaseImage();
            ReleaseAudio();
            Kind_ = Kind::None;
            Path_.clear();
            Error_.clear();
        }

        void AssetPreview::ReleaseImage()
        {
            if (Texture_ != nullptr && Gpu_ != nullptr)
            {
                SDL_ReleaseGPUTexture(Gpu_, Texture_);
            }
            Texture_ = nullptr;
            Width_ = 0;
            Height_ = 0;
        }

        void AssetPreview::ReleaseAudio()
        {
            if (Track_ != nullptr)
            {
                MIX_StopTrack(Track_, 0);
                MIX_DestroyTrack(Track_);
                Track_ = nullptr;
            }
            if (Audio_ != nullptr)
            {
                MIX_DestroyAudio(Audio_);
                Audio_ = nullptr;
            }
            DurationFrames_ = 0;
        }

        bool AssetPreview::Open(const std::filesystem::path& Path)
        {
            Close();

            switch (KindOf(Path))
            {
                case Kind::Image:
                    Path_ = Path;
                    Kind_ = Kind::Image;
                    return OpenImage(Path);

                case Kind::Audio:
                    Path_ = Path;
                    Kind_ = Kind::Audio;
                    return OpenAudio(Path);

                case Kind::None:
                    return false;
            }
            return false;
        }

        bool AssetPreview::OpenImage(const std::filesystem::path& Path)
        {
            core::GpuImage Image;
            if (!core::LoadGpuImage(Gpu_, Path, Image, Error_))
                return false;
            Texture_ = Image.Texture;
            Width_ = Image.Width;
            Height_ = Image.Height;
            return true;
        }

        bool AssetPreview::OpenAudio(const std::filesystem::path& Path)
        {
            if (Mixer_ == nullptr)
            {
                if (!MIX_Init())
                {
                    Error_ = TrFmt("Cannot init mixer: {0}", {SDL_GetError()});
                    return false;
                }
                Mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
                if (Mixer_ == nullptr)
                {
                    Error_ = TrFmt("Cannot open audio device: {0}", {SDL_GetError()});
                    MIX_Quit();
                    return false;
                }
            }

            Audio_ = MIX_LoadAudio(Mixer_, dbc::PathToUtf8(Path).c_str(), false);
            if (Audio_ == nullptr)
            {
                Error_ = TrFmt("Cannot load audio: {0}", {SDL_GetError()});
                return false;
            }

            Track_ = MIX_CreateTrack(Mixer_);
            if (Track_ == nullptr || !MIX_SetTrackAudio(Track_, Audio_))
            {
                Error_ = TrFmt("Cannot create track: {0}", {SDL_GetError()});
                ReleaseAudio();
                return false;
            }

            DurationFrames_ = MIX_GetAudioDuration(Audio_);
            return true;
        }

        void AssetPreview::DrawImage()
        {
            if (Texture_ == nullptr)
            {
                return;
            }

            ImGui::Text("%d x %d", Width_, Height_);
            ImGui::Separator();

            // Fit to the panel width, never upscale.
            const float Available = ImGui::GetContentRegionAvail().x;
            const float Scale = (Width_ > 0) ? std::min(1.0f, Available / static_cast<float>(Width_)) : 1.0f;
            const ImVec2 Size(static_cast<float>(Width_) * Scale, static_cast<float>(Height_) * Scale);

            ImGui::Image(reinterpret_cast<ImTextureID>(Texture_), Size);
        }

        void AssetPreview::DrawAudio()
        {
            if (Track_ == nullptr)
            {
                return;
            }

            const bool Playing = MIX_TrackPlaying(Track_);

            if (ImGui::Button(TrId(Playing ? "Pause" : "Play")))
            {
                if (Playing)
                {
                    MIX_PauseTrack(Track_);
                }
                else if (!MIX_ResumeTrack(Track_))
                {
                    MIX_PlayTrack(Track_, 0);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(TrId("Stop")))
            {
                MIX_StopTrack(Track_, 0);
            }

            const Sint64 PositionFrames = MIX_GetTrackPlaybackPosition(Track_);
            const Sint64 PositionMs = MIX_TrackFramesToMS(Track_, PositionFrames);
            const Sint64 DurationMs = (DurationFrames_ > 0) ? MIX_AudioFramesToMS(Audio_, DurationFrames_) : -1;

            char Position[16];
            char Duration[16];
            FormatTime(PositionMs, Position, sizeof(Position));
            FormatTime(DurationMs, Duration, sizeof(Duration));

            ImGui::SameLine();
            ImGui::Text("%s / %s", Position, Duration);

            if (DurationMs > 0)
            {
                ImGui::ProgressBar(static_cast<float>(PositionMs) / static_cast<float>(DurationMs), ImVec2(-1.0f, 0.0f),
                                   "");
            }
        }

        void AssetPreview::Draw()
        {
            if (Kind_ == Kind::None)
            {
                return;
            }

            ImGui::TextUnformatted(dbc::PathToUtf8(Path_.filename()).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TrId("Close")))
            {
                Close();
                return;
            }

            if (!Error_.empty())
            {
                ImGui::TextUnformatted(Error_.c_str());
                return;
            }

            if (Kind_ == Kind::Image)
            {
                DrawImage();
            }
            else
            {
                DrawAudio();
            }
        }
    } // namespace panels

} // namespace dbc::studio
