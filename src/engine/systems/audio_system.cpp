#include "audio_system.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "log.hpp"

#if __has_include(<SDL3_mixer/SDL_mixer.h>)
#include <SDL3_mixer/SDL_mixer.h>
#elif __has_include(<SDL_mixer.h>)
#include <SDL_mixer.h>
#endif

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>

namespace
{
    constexpr int KMaximumAudioNumber = 65535;

    template <typename T> T ReadLittleEndian(const std::uint8_t* Data)
    {
        T Result{};
        std::memcpy(&Result, Data, sizeof(Result));
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        if constexpr (sizeof(T) == 2)
            Result = static_cast<T>(SDL_Swap16(static_cast<std::uint16_t>(Result)));
        else if constexpr (sizeof(T) == 4)
            Result = static_cast<T>(SDL_Swap32(static_cast<std::uint32_t>(Result)));
#endif
        return Result;
    }

    template <typename T> void AppendLittleEndian(std::vector<std::uint8_t>& Data, T Value)
    {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        if constexpr (sizeof(T) == 2)
            Value = static_cast<T>(SDL_Swap16(static_cast<std::uint16_t>(Value)));
        else if constexpr (sizeof(T) == 4)
            Value = static_cast<T>(SDL_Swap32(static_cast<std::uint32_t>(Value)));
#endif
        const auto* Bytes = reinterpret_cast<const std::uint8_t*>(&Value);
        Data.insert(Data.end(), Bytes, Bytes + sizeof(Value));
    }

    void AppendFourCc(std::vector<std::uint8_t>& Data, const char* Value)
    {
        Data.insert(Data.end(), Value, Value + 4);
    }

    std::shared_ptr<MIX_Audio> OwnAudio(MIX_Audio* Audio)
    {
        return std::shared_ptr<MIX_Audio>(Audio,
                                          [](MIX_Audio* Value)
                                          {
                                              if (Value)
                                                  MIX_DestroyAudio(Value);
                                          });
    }
} // namespace

namespace dbc
{
    AudioSystem::AudioSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    void AudioSystem::Shutdown()
    {
        if (RecordingStream_)
        {
            SDL_PauseAudioStreamDevice(RecordingStream_);
            SDL_SetAudioStreamPutCallback(RecordingStream_, nullptr, nullptr);
            SDL_DestroyAudioStream(RecordingStream_);
            RecordingStream_ = nullptr;
        }
        RecordingSoundId_ = 0;
        RecordingMaximumBytes_ = 0;
        RecordingLimitReached_ = false;
        RecordingCaptureError_.fill(0);
        RecordingSamples_.clear();
        RecordingErrorSoundId_ = 0;
        RecordingError_.clear();

        for (auto& [Id, Slot] : Sounds_)
        {
            (void)Id;
            DestroySlot(Slot);
        }
        Sounds_.clear();
        for (auto& [Id, Slot] : Music_)
        {
            (void)Id;
            DestroySlot(Slot);
        }
        Music_.clear();
    }

    bool AudioSystem::SetSuspended(bool Suspended, std::string* Error)
    {
        if (RecordingStream_)
        {
            const bool Changed = Suspended ? SDL_PauseAudioStreamDevice(RecordingStream_)
                                           : SDL_ResumeAudioStreamDevice(RecordingStream_);
            if (!Changed)
            {
                if (Error)
                    *Error = std::string("Could not change recording suspension state: ") + SDL_GetError();
                return false;
            }
        }
        const auto Apply = [Suspended, Error](AudioSlot& Slot)
        {
            if (!Slot.Track || Slot.GlobalSound)
                return true;
            if (Suspended && MIX_TrackPlaying(Slot.Track))
            {
                if (!MIX_PauseTrack(Slot.Track))
                {
                    if (Error)
                        *Error = std::string("Could not suspend audio track: ") + SDL_GetError();
                    return false;
                }
                Slot.SuspendedByHost = true;
            }
            else if (!Suspended && Slot.SuspendedByHost)
            {
                if (!MIX_ResumeTrack(Slot.Track))
                {
                    if (Error)
                        *Error = std::string("Could not resume audio track: ") + SDL_GetError();
                    return false;
                }
                Slot.SuspendedByHost = false;
            }
            return true;
        };
        for (auto& [Id, Slot] : Sounds_)
        {
            (void)Id;
            if (!Apply(Slot))
                return false;
        }
        for (auto& [Id, Slot] : Music_)
        {
            (void)Id;
            if (!Apply(Slot))
                return false;
        }
        return true;
    }

    bool AudioSystem::ValidateSoundNumber(int SoundId, std::string* Error)
    {
        if (SoundId < 1 || SoundId > KMaximumAudioNumber)
        {
            if (Error)
                *Error = "Sound number must be between 1 and 65535";
            return false;
        }
        return true;
    }

    bool AudioSystem::ValidateMusicNumber(int MusicId, std::string* Error)
    {
        if (MusicId < 1 || MusicId > KMaximumAudioNumber)
        {
            if (Error)
                *Error = "Music number must be between 1 and 65535";
            return false;
        }
        return true;
    }

