#include "data_file_system.hpp"

#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace dbc
{
    namespace
    {
        bool ValidId(int Id, std::string* Error)
        {
            if (Id >= 0 && Id <= 255)
                return true;
            if (Error)
                *Error = "Data file number must be between 0 and 255";
            return false;
        }

        std::vector<std::string> SplitOn(const std::string& Line, const std::string& Separator)
        {
            std::vector<std::string> Fields;
            std::size_t Start = 0;
            for (std::size_t Found = Line.find(Separator); Found != std::string::npos;
                 Found = Line.find(Separator, Start))
            {
                Fields.push_back(Line.substr(Start, Found - Start));
                Start = Found + Separator.size();
            }
            Fields.push_back(Line.substr(Start));
            return Fields;
        }

        bool EndsWithInsensitive(const std::string& Text, const std::string& Suffix)
        {
            return Text.size() >= Suffix.size() &&
                   std::equal(Suffix.rbegin(), Suffix.rend(), Text.rbegin(), [](char Left, char Right)
                              { return Left == std::tolower(static_cast<unsigned char>(Right)); });
        }
    } // namespace

    std::vector<std::string> DataFileSystem::SplitRow(const std::string& Line, const std::string& Separator)
    {
        if (!Separator.empty())
        {
            const std::string Quoted = "\"" + Separator + "\"";
            if (Line.find(Quoted) != std::string::npos)
            {
                std::vector<std::string> Fields = SplitOn(Line, Quoted);
                if (!Fields.front().empty())
                    Fields.front().erase(0, 1);
                if (!Fields.back().empty())
                    Fields.back().pop_back();
                return Fields;
            }
            if (Line.find(Separator) != std::string::npos)
                return SplitOn(Line, Separator);
        }
        if (Line.size() >= 2 && Line.front() == '"' && Line.back() == '"')
            return {Line.substr(1, Line.size() - 2)};
        return {Line};
    }

    bool DataFileSystem::Read(Table& Output, std::string* Error)
    {
        // The Enhancement Pack opened .xls workbooks through Excel's ODBC driver, which only Windows had.
        if (EndsWithInsensitive(Output.Path, ".xls"))
        {
            if (Error)
                *Error = "OPEN DATA FILE reads delimited text; save '" + Output.Path + "' as CSV";
            return false;
        }
        std::ifstream Input(PathFromUtf8(Output.Path), std::ios::in | std::ios::binary);
        if (!Input)
        {
            if (Error)
                *Error = "OPEN DATA FILE cannot open '" + Output.Path + "'";
            return false;
        }
        Output.Rows.clear();
        Output.Columns = 0;
        std::string Line;
        while (std::getline(Input, Line))
        {
            if (!Line.empty() && Line.back() == '\r')
                Line.pop_back();
            Output.Rows.push_back(SplitRow(Line, Output.Separator));
            Output.Columns = std::max(Output.Columns, static_cast<int>(Output.Rows.back().size()));
        }
        return true;
    }

    const DataFileSystem::Table* DataFileSystem::Find(int Id, std::string* Error) const
    {
        if (!ValidId(Id, Error))
            return nullptr;
        const auto Found = Tables_.find(Id);
        if (Found != Tables_.end())
            return &Found->second;
        if (Error)
            *Error = "Data file " + std::to_string(Id) + " does not exist";
        return nullptr;
    }

    bool DataFileSystem::Open(int Id, const std::string& Path, const std::string& Separator, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        if (Tables_.count(Id) != 0)
        {
            if (Error)
                *Error = "Data file " + std::to_string(Id) + " is already open";
            return false;
        }
        Table Opened{Path, Separator, {}, 0};
        if (!Read(Opened, Error))
            return false;
        Tables_.emplace(Id, std::move(Opened));
        return true;
    }

    bool DataFileSystem::Close(int Id, std::string* Error)
    {
        if (!Find(Id, Error))
            return false;
        Tables_.erase(Id);
        return true;
    }

    bool DataFileSystem::Update(int Id, std::string* Error)
    {
        if (!Find(Id, Error))
            return false;
        Table Reread = Tables_.at(Id);
        if (!Read(Reread, Error))
            return false;
        Tables_.at(Id) = std::move(Reread);
        return true;
    }

    bool DataFileSystem::Exists(int Id, bool& Output, std::string* Error) const
    {
        if (!ValidId(Id, Error))
            return false;
        Output = Tables_.count(Id) != 0;
        return true;
    }

    bool DataFileSystem::Cell(int Id, int Column, int Row, std::string& Output, std::string* Error) const
    {
        const Table* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output.clear();
        if (Row >= 1 && Row <= static_cast<int>(Found->Rows.size()))
        {
            const std::vector<std::string>& Fields = Found->Rows[static_cast<std::size_t>(Row - 1)];
            if (Column >= 1 && Column <= static_cast<int>(Fields.size()))
                Output = Fields[static_cast<std::size_t>(Column - 1)];
        }
        return true;
    }

    bool DataFileSystem::RowCount(int Id, int& Output, std::string* Error) const
    {
        const Table* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output = static_cast<int>(Found->Rows.size());
        return true;
    }

    bool DataFileSystem::ColumnCount(int Id, int& Output, std::string* Error) const
    {
        const Table* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output = Found->Columns;
        return true;
    }

} // namespace dbc
