#include "file_map_system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dbc
{
    namespace
    {
        constexpr std::uint32_t KMagic = 0x504D4244; // "DBMP"
        constexpr std::uint32_t KMaximumSize = 1u << 30;

        // Starts every map, before the program's bytes; all programs reach it through the same atomics.
        struct SharedHeader
        {
            std::atomic<std::uint32_t> Magic;
            std::atomic<std::uint32_t> Size;
            std::atomic<std::uint32_t> Lock;
            std::atomic<std::uint32_t> Sequence;
        };

        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        static_assert(sizeof(SharedHeader) == 16);

        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

        bool ValidId(int Id, std::string* Error)
        {
            return (Id >= 0 && Id <= 255) || Fail(Error, "File map number must be between 0 and 255");
        }

        // Names may hold any character; the system object only takes a few.
        std::string EncodeName(const std::string& Name)
        {
            static constexpr char Digits[] = "0123456789ABCDEF";
            std::string Encoded;
            for (const char Character : Name)
            {
                const auto Byte = static_cast<unsigned char>(Character);
                if ((Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= '0' && Byte <= '9') ||
                    Byte == '-' || Byte == '_')
                {
                    Encoded.push_back(Character);
                    continue;
                }
                Encoded += '.';
                Encoded += Digits[Byte >> 4];
                Encoded += Digits[Byte & 0xF];
            }
            return Encoded;
        }

        class SpinLock
        {
          public:
            explicit SpinLock(std::atomic<std::uint32_t>& Word) : Word_(Word)
            {
                std::uint32_t Expected = 0;
                while (!Word_.compare_exchange_weak(Expected, 1, std::memory_order_acquire))
                {
                    Expected = 0;
                    std::this_thread::yield();
                }
            }

            ~SpinLock()
            {
                Word_.store(0, std::memory_order_release);
            }

            SpinLock(const SpinLock&) = delete;
            SpinLock& operator=(const SpinLock&) = delete;

          private:
            std::atomic<std::uint32_t>& Word_;
        };
    } // namespace

    class FileMapSystem::Mapping
    {
      public:
        // Size 0 opens a map that must exist.
        static std::unique_ptr<Mapping> Attach(const std::string& Name, std::uint32_t Size, std::string* Error)
        {
            auto Result = std::unique_ptr<Mapping>(new Mapping());
            if (!Result->Map(Name, Size, Error))
                return nullptr;
            return Result;
        }

        ~Mapping()
        {
            Release(false);
        }

        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;

        SharedHeader& Header() const
        {
            return *static_cast<SharedHeader*>(View_);
        }

        std::uint8_t* Data() const
        {
            return static_cast<std::uint8_t*>(View_) + sizeof(SharedHeader);
        }

        std::uint32_t Size() const
        {
            return Size_;
        }

        void Release(bool Remove)
        {
#if defined(_WIN32)
            // Windows removes the mapping with its last handle.
            (void)Remove;
            if (View_)
                UnmapViewOfFile(View_);
            if (Handle_)
                CloseHandle(Handle_);
            Handle_ = nullptr;
#else
            if (View_)
                munmap(View_, Length_);
            if (File_ >= 0)
            {
                if (Remove && flock(File_, LOCK_EX | LOCK_NB) == 0)
                    unlink(Path_.c_str());
                close(File_);
            }
            File_ = -1;
#endif
            View_ = nullptr;
        }

      private:
        Mapping() = default;

        void Initialise(std::uint32_t Size)
        {
            Size_ = Size;
            Header().Size.store(Size, std::memory_order_relaxed);
            Header().Magic.store(KMagic, std::memory_order_release);
        }

        bool Adopt(std::string* Error, const std::string& Name)
        {
            if (Header().Magic.load(std::memory_order_acquire) != KMagic)
                return Fail(Error, "File map '" + Name + "' is not ready");
            Size_ = Header().Size.load(std::memory_order_relaxed);
            return true;
        }

#if defined(_WIN32)
        bool Map(const std::string& Name, std::uint32_t Size, std::string* Error)
        {
            const std::string SystemName = "OpenDBC.FileMap." + EncodeName(Name);
            if (Size == 0)
            {
                Handle_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SystemName.c_str());
                if (!Handle_)
                    return Fail(Error, "File map '" + Name + "' does not exist");
            }
            else
            {
                Handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                             static_cast<DWORD>(sizeof(SharedHeader) + Size), SystemName.c_str());
                if (!Handle_)
                    return Fail(Error, "Cannot create file map '" + Name + "'");
            }
            const bool Existed = Size == 0 || GetLastError() == ERROR_ALREADY_EXISTS;
            View_ = MapViewOfFile(Handle_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            if (!View_)
                return Fail(Error, "Cannot map file map '" + Name + "'");
            if (Existed)
                return Adopt(Error, Name);
            Initialise(Size);
            return true;
        }

        HANDLE Handle_ = nullptr;
#else
        bool Map(const std::string& Name, std::uint32_t Size, std::string* Error)
        {
            std::error_code TempError;
            const std::filesystem::path Directory = std::filesystem::temp_directory_path(TempError);
            if (TempError)
                return Fail(Error, "File maps need a temporary directory");
            Path_ = (Directory / ("opendbc-filemap-" + EncodeName(Name))).string();
            File_ = open(Path_.c_str(), Size == 0 ? O_RDWR : O_RDWR | O_CREAT, 0600);
            if (File_ < 0)
                return Fail(Error, Size == 0 ? "File map '" + Name + "' does not exist"
                                             : "Cannot create file map '" + Name + "'");
            // Every program holds a shared lock while it uses the map, so an exclusive one means nobody does: the
            // file is new or what a program that stopped left behind. The creator sets the map up before it shares
            // the lock, so a program that opens it meanwhile waits for a ready map.
            const bool Abandoned = flock(File_, LOCK_EX | LOCK_NB) == 0;
            if (Abandoned && Size == 0)
            {
                unlink(Path_.c_str());
                return Fail(Error, "File map '" + Name + "' does not exist");
            }
            if (Abandoned)
            {
                if (ftruncate(File_, 0) != 0 || ftruncate(File_, static_cast<off_t>(sizeof(SharedHeader) + Size)) != 0)
                    return Fail(Error, "Cannot size file map '" + Name + "'");
                if (!MapFile(Name, Error))
                    return false;
                Initialise(Size);
                if (flock(File_, LOCK_SH) != 0)
                    return Fail(Error, "Cannot share file map '" + Name + "'");
                return true;
            }
            if (flock(File_, LOCK_SH) != 0)
                return Fail(Error, "Cannot share file map '" + Name + "'");
            if (!MapFile(Name, Error) || !Adopt(Error, Name))
                return false;
            if (Size_ > Length_ - sizeof(SharedHeader))
                return Fail(Error, "File map '" + Name + "' is damaged");
            return true;
        }

        bool MapFile(const std::string& Name, std::string* Error)
        {
            struct stat Status{};
            if (fstat(File_, &Status) != 0 || Status.st_size < static_cast<off_t>(sizeof(SharedHeader)))
                return Fail(Error, "File map '" + Name + "' is not ready");
            Length_ = static_cast<std::size_t>(Status.st_size);
            View_ = mmap(nullptr, Length_, PROT_READ | PROT_WRITE, MAP_SHARED, File_, 0);
            if (View_ == MAP_FAILED)
            {
                View_ = nullptr;
                return Fail(Error, "Cannot map file map '" + Name + "'");
            }
            return true;
        }

        std::size_t Length_ = 0;
        int File_ = -1;
        std::string Path_;
#endif

        void* View_ = nullptr;
        std::uint32_t Size_ = 0;
    };

    FileMapSystem::FileMapSystem() = default;

    FileMapSystem::~FileMapSystem() = default;

    FileMapSystem::Mapping* FileMapSystem::Find(int Id, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return nullptr;
        const auto Found = Maps_.find(Id);
        if (Found != Maps_.end())
            return Found->second.get();
        Fail(Error, "File map " + std::to_string(Id) + " does not exist");
        return nullptr;
    }

    bool FileMapSystem::Create(int Id, const std::string& Name, int Size, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        if (Size <= 0 || static_cast<std::uint32_t>(Size) > KMaximumSize)
            return Fail(Error, "File map size must be between 1 byte and 1 GB");
        Maps_.erase(Id);
        std::unique_ptr<Mapping> Attached = Mapping::Attach(Name, static_cast<std::uint32_t>(Size), Error);
        if (!Attached)
            return false;
        Maps_[Id] = std::move(Attached);
        return true;
    }

    bool FileMapSystem::Open(int Id, const std::string& Name, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        Maps_.erase(Id);
        std::unique_ptr<Mapping> Attached = Mapping::Attach(Name, 0, Error);
        if (!Attached)
            return false;
        Maps_[Id] = std::move(Attached);
        return true;
    }

    // The Enhancement Pack's example closes and then destroys the same map, so neither fails on a map already gone.
    bool FileMapSystem::Close(int Id, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        Maps_.erase(Id);
        return true;
    }

    bool FileMapSystem::Destroy(int Id, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        const auto Found = Maps_.find(Id);
        if (Found == Maps_.end())
            return true;
        Found->second->Release(true);
        Maps_.erase(Found);
        return true;
    }

    bool FileMapSystem::Access(int Id, int Offset, int Width, bool Write, const void* Input, void* Output,
                               std::string* Error)
    {
        Mapping* Map = Find(Id, Error);
        if (!Map)
            return false;
        if (Offset < 0 || static_cast<std::uint64_t>(Offset) + static_cast<std::uint64_t>(Width) > Map->Size())
            return Fail(Error, "File map offset " + std::to_string(Offset) + " is outside the map's " +
                                   std::to_string(Map->Size()) + " bytes");
        SpinLock Guard(Map->Header().Lock);
        if (Write)
        {
            std::memcpy(Map->Data() + Offset, Input, static_cast<std::size_t>(Width));
            Map->Header().Sequence.fetch_add(1, std::memory_order_release);
        }
        else
        {
            std::memcpy(Output, Map->Data() + Offset, static_cast<std::size_t>(Width));
        }
        return true;
    }

    bool FileMapSystem::ReadDword(int Id, int Offset, std::uint32_t& Output, std::string* Error)
    {
        return Access(Id, Offset, sizeof(Output), false, nullptr, &Output, Error);
    }

    bool FileMapSystem::ReadFloat(int Id, int Offset, float& Output, std::string* Error)
    {
        return Access(Id, Offset, sizeof(Output), false, nullptr, &Output, Error);
    }

    bool FileMapSystem::WriteDword(int Id, int Offset, std::uint32_t Value, std::string* Error)
    {
        return Access(Id, Offset, sizeof(Value), true, &Value, nullptr, Error);
    }

    bool FileMapSystem::WriteFloat(int Id, int Offset, float Value, std::string* Error)
    {
        return Access(Id, Offset, sizeof(Value), true, &Value, nullptr, Error);
    }

    bool FileMapSystem::ReadString(int Id, int Offset, std::string& Output, std::string* Error)
    {
        Mapping* Map = Find(Id, Error);
        if (!Map)
            return false;
        if (Offset < 0 || static_cast<std::uint32_t>(Offset) >= Map->Size())
            return Fail(Error, "File map offset " + std::to_string(Offset) + " is outside the map's " +
                                   std::to_string(Map->Size()) + " bytes");
        const std::uint32_t Field =
            std::min<std::uint32_t>(StringField, Map->Size() - static_cast<std::uint32_t>(Offset));
        SpinLock Guard(Map->Header().Lock);
        const auto* Start = reinterpret_cast<const char*>(Map->Data() + Offset);
        Output.assign(Start, strnlen(Start, Field));
        return true;
    }

    // As in the Enhancement Pack, the string's field is cleared first so a shorter string leaves nothing behind.
    bool FileMapSystem::WriteString(int Id, int Offset, const std::string& Value, std::string* Error)
    {
        Mapping* Map = Find(Id, Error);
        if (!Map)
            return false;
        if (Offset < 0 || static_cast<std::uint64_t>(Offset) + Value.size() > Map->Size() ||
            static_cast<std::uint32_t>(Offset) >= Map->Size())
            return Fail(Error, "File map offset " + std::to_string(Offset) + " and its " +
                                   std::to_string(Value.size()) + " characters are outside the map's " +
                                   std::to_string(Map->Size()) + " bytes");
        const std::uint32_t Field =
            std::min<std::uint32_t>(StringField, Map->Size() - static_cast<std::uint32_t>(Offset));
        SpinLock Guard(Map->Header().Lock);
        std::memset(Map->Data() + Offset, 0, Field);
        std::memcpy(Map->Data() + Offset, Value.data(), Value.size());
        Map->Header().Sequence.fetch_add(1, std::memory_order_release);
        return true;
    }

    bool FileMapSystem::WaitForWrite(int Id, std::string* Error)
    {
        Mapping* Map = Find(Id, Error);
        if (!Map)
            return false;
        const std::uint32_t Start = Map->Header().Sequence.load(std::memory_order_acquire);
        const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (Map->Header().Sequence.load(std::memory_order_acquire) == Start &&
               std::chrono::steady_clock::now() < Deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    }

} // namespace dbc
