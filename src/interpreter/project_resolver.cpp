#include "project_resolver.hpp"
#include "basic_path.hpp"
#include "path_utf8.hpp"

#include "language/language_profile.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace dbc::interpreter
{
    namespace
    {
        struct LanguageConfiguration
        {
            std::optional<language::LanguageProfile> Profile{};
            std::optional<std::filesystem::path> ProjectFile{};
            std::optional<std::filesystem::path> Entry{};
            std::string Error{};
        };

        std::string Lower(std::string Value)
        {
            std::transform(Value.begin(), Value.end(), Value.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Value;
        }

        std::optional<std::string> ReadText(const std::filesystem::path& Path, std::string& Error)
        {
            std::ifstream Input(Path, std::ios::binary);
            if (!Input)
            {
                Error = "Failed to read '" + dbc::PathToUtf8(Path) + "'";
                return std::nullopt;
            }
            std::ostringstream Text;
            Text << Input.rdbuf();
            if (!Input.good() && !Input.eof())
            {
                Error = "Failed while reading '" + dbc::PathToUtf8(Path) + "'";
                return std::nullopt;
            }
            return Text.str();
        }

        std::filesystem::path Absolute(const std::filesystem::path& Path, std::string& Error)
        {
            std::error_code PathError;
            std::filesystem::path Result = std::filesystem::absolute(Path, PathError).lexically_normal();
            if (PathError)
                Error = "Cannot resolve '" + dbc::PathToUtf8(Path) + "': " + PathError.message();
            return Result;
        }

        std::filesystem::path FindProjectRoot(std::filesystem::path Start)
        {
            std::error_code Error;
            if (!std::filesystem::is_directory(Start, Error))
                Start = Start.parent_path();
            for (std::filesystem::path Current = Start; !Current.empty(); Current = Current.parent_path())
            {
                if (std::filesystem::is_regular_file(Current / ".opendbc-project.json", Error))
                    return Current;
                bool HasDescriptor = false;
                for (const std::filesystem::directory_entry& Entry :
                     std::filesystem::directory_iterator(Current, Error))
                {
                    if (Entry.is_regular_file(Error) && language::IsDbProProject(Entry.path()))
                    {
                        HasDescriptor = true;
                        break;
                    }
                }
                Error.clear();
                if (HasDescriptor)
                    return Current;
                if (Current == Current.root_path() || Current.parent_path() == Current)
                    break;
            }
            return Start;
        }

        LanguageConfiguration ReadConfiguration(const std::filesystem::path& Root)
        {
            LanguageConfiguration Result;
            std::ifstream Input(Root / ".opendbc-project.json");
            if (!Input)
                return Result;
            nlohmann::json Json = nlohmann::json::parse(Input, nullptr, false);
            if (Json.is_discarded() || !Json.is_object())
            {
                Result.Error = "Invalid .opendbc-project.json";
                return Result;
            }
            const auto Language = Json.find("language");
            if (Language == Json.end())
                return Result;
            if (!Language->is_object())
            {
                Result.Error = ".opendbc-project.json language must be an object";
                return Result;
            }
            if (const auto Dialect = Language->find("dialect"); Dialect != Language->end())
            {
                if (!Dialect->is_string())
                    Result.Error = ".opendbc-project.json language.dialect must be dbc or dbpro";
                else
                {
                    Result.Profile = language::ParseProfile(Dialect->get<std::string>());
                    if (!Result.Profile)
                        Result.Error = ".opendbc-project.json language.dialect must be dbc or dbpro";
                }
            }
            const auto ReadPath = [&](const char* Name, std::optional<std::filesystem::path>& Destination)
            {
                const auto Value = Language->find(Name);
                if (Value == Language->end())
                    return;
                if (!Value->is_string() || Value->get<std::string>().empty())
                {
                    Result.Error =
                        std::string(".opendbc-project.json language.") + Name + " must be a non-empty relative path";
                    return;
                }
                const std::filesystem::path Path = NormalizeBasicPath(dbc::PathFromUtf8(Value->get<std::string>()));
                if (Path.is_absolute() || (!Path.empty() && *Path.begin() == ".."))
                {
                    Result.Error =
                        std::string(".opendbc-project.json language.") + Name + " must remain inside the project";
                    return;
                }
                std::filesystem::path Resolved;
                if (!ResolveConfinedBasicPath(Root, Path, Resolved, &Result.Error))
                    return;
                Destination = Path;
            };
            ReadPath("projectFile", Result.ProjectFile);
            ReadPath("entry", Result.Entry);
            if (Result.ProjectFile && Result.Profile && *Result.Profile != language::LanguageProfile::DbPro)
                Result.Error = "language.projectFile requires the dbpro dialect";
            return Result;
        }

        std::vector<std::filesystem::path> DirectProjectFiles(const std::filesystem::path& Root)
        {
            std::vector<std::filesystem::path> Result;
            std::error_code Error;
            for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Root, Error))
            {
                if (Entry.is_regular_file(Error) && language::IsDbProProject(Entry.path()))
                    Result.push_back(Entry.path().lexically_normal());
            }
            std::sort(Result.begin(), Result.end());
            return Result;
        }

        bool ExcludedDirectory(const std::filesystem::path& Path)
        {
            const std::string Name = Lower(dbc::PathToUtf8(Path.filename()));
            return Name == ".git" || Name == ".svn" || Name == ".hg" || Name == "dist" || Name == "exports" ||
                   Name == ".idea" || Name == ".vscode" || Name.starts_with("build");
        }

        std::vector<std::filesystem::path> FolderSources(const std::filesystem::path& Root)
        {
            std::vector<std::filesystem::path> Result;
            std::error_code Error;
            std::filesystem::recursive_directory_iterator Iterator(
                Root, std::filesystem::directory_options::skip_permission_denied, Error);
            const std::filesystem::recursive_directory_iterator End;
            while (Iterator != End)
            {
                if (Iterator->is_directory(Error) && ExcludedDirectory(Iterator->path()))
                    Iterator.disable_recursion_pending();
                else if (Iterator->is_regular_file(Error) && language::IsLanguageSource(Iterator->path()))
                    Result.push_back(Iterator->path().lexically_normal());
                Iterator.increment(Error);
                if (Error)
                    Error.clear();
            }
            std::sort(Result.begin(), Result.end());
            return Result;
        }

        std::optional<language::LanguageProfile> SelectProfile(const ProjectResolveRequest& Request,
                                                               const LanguageConfiguration& Configuration,
                                                               std::span<const std::filesystem::path> Sources,
                                                               ProjectResolveResult& Result, DialectOrigin& Origin)
        {
            if (Request.ForcedProfile)
            {
                Origin = DialectOrigin::Forced;
                return Request.ForcedProfile;
            }
            if (Configuration.Profile)
            {
                Origin = DialectOrigin::Configuration;
                return Configuration.Profile;
            }
            std::vector<language::DialectSource> Documents;
            Documents.reserve(Sources.size());
            for (const std::filesystem::path& Source : Sources)
            {
                std::string Error;
                std::optional<std::string> Text = ReadText(Source, Error);
                if (!Text)
                {
                    Result.Error = std::move(Error);
                    return std::nullopt;
                }
                Documents.push_back({Source, std::move(*Text)});
            }
            Result.Detection = language::DetectDialect(Documents);
            if (std::optional<language::LanguageProfile> Detected = Result.Detection.Profile())
            {
                Origin = DialectOrigin::Detected;
                return Detected;
            }
            if (Request.SelectDialect)
            {
                std::optional<language::LanguageProfile> Selected = Request.SelectDialect(Result.Detection);
                if (Selected)
                {
                    Origin = DialectOrigin::Interactive;
                    return Selected;
                }
            }
            Result.Error = Result.Detection.Kind == language::DialectDetectionKind::Conflict
                               ? "Source contains evidence for both DBC and DBPro; select one with --dialect"
                               : "Source dialect is ambiguous; select DBC or DBPro with --dialect";
            return std::nullopt;
        }

        bool Yes(const language::DbProProjectDocument& Document, std::string_view Key)
        {
            const std::optional<std::string> Value = Document.Value(Key);
            return Value && Lower(*Value) == "yes";
        }

        int ParsePositive(std::string_view Text, int Fallback)
        {
            int Value = 0;
            const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
            return Error == std::errc{} && End == Text.data() + Text.size() && Value > 0 ? Value : Fallback;
        }

        void ParseResolution(std::string Text, bool Fullscreen, ProjectStartup& Startup)
        {
            std::replace(Text.begin(), Text.end(), ',', 'x');
            std::vector<std::string> Parts;
            std::size_t Begin = 0;
            while (Begin <= Text.size())
            {
                const std::size_t End = Text.find_first_of("xX", Begin);
                Parts.push_back(Text.substr(Begin, End - Begin));
                if (End == std::string::npos)
                    break;
                Begin = End + 1;
            }
            if (Parts.size() >= 2)
            {
                Startup.Width = ParsePositive(Parts[0], 640);
                Startup.Height = ParsePositive(Parts[1], 480);
            }
            if (Fullscreen && Parts.size() == 3)
                Startup.Depth = Lower(Parts[2]) == "16m" ? 32 : ParsePositive(Parts[2], 32);
        }

        void ApplyProjectConfiguration(ResolvedProject& Project)
        {
            const language::DbProProjectDocument& Document = *Project.Descriptor;
            Project.Metadata.Name =
                Document.Value("project name").value_or(dbc::PathToUtf8(Project.DescriptorPath->stem()));
            Project.Metadata.Executable = Document.Value("executable").value_or(std::string{});
            Project.Metadata.Icon = Document.Value("icon1").value_or(std::string{});
            Project.Metadata.VersionName =
                Document.Value("verproductnumber").value_or(Document.Value("verfilenumber").value_or(std::string{}));
            Project.Metadata.Company = Document.Value("vercompany").value_or(std::string{});
            Project.Metadata.Description = Document.Value("verfiledesc").value_or(std::string{});
            Project.Metadata.Copyright = Document.Value("vercopyright").value_or(std::string{});
            Project.Startup.Title = Document.Value("app title").value_or(Project.Metadata.Name);
            Project.Startup.CommandLine = Document.Value("commandlinearguments").value_or(std::string{});
            const std::string Mode = Lower(Document.Value("graphics mode").value_or("window"));
            if (Mode == "fullscreen")
                Project.Startup.WindowMode = ProjectWindowMode::Fullscreen;
            else if (Mode == "desktop")
                Project.Startup.WindowMode = ProjectWindowMode::Desktop;
            else if (Mode == "fulldesktop")
                Project.Startup.WindowMode = ProjectWindowMode::FullDesktop;
            else if (Mode == "hidden")
                Project.Startup.WindowMode = ProjectWindowMode::Hidden;
            if (Project.Startup.WindowMode == ProjectWindowMode::Window)
                ParseResolution(Document.Value("window resolution").value_or("640x480"), false, Project.Startup);
            else
                ParseResolution(Document.Value("fullscreen resolution").value_or("640x480x32"), true, Project.Startup);

            const std::string BuildType = Lower(Document.Value("build type").value_or("exe"));
            if (BuildType != "exe")
                Project.ExportBlockers.push_back("DBPro build type '" + BuildType + "' is not reproducible");
            if (Yes(Document, "compression"))
                Project.ExportBlockers.push_back("DBPro media compression is not reproducible");
            if (Yes(Document, "encryption"))
                Project.ExportBlockers.push_back("DBPro media encryption is not implemented");
            if (Yes(Document, "card options window"))
                Project.RunBlockers.push_back("The DBPro graphics card chooser is not implemented");
            if (Yes(Document, "cli"))
                Project.RunBlockers.push_back("The DBPro compiler debugger mode is not implemented");
            if (Yes(Document, "removesafetycode") ||
                (Document.Value("safearrays") && Lower(*Document.Value("safearrays")) == "no"))
            {
                Project.RunBlockers.push_back("OpenDBC verification and array bounds safety cannot be disabled");
            }
            if (Yes(Document, "localtempfolder"))
                Project.ExportBlockers.push_back("DBPro project-local compiler temporary files are not used");
            if (Yes(Document, "externalisedlls"))
                Project.ExportBlockers.push_back("DBPro runtime DLL externalisation is not supported");
            for (const language::DbProProjectEntry& Entry : Document.Entries)
            {
                if ((Entry.CanonicalKey.starts_with("cursor") || Entry.CanonicalKey.starts_with("pointer")) &&
                    !Entry.Value.empty())
                    Project.RunBlockers.push_back("Custom DBPro cursors are not implemented");
            }
        }

        ProjectResolveResult ResolveDescriptor(const ProjectResolveRequest& Request,
                                               const std::filesystem::path& DescriptorPath, DialectOrigin Origin)
        {
            ProjectResolveResult Result;
            Result.PendingDescriptor = DescriptorPath;
            if (Request.ForcedProfile && *Request.ForcedProfile != language::LanguageProfile::DbPro)
            {
                Result.Error = "A .dbpro target is a DBPro project and cannot use --dialect dbc";
                return Result;
            }
            std::string Error;
            std::optional<std::string> Text = ReadText(DescriptorPath, Error);
            if (!Text)
            {
                Result.Error = std::move(Error);
                return Result;
            }
            language::DbProProjectDocument Document = language::ParseDbProProject(DescriptorPath, std::move(*Text));
            const std::optional<std::string> LegacyMediaRoot = Document.Value("media root path");
            const language::ProjectPathRemapper Remapper =
                [&Request, &LegacyMediaRoot](const std::filesystem::path& Descriptor,
                                             std::string_view RawPath) -> std::optional<std::filesystem::path>
            {
                if (Request.AssetRootOverride && LegacyMediaRoot && RawPath == *LegacyMediaRoot)
                    return Request.AssetRootOverride;
                if (Request.RemapPath)
                    return Request.RemapPath(Descriptor, RawPath);
                return std::nullopt;
            };
            language::DbProProjectResolution Resolution = language::ResolveDbProProject(Document, Remapper);
            if (!Resolution.Valid())
            {
                const auto Failure =
                    std::find_if(Resolution.Diagnostics.begin(), Resolution.Diagnostics.end(),
                                 [](const language::ProjectDiagnostic& Value)
                                 { return Value.Severity == language::ProjectDiagnosticSeverity::Error; });
                if (Failure == Resolution.Diagnostics.end())
                {
                    Result.Error = "Invalid DBPro project";
                    return Result;
                }
                Result.Error = Failure->Message;
                Result.RemappablePath = Failure->RemappablePath;
                return Result;
            }
            ResolvedProject Project;
            Project.InputKind = ProjectInputKind::DbProProject;
            Project.Profile = language::LanguageProfile::DbPro;
            Project.ProfileOrigin = Origin;
            Project.InputPath = DescriptorPath;
            Project.ProjectRoot = Resolution.ProjectRoot;
            Project.DescriptorPath = DescriptorPath;
            Project.EntrySource = Resolution.EntrySource;
            Project.OrderedSources = std::move(Resolution.OrderedSources);
            Project.AssetRoot = Resolution.AssetRoot;
            if (Request.AssetRootOverride)
                Project.AssetRoot = *Request.AssetRootOverride;
            Project.Descriptor = std::move(Document);
            for (const language::ProjectDiagnostic& Diagnostic : Resolution.Diagnostics)
            {
                if (Diagnostic.Severity == language::ProjectDiagnosticSeverity::Warning)
                    Project.Warnings.push_back(Diagnostic.Message);
            }
            ApplyProjectConfiguration(Project);
            Result.Project = std::move(Project);
            Result.PendingDescriptor.reset();
            return Result;
        }
    } // namespace

    ProjectResolveResult ResolveProject(const ProjectResolveRequest& Request)
    {
        ProjectResolveResult Result;
        if (Request.Target.empty())
        {
            Result.Error = "Missing source, project or folder path";
            return Result;
        }
        std::string Error;
        const std::filesystem::path Target = Absolute(Request.Target, Error);
        if (!Error.empty())
        {
            Result.Error = std::move(Error);
            return Result;
        }
        if (language::IsDbProProject(Target))
            return ResolveDescriptor(Request, Target, DialectOrigin::Project);
        if (language::IsLanguageSource(Target))
        {
            if (!std::filesystem::is_regular_file(Target))
            {
                Result.Error = "Source file does not exist: " + dbc::PathToUtf8(Target);
                return Result;
            }
            const std::filesystem::path Root = FindProjectRoot(Target.parent_path());
            const LanguageConfiguration Configuration =
                Request.ReadProjectConfiguration ? ReadConfiguration(Root) : LanguageConfiguration{};
            if (!Configuration.Error.empty())
            {
                Result.Error = Configuration.Error;
                return Result;
            }
            if (!Request.ForcedProfile && Configuration.ProjectFile)
                return ResolveDescriptor(Request, Root / *Configuration.ProjectFile, DialectOrigin::Configuration);
            if (!Request.ForcedProfile && !Configuration.Profile)
            {
                std::vector<std::filesystem::path> Projects = DirectProjectFiles(Root);
                if (Projects.size() == 1)
                    return ResolveDescriptor(Request, Projects.front(), DialectOrigin::Project);
                if (Projects.size() > 1)
                {
                    Result.ProjectCandidates = Projects;
                    if (Request.SelectProject)
                    {
                        if (std::optional<std::filesystem::path> Selected = Request.SelectProject(Projects))
                            return ResolveDescriptor(Request, *Selected, DialectOrigin::Interactive);
                    }
                    Result.Error = "Multiple DBPro projects exist in this folder; select one explicitly";
                    return Result;
                }
            }
            DialectOrigin Origin = DialectOrigin::Detected;
            const std::array<std::filesystem::path, 1> Sources = {Target};
            const std::optional<language::LanguageProfile> Profile =
                SelectProfile(Request, Configuration, Sources, Result, Origin);
            if (!Profile)
                return Result;
            ResolvedProject Project;
            Project.InputKind = ProjectInputKind::Source;
            Project.Profile = *Profile;
            Project.ProfileOrigin = Origin;
            Project.InputPath = Target;
            Project.ProjectRoot = Root;
            Project.EntrySource = Target;
            Project.OrderedSources = {Target};
            Project.AssetRoot = Target.parent_path();
            Result.Project = std::move(Project);
            return Result;
        }
        if (!std::filesystem::is_directory(Target))
        {
            Result.Error = "Target must be a .dba source, .dbpro project or folder: " + dbc::PathToUtf8(Target);
            return Result;
        }

        const LanguageConfiguration Configuration =
            Request.ReadProjectConfiguration ? ReadConfiguration(Target) : LanguageConfiguration{};
        if (!Configuration.Error.empty())
        {
            Result.Error = Configuration.Error;
            return Result;
        }
        std::vector<std::filesystem::path> Projects = DirectProjectFiles(Target);
        if (!Request.ForcedProfile && Configuration.ProjectFile)
            return ResolveDescriptor(Request, Target / *Configuration.ProjectFile, DialectOrigin::Configuration);
        if ((!Request.ForcedProfile || *Request.ForcedProfile == language::LanguageProfile::DbPro) &&
            !Configuration.Profile && !Projects.empty())
        {
            if (Projects.size() == 1)
                return ResolveDescriptor(Request, Projects.front(), DialectOrigin::Project);
            Result.ProjectCandidates = Projects;
            if (Request.SelectProject)
            {
                if (std::optional<std::filesystem::path> Selected = Request.SelectProject(Projects))
                    return ResolveDescriptor(Request, *Selected, DialectOrigin::Interactive);
            }
            Result.Error = "Multiple DBPro projects exist in this folder; select one explicitly";
            return Result;
        }

        std::vector<std::filesystem::path> Sources = FolderSources(Target);
        if (Sources.empty())
        {
            Result.Error = "Folder contains no .dba source";
            return Result;
        }
        DialectOrigin Origin = DialectOrigin::Detected;
        const std::optional<language::LanguageProfile> Profile =
            SelectProfile(Request, Configuration, Sources, Result, Origin);
        if (!Profile)
            return Result;
        std::filesystem::path Entry = Sources.front();
        if (Configuration.Entry)
        {
            Entry = (Target / *Configuration.Entry).lexically_normal();
            if (!std::filesystem::is_regular_file(Entry) || !language::IsLanguageSource(Entry))
            {
                Result.Error = "Configured language.entry is not a .dba source inside the project";
                return Result;
            }
        }
        ResolvedProject Project;
        Project.InputKind = ProjectInputKind::Folder;
        Project.Profile = *Profile;
        Project.ProfileOrigin = Origin;
        Project.InputPath = Target;
        Project.ProjectRoot = Target;
        Project.EntrySource = Entry;
        Project.OrderedSources = {Entry};
        Project.AssetRoot = Target;
        Result.Project = std::move(Project);
        return Result;
    }

    std::string_view ProjectInputKindName(ProjectInputKind Value)
    {
        switch (Value)
        {
            case ProjectInputKind::Source:
                return "source";
            case ProjectInputKind::DbProProject:
                return "dbpro-project";
            case ProjectInputKind::Folder:
                return "folder";
        }
        return {};
    }

    std::string_view DialectOriginName(DialectOrigin Value)
    {
        switch (Value)
        {
            case DialectOrigin::Forced:
                return "forced";
            case DialectOrigin::Configuration:
                return "configuration";
            case DialectOrigin::Project:
                return "project";
            case DialectOrigin::Detected:
                return "detected";
            case DialectOrigin::Interactive:
                return "interactive";
        }
        return {};
    }
} // namespace dbc::interpreter
