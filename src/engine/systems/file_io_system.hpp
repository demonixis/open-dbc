#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbc
{
    class EngineKernel;

    class FileIOSystem
    {
      public:
        explicit FileIOSystem(EngineKernel& Kernel);
        ~FileIOSystem();

        // File open/close
        bool OpenToRead(int Num, const std::string& Path, std::string* Error = nullptr);
        bool OpenToWrite(int Num, const std::string& Path, std::string* Error = nullptr);
        bool OpenToAppend(int Num, const std::string& Path, std::string* Error = nullptr);
        bool CloseFile(int Num, std::string* Error = nullptr);

        // Write primitives
        bool FileWriteByte(int Num, int Val, std::string* Error = nullptr);
        bool FileWriteWord(int Num, int Val, std::string* Error = nullptr);
        bool FileWriteLong(int Num, int Val, std::string* Error = nullptr);
        bool FileWriteFloat(int Num, float Val, std::string* Error = nullptr);
        bool FileWriteString(int Num, const std::string& S, std::string* Error = nullptr);
        bool FileWriteBlock(int Num, const std::filesystem::path& Path, std::string* Error = nullptr);

        // Read primitives
        bool FileReadByte(int& Output, int Num, std::string* Error = nullptr);
        bool FileReadWord(int& Output, int Num, std::string* Error = nullptr);
        bool FileReadLong(int& Output, int Num, std::string* Error = nullptr);
        bool FileReadFloat(float& Output, int Num, std::string* Error = nullptr);
        bool FileReadString(std::string& Output, int Num, std::string* Error = nullptr);
        bool FileReadBlock(int Num, const std::filesystem::path& Path, std::string* Error = nullptr);

        bool FileReadBytes(std::vector<uint8_t>& Output, int Num, std::size_t Size, std::string* Error = nullptr);
        bool FileWriteBytes(int Num, std::span<const uint8_t> Bytes, std::string* Error = nullptr);
        bool SkipBytes(int Num, int Count, std::string* Error = nullptr);
        bool FileSize(std::size_t& Output, int Num, std::string* Error = nullptr);

        // Queries
        bool FileEnd(int& Output, int Num, std::string* Error = nullptr) const;
        bool FileIsOpen(int& Output, int Num, std::string* Error = nullptr) const;

        // Directory iteration (FIND FIRST / FIND NEXT)
        bool FindFirst(const std::string& Path, std::string* Error = nullptr);
        bool FindNext(std::string* Error = nullptr);
        std::string GetFindFileName() const;
        int GetFindFileType() const;
        std::string GetFindFileDate() const;

      private:
        enum class FileMode
        {
            Read,
            Write
        };

        struct FileHandle
        {
            std::fstream Stream;
            FileMode Mode = FileMode::Read;
            bool AtEnd = false;
        };

        static bool ValidateFileNumber(int Num, std::string* Error = nullptr);
        FileHandle* OpenFile(int Num, FileMode Mode, std::string* Error = nullptr);
        const FileHandle* OpenFile(int Num, std::string* Error = nullptr) const;
        bool ReadExact(FileHandle& File, void* Destination, std::size_t Size, bool AllowEnd,
                       std::string* Error = nullptr);
        bool WriteExact(FileHandle& File, const void* Source, std::size_t Size, std::string* Error = nullptr);
        bool UpdateFindEntry(std::string* Error = nullptr);

        std::unordered_map<int, std::unique_ptr<FileHandle>> OpenFiles_;

        // Directory iteration state
        std::vector<std::filesystem::directory_entry> FindEntries_{};
        std::size_t FindIndex_ = 0;
        std::string FindFileName_;
        int FindFileType_ = 0;
        std::string FindFileDate_;
    };

} // namespace dbc
