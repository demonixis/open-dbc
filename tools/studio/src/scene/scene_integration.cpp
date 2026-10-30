#include "scene/scene_integration.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>

#include "interpreter/basic_path.hpp"
#include "language/frontend.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"

namespace dbc::studio
{
    namespace scene
    {
        namespace lang = dbc::language;

        namespace
        {
            std::string Upper(std::string_view Text)
            {
                std::string Result(Text);
                std::transform(Result.begin(), Result.end(), Result.begin(),
                               [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
                return Result;
            }

            std::string NormalizedInclude(const std::string& Path)
            {
                std::string Result = Upper(Path);
                std::replace(Result.begin(), Result.end(), '\\', '/');
                return Result;
            }

            const lang::Statement* FindCall(const std::vector<lang::Statement>& Statements,
                                            const std::string& Function);

            const lang::Statement* FindCall(const lang::Statement& Value, const std::string& Function)
            {
                if (Value.Kind == lang::StatementKind::Expression && !Value.Expressions.empty())
                {
                    const lang::Expression& Call = *Value.Expressions.front();
                    if ((Call.Kind == lang::ExpressionKind::CallOrIndex || Call.Kind == lang::ExpressionKind::Call) &&
                        Upper(Call.Text) == Function)
                        return &Value;
                }
                if (const auto* Found = FindCall(Value.Body, Function))
                    return Found;
                if (const auto* Found = FindCall(Value.ElseBody, Function))
                    return Found;
                for (const lang::CaseClause& Case : Value.Cases)
                    if (const auto* Found = FindCall(Case.Body, Function))
                        return Found;
                return nullptr;
            }

            const lang::Statement* FindCall(const std::vector<lang::Statement>& Statements, const std::string& Function)
            {
                for (const lang::Statement& Value : Statements)
                    if (const auto* Found = FindCall(Value, Function))
                        return Found;
                return nullptr;
            }

            // Offset just past the line that holds `Offset`.
            std::size_t AfterLine(const std::string& Text, std::size_t Offset)
            {
                const std::size_t End = Text.find('\n', Offset);
                return End == std::string::npos ? Text.size() : End + 1;
            }

            // Offset of the line that holds `Offset`.
            std::size_t LineStart(const std::string& Text, std::size_t Offset)
            {
                const std::size_t End = Offset == 0 ? std::string::npos : Text.rfind('\n', Offset - 1);
                return End == std::string::npos ? 0 : End + 1;
            }

            std::string_view TrimLine(std::string_view Line)
            {
                while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
                    Line.remove_prefix(1);
                while (!Line.empty() &&
                       (Line.back() == '\r' || Line.back() == '\n' || Line.back() == ' ' || Line.back() == '\t'))
                    Line.remove_suffix(1);
                return Line;
            }

            bool IsComment(std::string_view Line)
            {
                Line = TrimLine(Line);
                if (!Line.empty() && (Line.front() == '\'' || Line.front() == '`'))
                    return true;
                // A REMSTART block is code as far as the header goes: the lines go before it.
                return Line.size() >= 3 && Upper(Line.substr(0, 3)) == "REM" &&
                       (Line.size() == 3 || Line[3] == ' ' || Line[3] == '\t');
            }

            bool IsInclude(std::string_view Line)
            {
                Line = TrimLine(Line);
                return Line.size() >= 8 && Upper(Line.substr(0, 8)) == "#INCLUDE";
            }

            // The file's layout around its top: the leading comment block, then the
            // #include lines, then the first line of code.
            struct Header
            {
                std::size_t Top = 0;      // after the leading comments and the blank line ending them
                std::size_t Includes = 0; // after the last #include of the include block
                bool HasIncludes = false;
                std::size_t Code = 0;     // the first line after the include block and its blank lines
                bool CodeIsBlank = false; // true when nothing but blank lines follows
            };

            Header ReadHeader(const std::string& Text)
            {
                Header Result;
                std::size_t Offset = 0;
                bool SawComment = false;
                while (Offset < Text.size())
                {
                    const std::size_t Next = AfterLine(Text, Offset);
                    const std::string_view Line = std::string_view(Text).substr(Offset, Next - Offset);
                    if (IsComment(Line))
                        SawComment = true;
                    else if (!TrimLine(Line).empty())
                        break;
                    Offset = Next;
                    // The first blank line after the comments closes the header comment;
                    // comments further down describe the code they precede.
                    if (SawComment && TrimLine(Line).empty())
                        break;
                }
                Result.Top = Result.Includes = Result.Code = Offset;
                while (Offset < Text.size())
                {
                    const std::size_t Next = AfterLine(Text, Offset);
                    const std::string_view Line = std::string_view(Text).substr(Offset, Next - Offset);
                    if (IsInclude(Line))
                    {
                        Result.HasIncludes = true;
                        Result.Includes = Next;
                    }
                    else if (!TrimLine(Line).empty())
                        break;
                    Offset = Next;
                }
                Result.Code = Result.HasIncludes ? Offset : Result.Top;
                Result.CodeIsBlank = Result.Code >= Text.size();
                return Result;
            }

            // Every file `Root` reaches through its #include lines, read with `Read`.
            std::set<std::filesystem::path> IncludedFiles(const std::filesystem::path& Root,
                                                          lang::LanguageProfile Profile, const SourceReader& Read)
            {
                std::set<std::filesystem::path> Included;
                std::vector<std::filesystem::path> Pending{Root.lexically_normal()};
                while (!Pending.empty())
                {
                    const std::filesystem::path Current = std::move(Pending.back());
                    Pending.pop_back();
                    std::optional<std::string> Text = Read(Current);
                    if (!Text)
                        continue;
                    auto Source = std::make_shared<lang::SourceDocument>();
                    Source->LogicalPath = Current;
                    Source->Text = std::move(*Text);
                    Source->Profile = Profile;
                    for (const lang::IncludeDeclaration& Include : lang::LexAndParse(Source).Includes)
                    {
                        const std::filesystem::path Normal = interpreter::NormalizeBasicPath(Include.Path);
                        const std::filesystem::path Path =
                            Normal.is_absolute() ? Normal : (Current.parent_path() / Normal).lexically_normal();
                        if (Included.insert(Path).second)
                            Pending.push_back(Path);
                    }
                }
                return Included;
            }
        } // namespace

        LinkResult LinkIntoMain(const std::string& MainText, lang::LanguageProfile Profile,
                                const std::vector<MainLink>& Links)
        {
            auto Source = std::make_shared<lang::SourceDocument>();
            Source->LogicalPath = "main.dba";
            Source->Text = MainText;
            Source->Profile = Profile;
            const lang::Program Program = lang::LexAndParse(Source);

            const Header Layout = ReadHeader(MainText);

            std::vector<const lang::Statement*> Found;
            for (const MainLink& Link : Links)
            {
                const std::string Function = Upper(Link.Function);
                const lang::Statement* Call = FindCall(Program.Statements, Function);
                for (const lang::FunctionDeclaration& Declaration : Program.Functions)
                    Call = Call != nullptr ? Call : FindCall(Declaration.Body, Function);
                Found.push_back(Call);
            }

            // Includes join the include block at the top; calls follow it, in link
            // order. A missing call goes right after the previous link's call, else
            // right before the next link's, so the world settings keep running before
            // their level.
            std::vector<std::string> Includes;
            std::map<std::size_t, std::vector<std::string>> Calls;
            std::optional<std::size_t> Anchor;
            bool CallsOpenTheCode = false;
            for (std::size_t Index = 0; Index < Links.size(); ++Index)
            {
                const MainLink& Link = Links[Index];
                const std::string Wanted = NormalizedInclude(Link.IncludePath);
                const bool Included =
                    Link.Imported ||
                    std::any_of(Program.Includes.begin(), Program.Includes.end(),
                                [&](const lang::IncludeDeclaration& Include)
                                { return NormalizedInclude(dbc::GenericPathToUtf8(Include.Path)) == Wanted; });
                if (!Included)
                    Includes.push_back("#include \"" + Link.IncludePath + "\"");

                if (Found[Index] != nullptr)
                {
                    Anchor = AfterLine(MainText, Found[Index]->Span.Begin.Offset);
                    continue;
                }
                if (Included)
                    continue;
                if (Anchor)
                {
                    Calls[*Anchor].push_back(Link.Function + "()");
                    continue;
                }
                const auto Next = std::find_if(Found.begin() + static_cast<std::ptrdiff_t>(Index) + 1, Found.end(),
                                               [](const lang::Statement* Call) { return Call != nullptr; });
                if (Next != Found.end())
                {
                    Calls[LineStart(MainText, (*Next)->Span.Begin.Offset)].push_back(Link.Function + "()");
                    continue;
                }
                Calls[Layout.Code].push_back(Link.Function + "()");
                CallsOpenTheCode = true;
            }

            LinkResult Result{MainText, false};
            if (Includes.empty() && Calls.empty())
                return Result;

            // Blank lines keep the include block, the calls and the code apart.
            std::map<std::size_t, std::vector<std::string>> Inserts = Calls;
            if (CallsOpenTheCode && !Layout.CodeIsBlank)
                Inserts[Layout.Code].push_back({});
            if (!Includes.empty())
            {
                std::vector<std::string>& Block = Inserts[Layout.HasIncludes ? Layout.Includes : Layout.Top];
                const bool Separate = !Block.empty() || (!Layout.HasIncludes && !Layout.CodeIsBlank);
                Block.insert(Block.begin(), Includes.begin(), Includes.end());
                if (Separate)
                    Block.insert(Block.begin() + static_cast<std::ptrdiff_t>(Includes.size()), std::string{});
            }

            const std::string Newline = MainText.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            for (auto Entry = Inserts.rbegin(); Entry != Inserts.rend(); ++Entry)
            {
                std::string Block;
                // A last line without a newline gets one before the inserted lines.
                if (Entry->first == MainText.size() && !MainText.empty() && MainText.back() != '\n')
                    Block += Newline;
                for (const std::string& Line : Entry->second)
                    Block += Line + Newline;
                Result.Text.insert(Entry->first, Block);
            }
            Result.Changed = true;
            return Result;
        }

        bool SourceIncludes(const std::filesystem::path& Root, const std::filesystem::path& Target,
                            lang::LanguageProfile Profile, const SourceReader& Read)
        {
            return IncludedFiles(Root, Profile, Read).count(Target.lexically_normal()) != 0;
        }

        LevelImports FindLevelImports(const std::filesystem::path& Main, const std::filesystem::path& LevelFolder,
                                      const std::string& LevelName, lang::LanguageProfile Profile,
                                      const SourceReader& Read)
        {
            LevelImports Result;
            const std::filesystem::path Folder = LevelFolder.lexically_normal();
            const std::string Name = Upper(SanitizeLevelName(LevelName));
            for (const std::filesystem::path& Path : IncludedFiles(Main, Profile, Read))
            {
                const std::filesystem::path Relative = Path.lexically_relative(Folder);
                if (Relative.empty() || *Relative.begin() == "..")
                    continue;
                const std::string File = Upper(dbc::GenericPathToUtf8(Relative));
                if (File == Name + "/" + Name + ".DBA")
                    Result.Level = true;
                else if (File == Name + "/" + Upper(KWorldFileName))
                    Result.World = true;
                else if (File.rfind(Name + "/", 0) != 0)
                    Result.OtherLevel = true;
            }
            return Result;
        }
    } // namespace scene

} // namespace dbc::studio
