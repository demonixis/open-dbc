#include "input_system.hpp"
#include "core_system.hpp"
#include "debug_system.hpp"
#include "engine_kernel.hpp"
#include "input_haptic_mapping.hpp"
#include "renderer_system.hpp"
#include "vr_system.hpp"

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_touch.h>
#include <cctype>
#include <limits>

namespace dbc
{
    InputSystem::InputSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    InputSystem::~InputSystem()
    {
        CloseJoystick();
        if (Cursor_)
        {
            SDL_DestroyCursor(Cursor_);
            Cursor_ = nullptr;
        }
        if (AccelSensor_)
        {
            SDL_CloseSensor(AccelSensor_);
            AccelSensor_ = nullptr;
        }
    }

    void InputSystem::BeginEventFrame()
    {
        for (auto It = Touches_.begin(); It != Touches_.end();)
        {
            if (It->second.Phase == TouchPhase::Up)
                It = Touches_.erase(It);
            else if (It->second.Phase == TouchPhase::Down)
            {
                It->second.Phase = TouchPhase::None;
                ++It;
            }
            else
                ++It;
        }
    }

    void InputSystem::PollEvents()
    {
        BeginEventFrame();
        SDL_Event Event;
        while (SDL_PollEvent(&Event))
            HandleEvent(Event);
    }

    void InputSystem::HandleEvent(const SDL_Event& Event)
    {
        const SDL_WindowID RuntimeWindow = Kernel_.Core().Window() ? SDL_GetWindowID(Kernel_.Core().Window()) : 0;
        const auto IsRuntimeWindow = [RuntimeWindow](SDL_WindowID Window)
        { return Window == 0 || RuntimeWindow == 0 || Window == RuntimeWindow; };

        if (Event.type == SDL_EVENT_QUIT)
        {
            Kernel_.Core().Running() = false;
            return;
        }
        if (Event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && IsRuntimeWindow(Event.window.windowID))
        {
            Kernel_.Core().Running() = false;
            return;
        }
        if (Event.type == SDL_EVENT_KEY_DOWN && IsRuntimeWindow(Event.key.windowID))
        {
            if (Event.key.key == SDLK_ESCAPE && !Kernel_.Core().EscapeDisabled())
                Kernel_.Core().Running() = false;
            if (Event.key.key == SDLK_F3 && !Event.key.repeat)
                Kernel_.Debug().ToggleOverlay();
            if (Event.key.key == SDLK_F4 && !Event.key.repeat)
                Kernel_.Debug().DumpDiagnostics();
            if (!Event.key.repeat)
            {
                OnKeyDown(SDL_GetKeyName(Event.key.key));
                LastKeyScancode_ = static_cast<int>(Event.key.scancode);
                LastKeyShift_ = (Event.key.mod & SDL_KMOD_SHIFT) != 0;
                LastKeyCaps_ = (Event.key.mod & SDL_KMOD_CAPS) != 0;
                HeldScancodes_[LastKeyScancode_] = true;
                if (InputModeActive_ && (Event.key.key == SDLK_RETURN || Event.key.key == SDLK_KP_ENTER))
                    TextInputSubmitted_ = true;
                else if (Event.key.key == SDLK_BACKSPACE)
                {
                    if (InputModeActive_)
                    {
                        if (!InputBuffer_.empty())
                            InputBuffer_.pop_back();
                    }
                    else
                    {
                        EntryBuffer_.push_back('\b');
                    }
                }
            }
        }
        if (Event.type == SDL_EVENT_KEY_UP && IsRuntimeWindow(Event.key.windowID))
        {
            const int Scancode = static_cast<int>(Event.key.scancode);
            HeldScancodes_.erase(Scancode);
            if (Scancode == LastKeyScancode_)
            {
                LastKeyText_.clear();
                LastKeyScancode_ = 0;
                LastKeyShift_ = false;
                LastKeyCaps_ = false;
            }
        }
        if (Event.type == SDL_EVENT_WINDOW_RESIZED && IsRuntimeWindow(Event.window.windowID))
        {
            Kernel_.Renderer().InvalidateTriCache();
            Kernel_.Core().WindowWidth() = Event.window.data1;
            Kernel_.Core().WindowHeight() = Event.window.data2;
            Kernel_.Core().CameraAspect() =
                static_cast<float>(Kernel_.Core().WindowWidth()) / std::max(1, Kernel_.Core().WindowHeight());
        }
        if (Event.type == SDL_EVENT_TEXT_INPUT && IsRuntimeWindow(Event.text.windowID))
        {
            if (InputModeActive_)
                InputBuffer_ += Event.text.text;
            else
                EntryBuffer_ += Event.text.text;
        }
        if (Event.type == SDL_EVENT_MOUSE_MOTION && IsRuntimeWindow(Event.motion.windowID))
        {
            MouseX_ = static_cast<int>(Event.motion.x);
            MouseY_ = static_cast<int>(Event.motion.y);
            MousePositionKnown_ = true;
            OnMouseMotion(Event.motion.xrel, Event.motion.yrel);
        }
        if (Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && IsRuntimeWindow(Event.button.windowID))
            OnMouseButtonDown(Event.button.button);
        if (Event.type == SDL_EVENT_MOUSE_BUTTON_UP && IsRuntimeWindow(Event.button.windowID))
            OnMouseButtonUp(Event.button.button);
        if (Event.type == SDL_EVENT_MOUSE_WHEEL && IsRuntimeWindow(Event.wheel.windowID))
        {
            const int Notches = static_cast<int>(std::lround(Event.wheel.y * 120.0f));
            MouseWheel_ += Notches;
            MouseWheelDelta_ += Notches;
        }
        if (Event.type == SDL_EVENT_JOYSTICK_ADDED && !Joystick_)
            TryOpenJoystick();
        if (Joystick_ && Event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
            Event.jaxis.which == SDL_GetJoystickID(Joystick_) && Event.jaxis.axis < JoystickAxes_.size())
        {
            JoystickAxes_[Event.jaxis.axis] = Event.jaxis.value;
            JoystickAxisKnown_[Event.jaxis.axis] = true;
        }
        if (Joystick_ && (Event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || Event.type == SDL_EVENT_JOYSTICK_BUTTON_UP) &&
            Event.jbutton.which == SDL_GetJoystickID(Joystick_) && Event.jbutton.button < JoystickButtons_.size())
        {
            JoystickButtons_[Event.jbutton.button] = Event.jbutton.down;
            JoystickButtonKnown_[Event.jbutton.button] = true;
        }
        if (Joystick_ && Event.type == SDL_EVENT_JOYSTICK_HAT_MOTION &&
            Event.jhat.which == SDL_GetJoystickID(Joystick_) && Event.jhat.hat < JoystickHats_.size())
        {
            JoystickHats_[Event.jhat.hat] = Event.jhat.value;
            JoystickHatKnown_[Event.jhat.hat] = true;
        }
        if (Event.type == SDL_EVENT_JOYSTICK_REMOVED && Joystick_ &&
            Event.jdevice.which == SDL_GetJoystickID(Joystick_))
        {
            CloseJoystick();
            JoystickOpenAttempted_ = false;
        }
        if ((Event.type == SDL_EVENT_WILL_ENTER_BACKGROUND || Event.type == SDL_EVENT_WINDOW_MINIMIZED) &&
            IsRuntimeWindow(Event.window.windowID) && !Kernel_.Core().AlwaysActive() &&
            !(Event.type == SDL_EVENT_WINDOW_MINIMIZED && Kernel_.Vr().Enabled()))
            Kernel_.Core().SetSuspended(true);
        if ((Event.type == SDL_EVENT_DID_ENTER_FOREGROUND || Event.type == SDL_EVENT_WINDOW_RESTORED) &&
            IsRuntimeWindow(Event.window.windowID))
            Kernel_.Core().SetSuspended(false);
        const bool DirectTouch = (Event.type == SDL_EVENT_FINGER_DOWN || Event.type == SDL_EVENT_FINGER_MOTION ||
                                  Event.type == SDL_EVENT_FINGER_UP) &&
                                 Event.tfinger.touchID != SDL_MOUSE_TOUCHID &&
                                 IsDirectTouchDevice(static_cast<int>(SDL_GetTouchDeviceType(Event.tfinger.touchID)));
        if (DirectTouch && Event.type == SDL_EVENT_FINGER_DOWN && IsRuntimeWindow(Event.tfinger.windowID))
        {
            const int Slot = MapFingerSlot(Event.tfinger.fingerID);
            Touches_[Slot] = {Event.tfinger.x, Event.tfinger.y, TouchPhase::Down};
        }
        if (DirectTouch && Event.type == SDL_EVENT_FINGER_MOTION && IsRuntimeWindow(Event.tfinger.windowID))
        {
            const int Slot = MapFingerSlot(Event.tfinger.fingerID);
            Touches_[Slot] = {Event.tfinger.x, Event.tfinger.y, TouchPhase::Moved};
        }
        if (DirectTouch && Event.type == SDL_EVENT_FINGER_UP && IsRuntimeWindow(Event.tfinger.windowID))
        {
            const int Slot = MapFingerSlot(Event.tfinger.fingerID);
            Touches_[Slot] = {Event.tfinger.x, Event.tfinger.y, TouchPhase::Up};
            FingerSlotMap_.erase(Event.tfinger.fingerID);
        }
        if (Event.type == SDL_EVENT_SENSOR_UPDATE && AccelSensor_ &&
            Event.sensor.which == SDL_GetSensorID(AccelSensor_))
        {
            AccelX_ = Event.sensor.data[0];
            AccelY_ = Event.sensor.data[1];
            AccelZ_ = Event.sensor.data[2];
        }
    }

