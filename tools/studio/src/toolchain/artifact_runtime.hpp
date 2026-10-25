#pragma once

#include "language/language_profile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        struct EmbeddedArtifact
        {
            std::filesystem::path EntryPath;
            std::filesystem::path AssetRoot;
            std::filesystem::path PayloadRoot;
            std::vector<std::filesystem::path> OrderedSources;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
        };

        struct EmbeddedArtifactResult
        {
            std::optional<EmbeddedArtifact> Artifact;
            std::string Error;
        };

        EmbeddedArtifactResult FindEmbeddedArtifact(const std::filesystem::path& ResourceRoot);

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
    } // namespace toolchain

} // namespace dbc::studio
