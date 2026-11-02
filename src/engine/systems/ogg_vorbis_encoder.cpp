#include "ogg_vorbis_encoder.hpp"

#include "path_utf8.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_stdinc.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <fstream>
#include <random>

namespace dbc
{
    namespace
    {
        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

        // libvorbis's encoder state, released in reverse order of creation.
        struct Encoder
        {
            vorbis_info Info{};
            vorbis_comment Comment{};
            vorbis_dsp_state Dsp{};
            vorbis_block Block{};
            ogg_stream_state Stream{};
            bool Analysing = false;
            bool Streaming = false;

            Encoder()
            {
                vorbis_info_init(&Info);
                vorbis_comment_init(&Comment);
            }

            ~Encoder()
            {
                if (Streaming)
                    ogg_stream_clear(&Stream);
                if (Analysing)
                {
                    vorbis_block_clear(&Block);
                    vorbis_dsp_clear(&Dsp);
                }
                vorbis_comment_clear(&Comment);
                vorbis_info_clear(&Info);
            }

            Encoder(const Encoder&) = delete;
            Encoder& operator=(const Encoder&) = delete;
        };

        void WritePages(ogg_stream_state& Stream, std::ofstream& Output, bool Flush)
        {
            ogg_page Page{};
            while (Flush ? ogg_stream_flush(&Stream, &Page) != 0 : ogg_stream_pageout(&Stream, &Page) != 0)
            {
                Output.write(reinterpret_cast<const char*>(Page.header), Page.header_len);
                Output.write(reinterpret_cast<const char*>(Page.body), Page.body_len);
            }
        }

        void Drain(Encoder& State, std::ofstream& Output)
        {
            ogg_packet Packet{};
            while (vorbis_analysis_blockout(&State.Dsp, &State.Block) == 1)
            {
                vorbis_analysis(&State.Block, nullptr);
                vorbis_bitrate_addblock(&State.Block);
                while (vorbis_bitrate_flushpacket(&State.Dsp, &Packet) != 0)
                {
                    ogg_stream_packetin(&State.Stream, &Packet);
                    WritePages(State.Stream, Output, false);
                }
            }
        }
    } // namespace

    bool EncodeOggVorbis(const std::string& InputPath, const std::string& OutputPath, float Quality, std::string* Error)
    {
        if (!(Quality >= -0.1f && Quality <= 1.0f))
            return Fail(Error, "ENCODE TO OGG VORBIS takes a quality between -0.1 and 1.0");
        SDL_AudioSpec Spec{};
        Uint8* Wave = nullptr;
        Uint32 WaveLength = 0;
        if (!SDL_LoadWAV(InputPath.c_str(), &Spec, &Wave, &WaveLength))
            return Fail(Error, "ENCODE TO OGG VORBIS cannot read the WAV file '" + InputPath + "': " + SDL_GetError());
        const SDL_AudioSpec FloatSpec{SDL_AUDIO_F32, Spec.channels, Spec.freq};
        Uint8* Samples = nullptr;
        int SamplesLength = 0;
        const bool Converted =
            SDL_ConvertAudioSamples(&Spec, Wave, static_cast<int>(WaveLength), &FloatSpec, &Samples, &SamplesLength);
        SDL_free(Wave);
        if (!Converted)
            return Fail(Error, std::string("ENCODE TO OGG VORBIS cannot convert the samples: ") + SDL_GetError());

        Encoder State;
        const int Channels = Spec.channels;
        if (vorbis_encode_init_vbr(&State.Info, Channels, Spec.freq, Quality) != 0)
        {
            SDL_free(Samples);
            return Fail(Error, "ENCODE TO OGG VORBIS cannot encode " + std::to_string(Channels) + " channels at " +
                                   std::to_string(Spec.freq) + " Hz");
        }
        vorbis_comment_add_tag(&State.Comment, "ENCODER", "OpenDBC");
        vorbis_analysis_init(&State.Dsp, &State.Info);
        vorbis_block_init(&State.Dsp, &State.Block);
        State.Analysing = true;
        std::random_device Random;
        ogg_stream_init(&State.Stream, static_cast<int>(Random() & 0x7FFFFFFF));
        State.Streaming = true;

        std::ofstream Output(PathFromUtf8(OutputPath), std::ios::binary | std::ios::trunc);
        if (!Output)
        {
            SDL_free(Samples);
            return Fail(Error, "ENCODE TO OGG VORBIS cannot write '" + OutputPath + "'");
        }
        ogg_packet Header{};
        ogg_packet CommentHeader{};
        ogg_packet CodeHeader{};
        vorbis_analysis_headerout(&State.Dsp, &State.Comment, &Header, &CommentHeader, &CodeHeader);
        ogg_stream_packetin(&State.Stream, &Header);
        ogg_stream_packetin(&State.Stream, &CommentHeader);
        ogg_stream_packetin(&State.Stream, &CodeHeader);
        // The audio starts on a page of its own.
        WritePages(State.Stream, Output, true);

        const auto* Interleaved = reinterpret_cast<const float*>(Samples);
        const long Frames = static_cast<long>(SamplesLength / static_cast<int>(sizeof(float)) / Channels);
        constexpr long KChunk = 4096;
        for (long Start = 0; Start < Frames; Start += KChunk)
        {
            const long Count = std::min(KChunk, Frames - Start);
            float** Buffer = vorbis_analysis_buffer(&State.Dsp, static_cast<int>(Count));
            for (long Frame = 0; Frame < Count; ++Frame)
            {
                for (int Channel = 0; Channel < Channels; ++Channel)
                    Buffer[Channel][Frame] = Interleaved[(Start + Frame) * Channels + Channel];
            }
            vorbis_analysis_wrote(&State.Dsp, static_cast<int>(Count));
            Drain(State, Output);
        }
        SDL_free(Samples);
        vorbis_analysis_wrote(&State.Dsp, 0);
        Drain(State, Output);
        WritePages(State.Stream, Output, true);
        if (!Output.flush())
            return Fail(Error, "ENCODE TO OGG VORBIS cannot write '" + OutputPath + "'");
        return true;
    }

} // namespace dbc
