#pragma once

#include <string>

namespace dbc
{
    // Encodes a WAV file to Ogg Vorbis at a variable bit rate; quality runs from -0.1 to 1.0, as in libvorbis.
    bool EncodeOggVorbis(const std::string& InputPath, const std::string& OutputPath, float Quality,
                         std::string* Error = nullptr);

} // namespace dbc
