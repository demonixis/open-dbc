#include "midi_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#define TML_IMPLEMENTATION
#define TML_NO_STDIO
#include <tml.h>
#include <tsf.h>

#include <midi_bank.inc>

namespace dbc
{
    namespace
    {
        constexpr int KMidiChannels = 16;
        constexpr int KDrumChannel = 9;
        constexpr int KMaxVoices = 256;
        constexpr int KBlockFrames = TSF_RENDER_EFFECTSAMPLEBLOCK;
        // The bank peaks near full scale per voice, so a mix needs the headroom FluidSynth's default gain gives it.
        constexpr float KOutputGainDb = -14.0f;
        constexpr std::size_t KMaxTailFrames = 3 * KMidiRenderRate;
        // 211 MB of PCM and some 20 s of work: a file claiming more is damaged or hostile rather than music.
        constexpr unsigned int KMaxLengthMs = 20 * 60 * 1000;

        void ApplyMessage(tsf* Synth, const tml_message& Message)
        {
            switch (Message.type)
            {
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(Synth, Message.channel, Message.program,
                                                 Message.channel == KDrumChannel);
                    break;
                case TML_NOTE_ON:
                    tsf_channel_note_on(Synth, Message.channel, Message.key, Message.velocity / 127.0f);
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(Synth, Message.channel, Message.key);
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(Synth, Message.channel, Message.pitch_bend);
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(Synth, Message.channel, Message.control, Message.control_value);
                    break;
                default:
                    break;
            }
        }
    } // namespace

    bool RenderMidiToPcm(std::vector<std::int16_t>& Output, const void* Data, std::size_t Size, std::string* Error)
    {
        // The loader divides by the header's ticks per beat without checking it.
        constexpr std::size_t KDivisionOffset = 12;
        const auto* Bytes = static_cast<const unsigned char*>(Data);
        const bool NoTimeBase =
            Size >= KDivisionOffset + 2 && Bytes[KDivisionOffset] == 0 && Bytes[KDivisionOffset + 1] == 0;
        if (NoTimeBase || Size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            if (Error)
                *Error = "the MIDI header is invalid";
            return false;
        }

        std::unique_ptr<tml_message, decltype(&tml_free)> Midi(tml_load_memory(Data, static_cast<int>(Size)),
                                                               &tml_free);
        if (!Midi)
        {
            if (Error)
                *Error = "the file is not a Standard MIDI File or holds no events";
            return false;
        }

        unsigned int LengthMs = 0;
        for (const tml_message* Message = Midi.get(); Message; Message = Message->next)
            LengthMs = std::max(LengthMs, Message->time);
        if (LengthMs > KMaxLengthMs)
        {
            if (Error)
                *Error = "the MIDI file lasts over 20 minutes";
            return false;
        }

        const std::string_view Bank = KMidiBankFiles[0].second;
        std::unique_ptr<tsf, decltype(&tsf_close)> Synth(tsf_load_memory(Bank.data(), static_cast<int>(Bank.size())),
                                                         &tsf_close);
        if (!Synth)
        {
            if (Error)
                *Error = "the built-in MIDI bank could not be read";
            return false;
        }
        tsf_set_output(Synth.get(), TSF_STEREO_INTERLEAVED, KMidiRenderRate, KOutputGainDb);
        if (!tsf_set_max_voices(Synth.get(), KMaxVoices))
        {
            if (Error)
                *Error = "out of memory for the MIDI voices";
            return false;
        }
        // General MIDI starts every channel on program 0, with the drum kit on channel 10.
        for (int Channel = 0; Channel < KMidiChannels; ++Channel)
            tsf_channel_set_presetnumber(Synth.get(), Channel, 0, Channel == KDrumChannel);

        Output.clear();
        const std::size_t LengthFrames = static_cast<std::size_t>(LengthMs) * KMidiRenderRate / 1000;
        Output.reserve((LengthFrames + KMaxTailFrames) * 2);
        const double BlockMs = KBlockFrames * 1000.0 / KMidiRenderRate;
        const tml_message* Next = Midi.get();
        double NowMs = 0.0;
        std::size_t TailFrames = 0;
        std::array<std::int16_t, KBlockFrames * 2> Block;
        while (true)
        {
            NowMs += BlockMs;
            for (; Next && NowMs >= Next->time; Next = Next->next)
                ApplyMessage(Synth.get(), *Next);
            const bool Ended = !Next && TailFrames == 0;
            // A player stops its notes at the end of the file, so release what the file left sounding.
            if (Ended)
                tsf_note_off_all(Synth.get());
            tsf_render_short(Synth.get(), Block.data(), KBlockFrames, 0);
            Output.insert(Output.end(), Block.begin(), Block.end());
            if (Next)
                continue;
            TailFrames += KBlockFrames;
            if (tsf_active_voice_count(Synth.get()) == 0 || TailFrames >= KMaxTailFrames)
                break;
        }
        // Envelopes fade on long past 16-bit silence, which would only lengthen every LOOP MUSIC pass.
        const std::size_t KeptSamples = std::max<std::size_t>(LengthFrames, KBlockFrames) * 2;
        while (Output.size() > KeptSamples && Output[Output.size() - 1] == 0 && Output[Output.size() - 2] == 0)
            Output.resize(Output.size() - 2);
        return true;
    }
} // namespace dbc
