#pragma once

#include "path_utf8.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dbc::interpreter
{
    inline bool CreateExclusiveTemporary(const std::filesystem::path& Destination, std::filesystem::path& Temporary,
                                         std::FILE*& Output, std::string* Error = nullptr)
    {
        std::FILE* File = nullptr;
        static std::atomic_uint64_t Counter = 0;
        for (int Attempt = 0; Attempt < 128; ++Attempt)
        {
            const std::uint64_t Suffix =
                static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                Counter.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
            Temporary = Destination.native() + std::wstring(L".opendbc-") + std::to_wstring(Suffix) + L".tmp";
            int Descriptor = -1;
            const errno_t Status =
                _wsopen_s(&Descriptor, Temporary.wstring().c_str(), _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL,
                          _SH_DENYRW, _S_IREAD | _S_IWRITE);
            if (Status == EEXIST)
                continue;
            if (Status == 0)
                File = _wfdopen(Descriptor, L"wb");
#else
            Temporary = Destination.native() + std::string(".opendbc-") + std::to_string(Suffix) + ".tmp";
            const int Descriptor = ::open(Temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            if (Descriptor < 0 && errno == EEXIST)
                continue;
            if (Descriptor >= 0)
                File = ::fdopen(Descriptor, "wb");
#endif
            if (File)
            {
                Output = File;
                return true;
            }
            if (Descriptor >= 0)
            {
#if defined(_WIN32)
                _close(Descriptor);
#else
                ::close(Descriptor);
#endif
                std::error_code RemoveError;
                std::filesystem::remove(Temporary, RemoveError);
            }
            if (Error)
                *Error = "Cannot create an exclusive temporary file for '" + dbc::PathToUtf8(Destination) + "'";
            return false;
        }
        if (Error)
            *Error = "Cannot allocate a unique temporary file for '" + dbc::PathToUtf8(Destination) + "'";
        return false;
    }

    inline bool AtomicWriteText(const std::filesystem::path& Path, std::string_view Bytes, std::string* Error = nullptr)
    {
        if (Path.empty() || Path.filename().empty())
        {
            if (Error)
                *Error = "Output path must name a file";
            return false;
        }
        std::filesystem::path Parent = Path.parent_path();
        if (Parent.empty())
            Parent = ".";
        std::error_code FileError;
        std::filesystem::create_directories(Parent, FileError);
        if (FileError)
        {
            if (Error)
                *Error = "Cannot create output directory '" + dbc::PathToUtf8(Parent) + "': " + FileError.message();
            return false;
        }
        std::filesystem::path Temporary;
        std::FILE* Output = nullptr;
        if (!CreateExclusiveTemporary(Path, Temporary, Output, Error))
            return false;

        struct TemporaryCleanup
        {
            std::filesystem::path Value;
            bool Active = true;

            ~TemporaryCleanup()
            {
                if (!Active)
                    return;
                std::error_code RemoveError;
                std::filesystem::remove(Value, RemoveError);
            }
        } Cleanup{Temporary};

        const bool Written =
            std::fwrite(Bytes.data(), 1, Bytes.size(), Output) == Bytes.size() && std::fflush(Output) == 0;
        const bool Closed = std::fclose(Output) == 0;
        if (!Written || !Closed)
        {
            if (Error)
                *Error = "Cannot finish output '" + dbc::PathToUtf8(Path) + "'";
            return false;
        }
#if defined(_WIN32)
        if (!MoveFileExW(Temporary.wstring().c_str(), Path.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            if (Error)
                *Error = "Cannot publish output '" + dbc::PathToUtf8(Path) + "'";
            return false;
        }
#else
        std::filesystem::rename(Temporary, Path, FileError);
        if (FileError)
        {
            if (Error)
                *Error = "Cannot publish output '" + dbc::PathToUtf8(Path) + "': " + FileError.message();
            return false;
        }
#endif
        Cleanup.Active = false;
        return true;
    }

} // namespace dbc::interpreter
