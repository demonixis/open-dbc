#include "snippet_library.hpp"

#include "interpreter/atomic_file.hpp"
#include "language/frontend.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>

namespace dbc::studio
{
    namespace
    {
        std::string Lower(std::string_view Text)
        {
            std::string Result(Text);
            std::transform(Result.begin(), Result.end(), Result.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Result;
        }

        std::string_view Trim(std::string_view Text)
        {
            while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())))
                Text.remove_prefix(1);
            while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())))
                Text.remove_suffix(1);
            return Text;
        }

        // The value of a `rem Key: value` header line, or false when the line is
        // not that header.
        bool HeaderValue(std::string_view Line, std::string_view Key, std::string& Value)
        {
            const std::string Prefix = "rem " + std::string(Key) + ":";
            if (Line.size() < Prefix.size() || Lower(Line.substr(0, Prefix.size())) != Prefix)
                return false;
            Value = std::string(Trim(Line.substr(Prefix.size())));
            return true;
        }

        // Lowercase letters and digits joined by underscores: "FPS Camera" -> "fps_camera".
        std::string FileStem(std::string_view Name)
        {
            std::string Result;
            for (const char Character : Name)
            {
                const unsigned char Byte = static_cast<unsigned char>(Character);
                if (Byte < 0x80 && std::isalnum(Byte))
                    Result += static_cast<char>(std::tolower(Byte));
                else if (!Result.empty() && Result.back() != '_')
                    Result += '_';
            }
            while (!Result.empty() && Result.back() == '_')
                Result.pop_back();
            return Result;
        }

        constexpr std::string_view KRegionStart = "rem #region snippets";
        constexpr std::string_view KRegionEnd = "rem #endregion";

        dbc::language::Program Parse(std::string_view Text, dbc::language::LanguageProfile Profile)
        {
            auto Source = std::make_shared<dbc::language::SourceDocument>();
            Source->LogicalPath = "snippet.dba";
            Source->Text = std::string(Text);
            Source->Profile = Profile;
            return dbc::language::LexAndParse(Source);
        }

        void SortByName(std::vector<core::Snippet>& Snippets)
        {
            std::stable_sort(Snippets.begin(), Snippets.end(), [](const core::Snippet& Left, const core::Snippet& Right)
                             { return Lower(Left.Name) < Lower(Right.Name); });
        }
    } // namespace

    namespace core
    {
        SnippetInsertion PlanSnippetInsertion(std::string_view Document, const Snippet& Value,
                                              dbc::language::LanguageProfile Profile)
        {
            SnippetInsertion Result;
            std::string Body = Value.Body;
            if (!Body.empty() && Body.back() != '\n')
                Body += '\n';
            const dbc::language::Program Parsed = Parse(Body, Profile);
            if (Parsed.HasErrors() || Parsed.Functions.empty() || !Parsed.Statements.empty() ||
                !Parsed.Includes.empty() || !Parsed.Types.empty() || !Parsed.Constants.empty())
            {
                Result.Text = std::move(Body);
                return Result;
            }

            const dbc::language::Program Existing = Parse(Document, Profile);
            for (const dbc::language::FunctionDeclaration& Function : Parsed.Functions)
            {
                const bool Declared =
                    std::any_of(Existing.Functions.begin(), Existing.Functions.end(),
                                [&](const dbc::language::FunctionDeclaration& Other)
                                { return Lower(Other.Name) == Lower(Function.Name); });
                if (Declared)
                {
                    // The parser upper-cases names; quote the snippet's own spelling.
                    std::string Name = Function.Name;
                    const std::size_t At = Lower(Body).find(Lower(Function.Name), Function.Span.Begin.Offset);
                    if (At != std::string::npos)
                        Name = Body.substr(At, Function.Name.size());
                    Result.Error = "This file already declares " + Name + "().";
                    return Result;
                }
            }

            // The first #endregion after the region start closes it.
            std::size_t Line = 0;
            bool InRegion = false;
            for (std::size_t Offset = 0; Offset <= Document.size(); ++Line)
            {
                const std::size_t End = std::min(Document.find('\n', Offset), Document.size());
                const std::string Trimmed = Lower(Trim(Document.substr(Offset, End - Offset)));
                if (Trimmed == KRegionStart)
                    InRegion = true;
                else if (InRegion && Trimmed == KRegionEnd)
                {
                    Result.Where = SnippetInsertion::Place::Line;
                    Result.Line = Line;
                    Result.Text = Body + "\n";
                    return Result;
                }
                Offset = End + 1;
            }

            Result.Where = SnippetInsertion::Place::End;
            std::string_view Before = Document;
            while (!Before.empty() && (Before.back() == '\n' || Before.back() == '\r'))
                Before.remove_suffix(1);
            if (!Document.empty())
            {
                // Close the last line, then leave one blank line before the region.
                const std::size_t Newlines = static_cast<std::size_t>(
                    std::count(Document.begin() + static_cast<std::ptrdiff_t>(Before.size()), Document.end(), '\n'));
                if (!Before.empty())
                    Result.Text.append(Newlines >= 2 ? 0 : 2 - Newlines, '\n');
            }
            Result.Text += "rem #region Snippets\n\n" + Body + "\nrem #endregion\n";
            return Result;
        }

        Snippet ParseSnippet(std::string_view FileName, std::string_view Text)
        {
            Snippet Result;
            Result.Name = std::filesystem::path(FileName).stem().string();
            std::string Normalized;
            Normalized.reserve(Text.size());
            for (const char Character : Text)
                if (Character != '\r')
                    Normalized += Character;

            std::size_t Offset = 0;
            while (Offset < Normalized.size())
            {
                const std::size_t End = Normalized.find('\n', Offset);
                const std::string_view Line =
                    std::string_view(Normalized).substr(Offset, End == std::string::npos ? End : End - Offset);
                std::string Value;
                if (HeaderValue(Line, "snippet", Value))
                {
                    if (!Value.empty())
                        Result.Name = Value;
                }
                else if (HeaderValue(Line, "description", Value))
                    Result.Description = Value;
                else if (HeaderValue(Line, "profiles", Value))
                {
                    const std::string Profiles = Lower(Value);
                    bool Dbc = false;
                    bool DbPro = false;
                    std::size_t Token = 0;
                    while (Token <= Profiles.size())
                    {
                        const std::size_t Next = std::min(Profiles.find_first_of(" ,", Token), Profiles.size());
                        const std::string_view Name = std::string_view(Profiles).substr(Token, Next - Token);
                        Dbc = Dbc || Name == "dbc";
                        DbPro = DbPro || Name == "dbpro";
                        Token = Next + 1;
                    }
                    // A header naming neither profile is ignored rather than hiding the snippet.
                    if (Dbc || DbPro)
                    {
                        Result.Dbc = Dbc;
                        Result.DbPro = DbPro;
                    }
                }
                else
                    break;
                Offset = End == std::string::npos ? Normalized.size() : End + 1;
            }
            while (Offset < Normalized.size() && Normalized[Offset] == '\n')
                ++Offset;
            Result.Body = Normalized.substr(Offset);
            return Result;
        }

        std::string FormatSnippet(const Snippet& Value)
        {
            std::string Text = "rem Snippet: " + Value.Name + "\n";
            if (!(Value.Dbc && Value.DbPro))
                Text += std::string("rem Profiles: ") + (Value.DbPro ? "dbpro" : "dbc") + "\n";
            if (!Value.Description.empty())
                Text += "rem Description: " + Value.Description + "\n";
            Text += "\n" + Value.Body;
            if (!Value.Body.empty() && Value.Body.back() != '\n')
                Text += "\n";
            return Text;
        }

        std::vector<Snippet> LoadSnippets(const std::filesystem::path& Folder)
        {
            std::vector<Snippet> Result;
            std::error_code Error;
            for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Folder, Error))
            {
                if (!Entry.is_regular_file(Error) || Lower(Entry.path().extension().string()) != ".dba")
                    continue;
                std::ifstream Input(Entry.path(), std::ios::binary);
                const std::string Text(std::istreambuf_iterator<char>(Input), {});
                if (Input.bad())
                    continue;
                Snippet Value = ParseSnippet(Entry.path().filename().string(), Text);
                Value.Path = Entry.path();
                Result.push_back(std::move(Value));
            }
            SortByName(Result);
            return Result;
        }

        std::filesystem::path SaveSnippet(const std::filesystem::path& Folder, const Snippet& Value,
                                          std::string& Error)
        {
            const std::string Stem = FileStem(Value.Name);
            if (Stem.empty())
            {
                Error = "The snippet name needs at least one letter or digit.";
                return {};
            }
            if (Trim(Value.Body).empty())
            {
                Error = "The snippet is empty.";
                return {};
            }
            const std::filesystem::path Path = Folder / (Stem + ".dba");
            std::error_code FileError;
            if (std::filesystem::exists(Path, FileError) || FileError)
            {
                Error = "A snippet file named " + Path.filename().string() + " already exists.";
                return {};
            }
            if (!dbc::interpreter::AtomicWriteText(Path, FormatSnippet(Value), &Error))
                return {};
            return Path;
        }
    } // namespace core

} // namespace dbc::studio
