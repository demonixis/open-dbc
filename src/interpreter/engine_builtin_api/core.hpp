#pragma once

#include "graphics_2d.hpp"
#include "math.hpp"

#include "../../engine/systems/core_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"

#include <string>

namespace dbc
{
    // --- Lifecycle (inline wrappers to CoreSystem) ---

    inline bool Init(EngineKernel& K)
    {
        return K.Core().Init();
    }
    inline void Shutdown(EngineKernel& K)
    {
        K.Core().Shutdown();
    }
    inline bool Running(EngineKernel& K)
    {
        return K.Core().IsRunning();
    }

    // --- Display ---

    inline bool SetDisplayMode(EngineKernel& K, int Width, int Height, int Bpp, std::string* Error = nullptr)
    {
        return K.Core().SetDisplayMode(Width, Height, Bpp, 1, 0, 0, Error);
    }
    inline void HideMouse(EngineKernel& K)
    {
        K.Core().HideCursor();
    }
    inline void ShowMouse(EngineKernel& K)
    {
        K.Core().ShowCursor();
    }

    // --- Timing / sync ---

    inline void SyncOn(EngineKernel& K)
    {
        K.Core().DoSyncOn();
    }
    inline void SyncOff(EngineKernel& K)
    {
        K.Core().DoSyncOff();
    }
    inline void SyncRate(EngineKernel& K, int Rate)
    {
        K.Core().DoSyncRate(Rate);
    }
    inline void Sync(EngineKernel& K)
    {
        K.Core().Sync();
    }
    inline float DeltaTime(EngineKernel& K)
    {
        return K.Core().DeltaTime();
    }
    inline int Timer(EngineKernel& K)
    {
        return K.Core().Timer();
    }
    inline std::string GetDate(EngineKernel& K)
    {
        return K.Core().GetDate();
    }
    inline std::string GetTime(EngineKernel& K)
    {
        return K.Core().GetTime();
    }

    // --- Clipboard ---

    inline void WriteToClipboard(EngineKernel& K, const std::string& Text)
    {
        K.Core().WriteToClipboard(Text);
    }
    inline std::string GetClipboard(EngineKernel& K)
    {
        return K.Core().GetClipboard();
    }

    // NOTE: InputBlocking is declared in input.hpp (inline wrapper to InputSystem).

} // namespace dbc
