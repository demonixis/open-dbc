#include "core_system.hpp"
#include "../engine_math.hpp"
#include "audio_system.hpp"
#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "input_system.hpp"
#include "renderer_system.hpp"
#include "text_system.hpp"
#include "vr_system.hpp"

#include "log.hpp"
#include <array>
#include <ctime>

#include <random>

#if __has_include(<SDL3_mixer/SDL_mixer.h>)
#include <SDL3_mixer/SDL_mixer.h>
#elif __has_include(<SDL_mixer.h>)
#include <SDL_mixer.h>
#endif

#if defined(__APPLE__)
#if __has_include(<SDL3_shadercross/SDL_shadercross.h>)
#include <SDL3_shadercross/SDL_shadercross.h>
#elif __has_include(<SDL_shadercross.h>)
#include <SDL_shadercross.h>
#endif
#endif

namespace dbc
{
    CoreSystem::CoreSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    CoreSystem::~CoreSystem()
    {
        Window_ = nullptr;
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    bool CoreSystem::Init()
    {
        return EnsureInitialized();
    }

    void CoreSystem::Shutdown()
    {
        Kernel_.Vr().DetachDevice();
        if (!Initialized_)
        {
            return;
        }

        if (Kernel_.Audio().Mixer())
        {
            Kernel_.Audio().Shutdown();
            MIX_DestroyMixer(Kernel_.Audio().Mixer());
            Kernel_.Audio().Mixer() = nullptr;
        }

        if (MixerInitialized_)
        {
            MIX_Quit();
            MixerInitialized_ = false;
        }

        Kernel_.Text().DestroyTextSystem();
        Kernel_.Gpu().DestroyGpuBackend();
        if (Window_)
        {
            SDL_SetWindowRelativeMouseMode(Window_, false);
            SDL_ShowCursor();
            SDL_DestroyWindow(Window_);
            Window_ = nullptr;
        }

        if (SdlInitFlags_ != 0)
        {
            SDL_QuitSubSystem(SdlInitFlags_);
            SdlInitFlags_ = 0;
        }
        Initialized_ = false;
        LastRenderTicks_ = 0;
        LastSyncNs_ = 0;
        DeltaTime_ = 0.0f;
    }

    bool CoreSystem::IsRunning()
    {
        return Running_;
    }

    bool CoreSystem::EnsureInitialized()
    {
        if (Initialized_)
        {
            return true;
        }

        // Headless runs (tests, run --headless) must never take focus or play sound:
        // the hints are applied before SDL creates the application and the audio device.
        Headless_ = SDL_GetHintBoolean(KHeadlessHint, false);
        if (Headless_)
        {
            SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        }

        Uint32 InitFlags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD;
#ifdef DBC_MOBILE
        InitFlags |= SDL_INIT_SENSOR;
#endif
        if (!SDL_InitSubSystem(InitFlags))
        {
            DBC_LOG_ERROR("SDL subsystem initialization failed: {}", SDL_GetError());
            return false;
        }
        SdlInitFlags_ = InitFlags;
        if (SDL_InitSubSystem(SDL_INIT_HAPTIC))
        {
            SdlInitFlags_ |= SDL_INIT_HAPTIC;
        }
        else
        {
            DBC_LOG_DEBUG("SDL haptic subsystem is unavailable: {}", SDL_GetError());
            SDL_ClearError();
        }
        Initialized_ = true;

#ifdef DBC_MOBILE
        // On mobile, pass 0x0 so SDL uses the device's native surface dimensions.
        Window_ = SDL_CreateWindow("OpenDBC Artifact", 0, 0, 0);
#else
        Window_ = SDL_CreateWindow("OpenDBC Runtime", WindowWidth_, WindowHeight_,
                                   Headless_ ? SDL_WINDOW_HIDDEN : 0);
#endif
        if (!Window_)
        {
            DBC_LOG_ERROR("SDL window creation failed: {}", SDL_GetError());
            Shutdown();
            return false;
        }

#ifdef DBC_MOBILE
        // On mobile the window is created at the device's native size, so the
        // requested dimensions were only a hint: adopt what we actually got,
        // otherwise the camera aspect stays stuck on the requested one.
        SDL_GetWindowSize(Window_, &WindowWidth_, &WindowHeight_);
#endif

        SDL_GPUShaderFormat ShaderFormats = SDL_GPU_SHADERFORMAT_SPIRV;
        [[maybe_unused]] const char* GpuDriver = "vulkan";
#if defined(__APPLE__)
        ShaderFormats = SDL_GPU_SHADERFORMAT_MSL;
        GpuDriver = "metal";
#endif

        auto& Gpu = Kernel_.Gpu();
        const auto SetupDevice = [&]()
        {
            if (!Gpu.GpuDevice() || !SDL_ClaimWindowForGPUDevice(Gpu.GpuDevice(), Window_))
                return false;
            if (SDL_strcasecmp(SDL_GetGPUDeviceDriver(Gpu.GpuDevice()), "metal") == 0 && !Gpu.InitializeShaderCross())
                return false;
            Gpu.Renderer() = SDL_CreateGPURenderer(Gpu.GpuDevice(), nullptr);
            return Gpu.Renderer() && Gpu.CreateGpuWhiteTexture() && Gpu.CreateGpuPipeline();
        };
        Gpu.GpuDevice() = Kernel_.Vr().CreateDevice();
        bool Ready = Gpu.GpuDevice() && SetupDevice();
        if (!Ready)
        {
            if (Gpu.GpuDevice())
            {
                DBC_LOG_WARN("XR device setup failed; retrying the flat renderer: {}", SDL_GetError());
                Kernel_.Vr().DetachDevice();
                Gpu.DestroyGpuBackend();
            }
            Gpu.GpuDevice() = SDL_CreateGPUDevice(ShaderFormats, true, GpuDriver);
            Ready = SetupDevice();
            if (!Ready)
            {
                DBC_LOG_ERROR("GPU backend initialization failed: {}", SDL_GetError());
                Gpu.DestroyGpuBackend();
            }
        }

        if (!Kernel_.Gpu().Renderer())
        {
            DBC_LOG_ERROR("No SDL_GPU renderer is available");
            Shutdown();
            return false;
        }
        if (Kernel_.Gpu().Renderer())
        {
            SDL_SetRenderDrawBlendMode(Kernel_.Gpu().Renderer(), SDL_BLENDMODE_BLEND);
        }
        CameraAspect_ = static_cast<float>(WindowWidth_) / std::max(1, WindowHeight_);
        Kernel_.Renderer().InvalidateTriCache();

        FpsWindowStart_ = SDL_GetTicks();
        LastRenderTicks_ = FpsWindowStart_;

        if (!MIX_Init())
        {
            DBC_LOG_ERROR("SDL_mixer Init failed: {}", SDL_GetError());
        }
        else
        {
            MixerInitialized_ = true;
            Kernel_.Audio().Mixer() = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
            if (!Kernel_.Audio().Mixer())
            {
                DBC_LOG_ERROR("MIX_CreateMixerDevice failed: {}", SDL_GetError());
            }
        }

        if (!TTF_Init())
        {
            DBC_LOG_ERROR("SDL_ttf Init failed: {}", SDL_GetError());
        }
        else
        {
            Kernel_.Text().TtfInitialized() = true;
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // Display / window
    // -----------------------------------------------------------------------

    bool CoreSystem::SetDisplayMode(int Width, int Height, int Depth, int Vsync, int Multisampling, int Multimonitor,
                                    std::string* Error)
    {
        if (Width <= 0 || Height <= 0)
        {
            if (Error)
                *Error = "SET DISPLAY MODE requires positive dimensions";
            return false;
        }
        if (Depth != 16 && Depth != 32)
        {
            if (Error)
                *Error = "SET DISPLAY MODE colour depth must be 16 or 32 bits";
            return false;
        }
        if (Vsync != 0 && Vsync != 1)
        {
            if (Error)
                *Error = "SET DISPLAY MODE VSync must be zero or one";
            return false;
        }
        if (Multisampling != 0)
        {
            if (Error)
                *Error = "SET DISPLAY MODE multisampling is not available";
            return false;
        }
        if (Multimonitor != 0)
        {
            if (Error)
                *Error = "SET DISPLAY MODE multimonitor mode is not available";
            return false;
        }
#ifndef DBC_MOBILE
        if (Window_)
        {
            if (!SDL_SetWindowSize(Window_, Width, Height))
            {
                if (Error)
                    *Error = std::string("SET DISPLAY MODE resize failed: ") + SDL_GetError();
                return false;
            }
        }
#endif
        if (Width != WindowWidth_ || Height != WindowHeight_)
            if (!Kernel_.ImageStoreRef().DeleteBitmap(0, Error))
                return false;
        WindowWidth_ = Width;
        WindowHeight_ = Height;
        ScreenDepth_ = Depth;
        DisplayMultisampling_ = Multisampling;
        DisplayMultimonitor_ = Multimonitor;
        CameraAspect_ = static_cast<float>(WindowWidth_) / std::max(1, WindowHeight_);
        Kernel_.Renderer().InvalidateTriCache();
        if (Kernel_.Gpu().GpuDevice() && Window_ &&
            !SDL_SetGPUSwapchainParameters(Kernel_.Gpu().GpuDevice(), Window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                           Vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE))
        {
            if (Error)
                *Error = std::string("SET DISPLAY MODE VSync failed: ") + SDL_GetError();
            return false;
        }
        DisplayVsync_ = Vsync;
        return true;
    }

    void CoreSystem::HideMouse()
    {
        if (!EnsureInitialized())
        {
            return;
        }
        SDL_HideCursor();
        SDL_SetWindowRelativeMouseMode(Window_, true);
    }

    void CoreSystem::ShowMouse()
    {
        if (!EnsureInitialized())
        {
            return;
        }
        SDL_ShowCursor();
        SDL_SetWindowRelativeMouseMode(Window_, false);
    }

    void CoreSystem::HideCursor()
    {
        if (!EnsureInitialized())
        {
            return;
        }
        SDL_HideCursor();
    }

    void CoreSystem::ShowCursor()
    {
        if (!EnsureInitialized())
        {
            return;
        }
        SDL_ShowCursor();
    }

    bool CoreSystem::HideWindow(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "HIDE WINDOW requires an active window";
            return false;
        }
        if (!SDL_HideWindow(Window_))
        {
            if (Error)
                *Error = std::string("HIDE WINDOW failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::ShowWindow(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SHOW WINDOW requires an active window";
            return false;
        }
        if (Headless_)
            return true; // A headless window stays hidden and windowed.
        if (!SDL_ShowWindow(Window_))
        {
            if (Error)
                *Error = std::string("SHOW WINDOW failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::MaximizeWindow(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "MAXIMIZE WINDOW requires an active window";
            return false;
        }
        if (Headless_)
            return true; // A headless window stays hidden and windowed.
        if (!SDL_MaximizeWindow(Window_))
        {
            if (Error)
                *Error = std::string("MAXIMIZE WINDOW failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::MinimizeWindow(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "MINIMIZE WINDOW requires an active window";
            return false;
        }
        if (!SDL_MinimizeWindow(Window_))
        {
            if (Error)
                *Error = std::string("MINIMIZE WINDOW failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::RestoreWindow(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "RESTORE WINDOW requires an active window";
            return false;
        }
        if (Headless_)
            return true; // A headless window stays hidden and windowed.
        if (!SDL_RestoreWindow(Window_))
        {
            if (Error)
                *Error = std::string("RESTORE WINDOW failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::SetWindowTitle(const std::string& Title, std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SET WINDOW TITLE requires an active window";
            return false;
        }
        if (!SDL_SetWindowTitle(Window_, Title.c_str()))
        {
            if (Error)
                *Error = std::string("SET WINDOW TITLE failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::SetWindowPosition(int X, int Y, std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SET WINDOW POSITION requires an active window";
            return false;
        }
        if (!SDL_SetWindowPosition(Window_, X, Y))
        {
            if (Error)
                *Error = std::string("SET WINDOW POSITION failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::SetWindowSize(int Width, int Height, std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SET WINDOW SIZE requires an active window";
            return false;
        }
        if (Width <= 0 || Height <= 0)
        {
            if (Error)
                *Error = "SET WINDOW SIZE requires positive dimensions";
            return false;
        }
#ifndef DBC_MOBILE
        if (!SDL_SetWindowSize(Window_, Width, Height))
        {
            if (Error)
                *Error = std::string("SET WINDOW SIZE failed: ") + SDL_GetError();
            return false;
        }
#endif
        if (Width != WindowWidth_ || Height != WindowHeight_)
            if (!Kernel_.ImageStoreRef().DeleteBitmap(0, Error))
                return false;
        WindowWidth_ = Width;
        WindowHeight_ = Height;
        CameraAspect_ = static_cast<float>(Width) / std::max(1, Height);
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    // SET WINDOW ON activates windowed (non-fullscreen) mode
    bool CoreSystem::SetWindowOn(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SET WINDOW ON requires an active window";
            return false;
        }
        if (!SDL_SetWindowFullscreen(Window_, false))
        {
            if (Error)
                *Error = std::string("SET WINDOW ON failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    // SET WINDOW OFF activates fullscreen mode
    bool CoreSystem::SetWindowOff(std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SET WINDOW OFF requires an active window";
            return false;
        }
        if (Headless_)
            return true; // A headless window stays hidden and windowed.
        if (!SDL_SetWindowFullscreen(Window_, true))
        {
            if (Error)
                *Error = std::string("SET WINDOW OFF failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::SetWindowLayout(int FullWindow, int TitleBar, int Icon, std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "SET WINDOW LAYOUT requires an active window";
            return false;
        }
        if (FullWindow < 0 || FullWindow > 4 || (TitleBar != 0 && TitleBar != 1) || (Icon != 0 && Icon != 1))
        {
            if (Error)
                *Error = "SET WINDOW LAYOUT requires historical style, caption and icon values";
            return false;
        }
        if (Icon == 0)
        {
            if (Error)
                *Error = "SET WINDOW LAYOUT cannot clear the application icon on this platform";
            return false;
        }
        if (FullWindow == 2 || FullWindow == 4)
        {
            if (Error)
                *Error = "SET WINDOW LAYOUT cannot hide individual title-bar buttons on this platform";
            return false;
        }
        const bool Bordered = FullWindow != 0 || TitleBar != 0;
        if (!SDL_SetWindowBordered(Window_, Bordered))
        {
            if (Error)
                *Error = std::string("SET WINDOW LAYOUT border failed: ") + SDL_GetError();
            return false;
        }
        if (!SDL_SetWindowResizable(Window_, FullWindow == 1))
        {
            if (Error)
                *Error = std::string("SET WINDOW LAYOUT resize policy failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    bool CoreSystem::WindowExists(const std::string& Title) const
    {
        int Count = 0;
        SDL_Window** Windows = SDL_GetWindows(&Count);
        if (!Windows)
            return false;
        bool Found = false;
        for (int Index = 0; Index < Count; ++Index)
        {
            const char* WindowTitle = SDL_GetWindowTitle(Windows[Index]);
            if (WindowTitle && Title == WindowTitle)
            {
                Found = true;
                break;
            }
        }
        SDL_free(Windows);
        return Found;
    }

    bool CoreSystem::RaiseWindow(const std::string& Title, std::string* Error)
    {
        if (!Window_)
        {
            if (Error)
                *Error = "WINDOW TO FRONT requires an active window";
            return false;
        }
        SDL_Window* Target = Window_;
        if (!Title.empty())
        {
            int Count = 0;
            SDL_Window** Windows = SDL_GetWindows(&Count);
            Target = nullptr;
            for (int Index = 0; Windows && Index < Count; ++Index)
            {
                const char* WindowTitle = SDL_GetWindowTitle(Windows[Index]);
                if (WindowTitle && Title == WindowTitle)
                {
                    Target = Windows[Index];
                    break;
                }
            }
            SDL_free(Windows);
            if (!Target)
            {
                if (Error)
                    *Error = "WINDOW TO FRONT cannot find '" + Title + "'";
                return false;
            }
        }
        if (Headless_)
            return true; // A headless run never raises a window.
        if (!SDL_RaiseWindow(Target))
        {
            if (Error)
                *Error = std::string("WINDOW TO FRONT failed: ") + SDL_GetError();
            return false;
        }
        return true;
    }

    int CoreSystem::ScreenDepth() const
    {
        return ScreenDepth_;
    }
    int CoreSystem::ScreenType() const
    {
        return Kernel_.Gpu().GpuDevice() ? 1 : 0;
    }
    int CoreSystem::ConsumeScreenInvalid()
    {
        const int Result = ScreenInvalid_ ? 1 : 0;
        ScreenInvalid_ = false;
        return Result;
    }
    void CoreSystem::MarkScreenInvalid()
    {
        ScreenInvalid_ = true;
    }
    int CoreSystem::DisplayVSync() const
    {
        return DisplayVsync_;
    }
    int CoreSystem::DisplayMultisampling() const
    {
        return DisplayMultisampling_;
    }
    int CoreSystem::DisplayMultimonitor() const
    {
        return DisplayMultimonitor_;
    }

    // -----------------------------------------------------------------------
    // VSync
    // -----------------------------------------------------------------------

    void CoreSystem::DisableVSync()
    {
#ifdef DBC_MOBILE
        return;
#endif
        auto* Device = Kernel_.Gpu().GpuDevice();
        if (Device && Window_)
        {
            SDL_SetGPUSwapchainParameters(Device, Window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                          SDL_GPU_PRESENTMODE_IMMEDIATE);
        }
    }

    // -----------------------------------------------------------------------
    // Escape key
    // -----------------------------------------------------------------------

    void CoreSystem::DisableEscapekey()
    {
        EscapeDisabled_ = true;
    }

    void CoreSystem::EnableEscapekey()
    {
        EscapeDisabled_ = false;
    }

    // -----------------------------------------------------------------------
    // Clipboard
    // -----------------------------------------------------------------------

    void CoreSystem::WriteToClipboard(const std::string& Text)
    {
        SDL_SetClipboardText(Text.c_str());
    }

    std::string CoreSystem::GetClipboard()
    {
        char* Raw = SDL_GetClipboardText();
        if (!Raw)
            return {};
        std::string Result(Raw);
        SDL_free(Raw);
        return Result;
    }

    // -----------------------------------------------------------------------
    // Timing / sync
    // -----------------------------------------------------------------------

    void CoreSystem::DoSyncOn()
    {
        SyncOn_ = true;
    }

    void CoreSystem::DoSyncOff()
    {
        SyncOn_ = false;
    }

    void CoreSystem::DoSyncRate(int Rate)
    {
        if (Rate <= 0)
        {
            SyncTargetNs_ = 0;
            return;
        }
        SyncTargetNs_ = 1'000'000'000ULL / static_cast<Uint64>(Rate);
    }

    std::optional<std::chrono::steady_clock::time_point> CoreSystem::NextSyncDeadline() const
    {
        if (Kernel_.Vr().Pacing() || SyncTargetNs_ == 0 || LastSyncNs_ == 0)
            return std::nullopt;
        const Uint64 Elapsed = SDL_GetTicksNS() - LastSyncNs_;
        if (Elapsed >= SyncTargetNs_)
            return std::nullopt;
        return std::chrono::steady_clock::now() + std::chrono::nanoseconds(SyncTargetNs_ - Elapsed);
    }

    void CoreSystem::Sync()
    {
        if (!EnsureInitialized())
            return;

        if (Suspended_ && !Kernel_.Vr().Enabled())
            return;

        // SYNC presents in both modes; SYNC OFF only adds the host's automatic refresh between calls.
        Kernel_.Renderer().RenderScene();

        const Uint64 Now = SDL_GetTicksNS();
        if (LastSyncNs_ > 0)
            DeltaTime_ = static_cast<float>(static_cast<double>(Now - LastSyncNs_) / 1'000'000'000.0);
        else
            DeltaTime_ = 0.016f;
        LastSyncNs_ = Now;
    }

    void CoreSystem::FastSync()
    {
        if (!EnsureInitialized())
            return;

        if (Suspended_ && !Kernel_.Vr().Enabled())
            return;

        Kernel_.Renderer().RenderScene();

        const Uint64 Now = SDL_GetTicksNS();
        if (LastSyncNs_ > 0)
            DeltaTime_ = static_cast<float>(static_cast<double>(Now - LastSyncNs_) / 1'000'000'000.0);
        else
            DeltaTime_ = 0.016f;
        LastSyncNs_ = Now;
    }

    int CoreSystem::Timer()
    {
        return static_cast<int>(SDL_GetTicks());
    }

    std::string CoreSystem::GetDate()
    {
        return FormatLocalTime("%m/%d/%y");
    }

    std::string CoreSystem::GetTime()
    {
        return FormatLocalTime("%H:%M:%S");
    }

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    std::string CoreSystem::FormatLocalTime(const char* Fmt)
    {
        const std::time_t Now = std::time(nullptr);
        std::tm Out{};
#if defined(_WIN32)
        localtime_s(&Out, &Now);
#else
        localtime_r(&Now, &Out);
#endif
        std::array<char, 64> Buffer{};
        if (std::strftime(Buffer.data(), Buffer.size(), Fmt, &Out) == 0)
        {
            return {};
        }
        return std::string(Buffer.data());
    }

    // --- Mutable accessors ---

    SDL_Window*& CoreSystem::Window()
    {
        return Window_;
    }
    int& CoreSystem::WindowWidth()
    {
        return WindowWidth_;
    }
    int& CoreSystem::WindowHeight()
    {
        return WindowHeight_;
    }
    bool& CoreSystem::Initialized()
    {
        return Initialized_;
    }
    bool& CoreSystem::Running()
    {
        return Running_;
    }
    bool& CoreSystem::EscapeDisabled()
    {
        return EscapeDisabled_;
    }
    bool& CoreSystem::SyncOn()
    {
        return SyncOn_;
    }
    float CoreSystem::DeltaTime() const
    {
        return DeltaTime_;
    }
    int& CoreSystem::Fps()
    {
        return Fps_;
    }
    int& CoreSystem::FramesThisSecond()
    {
        return FramesThisSecond_;
    }
    Uint64& CoreSystem::FpsWindowStart()
    {
        return FpsWindowStart_;
    }
    Uint64& CoreSystem::LastRenderTicks()
    {
        return LastRenderTicks_;
    }
    float& CoreSystem::CameraAspect()
    {
        return CameraAspect_;
    }
    std::mt19937& CoreSystem::Rng()
    {
        return Rng_;
    }
    bool CoreSystem::Suspended() const
    {
        return Suspended_;
    }
    void CoreSystem::SetSuspended(bool Suspended)
    {
        if (Suspended && !Suspended_)
            MarkScreenInvalid();
        if (Suspended != Suspended_)
        {
            std::string Error;
            if (!Kernel_.Audio().SetSuspended(Suspended, &Error))
                DBC_LOG_ERROR("Audio suspension failed: {}", Error);
        }
        Suspended_ = Suspended;
    }
    bool CoreSystem::AlwaysActive() const
    {
        return AlwaysActive_;
    }
    bool CoreSystem::Headless() const
    {
        return Headless_;
    }

    void CoreSystem::SetAlwaysActive(bool Enabled)
    {
        AlwaysActive_ = Enabled;
        if (Enabled)
            SetSuspended(false);
    }

} // namespace dbc

// =======================================================================
// Free functions that remain in dbc:: namespace (pure math + RNG)
// =======================================================================

namespace dbc
{
    void Randomize(EngineKernel& K, int Seed)
    {
        K.Core().Rng().seed(static_cast<std::mt19937::result_type>(Seed));
    }

    int Rnd(EngineKernel& K, int MaxValue)
    {
        if (MaxValue <= 0)
        {
            return 0;
        }
        std::uniform_int_distribution<int> Dist(0, MaxValue);
        return Dist(K.Core().Rng());
    }

    float Curvevalue(EngineKernel& K, float Target, float Current, float Speed)
    {
        (void)K;
        const float S = std::max(1.0f, Speed);
        return Current + (Target - Current) / S;
    }

    float Wrapvalue(EngineKernel& K, float Value)
    {
        (void)K;
        float Out = std::fmod(Value, 360.0f);
        if (Out < 0.0f)
        {
            Out += 360.0f;
        }
        return Out;
    }

    float Newxvalue(EngineKernel& K, float X, float AngleDegrees, float Step)
    {
        (void)K;
        return X + std::sin(DegToRad(AngleDegrees)) * Step;
    }

    float Newyvalue(EngineKernel& K, float Y, float AngleDegrees, float Step)
    {
        (void)K;
        return Y + std::sin(DegToRad(AngleDegrees)) * Step;
    }

    float Newzvalue(EngineKernel& K, float Z, float AngleDegrees, float Step)
    {
        (void)K;
        return Z + std::cos(DegToRad(AngleDegrees)) * Step;
    }

    float Atanfull(EngineKernel& K, float X, float Z)
    {
        (void)K;
        float A = static_cast<float>(std::atan2(X, Z) * 180.0 / M_PI);
        if (A < 0.0f)
        {
            A += 360.0f;
        }
        return A;
    }

} // namespace dbc
