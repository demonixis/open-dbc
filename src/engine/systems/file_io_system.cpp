#include "file_io_system.hpp"
#include "engine_kernel.hpp"
#include "log.hpp"
#include "path_utf8.hpp"
#include <cmath>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace dbc
{
    FileIOSystem::FileIOSystem(EngineKernel& /*kernel*/) {}

    FileIOSystem::~FileIOSystem()
    {
        for (const auto& [Number, File] : OpenFiles_)
        {
            if (File->Mode == FileMode::Write)
            {
                File->Stream.flush();
                if (!File->Stream)
                    DBC_LOG_ERROR("Cannot flush file {} during shutdown", Number);
            }
        }
        OpenFiles_.clear();
    }

    // --- File open/close ---

    bool FileIOSystem::ValidateFileNumber(int Num, std::string* Error)
    {
        if (Num < 1 || Num > 64)
        {
            if (Error)
                *Error = "File number must be between 1 and 64";
            return false;
        }
        return true;
    }

    FileIOSystem::FileHandle* FileIOSystem::OpenFile(int Num, FileMode Mode, std::string* Error)
    {
        if (!ValidateFileNumber(Num, Error))
            return nullptr;
        const auto It = OpenFiles_.find(Num);
        if (It == OpenFiles_.end())
        {
            if (Error)
                *Error = "File " + std::to_string(Num) + " is not open";
            return nullptr;
        }
        if (It->second->Mode != Mode)
        {
            if (Error)
                *Error = "File " + std::to_string(Num) +
                         (Mode == FileMode::Read ? " is not open for reading" : " is not open for writing");
            return nullptr;
        }
        return It->second.get();
    }

    const FileIOSystem::FileHandle* FileIOSystem::OpenFile(int Num, std::string* Error) const
    {
        if (!ValidateFileNumber(Num, Error))
            return nullptr;
        const auto It = OpenFiles_.find(Num);
        if (It == OpenFiles_.end())
        {
            if (Error)
                *Error = "File " + std::to_string(Num) + " is not open";
            return nullptr;
        }
        return It->second.get();
    }

    bool FileIOSystem::ReadExact(FileHandle& File, void* Destination, std::size_t Size, bool AllowEnd,
                                 std::string* Error)
    {
        if (Size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        {
            if (Error)
                *Error = "File read size exceeds the stream range";
            return false;
        }
        File.Stream.read(static_cast<char*>(Destination), static_cast<std::streamsize>(Size));
        if (File.Stream.bad())
        {
            if (Error)
                *Error = "File read failed";
            return false;
        }
        const std::size_t Read = static_cast<std::size_t>(File.Stream.gcount());
        File.AtEnd = Read == 0;
        if (Read == 0)
        {
            std::memset(Destination, 0, Size);
            if (AllowEnd)
                return true;
            if (Error)
                *Error = "File read reached the end before any data was available";
            return false;
        }
        if (Read != Size)
        {
            if (Error)
                *Error = "File read ended before the requested value was complete";
            return false;
        }
        return true;
    }

    bool FileIOSystem::WriteExact(FileHandle& File, const void* Source, std::size_t Size, std::string* Error)
    {
        if (Size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        {
            if (Error)
                *Error = "File write size exceeds the stream range";
            return false;
        }
        File.Stream.write(static_cast<const char*>(Source), static_cast<std::streamsize>(Size));
        if (!File.Stream)
        {
            if (Error)
                *Error = "File write failed";
            return false;
        }
        return true;
    }

    bool FileIOSystem::OpenToRead(int Num, const std::string& Path, std::string* Error)
    {
        if (!ValidateFileNumber(Num, Error))
            return false;
        if (OpenFiles_.contains(Num))
        {
            if (Error)
                *Error = "File " + std::to_string(Num) + " is already open";
            return false;
        }
        auto Fh = std::make_unique<FileHandle>();
        Fh->Mode = FileMode::Read;
        Fh->Stream.open(dbc::PathFromUtf8(Path), std::ios::in | std::ios::binary);
        if (!Fh->Stream.is_open())
        {
            if (Error)
                *Error = "Cannot open file for reading: '" + Path + "'";
            return false;
        }
        OpenFiles_[Num] = std::move(Fh);
        return true;
    }

    bool FileIOSystem::OpenToWrite(int Num, const std::string& Path, std::string* Error)
    {
        if (!ValidateFileNumber(Num, Error))
            return false;
        if (OpenFiles_.contains(Num))
        {
            if (Error)
                *Error = "File " + std::to_string(Num) + " is already open";
            return false;
        }
        std::error_code StatusError;
        if (std::filesystem::exists(dbc::PathFromUtf8(Path), StatusError))
        {
            if (Error)
                *Error = "OPEN TO WRITE refuses to overwrite: '" + Path + "'";
            return false;
        }
        if (StatusError)
        {
            if (Error)
                *Error = "OPEN TO WRITE cannot inspect: '" + Path + "'";
            return false;
        }
        auto Fh = std::make_unique<FileHandle>();
        Fh->Mode = FileMode::Write;
        Fh->Stream.open(dbc::PathFromUtf8(Path), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!Fh->Stream.is_open())
        {
            if (Error)
                *Error = "Cannot open file for writing: '" + Path + "'";
            return false;
        }
        OpenFiles_[Num] = std::move(Fh);
        return true;
    }

    bool FileIOSystem::OpenToAppend(int Num, const std::string& Path, std::string* Error)
    {
        if (!ValidateFileNumber(Num, Error))
            return false;
        if (OpenFiles_.contains(Num))
        {
            if (Error)
                *Error = "File " + std::to_string(Num) + " is already open";
            return false;
        }
        auto Fh = std::make_unique<FileHandle>();
        Fh->Mode = FileMode::Write;
        Fh->Stream.open(dbc::PathFromUtf8(Path), std::ios::out | std::ios::binary | std::ios::app);
        if (!Fh->Stream.is_open())
        {
            if (Error)
                *Error = "Cannot open file for appending: '" + Path + "'";
            return false;
        }
        OpenFiles_[Num] = std::move(Fh);
        return true;
    }

    bool FileIOSystem::CloseFile(int Num, std::string* Error)
    {
        if (!ValidateFileNumber(Num, Error))
            return false;
        if (!OpenFiles_.contains(Num))
        {
            if (Error)
                *Error = "File " + std::to_string(Num) + " is not open";
            return false;
        }
        auto& File = *OpenFiles_.at(Num);
        bool Complete = true;
        if (File.Mode == FileMode::Write)
        {
            File.Stream.flush();
            Complete = File.Stream.good();
        }
        File.Stream.clear();
        File.Stream.close();
        Complete = Complete && !File.Stream.fail();
        OpenFiles_.erase(Num);
        if (!Complete)
        {
            if (Error)
                *Error = "Cannot close file " + std::to_string(Num) + " after an I/O failure";
            return false;
        }
        return true;
    }

    // --- Write primitives ---

    bool FileIOSystem::FileWriteByte(int Num, int Val, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Write, Error);
        if (!File)
            return false;
        const uint8_t V = static_cast<uint8_t>(Val);
        if (!WriteExact(*File, &V, 1, Error))
            return false;
        return true;
    }

    bool FileIOSystem::FileWriteWord(int Num, int Val, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Write, Error);
        if (!File)
            return false;
        const uint16_t V = static_cast<uint16_t>(Val);
        if (!WriteExact(*File, &V, 2, Error))
            return false;
        return true;
    }

    bool FileIOSystem::FileWriteLong(int Num, int Val, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Write, Error);
        if (!File)
            return false;
        const int32_t V = static_cast<int32_t>(Val);
        if (!WriteExact(*File, &V, 4, Error))
            return false;
        return true;
    }

    bool FileIOSystem::FileWriteFloat(int Num, float Val, std::string* Error)
    {
        if (!std::isfinite(Val))
        {
            if (Error)
                *Error = "WRITE FLOAT requires a finite value";
            return false;
        }
        FileHandle* File = OpenFile(Num, FileMode::Write, Error);
        if (!File)
            return false;
        if (!WriteExact(*File, &Val, 4, Error))
            return false;
        return true;
    }

    bool FileIOSystem::FileWriteString(int Num, const std::string& S, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Write, Error);
        if (!File)
            return false;
        if (!WriteExact(*File, S.data(), S.size(), Error))
            return false;
        constexpr char Newline = '\n';
        if (!WriteExact(*File, &Newline, 1, Error))
            return false;
        return true;
    }

    bool FileIOSystem::FileWriteBlock(int Num, const std::filesystem::path& Path, std::string* Error)
    {
        std::error_code FileError;
        const std::uintmax_t Size = std::filesystem::file_size(Path, FileError);
        if (FileError)
        {
            if (Error)
                *Error = "WRITE FILEBLOCK cannot read '" + dbc::PathToUtf8(Path) + "': " + FileError.message();
            return false;
        }
        if (Size > static_cast<std::uintmax_t>(std::numeric_limits<std::int32_t>::max()))
        {
            if (Error)
                *Error = "WRITE FILEBLOCK source exceeds the historical 32-bit format";
            return false;
        }

        std::ifstream Input(Path, std::ios::in | std::ios::binary);
        if (!Input.is_open())
        {
            if (Error)
                *Error = "WRITE FILEBLOCK cannot open '" + dbc::PathToUtf8(Path) + "'";
            return false;
        }
        std::vector<uint8_t> Bytes(static_cast<std::size_t>(Size));
        if (!Bytes.empty())
        {
            Input.read(reinterpret_cast<char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
            if (Input.gcount() != static_cast<std::streamsize>(Bytes.size()))
            {
                if (Error)
                    *Error = "WRITE FILEBLOCK could not read the complete source file";
                return false;
            }
        }
        if (!FileWriteLong(Num, static_cast<int>(Bytes.size()), Error))
            return false;
        if (!FileWriteBytes(Num, Bytes, Error))
            return false;
        return true;
    }

    // --- Read primitives ---

    bool FileIOSystem::FileReadByte(int& Output, int Num, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        uint8_t V = 0;
        if (!ReadExact(*File, &V, 1, true, Error))
            return false;
        {
            Output = static_cast<int>(V);
            return true;
        }
    }

    bool FileIOSystem::FileReadWord(int& Output, int Num, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        uint16_t V = 0;
        if (!ReadExact(*File, &V, 2, true, Error))
            return false;
        {
            Output = static_cast<int>(V);
            return true;
        }
    }

    bool FileIOSystem::FileReadLong(int& Output, int Num, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        int32_t V = 0;
        if (!ReadExact(*File, &V, 4, true, Error))
            return false;
        {
            Output = static_cast<int>(V);
            return true;
        }
    }

    bool FileIOSystem::FileReadFloat(float& Output, int Num, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        float V = 0.0f;
        if (!ReadExact(*File, &V, 4, true, Error))
            return false;
        if (!std::isfinite(V))
        {
            if (Error)
                *Error = "READ FLOAT found a non-finite value";
            return false;
        }
        {
            Output = V;
            return true;
        }
    }

    bool FileIOSystem::FileReadString(std::string& Output, int Num, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        std::string Result;
        char Character = 0;
        while (true)
        {
            File->Stream.read(&Character, 1);
            if (File->Stream.bad())
            {
                if (Error)
                    *Error = "File string read failed";
                return false;
            }
            if (File->Stream.gcount() == 0)
            {
                File->AtEnd = true;
                break;
            }
            if (static_cast<unsigned char>(Character) <= 13)
                break;
            Result.push_back(Character);
        }
        {
            Output = Result;
            return true;
        }
    }

    bool FileIOSystem::FileReadBlock(int Num, const std::filesystem::path& Path, std::string* Error)
    {
        int Size = 0;
        if (!FileReadLong(Size, Num, Error))
            return false;
        if (Size < 0)
        {
            if (Error)
                *Error = "READ FILEBLOCK found an invalid size header";
            return false;
        }
        std::vector<uint8_t> Bytes;
        if (!FileReadBytes(Bytes, Num, static_cast<std::size_t>(Size), Error))
            return false;

        std::error_code FileError;
        if (!Path.parent_path().empty())
        {
            std::filesystem::create_directories(Path.parent_path(), FileError);
            if (FileError)
            {
                if (Error)
                    *Error = "READ FILEBLOCK cannot create destination directories: " + FileError.message();
                return false;
            }
        }
        std::ofstream Output(Path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!Output.is_open())
        {
            if (Error)
                *Error = "READ FILEBLOCK cannot create '" + dbc::PathToUtf8(Path) + "'";
            return false;
        }
        if (!Bytes.empty())
        {
            Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
            if (!Output)
            {
                if (Error)
                    *Error = "READ FILEBLOCK failed while writing '" + dbc::PathToUtf8(Path) + "'";
                return false;
            }
        }
        return true;
    }

    bool FileIOSystem::FileReadBytes(std::vector<uint8_t>& Output, int Num, std::size_t Size, std::string* Error)
    {
        if (Size == 0)
        {
            Output.clear();
            return true;
        }
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        std::vector<uint8_t> Bytes(Size);
        if (!ReadExact(*File, Bytes.data(), Bytes.size(), false, Error))
            return false;
        Output = std::move(Bytes);
        return true;
    }

    bool FileIOSystem::FileWriteBytes(int Num, std::span<const uint8_t> Bytes, std::string* Error)
    {
        if (Bytes.empty())
            return true;
        FileHandle* File = OpenFile(Num, FileMode::Write, Error);
        return File && WriteExact(*File, Bytes.data(), Bytes.size(), Error);
    }

    bool FileIOSystem::SkipBytes(int Num, int Count, std::string* Error)
    {
        if (Count < 0)
        {
            if (Error)
                *Error = "SKIP BYTES requires a non-negative count";
            return false;
        }
        std::vector<uint8_t> Skipped;
        if (!FileReadBytes(Skipped, Num, static_cast<std::size_t>(Count), Error))
            return false;
        return true;
    }

    bool FileIOSystem::FileSize(std::size_t& Output, int Num, std::string* Error)
    {
        FileHandle* File = OpenFile(Num, FileMode::Read, Error);
        if (!File)
            return false;
        const std::streampos Position = File->Stream.tellg();
        if (Position < 0)
        {
            if (Error)
                *Error = "Cannot determine the current file position";
            return false;
        }
        File->Stream.seekg(0, std::ios::end);
        const std::streampos End = File->Stream.tellg();
        File->Stream.seekg(Position);
        if (End < 0 || !File->Stream)
        {
            if (Error)
                *Error = "Cannot determine the file size";
            return false;
        }
        {
            Output = static_cast<std::size_t>(End);
            return true;
        }
    }

    // --- Queries ---

    bool FileIOSystem::FileEnd(int& Output, int Num, std::string* Error) const
    {
        const FileHandle* File = OpenFile(Num, Error);
        if (!File)
            return false;
        Output = File->AtEnd ? 1 : 0;
        return true;
    }

    bool FileIOSystem::FileIsOpen(int& Output, int Num, std::string* Error) const
    {
        if (!ValidateFileNumber(Num, Error))
            return false;
        {
            Output = OpenFiles_.count(Num) ? 1 : 0;
            return true;
        }
    }

    // --- Directory iteration ---

    bool FileIOSystem::UpdateFindEntry(std::string* Error)
    {
        if (FindIndex_ >= FindEntries_.size())
        {
            FindFileName_.clear();
            FindFileType_ = -1;
            FindFileDate_.clear();
            return true;
        }
        const auto& Entry = FindEntries_[FindIndex_];
        FindFileName_ = dbc::PathToUtf8(Entry.path().filename());
        std::error_code Ec;
        FindFileType_ = Entry.is_directory(Ec) ? 1 : 0;
        if (Ec)
        {
            if (Error)
                *Error = "Cannot inspect directory entry: " + Ec.message();
            return false;
        }
        auto Ftime = std::filesystem::last_write_time(Entry.path(), Ec);
        if (!Ec)
        {
            const auto SysTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                Ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const std::time_t T = std::chrono::system_clock::to_time_t(SysTime);
            std::tm TmBuf{};
#if defined(_WIN32)
            localtime_s(&TmBuf, &T);
#else
            localtime_r(&T, &TmBuf);
#endif
            char Buf[32];
            std::strftime(Buf, sizeof(Buf), "%a %b %d %H:%M:%S %Y", &TmBuf);
            FindFileDate_ = Buf;
        }
        else
        {
            FindFileDate_.clear();
            if (Error)
                *Error = "Cannot read directory entry date: " + Ec.message();
            return false;
        }
        return true;
    }

    bool FileIOSystem::FindFirst(const std::string& Path, std::string* Error)
    {
        FindEntries_.clear();
        FindIndex_ = 0;
        std::error_code Ec;
        const std::filesystem::path Dir = Path.empty() ? std::filesystem::current_path(Ec) : dbc::PathFromUtf8(Path);
        if (Ec)
        {
            if (Error)
                *Error = "Cannot resolve the current directory: " + Ec.message();
            return false;
        }
        for (std::filesystem::directory_iterator It(Dir, Ec), End; !Ec && It != End; It.increment(Ec))
            FindEntries_.push_back(*It);
        if (Ec)
        {
            if (Error)
                *Error = "Cannot scan directory '" + dbc::PathToUtf8(Dir) + "': " + Ec.message();
            return false;
        }
        std::sort(FindEntries_.begin(), FindEntries_.end(), [](const auto& Left, const auto& Right)
                  { return dbc::PathToUtf8(Left.path().filename()) < dbc::PathToUtf8(Right.path().filename()); });
        if (!UpdateFindEntry(Error))
            return false;
        return true;
    }

    bool FileIOSystem::FindNext(std::string* Error)
    {
        if (FindIndex_ >= FindEntries_.size())
        {
            if (Error)
                *Error = "FIND NEXT has no more directory entries";
            return false;
        }
        ++FindIndex_;
        if (!UpdateFindEntry(Error))
            return false;
        return true;
    }

    std::string FileIOSystem::GetFindFileName() const
    {
        return FindFileName_;
    }

    int FileIOSystem::GetFindFileType() const
    {
        return FindFileType_;
    }

    std::string FileIOSystem::GetFindFileDate() const
    {
        return FindFileDate_;
    }

} // namespace dbc
