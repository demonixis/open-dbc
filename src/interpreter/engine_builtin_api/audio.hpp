#pragma once

#include "../../engine/systems/audio_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"

namespace dbc
{
    inline bool LoadSound(EngineKernel& K, const std::string& Path, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().LoadSound(Path, SoundId, false, false, Error);
    }
    inline bool Load3dsound(EngineKernel& K, const std::string& Path, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().Load3dsound(Path, SoundId, false, false, Error);
    }
    inline bool CloneSound(EngineKernel& K, int TargetSoundId, int SourceSoundId, std::string* Error = nullptr)
    {
        return K.Audio().CloneSound(TargetSoundId, SourceSoundId, Error);
    }
    inline bool PlaySound(EngineKernel& K, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().PlaySound(SoundId, 0, Error);
    }
    inline bool LoopSound(EngineKernel& K, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().LoopSound(SoundId, -1, -1, -1, Error);
    }
    inline bool StopSound(EngineKernel& K, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().StopSound(SoundId, Error);
    }
    inline bool SetSoundSpeed(EngineKernel& K, int SoundId, int Hz, std::string* Error = nullptr)
    {
        return K.Audio().SetSoundSpeed(SoundId, Hz, Error);
    }
    inline bool SetSoundVolume(EngineKernel& K, int SoundId, int VolumePercent, std::string* Error = nullptr)
    {
        return K.Audio().SetSoundVolume(SoundId, VolumePercent, Error);
    }
    inline bool PositionSound(EngineKernel& K, int SoundId, float X, float Y, float Z, std::string* Error = nullptr)
    {
        return K.Audio().PositionSound(SoundId, X, Y, Z, Error);
    }
    inline bool SoundPlaying(EngineKernel& K, int& Output, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().SoundPlaying(Output, SoundId, Error);
    }
    inline bool SoundExist(EngineKernel& K, int& Output, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().SoundExist(Output, SoundId, Error);
    }
    inline bool SoundLooping(EngineKernel& K, int& Output, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().SoundLooping(Output, SoundId, Error);
    }
    inline bool SoundPaused(EngineKernel& K, int& Output, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().SoundPaused(Output, SoundId, Error);
    }
    inline bool PauseSound(EngineKernel& K, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().PauseSound(SoundId, Error);
    }
    inline bool ResumeSound(EngineKernel& K, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().ResumeSound(SoundId, Error);
    }
    inline bool DeleteSound(EngineKernel& K, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().DeleteSound(SoundId, Error);
    }
    inline bool SetSoundPan(EngineKernel& K, int SoundId, int Pan, std::string* Error = nullptr)
    {
        return K.Audio().SetSoundPan(SoundId, Pan, Error);
    }
    inline bool RecordSound(EngineKernel& K, int SoundId, int DurationSeconds = 5, std::string* Error = nullptr)
    {
        return K.Audio().RecordSound(SoundId, DurationSeconds, Error);
    }
    inline bool StopRecordingSound(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.Audio().StopRecordingSound(Error);
    }
    inline bool SaveSound(EngineKernel& K, const std::string& Path, int SoundId, std::string* Error = nullptr)
    {
        return K.Audio().SaveSound(Path, SoundId, Error);
    }

    inline bool LoadMusic(EngineKernel& K, const std::string& Path, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().LoadMusic(Path, MusicId, Error);
    }
    inline bool LoopMusic(EngineKernel& K, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().LoopMusic(MusicId, Error);
    }
    inline bool PlayMusic(EngineKernel& K, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().PlayMusic(MusicId, Error);
    }
    inline bool StopMusic(EngineKernel& K, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().StopMusic(MusicId, Error);
    }
    inline bool PauseMusic(EngineKernel& K, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().PauseMusic(MusicId, Error);
    }
    inline bool ResumeMusic(EngineKernel& K, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().ResumeMusic(MusicId, Error);
    }
    inline bool MusicExist(EngineKernel& K, int& Output, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().MusicExist(Output, MusicId, Error);
    }
    inline bool MusicPlaying(EngineKernel& K, int& Output, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().MusicPlaying(Output, MusicId, Error);
    }
    inline bool MusicLooping(EngineKernel& K, int& Output, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().MusicLooping(Output, MusicId, Error);
    }
    inline bool MusicPaused(EngineKernel& K, int& Output, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().MusicPaused(Output, MusicId, Error);
    }
    inline bool DeleteMusic(EngineKernel& K, int MusicId, std::string* Error = nullptr)
    {
        return K.Audio().DeleteMusic(MusicId, Error);
    }
    inline bool SetMusicVolume(EngineKernel& K, int MusicId, int VolumePercent, std::string* Error = nullptr)
    {
        return K.Audio().SetMusicVolume(MusicId, VolumePercent, Error);
    }
    inline bool SetMusicSpeed(EngineKernel& K, int MusicId, int Speed, std::string* Error = nullptr)
    {
        return K.Audio().SetMusicSpeed(MusicId, Speed, Error);
    }

    inline bool PositionListener(EngineKernel& K, float X, float Y, float Z, std::string* Error = nullptr)
    {
        return K.Audio().PositionListener(X, Y, Z, Error);
    }
    inline bool RotateListener(EngineKernel& K, float XDegrees, float YDegrees, float ZDegrees,
                               std::string* Error = nullptr)
    {
        return K.Audio().RotateListener(XDegrees, YDegrees, ZDegrees, Error);
    }

} // namespace dbc
