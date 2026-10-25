#pragma once

#include "../../engine/systems/core_system.hpp"
#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/input_system.hpp"

#include <string>

namespace dbc
{
    inline int Upkey(EngineKernel& K)
    {
        return K.Input().Upkey();
    }
    inline int Leftkey(EngineKernel& K)
    {
        return K.Input().Leftkey();
    }
    inline int Rightkey(EngineKernel& K)
    {
        return K.Input().Rightkey();
    }
    inline int Downkey(EngineKernel& K)
    {
        return K.Input().Downkey();
    }
    inline int Spacekey(EngineKernel& K)
    {
        return K.Input().Spacekey();
    }
    inline int Controlkey(EngineKernel& K)
    {
        return K.Input().Controlkey();
    }
    inline int Shiftkey(EngineKernel& K)
    {
        return K.Input().Shiftkey();
    }
    inline int Returnkey(EngineKernel& K)
    {
        return K.Input().Returnkey();
    }
    inline int Escapekey(EngineKernel& K)
    {
        return K.Input().Escapekey();
    }
    inline int Keystate(EngineKernel& K, int DbScancode)
    {
        return K.Input().Keystate(DbScancode);
    }
    inline std::string Inkey(EngineKernel& K)
    {
        return K.Input().Inkey();
    }
    inline bool Entry(EngineKernel& K, std::string& Text, int Mode = 0, std::string* Error = nullptr)
    {
        return K.Input().Entry(Text, Mode, Error);
    }
    inline void ClearEntryBuffer(EngineKernel& K)
    {
        K.Input().ClearEntryBuffer();
    }
    inline int GetKeyState(EngineKernel& K, int VirtualKey)
    {
        return K.Input().GetKeyState(VirtualKey);
    }
    inline bool SetKeyStateToggle(EngineKernel& K, int VirtualKey, int Toggle, std::string* Error = nullptr)
    {
        return K.Input().SetKeyStateToggle(VirtualKey, Toggle, Error);
    }

    inline int Mouseclick(EngineKernel& K)
    {
        return K.Input().Mouseclick();
    }
    inline int Mousemovex(EngineKernel& K)
    {
        return K.Input().Mousemovex();
    }
    inline int Mousemovey(EngineKernel& K)
    {
        return K.Input().Mousemovey();
    }
    inline int Mousemovez(EngineKernel& K)
    {
        return K.Input().Mousemovez();
    }
    inline int Mousex(EngineKernel& K)
    {
        return K.Input().Mousex();
    }
    inline int Mousey(EngineKernel& K)
    {
        return K.Input().Mousey();
    }
    inline bool PositionMouse(EngineKernel& K, int X, int Y, std::string* Error = nullptr)
    {
        return K.Input().PositionMouse(X, Y, Error);
    }
    inline bool ChangeMouse(EngineKernel& K, int Cursor, int TouchFriendly = 0, std::string* Error = nullptr)
    {
        return K.Input().ChangeMouse(Cursor, TouchFriendly, Error);
    }

    inline bool SetControlDevice(EngineKernel& K, const std::string& Name, int DuplicateIndex = 0,
                                 std::string* Error = nullptr)
    {
        return K.Input().SetControlDevice(Name, DuplicateIndex, Error);
    }
    inline bool SetControlDeviceIndex(EngineKernel& K, int Index, std::string* Error = nullptr)
    {
        return K.Input().SetControlDeviceIndex(Index, Error);
    }
    inline int ControlDeviceIndex(EngineKernel& K)
    {
        return K.Input().ControlDeviceIndex();
    }
    inline std::string ControlDeviceName(EngineKernel& K)
    {
        return K.Input().ControlDeviceName();
    }
    inline int ControlDeviceX(EngineKernel& K)
    {
        return K.Input().ControlDeviceX();
    }
    inline int ControlDeviceY(EngineKernel& K)
    {
        return K.Input().ControlDeviceY();
    }
    inline int ControlDeviceZ(EngineKernel& K)
    {
        return K.Input().ControlDeviceZ();
    }
    inline int JoystickInformation(EngineKernel& K, int Mode)
    {
        return K.Input().JoystickInformation(Mode);
    }
    inline void SetJoystickDeadzone(EngineKernel& K, int Percentage)
    {
        K.Input().SetJoystickDeadzone(Percentage);
    }
    inline bool ForceUp(EngineKernel& K, int Magnitude, std::string* Error = nullptr)
    {
        return K.Input().ForceUp(Magnitude, Error);
    }
    inline bool ForceDown(EngineKernel& K, int Magnitude, std::string* Error = nullptr)
    {
        return K.Input().ForceDown(Magnitude, Error);
    }
    inline bool ForceLeft(EngineKernel& K, int Magnitude, std::string* Error = nullptr)
    {
        return K.Input().ForceLeft(Magnitude, Error);
    }
    inline bool ForceRight(EngineKernel& K, int Magnitude, std::string* Error = nullptr)
    {
        return K.Input().ForceRight(Magnitude, Error);
    }
    inline bool ForceChainsaw(EngineKernel& K, int Magnitude, int DurationMs, std::string* Error = nullptr)
    {
        return K.Input().ForceChainsaw(Magnitude, DurationMs, Error);
    }
    inline bool ForceImpact(EngineKernel& K, int Magnitude, int DurationMs, std::string* Error = nullptr)
    {
        return K.Input().ForceImpact(Magnitude, DurationMs, Error);
    }
    inline void ForceNoEffect(EngineKernel& K)
    {
        K.Input().ForceNoEffect();
    }
    inline void ForceAutoCenter(EngineKernel& K, bool Enabled)
    {
        K.Input().ForceAutoCenter(Enabled);
    }

    inline int ScreenWidth(EngineKernel& K)
    {
        return K.Core().WindowWidth();
    }
    inline int ScreenHeight(EngineKernel& K)
    {
        return K.Core().WindowHeight();
    }
    inline int ScreenFps(EngineKernel& K)
    {
        return K.Core().Fps();
    }

    inline std::string InputBlocking(EngineKernel& K, const std::string& Prompt)
    {
        return K.Input().InputBlocking(Prompt);
    }

    // Mobile touch input
    inline int GetTouchPositionX(EngineKernel& K, int FingerId)
    {
        return K.Input().GetTouchPositionX(FingerId);
    }
    inline int GetTouchPositionY(EngineKernel& K, int FingerId)
    {
        return K.Input().GetTouchPositionY(FingerId);
    }
    inline int IsTouchDown(EngineKernel& K, int FingerId)
    {
        return K.Input().IsTouchDown(FingerId);
    }
    inline int IsTouchMoved(EngineKernel& K, int FingerId)
    {
        return K.Input().IsTouchMoved(FingerId);
    }
    inline int IsTouchUp(EngineKernel& K, int FingerId)
    {
        return K.Input().IsTouchUp(FingerId);
    }

    // Mobile settings
    inline void SetMobileInputEmulation(EngineKernel& K, int Mode)
    {
        K.Input().SetMobileInputEmulation(Mode);
    }
    inline void SetScreenOrientation(EngineKernel& K, int Mode)
    {
        K.Input().SetScreenOrientation(Mode);
    }

    // Accelerometer
    inline float GetAccelerometerX(EngineKernel& K)
    {
        return K.Input().GetAccelerometerX();
    }
    inline float GetAccelerometerY(EngineKernel& K)
    {
        return K.Input().GetAccelerometerY();
    }
    inline float GetAccelerometerZ(EngineKernel& K)
    {
        return K.Input().GetAccelerometerZ();
    }

} // namespace dbc
