#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dbc
{
    inline constexpr int KMidiRenderRate = 44100;

    // Renders a Standard MIDI File to interleaved 16-bit stereo PCM at KMidiRenderRate, with the built-in
    // General MIDI bank. Output always holds at least one block on success.
    bool RenderMidiToPcm(std::vector<std::int16_t>& Output, const void* Data, std::size_t Size, std::string* Error);
} // namespace dbc
