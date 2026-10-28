#include "scene/scene_integration.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

#include "language/frontend.hpp"

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

            bool IsLeadingComment(std::string_view Line)
            {
                while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
                    Line.remove_prefix(1);
                while (!Line.empty() && (Line.back() == '\r' || Line.back() == '\n'))
                    Line.remove_suffix(1);
                if (Line.empty() || Line.front() == '\'' || Line.front() == '`')
                    return true;
                // A REMSTART block ends the leading comments: the lines go before it.
                return Line.size() >= 3 && Upper(Line.substr(0, 3)) == "REM" &&
                       (Line.size() == 3 || Line[3] == ' ' || Line[3] == '\t');
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

            std::size_t Top = 0;
            while (Top < MainText.size())
            {
                const std::size_t Next = AfterLine(MainText, Top);
                if (!IsLeadingComment(std::string_view(MainText).substr(Top, Next - Top)))
                    break;
                Top = Next;
            }

            // Lines to insert at each offset, in link order. A missing call goes right
            // after the previous link's call, so SetupGame() keeps running first.
            std::map<std::size_t, std::vector<std::string>> Inserts;
            std::optional<std::size_t> Anchor;
            for (const MainLink& Link : Links)
            {
                const std::string Wanted = NormalizedInclude(Link.IncludePath);
                const bool Included =
                    std::any_of(Program.Includes.begin(), Program.Includes.end(),
                                [&](const lang::IncludeDeclaration& Include)
                                { return NormalizedInclude(Include.Path.generic_string()) == Wanted; });
                if (!Included)
                    Inserts[Top].push_back("#include \"" + Link.IncludePath + "\"");

                const std::string Function = Upper(Link.Function);
                const lang::Statement* Call = FindCall(Program.Statements, Function);
                for (const lang::FunctionDeclaration& Declaration : Program.Functions)
                    Call = Call != nullptr ? Call : FindCall(Declaration.Body, Function);
                if (Call != nullptr)
                {
                    Anchor = AfterLine(MainText, Call->Span.Begin.Offset);
                    continue;
                }
                Inserts[Anchor.value_or(Top)].push_back(Link.Function + "()");
            }

            LinkResult Result{MainText, false};
            if (Inserts.empty())
                return Result;

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
    } // namespace scene

} // namespace dbc::studio
