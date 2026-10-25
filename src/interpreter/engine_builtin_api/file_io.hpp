#pragma once

#include "../../engine/systems/engine_kernel.hpp"
#include "../../engine/systems/file_io_system.hpp"

namespace dbc
{
    // File open/close
    inline bool OpenToRead(EngineKernel& K, int Num, const std::string& Path, std::string* Error = nullptr)
    {
        return K.FileIO().OpenToRead(Num, Path, Error);
    }
    inline bool OpenToWrite(EngineKernel& K, int Num, const std::string& Path, std::string* Error = nullptr)
    {
        return K.FileIO().OpenToWrite(Num, Path, Error);
    }
    inline bool OpenToAppend(EngineKernel& K, int Num, const std::string& Path, std::string* Error = nullptr)
    {
        return K.FileIO().OpenToAppend(Num, Path, Error);
    }
    inline bool CloseFile(EngineKernel& K, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().CloseFile(Num, Error);
    }

    // Write primitives
    inline bool FileWriteByte(EngineKernel& K, int Num, int Val, std::string* Error = nullptr)
    {
        return K.FileIO().FileWriteByte(Num, Val, Error);
    }
    inline bool FileWriteWord(EngineKernel& K, int Num, int Val, std::string* Error = nullptr)
    {
        return K.FileIO().FileWriteWord(Num, Val, Error);
    }
    inline bool FileWriteLong(EngineKernel& K, int Num, int Val, std::string* Error = nullptr)
    {
        return K.FileIO().FileWriteLong(Num, Val, Error);
    }
    inline bool FileWriteFloat(EngineKernel& K, int Num, float Val, std::string* Error = nullptr)
    {
        return K.FileIO().FileWriteFloat(Num, Val, Error);
    }
    inline bool FileWriteString(EngineKernel& K, int Num, const std::string& S, std::string* Error = nullptr)
    {
        return K.FileIO().FileWriteString(Num, S, Error);
    }

    // Read primitives preserve neutral values at a clean end of file.
    inline bool FileReadByte(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileReadByte(Output, Num, Error);
    }
    inline bool FileReadWord(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileReadWord(Output, Num, Error);
    }
    inline bool FileReadLong(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileReadLong(Output, Num, Error);
    }
    inline bool FileReadFloat(EngineKernel& K, float& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileReadFloat(Output, Num, Error);
    }
    inline bool FileReadString(EngineKernel& K, std::string& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileReadString(Output, Num, Error);
    }

    // Queries
    inline bool FileEnd(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileEnd(Output, Num, Error);
    }
    inline bool FileIsOpen(EngineKernel& K, int& Output, int Num, std::string* Error = nullptr)
    {
        return K.FileIO().FileIsOpen(Output, Num, Error);
    }

    // Directory iteration (FIND FIRST / FIND NEXT)
    inline bool FindFirst(EngineKernel& K, const std::string& Path, std::string* Error = nullptr)
    {
        return K.FileIO().FindFirst(Path, Error);
    }
    inline bool FindNext(EngineKernel& K, std::string* Error = nullptr)
    {
        return K.FileIO().FindNext(Error);
    }
    inline std::string GetFindFileName(EngineKernel& K)
    {
        return K.FileIO().GetFindFileName();
    }
    inline int GetFindFileType(EngineKernel& K)
    {
        return K.FileIO().GetFindFileType();
    }
    inline std::string GetFindFileDate(EngineKernel& K)
    {
        return K.FileIO().GetFindFileDate();
    }

} // namespace dbc