    AudioSystem::AudioSlot* AudioSystem::RequireSound(int SoundId, const char* Command, std::string* Error)
    {
        PumpRecording();
        if (!ValidateSoundNumber(SoundId, Error))
            return nullptr;
        const auto It = Sounds_.find(SoundId);
        if (It == Sounds_.end() || !It->second.Audio)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing sound " + std::to_string(SoundId);
            return nullptr;
        }
        return &It->second;
    }

    const AudioSystem::AudioSlot* AudioSystem::RequireSound(int SoundId, const char* Command, std::string* Error) const
    {
        const_cast<AudioSystem*>(this)->PumpRecording();
        if (!ValidateSoundNumber(SoundId, Error))
            return nullptr;
        const auto It = Sounds_.find(SoundId);
        if (It == Sounds_.end() || !It->second.Audio)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing sound " + std::to_string(SoundId);
            return nullptr;
        }
        return &It->second;
    }

    AudioSystem::AudioSlot* AudioSystem::RequireMusic(int MusicId, const char* Command, std::string* Error)
    {
        if (!ValidateMusicNumber(MusicId, Error))
            return nullptr;
        const auto It = Music_.find(MusicId);
        if (It == Music_.end() || !It->second.Audio)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing music " + std::to_string(MusicId);
            return nullptr;
        }
        return &It->second;
    }

    const AudioSystem::AudioSlot* AudioSystem::RequireMusic(int MusicId, const char* Command, std::string* Error) const
    {
        if (!ValidateMusicNumber(MusicId, Error))
            return nullptr;
        const auto It = Music_.find(MusicId);
        if (It == Music_.end() || !It->second.Audio)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing music " + std::to_string(MusicId);
            return nullptr;
        }
        return &It->second;
    }

    void AudioSystem::DestroySlot(AudioSlot& Slot)
    {
        if (Slot.Track)
        {
            MIX_StopTrack(Slot.Track, 0);
            MIX_DestroyTrack(Slot.Track);
            Slot.Track = nullptr;
        }
        Slot.Audio.reset();
        Slot = AudioSlot{};
    }

    MIX_Track* AudioSystem::RequireTrack(AudioSlot& Slot, const char* Command, std::string* Error)
    {
        if (!Mixer_)
        {
            if (Error)
                *Error = std::string(Command) + " requires an audio playback device";
            return nullptr;
        }
        if (!Slot.Track)
        {
            Slot.Track = MIX_CreateTrack(Mixer_);
            if (!Slot.Track || !MIX_SetTrackAudio(Slot.Track, Slot.Audio.get()))
            {
                if (Slot.Track)
                {
                    MIX_DestroyTrack(Slot.Track);
                    Slot.Track = nullptr;
                }
                {
                    if (Error)
                        *Error = std::string(Command) + " could not create a playback track: " + SDL_GetError();
                    return nullptr;
                }
            }
        }
        return Slot.Track;
    }

    bool AudioSystem::ApplySpatialPosition(AudioSlot& Slot, const char* Command, std::string* Error)
    {
        MIX_Track* Track = RequireTrack(Slot, Command, Error);
        if (!Track)
            return false;
        if (!Slot.Is3d)
        {
            const float Pan = static_cast<float>(Slot.Pan);
            MIX_StereoGains Gains{1.0f, 1.0f};
            if (Pan < 0.0f)
                Gains.right = std::pow(10.0f, Pan / 2000.0f);
            else if (Pan > 0.0f)
                Gains.left = std::pow(10.0f, -Pan / 2000.0f);
            if (!MIX_SetTrackStereo(Track, &Gains))
            {
                if (Error)
                    *Error = std::string(Command) + " could not set stereo pan: " + SDL_GetError();
                return false;
            }
            return true;
        }

        float X = (Slot.PosX - ListenerX_) / (100.0f * ListenerScale_);
        float Y = (Slot.PosY - ListenerY_) / (100.0f * ListenerScale_);
        float Z = (Slot.PosZ - ListenerZ_) / (100.0f * ListenerScale_);
        const float Rx = -ListenerRotX_ * 0.01745329251994329577f;
        const float Ry = -ListenerRotY_ * 0.01745329251994329577f;
        const float Rz = -ListenerRotZ_ * 0.01745329251994329577f;

        const auto RotateZ = [&]
        {
            const float C = std::cos(Rz);
            const float S = std::sin(Rz);
            const float NewX = C * X - S * Y;
            Y = S * X + C * Y;
            X = NewX;
        };
        const auto RotateY = [&]
        {
            const float C = std::cos(Ry);
            const float S = std::sin(Ry);
            const float NewX = C * X + S * Z;
            Z = -S * X + C * Z;
            X = NewX;
        };
        const auto RotateX = [&]
        {
            const float C = std::cos(Rx);
            const float S = std::sin(Rx);
            const float NewY = C * Y - S * Z;
            Z = S * Y + C * Z;
            Y = NewY;
        };
        RotateZ();
        RotateY();
        RotateX();

        const MIX_Point3D Position{X, Y, -Z};
        if (!MIX_SetTrack3DPosition(Track, &Position))
        {
            if (Error)
                *Error = std::string(Command) + " could not set 3D position: " + SDL_GetError();
            return false;
        }
        return true;
    }

    bool AudioSystem::ApplySoundProperties(AudioSlot& Slot, const char* Command, std::string* Error)
    {
        MIX_Track* Track = RequireTrack(Slot, Command, Error);
        if (!Track)
            return false;
        if (!MIX_SetTrackGain(Track, static_cast<float>(Slot.VolumePercent) / 100.0f))
        {
            if (Error)
                *Error = std::string(Command) + " could not set volume: " + SDL_GetError();
            return false;
        }
        const int SourceRate = std::max(1, static_cast<int>(Slot.SampleRate));
        const float Ratio =
            std::clamp(static_cast<float>(Slot.SpeedHz) / static_cast<float>(SourceRate), 0.01f, 100.0f);
        if (!MIX_SetTrackFrequencyRatio(Track, Ratio))
        {
            if (Error)
                *Error = std::string(Command) + " could not set frequency: " + SDL_GetError();
            return false;
        }
        if (!ApplySpatialPosition(Slot, Command, Error))
            return false;
        return true;
    }

    bool AudioSystem::RefreshSpatialPositions(std::string* Error)
    {
        for (auto& [Id, Slot] : Sounds_)
        {
            (void)Id;
            if (Slot.Audio && Slot.Is3d && Slot.Track)
                if (!ApplySpatialPosition(Slot, "listener update", Error))
                    return false;
        }
        return true;
    }

    bool AudioSystem::LoadSound(const std::string& Path, int SoundId, bool SilentFail, bool GlobalSound,
                                std::string* Error)
    {
        if (!ValidateSoundNumber(SoundId, Error))
            return false;
        if (Sounds_.contains(SoundId))
        {
            if (Error)
                *Error = "LOAD SOUND cannot replace existing sound " + std::to_string(SoundId);
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized() || !Mixer_)
        {
            if (Error)
                *Error = "LOAD SOUND requires an audio playback device";
            return false;
        }

        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        MIX_Audio* Audio = MIX_LoadAudio(Mixer_, Resolved.c_str(), true);
        if (!Audio)
        {
            if (SilentFail)
                return true;
            {
                if (Error)
                    *Error = "LOAD SOUND failed for '" + Path + "': " + SDL_GetError();
                return false;
            }
        }

        AudioSlot Slot;
        Slot.Audio = OwnAudio(Audio);
        Slot.GlobalSound = GlobalSound;
        SDL_AudioSpec Spec{};
        if (MIX_GetAudioFormat(Audio, &Spec))
        {
            Slot.Channels = static_cast<std::uint16_t>(std::max(1, Spec.channels));
            Slot.SampleRate = static_cast<std::uint32_t>(std::max(1, Spec.freq));
            Slot.BitsPerSample = static_cast<std::uint16_t>(SDL_AUDIO_BITSIZE(Spec.format));
            Slot.BlockAlign = static_cast<std::uint16_t>(Slot.Channels * std::max(1, Slot.BitsPerSample / 8));
            Slot.AverageBytesPerSecond = Slot.SampleRate * Slot.BlockAlign;
            Slot.FormatTag = SDL_AUDIO_ISFLOAT(Spec.format) ? 3 : 1;
        }

        std::ifstream Input(Resolved, std::ios::binary);
        const std::vector<std::uint8_t> FileData((std::istreambuf_iterator<char>(Input)),
                                                 std::istreambuf_iterator<char>());
        if (FileData.size() >= 12 && std::memcmp(FileData.data(), "RIFF", 4) == 0 &&
            std::memcmp(FileData.data() + 8, "WAVE", 4) == 0)
        {
            bool HaveFormat = false;
            for (std::size_t Offset = 12; Offset + 8 <= FileData.size();)
            {
                const std::uint32_t Size = ReadLittleEndian<std::uint32_t>(FileData.data() + Offset + 4);
                const std::size_t DataOffset = Offset + 8;
                if (DataOffset > FileData.size() || Size > FileData.size() - DataOffset)
                    break;
                if (std::memcmp(FileData.data() + Offset, "fmt ", 4) == 0 && Size >= 16)
                {
                    Slot.FormatTag = ReadLittleEndian<std::uint16_t>(FileData.data() + DataOffset);
                    Slot.Channels = ReadLittleEndian<std::uint16_t>(FileData.data() + DataOffset + 2);
                    Slot.SampleRate = ReadLittleEndian<std::uint32_t>(FileData.data() + DataOffset + 4);
                    Slot.AverageBytesPerSecond = ReadLittleEndian<std::uint32_t>(FileData.data() + DataOffset + 8);
                    Slot.BlockAlign = ReadLittleEndian<std::uint16_t>(FileData.data() + DataOffset + 12);
                    Slot.BitsPerSample = ReadLittleEndian<std::uint16_t>(FileData.data() + DataOffset + 14);
                    Slot.ExtraSize =
                        Size >= 18 ? ReadLittleEndian<std::uint16_t>(FileData.data() + DataOffset + 16) : 0;
                    HaveFormat = true;
                }
                else if (std::memcmp(FileData.data() + Offset, "data", 4) == 0 && HaveFormat)
                {
                    Slot.SampleData.assign(FileData.begin() + static_cast<std::ptrdiff_t>(DataOffset),
                                           FileData.begin() + static_cast<std::ptrdiff_t>(DataOffset + Size));
                    break;
                }
                Offset = DataOffset + Size + (Size & 1u);
            }
        }
        Slot.SpeedHz = std::max(1, static_cast<int>(Slot.SampleRate));
        Sounds_.emplace(SoundId, std::move(Slot));
        return true;
    }

    bool AudioSystem::Load3dsound(const std::string& Path, int SoundId, bool SilentFail, bool GlobalSound,
                                  std::string* Error)
    {
        if (!LoadSound(Path, SoundId, SilentFail, GlobalSound, Error))
            return false;
        const auto It = Sounds_.find(SoundId);
        if (It != Sounds_.end())
            It->second.Is3d = true;
        return true;
    }

    bool AudioSystem::CloneSound(int TargetSoundId, int SourceSoundId, std::string* Error)
    {
        const AudioSlot* SourcePointer = RequireSound(SourceSoundId, "CLONE SOUND", Error);
        if (!SourcePointer)
            return false;
        const AudioSlot& Source = *SourcePointer;
        if (!ValidateSoundNumber(TargetSoundId, Error))
            return false;
        if (Sounds_.contains(TargetSoundId))
        {
            if (Error)
                *Error = "CLONE SOUND destination already exists " + std::to_string(TargetSoundId);
            return false;
        }
        AudioSlot Clone = Source;
        Clone.Track = nullptr;
        Clone.Playing = false;
        Clone.Paused = false;
        Clone.SuspendedByHost = false;
        Clone.Looping = false;
        Sounds_.emplace(TargetSoundId, std::move(Clone));
        return true;
    }

    bool AudioSystem::PlaySound(int SoundId, int StartPosition, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "PLAY SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (StartPosition < 0 ||
            (!Slot.SampleData.empty() && static_cast<std::size_t>(StartPosition) > Slot.SampleData.size()))
        {
            if (Error)
                *Error = "PLAY SOUND start position is outside the sound data";
            return false;
        }
        if (!ApplySoundProperties(Slot, "PLAY SOUND", Error))
            return false;
        SDL_PropertiesID Options = 0;
        if (StartPosition > 0)
        {
            Options = SDL_CreateProperties();
            SDL_SetNumberProperty(Options, MIX_PROP_PLAY_START_FRAME_NUMBER,
                                  StartPosition / std::max(1, static_cast<int>(Slot.BlockAlign)));
        }
        const bool Played = MIX_PlayTrack(Slot.Track, Options);
        if (Options)
            SDL_DestroyProperties(Options);
        if (!Played)
        {
            if (Error)
                *Error = std::string("PLAY SOUND failed: ") + SDL_GetError();
            return false;
        }
        Slot.Playing = true;
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        Slot.Looping = false;
        return true;
    }

    bool AudioSystem::LoopSound(int SoundId, int LoopStart, int LoopEnd, int InitialPosition, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "LOOP SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        const auto ValidPosition = [&](int Position)
        {
            return Position >= -1 && (Position < 0 || Slot.SampleData.empty() ||
                                      static_cast<std::size_t>(Position) <= Slot.SampleData.size());
        };
        if (!ValidPosition(LoopStart) || !ValidPosition(LoopEnd) || !ValidPosition(InitialPosition) ||
            (LoopStart >= 0 && LoopEnd >= 0 && LoopEnd <= LoopStart))
        {
            if (Error)
                *Error = "LOOP SOUND positions are outside the sound data or out of order";
            return false;
        }

        if (!ApplySoundProperties(Slot, "LOOP SOUND", Error))
            return false;
        SDL_PropertiesID Options = SDL_CreateProperties();
        if (!Options)
        {
            if (Error)
                *Error = std::string("LOOP SOUND could not allocate playback options: ") + SDL_GetError();
            return false;
        }
        const int BlockAlign = std::max(1, static_cast<int>(Slot.BlockAlign));
        SDL_SetNumberProperty(Options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        if (InitialPosition >= 0)
            SDL_SetNumberProperty(Options, MIX_PROP_PLAY_START_FRAME_NUMBER, InitialPosition / BlockAlign);
        if (LoopStart >= 0)
            SDL_SetNumberProperty(Options, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER, LoopStart / BlockAlign);
        if (LoopEnd >= 0)
            SDL_SetNumberProperty(Options, MIX_PROP_PLAY_MAX_FRAME_NUMBER, LoopEnd / BlockAlign);
        const bool Played = MIX_PlayTrack(Slot.Track, Options);
        SDL_DestroyProperties(Options);
        if (!Played)
        {
            if (Error)
                *Error = std::string("LOOP SOUND failed: ") + SDL_GetError();
            return false;
        }
        Slot.Playing = true;
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        Slot.Looping = true;
        return true;
    }

    bool AudioSystem::StopSound(int SoundId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "STOP SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (Slot.Track && !MIX_StopTrack(Slot.Track, 0))
        {
            if (Error)
                *Error = std::string("STOP SOUND failed: ") + SDL_GetError();
            return false;
        }
        Slot.Playing = false;
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        Slot.Looping = false;
        return true;
    }

    bool AudioSystem::SetSoundSpeed(int SoundId, int Hz, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "SET SOUND SPEED", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (Hz < 100 || Hz > 100000)
        {
            if (Error)
                *Error = "SET SOUND SPEED frequency must be between 100 and 100000";
            return false;
        }
        Slot.SpeedHz = Hz;
        if (Slot.Track)
        {
            const float Ratio =
                std::clamp(static_cast<float>(Hz) / static_cast<float>(std::max(1u, Slot.SampleRate)), 0.01f, 100.0f);
            if (!MIX_SetTrackFrequencyRatio(Slot.Track, Ratio))
            {
                if (Error)
                    *Error = std::string("SET SOUND SPEED failed: ") + SDL_GetError();
                return false;
            }
        }
        return true;
    }

    bool AudioSystem::SetSoundVolume(int SoundId, int VolumePercent, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "SET SOUND VOLUME", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (VolumePercent < 0 || VolumePercent > 100)
        {
            if (Error)
                *Error = "SET SOUND VOLUME must be between zero and 100";
            return false;
        }
        Slot.VolumePercent = VolumePercent;
        if (Slot.Track && !MIX_SetTrackGain(Slot.Track, static_cast<float>(VolumePercent) / 100.0f))
        {
            if (Error)
                *Error = std::string("SET SOUND VOLUME failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool AudioSystem::PositionSound(int SoundId, float X, float Y, float Z, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "POSITION SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (!Slot.Is3d)
            return true;
        Slot.PosX = X;
        Slot.PosY = Y;
        Slot.PosZ = Z;
        if (Slot.Track)
            if (!ApplySpatialPosition(Slot, "POSITION SOUND", Error))
                return false;
        return true;
    }

    bool AudioSystem::SoundPlaying(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* SlotPointer = RequireSound(SoundId, "SOUND PLAYING", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        if (!Slot.Playing)
        {
            Output = 0;
            return true;
        }
        if (Slot.Paused || Slot.SuspendedByHost)
        {
            Output = 1;
            return true;
        }
        {
            Output = Slot.Track && MIX_TrackPlaying(Slot.Track) ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::SoundExist(int& Output, int SoundId, std::string* Error) const
    {
        const_cast<AudioSystem*>(this)->PumpRecording();
        if (!ValidateSoundNumber(SoundId, Error))
            return false;
        const auto It = Sounds_.find(SoundId);
        {
            Output = It != Sounds_.end() && It->second.Audio ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::SoundLooping(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND LOOPING", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->Looping ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::SoundPaused(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* SlotPointer = RequireSound(SoundId, "SOUND PAUSED", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        {
            Output = Slot.Paused || Slot.SuspendedByHost ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::SoundType(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND TYPE", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->Is3d ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::SoundVolume(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND VOLUME", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->VolumePercent;
            return true;
        }
    }

    bool AudioSystem::SoundPan(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* SlotPointer = RequireSound(SoundId, "SOUND PAN", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        {
            Output = Slot.Is3d ? 0 : Slot.Pan;
            return true;
        }
    }

    bool AudioSystem::SoundSpeed(int& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND SPEED", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->SpeedHz;
            return true;
        }
    }

    bool AudioSystem::SoundPositionX(float& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND POSITION X", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->PosX;
            return true;
        }
    }

    bool AudioSystem::SoundPositionY(float& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND POSITION Y", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->PosY;
            return true;
        }
    }

    bool AudioSystem::SoundPositionZ(float& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireSound(SoundId, "SOUND POSITION Z", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->PosZ;
            return true;
        }
    }

    bool AudioSystem::SoundMemblockData(std::vector<std::uint8_t>& Output, int SoundId, std::string* Error) const
    {
        const AudioSlot* SlotPointer = RequireSound(SoundId, "MAKE MEMBLOCK FROM SOUND", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        if (Slot.SampleData.empty() || Slot.FormatTag == 0 || Slot.Channels == 0 || Slot.SampleRate == 0 ||
            Slot.BlockAlign == 0 || Slot.BitsPerSample == 0)
        {
            if (Error)
                *Error = "MAKE MEMBLOCK FROM SOUND requires decoded WAVE sample data";
            return false;
        }
        std::vector<std::uint8_t> Data;
        Data.reserve(28 + Slot.SampleData.size());
        for (const std::uint32_t Value :
             {static_cast<std::uint32_t>(Slot.FormatTag), static_cast<std::uint32_t>(Slot.Channels), Slot.SampleRate,
              Slot.AverageBytesPerSecond, static_cast<std::uint32_t>(Slot.BlockAlign),
              static_cast<std::uint32_t>(Slot.BitsPerSample), static_cast<std::uint32_t>(Slot.ExtraSize)})
            AppendLittleEndian(Data, Value);
        Data.insert(Data.end(), Slot.SampleData.begin(), Slot.SampleData.end());
        {
            Output = std::move(Data);
            return true;
        }
    }

    bool AudioSystem::MakeSoundFromMemblock(int SoundId, const std::vector<std::uint8_t>& Data, std::string* Error)
    {
        if (!ValidateSoundNumber(SoundId, Error))
            return false;
        if (Sounds_.contains(SoundId))
        {
            if (Error)
                *Error = "MAKE SOUND FROM MEMBLOCK cannot replace existing sound " + std::to_string(SoundId);
            return false;
        }
        if (Data.size() <= 28)
        {
            if (Error)
                *Error = "MAKE SOUND FROM MEMBLOCK requires a 28-byte WAVEFORMATEX header and samples";
            return false;
        }

        // WAVEFORMATEX fields are stored as DWORDs in a Pro memblock.
        // Reject values before narrowing them to their native WAVE widths.
        for (const std::size_t Offset : {0u, 4u, 16u, 20u, 24u})
        {
            if (ReadLittleEndian<std::uint32_t>(Data.data() + Offset) > std::numeric_limits<std::uint16_t>::max())
            {
                if (Error)
                    *Error = "MAKE SOUND FROM MEMBLOCK received an out-of-range WAVEFORMATEX field";
                return false;
            }
        }
        if (ReadLittleEndian<std::uint32_t>(Data.data() + 8) >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            if (Error)
                *Error = "MAKE SOUND FROM MEMBLOCK sample rate is too large";
            return false;
        }
        AudioSlot Slot;
        Slot.FormatTag = static_cast<std::uint16_t>(ReadLittleEndian<std::uint32_t>(Data.data()));
        Slot.Channels = static_cast<std::uint16_t>(ReadLittleEndian<std::uint32_t>(Data.data() + 4));
        Slot.SampleRate = ReadLittleEndian<std::uint32_t>(Data.data() + 8);
        Slot.AverageBytesPerSecond = ReadLittleEndian<std::uint32_t>(Data.data() + 12);
        Slot.BlockAlign = static_cast<std::uint16_t>(ReadLittleEndian<std::uint32_t>(Data.data() + 16));
        Slot.BitsPerSample = static_cast<std::uint16_t>(ReadLittleEndian<std::uint32_t>(Data.data() + 20));
        Slot.ExtraSize = static_cast<std::uint16_t>(ReadLittleEndian<std::uint32_t>(Data.data() + 24));
        Slot.SampleData.assign(Data.begin() + 28, Data.end());
        if ((Slot.FormatTag != 1 && Slot.FormatTag != 3) || Slot.Channels == 0 || Slot.Channels > 8 ||
            Slot.SampleRate == 0 || Slot.BlockAlign == 0 || Slot.BitsPerSample == 0 ||
            Slot.SampleData.size() % Slot.BlockAlign != 0)
        {
            if (Error)
                *Error = "MAKE SOUND FROM MEMBLOCK received an invalid or unsupported WAVEFORMATEX header";
            return false;
        }

        std::vector<std::uint8_t> Wave;
        if (!BuildWaveData(Wave, Slot, Error))
            return false;

        if (!Kernel_.Core().EnsureInitialized() || !Mixer_)
        {
            if (Error)
                *Error = "MAKE SOUND FROM MEMBLOCK requires an audio playback device";
            return false;
        }

        SDL_IOStream* Io = SDL_IOFromConstMem(Wave.data(), Wave.size());
        MIX_Audio* Audio = Io ? MIX_LoadAudio_IO(Mixer_, Io, true, true) : nullptr;
        if (!Audio)
        {
            if (Error)
                *Error = std::string("MAKE SOUND FROM MEMBLOCK could not decode samples: ") + SDL_GetError();
            return false;
        }
        Slot.Audio = OwnAudio(Audio);
        Slot.SpeedHz = static_cast<int>(Slot.SampleRate);
        Sounds_.emplace(SoundId, std::move(Slot));
        return true;
    }

    bool AudioSystem::PauseSound(int SoundId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "PAUSE SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (Slot.Track && MIX_TrackPlaying(Slot.Track) && !MIX_PauseTrack(Slot.Track))
        {
            if (Error)
                *Error = std::string("PAUSE SOUND failed: ") + SDL_GetError();
            return false;
        }
        Slot.Paused = true;
        Slot.SuspendedByHost = false;
        return true;
    }

    bool AudioSystem::ResumeSound(int SoundId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "RESUME SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (!Slot.Paused)
            return true;
        if (Slot.Track && MIX_TrackPaused(Slot.Track))
        {
            if (!MIX_ResumeTrack(Slot.Track))
            {
                if (Error)
                    *Error = std::string("RESUME SOUND failed: ") + SDL_GetError();
                return false;
            }
        }
        else if (Slot.Looping)
        {
            if (!LoopSound(SoundId, -1, -1, -1, Error))
                return false;
        }
        else
        {
            if (!PlaySound(SoundId, 0, Error))
                return false;
        }
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        return true;
    }

    bool AudioSystem::DeleteSound(int SoundId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "DELETE SOUND", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        DestroySlot(Slot);
        Sounds_.erase(SoundId);
        return true;
    }

    bool AudioSystem::SetSoundPan(int SoundId, int Pan, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireSound(SoundId, "SET SOUND PAN", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (Pan < -10000 || Pan > 10000)
        {
            if (Error)
                *Error = "SET SOUND PAN must be between -10000 and 10000";
            return false;
        }
        Slot.Pan = Pan;
        if (Slot.Track && !Slot.Is3d)
            if (!ApplySpatialPosition(Slot, "SET SOUND PAN", Error))
                return false;
        return true;
    }

    void SDLCALL AudioSystem::RecordingCallback(void* Userdata, SDL_AudioStream* Stream, int, int)
    {
        auto& Audio = *static_cast<AudioSystem*>(Userdata);
        // Exceptions cannot escape SDL's callback boundary.
        try
        {
            Audio.CollectRecordingData(Stream);
        }
        catch (const std::bad_alloc&)
        {
            SDL_strlcpy(Audio.RecordingCaptureError_.data(), "Recording sample allocation failed",
                        Audio.RecordingCaptureError_.size());
            Audio.RecordingLimitReached_ = true;
        }
    }

    void AudioSystem::CollectRecordingData(SDL_AudioStream* Stream)
    {
        std::array<std::uint8_t, 16384> Buffer{};
        while (true)
        {
            const int Available = SDL_GetAudioStreamAvailable(Stream);
            if (Available < 0)
            {
                SDL_strlcpy(RecordingCaptureError_.data(), SDL_GetError(), RecordingCaptureError_.size());
                RecordingLimitReached_ = true;
                break;
            }
            if (Available == 0)
                break;

            const std::size_t Remaining = RecordingSamples_.size() < RecordingMaximumBytes_
                                              ? RecordingMaximumBytes_ - RecordingSamples_.size()
                                              : 0;
            const int Requested = static_cast<int>(std::min<std::size_t>(
                Buffer.size(), Remaining == 0 ? Buffer.size() : std::min<std::size_t>(Remaining, Available)));
            const int Received = SDL_GetAudioStreamData(Stream, Buffer.data(), Requested);
            if (Received <= 0)
            {
                if (Received < 0)
                {
                    SDL_strlcpy(RecordingCaptureError_.data(), SDL_GetError(), RecordingCaptureError_.size());
                    RecordingLimitReached_ = true;
                }
                break;
            }
            if (Remaining != 0)
            {
                const std::size_t Kept = std::min<std::size_t>(Remaining, static_cast<std::size_t>(Received));
                RecordingSamples_.insert(RecordingSamples_.end(), Buffer.begin(), Buffer.begin() + Kept);
            }
            if (RecordingSamples_.size() >= RecordingMaximumBytes_)
                RecordingLimitReached_ = true;
        }
    }

    bool AudioSystem::RecordSound(int SoundId, int DurationSeconds, std::string* Error)
    {
        if (!ValidateSoundNumber(SoundId, Error))
            return false;
        PumpRecording();
        if (RecordingStream_)
        {
            if (Error)
                *Error = "RECORD SOUND cannot start while another recording is active";
            return false;
        }
        if (Sounds_.contains(SoundId))
        {
            if (Error)
                *Error = "RECORD SOUND requires an empty sound number";
            return false;
        }
        if (DurationSeconds <= 0)
        {
            if (Error)
                *Error = "RECORD SOUND duration must be positive";
            return false;
        }
        RecordingErrorSoundId_ = 0;
        RecordingError_.clear();
        if (!Kernel_.Core().EnsureInitialized() || !Mixer_)
        {
            if (Error)
                *Error = "RECORD SOUND requires an audio device";
            return false;
        }

        constexpr int KRecordingFrequency = 44100;
        constexpr int KRecordingChannels = 1;
        constexpr std::size_t KBytesPerSample = sizeof(std::int16_t);
        const std::uint64_t MaximumBytes =
            static_cast<std::uint64_t>(DurationSeconds) * KRecordingFrequency * KRecordingChannels * KBytesPerSample;
        if (MaximumBytes > std::numeric_limits<std::size_t>::max())
        {
            if (Error)
                *Error = "RECORD SOUND duration is too large";
            return false;
        }

        SDL_AudioSpec Spec{};
        Spec.format = SDL_AUDIO_S16LE;
        Spec.channels = KRecordingChannels;
        Spec.freq = KRecordingFrequency;
        RecordingSoundId_ = SoundId;
        RecordingMaximumBytes_ = static_cast<std::size_t>(MaximumBytes);
        RecordingLimitReached_ = false;
        RecordingCaptureError_.fill(0);
        RecordingSamples_.clear();
        RecordingSamples_.reserve(std::min<std::size_t>(RecordingMaximumBytes_, 44100u * 2u));
        RecordingStream_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &Spec, RecordingCallback, this);
        if (!RecordingStream_)
        {
            RecordingSoundId_ = 0;
            RecordingMaximumBytes_ = 0;
            RecordingSamples_.clear();
            {
                if (Error)
                    *Error = std::string("RECORD SOUND could not open the recording device: ") + SDL_GetError();
                return false;
            }
        }
        if (!SDL_ResumeAudioStreamDevice(RecordingStream_))
        {
            const std::string OperationError = SDL_GetError();
            SDL_DestroyAudioStream(RecordingStream_);
            RecordingStream_ = nullptr;
            RecordingSoundId_ = 0;
            RecordingMaximumBytes_ = 0;
            RecordingSamples_.clear();
            {
                if (Error)
                    *Error = "RECORD SOUND could not start the recording device: " + OperationError;
                return false;
            }
        }
        return true;
    }

    void AudioSystem::PumpRecording()
    {
        if (!RecordingStream_)
            return;
        const int SoundId = RecordingSoundId_;
        std::string Error;
        bool Complete = false;
        if (!SDL_LockAudioStream(RecordingStream_))
        {
            Error = std::string("Could not lock the recording stream: ") + SDL_GetError();
        }
        else
        {
            Complete = RecordingLimitReached_;
            if (!SDL_UnlockAudioStream(RecordingStream_))
                Error = std::string("Could not unlock the recording stream: ") + SDL_GetError();
        }
        if (!Error.empty())
        {
            SDL_DestroyAudioStream(RecordingStream_);
            RecordingStream_ = nullptr;
            RecordingSoundId_ = 0;
            RecordingMaximumBytes_ = 0;
            RecordingSamples_.clear();
        }
        else if (!Complete || FinishRecording(&Error))
            return;
        RecordingErrorSoundId_ = SoundId;
        RecordingError_ = std::move(Error);
        DBC_LOG_ERROR("RECORD SOUND {} failed while finalizing: {}", SoundId, RecordingError_);
    }

    bool AudioSystem::StopRecordingSound(std::string* Error)
    {
        if (!RecordingError_.empty())
        {
            if (Error)
                *Error = "STOP RECORDING SOUND failed: " + RecordingError_;
            return false;
        }
        if (RecordingStream_)
            if (!FinishRecording(Error))
                return false;
        return true;
    }

    bool AudioSystem::FinishRecording(std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        Diagnostic.clear();
        SDL_AudioStream* Stream = RecordingStream_;
        if (!Stream)
            return true;

        if (!SDL_PauseAudioStreamDevice(Stream))
            Diagnostic = std::string("Could not pause the recording stream: ") + SDL_GetError();
        if (SDL_LockAudioStream(Stream))
        {
            if (RecordingCaptureError_[0] == '\0')
                CollectRecordingData(Stream);
            if (!SDL_UnlockAudioStream(Stream) && Diagnostic.empty())
                Diagnostic = std::string("Could not unlock the recording stream: ") + SDL_GetError();
        }
        else if (Diagnostic.empty())
            Diagnostic = std::string("Could not lock the recording stream: ") + SDL_GetError();
        if (!SDL_SetAudioStreamPutCallback(Stream, nullptr, nullptr) && Diagnostic.empty())
            Diagnostic = std::string("Could not stop the recording callback: ") + SDL_GetError();
        SDL_DestroyAudioStream(Stream);
        RecordingStream_ = nullptr;
        if (Diagnostic.empty() && RecordingCaptureError_[0] != '\0')
            Diagnostic = RecordingCaptureError_.data();

        const int SoundId = RecordingSoundId_;
        RecordingSoundId_ = 0;
        RecordingMaximumBytes_ = 0;
        RecordingLimitReached_ = false;
        RecordingCaptureError_.fill(0);
        if (!Diagnostic.empty())
        {
            RecordingSamples_.clear();
            return false;
        }
        if (RecordingSamples_.empty())
        {
            RecordingSamples_.clear();
            {
                Diagnostic = "STOP RECORDING SOUND did not receive any audio samples";
                return false;
            }
        }

        AudioSlot Slot;
        Slot.FormatTag = 1;
        Slot.Channels = 1;
        Slot.SampleRate = 44100;
        Slot.BitsPerSample = 16;
        Slot.BlockAlign = 2;
        Slot.AverageBytesPerSecond = Slot.SampleRate * Slot.BlockAlign;
        Slot.SpeedHz = static_cast<int>(Slot.SampleRate);
        Slot.SampleData = std::move(RecordingSamples_);
        RecordingSamples_.clear();
        if (!InstallPcmSound(SoundId, std::move(Slot), &Diagnostic))
            return false;
        return true;
    }

    bool AudioSystem::BuildWaveData(std::vector<std::uint8_t>& Output, const AudioSlot& Slot, std::string* Error) const
    {
        if (Slot.SampleData.empty() || (Slot.FormatTag != 1 && Slot.FormatTag != 3) || Slot.Channels == 0 ||
            Slot.SampleRate == 0 || Slot.BlockAlign == 0 || Slot.BitsPerSample == 0)
        {
            if (Error)
                *Error = "SAVE SOUND requires PCM or IEEE-float sample data";
            return false;
        }
        const std::uint64_t RiffSize =
            36u + static_cast<std::uint64_t>(Slot.SampleData.size()) + (Slot.SampleData.size() & 1u);
        if (RiffSize > std::numeric_limits<std::uint32_t>::max())
        {
            if (Error)
                *Error = "SAVE SOUND sample data is too large for a WAVE file";
            return false;
        }

        std::vector<std::uint8_t> Wave;
        Wave.reserve(44 + Slot.SampleData.size() + (Slot.SampleData.size() & 1u));
        AppendFourCc(Wave, "RIFF");
        AppendLittleEndian(Wave, static_cast<std::uint32_t>(RiffSize));
        AppendFourCc(Wave, "WAVE");
        AppendFourCc(Wave, "fmt ");
        AppendLittleEndian(Wave, std::uint32_t{16});
        AppendLittleEndian(Wave, Slot.FormatTag);
        AppendLittleEndian(Wave, Slot.Channels);
        AppendLittleEndian(Wave, Slot.SampleRate);
        AppendLittleEndian(Wave, Slot.AverageBytesPerSecond);
        AppendLittleEndian(Wave, Slot.BlockAlign);
        AppendLittleEndian(Wave, Slot.BitsPerSample);
        AppendFourCc(Wave, "data");
        AppendLittleEndian(Wave, static_cast<std::uint32_t>(Slot.SampleData.size()));
        Wave.insert(Wave.end(), Slot.SampleData.begin(), Slot.SampleData.end());
        if (Slot.SampleData.size() & 1u)
            Wave.push_back(0);
        {
            Output = std::move(Wave);
            return true;
        }
    }

    bool AudioSystem::InstallPcmSound(int SoundId, AudioSlot Slot, std::string* Error)
    {
        std::vector<std::uint8_t> Wave;
        if (!BuildWaveData(Wave, Slot, Error))
            return false;
        SDL_IOStream* Io = SDL_IOFromConstMem(Wave.data(), Wave.size());
        MIX_Audio* Audio = Io ? MIX_LoadAudio_IO(Mixer_, Io, true, true) : nullptr;
        if (!Audio)
        {
            if (Error)
                *Error = std::string("Could not create sound from recorded samples: ") + SDL_GetError();
            return false;
        }
        Slot.Audio = OwnAudio(Audio);
        Sounds_.emplace(SoundId, std::move(Slot));
        return true;
    }

    bool AudioSystem::SaveSound(const std::string& Path, int SoundId, std::string* Error)
    {
        PumpRecording();
        if (!RecordingError_.empty() && RecordingErrorSoundId_ == SoundId)
        {
            if (Error)
                *Error = "SAVE SOUND failed: " + RecordingError_;
            return false;
        }
        const AudioSlot* SlotPointer = RequireSound(SoundId, "SAVE SOUND", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        std::vector<std::uint8_t> Wave;
        if (!BuildWaveData(Wave, Slot, Error))
            return false;
        const std::filesystem::path Destination(Path);
        if (Destination.empty())
        {
            if (Error)
                *Error = "SAVE SOUND requires a filename";
            return false;
        }
        std::error_code OperationError;
        if (!Destination.parent_path().empty())
            std::filesystem::create_directories(Destination.parent_path(), OperationError);
        if (OperationError)
        {
            if (Error)
                *Error = "SAVE SOUND could not create its destination directory: " + OperationError.message();
            return false;
        }
        std::ofstream Output(Destination, std::ios::binary | std::ios::trunc);
        if (!Output ||
            !Output.write(reinterpret_cast<const char*>(Wave.data()), static_cast<std::streamsize>(Wave.size())))
        {
            if (Error)
                *Error = "SAVE SOUND could not write '" + Path + "'";
            return false;
        }
        return true;
    }

    bool AudioSystem::LoadMusic(const std::string& Path, int MusicId, std::string* Error)
    {
        if (!ValidateMusicNumber(MusicId, Error))
            return false;
        if (Music_.contains(MusicId))
        {
            if (Error)
                *Error = "LOAD MUSIC cannot replace existing music " + std::to_string(MusicId);
            return false;
        }
        if (Path.empty())
        {
            if (Error)
                *Error = "LOAD MUSIC requires a filename";
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized() || !Mixer_)
        {
            if (Error)
                *Error = "LOAD MUSIC requires an audio playback device";
            return false;
        }

        MIX_Audio* Audio = LoadMusicAudioWithFallback(Path, Error);
        if (!Audio)
        {
            if (Error)
                *Error = "LOAD MUSIC failed for '" + Path + "': " + SDL_GetError();
            return false;
        }
        AudioSlot Slot;
        Slot.Audio = OwnAudio(Audio);
        Music_.emplace(MusicId, std::move(Slot));
        return true;
    }

    bool AudioSystem::LoopMusic(int MusicId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "LOOP MUSIC", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        MIX_Track* Track = RequireTrack(Slot, "LOOP MUSIC", Error);
        if (!Track)
            return false;
        if (!MIX_SetTrackGain(Track, static_cast<float>(Slot.VolumePercent) / 100.0f) ||
            !MIX_SetTrackFrequencyRatio(Track,
                                        std::clamp(static_cast<float>(Slot.MusicSpeedPercent) / 100.0f, 0.01f, 100.0f)))
        {
            if (Error)
                *Error = std::string("LOOP MUSIC could not apply playback properties: ") + SDL_GetError();
            return false;
        }
        SDL_PropertiesID Options = SDL_CreateProperties();
        if (!Options)
        {
            if (Error)
                *Error = std::string("LOOP MUSIC could not allocate playback options: ") + SDL_GetError();
            return false;
        }
        SDL_SetNumberProperty(Options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        const bool Played = MIX_PlayTrack(Track, Options);
        SDL_DestroyProperties(Options);
        if (!Played)
        {
            if (Error)
                *Error = std::string("LOOP MUSIC failed: ") + SDL_GetError();
            return false;
        }
        Slot.Playing = true;
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        Slot.Looping = true;
        return true;
    }

    bool AudioSystem::PlayMusic(int MusicId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "PLAY MUSIC", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        MIX_Track* Track = RequireTrack(Slot, "PLAY MUSIC", Error);
        if (!Track)
            return false;
        if (!MIX_SetTrackGain(Track, static_cast<float>(Slot.VolumePercent) / 100.0f) ||
            !MIX_SetTrackFrequencyRatio(
                Track, std::clamp(static_cast<float>(Slot.MusicSpeedPercent) / 100.0f, 0.01f, 100.0f)) ||
            !MIX_PlayTrack(Track, 0))
        {
            if (Error)
                *Error = std::string("PLAY MUSIC failed: ") + SDL_GetError();
            return false;
        }
        Slot.Playing = true;
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        Slot.Looping = false;
        return true;
    }

    bool AudioSystem::StopMusic(int MusicId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "STOP MUSIC", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (Slot.Track && !MIX_StopTrack(Slot.Track, 0))
        {
            if (Error)
                *Error = std::string("STOP MUSIC failed: ") + SDL_GetError();
            return false;
        }
        Slot.Playing = false;
        Slot.Paused = false;
        Slot.SuspendedByHost = false;
        return true;
    }

    bool AudioSystem::PauseMusic(int MusicId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "PAUSE MUSIC", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (!Slot.Playing || Slot.Paused)
            return true;
        if (Slot.Track && !MIX_PauseTrack(Slot.Track))
        {
            if (Error)
                *Error = std::string("PAUSE MUSIC failed: ") + SDL_GetError();
            return false;
        }
        Slot.Paused = true;
        return true;
    }

    bool AudioSystem::ResumeMusic(int MusicId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "RESUME MUSIC", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (!Slot.Paused)
            return true;
        if (!Slot.Track || !MIX_ResumeTrack(Slot.Track))
        {
            if (Error)
                *Error = std::string("RESUME MUSIC failed: ") + SDL_GetError();
            return false;
        }
        Slot.Paused = false;
        return true;
    }

    bool AudioSystem::MusicExist(int& Output, int MusicId, std::string* Error) const
    {
        if (!ValidateMusicNumber(MusicId, Error))
            return false;
        const auto It = Music_.find(MusicId);
        {
            Output = It != Music_.end() && It->second.Audio ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::MusicPlaying(int& Output, int MusicId, std::string* Error) const
    {
        const AudioSlot* SlotPointer = RequireMusic(MusicId, "MUSIC PLAYING", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        {
            Output = Slot.Playing && !Slot.Paused && Slot.Track && MIX_TrackPlaying(Slot.Track) ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::MusicLooping(int& Output, int MusicId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireMusic(MusicId, "MUSIC LOOPING", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->Looping ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::MusicPaused(int& Output, int MusicId, std::string* Error) const
    {
        const AudioSlot* SlotPointer = RequireMusic(MusicId, "MUSIC PAUSED", Error);
        if (!SlotPointer)
            return false;
        const AudioSlot& Slot = *SlotPointer;
        {
            Output = Slot.Paused || Slot.SuspendedByHost ? 1 : 0;
            return true;
        }
    }

    bool AudioSystem::MusicVolume(int& Output, int MusicId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireMusic(MusicId, "MUSIC VOLUME", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->VolumePercent;
            return true;
        }
    }

    bool AudioSystem::MusicSpeed(int& Output, int MusicId, std::string* Error) const
    {
        const AudioSlot* Slot = RequireMusic(MusicId, "MUSIC SPEED", Error);
        if (!Slot)
            return false;
        {
            Output = Slot->MusicSpeedPercent;
            return true;
        }
    }

    bool AudioSystem::DeleteMusic(int MusicId, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "DELETE MUSIC", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        DestroySlot(Slot);
        Music_.erase(MusicId);
        return true;
    }

    bool AudioSystem::SetMusicVolume(int MusicId, int VolumePercent, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "SET MUSIC VOLUME", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        Slot.VolumePercent = std::clamp(VolumePercent, 0, 100);
        if (Slot.Track && !MIX_SetTrackGain(Slot.Track, static_cast<float>(Slot.VolumePercent) / 100.0f))
        {
            if (Error)
                *Error = std::string("SET MUSIC VOLUME failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool AudioSystem::SetMusicSpeed(int MusicId, int Speed, std::string* Error)
    {
        AudioSlot* SlotPointer = RequireMusic(MusicId, "SET MUSIC SPEED", Error);
        if (!SlotPointer)
            return false;
        AudioSlot& Slot = *SlotPointer;
        if (Speed < 0)
        {
            if (Error)
                *Error = "SET MUSIC SPEED requires a non-negative percentage";
            return false;
        }
        Slot.MusicSpeedPercent = Speed;
        if (Slot.Track &&
            !MIX_SetTrackFrequencyRatio(Slot.Track, std::clamp(static_cast<float>(Speed) / 100.0f, 0.01f, 100.0f)))
        {
            if (Error)
                *Error = std::string("SET MUSIC SPEED failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool AudioSystem::PositionListener(float X, float Y, float Z, std::string* Error)
    {
        ListenerX_ = X;
        ListenerY_ = Y;
        ListenerZ_ = Z;
        if (!RefreshSpatialPositions(Error))
            return false;
        return true;
    }

    bool AudioSystem::RotateListener(float XDegrees, float YDegrees, float ZDegrees, std::string* Error)
    {
        ListenerRotX_ = XDegrees;
        ListenerRotY_ = YDegrees;
        ListenerRotZ_ = ZDegrees;
        if (!RefreshSpatialPositions(Error))
            return false;
        return true;
    }

    bool AudioSystem::ScaleListener(float Scale, std::string* Error)
    {
        if (!std::isfinite(Scale) || Scale <= 0.0f)
        {
            if (Error)
                *Error = "SCALE LISTENER requires a positive scaling factor";
            return false;
        }
        ListenerScale_ = Scale;
        if (!RefreshSpatialPositions(Error))
            return false;
        return true;
    }

    MIX_Audio* AudioSystem::LoadMusicAudioWithFallback(const std::string& RequestedPath, std::string* Error)
    {
        namespace fs = std::filesystem;

        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, RequestedPath, Error))
            return nullptr;
        if (MIX_Audio* Audio = MIX_LoadAudio(Mixer_, Resolved.c_str(), false))
            return Audio;

        const std::string FirstError = SDL_GetError();
        const fs::path BasePath = fs::path(Resolved);
        std::string Extension = BasePath.extension().string();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        if (Extension != ".mid" && Extension != ".midi")
        {
            if (!FirstError.empty())
                SDL_SetError("%s", FirstError.c_str());
            return nullptr;
        }
        const fs::path StemPath = BasePath.parent_path() / BasePath.stem();

        static constexpr std::array<const char*, 2> KFallbackExts = {".mp3", ".wav"};
        for (const char* Extension : KFallbackExts)
        {
            const fs::path Candidate = StemPath.string() + Extension;
            const std::string CandidateString = Candidate.string();
            if (MIX_Audio* Audio = MIX_LoadAudio(Mixer_, CandidateString.c_str(), false))
            {
                DBC_LOG_INFO("LoadMusic fallback: {} -> {}", RequestedPath, CandidateString);
                return Audio;
            }
        }

        if (!FirstError.empty())
            SDL_SetError("%s", FirstError.c_str());
        return nullptr;
    }
} // namespace dbc
