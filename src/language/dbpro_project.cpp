#include "dbpro_project.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <set>
#include <system_error>

namespace dbc::language
{
    namespace
    {
        std::string Trim(std::string_view Value)
        {
            while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.front())))
                Value.remove_prefix(1);
            while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.back())))
                Value.remove_suffix(1);
            return std::string(Value);
        }

        std::string Lower(std::string_view Value)
        {
            std::string Result(Value);
            std::transform(Result.begin(), Result.end(), Result.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Result;
        }

        bool StartsWithNumberedKey(std::string_view Key, std::string_view Prefix)
        {
            if (!Key.starts_with(Prefix) || Key.size() == Prefix.size())
                return false;
            return std::all_of(Key.begin() + static_cast<std::ptrdiff_t>(Prefix.size()), Key.end(),
                               [](unsigned char Character) { return std::isdigit(Character) != 0; });
        }

        bool IsBooleanKey(std::string_view Key)
        {
            static constexpr std::string_view Keys[] = {
                "compression",      "encryption",    "card options window", "cli",
                "removesafetycode", "safearrays",    "localtempfolder",     "externalisedlls",
                "source as mmf",    "runtimeerrors", "generatehelptxt",     "speed over stability",
            };
            return std::find(std::begin(Keys), std::end(Keys), Key) != std::end(Keys);
        }

        bool ParseBoolean(std::string_view Value)
        {
            const std::string Normalized = Lower(Trim(Value));
            return Normalized == "yes" || Normalized == "no";
        }

        bool ValidResolution(std::string_view Value, bool Fullscreen)
        {
            std::string Normalized(Value);
            std::replace(Normalized.begin(), Normalized.end(), ',', 'x');
            std::vector<std::string_view> Parts;
            std::string_view Remaining = Normalized;
            while (true)
            {
                const std::size_t Separator = Remaining.find_first_of("xX");
                Parts.push_back(Remaining.substr(0, Separator));
                if (Separator == std::string_view::npos)
                    break;
                Remaining.remove_prefix(Separator + 1);
            }
            if (Parts.size() != (Fullscreen ? 3u : 2u))
                return false;
            for (std::size_t Index = 0; Index < 2; ++Index)
            {
                int Number = 0;
                const auto [End, Error] =
                    std::from_chars(Parts[Index].data(), Parts[Index].data() + Parts[Index].size(), Number);
                if (Error != std::errc{} || End != Parts[Index].data() + Parts[Index].size() || Number <= 0)
                    return false;
            }
            if (Fullscreen)
            {
                const std::string Depth = Lower(Parts[2]);
                if (Depth != "16" && Depth != "32" && Depth != "16m")
                    return false;
            }
            return true;
        }

        bool IsWindowsAbsolute(std::string_view Value)
        {
            return Value.starts_with("\\\\") || Value.starts_with("//") ||
                   (Value.size() >= 3 && std::isalpha(static_cast<unsigned char>(Value[0])) && Value[1] == ':' &&
                    (Value[2] == '\\' || Value[2] == '/'));
        }

        std::filesystem::path PortablePath(std::string_view Value)
        {
            std::string Result(Value);
            std::replace(Result.begin(), Result.end(), '\\', '/');
            return dbc::PathFromUtf8(Result).lexically_normal();
        }

        bool IsOutside(const std::filesystem::path& Relative)
        {
            return Relative.empty() || (!Relative.empty() && *Relative.begin() == "..");
        }

        std::optional<std::filesystem::path> ResolveCaseInsensitive(const std::filesystem::path& Root,
                                                                    const std::filesystem::path& Relative,
                                                                    std::string& Error)
        {
            std::filesystem::path Current = Root;
            for (const std::filesystem::path& Segment : Relative)
            {
                if (Segment == "." || Segment.empty())
                    continue;
                if (Segment == "..")
                {
                    Current = Current.parent_path();
                    continue;
                }
                std::error_code IteratorError;
                std::vector<std::filesystem::path> Matches;
                if (std::filesystem::is_directory(Current, IteratorError))
                {
                    const std::string Wanted = Lower(dbc::PathToUtf8(Segment));
                    for (const std::filesystem::directory_entry& Entry :
                         std::filesystem::directory_iterator(Current, IteratorError))
                    {
                        if (Lower(dbc::PathToUtf8(Entry.path().filename())) == Wanted)
                            Matches.push_back(Entry.path().filename());
                    }
                }
                if (Matches.size() > 1)
                {
                    Error = "Path is ambiguous because multiple entries differ only by case: " +
                            dbc::PathToUtf8((Current / Segment));
                    return std::nullopt;
                }
                Current /= Matches.empty() ? Segment : Matches.front();
            }
            return Current.lexically_normal();
        }

        std::optional<std::filesystem::path> ResolveProjectPath(const DbProProjectDocument& Document,
                                                                std::string_view RawPath,
                                                                const ProjectPathRemapper& Remapper,
                                                                ProjectDiagnostic& Failure)
        {
            const std::filesystem::path Root = Document.LogicalPath.parent_path();
            const std::filesystem::path Portable = PortablePath(Trim(RawPath));
            const bool NeedsConsent = IsWindowsAbsolute(RawPath) || Portable.is_absolute() ||
                                      (!Portable.empty() && *Portable.begin() == "..");
            if (NeedsConsent)
            {
                if (!Remapper)
                {
                    Failure.Message = "Project path requires an explicit local remapping: " + std::string(RawPath);
                    Failure.RemappablePath = RawPath;
                    return std::nullopt;
                }
                std::optional<std::filesystem::path> Mapped = Remapper(Document.LogicalPath, RawPath);
                if (!Mapped)
                {
                    Failure.Message = "No local remapping was granted for project path: " + std::string(RawPath);
                    Failure.RemappablePath = RawPath;
                    return std::nullopt;
                }
                return Mapped->lexically_normal();
            }

            std::optional<std::filesystem::path> Resolved = ResolveCaseInsensitive(Root, Portable, Failure.Message);
            if (!Resolved)
                return std::nullopt;
            std::error_code CanonicalError;
            const std::filesystem::path CanonicalRoot = std::filesystem::weakly_canonical(Root, CanonicalError);
            const std::filesystem::path CanonicalCandidate =
                std::filesystem::weakly_canonical(*Resolved, CanonicalError);
            if (!CanonicalError && IsOutside(CanonicalCandidate.lexically_relative(CanonicalRoot)))
            {
                if (!Remapper)
                {
                    Failure.Message = "Project path crosses a symlink outside the project and requires consent: " +
                                      std::string(RawPath);
                    Failure.RemappablePath = RawPath;
                    return std::nullopt;
                }
                std::optional<std::filesystem::path> Mapped = Remapper(Document.LogicalPath, RawPath);
                if (!Mapped)
                {
                    Failure.Message = "No local remapping was granted for project path: " + std::string(RawPath);
                    Failure.RemappablePath = RawPath;
                    return std::nullopt;
                }
                return Mapped->lexically_normal();
            }
            return Resolved;
        }

        void AddDiagnostic(std::vector<ProjectDiagnostic>& Diagnostics, ProjectDiagnosticSeverity Severity,
                           std::size_t Line, std::string Message)
        {
            Diagnostics.push_back({Severity, Line, std::move(Message)});
        }

        std::vector<std::pair<int, const DbProProjectEntry*>> NumberedEntries(const DbProProjectDocument& Document,
                                                                              std::string_view Prefix)
        {
            std::vector<std::pair<int, const DbProProjectEntry*>> Result;
            for (const DbProProjectEntry& Entry : Document.Entries)
            {
                if (!StartsWithNumberedKey(Entry.CanonicalKey, Prefix))
                    continue;
                int Index = 0;
                const std::string_view Digits(Entry.CanonicalKey.data() + Prefix.size(),
                                              Entry.CanonicalKey.size() - Prefix.size());
                std::from_chars(Digits.data(), Digits.data() + Digits.size(), Index);
                Result.emplace_back(Index, &Entry);
            }
            std::sort(Result.begin(), Result.end(),
                      [](const auto& Left, const auto& Right) { return Left.first < Right.first; });
            return Result;
        }
    } // namespace

    std::string CanonicalProjectKey(std::string_view Key)
    {
        std::string Result;
        bool Space = false;
        for (const unsigned char Character : Key)
        {
            if (std::isspace(Character))
            {
                Space = !Result.empty();
                continue;
            }
            if (Space)
                Result.push_back(' ');
            Space = false;
            Result.push_back(static_cast<char>(std::tolower(Character)));
        }
        while (!Result.empty() && Result.back() == ' ')
            Result.pop_back();
        return Result;
    }

    bool IsKnownDbProProjectKey(std::string_view Key)
    {
        static constexpr std::string_view Fixed[] = {
            "version",
            "project name",
            "main",
            "linemain",
            "final source",
            "executable",
            "build type",
            "compression",
            "encryption",
            "card options window",
            "cli",
            "commandlinearguments",
            "app title",
            "graphics mode",
            "fullscreen resolution",
            "window resolution",
            "media root path",
            "vercomments",
            "vercompany",
            "verfiledesc",
            "verfilenumber",
            "verinternal",
            "vercopyright",
            "vertrademark",
            "verfilename",
            "verproduct",
            "verproductnumber",
            "removesafetycode",
            "safearrays",
            "localtempfolder",
            "externalisedlls",
            "source as mmf",
            "runtimeerrors",
            "generatehelptxt",
            "speed over stability",
        };
        if (std::find(std::begin(Fixed), std::end(Fixed), Key) != std::end(Fixed))
            return true;
        for (const std::string_view Prefix :
             {"include", "lineinclude", "media", "icon", "cursorarrow", "cursorwait", "pointer", "comments", "todo"})
        {
            if (StartsWithNumberedKey(Key, Prefix))
                return true;
        }
        return false;
    }

    bool DbProProjectDocument::Valid() const
    {
        return std::none_of(Diagnostics.begin(), Diagnostics.end(), [](const ProjectDiagnostic& Value)
                            { return Value.Severity == ProjectDiagnosticSeverity::Error; });
    }

    std::optional<std::string> DbProProjectDocument::Value(std::string_view Key) const
    {
        const std::string Canonical = CanonicalProjectKey(Key);
        const auto Found = std::find_if(Entries.begin(), Entries.end(), [&](const DbProProjectEntry& Entry)
                                        { return Entry.CanonicalKey == Canonical; });
        if (Found == Entries.end())
            return std::nullopt;
        return Found->Value;
    }

    bool DbProProjectResolution::Valid() const
    {
        return std::none_of(Diagnostics.begin(), Diagnostics.end(), [](const ProjectDiagnostic& Value)
                            { return Value.Severity == ProjectDiagnosticSeverity::Error; });
    }

    DbProProjectDocument ParseDbProProject(std::filesystem::path LogicalPath, std::string Text)
    {
        DbProProjectDocument Result;
        Result.LogicalPath = std::move(LogicalPath);
        Result.Text = std::move(Text);
        Result.HasUtf8Bom = Result.Text.starts_with("\xef\xbb\xbf");
        Result.NewLine = Result.Text.find("\r\n") != std::string::npos ? "\r\n" : "\n";

        std::set<std::string> Seen;
        std::size_t Offset = Result.HasUtf8Bom ? 3 : 0;
        std::size_t Line = 1;
        while (Offset <= Result.Text.size())
        {
            const std::size_t End = Result.Text.find('\n', Offset);
            const std::size_t RawEnd = End == std::string::npos ? Result.Text.size() : End;
            std::size_t ContentEnd = RawEnd;
            if (ContentEnd > Offset && Result.Text[ContentEnd - 1] == '\r')
                --ContentEnd;
            std::string_view Content(Result.Text.data() + Offset, ContentEnd - Offset);
            std::string_view Trimmed = Content;
            while (!Trimmed.empty() && std::isspace(static_cast<unsigned char>(Trimmed.front())))
                Trimmed.remove_prefix(1);
            if (!Trimmed.empty() && Trimmed.front() != ';')
            {
                const std::size_t Equals = Content.find('=');
                if (Equals == std::string_view::npos)
                {
                    AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Line,
                                  "Expected key=value project entry");
                }
                else
                {
                    const std::string Key = Trim(Content.substr(0, Equals));
                    const std::string Canonical = CanonicalProjectKey(Key);
                    std::size_t BeginInLine = Equals + 1;
                    while (BeginInLine < Content.size() &&
                           std::isspace(static_cast<unsigned char>(Content[BeginInLine])))
                        ++BeginInLine;
                    std::size_t EndInLine = Content.size();
                    while (EndInLine > BeginInLine && std::isspace(static_cast<unsigned char>(Content[EndInLine - 1])))
                        --EndInLine;
                    const std::string Value(Content.substr(BeginInLine, EndInLine - BeginInLine));
                    if (Canonical.empty())
                    {
                        AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Line,
                                      "Project entry key cannot be empty");
                    }
                    else if (!Seen.insert(Canonical).second)
                    {
                        AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Line,
                                      "Duplicate project key '" + Key + "'");
                    }
                    Result.Entries.push_back({Key, Canonical, Value, Line, Offset + BeginInLine, Offset + EndInLine});
                    if (!IsKnownDbProProjectKey(Canonical))
                    {
                        AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Warning, Line,
                                      "Unknown DBPro project key '" + Key + "' is preserved");
                    }
                    if (IsBooleanKey(Canonical) && !ParseBoolean(Value))
                    {
                        AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Line,
                                      "Project boolean must be YES or NO");
                    }
                }
            }
            if (End == std::string::npos)
                break;
            Offset = End + 1;
            ++Line;
        }

        const std::optional<std::string> Version = Result.Value("version");
        if (!Version)
            AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1, "Missing project version");
        else if (Lower(Trim(*Version)) != "dbp1.00")
            AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1,
                          "Unsupported project version '" + *Version + "'");
        const std::optional<std::string> Main = Result.Value("main");
        if (!Main || Trim(*Main).empty())
            AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1, "Missing project main source");
        else if (Lower(dbc::PathToUtf8(PortablePath(*Main).extension())) != ".dba")
            AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1,
                          "Project main source must use the .dba extension");

        if (const std::optional<std::string> Mode = Result.Value("graphics mode"))
        {
            static constexpr std::string_view Modes[] = {"window", "fullscreen", "desktop", "fulldesktop", "hidden"};
            const std::string Normalized = Lower(Trim(*Mode));
            if (std::find(std::begin(Modes), std::end(Modes), Normalized) == std::end(Modes))
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1,
                              "Unknown graphics mode '" + *Mode + "'");
        }
        if (const std::optional<std::string> Resolution = Result.Value("window resolution"))
        {
            if (!ValidResolution(*Resolution, false))
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1,
                              "Window resolution must be WxH or W,H");
        }
        if (const std::optional<std::string> Resolution = Result.Value("fullscreen resolution"))
        {
            if (!ValidResolution(*Resolution, true))
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1,
                              "Fullscreen resolution must be WxHx16, WxHx32 or WxHx16M");
        }
        return Result;
    }

    bool SetDbProProjectValue(DbProProjectDocument& Document, std::string_view Key, std::string_view Value)
    {
        const std::string Canonical = CanonicalProjectKey(Key);
        const auto Found =
            std::find_if(Document.Entries.begin(), Document.Entries.end(),
                         [&](const DbProProjectEntry& Entry) { return Entry.CanonicalKey == Canonical; });
        std::string Updated = Document.Text;
        if (Found == Document.Entries.end())
        {
            if (!Updated.empty() && !Updated.ends_with('\n'))
                Updated += Document.NewLine;
            Updated += std::string(Key) + "=" + std::string(Value) + Document.NewLine;
        }
        else
        {
            Updated.replace(Found->ValueBegin, Found->ValueEnd - Found->ValueBegin, Value);
        }
        Document = ParseDbProProject(Document.LogicalPath, std::move(Updated));
        return Document.Valid();
    }

    DbProProjectResolution ResolveDbProProject(const DbProProjectDocument& Document,
                                               const ProjectPathRemapper& Remapper)
    {
        DbProProjectResolution Result;
        Result.DescriptorPath = Document.LogicalPath;
        Result.ProjectRoot = Document.LogicalPath.parent_path();
        Result.AssetRoot = Result.ProjectRoot;
        Result.Diagnostics = Document.Diagnostics;
        if (!Document.Valid())
            return Result;

        const auto ResolveSource = [&](std::string_view Raw, std::size_t Line) -> std::optional<std::filesystem::path>
        {
            ProjectDiagnostic Failure;
            Failure.Line = Line;
            std::optional<std::filesystem::path> Path = ResolveProjectPath(Document, Raw, Remapper, Failure);
            if (!Path)
            {
                Result.Diagnostics.push_back(std::move(Failure));
                return std::nullopt;
            }
            if (Lower(dbc::PathToUtf8(Path->extension())) != ".dba")
            {
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Line,
                              "Project source must use the .dba extension: " + dbc::PathToUtf8(*Path));
                return std::nullopt;
            }
            if (!std::filesystem::is_regular_file(*Path))
            {
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Line,
                              "Project source does not exist: " + dbc::PathToUtf8(*Path));
                return std::nullopt;
            }
            return Path;
        };

        const std::optional<std::string> Main = Document.Value("main");
        if (Main)
        {
            if (std::optional<std::filesystem::path> Path = ResolveSource(*Main, 1))
            {
                Result.EntrySource = *Path;
                Result.OrderedSources.push_back(*Path);
            }
        }
        const std::vector<std::pair<int, const DbProProjectEntry*>> Includes = NumberedEntries(Document, "include");
        int Expected = 1;
        std::set<std::filesystem::path> Sources(Result.OrderedSources.begin(), Result.OrderedSources.end());
        for (const auto& [Index, Entry] : Includes)
        {
            if (Index != Expected)
            {
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Entry->Line,
                              "Project include indices must be contiguous from include1");
                Expected = Index;
            }
            ++Expected;
            if (std::optional<std::filesystem::path> Path = ResolveSource(Entry->Value, Entry->Line))
            {
                if (!Sources.insert(*Path).second)
                    AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, Entry->Line,
                                  "Duplicate project source: " + dbc::PathToUtf8(*Path));
                else
                    Result.OrderedSources.push_back(*Path);
            }
        }

        if (const std::optional<std::string> MediaRoot = Document.Value("media root path");
            MediaRoot && !Trim(*MediaRoot).empty())
        {
            ProjectDiagnostic Failure;
            std::optional<std::filesystem::path> Path = ResolveProjectPath(Document, *MediaRoot, Remapper, Failure);
            if (!Path)
                Result.Diagnostics.push_back(std::move(Failure));
            else if (!std::filesystem::is_directory(*Path))
                AddDiagnostic(Result.Diagnostics, ProjectDiagnosticSeverity::Error, 1,
                              "Media root is not a directory: " + dbc::PathToUtf8(*Path));
            else
                Result.AssetRoot = *Path;
        }
        return Result;
    }
} // namespace dbc::language
