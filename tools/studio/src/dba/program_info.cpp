#include "program_info.hpp"
#include "path_utf8.hpp"

#include "language/language_catalogue.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string Trim(const std::string& Value)
        {
            const auto First = Value.find_first_not_of(" \t\r\n");
            if (First == std::string::npos)
                return {};
            const auto Last = Value.find_last_not_of(" \t\r\n");
            return Value.substr(First, Last - First + 1);
        }

        std::string Upper(std::string Value)
        {
            std::transform(Value.begin(), Value.end(), Value.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
            return Value;
        }

        bool IsComment(const std::string& UpperLine, const std::string& Trimmed)
        {
            return UpperLine == "REM" || UpperLine.rfind("REM ", 0) == 0 ||
                   (!Trimmed.empty() && Trimmed.front() == '`');
        }

        bool HeaderField(const std::string& Trimmed, std::string_view Field, std::string& Output)
        {
            const std::string UpperLine = Upper(Trimmed);
            if (UpperLine.rfind("REM", 0) != 0)
                return false;
            const std::size_t At = UpperLine.find(Upper(std::string(Field)));
            const std::size_t Colon = At == std::string::npos ? std::string::npos : Trimmed.find(':', At);
            if (Colon == std::string::npos)
                return false;
            const std::string Value = Trim(Trimmed.substr(Colon + 1));
            if (Value.empty())
                return false;
            Output = Value;
            return true;
        }

        bool IsAssignment(const std::string& Line)
        {
            bool InString = false;
            for (std::size_t Index = 0; Index < Line.size(); ++Index)
            {
                if (Line[Index] == '"')
                    InString = !InString;
                if (InString || Line[Index] != '=')
                    continue;
                const bool Comparison =
                    (Index > 0 && (Line[Index - 1] == '!' || Line[Index - 1] == '<' || Line[Index - 1] == '>')) ||
                    (Index + 1 < Line.size() && Line[Index + 1] == '=');
                return !Comparison;
            }
            return false;
        }

        bool StartsWithStatement(dbc::language::LanguageProfile Profile, const std::string& UpperLine)
        {
            for (const dbc::language::SymbolDescriptor* Descriptor :
                 dbc::language::Symbols(Profile, dbc::language::SymbolKind::Statement))
            {
                const std::string_view Name = Descriptor->CanonicalName;
                if (UpperLine.compare(0, Name.size(), Name) == 0 &&
                    (UpperLine.size() == Name.size() ||
                     std::isspace(static_cast<unsigned char>(UpperLine[Name.size()])) != 0))
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    namespace dba
    {
        std::vector<ProgramInfo> ScanPrograms(const fs::path& Root, dbc::language::LanguageProfile Profile)
        {
            std::vector<ProgramInfo> Results;
            if (Root.empty())
                return Results;

            std::error_code Error;
            for (fs::recursive_directory_iterator It(Root, fs::directory_options::skip_permission_denied, Error), End;
                 It != End; It.increment(Error))
            {
                if (Error)
                    break;
                const fs::path& Path = It->path();
                const std::string Name = dbc::PathToUtf8(Path.filename());
                if (!Name.empty() && Name.front() == '.')
                {
                    if (It->is_directory(Error))
                        It.disable_recursion_pending();
                    continue;
                }
                if (!It->is_regular_file(Error))
                    continue;
                if (!dbc::language::IsLanguageSource(Path))
                    continue;

                ProgramInfo Info;
                Info.Path = Path;
                Info.Profile = Profile;
                const fs::path Relative = fs::relative(Path, Root, Error);
                Info.RelativeName = Error ? Name : dbc::GenericPathToUtf8(Relative);
                Info.Size = It->file_size(Error);

                std::ifstream Input(Path);
                std::string Line;
                int Scanned = 0;
                while (Scanned < 20 && std::getline(Input, Line))
                {
                    const std::string Trimmed = Trim(Line);
                    if (Trimmed.empty())
                        continue;
                    ++Scanned;
                    if (!IsComment(Upper(Trimmed), Trimmed))
                        break;
                    HeaderField(Trimmed, "Title", Info.Title);
                    HeaderField(Trimmed, "Author", Info.Author);
                    HeaderField(Trimmed, "Date", Info.Date);
                }
                Results.push_back(std::move(Info));
            }

            std::sort(Results.begin(), Results.end(), [](const ProgramInfo& Left, const ProgramInfo& Right)
                      { return Left.RelativeName < Right.RelativeName; });
            return Results;
        }

        void AnalyzeProgram(ProgramInfo& Info)
        {
            if (Info.Analyzed)
                return;
            Info.Analyzed = true;
            std::ifstream Input(Info.Path);
            if (!Input)
                return;

            std::unordered_set<std::string> Seen;
            std::string Line;
            int Number = 0;
            bool InType = false;
            while (std::getline(Input, Line))
            {
                ++Number;
                Info.LineCount = Number;
                const std::string Trimmed = Trim(Line);
                if (Trimmed.empty())
                    continue;
                const std::string UpperLine = Upper(Trimmed);
                if (IsComment(UpperLine, Trimmed))
                {
                    ++Info.CommentLines;
                    continue;
                }
                ++Info.CodeLines;

                if (UpperLine.rfind("ENDTYPE", 0) == 0)
                {
                    InType = false;
                    continue;
                }
                if (InType)
                    continue;
                if (UpperLine.rfind("TYPE ", 0) == 0)
                {
                    InType = true;
                    continue;
                }
                if (Trimmed.front() == '#' || Trimmed.back() == ':' || IsAssignment(Trimmed) ||
                    StartsWithStatement(Info.Profile, UpperLine))
                {
                    continue;
                }

                const dbc::language::SymbolDescriptor* Descriptor =
                    dbc::language::FindLongestCommandPrefix(Info.Profile, UpperLine);
                if (!Descriptor)
                {
                    if (Info.Unrecognised.size() < 200)
                        Info.Unrecognised.push_back({Number, Trimmed});
                    continue;
                }
                const std::string Name(Descriptor->CanonicalName);
                if (Seen.insert(Name).second)
                {
                    Info.Commands.push_back(Name);
                    const dbc::language::SupportStatus Support = dbc::language::SupportFor(*Descriptor, Info.Profile);
                    if (Support == dbc::language::SupportStatus::Unsupported)
                    {
                        Info.UnsupportedUsed.push_back(Name);
                    }
                    else if (Support == dbc::language::SupportStatus::CompatibilityStub)
                    {
                        Info.CompatibilityStubsUsed.push_back(
                            {Name, dbc::language::StubPolicyFor(*Descriptor, Info.Profile) ==
                                       dbc::language::CompatibilityStubPolicy::Permanent});
                    }
                }
            }
            std::sort(Info.Commands.begin(), Info.Commands.end());
            std::sort(Info.UnsupportedUsed.begin(), Info.UnsupportedUsed.end());
            std::sort(Info.CompatibilityStubsUsed.begin(), Info.CompatibilityStubsUsed.end(),
                      [](const ProgramInfo::CompatibilityStubUse& Left, const ProgramInfo::CompatibilityStubUse& Right)
                      { return Left.Name < Right.Name; });
        }
    } // namespace dba

} // namespace dbc::studio
