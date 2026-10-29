#include "core_system.hpp"
#include "log.hpp"

#include "video_decoder.hpp"
#include <SDL3/SDL_hints.h>

#define NOMINMAX
#include <mfapi.h>
#include <mferror.h>
#include <mfmediaengine.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace dbc
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        // The engine owns a reference to this callback. It never points back at the decoder,
        // so an in-flight notification remains safe during Shutdown and failed opens.
        class MediaEvents final : public IMFMediaEngineNotify
        {
          public:
            std::mutex Mutex;
            std::condition_variable Changed;
            bool Ready = false;
            std::atomic<HRESULT> Error{S_OK};
            std::atomic<bool> Ended{false};

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Id, void** Object) override
            {
                if (!Object)
                    return E_POINTER;
                *Object = nullptr;
                if (Id != __uuidof(IUnknown) && Id != __uuidof(IMFMediaEngineNotify))
                    return E_NOINTERFACE;
                *Object = static_cast<IMFMediaEngineNotify*>(this);
                AddRef();
                return S_OK;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return ++References_;
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                const ULONG Remaining = --References_;
                if (!Remaining)
                    delete this;
                return Remaining;
            }

            HRESULT STDMETHODCALLTYPE EventNotify(DWORD Event, DWORD_PTR, DWORD Status) override
            {
                std::lock_guard Lock(Mutex);
                if (Event == MF_MEDIA_ENGINE_EVENT_CANPLAY)
                    Ready = true;
                else if (Event == MF_MEDIA_ENGINE_EVENT_ERROR)
                    Error.store(FAILED(static_cast<HRESULT>(Status)) ? static_cast<HRESULT>(Status) : E_FAIL);
                else if (Event == MF_MEDIA_ENGINE_EVENT_ENDED)
                    Ended.store(true);
                Changed.notify_all();
                return S_OK;
            }

          private:
            std::atomic<ULONG> References_{1};
        };
    } // namespace

    struct VideoDecoder::Impl
    {
        bool OwnCom = false;
        bool OwnMediaFoundation = false;
        ComPtr<MediaEvents> Events;
        ComPtr<IMFMediaEngine> Player;
        ComPtr<IWICBitmap> Bitmap;
        std::vector<std::uint8_t> Pixels;
        std::mutex FrameMutex;
        bool NewFrame = false;
        std::jthread Pump;
        int Width = 0;
        int Height = 0;
        bool HaveFrame = false;
        std::atomic<bool> ReportedError{false};

        ~Impl()
        {
            Pump.request_stop();
            if (Pump.joinable())
                Pump.join();
            if (Player)
                Player->Shutdown();
            Player.Reset();
            Bitmap.Reset();
            Events.Reset();
            if (OwnMediaFoundation)
                MFShutdown();
            if (OwnCom)
                CoUninitialize();
        }

        bool Check(HRESULT Status)
        {
            if (SUCCEEDED(Status))
                return true;
            Events->Error.store(Status);
            if (!ReportedError.exchange(true))
            {
                DBC_LOG_WARN("Media Foundation playback failed (HRESULT 0x{:08x})", static_cast<unsigned>(Status));
            }
            return false;
        }

        void PumpFrames(std::stop_token Stop)
        {
            const HRESULT ComStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (!Check(ComStatus))
                return;
            // The frame server must be ticked even when BASIC is busy and does not render.
            // Media Foundation otherwise waits for video consumption before signalling EOS.
            while (!Stop.stop_requested() && SUCCEEDED(Events->Error.load()))
            {
                LONGLONG Timestamp = 0;
                const HRESULT Status = Player->OnVideoStreamTick(&Timestamp);
                if (Check(Status) && Status == S_OK)
                {
                    const RECT Destination{0, 0, Width, Height};
                    const MFARGB Border{0, 0, 0, 255};
                    std::lock_guard Lock(FrameMutex);
                    if (Check(Player->TransferVideoFrame(Bitmap.Get(), nullptr, &Destination, &Border)))
                        NewFrame = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            CoUninitialize();
        }
    };

    VideoDecoder::VideoDecoder(std::unique_ptr<Impl> Value) : Impl_(std::move(Value)) {}

    VideoDecoder::~VideoDecoder() = default;

    std::unique_ptr<VideoDecoder> VideoDecoder::Open(const std::string& Path, std::string& Reason)
    {
        Reason.clear();
        if (Path.empty() || Path.find('\0') != std::string::npos || Path.size() > INT_MAX)
        {
            Reason = "the video path is empty or invalid";
            return nullptr;
        }
        const int Length =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.data(), static_cast<int>(Path.size()), nullptr, 0);
        if (!Length)
        {
            Reason = "the video path is not valid UTF-8";
            return nullptr;
        }
        std::wstring WidePath(static_cast<std::size_t>(Length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.data(), static_cast<int>(Path.size()), WidePath.data(),
                            Length);
        const DWORD Attributes = GetFileAttributesW(WidePath.c_str());
        if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            Reason = "the video path is not an existing file";
            return nullptr;
        }

        auto Value = std::make_unique<Impl>();
        const auto Failed = [&](HRESULT Status, const char* Operation)
        {
            if (SUCCEEDED(Status))
                return false;
            Reason = std::string("Media Foundation ") + Operation + " failed (HRESULT " +
                     std::to_string(static_cast<unsigned>(Status)) + ")";
            return true;
        };
        HRESULT Status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Value->OwnCom = SUCCEEDED(Status);
        // SDL may already have initialized a single-threaded apartment on this thread.
        if (Status != RPC_E_CHANGED_MODE && Failed(Status, "COM initialization"))
            return nullptr;
        Status = MFStartup(MF_VERSION);
        Value->OwnMediaFoundation = SUCCEEDED(Status);
        if (Failed(Status, "startup"))
            return nullptr;

        Value->Events.Attach(new MediaEvents());
        ComPtr<IMFAttributes> Settings;
        ComPtr<IMFMediaEngineClassFactory> Factory;
        if (Failed(MFCreateAttributes(&Settings, 2), "attributes") ||
            Failed(Settings->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, Value->Events.Get()), "callback") ||
            Failed(Settings->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM),
                   "output format") ||
            Failed(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&Factory)),
                   "factory") ||
            Failed(Factory->CreateInstance(0, Settings.Get(), &Value->Player), "player"))
            return nullptr;

        if (SDL_GetHintBoolean(KHeadlessHint, false) && Failed(Value->Player->SetMuted(TRUE), "headless audio"))
            return nullptr;

        BSTR Source = SysAllocStringLen(WidePath.data(), static_cast<UINT>(WidePath.size()));
        if (!Source)
        {
            Reason = "cannot allocate the video path";
            return nullptr;
        }
        Status = Value->Player->SetSource(Source);
        SysFreeString(Source);
        if (Failed(Status, "source"))
            return nullptr;
        {
            std::unique_lock Lock(Value->Events->Mutex);
            if (!Value->Events->Changed.wait_for(
                    Lock, std::chrono::seconds(10),
                    [&] { return Value->Events->Ready || FAILED(Value->Events->Error.load()); }))
            {
                Reason = "Media Foundation did not open the video in time";
                return nullptr;
            }
        }
        if (Failed(Value->Events->Error.load(), "decoding"))
            return nullptr;
        DWORD Width = 0;
        DWORD Height = 0;
        if (!Value->Player->HasVideo() || FAILED(Value->Player->GetNativeVideoSize(&Width, &Height)) || !Width ||
            !Height || Width > 16384 || Height > 16384 ||
            static_cast<std::uint64_t>(Width) * Height * 4 > 512 * 1024 * 1024)
        {
            Reason = "Media Foundation found no video track with supported dimensions";
            return nullptr;
        }
        Value->Width = static_cast<int>(Width);
        Value->Height = static_cast<int>(Height);
        Value->Pixels.resize(static_cast<std::size_t>(Width) * Height * 4);
        ComPtr<IWICImagingFactory> Imaging;
        if (Failed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Imaging)),
                   "bitmap factory") ||
            Failed(Imaging->CreateBitmap(Width, Height, GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnLoad,
                                         &Value->Bitmap),
                   "bitmap"))
            return nullptr;
        Value->Pump = std::jthread([State = Value.get()](std::stop_token Stop) { State->PumpFrames(Stop); });
        return std::unique_ptr<VideoDecoder>(new VideoDecoder(std::move(Value)));
    }

    int VideoDecoder::Width() const
    {
        return Impl_->Width;
    }

    int VideoDecoder::Height() const
    {
        return Impl_->Height;
    }

    bool VideoDecoder::Start()
    {
        if (FAILED(Impl_->Events->Error.load()))
            return false;
        Impl_->Events->Ended.store(false);
        Impl_->HaveFrame = false;
        return Impl_->Check(Impl_->Player->SetCurrentTime(0)) && Impl_->Check(Impl_->Player->Play());
    }

    void VideoDecoder::Pause()
    {
        Impl_->Check(Impl_->Player->Pause());
    }

    void VideoDecoder::Resume()
    {
        Impl_->Check(Impl_->Player->Play());
    }

    bool VideoDecoder::Finished() const
    {
        return !Impl_->Check(Impl_->Events->Error.load()) || Impl_->Events->Ended.load() || Impl_->Player->IsEnded();
    }

    VideoFrame VideoDecoder::CurrentFrame()
    {
        std::lock_guard Lock(Impl_->FrameMutex);
        if (Impl_->NewFrame)
        {
            if (Impl_->Check(Impl_->Bitmap->CopyPixels(nullptr, static_cast<UINT>(Impl_->Width * 4),
                                                       static_cast<UINT>(Impl_->Pixels.size()), Impl_->Pixels.data())))
                Impl_->HaveFrame = true;
            Impl_->NewFrame = false;
        }
        if (!Impl_->HaveFrame)
            return {};
        return {Impl_->Pixels.data(), Impl_->Width, Impl_->Height, Impl_->Width * 4};
    }
} // namespace dbc
