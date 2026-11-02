#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace dbc
{
    // The Enhancement Pack's data files: delimited text tables read whole, addressed by 1-based column and row.
    class DataFileSystem
    {
      public:
        bool Open(int Id, const std::string& Path, const std::string& Separator, std::string* Error = nullptr);
        bool Close(int Id, std::string* Error = nullptr);
        // Reads the file again, so a program sees what another one wrote since it opened it.
        bool Update(int Id, std::string* Error = nullptr);
        bool Exists(int Id, bool& Output, std::string* Error = nullptr) const;
        // A cell outside the table reads as an empty string.
        bool Cell(int Id, int Column, int Row, std::string& Output, std::string* Error = nullptr) const;
        bool RowCount(int Id, int& Output, std::string* Error = nullptr) const;
        bool ColumnCount(int Id, int& Output, std::string* Error = nullptr) const;

        // Splits a line as the Enhancement Pack did: on "separator" between quoted fields when the line has one,
        // otherwise on the bare separator; a line with neither is one field without its surrounding quotes.
        static std::vector<std::string> SplitRow(const std::string& Line, const std::string& Separator);

      private:
        struct Table
        {
            std::string Path;
            std::string Separator;
            std::vector<std::vector<std::string>> Rows;
            int Columns = 0;
        };

        static bool Read(Table& Output, std::string* Error);
        const Table* Find(int Id, std::string* Error) const;

        std::unordered_map<int, Table> Tables_;
    };

} // namespace dbc