    // === Event dispatch (called by PollEvents) ===

    void InputSystem::OnKeyDown(const char* KeyName)
    {
        AnyKeyPressed_ = true;
        LastKeyText_.clear();
        if (KeyName && KeyName[0] != '\0' && KeyName[1] == '\0')
        {
            LastKeyText_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(KeyName[0]))));
        }
    }

    void InputSystem::OnMouseMotion(int Xrel, int Yrel)
    {
        MouseDx_ += Xrel;
        MouseDy_ += Yrel;
    }

    void InputSystem::OnMouseButtonDown(int Button)
    {
        // SDL button constants: 1=left, 2=middle, 3=right
        if (Button == 1)
        {
            MouseButtonMask_ |= 1;
        }
        else if (Button == 3)
        {
            MouseButtonMask_ |= 2;
        }
        else if (Button == 2)
        {
            MouseButtonMask_ |= 4;
        }
    }

    void InputSystem::OnMouseButtonUp(int Button)
    {
        if (Button == 1)
        {
            MouseButtonMask_ &= ~1;
        }
        else if (Button == 3)
        {
            MouseButtonMask_ &= ~2;
        }
        else if (Button == 2)
        {
            MouseButtonMask_ &= ~4;
        }
    }

    // === Keyboard queries ===

    namespace
    {
        [[nodiscard]] SDL_Scancode MapDbScancode(int DbScancode)
        {
            switch (DbScancode)
            {
                // Row 1: number row
                case 1:
                    return SDL_SCANCODE_ESCAPE;
                case 2:
                    return SDL_SCANCODE_1;
                case 3:
                    return SDL_SCANCODE_2;
                case 4:
                    return SDL_SCANCODE_3;
                case 5:
                    return SDL_SCANCODE_4;
                case 6:
                    return SDL_SCANCODE_5;
                case 7:
                    return SDL_SCANCODE_6;
                case 8:
                    return SDL_SCANCODE_7;
                case 9:
                    return SDL_SCANCODE_8;
                case 10:
                    return SDL_SCANCODE_9;
                case 11:
                    return SDL_SCANCODE_0;
                case 12:
                    return SDL_SCANCODE_MINUS;
                case 13:
                    return SDL_SCANCODE_EQUALS;
                case 14:
                    return SDL_SCANCODE_BACKSPACE;

                // Row 2: QWERTY
                case 15:
                    return SDL_SCANCODE_TAB;
                case 16:
                    return SDL_SCANCODE_Q;
                case 17:
                    return SDL_SCANCODE_W;
                case 18:
                    return SDL_SCANCODE_E;
                case 19:
                    return SDL_SCANCODE_R;
                case 20:
                    return SDL_SCANCODE_T;
                case 21:
                    return SDL_SCANCODE_Y;
                case 22:
                    return SDL_SCANCODE_U;
                case 23:
                    return SDL_SCANCODE_I;
                case 24:
                    return SDL_SCANCODE_O;
                case 25:
                    return SDL_SCANCODE_P;
                case 26:
                    return SDL_SCANCODE_LEFTBRACKET;
                case 27:
                    return SDL_SCANCODE_RIGHTBRACKET;
                case 28:
                    return SDL_SCANCODE_RETURN;

                // Row 3: ASDF
                case 29:
                    return SDL_SCANCODE_LCTRL;
                case 30:
                    return SDL_SCANCODE_A;
                case 31:
                    return SDL_SCANCODE_S;
                case 32:
                    return SDL_SCANCODE_D;
                case 33:
                    return SDL_SCANCODE_F;
                case 34:
                    return SDL_SCANCODE_G;
                case 35:
                    return SDL_SCANCODE_H;
                case 36:
                    return SDL_SCANCODE_J;
                case 37:
                    return SDL_SCANCODE_K;
                case 38:
                    return SDL_SCANCODE_L;
                case 39:
                    return SDL_SCANCODE_SEMICOLON;
                case 40:
                    return SDL_SCANCODE_APOSTROPHE;
                case 41:
                    return SDL_SCANCODE_GRAVE;

                // Row 4: ZXCV
                case 42:
                    return SDL_SCANCODE_LSHIFT;
                case 43:
                    return SDL_SCANCODE_BACKSLASH;
                case 44:
                    return SDL_SCANCODE_Z;
                case 45:
                    return SDL_SCANCODE_X;
                case 46:
                    return SDL_SCANCODE_C;
                case 47:
                    return SDL_SCANCODE_V;
                case 48:
                    return SDL_SCANCODE_B;
                case 49:
                    return SDL_SCANCODE_N;
                case 50:
                    return SDL_SCANCODE_M;
                case 51:
                    return SDL_SCANCODE_COMMA;
                case 52:
                    return SDL_SCANCODE_PERIOD;
                case 53:
                    return SDL_SCANCODE_SLASH;
                case 54:
                    return SDL_SCANCODE_RSHIFT;
                case 55:
                    return SDL_SCANCODE_KP_MULTIPLY;

                // Bottom row
                case 56:
                    return SDL_SCANCODE_LALT;
                case 57:
                    return SDL_SCANCODE_SPACE;
                case 58:
                    return SDL_SCANCODE_CAPSLOCK;

                // Function keys
                case 59:
                    return SDL_SCANCODE_F1;
                case 60:
                    return SDL_SCANCODE_F2;
                case 61:
                    return SDL_SCANCODE_F3;
                case 62:
                    return SDL_SCANCODE_F4;
                case 63:
                    return SDL_SCANCODE_F5;
                case 64:
                    return SDL_SCANCODE_F6;
                case 65:
                    return SDL_SCANCODE_F7;
                case 66:
                    return SDL_SCANCODE_F8;
                case 67:
                    return SDL_SCANCODE_F9;
                case 68:
                    return SDL_SCANCODE_F10;
                case 69:
                    return SDL_SCANCODE_NUMLOCKCLEAR;
                case 70:
                    return SDL_SCANCODE_SCROLLLOCK;

                // Numpad
                case 71:
                    return SDL_SCANCODE_KP_7;
                case 72:
                    return SDL_SCANCODE_KP_8;
                case 73:
                    return SDL_SCANCODE_KP_9;
                case 74:
                    return SDL_SCANCODE_KP_MINUS;
                case 75:
                    return SDL_SCANCODE_KP_4;
                case 76:
                    return SDL_SCANCODE_KP_5;
                case 77:
                    return SDL_SCANCODE_KP_6;
                case 78:
                    return SDL_SCANCODE_KP_PLUS;
                case 79:
                    return SDL_SCANCODE_KP_1;
                case 80:
                    return SDL_SCANCODE_KP_2;
                case 81:
                    return SDL_SCANCODE_KP_3;
                case 82:
                    return SDL_SCANCODE_KP_0;
                case 83:
                    return SDL_SCANCODE_KP_PERIOD;
                case 86:
                    return SDL_SCANCODE_NONUSBACKSLASH;
                case 87:
                    return SDL_SCANCODE_F11;
                case 88:
                    return SDL_SCANCODE_F12;
                case 100:
                    return SDL_SCANCODE_F13;
                case 101:
                    return SDL_SCANCODE_F14;
                case 102:
                    return SDL_SCANCODE_F15;

                // International and media keys in the DirectInput table
                case 112:
                    return SDL_SCANCODE_LANG3;
                case 121:
                    return SDL_SCANCODE_INTERNATIONAL4;
                case 123:
                    return SDL_SCANCODE_INTERNATIONAL5;
                case 125:
                    return SDL_SCANCODE_INTERNATIONAL3;
                case 141:
                    return SDL_SCANCODE_KP_EQUALS;
                case 144:
                    return SDL_SCANCODE_MEDIA_PREVIOUS_TRACK;
                case 145:
                    return SDL_SCANCODE_INTERNATIONAL2;
                case 146:
                    return SDL_SCANCODE_SEMICOLON;
                case 147:
                    return SDL_SCANCODE_MINUS;
                case 148:
                    return SDL_SCANCODE_LANG2;
                case 149:
                    return SDL_SCANCODE_STOP;
                case 150:
                    return SDL_SCANCODE_INTERNATIONAL1;
                case 151:
                    return SDL_SCANCODE_INTERNATIONAL6;
                case 153:
                    return SDL_SCANCODE_MEDIA_NEXT_TRACK;
                case 156:
                    return SDL_SCANCODE_KP_ENTER;
                case 157:
                    return SDL_SCANCODE_RCTRL;
                case 160:
                    return SDL_SCANCODE_MUTE;
                case 162:
                    return SDL_SCANCODE_MEDIA_PLAY_PAUSE;
                case 164:
                    return SDL_SCANCODE_MEDIA_STOP;
                case 174:
                    return SDL_SCANCODE_VOLUMEDOWN;
                case 176:
                    return SDL_SCANCODE_VOLUMEUP;
                case 178:
                    return SDL_SCANCODE_AC_HOME;
                case 179:
                    return SDL_SCANCODE_KP_COMMA;
                case 181:
                    return SDL_SCANCODE_KP_DIVIDE;
                case 183:
                    return SDL_SCANCODE_PRINTSCREEN;
                case 184:
                    return SDL_SCANCODE_RALT;
                case 197:
                    return SDL_SCANCODE_PAUSE;

                // Navigation
                case 199:
                    return SDL_SCANCODE_HOME;
                case 200:
                    return SDL_SCANCODE_UP;
                case 201:
                    return SDL_SCANCODE_PAGEUP;
                case 203:
                    return SDL_SCANCODE_LEFT;
                case 205:
                    return SDL_SCANCODE_RIGHT;
                case 207:
                    return SDL_SCANCODE_END;
                case 208:
                    return SDL_SCANCODE_DOWN;
                case 209:
                    return SDL_SCANCODE_PAGEDOWN;
                case 210:
                    return SDL_SCANCODE_INSERT;
                case 211:
                    return SDL_SCANCODE_DELETE;
                case 219:
                    return SDL_SCANCODE_LGUI;
                case 220:
                    return SDL_SCANCODE_RGUI;
                case 221:
                    return SDL_SCANCODE_APPLICATION;
                case 222:
                    return SDL_SCANCODE_POWER;
                case 223:
                    return SDL_SCANCODE_SLEEP;
                case 227:
                    return SDL_SCANCODE_WAKE;
                case 229:
                    return SDL_SCANCODE_AC_SEARCH;
                case 230:
                    return SDL_SCANCODE_AC_BOOKMARKS;
                case 231:
                    return SDL_SCANCODE_AC_REFRESH;
                case 232:
                    return SDL_SCANCODE_AC_STOP;
                case 233:
                    return SDL_SCANCODE_AC_FORWARD;
                case 234:
                    return SDL_SCANCODE_AC_BACK;
                case 237:
                    return SDL_SCANCODE_MEDIA_SELECT;

                default:
                    return SDL_SCANCODE_UNKNOWN;
            }
        }

        [[nodiscard]] SDL_Scancode MapVirtualKey(int VirtualKey)
        {
            if (VirtualKey >= 0x41 && VirtualKey <= 0x5A)
                return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (VirtualKey - 0x41));
            if (VirtualKey >= 0x70 && VirtualKey <= 0x7B)
                return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (VirtualKey - 0x70));
            if (VirtualKey >= 0x7C && VirtualKey <= 0x87)
                return static_cast<SDL_Scancode>(SDL_SCANCODE_F13 + (VirtualKey - 0x7C));
            switch (VirtualKey)
            {
                case 0x08:
                    return SDL_SCANCODE_BACKSPACE;
                case 0x09:
                    return SDL_SCANCODE_TAB;
                case 0x0D:
                    return SDL_SCANCODE_RETURN;
                case 0x12:
                    return SDL_SCANCODE_LALT;
                case 0x13:
                    return SDL_SCANCODE_PAUSE;
                case 0x14:
                    return SDL_SCANCODE_CAPSLOCK;
                case 0x1B:
                    return SDL_SCANCODE_ESCAPE;
                case 0x20:
                    return SDL_SCANCODE_SPACE;
                case 0x21:
                    return SDL_SCANCODE_PAGEUP;
                case 0x22:
                    return SDL_SCANCODE_PAGEDOWN;
                case 0x23:
                    return SDL_SCANCODE_END;
                case 0x24:
                    return SDL_SCANCODE_HOME;
                case 0x25:
                    return SDL_SCANCODE_LEFT;
                case 0x26:
                    return SDL_SCANCODE_UP;
                case 0x27:
                    return SDL_SCANCODE_RIGHT;
                case 0x28:
                    return SDL_SCANCODE_DOWN;
                case 0x2C:
                    return SDL_SCANCODE_PRINTSCREEN;
                case 0x2D:
                    return SDL_SCANCODE_INSERT;
                case 0x2E:
                    return SDL_SCANCODE_DELETE;
                case 0x30:
                    return SDL_SCANCODE_0;
                case 0x31:
                    return SDL_SCANCODE_1;
                case 0x32:
                    return SDL_SCANCODE_2;
                case 0x33:
                    return SDL_SCANCODE_3;
                case 0x34:
                    return SDL_SCANCODE_4;
                case 0x35:
                    return SDL_SCANCODE_5;
                case 0x36:
                    return SDL_SCANCODE_6;
                case 0x37:
                    return SDL_SCANCODE_7;
                case 0x38:
                    return SDL_SCANCODE_8;
                case 0x39:
                    return SDL_SCANCODE_9;
                case 0x5B:
                    return SDL_SCANCODE_LGUI;
                case 0x5C:
                    return SDL_SCANCODE_RGUI;
                case 0x5D:
                    return SDL_SCANCODE_APPLICATION;
                case 0x60:
                    return SDL_SCANCODE_KP_0;
                case 0x61:
                    return SDL_SCANCODE_KP_1;
                case 0x62:
                    return SDL_SCANCODE_KP_2;
                case 0x63:
                    return SDL_SCANCODE_KP_3;
                case 0x64:
                    return SDL_SCANCODE_KP_4;
                case 0x65:
                    return SDL_SCANCODE_KP_5;
                case 0x66:
                    return SDL_SCANCODE_KP_6;
                case 0x67:
                    return SDL_SCANCODE_KP_7;
                case 0x68:
                    return SDL_SCANCODE_KP_8;
                case 0x69:
                    return SDL_SCANCODE_KP_9;
                case 0x6A:
                    return SDL_SCANCODE_KP_MULTIPLY;
                case 0x6B:
                    return SDL_SCANCODE_KP_PLUS;
                case 0x6D:
                    return SDL_SCANCODE_KP_MINUS;
                case 0x6E:
                    return SDL_SCANCODE_KP_PERIOD;
                case 0x6F:
                    return SDL_SCANCODE_KP_DIVIDE;
                case 0x90:
                    return SDL_SCANCODE_NUMLOCKCLEAR;
                case 0x91:
                    return SDL_SCANCODE_SCROLLLOCK;
                case 0xA0:
                    return SDL_SCANCODE_LSHIFT;
                case 0xA1:
                    return SDL_SCANCODE_RSHIFT;
                case 0xA2:
                    return SDL_SCANCODE_LCTRL;
                case 0xA3:
                    return SDL_SCANCODE_RCTRL;
                case 0xA4:
                    return SDL_SCANCODE_LALT;
                case 0xA5:
                    return SDL_SCANCODE_RALT;
                default:
                    return SDL_SCANCODE_UNKNOWN;
            }
        }
    } // namespace

    int InputSystem::SdlScancodeForDarkBasic(int DbScancode)
    {
        return static_cast<int>(MapDbScancode(DbScancode));
    }

    int InputSystem::Upkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_UP)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        if (Keys && Keys[SDL_SCANCODE_UP])
            return 1;
        if (MobileInputEmulation_ && TouchInZoneUp())
            return 1;
        return 0;
    }

    int InputSystem::Leftkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_LEFT)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        if (Keys && Keys[SDL_SCANCODE_LEFT])
            return 1;
        if (MobileInputEmulation_ && TouchInZoneLeft())
            return 1;
        return 0;
    }

    int InputSystem::Rightkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_RIGHT)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        if (Keys && Keys[SDL_SCANCODE_RIGHT])
            return 1;
        if (MobileInputEmulation_ && TouchInZoneRight())
            return 1;
        return 0;
    }

    int InputSystem::Downkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_DOWN)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        if (Keys && Keys[SDL_SCANCODE_DOWN])
            return 1;
        if (MobileInputEmulation_ && TouchInZoneDown())
            return 1;
        return 0;
    }

    int InputSystem::Spacekey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_SPACE)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        if (Keys && Keys[SDL_SCANCODE_SPACE])
            return 1;
        if (MobileInputEmulation_ && TouchInZoneCenter())
            return 1;
        return 0;
    }

    int InputSystem::Controlkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(SDL_SCANCODE_LCTRL) || HeldScancodes_.contains(SDL_SCANCODE_RCTRL))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        return Keys && (Keys[SDL_SCANCODE_LCTRL] || Keys[SDL_SCANCODE_RCTRL]) ? 1 : 0;
    }

    int InputSystem::Shiftkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(SDL_SCANCODE_LSHIFT) || HeldScancodes_.contains(SDL_SCANCODE_RSHIFT))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        return Keys && (Keys[SDL_SCANCODE_LSHIFT] || Keys[SDL_SCANCODE_RSHIFT]) ? 1 : 0;
    }

    int InputSystem::Returnkey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_RETURN)) ||
            HeldScancodes_.contains(static_cast<int>(SDL_SCANCODE_KP_ENTER)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        return Keys && (Keys[SDL_SCANCODE_RETURN] || Keys[SDL_SCANCODE_KP_ENTER]) ? 1 : 0;
    }

    int InputSystem::Escapekey() const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        if (HeldScancodes_.contains(SDL_SCANCODE_ESCAPE))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        return Keys && Keys[SDL_SCANCODE_ESCAPE] ? 1 : 0;
    }

    int InputSystem::Keystate(int DbScancode) const
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return 0;
        }
        SDL_PumpEvents();
        const SDL_Scancode Sc = MapDbScancode(DbScancode);
        if (Sc == SDL_SCANCODE_UNKNOWN)
        {
            return 0;
        }
        if (HeldScancodes_.contains(static_cast<int>(Sc)))
            return 1;
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        return Keys && Keys[Sc] ? 1 : 0;
    }

    int InputSystem::GetKeyState(int VirtualKey) const
    {
        if (VirtualKey < 0 || VirtualKey > 255)
            return 0;
        SDL_PumpEvents();
        bool Pressed = false;
        if (VirtualKey == 0x01)
            Pressed = (MouseButtonMask_ & 1) != 0;
        else if (VirtualKey == 0x02)
            Pressed = (MouseButtonMask_ & 2) != 0;
        else if (VirtualKey == 0x04)
            Pressed = (MouseButtonMask_ & 4) != 0;
        else if (VirtualKey == 0x10)
            Pressed = Shiftkey() != 0;
        else if (VirtualKey == 0x11)
            Pressed = Controlkey() != 0;
        else
        {
            const SDL_Scancode Scancode = MapVirtualKey(VirtualKey);
            const bool* Keys = SDL_GetKeyboardState(nullptr);
            Pressed = Scancode != SDL_SCANCODE_UNKNOWN &&
                      (HeldScancodes_.contains(static_cast<int>(Scancode)) || (Keys && Keys[Scancode]));
        }
        const SDL_Keymod Modifiers = SDL_GetModState();
        bool Toggled = (VirtualKey == 0x14 && (Modifiers & SDL_KMOD_CAPS) != 0) ||
                       (VirtualKey == 0x90 && (Modifiers & SDL_KMOD_NUM) != 0) ||
                       (VirtualKey == 0x91 && (Modifiers & SDL_KMOD_SCROLL) != 0);
        Toggled = Toggled != KeyStateToggle_[static_cast<std::size_t>(VirtualKey)];
        const uint16_t Bits = static_cast<uint16_t>((Pressed ? 0x8000 : 0) | (Toggled ? 1 : 0));
        return static_cast<int>(static_cast<int16_t>(Bits));
    }

    bool InputSystem::SetKeyStateToggle(int VirtualKey, int Toggle, std::string* Error)
    {
        if (VirtualKey < 0 || VirtualKey > 255)
        {
            if (Error)
                *Error = "SET KEY STATE TOGGLE virtual key must be between zero and 255";
            return false;
        }
        const bool Requested = Toggle != 0;
        const bool Current = (GetKeyState(VirtualKey) & 1) != 0;
        if (Current != Requested)
            KeyStateToggle_[static_cast<std::size_t>(VirtualKey)] =
                !KeyStateToggle_[static_cast<std::size_t>(VirtualKey)];
        return true;
    }

    std::string InputSystem::Inkey()
    {
        if (LastKeyText_.size() != 1 || !HeldScancodes_.contains(LastKeyScancode_))
            return {};
        char Result = LastKeyText_.front();
        const unsigned char Character = static_cast<unsigned char>(Result);
        if (std::isalpha(Character) && (LastKeyShift_ != LastKeyCaps_))
            Result = static_cast<char>(std::toupper(Character));
        else if (LastKeyShift_)
        {
            constexpr const char* Unshifted = "1234567890-=[]\\;',./`";
            constexpr const char* Shifted = "!@#$%^&*()_+{}|:\"<>?~";
            if (const char* Position = std::strchr(Unshifted, Result))
                Result = Shifted[Position - Unshifted];
        }
        return std::string(1, Result);
    }

    // === Mouse queries ===

    int InputSystem::Mouseclick() const
    {
        return MouseButtonMask_;
    }

    int InputSystem::Mousemovex()
    {
        const int Dx = MouseDx_;
        MouseDx_ = 0;
        return Dx;
    }

    int InputSystem::Mousemovey()
    {
        const int Dy = MouseDy_;
        MouseDy_ = 0;
        return Dy;
    }

    int InputSystem::Mousez() const
    {
        return MouseWheel_;
    }

    int InputSystem::Mousemovez()
    {
        const int Delta = MouseWheelDelta_;
        MouseWheelDelta_ = 0;
        return Delta;
    }

    int InputSystem::Scancode() const
    {
        if (!Kernel_.Core().EnsureInitialized())
            return 0;
        SDL_PumpEvents();
        const bool* Keys = SDL_GetKeyboardState(nullptr);
        if (!Keys)
            return 0;
        for (int Code = 1; Code < 256; ++Code)
        {
            const SDL_Scancode Sc = MapDbScancode(Code);
            if (Sc != SDL_SCANCODE_UNKNOWN && HeldScancodes_.contains(static_cast<int>(Sc)))
                return Code;
            if (Sc != SDL_SCANCODE_UNKNOWN && Keys[Sc])
                return Code;
        }
        return 0;
    }

    int InputSystem::Mousex() const
    {
        if (!MousePositionKnown_)
        {
            float X = 0.0f;
            float Y = 0.0f;
            SDL_GetMouseState(&X, &Y);
            return static_cast<int>(X);
        }
        return MouseX_;
    }

    int InputSystem::Mousey() const
    {
        if (!MousePositionKnown_)
        {
            float X = 0.0f;
            float Y = 0.0f;
            SDL_GetMouseState(&X, &Y);
            return static_cast<int>(Y);
        }
        return MouseY_;
    }

    bool InputSystem::PositionMouse(int X, int Y, std::string* Error)
    {
        if (X < 0 && (X == std::numeric_limits<int>::min() || Y == std::numeric_limits<int>::min()))
        {
            if (Error)
                *Error = "POSITION MOUSE coordinates exceed the integer range";
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "POSITION MOUSE could not initialize input";
            return false;
        }
        if (X < 0)
        {
            const int GlobalX = -X;
            const int GlobalY = -Y;
            if (!SDL_WarpMouseGlobal(static_cast<float>(GlobalX), static_cast<float>(GlobalY)))
            {
                if (Error)
                    *Error = std::string("POSITION MOUSE global warp failed: ") + SDL_GetError();
                return false;
            }
            MouseX_ = GlobalX;
            MouseY_ = GlobalY;
        }
        else
        {
            if (!Kernel_.Core().Window())
            {
                if (Error)
                    *Error = "POSITION MOUSE requires an active window";
                return false;
            }
            SDL_WarpMouseInWindow(Kernel_.Core().Window(), static_cast<float>(X), static_cast<float>(Y));
            MouseX_ = X;
            MouseY_ = Y;
        }
        MousePositionKnown_ = true;
        MouseDx_ = 0;
        MouseDy_ = 0;
        return true;
    }

    bool InputSystem::ChangeMouse(int CursorId, std::string* Error)
    {
        if (!ChangeMouse(CursorId, 0, Error))
            return false;
        return true;
    }

    bool InputSystem::ChangeMouse(int CursorId, int TouchFriendly, std::string* Error)
    {
        if (TouchFriendly != 0 && TouchFriendly != 1)
        {
            if (Error)
                *Error = "CHANGE MOUSE touch-friendly mode must be zero or one";
            return false;
        }
        if (CursorId == 32)
        {
            if (!SDL_HideCursor())
            {
                if (Error)
                    *Error = std::string("CHANGE MOUSE could not hide the cursor: ") + SDL_GetError();
                return false;
            }
            return true;
        }
        if (CursorId >= 2 && CursorId <= 31)
        {
            if (Error)
                *Error = "CHANGE MOUSE project cursor slots are not available without cursor assets";
            return false;
        }
        if (CursorId != 0 && CursorId != 1)
        {
            if (Error)
                *Error = "CHANGE MOUSE cursor number must be between zero and 32";
            return false;
        }
        SDL_Cursor* Cursor = SDL_CreateSystemCursor(CursorId == 1 ? SDL_SYSTEM_CURSOR_WAIT : SDL_SYSTEM_CURSOR_DEFAULT);
        if (!Cursor)
        {
            if (Error)
                *Error = std::string("CHANGE MOUSE could not create the cursor: ") + SDL_GetError();
            return false;
        }
        if (!SDL_SetCursor(Cursor))
        {
            if (Error)
                *Error = std::string("CHANGE MOUSE could not set the cursor: ") + SDL_GetError();
            SDL_DestroyCursor(Cursor);
            return false;
        }
        if (!SDL_ShowCursor())
        {
            SDL_DestroyCursor(Cursor);
            {
                if (Error)
                    *Error = std::string("CHANGE MOUSE could not show the cursor: ") + SDL_GetError();
                return false;
            }
        }
        if (Cursor_)
            SDL_DestroyCursor(Cursor_);
        Cursor_ = Cursor;
        return true;
    }

    // === Entry buffer (non-blocking) ===

    bool InputSystem::Entry(std::string& Text, int AutoBackspaceMode, std::string* Error)
    {
        if (AutoBackspaceMode != 0 && AutoBackspaceMode != 1)
        {
            if (Error)
                *Error = "ENTRY$ auto-backspace mode must be zero or one";
            return false;
        }
        if (!TextInputActive_ && Kernel_.Core().Window())
        {
            if (!SDL_StartTextInput(Kernel_.Core().Window()))
            {
                if (Error)
                    *Error = std::string("ENTRY$ could not start text input: ") + SDL_GetError();
                return false;
            }
            TextInputActive_ = true;
        }
        if (AutoBackspaceMode == 0)
        {
            Text = EntryBuffer_;
            return true;
        }
        std::string Result;
        Result.reserve(EntryBuffer_.size());
        for (const char Character : EntryBuffer_)
        {
            if (Character == '\b')
            {
                if (!Result.empty())
                    Result.pop_back();
            }
            else
            {
                Result.push_back(Character);
            }
        }
        Text = std::move(Result);
        return true;
    }

    void InputSystem::ClearEntryBuffer()
    {
        EntryBuffer_.clear();
    }

    bool InputSystem::BeginTextInput()
    {
        if (!Kernel_.Core().EnsureInitialized() || !Kernel_.Core().Window())
            return false;
        InputBuffer_.clear();
        TextInputSubmitted_ = false;
        if (!SDL_StartTextInput(Kernel_.Core().Window()))
            return false;
        TextInputActive_ = true;
        InputModeActive_ = true;
        return true;
    }

    std::string InputSystem::TakeTextInput()
    {
        if (!TextInputSubmitted_)
            return {};
        std::string Result = std::move(InputBuffer_);
        InputBuffer_.clear();
        return Result;
    }

    bool InputSystem::TextInputSubmitted() const
    {
        return TextInputSubmitted_;
    }

    bool InputSystem::EndTextInput(std::string* Error)
    {
        bool Stopped = true;
        if (TextInputActive_ && Kernel_.Core().Window())
        {
            Stopped = SDL_StopTextInput(Kernel_.Core().Window());
            if (!Stopped)
                if (Error)
                    *Error = std::string("Cannot stop text input: ") + SDL_GetError();
        }
        TextInputActive_ = false;
        InputModeActive_ = false;
        TextInputSubmitted_ = false;
        InputBuffer_.clear();
        return Stopped;
    }

    // === Blocking text input ===

    std::string InputSystem::InputBlocking(const std::string& /*prompt*/)
    {
        if (!Kernel_.Core().EnsureInitialized())
        {
            return {};
        }

        SDL_StartTextInput(Kernel_.Core().Window());
        std::string Buffer;
        bool Done = false;
        while (Kernel_.Core().Running() && !Done)
        {
            SDL_Event E;
            while (SDL_PollEvent(&E))
            {
                if (E.type == SDL_EVENT_QUIT)
                {
                    Kernel_.Core().Running() = false;
                    Done = true;
                }
                else if (E.type == SDL_EVENT_TEXT_INPUT)
                {
                    Buffer += E.text.text;
                }
                else if (E.type == SDL_EVENT_KEY_DOWN)
                {
                    if (E.key.key == SDLK_RETURN || E.key.key == SDLK_KP_ENTER)
                    {
                        Done = true;
                    }
                    else if (E.key.key == SDLK_BACKSPACE && !Buffer.empty())
                    {
                        Buffer.pop_back();
                    }
                }
            }
            if (!Done)
            {
                SDL_Delay(10);
            }
        }
        SDL_StopTextInput(Kernel_.Core().Window());
        return Buffer;
    }

    // === Control devices ===

    void InputSystem::TryOpenJoystick()
    {
        if (!Joystick_)
            (void)SelectJoystick(ControlDeviceIndex_);
        JoystickOpenAttempted_ = true;
    }

    void InputSystem::TryOpenHaptic()
    {
        if (Haptic_ || HapticOpenAttempted_ || !Joystick_)
            return;
        HapticOpenAttempted_ = true;
        if (!SDL_IsJoystickHaptic(Joystick_))
        {
            SDL_ClearError();
            return;
        }
        Haptic_ = SDL_OpenHapticFromJoystick(Joystick_);
        if (!Haptic_)
            SDL_ClearError();
    }

    void InputSystem::CloseHaptic()
    {
        if (Haptic_)
        {
            if (AngleHapticEffect_ >= 0)
                SDL_DestroyHapticEffect(Haptic_, AngleHapticEffect_);
            if (WaterHapticEffect_ >= 0)
                SDL_DestroyHapticEffect(Haptic_, WaterHapticEffect_);
            if (ChainsawHapticEffect_ >= 0)
                SDL_DestroyHapticEffect(Haptic_, ChainsawHapticEffect_);
            if (ImpactHapticEffect_ >= 0)
                SDL_DestroyHapticEffect(Haptic_, ImpactHapticEffect_);
            SDL_CloseHaptic(Haptic_);
            Haptic_ = nullptr;
        }
        AngleHapticEffect_ = -1;
        WaterHapticEffect_ = -1;
        ChainsawHapticEffect_ = -1;
        ImpactHapticEffect_ = -1;
        HapticRumbleInitialized_ = false;
        HapticOpenAttempted_ = false;
    }

    void InputSystem::CloseJoystick()
    {
        CloseHaptic();
        if (Joystick_)
        {
            SDL_CloseJoystick(Joystick_);
            Joystick_ = nullptr;
        }
        JoystickAxes_.fill(0);
        JoystickAxisKnown_.fill(false);
        JoystickButtons_.fill(false);
        JoystickButtonKnown_.fill(false);
        JoystickHats_.fill(SDL_HAT_CENTERED);
        JoystickHatKnown_.fill(false);
    }

    namespace
    {
        [[nodiscard]] int ScaleAxis(int16_t Raw)
        {
            return std::clamp(static_cast<int>((static_cast<float>(Raw) / 32767.0f) * 1000.0f), -1000, 1000);
        }

        [[nodiscard]] int HatAngle(Uint8 Value)
        {
            switch (Value)
            {
                case SDL_HAT_UP:
                    return 0;
                case SDL_HAT_RIGHTUP:
                    return 4500;
                case SDL_HAT_RIGHT:
                    return 9000;
                case SDL_HAT_RIGHTDOWN:
                    return 13500;
                case SDL_HAT_DOWN:
                    return 18000;
                case SDL_HAT_LEFTDOWN:
                    return 22500;
                case SDL_HAT_LEFT:
                    return 27000;
                case SDL_HAT_LEFTUP:
                    return 31500;
                default:
                    return -1;
            }
        }
    } // namespace

    std::vector<ControlDeviceInfo> InputSystem::ControlDevices() const
    {
        std::vector<ControlDeviceInfo> Result;
        int Count = 0;
        SDL_JoystickID* Identifiers = SDL_GetJoysticks(&Count);
        std::unordered_map<std::string, int> Duplicates;
        for (int Index = 0; Identifiers && Index < Count; ++Index)
        {
            const char* DeviceName = SDL_GetJoystickNameForID(Identifiers[Index]);
            ControlDeviceInfo Item;
            Item.Name = DeviceName && *DeviceName ? DeviceName : "Joystick";
            Item.DuplicateIndex = Duplicates[Item.Name]++;
            SDL_Joystick* Device = Joystick_ && SDL_GetJoystickID(Joystick_) == Identifiers[Index]
                                       ? Joystick_
                                       : SDL_OpenJoystick(Identifiers[Index]);
            if (Device)
            {
                const SDL_PropertiesID Properties = SDL_GetJoystickProperties(Device);
                Item.HasForceFeedback = SDL_IsJoystickHaptic(Device) ||
                                        SDL_GetBooleanProperty(Properties, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
                SDL_ClearError();
                if (Device != Joystick_)
                    SDL_CloseJoystick(Device);
            }
            Result.push_back(std::move(Item));
        }
        SDL_free(Identifiers);
        return Result;
    }

    bool InputSystem::SelectJoystick(int Index)
    {
        int Count = 0;
        SDL_JoystickID* Identifiers = SDL_GetJoysticks(&Count);
        if (!Identifiers || Index < 0 || Index >= Count)
        {
            SDL_free(Identifiers);
            return false;
        }
        SDL_Joystick* Device = SDL_OpenJoystick(Identifiers[Index]);
        SDL_free(Identifiers);
        if (!Device)
            return false;
        CloseJoystick();
        Joystick_ = Device;
        ControlDeviceIndex_ = Index;
        JoystickOpenAttempted_ = true;
        return true;
    }

    bool InputSystem::SetControlDevice(const std::string& Name, int DuplicateIndex, std::string* Error)
    {
        if (DuplicateIndex < 0)
        {
            if (Error)
                *Error = "SET CONTROL DEVICE duplicate index must be non-negative";
            return false;
        }
        int Count = 0;
        SDL_JoystickID* Identifiers = SDL_GetJoysticks(&Count);
        int Match = 0;
        int Selected = -1;
        for (int Index = 0; Identifiers && Index < Count; ++Index)
        {
            const char* DeviceName = SDL_GetJoystickNameForID(Identifiers[Index]);
            if ((Name.empty() || (DeviceName && Name == DeviceName)) && Match++ == DuplicateIndex)
            {
                Selected = Index;
                break;
            }
        }
        SDL_free(Identifiers);
        if (Selected < 0 || !SelectJoystick(Selected))
        {
            if (Error)
                *Error = "SET CONTROL DEVICE could not find '" + Name + "'";
            return false;
        }
        return true;
    }

    bool InputSystem::SetControlDeviceIndex(int Index, std::string* Error)
    {
        if (!SelectJoystick(Index))
        {
            if (Error)
                *Error = "SET CONTROL DEVICE INDEX is outside the connected device range";
            return false;
        }
        return true;
    }

    int InputSystem::ControlDeviceIndex() const
    {
        return ControlDeviceIndex_;
    }

    std::string InputSystem::ControlDeviceName()
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (!Joystick_)
            return {};
        const char* Name = SDL_GetJoystickName(Joystick_);
        return Name ? Name : "";
    }

    int InputSystem::JoystickAxis(int Axis)
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (!Joystick_ || Axis < 0 || Axis >= SDL_GetNumJoystickAxes(Joystick_))
            return 0;
        SDL_UpdateJoysticks();
        const int16_t Raw = Axis < static_cast<int>(JoystickAxisKnown_.size()) && JoystickAxisKnown_[Axis]
                                ? JoystickAxes_[Axis]
                                : SDL_GetJoystickAxis(Joystick_, Axis);
        return ScaleAxis(Raw);
    }

    int InputSystem::JoystickButton(int Button)
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (!Joystick_ || Button < 0 || Button >= SDL_GetNumJoystickButtons(Joystick_))
            return 0;
        SDL_UpdateJoysticks();
        if (Button < static_cast<int>(JoystickButtonKnown_.size()) && JoystickButtonKnown_[Button])
            return JoystickButtons_[Button] ? 1 : 0;
        return SDL_GetJoystickButton(Joystick_, Button) ? 1 : 0;
    }

    int InputSystem::ControlDeviceX()
    {
        return JoystickAxis(0);
    }
    int InputSystem::ControlDeviceY()
    {
        return JoystickAxis(1);
    }
    int InputSystem::ControlDeviceZ()
    {
        return JoystickAxis(2);
    }

    int InputSystem::JoystickInformation(int Mode)
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (!Joystick_)
            return 0;
        const int AxisCount = SDL_GetNumJoystickAxes(Joystick_);
        switch (Mode)
        {
            case 0:
                return SDL_GetNumJoystickButtons(Joystick_);
            case 1:
            {
                const bool HasForceFeedback = SDL_IsJoystickHaptic(Joystick_) ||
                                              SDL_GetBooleanProperty(SDL_GetJoystickProperties(Joystick_),
                                                                     SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
                SDL_ClearError();
                return HasForceFeedback ? 1 : 0;
            }
            case 2:
                return AxisCount > 2 ? 1 : 0;
            case 3:
                return AxisCount > 3 ? 1 : 0;
            case 4:
                return AxisCount > 4 ? 1 : 0;
            case 5:
                return AxisCount > 5 ? 1 : 0;
            case 6:
                return std::clamp(AxisCount - 6, 0, 2);
            case 7:
                return SDL_GetNumJoystickHats(Joystick_);
            case 8:
                return JoystickDeadzonePercentage_;
            default:
                return 0;
        }
    }

    void InputSystem::SetJoystickDeadzone(int Percentage)
    {
        JoystickDeadzonePercentage_ = std::clamp(Percentage, 0, 99);
    }

    int InputSystem::JoystickUp()
    {
        return JoystickAxis(1) < -(JoystickDeadzonePercentage_ * 10) ? 1 : 0;
    }

    int InputSystem::JoystickDown()
    {
        return JoystickAxis(1) > JoystickDeadzonePercentage_ * 10 ? 1 : 0;
    }

    int InputSystem::JoystickLeft()
    {
        return JoystickAxis(0) < -(JoystickDeadzonePercentage_ * 10) ? 1 : 0;
    }

    int InputSystem::JoystickRight()
    {
        return JoystickAxis(0) > JoystickDeadzonePercentage_ * 10 ? 1 : 0;
    }

    int InputSystem::JoystickFireA()
    {
        return JoystickButton(0);
    }

    int InputSystem::JoystickFireB()
    {
        return JoystickButton(1);
    }

    int InputSystem::JoystickFireC()
    {
        return JoystickButton(2);
    }

    int InputSystem::JoystickFireD()
    {
        return JoystickButton(3);
    }

    int InputSystem::JoystickFireX(int Button)
    {
        if (Button < 0 || Button > 31)
            return 0;
        return JoystickButton(Button);
    }

    int InputSystem::JoystickX()
    {
        return JoystickAxis(0);
    }

    int InputSystem::JoystickY()
    {
        return JoystickAxis(1);
    }

    int InputSystem::JoystickZ()
    {
        return JoystickAxis(2);
    }

    int InputSystem::JoystickTwistX()
    {
        return JoystickAxis(3);
    }

    int InputSystem::JoystickTwistY()
    {
        return JoystickAxis(4);
    }

    int InputSystem::JoystickTwistZ()
    {
        return JoystickAxis(5);
    }

    int InputSystem::JoystickSliderA()
    {
        return JoystickAxis(6);
    }

    int InputSystem::JoystickSliderB()
    {
        return JoystickAxis(7);
    }

    int InputSystem::JoystickSliderC()
    {
        return JoystickAxis(8);
    }

    int InputSystem::JoystickSliderD()
    {
        return JoystickAxis(9);
    }

    int InputSystem::JoystickHatAngle(int Hat)
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (!Joystick_ || Hat < 0 || Hat > 3 || Hat >= SDL_GetNumJoystickHats(Joystick_))
            return -1;
        SDL_UpdateJoysticks();
        const Uint8 Value = JoystickHatKnown_[Hat] ? JoystickHats_[Hat] : SDL_GetJoystickHat(Joystick_, Hat);
        return HatAngle(Value);
    }

    void InputSystem::PlayHapticEffect(int& EffectId, const SDL_HapticEffect& Effect)
    {
        if (EffectId >= 0 && !SDL_UpdateHapticEffect(Haptic_, EffectId, &Effect))
        {
            SDL_DestroyHapticEffect(Haptic_, EffectId);
            EffectId = -1;
            SDL_ClearError();
        }
        if (EffectId < 0)
        {
            EffectId = SDL_CreateHapticEffect(Haptic_, &Effect);
            if (EffectId < 0)
            {
                SDL_ClearError();
                return;
            }
        }
        if (!SDL_RunHapticEffect(Haptic_, EffectId, 1))
            SDL_ClearError();
    }

    bool InputSystem::PlayWholeDeviceRumble(int Magnitude, int DurationMs, std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        Diagnostic.clear();
        detail::ForcePeriodicHapticParameters Parameters;
        if (!detail::MakeForcePeriodicHapticParameters("FORCE", Magnitude, DurationMs, Parameters, &Diagnostic))
            return false;
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (Joystick_ &&
            SDL_GetBooleanProperty(SDL_GetJoystickProperties(Joystick_), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false))
        {
            const Uint16 Strength = static_cast<Uint16>(Magnitude * std::numeric_limits<Uint16>::max() / 100);
            if (SDL_RumbleJoystick(Joystick_, Strength, Strength, Parameters.Duration))
                return true;
            SDL_ClearError();
        }

        TryOpenHaptic();
        if (!Haptic_ || !SDL_HapticRumbleSupported(Haptic_))
        {
            SDL_ClearError();
            return false;
        }
        if (!HapticRumbleInitialized_)
        {
            if (!SDL_InitHapticRumble(Haptic_))
            {
                SDL_ClearError();
                return false;
            }
            HapticRumbleInitialized_ = true;
        }
        if (!SDL_PlayHapticRumble(Haptic_, static_cast<float>(Magnitude) / 100.0f, Parameters.Duration))
        {
            SDL_ClearError();
            return false;
        }
        return true;
    }

    bool InputSystem::PlayPeriodicForce(int& EffectId, int PreferredType, int FallbackType, int Magnitude,
                                        int DurationMs, int PeriodMs, const char* Command, std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        detail::ForcePeriodicHapticParameters Parameters;
        if (!detail::MakeForcePeriodicHapticParameters(Command, Magnitude, DurationMs, Parameters, &Diagnostic))
            return false;
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        TryOpenHaptic();
        if (Haptic_)
        {
            const Uint32 Features = SDL_GetHapticFeatures(Haptic_);
            const int Type = (Features & static_cast<Uint32>(PreferredType)) != 0
                                 ? PreferredType
                                 : ((Features & static_cast<Uint32>(FallbackType)) != 0 ? FallbackType : 0);
            if (Type != 0)
            {
                SDL_HapticEffect Effect{};
                Effect.periodic.type = static_cast<SDL_HapticEffectType>(Type);
                Effect.periodic.direction.type = SDL_HAPTIC_POLAR;
                Effect.periodic.direction.dir[0] = 0;
                Effect.periodic.length = Parameters.Duration;
                Effect.periodic.period = static_cast<Uint16>(PeriodMs);
                Effect.periodic.magnitude = Parameters.Magnitude;
                PlayHapticEffect(EffectId, Effect);
                return true;
            }
        }
        SDL_ClearError();
        (void)PlayWholeDeviceRumble(Magnitude, DurationMs, &Diagnostic);
        if (!Diagnostic.empty())
            return false;
        return true;
    }

    bool InputSystem::ForceUp(int Magnitude, std::string* Error)
    {
        if (!ForceAngle(Magnitude, 0.0, 0, Error))
            return false;
        return true;
    }

    bool InputSystem::ForceDown(int Magnitude, std::string* Error)
    {
        if (!ForceAngle(Magnitude, 180.0, 0, Error))
            return false;
        return true;
    }

    bool InputSystem::ForceLeft(int Magnitude, std::string* Error)
    {
        if (!ForceAngle(Magnitude, 270.0, 0, Error))
            return false;
        return true;
    }

    bool InputSystem::ForceRight(int Magnitude, std::string* Error)
    {
        if (!ForceAngle(Magnitude, 90.0, 0, Error))
            return false;
        return true;
    }

    bool InputSystem::ForceChainsaw(int Magnitude, int DurationMs, std::string* Error)
    {
        if (!PlayPeriodicForce(ChainsawHapticEffect_, SDL_HAPTIC_SAWTOOTHDOWN, SDL_HAPTIC_SQUARE, Magnitude, DurationMs,
                               30, "FORCE CHAINSAW", Error))
            return false;
        return true;
    }

    bool InputSystem::ForceImpact(int Magnitude, int DurationMs, std::string* Error)
    {
        if (!PlayPeriodicForce(ImpactHapticEffect_, SDL_HAPTIC_SINE, SDL_HAPTIC_TRIANGLE, Magnitude, DurationMs, 80,
                               "FORCE IMPACT", Error))
            return false;
        return true;
    }

    void InputSystem::ForceNoEffect()
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (Joystick_ && !SDL_RumbleJoystick(Joystick_, 0, 0, 0))
            SDL_ClearError();
        TryOpenHaptic();
        if (!Haptic_)
            return;
        if (!SDL_StopHapticEffects(Haptic_))
            SDL_ClearError();
        if (HapticRumbleInitialized_ && !SDL_StopHapticRumble(Haptic_))
            SDL_ClearError();
    }

    void InputSystem::ForceAutoCenter(bool Enabled)
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        TryOpenHaptic();
        if (!Haptic_ || (SDL_GetHapticFeatures(Haptic_) & SDL_HAPTIC_AUTOCENTER) == 0)
        {
            SDL_ClearError();
            return;
        }
        if (!SDL_SetHapticAutocenter(Haptic_, Enabled ? 100 : 0))
            SDL_ClearError();
    }

    bool InputSystem::ForceAngle(int Magnitude, double Angle, int DurationMs, std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        detail::ForceAngleHapticParameters Parameters;
        if (!detail::MakeForceAngleHapticParameters(Magnitude, Angle, DurationMs, Parameters, &Diagnostic))
            return false;
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        TryOpenHaptic();
        if (!Haptic_ || (SDL_GetHapticFeatures(Haptic_) & SDL_HAPTIC_CONSTANT) == 0)
        {
            SDL_ClearError();
            return true;
        }

        SDL_HapticEffect Effect{};
        Effect.constant.type = SDL_HAPTIC_CONSTANT;
        Effect.constant.direction.type = SDL_HAPTIC_POLAR;
        Effect.constant.direction.dir[0] = Parameters.Direction;
        Effect.constant.length = Parameters.Duration;
        Effect.constant.level = Parameters.Level;
        PlayHapticEffect(AngleHapticEffect_, Effect);
        return true;
    }

    void InputSystem::ForceShoot(int Magnitude, int DurationMs)
    {
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        if (!Joystick_)
            return;
        if (!SDL_GetBooleanProperty(SDL_GetJoystickProperties(Joystick_), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false))
            return;
        const Uint16 Strength = static_cast<Uint16>(std::clamp(Magnitude, 0, 100) * 65535 / 100);
        if (!SDL_RumbleJoystick(Joystick_, Strength, Strength, static_cast<Uint32>(std::max(0, DurationMs))))
            SDL_ClearError();
    }

    bool InputSystem::ForceWaterEffect(int Magnitude, int DurationMs, std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        detail::ForceWaterHapticParameters Parameters;
        if (!detail::MakeForceWaterHapticParameters(Magnitude, DurationMs, Parameters, &Diagnostic))
            return false;
        if (!Joystick_ && !JoystickOpenAttempted_)
            TryOpenJoystick();
        TryOpenHaptic();
        if (!Haptic_ || (SDL_GetHapticFeatures(Haptic_) & SDL_HAPTIC_DAMPER) == 0)
        {
            SDL_ClearError();
            return true;
        }

        SDL_HapticEffect Effect{};
        Effect.condition.type = SDL_HAPTIC_DAMPER;
        Effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        Effect.condition.direction.dir[0] = 1;
        Effect.condition.direction.dir[1] = 1;
        Effect.condition.length = Parameters.Duration;
        for (int Axis = 0; Axis < std::min(2, SDL_GetNumHapticAxes(Haptic_)); ++Axis)
        {
            Effect.condition.right_sat[Axis] = Parameters.Saturation;
            Effect.condition.left_sat[Axis] = Parameters.Saturation;
            Effect.condition.right_coeff[Axis] = Parameters.Coefficient;
            Effect.condition.left_coeff[Axis] = Parameters.Coefficient;
        }
        PlayHapticEffect(WaterHapticEffect_, Effect);
        return true;
    }

    bool InputSystem::IsDirectTouchDevice(int DeviceType)
    {
        return DeviceType == static_cast<int>(SDL_TOUCH_DEVICE_DIRECT);
    }

    // === Touch input ===

    int InputSystem::MapFingerSlot(uint64_t SdlFingerId)
    {
        auto It = FingerSlotMap_.find(SdlFingerId);
        if (It != FingerSlotMap_.end())
            return It->second;
        const int Slot = NextFingerSlot_++;
        FingerSlotMap_[SdlFingerId] = Slot;
        return Slot;
    }

    int InputSystem::GetTouchPositionX(int FingerId) const
    {
        auto It = Touches_.find(FingerId);
        if (It == Touches_.end())
            return 0;
        return static_cast<int>(It->second.X * static_cast<float>(Kernel_.Core().WindowWidth()));
    }

    int InputSystem::GetTouchPositionY(int FingerId) const
    {
        auto It = Touches_.find(FingerId);
        if (It == Touches_.end())
            return 0;
        return static_cast<int>(It->second.Y * static_cast<float>(Kernel_.Core().WindowHeight()));
    }

    int InputSystem::IsTouchDown(int FingerId) const
    {
        auto It = Touches_.find(FingerId);
        return (It != Touches_.end() && It->second.Phase == TouchPhase::Down) ? 1 : 0;
    }

    int InputSystem::IsTouchMoved(int FingerId) const
    {
        auto It = Touches_.find(FingerId);
        return (It != Touches_.end() && It->second.Phase == TouchPhase::Moved) ? 1 : 0;
    }

    int InputSystem::IsTouchUp(int FingerId) const
    {
        auto It = Touches_.find(FingerId);
        return (It != Touches_.end() && It->second.Phase == TouchPhase::Up) ? 1 : 0;
    }

    // === Mobile input emulation (touch zones → virtual keys) ===

    bool InputSystem::TouchInZoneUp() const
    {
        for (const auto& [Id, Touch] : Touches_)
            if (Touch.Phase != TouchPhase::Up && Touch.Y < 0.333f)
                return true;
        return false;
    }

    bool InputSystem::TouchInZoneDown() const
    {
        for (const auto& [Id, Touch] : Touches_)
            if (Touch.Phase != TouchPhase::Up && Touch.Y > 0.666f)
                return true;
        return false;
    }

    bool InputSystem::TouchInZoneLeft() const
    {
        for (const auto& [Id, Touch] : Touches_)
            if (Touch.Phase != TouchPhase::Up && Touch.X < 0.333f)
                return true;
        return false;
    }

    bool InputSystem::TouchInZoneRight() const
    {
        for (const auto& [Id, Touch] : Touches_)
            if (Touch.Phase != TouchPhase::Up && Touch.X > 0.666f)
                return true;
        return false;
    }

    bool InputSystem::TouchInZoneCenter() const
    {
        for (const auto& [Id, Touch] : Touches_)
            if (Touch.Phase != TouchPhase::Up && Touch.X >= 0.333f && Touch.X <= 0.666f && Touch.Y >= 0.333f &&
                Touch.Y <= 0.666f)
                return true;
        return false;
    }

    void InputSystem::SetMobileInputEmulation(int Mode)
    {
        MobileInputEmulation_ = (Mode != 0);
    }

    void InputSystem::SetScreenOrientation(int Mode)
    {
        if (Mode == 0)
            SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait PortraitUpsideDown");
        else
            SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    }

    // === Accelerometer ===

    void InputSystem::TryOpenAccelerometer()
    {
        if (AccelOpenAttempted_)
            return;
        AccelOpenAttempted_ = true;

        int Count = 0;
        SDL_SensorID* Ids = SDL_GetSensors(&Count);
        if (!Ids)
            return;

        for (int I = 0; I < Count; ++I)
        {
            if (SDL_GetSensorTypeForID(Ids[I]) == SDL_SENSOR_ACCEL)
            {
                AccelSensor_ = SDL_OpenSensor(Ids[I]);
                break;
            }
        }
        SDL_free(Ids);
    }

} // namespace dbc
