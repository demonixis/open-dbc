#pragma once

#include "../virtual_pad.hpp"

#include <SDL3/SDL_rect.h>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration for SDL types
union SDL_Event;
union SDL_HapticEffect;
struct SDL_Cursor;
struct SDL_Haptic;
struct SDL_Joystick;
struct SDL_Sensor;

namespace dbc
{
    class EngineKernel;

    // Touch finger phase (transient: Down and Up last a single frame)
    enum class TouchPhase
    {
        None = 0,
        Down = 1,
        Moved = 2,
        Up = 3
    };

    struct TouchFinger
    {
        float X = 0.0f; // normalized 0.0–1.0
        float Y = 0.0f;
        TouchPhase Phase = TouchPhase::None;
    };

    struct ControlDeviceInfo
    {
        std::string Name;
        int DuplicateIndex = 0;
        bool HasForceFeedback = false;
    };

    class InputSystem
    {
      public:
        explicit InputSystem(EngineKernel& Kernel);
        ~InputSystem();

        // Standalone hosts may pump here. Embedded hosts call BeginEventFrame
        // once, then route each SDL event through HandleEvent.
        void PollEvents();
        void BeginEventFrame();
        void HandleEvent(const SDL_Event& Event);

        // Event dispatch (called by PollEvents)
        void OnKeyDown(const char* KeyName);
        void OnMouseMotion(int Xrel, int Yrel);
        void OnMouseButtonDown(int Button);
        void OnMouseButtonUp(int Button);

        // Keyboard queries
        int Upkey() const;
        int Leftkey() const;
        int Rightkey() const;
        int Downkey() const;
        int Spacekey() const;
        int Controlkey() const;
        int Shiftkey() const;
        int Returnkey() const;
        int Escapekey() const;
        int Keystate(int DbScancode) const;
        static int SdlScancodeForDarkBasic(int DbScancode);
        std::string Inkey();

        // Entry buffer (non-blocking text accumulation)
        bool Entry(std::string& Text, int AutoBackspaceMode = 0, std::string* Error = nullptr);
        void ClearEntryBuffer();
        bool BeginTextInput();
        std::string TakeTextInput();
        bool TextInputSubmitted() const;
        bool EndTextInput(std::string* Error = nullptr);

        // Mouse queries
        int Mouseclick() const;
        int Mousemovex();
        int Mousemovey();
        int Mousemovez();
        int Mousez() const;
        // The DarkBASIC scancode (DirectInput numbering) of a key held down, 0 when none.
        int Scancode() const;
        int Mousex() const;
        int Mousey() const;
        bool PositionMouse(int X, int Y, std::string* Error = nullptr);
        bool ChangeMouse(int CursorId, std::string* Error = nullptr);
        bool ChangeMouse(int CursorId, int TouchFriendly, std::string* Error = nullptr);
        int GetKeyState(int VirtualKey) const;
        bool SetKeyStateToggle(int VirtualKey, int Toggle, std::string* Error = nullptr);

        // Direct state access (used by WaitKey, etc.)
        bool& AnyKeyPressed()
        {
            return AnyKeyPressed_;
        }

        bool AnyKeyPressed() const
        {
            return AnyKeyPressed_;
        }

        // Blocking text input
        std::string InputBlocking(const std::string& Prompt);

        std::vector<ControlDeviceInfo> ControlDevices() const;
        bool SetControlDevice(const std::string& Name, int DuplicateIndex = 0, std::string* Error = nullptr);
        bool SetControlDeviceIndex(int Index, std::string* Error = nullptr);
        int ControlDeviceIndex() const;
        std::string ControlDeviceName();
        int ControlDeviceX();
        int ControlDeviceY();
        int ControlDeviceZ();
        int JoystickInformation(int Mode);
        void SetJoystickDeadzone(int Percentage);

