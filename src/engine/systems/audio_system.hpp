#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_audio.h>

// Forward declarations for SDL3_mixer types
struct MIX_Audio;
struct MIX_Track;
struct MIX_Mixer;

namespace dbc
{
    class EngineKernel;

    class AudioSystem
    {
      public:
        explicit AudioSystem(EngineKernel& Kernel);
        ~AudioSystem();

        void Shutdown();
        bool SetSuspended(bool Suspended, std::string* Error = nullptr);

        // The core lifecycle creates and destroys the mixer; this system owns its tracks.
        MIX_Mixer*& Mixer()
        {
            return Mixer_;
        }
        MIX_Mixer* Mixer() const
        {
            return Mixer_;
        }

        // Sound
        bool LoadSound(const std::string& Path, int SoundId, bool SilentFail = false, bool GlobalSound = false,
                       std::string* Error = nullptr);
        bool Load3dsound(const std::string& Path, int SoundId, bool SilentFail = false, bool GlobalSound = false,
                         std::string* Error = nullptr);
        bool CloneSound(int TargetSoundId, int SourceSoundId, std::string* Error = nullptr);
        bool PlaySound(int SoundId, int StartPosition = 0, std::string* Error = nullptr);
        bool LoopSound(int SoundId, int LoopStart = -1, int LoopEnd = -1, int InitialPosition = -1,
                       std::string* Error = nullptr);
        bool StopSound(int SoundId, std::string* Error = nullptr);
        bool SetSoundSpeed(int SoundId, int Hz, std::string* Error = nullptr);
        bool SetSoundVolume(int SoundId, int VolumePercent, std::string* Error = nullptr);
        bool PositionSound(int SoundId, float X, float Y, float Z, std::string* Error = nullptr);
        bool SoundPlaying(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundExist(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundLooping(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundPaused(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundType(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundVolume(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundPan(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundSpeed(int& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundPositionX(float& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundPositionY(float& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundPositionZ(float& Output, int SoundId, std::string* Error = nullptr) const;
        bool SoundMemblockData(std::vector<std::uint8_t>& Output, int SoundId, std::string* Error = nullptr) const;
        bool MakeSoundFromMemblock(int SoundId, const std::vector<std::uint8_t>& Data, std::string* Error = nullptr);
        bool PauseSound(int SoundId, std::string* Error = nullptr);
        bool ResumeSound(int SoundId, std::string* Error = nullptr);
        bool DeleteSound(int SoundId, std::string* Error = nullptr);
        bool SetSoundPan(int SoundId, int Pan, std::string* Error = nullptr);
        bool RecordSound(int SoundId, int DurationSeconds = 5, std::string* Error = nullptr);
        bool StopRecordingSound(std::string* Error = nullptr);
        bool SaveSound(const std::string& Path, int SoundId, std::string* Error = nullptr);
        void PumpRecording();

        // Music
        bool LoadMusic(const std::string& Path, int MusicId, std::string* Error = nullptr);
        bool LoopMusic(int MusicId, std::string* Error = nullptr);
        bool PlayMusic(int MusicId, std::string* Error = nullptr);
        bool StopMusic(int MusicId, std::string* Error = nullptr);
        bool PauseMusic(int MusicId, std::string* Error = nullptr);
        bool ResumeMusic(int MusicId, std::string* Error = nullptr);
        bool MusicExist(int& Output, int MusicId, std::string* Error = nullptr) const;
        bool MusicPlaying(int& Output, int MusicId, std::string* Error = nullptr) const;
        bool MusicLooping(int& Output, int MusicId, std::string* Error = nullptr) const;
        bool MusicPaused(int& Output, int MusicId, std::string* Error = nullptr) const;
        bool MusicVolume(int& Output, int MusicId, std::string* Error = nullptr) const;
        bool MusicSpeed(int& Output, int MusicId, std::string* Error = nullptr) const;
        bool DeleteMusic(int MusicId, std::string* Error = nullptr);
        bool SetMusicVolume(int MusicId, int VolumePercent, std::string* Error = nullptr);
        bool SetMusicSpeed(int MusicId, int Speed, std::string* Error = nullptr);

        // Listener
        bool PositionListener(float X, float Y, float Z, std::string* Error = nullptr);
        bool RotateListener(float XDegrees, float YDegrees, float ZDegrees, std::string* Error = nullptr);
        bool ScaleListener(float Scale, std::string* Error = nullptr);
        float ListenerPositionX() const
        {
            return ListenerX_;
        }
        float ListenerPositionY() const
        {
            return ListenerY_;
        }
        float ListenerPositionZ() const
        {
            return ListenerZ_;
        }
        float ListenerAngleX() const
        {
            return ListenerRotX_;
        }
        float ListenerAngleY() const
        {
            return ListenerRotY_;
        }
        float ListenerAngleZ() const
        {
            return ListenerRotZ_;
        }

      private:
        struct AudioSlot
        {
            std::shared_ptr<MIX_Audio> Audio;
            MIX_Track* Track = nullptr;
            bool Playing = false;
            bool Paused = false;
            bool SuspendedByHost = false;
            bool Looping = false;
            bool Is3d = false;
            bool GlobalSound = false;
            int VolumePercent = 100;
            int Pan = 0;
            int SpeedHz = 0;
            int MusicSpeedPercent = 100;
            float PosX = 0.0f;
            float PosY = 0.0f;
            float PosZ = 0.0f;
            std::uint16_t FormatTag = 0;
            std::uint16_t Channels = 0;
            std::uint32_t SampleRate = 0;
            std::uint32_t AverageBytesPerSecond = 0;
            std::uint16_t BlockAlign = 0;
            std::uint16_t BitsPerSample = 0;
            std::uint16_t ExtraSize = 0;
            std::vector<std::uint8_t> SampleData;
        };

        static bool ValidateSoundNumber(int SoundId, std::string* Error = nullptr);
        static bool ValidateMusicNumber(int MusicId, std::string* Error = nullptr);
        AudioSlot* RequireSound(int SoundId, const char* Command, std::string* Error = nullptr);
        const AudioSlot* RequireSound(int SoundId, const char* Command, std::string* Error = nullptr) const;
        AudioSlot* RequireMusic(int MusicId, const char* Command, std::string* Error = nullptr);
        const AudioSlot* RequireMusic(int MusicId, const char* Command, std::string* Error = nullptr) const;
        MIX_Track* RequireTrack(AudioSlot& Slot, const char* Command, std::string* Error = nullptr);
        bool ApplySoundProperties(AudioSlot& Slot, const char* Command, std::string* Error = nullptr);
        bool ApplySpatialPosition(AudioSlot& Slot, const char* Command, std::string* Error = nullptr);
        bool RefreshSpatialPositions(std::string* Error = nullptr);
        void DestroySlot(AudioSlot& Slot);
        MIX_Audio* LoadMusicAudioWithFallback(const std::string& RequestedPath, std::string* Error = nullptr);
        static void SDLCALL RecordingCallback(void* Userdata, SDL_AudioStream* Stream, int AdditionalAmount,
                                              int TotalAmount);
        void CollectRecordingData(SDL_AudioStream* Stream);
        bool FinishRecording(std::string* Error = nullptr);
        bool BuildWaveData(std::vector<std::uint8_t>& Output, const AudioSlot& Slot,
                           std::string* Error = nullptr) const;
        bool InstallPcmSound(int SoundId, AudioSlot Slot, std::string* Error = nullptr);

        EngineKernel& Kernel_;
        MIX_Mixer* Mixer_ = nullptr;
        std::unordered_map<int, AudioSlot> Sounds_;
        std::unordered_map<int, AudioSlot> Music_;
        float ListenerX_ = 0.0f;
        float ListenerY_ = 0.0f;
        float ListenerZ_ = 0.0f;
        float ListenerRotX_ = 0.0f;
        float ListenerRotY_ = 0.0f;
        float ListenerRotZ_ = 0.0f;
        float ListenerScale_ = 1.0f;
        SDL_AudioStream* RecordingStream_ = nullptr;
        int RecordingSoundId_ = 0;
        std::size_t RecordingMaximumBytes_ = 0;
        bool RecordingLimitReached_ = false;
        std::array<char, 256> RecordingCaptureError_{};
        std::vector<std::uint8_t> RecordingSamples_;
        int RecordingErrorSoundId_ = 0;
        std::string RecordingError_;
    };

} // namespace dbc
