#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace dbc::interpreter
{
    // Classic WRITE TO REGISTRY and GET REGISTRY keep their DWORD values in a JSON file of the user's storage
    // rather than the Windows registry, so the same programs work on every platform without elevated rights.
    // Like the registry, folder and key names match regardless of ASCII case.

    // Returns registry.json in SDL's preference folder for OpenDBC, or an empty path when it is unavailable.
    std::filesystem::path DefaultRegistryPath();

    // A missing file, folder or key reads as 0.
    bool ReadRegistryValue(const std::filesystem::path& File, std::string_view Folder, std::string_view Key,
                           std::int32_t& Value, std::string* Error = nullptr);
    bool WriteRegistryValue(const std::filesystem::path& File, std::string_view Folder, std::string_view Key,
                            std::int32_t Value, std::string* Error = nullptr);
} // namespace dbc::interpreter
