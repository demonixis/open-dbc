#pragma once

#include "project_resolver.hpp"
#include "vm/bytecode.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::interpreter
{
    // A program image is a compiled BASIC program: its bytecode, the startup settings of its
    // project and source spans for run-time errors. It only loads into the runtime that built it.
    inline constexpr std::string_view ProgramImageExtension = ".obc";

    struct ProgramImage
    {
        std::shared_ptr<vm::Module> Bytecode{};
        ProjectStartup Startup{};
    };

    struct ProgramImageReadResult
    {
        std::optional<ProgramImage> Image{};
        std::string Error{};
    };

    // Fingerprint of the runtime sources this binary was built from (see opendbc-runtime.json).
    std::string_view RuntimeFingerprint();
    bool IsProgramImagePath(const std::filesystem::path& Path);

    // Source paths are stored relative to SourceRoot so that images do not carry build-machine paths.
    std::vector<std::uint8_t> WriteProgramImage(const vm::Module& Bytecode, const ProjectStartup& Startup,
                                                const std::filesystem::path& SourceRoot,
                                                std::string_view Fingerprint = RuntimeFingerprint());
    // Checks the container only; callers must still run vm::Verify before executing the bytecode.
    ProgramImageReadResult ReadProgramImage(std::span<const std::uint8_t> Bytes,
                                            std::string_view Fingerprint = RuntimeFingerprint());
} // namespace dbc::interpreter
