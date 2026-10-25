#include "asset_preview.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

#include <SDL3_image/SDL_image.h>

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
            const std::string Ext = ToLower(Path.extension().string());

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
            if (Gpu_ == nullptr)
            {
                Error_ = "No GPU device";
                return false;
            }

            SDL_Surface* Loaded = IMG_Load(Path.string().c_str());
            if (Loaded == nullptr)
            {
                Error_ = std::string("Cannot load image: ") + SDL_GetError();
                return false;
            }

            SDL_Surface* Rgba = SDL_ConvertSurface(Loaded, SDL_PIXELFORMAT_ABGR8888);
            SDL_DestroySurface(Loaded);
            if (Rgba == nullptr)
            {
                Error_ = std::string("Cannot convert image: ") + SDL_GetError();
                return false;
            }

            Width_ = Rgba->w;
            Height_ = Rgba->h;

            SDL_GPUTextureCreateInfo Info = {};
            Info.type = SDL_GPU_TEXTURETYPE_2D;
            Info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            Info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            Info.width = static_cast<Uint32>(Width_);
            Info.height = static_cast<Uint32>(Height_);
            Info.layer_count_or_depth = 1;
            Info.num_levels = 1;

            Texture_ = SDL_CreateGPUTexture(Gpu_, &Info);
            if (Texture_ == nullptr)
            {
                SDL_DestroySurface(Rgba);
                Error_ = std::string("Cannot create texture: ") + SDL_GetError();
                return false;
            }

            // Upload through a staging buffer, then wait: previews are opened rarely
            // and this keeps the frame loop free of pending transfers.
            const std::uint64_t RequiredBytes = static_cast<std::uint64_t>(Width_) * Height_ * 4u;
            if (Width_ <= 0 || Height_ <= 0 || RequiredBytes > std::numeric_limits<Uint32>::max())
            {
                SDL_DestroySurface(Rgba);
                ReleaseImage();
                Error_ = "Image is too large for a GPU transfer";
                return false;
            }
            const Uint32 ByteCount = static_cast<Uint32>(RequiredBytes);

            SDL_GPUTransferBufferCreateInfo TransferInfo = {};
            TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            TransferInfo.size = ByteCount;

            SDL_GPUTransferBuffer* Transfer = SDL_CreateGPUTransferBuffer(Gpu_, &TransferInfo);
            if (Transfer == nullptr)
            {
                SDL_DestroySurface(Rgba);
                ReleaseImage();
                Error_ = std::string("Cannot create transfer buffer: ") + SDL_GetError();
                return false;
            }

            const auto FailUpload = [&](const char* Operation)
            {
                Error_ = std::string(Operation) + ": " + SDL_GetError();
                SDL_ReleaseGPUTransferBuffer(Gpu_, Transfer);
                SDL_DestroySurface(Rgba);
                ReleaseImage();
                return false;
            };
            void* Mapped = SDL_MapGPUTransferBuffer(Gpu_, Transfer, false);
            if (!Mapped)
                return FailUpload("Cannot map image upload");
            if (Mapped)
            {
                // SDL surfaces can be padded, so copy row by row.
                auto* Dst = static_cast<Uint8*>(Mapped);
                const auto* Src = static_cast<const Uint8*>(Rgba->pixels);
                for (int Y = 0; Y < Height_; ++Y)
                {
                    SDL_memcpy(Dst + static_cast<size_t>(Y) * Width_ * 4, Src + static_cast<size_t>(Y) * Rgba->pitch,
                               static_cast<size_t>(Width_) * 4);
                }
                SDL_UnmapGPUTransferBuffer(Gpu_, Transfer);
            }

            SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Gpu_);
            if (!Commands)
                return FailUpload("Cannot acquire image upload command");
            SDL_GPUCopyPass* CopyPass = SDL_BeginGPUCopyPass(Commands);
            if (!CopyPass)
            {
                SDL_CancelGPUCommandBuffer(Commands);
                return FailUpload("Cannot begin image upload pass");
            }

            SDL_GPUTextureTransferInfo Source = {};
            Source.transfer_buffer = Transfer;
            Source.offset = 0;

            SDL_GPUTextureRegion Region = {};
            Region.texture = Texture_;
            Region.w = static_cast<Uint32>(Width_);
            Region.h = static_cast<Uint32>(Height_);
            Region.d = 1;

            SDL_UploadToGPUTexture(CopyPass, &Source, &Region, false);
            SDL_EndGPUCopyPass(CopyPass);

            SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
            if (!Fence)
                return FailUpload("Cannot submit image upload");
            const bool Completed = SDL_WaitForGPUFences(Gpu_, true, &Fence, 1);
            SDL_ReleaseGPUFence(Gpu_, Fence);
            if (!Completed)
            {
                SDL_WaitForGPUIdle(Gpu_);
                return FailUpload("Cannot wait for image upload");
            }

            SDL_ReleaseGPUTransferBuffer(Gpu_, Transfer);
            SDL_DestroySurface(Rgba);
            return true;
        }

        bool AssetPreview::OpenAudio(const std::filesystem::path& Path)
        {
            if (Mixer_ == nullptr)
            {
                if (!MIX_Init())
                {
                    Error_ = std::string("Cannot init mixer: ") + SDL_GetError();
                    return false;
                }
                Mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
                if (Mixer_ == nullptr)
                {
                    Error_ = std::string("Cannot open audio device: ") + SDL_GetError();
                    MIX_Quit();
                    return false;
                }
            }

            Audio_ = MIX_LoadAudio(Mixer_, Path.string().c_str(), false);
            if (Audio_ == nullptr)
            {
                Error_ = std::string("Cannot load audio: ") + SDL_GetError();
                return false;
            }

            Track_ = MIX_CreateTrack(Mixer_);
            if (Track_ == nullptr || !MIX_SetTrackAudio(Track_, Audio_))
            {
                Error_ = std::string("Cannot create track: ") + SDL_GetError();
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

            if (ImGui::Button(Playing ? "Pause" : "Play"))
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
            if (ImGui::Button("Stop"))
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

            ImGui::TextUnformatted(Path_.filename().string().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Close"))
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
