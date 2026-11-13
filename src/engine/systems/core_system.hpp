#pragma once

#include <SDL3/SDL.h>
#include <chrono>
#include <optional>
#include <random>
#include <string>

namespace dbc
{
    class EngineKernel;

    // SDL hint (or environment variable) that selects headless execution: a hidden
    // window rendered offscreen, no application activation and the dummy audio driver.
    inline constexpr const char* KHeadlessHint = "OPENDBC_HEADLESS";

    class CoreSystem
    {
      public:
        explicit CoreSystem(EngineKernel& Kernel);
        ~CoreSystem();

        // --- Lifecycle ---
        bool Init();
        void Shutdown();
        bool IsRunning();
        bool EnsureInitialized();
        // Draws into a window the host owns instead of creating one: a platform with a single window per
        // display has none to spare. Call before the runtime initializes; Shutdown leaves the window alive.
        void UseHostWindow(SDL_Window* Window);

        // Whether the display keeps its proportions in a window that has others, between black bands.
        // True where the window cannot take the display's size: a phone, a tablet, a host's window.
        bool FitsDisplay() const;
        // The part of the window, in window coordinates, that shows the display.
        SDL_FRect DisplayArea() const;
        // Turns a position in the window into the position the program sees on its display.
        void WindowToDisplay(float& X, float& Y) const;
        // Whether the program chose its display's size, which a resized window then leaves alone.
        bool DisplayModeChosen() const;

        // --- Display / window ---
        bool SetDisplayMode(int Width, int Height, int Depth, int Vsync = 1, int Multisampling = 0,
                            int Multimonitor = 0, std::string* Error = nullptr);
        void HideMouse();
        void ShowMouse();
        void HideCursor();
        void ShowCursor();
        bool HideWindow(std::string* Error = nullptr);
        bool ShowWindow(std::string* Error = nullptr);
        bool MaximizeWindow(std::string* Error = nullptr);
        bool MinimizeWindow(std::string* Error = nullptr);
        bool RestoreWindow(std::string* Error = nullptr);
        bool SetWindowTitle(const std::string& Title, std::string* Error = nullptr);
        bool SetWindowPosition(int X, int Y, std::string* Error = nullptr);
        bool SetWindowSize(int Width, int Height, std::string* Error = nullptr);
        bool SetWindowOn(std::string* Error = nullptr);
        bool SetWindowOff(std::string* Error = nullptr);
        bool SetWindowLayout(int FullWindow, int TitleBar, int Icon, std::string* Error = nullptr);
        bool WindowExists(const std::string& Title) const;
        bool RaiseWindow(const std::string& Title = {}, std::string* Error = nullptr);
        int ScreenDepth() const;
        int ScreenType() const;
        int ConsumeScreenInvalid();
        void MarkScreenInvalid();
        int DisplayVSync() const;
        int DisplayMultisampling() const;
        int DisplayMultimonitor() const;

        // --- VSync ---
        void DisableVSync();

        // --- Escape key ---
        void DisableEscapekey();
        void EnableEscapekey();

        // --- Clipboard ---
        void WriteToClipboard(const std::string& Text);
        std::string GetClipboard();

        // --- Timing / sync ---
        void DoSyncOn();
        void DoSyncOff();
        void DoSyncRate(int Rate);
        std::optional<std::chrono::steady_clock::time_point> NextSyncDeadline() const;
        bool RefreshesWhileWaiting() const;
        void Sync();
        void FastSync();
        int Timer();
        std::string GetDate();
        std::string GetTime();

        // Mutable accessors (transitional)
        SDL_Window*& Window();
        int& WindowWidth();
        int& WindowHeight();
        bool& Initialized();
        bool& Running();
        bool& EscapeDisabled();
        bool& SyncOn();
        float DeltaTime() const;
        int& Fps();
        int& FramesThisSecond();
        Uint64& FpsWindowStart();
        Uint64& LastRenderTicks();
        float& CameraAspect();
        std::mt19937& Rng();
        bool Suspended() const;
        void SetSuspended(bool Suspended);
        // ALWAYS ACTIVE ON: keep running while minimised or in the background.
        bool AlwaysActive() const;
        void SetAlwaysActive(bool Enabled);
        bool Headless() const;

      private:
        std::string FormatLocalTime(const char* Fmt);

        EngineKernel& Kernel_;
        SDL_Window* Window_ = nullptr;
        bool HostWindow_ = false;
#ifdef DBC_MOBILE
        bool FitDisplay_ = true;
#else
        bool FitDisplay_ = false;
#endif
        bool DisplayModeChosen_ = false;
        int WindowWidth_ = 640;
        int WindowHeight_ = 480;
        // Logical BASIC depth; SDL_GPU render targets remain RGBA32.
        int ScreenDepth_ = 32;
        int DisplayVsync_ = 1;
        int DisplayMultisampling_ = 0;
        int DisplayMultimonitor_ = 0;
        bool ScreenInvalid_ = false;
        bool Initialized_ = false;
        bool Running_ = true;
        bool EscapeDisabled_ = false;
        // DarkBASIC starts in automatic refresh; SYNC ON hands presentation to the program.
        bool SyncOn_ = false;
        bool AlwaysActive_ = false;
        bool Headless_ = false;
        Uint64 SyncTargetNs_ = 0;
        Uint64 LastSyncNs_ = 0;
        float DeltaTime_ = 0.0f;
        int Fps_ = 0;
        int FramesThisSecond_ = 0;
        Uint64 FpsWindowStart_ = 0;
        Uint64 LastRenderTicks_ = 0;
        float CameraAspect_ = 640.0f / 480.0f;
        std::mt19937 Rng_{std::random_device{}()};
        bool Suspended_ = false;
        Uint32 SdlInitFlags_ = 0;
        bool MixerInitialized_ = false;
    };

} // namespace dbc
