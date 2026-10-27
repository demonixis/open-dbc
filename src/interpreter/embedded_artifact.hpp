#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::interpreter
{
    // An exported game: opendbc-artifact.json in the host's resources and the payload (program.obc and
    // the assets) in resources/artifact, copied to writable per-user storage before the program runs.
    inline constexpr std::string_view EmbeddedArtifactManifestName = "opendbc-artifact.json";
    inline constexpr int EmbeddedArtifactFormat = 6;

    struct EmbeddedArtifact
    {
        std::filesystem::path ProgramPath;
        std::filesystem::path AssetRoot;
        std::filesystem::path PayloadRoot;
    };

    struct EmbeddedArtifactResult
    {
        std::optional<EmbeddedArtifact> Artifact;
        std::string Error;
    };

    // No manifest means no embedded artifact: both members are empty. WritableRoot replaces the
    // per-user storage directory (tests).
    EmbeddedArtifactResult FindEmbeddedArtifact(const std::filesystem::path& ResourceRoot,
                                                const std::filesystem::path& WritableRoot = {});

    namespace detail
    {
        struct ArtifactFile
        {
            std::filesystem::path RelativePath;
            std::string ContentHash;
        };

        std::string ArtifactContentHash(std::string_view Bytes);
        std::optional<std::string> ArtifactContentHashFile(const std::filesystem::path& Path);
        bool SynchronizeArtifactPayload(const std::filesystem::path& SourceRoot,
                                        const std::filesystem::path& DestinationRoot,
                                        const std::vector<ArtifactFile>& Files, std::string& Error);
    } // namespace detail
} // namespace dbc::interpreter
