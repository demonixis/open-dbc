#include "speech_system.hpp"

#import <AVFoundation/AVFoundation.h>

#include <string>

namespace dbc
{
    namespace
    {
        // SAPI markup has no Apple equivalent, so its tags are dropped and the text is spoken plainly.
        NSString* PlainText(const std::string& Markup)
        {
            std::string Text;
            bool InTag = false;
            for (char Character : Markup)
            {
                if (Character == '<')
                    InTag = true;
                else if (Character == '>')
                    InTag = false;
                else if (!InTag)
                    Text.push_back(Character);
            }
            NSMutableString* Result = [NSMutableString stringWithUTF8String:Text.c_str()];
            const char* const Entities[][2] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
            for (const auto& Entity : Entities)
                [Result replaceOccurrencesOfString:@(Entity[0])
                                        withString:@(Entity[1])
                                           options:0
                                             range:NSMakeRange(0, Result.length)];
            return Result;
        }

        bool IsSsml(const std::string& Markup)
        {
            const std::size_t Start = Markup.find_first_not_of(" \t\r\n");
            return Start != std::string::npos && Markup.compare(Start, 6, "<speak") == 0;
        }

        class AppleSpeechBackend final : public SpeechBackend
        {
          public:
            AppleSpeechBackend()
            {
                Synthesizer_ = [[AVSpeechSynthesizer alloc] init];
                NSSortDescriptor* ByIdentifier = [NSSortDescriptor sortDescriptorWithKey:@"identifier" ascending:YES];
                Voices_ = [[AVSpeechSynthesisVoice speechVoices] sortedArrayUsingDescriptors:@[ ByIdentifier ]];
            }

            std::vector<std::string> Voices() const override
            {
                std::vector<std::string> Names;
                for (AVSpeechSynthesisVoice* Voice in Voices_)
                    Names.emplace_back(Voice.identifier.UTF8String);
                return Names;
            }

            void SelectVoice(int Index) override
            {
                Voice_ = Voices_[static_cast<NSUInteger>(Index)];
            }

            bool Speak(const std::string& Text, bool Markup, int Speed, int Volume, std::string* Error) override
            {
                AVSpeechUtterance* Utterance = nil;
                if (Markup && IsSsml(Text))
                    Utterance = [[AVSpeechUtterance alloc] initWithSSMLRepresentation:@(Text.c_str())];
                if (!Utterance)
                {
                    NSString* Spoken = Markup ? PlainText(Text) : [NSString stringWithUTF8String:Text.c_str()];
                    if (!Spoken)
                    {
                        if (Error)
                            *Error = "The voice message is not valid UTF-8";
                        return false;
                    }
                    Utterance = [AVSpeechUtterance speechUtteranceWithString:Spoken];
                }
                // SAPI's -10..10 rate spans a third to three times normal speed; Apple's rate spans minimum to
                // maximum around its default, so each side maps linearly.
                const float Default = AVSpeechUtteranceDefaultSpeechRate;
                const float Span = Speed >= 0 ? AVSpeechUtteranceMaximumSpeechRate - Default
                                              : Default - AVSpeechUtteranceMinimumSpeechRate;
                Utterance.rate = Default + Span * static_cast<float>(Speed) / 10.0f;
                Utterance.volume = static_cast<float>(Volume) / 100.0f;
                if (Voice_)
                    Utterance.voice = Voice_;
                [Synthesizer_ speakUtterance:Utterance];
                return true;
            }

          private:
            AVSpeechSynthesizer* Synthesizer_ = nil;
            NSArray<AVSpeechSynthesisVoice*>* Voices_ = nil;
            AVSpeechSynthesisVoice* Voice_ = nil;
        };
    } // namespace

    std::unique_ptr<SpeechBackend> CreateSpeechBackend()
    {
        return std::make_unique<AppleSpeechBackend>();
    }

} // namespace dbc
