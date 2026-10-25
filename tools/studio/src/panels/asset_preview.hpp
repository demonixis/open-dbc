#pragma once

#include <filesystem>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <imgui.h>

namespace dbc::studio
{
    namespace panels
    {
        // Image and audio preview for project assets. Replaces the editor's image
        // tab (QPixmap in a QLabel) and audio tab (QMediaPlayer + QAudioOutput):
        // images become an SDL_GPU texture drawn with ImGui::Image, audio goes
        // through SDL3_mixer, which the engine already depends on.
        class AssetPreview
        {
          public:
            enum class Kind
            {
                None,
                Image,
                Audio,
            };

            // Extension test, so the caller knows whether to open a preview or a
            // code tab.
            static Kind KindOf(const std::filesystem::path& Path);

            void Initialize(SDL_GPUDevice* Gpu);
            void Shutdown();

            bool Open(const std::filesystem::path& Path);
            void Close();

            bool IsOpen() const
            {
                return Kind_ != Kind::None;
            }

            const std::filesystem::path& Path() const
            {
                return Path_;
            }

            void Draw();

          private:
            void ReleaseImage();
            void ReleaseAudio();
            bool OpenImage(const std::filesystem::path& Path);
            bool OpenAudio(const std::filesystem::path& Path);
            void DrawImage();
            void DrawAudio();

            SDL_GPUDevice* Gpu_ = nullptr;

            Kind Kind_ = Kind::None;
            std::filesystem::path Path_;
            std::string Error_;

            // Image
            SDL_GPUTexture* Texture_ = nullptr;
            int Width_ = 0;
            int Height_ = 0;

            // Audio
            MIX_Mixer* Mixer_ = nullptr;
            MIX_Audio* Audio_ = nullptr;
            MIX_Track* Track_ = nullptr;
            Sint64 DurationFrames_ = 0;
        };
    } // namespace panels

} // namespace dbc::studio
