#pragma once

#include <memory>
#include <string>
#include <vector>

namespace dbc
{
    class EngineKernel;

    // The platform's text-to-speech engine. Implementations exist for Apple (AVSpeechSynthesizer) and Windows
    // (SAPI); elsewhere no voice exists.
    class SpeechBackend
    {
      public:
        virtual ~SpeechBackend() = default;
        // Stable voice identifiers, in the order SET VOICE counts them from 1.
        virtual std::vector<std::string> Voices() const = 0;
        virtual void SelectVoice(int Index) = 0;
        // Queues Text after any speech in progress. Speed is -10..10 and Volume 0..100. Markup is SAPI XML or SSML.
        virtual bool Speak(const std::string& Text, bool Markup, int Speed, int Volume, std::string* Error) = 0;
    };

    // The platform backend, or null where none exists or it cannot start.
    std::unique_ptr<SpeechBackend> CreateSpeechBackend();

    // The speech commands of the Enhancement Pack.
    class SpeechSystem
    {
      public:
        explicit SpeechSystem(EngineKernel& Kernel);
        ~SpeechSystem();

        bool Available();
        bool Voices(std::vector<std::string>& Output, std::string* Error);
        // Counts from 1, as GET VOICE LIST numbers the checklist.
        bool SelectVoice(int Index, std::string* Error);
        bool SetSpeed(int Speed, std::string* Error);
        bool SetVolume(int Volume, std::string* Error);
        // -1 without a synthesiser, as the Enhancement Pack reported.
        int Speed();
        int Volume();
        bool Speak(const std::string& Text, bool Markup, std::string* Error);

      private:
        SpeechBackend* Backend();

        EngineKernel& Kernel_;
        bool BackendCreated_ = false;
        std::unique_ptr<SpeechBackend> Backend_;
        int Speed_ = 0;
        int Volume_ = 100;
    };

} // namespace dbc
