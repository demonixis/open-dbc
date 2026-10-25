#include "reference.hpp"

#include <cctype>
#include <fstream>
#include <vector>

namespace dbc::studio
{
    namespace
    {
        std::string Trim(std::string_view S)
        {
            const auto First = S.find_first_not_of(" \t\r\n`");
            if (First == std::string_view::npos)
            {
                return std::string();
            }
            const auto Last = S.find_last_not_of(" \t\r\n`");
            return std::string(S.substr(First, Last - First + 1));
        }

        std::string ToLower(std::string_view S)
        {
            std::string Out;
            Out.reserve(S.size());
            for (const char C : S)
            {
                Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
            }
            return Out;
        }

        // Table cells read "LOAD BITMAP filename$, n": the command is the leading
        // run of upper case words, the rest is the argument list.
        //
        // Note this deliberately looks at the original casing. The Qt version
        // upper-cased the cell first, which made the key swallow the first argument
        // ("INC variable" keyed as "INC VARIABLE") so most lookups missed.
        std::string CommandPrefix(std::string_view Cell)
        {
            size_t End = 0;
            while (End < Cell.size())
            {
                const char C = Cell[End];
                const bool Allowed = (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == ' ';
                if (!Allowed)
                {
                    break;
                }
                ++End;
            }

            if (End == 0 || !(Cell[0] >= 'A' && Cell[0] <= 'Z'))
            {
                return std::string();
            }
            return Trim(Cell.substr(0, End));
        }

        std::vector<std::string> SplitCells(const std::string& Line)
        {
            std::vector<std::string> Cells;
            size_t Start = 0;
            for (;;)
            {
                const size_t Bar = Line.find('|', Start);
                if (Bar == std::string::npos)
                {
                    Cells.push_back(Line.substr(Start));
                    break;
                }
                Cells.push_back(Line.substr(Start, Bar - Start));
                Start = Bar + 1;
            }
            return Cells;
        }
    } // namespace

    namespace dba
    {
        bool Reference::Load(const std::filesystem::path& Markdown)
        {
            Entries_.clear();

            std::ifstream In(Markdown);
            if (!In)
            {
                return false;
            }

            std::string Line;
            while (std::getline(In, Line))
            {
                const size_t FirstBar = Line.find_first_not_of(" \t");
                if (FirstBar == std::string::npos || Line[FirstBar] != '|')
                {
                    continue;
                }

                // "| a | b |" splits into an empty lead cell, a, b and an empty tail.
                const std::vector<std::string> Cells = SplitCells(Line);
                if (Cells.size() < 4)
                {
                    continue;
                }

                // Tables in the reference come in several shapes:
                //   | Command | Description |
                //   | Command | Parameters | Description |
                //   | Command | Parameters | Status | Description |
                //   | Keyword | Category | Role |
                // The command is always the first column and the prose always the
                // last, so there is no need to read the headers.
                const std::string Command = Trim(Cells[1]);
                const std::string Description = Trim(Cells[Cells.size() - 2]);
                if (Command.empty() || Description.size() < 4)
                {
                    continue;
                }
                if (Command.find("---") != std::string::npos || Description.find("---") != std::string::npos)
                {
                    continue;
                }

                const std::string Key = CommandPrefix(Command);
                if (Key.size() < 2)
                {
                    continue;
                }

                // First entry wins: it is the main one, later rows are variants.
                const Entry EntryValue{Command, Description};
                Entries_.emplace(ToLower(Key), EntryValue);
            }

            return !Entries_.empty();
        }

        const Reference::Entry* Reference::Find(std::string_view Command) const
        {
            const auto It = Entries_.find(ToLower(Command));
            return (It != Entries_.end()) ? &It->second : nullptr;
        }

        const std::string* Reference::FindDescription(std::string_view Command) const
        {
            const Entry* EntryValue = Find(Command);
            return (EntryValue != nullptr) ? &EntryValue->Description : nullptr;
        }
    } // namespace dba

} // namespace dbc::studio
