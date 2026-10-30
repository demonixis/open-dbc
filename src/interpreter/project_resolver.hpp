#pragma once

#include "language/dbpro_project.hpp"
#include "language/dialect_detector.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dbc::interpreter
{
    enum class ProjectInputKind
    {
        Source,
        DbProProject,
        Folder,
    };

    enum class DialectOrigin
    {
        Forced,
        Configuration,
        Project,
        Detected,
        Interactive,
    };

    enum class ProjectWindowMode
    {
        Window,
        Fullscreen,
        Desktop,
        FullDesktop,
        Hidden,
    };

    struct ProjectStartup
    {
        std::string Title = "OpenDBC Runtime";
        ProjectWindowMode WindowMode = ProjectWindowMode::Window;
        int Width = 640;
        int Height = 480;
        int Depth = 32;
        std::string CommandLine{};
    };

    struct ProjectMetadata
    {
        std::string Name{};
        std::string Executable{};
        std::string Icon{};
        std::string VersionName{};
        std::string Company{};
        std::string Description{};
        std::string Copyright{};
    };

    struct ResolvedProject
    {
        ProjectInputKind InputKind = ProjectInputKind::Source;
        language::LanguageProfile Profile = language::LanguageProfile::Dbc;
        DialectOrigin ProfileOrigin = DialectOrigin::Detected;
        std::filesystem::path InputPath{};
        std::filesystem::path ProjectRoot{};
        std::optional<std::filesystem::path> DescriptorPath{};
        std::filesystem::path EntrySource{};
        std::vector<std::filesystem::path> OrderedSources{};
        std::filesystem::path AssetRoot{};
        ProjectStartup Startup{};
        ProjectMetadata Metadata{};
        std::vector<std::string> Warnings{};
        std::vector<std::string> RunBlockers{};
        std::vector<std::string> ExportBlockers{};
        std::optional<language::DbProProjectDocument> Descriptor{};
    };

    using DialectSelector =
        std::function<std::optional<language::LanguageProfile>(const language::DialectDetection& Detection)>;
    using ProjectSelector =
        std::function<std::optional<std::filesystem::path>(std::span<const std::filesystem::path> Candidates)>;

    struct ProjectResolveRequest
    {
        std::filesystem::path Target{};
        std::optional<language::LanguageProfile> ForcedProfile{};
        DialectSelector SelectDialect{};
        ProjectSelector SelectProject{};
        language::ProjectPathRemapper RemapPath{};
        std::optional<std::filesystem::path> AssetRootOverride{};
        bool ReadProjectConfiguration = true;
    };

    struct ProjectResolveResult
    {
        std::optional<ResolvedProject> Project{};
        language::DialectDetection Detection{};
        std::vector<std::filesystem::path> ProjectCandidates{};
        std::optional<std::filesystem::path> PendingDescriptor{};
        std::string Error{};
        // The raw project path a local remapping would fix, when that is what Error reports.
        std::string RemappablePath{};

        explicit operator bool() const
        {
            return Project.has_value() && Error.empty();
        }
    };

    ProjectResolveResult ResolveProject(const ProjectResolveRequest& Request);
    std::string_view ProjectInputKindName(ProjectInputKind Value);
    std::string_view DialectOriginName(DialectOrigin Value);
} // namespace dbc::interpreter
