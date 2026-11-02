#include "speech_system.hpp"

#include "core_system.hpp"
#include "engine_kernel.hpp"

#if defined(_WIN32)
#include <sapi.h>
#include <windows.h>
#endif

namespace dbc
{
    namespace
    {
        bool Fail(const std::string& Message, std::string* Error)
        {
            if (Error)
                *Error = Message;
            return false;
        }

#if defined(_WIN32)
        std::wstring WideText(const std::string& Text)
        {
            const int Length = MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
            std::wstring Wide(static_cast<std::size_t>(Length), L'\0');
            if (Length > 0)
                MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Wide.data(), Length);
            return Wide;
        }

        std::string NarrowText(const wchar_t* Text)
        {
            const int Length = WideCharToMultiByte(CP_UTF8, 0, Text, -1, nullptr, 0, nullptr, nullptr);
            if (Length <= 1)
                return {};
            std::string Narrow(static_cast<std::size_t>(Length - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, Text, -1, Narrow.data(), Length, nullptr, nullptr);
            return Narrow;
        }

        class SapiSpeechBackend final : public SpeechBackend
        {
          public:
            ~SapiSpeechBackend() override
            {
                for (ISpObjectToken* Token : Tokens_)
                    Token->Release();
                if (Voice_)
                    Voice_->Release();
                if (OwnCom_)
                    CoUninitialize();
            }

            bool Initialize()
            {
                const HRESULT Status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                OwnCom_ = SUCCEEDED(Status);
                // SDL may already have initialized another apartment on this thread, which SAPI accepts.
                if (FAILED(Status) && Status != RPC_E_CHANGED_MODE)
                    return false;
                if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                            reinterpret_cast<void**>(&Voice_))))
                    return false;

                ISpObjectTokenCategory* Category = nullptr;
                if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                            IID_ISpObjectTokenCategory, reinterpret_cast<void**>(&Category))))
                    return true;
                IEnumSpObjectTokens* Tokens = nullptr;
                if (SUCCEEDED(Category->SetId(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices", FALSE)) &&
                    SUCCEEDED(Category->EnumTokens(nullptr, nullptr, &Tokens)))
                {
                    ISpObjectToken* Token = nullptr;
                    while (Tokens->Next(1, &Token, nullptr) == S_OK)
                        Tokens_.push_back(Token);
                    Tokens->Release();
                }
                Category->Release();
                return true;
            }

            std::vector<std::string> Voices() const override
            {
                std::vector<std::string> Names;
                for (ISpObjectToken* Token : Tokens_)
                {
                    // An unnamed token keeps its place so SET VOICE numbers still match the list.
                    wchar_t* Id = nullptr;
                    if (FAILED(Token->GetId(&Id)))
                    {
                        Names.emplace_back();
                        continue;
                    }
                    Names.push_back(NarrowText(Id));
                    CoTaskMemFree(Id);
                }
                return Names;
            }

            void SelectVoice(int Index) override
            {
                Voice_->SetVoice(Tokens_[static_cast<std::size_t>(Index)]);
            }

            bool Speak(const std::string& Text, bool Markup, int Speed, int Volume, std::string* Error) override
            {
                Voice_->SetRate(Speed);
                Voice_->SetVolume(static_cast<USHORT>(Volume));
                const std::wstring Wide = WideText(Text);
                const DWORD Flags = SPF_ASYNC | (Markup ? SPF_IS_XML : SPF_IS_NOT_XML);
                if (FAILED(Voice_->Speak(Wide.c_str(), Flags, nullptr)))
                    return Fail("The speech synthesiser rejected the message", Error);
                return true;
            }

          private:
            bool OwnCom_ = false;
            ISpVoice* Voice_ = nullptr;
            std::vector<ISpObjectToken*> Tokens_;
        };
#endif
    } // namespace

#if defined(_WIN32)
    std::unique_ptr<SpeechBackend> CreateSpeechBackend()
    {
        auto Backend = std::make_unique<SapiSpeechBackend>();
        if (!Backend->Initialize())
            return nullptr;
        return Backend;
    }
#elif !defined(__APPLE__)
    std::unique_ptr<SpeechBackend> CreateSpeechBackend()
    {
        return nullptr;
    }
#endif

    SpeechSystem::SpeechSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    SpeechSystem::~SpeechSystem() = default;

    SpeechBackend* SpeechSystem::Backend()
    {
        // Started on first use so programs that never speak pay nothing for the synthesiser.
        if (!BackendCreated_)
        {
            BackendCreated_ = true;
            Backend_ = CreateSpeechBackend();
        }
        return Backend_.get();
    }

    bool SpeechSystem::Available()
    {
        return Backend() != nullptr;
    }

    bool SpeechSystem::Voices(std::vector<std::string>& Output, std::string* Error)
    {
        SpeechBackend* Synthesiser = Backend();
        if (!Synthesiser)
            return Fail("No speech synthesiser is available on this platform", Error);
        Output = Synthesiser->Voices();
        return true;
    }

    bool SpeechSystem::SelectVoice(int Index, std::string* Error)
    {
        SpeechBackend* Synthesiser = Backend();
        if (!Synthesiser)
            return Fail("No speech synthesiser is available on this platform", Error);
        const std::size_t Count = Synthesiser->Voices().size();
        if (Index < 1 || static_cast<std::size_t>(Index) > Count)
            return Fail("Voice number must be between 1 and " + std::to_string(Count), Error);
        Synthesiser->SelectVoice(Index - 1);
        return true;
    }

    bool SpeechSystem::SetSpeed(int Speed, std::string* Error)
    {
        if (Speed < -10 || Speed > 10)
            return Fail("Voice speed must be between -10 and 10", Error);
        Speed_ = Speed;
        return true;
    }

    bool SpeechSystem::SetVolume(int Volume, std::string* Error)
    {
        if (Volume < 0 || Volume > 100)
            return Fail("Voice volume must be between 0 and 100", Error);
        Volume_ = Volume;
        return true;
    }

    int SpeechSystem::Speed()
    {
        return Available() ? Speed_ : -1;
    }

    int SpeechSystem::Volume()
    {
        return Available() ? Volume_ : -1;
    }

    bool SpeechSystem::Speak(const std::string& Text, bool Markup, std::string* Error)
    {
        // Like the Enhancement Pack, a machine without a synthesiser stays silent; headless runs never make sound.
        SpeechBackend* Synthesiser = Backend();
        if (!Synthesiser || Text.empty())
            return true;
        if (!Kernel_.Core().EnsureInitialized())
            return Fail("The engine could not start", Error);
        if (Kernel_.Core().Headless())
            return true;
        return Synthesiser->Speak(Text, Markup, Speed_, Volume_, Error);
    }

} // namespace dbc
