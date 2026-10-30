#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::language
{
    enum class ProjectDiagnosticSeverity
    {
        Warning,
        Error,
    };

    struct ProjectDiagnostic
    {
        ProjectDiagnosticSeverity Severity = ProjectDiagnosticSeverity::Error;
        std::size_t Line = 1;
        std::string Message{};
        // The raw project path a local remapping would fix, when that is the failure.
        std::string RemappablePath{};
    };

    struct DbProProjectEntry
    {
        std::string Key{};
        std::string CanonicalKey{};
        std::string Value{};
        std::size_t Line = 1;
        std::size_t ValueBegin = 0;
        std::size_t ValueEnd = 0;
    };

    struct DbProProjectDocument
    {
        std::filesystem::path LogicalPath{};
        std::string Text{};
        std::string NewLine = "\n";
        bool HasUtf8Bom = false;
        std::vector<DbProProjectEntry> Entries{};
        std::vector<ProjectDiagnostic> Diagnostics{};

        bool Valid() const;
        std::optional<std::string> Value(std::string_view Key) const;
    };

    using ProjectPathRemapper = std::function<std::optional<std::filesystem::path>(
        const std::filesystem::path& Descriptor, std::string_view RawPath)>;

    struct DbProProjectResolution
    {
        std::filesystem::path DescriptorPath{};
        std::filesystem::path ProjectRoot{};
        std::filesystem::path EntrySource{};
        std::vector<std::filesystem::path> OrderedSources{};
        std::filesystem::path AssetRoot{};
        std::vector<ProjectDiagnostic> Diagnostics{};

        bool Valid() const;
    };

    DbProProjectDocument ParseDbProProject(std::filesystem::path LogicalPath, std::string Text);
    bool SetDbProProjectValue(DbProProjectDocument& Document, std::string_view Key, std::string_view Value);
    DbProProjectResolution ResolveDbProProject(const DbProProjectDocument& Document,
                                               const ProjectPathRemapper& Remapper = {});
    std::string CanonicalProjectKey(std::string_view Key);
    bool IsKnownDbProProjectKey(std::string_view CanonicalKey);
} // namespace dbc::language