        int JoystickUp();
        int JoystickDown();
        int JoystickLeft();
        int JoystickRight();
        int JoystickFireA();
        int JoystickFireB();
        int JoystickFireC();
        int JoystickFireD();
        int JoystickFireX(int Button);
        int JoystickX();
        int JoystickY();
        int JoystickZ();
        int JoystickTwistX();
        int JoystickTwistY();
        int JoystickTwistZ();
        int JoystickSliderA();
        int JoystickSliderB();
        int JoystickSliderC();
        int JoystickSliderD();
        int JoystickHatAngle(int Hat);
        bool ForceUp(int Magnitude, std::string* Error = nullptr);
        bool ForceDown(int Magnitude, std::string* Error = nullptr);
        bool ForceLeft(int Magnitude, std::string* Error = nullptr);
        bool ForceRight(int Magnitude, std::string* Error = nullptr);
        bool ForceChainsaw(int Magnitude, int DurationMs, std::string* Error = nullptr);
        bool ForceImpact(int Magnitude, int DurationMs, std::string* Error = nullptr);
        void ForceNoEffect();
        void ForceAutoCenter(bool Enabled);
        bool ForceAngle(int Magnitude, double Angle, int DurationMs, std::string* Error = nullptr);
        void ForceShoot(int Magnitude, int DurationMs);
        bool ForceWaterEffect(int Magnitude, int DurationMs, std::string* Error = nullptr);
        static bool IsDirectTouchDevice(int DeviceType);

        // Mobile touch input
        // A finger's position, given as a fraction of the window, on the program's display.
        SDL_FPoint TouchOnDisplay(float X, float Y) const;
        int GetTouchPositionX(int FingerId) const;
        int GetTouchPositionY(int FingerId) const;
        int IsTouchDown(int FingerId) const;
        int IsTouchMoved(int FingerId) const;
        int IsTouchUp(int FingerId) const;

        // A finger on a direct touch screen, by its position as a fraction of the window. Outside the
        // virtual pad the first finger is the mouse: it moves the pointer and holds the left button,
        // and a second finger makes it the right button instead.
        void OnFingerDown(uint64_t Finger, float X, float Y);
        void OnFingerMotion(uint64_t Finger, float X, float Y);
        void OnFingerUp(uint64_t Finger, float X, float Y);

        // The virtual pad stands for the arrow keys, space and the mouse buttons. Auto shows it on a
        // touch screen that has no keyboard.
        enum class VirtualPadMode
        {
            Auto,
            Shown,
            Hidden,
        };

        void SetVirtualPadMode(VirtualPadMode Mode);
        bool VirtualPadVisible() const;
        bool VirtualPadHeld(PadButton Button) const;

        // Mobile settings
        void SetScreenOrientation(int Mode);

        // Accelerometer (lazy-init sensor on first call)
        float GetAccelerometerX()
        {
            TryOpenAccelerometer();
            return AccelX_;
        }

        float GetAccelerometerY()
        {
            TryOpenAccelerometer();
            return AccelY_;
        }

        float GetAccelerometerZ()
        {
            TryOpenAccelerometer();
            return AccelZ_;
        }

      private:
        EngineKernel& Kernel_;

        // Mouse state
        int MouseButtonMask_ = 0;
        int MouseDx_ = 0;
        int MouseX_ = 0;
        bool MousePositionKnown_ = false;
        int MouseWheel_ = 0;      // cumulative, 120 per notch like the original
        int MouseWheelDelta_ = 0; // since the last MOUSEMOVEZ
        int MouseDy_ = 0;
        int MouseY_ = 0;

        // Keyboard state
        std::string LastKeyText_;
        int LastKeyScancode_ = 0;
        bool LastKeyShift_ = false;
        bool LastKeyCaps_ = false;
        std::unordered_map<int, bool> HeldScancodes_;
        std::array<bool, 256> KeyStateToggle_{};
        bool AnyKeyPressed_ = false;

        // Entry buffer (accumulates SDL_EVENT_TEXT_INPUT between Entry()/ClearEntryBuffer())
        std::string EntryBuffer_;
        std::string InputBuffer_;
        bool TextInputActive_ = false; // lazy SDL_StartTextInput activation
        bool InputModeActive_ = false;
        bool TextInputSubmitted_ = false;

        SDL_Cursor* Cursor_ = nullptr;

