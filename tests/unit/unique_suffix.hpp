#pragma once

#include <cstdint>
#include <random>

namespace dbc::testing
{
    // Names a temporary path. CTest starts test processes side by side, and their clocks can read
    // the same value: two tests then share a directory, and the first to finish deletes it.
    inline std::uint64_t UniqueSuffix()
    {
        std::random_device Device;
        return (static_cast<std::uint64_t>(Device()) << 32) | Device();
    }
} // namespace dbc::testing