        SDL_Joystick* Joystick_ = nullptr;
        SDL_Haptic* Haptic_ = nullptr;
        int ControlDeviceIndex_ = 0;
        int JoystickDeadzonePercentage_ = 0;
        bool JoystickOpenAttempted_ = false;
        bool HapticOpenAttempted_ = false;
        int AngleHapticEffect_ = -1;
        int WaterHapticEffect_ = -1;
        int ChainsawHapticEffect_ = -1;
        int ImpactHapticEffect_ = -1;
        bool HapticRumbleInitialized_ = false;
        std::array<int16_t, 10> JoystickAxes_{};
        std::array<bool, 10> JoystickAxisKnown_{};
        std::array<bool, 32> JoystickButtons_{};
        std::array<bool, 32> JoystickButtonKnown_{};
        std::array<uint8_t, 4> JoystickHats_{};
        std::array<bool, 4> JoystickHatKnown_{};

        void TryOpenJoystick();
        void TryOpenHaptic();
        void CloseHaptic();
        void CloseJoystick();
        void PlayHapticEffect(int& EffectId, const SDL_HapticEffect& Effect);
        bool PlayWholeDeviceRumble(int Magnitude, int DurationMs, std::string* Error = nullptr);
        bool PlayPeriodicForce(int& EffectId, int PreferredType, int FallbackType, int Magnitude, int DurationMs,
                               int PeriodMs, const char* Command, std::string* Error = nullptr);
        bool SelectJoystick(int Index);
        int JoystickAxis(int Axis);
        int JoystickButton(int Button);

        // Touch state (finger_id → state, 1-based DBA slots)
        std::unordered_map<int, TouchFinger> Touches_;
        int MapFingerSlot(uint64_t SdlFingerId);
        std::unordered_map<uint64_t, int> FingerSlotMap_;
        int NextFingerSlot_ = 1;

        // What a finger on the screen is doing.
        enum class FingerRole
        {
            Pad,
            Pointer,
            // The second finger of a right click.
            Second,
            Idle,
        };

        struct FingerUse
        {
            FingerRole Role = FingerRole::Idle;
            int Button = -1;
        };

        enum class PointerState
        {
            None,
            // Down, and not yet a left click: a second finger may still make it a right click.
            Pending,
            Left,
            Right,
            // The second finger was lifted: the pointer moves without a button.
            Hover,
        };

        // A press lasts at least this long, so that a program polling once a frame sees a tap.
        static constexpr uint64_t KPadHoldMs = 60;
        // How long a finger waits for a second one before it presses the left button.
        static constexpr uint64_t KSecondFingerMs = 80;

        void PadPress(int Button);
        void PadRelease(int Button);
        void ApplyPadButton(int Button, bool Down);
        void ReleaseDuePadButtons();

        VirtualPadMode VirtualPadMode_ = VirtualPadMode::Auto;
#ifdef DBC_MOBILE
        bool DirectTouchSeen_ = true;
#else
        bool DirectTouchSeen_ = false;
#endif
        std::unordered_map<int, FingerUse> FingerUses_;
        // Per pad button: the fingers holding it, whether the program sees it held, when it was
        // pressed, and when a lifted finger lets go of it (0: not waiting).
        std::array<int, KPadButtonCount> PadHolders_{};
        std::array<bool, KPadButtonCount> PadActive_{};
        std::array<uint64_t, KPadButtonCount> PadPressedAt_{};
        std::array<uint64_t, KPadButtonCount> PadReleaseAt_{};
        PointerState PointerState_ = PointerState::None;
        int PointerSlot_ = 0;
        int SecondSlot_ = 0;
        uint64_t PointerDownAt_ = 0;
        int PointerStartX_ = 0;
        int PointerStartY_ = 0;

        // Accelerometer
        SDL_Sensor* AccelSensor_ = nullptr;
        bool AccelOpenAttempted_ = false;
        float AccelX_ = 0.0f;
        float AccelY_ = 0.0f;
        float AccelZ_ = 0.0f;

        void TryOpenAccelerometer();
    };

} // namespace dbc
