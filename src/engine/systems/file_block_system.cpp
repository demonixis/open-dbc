#include "file_block_system.hpp"

#include "path_utf8.hpp"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <fstream>
#include <random>

namespace dbc
{
    namespace
    {
        constexpr std::uint32_t KLocalHeader = 0x04034B50;
        constexpr std::uint32_t KCentralHeader = 0x02014B50;
        constexpr std::uint32_t KEndOfDirectory = 0x06054B50;
        constexpr std::uint16_t KEncrypted = 0x0001;
        constexpr std::uint16_t KDataDescriptor = 0x0008;
        constexpr std::uint16_t KStrongEncryption = 0x0040;
        constexpr std::uint16_t KUtf8Name = 0x0800;
        constexpr std::size_t KEncryptionHeader = 12;

        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

        constexpr std::array<std::uint32_t, 256> MakeCrcTable()
        {
            std::array<std::uint32_t, 256> Table{};
            for (std::uint32_t Index = 0; Index < 256; ++Index)
            {
                std::uint32_t Value = Index;
                for (int Bit = 0; Bit < 8; ++Bit)
                    Value = (Value & 1u) != 0 ? 0xEDB88320u ^ (Value >> 1) : Value >> 1;
                Table[Index] = Value;
            }
            return Table;
        }

        constexpr std::array<std::uint32_t, 256> KCrcTable = MakeCrcTable();

        std::uint32_t CrcStep(std::uint32_t Crc, std::uint8_t Byte)
        {
            return KCrcTable[(Crc ^ Byte) & 0xFFu] ^ (Crc >> 8);
        }

        std::uint32_t Crc32(const std::vector<std::uint8_t>& Bytes)
        {
            std::uint32_t Crc = 0xFFFFFFFFu;
            for (const std::uint8_t Byte : Bytes)
                Crc = CrcStep(Crc, Byte);
            return ~Crc;
        }

        // Traditional PKWARE encryption, which the Enhancement Pack's ZIP library wrote for keyed blocks.
        class ZipCrypto
        {
          public:
            explicit ZipCrypto(const std::string& Key)
            {
                for (const char Character : Key)
                    Update(static_cast<std::uint8_t>(Character));
            }

            std::uint8_t Encrypt(std::uint8_t Plain)
            {
                const std::uint8_t Cipher = Plain ^ Stream();
                Update(Plain);
                return Cipher;
            }

            std::uint8_t Decrypt(std::uint8_t Cipher)
            {
                const std::uint8_t Plain = Cipher ^ Stream();
                Update(Plain);
                return Plain;
            }

          private:
            std::uint8_t Stream() const
            {
                const std::uint32_t Temp = (Keys_[2] | 2u) & 0xFFFFu;
                return static_cast<std::uint8_t>((Temp * (Temp ^ 1u)) >> 8);
            }

            void Update(std::uint8_t Plain)
            {
                Keys_[0] = CrcStep(Keys_[0], Plain);
                Keys_[1] = (Keys_[1] + (Keys_[0] & 0xFFu)) * 134775813u + 1u;
                Keys_[2] = CrcStep(Keys_[2], static_cast<std::uint8_t>(Keys_[1] >> 24));
            }

            std::array<std::uint32_t, 3> Keys_ = {0x12345678u, 0x23456789u, 0x34567890u};
        };

        void Put16(std::vector<std::uint8_t>& Output, std::uint32_t Value)
        {
            Output.push_back(static_cast<std::uint8_t>(Value));
            Output.push_back(static_cast<std::uint8_t>(Value >> 8));
        }

        void Put32(std::vector<std::uint8_t>& Output, std::uint32_t Value)
        {
            Put16(Output, Value & 0xFFFFu);
            Put16(Output, Value >> 16);
        }

        std::uint16_t Get16(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
        {
            return static_cast<std::uint16_t>(Bytes[Offset] | Bytes[Offset + 1] << 8);
        }

        std::uint32_t Get32(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
        {
            return static_cast<std::uint32_t>(Get16(Bytes, Offset)) |
                   static_cast<std::uint32_t>(Get16(Bytes, Offset + 2)) << 16;
        }

        bool ReadWholeFile(const std::string& Path, std::vector<std::uint8_t>& Output)
        {
            std::ifstream Input(PathFromUtf8(Path), std::ios::binary);
            if (!Input)
                return false;
            Output.assign(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
            return !Input.bad();
        }

        bool WriteWholeFile(const std::filesystem::path& Path, const std::vector<std::uint8_t>& Bytes)
        {
            std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
            Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
            return static_cast<bool>(Output.flush());
        }

        bool SameName(const std::string& Left, const std::string& Right)
        {
            return Left.size() == Right.size() && std::equal(Left.begin(), Left.end(), Right.begin(),
                                                             [](char A, char B)
                                                             {
                                                                 return std::tolower(static_cast<unsigned char>(A)) ==
                                                                        std::tolower(static_cast<unsigned char>(B));
                                                             });
        }

        void DosNow(std::uint16_t& Time, std::uint16_t& Date)
        {
            const std::time_t Now = std::time(nullptr);
            std::tm Local{};
#if defined(_WIN32)
            localtime_s(&Local, &Now);
#else
            localtime_r(&Now, &Local);
#endif
            Time = static_cast<std::uint16_t>(Local.tm_hour << 11 | Local.tm_min << 5 | Local.tm_sec / 2);
            Date = static_cast<std::uint16_t>(std::max(0, Local.tm_year - 80) << 9 | (Local.tm_mon + 1) << 5 |
                                              Local.tm_mday);
        }

        bool ValidId(int Id, std::string* Error)
        {
            return (Id >= 0 && Id <= 255) || Fail(Error, "File block number must be between 0 and 255");
        }

        // Parses a whole archive; entries keep their stored bytes until something reads them.
        template <typename EntryType>
        bool ParseArchive(const std::vector<std::uint8_t>& Bytes, std::vector<EntryType>& Entries,
                          const std::string& Path, std::string* Error)
        {
            const auto Damaged = [&] { return Fail(Error, "'" + Path + "' is not a file block (ZIP archive)"); };
            if (Bytes.size() < 22)
                return Damaged();
            std::size_t End = Bytes.size() - 22;
            const std::size_t Lowest = Bytes.size() > 22 + 0xFFFF ? Bytes.size() - 22 - 0xFFFF : 0;
            while (Get32(Bytes, End) != KEndOfDirectory)
            {
                if (End == Lowest)
                    return Damaged();
                --End;
            }
            const std::uint16_t Count = Get16(Bytes, End + 10);
            std::size_t Offset = Get32(Bytes, End + 16);
            for (std::uint16_t Index = 0; Index < Count; ++Index)
            {
                if (Offset + 46 > Bytes.size() || Get32(Bytes, Offset) != KCentralHeader)
                    return Damaged();
                EntryType Item;
                Item.Flags = Get16(Bytes, Offset + 8);
                Item.Method = Get16(Bytes, Offset + 10);
                Item.Time = Get16(Bytes, Offset + 12);
                Item.Date = Get16(Bytes, Offset + 14);
                Item.Crc = Get32(Bytes, Offset + 16);
                const std::uint32_t Compressed = Get32(Bytes, Offset + 20);
                Item.UncompressedSize = Get32(Bytes, Offset + 24);
                const std::size_t NameLength = Get16(Bytes, Offset + 28);
                const std::size_t Skipped =
                    static_cast<std::size_t>(Get16(Bytes, Offset + 30)) + Get16(Bytes, Offset + 32);
                const std::size_t Local = Get32(Bytes, Offset + 42);
                if (Offset + 46 + NameLength + Skipped > Bytes.size() || Local + 30 > Bytes.size() ||
                    Get32(Bytes, Local) != KLocalHeader)
                    return Damaged();
                Item.Name.assign(reinterpret_cast<const char*>(Bytes.data() + Offset + 46), NameLength);
                const std::size_t Data = Local + 30 + Get16(Bytes, Local + 26) + Get16(Bytes, Local + 28);
                if (Data + Compressed > Bytes.size())
                    return Damaged();
                Item.Data.assign(Bytes.begin() + static_cast<std::ptrdiff_t>(Data),
                                 Bytes.begin() + static_cast<std::ptrdiff_t>(Data + Compressed));
                Entries.push_back(std::move(Item));
                Offset += 46 + NameLength + Skipped;
            }
            return true;
        }
    } // namespace

    FileBlockSystem::FileBlockSystem() = default;

    FileBlockSystem::~FileBlockSystem()
    {
        if (!Temporary_.empty())
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Temporary_, Ignored);
        }
    }

    std::string FileBlockSystem::NormaliseEntryName(const std::string& Name)
    {
        std::string Path = Name;
        std::replace(Path.begin(), Path.end(), '\\', '/');
        if (Path.size() >= 2 && Path[1] == ':')
            Path.erase(0, 2);
        std::string Result;
        std::size_t Start = 0;
        while (Start <= Path.size())
        {
            const std::size_t Slash = std::min(Path.find('/', Start), Path.size());
            const std::string Part = Path.substr(Start, Slash - Start);
            if (!Part.empty() && Part != ".")
                Result += (Result.empty() ? "" : "/") + Part;
            Start = Slash + 1;
        }
        return Result;
    }

    FileBlockSystem::Block* FileBlockSystem::Find(int Id, std::string* Error)
    {
        return const_cast<Block*>(static_cast<const FileBlockSystem*>(this)->Find(Id, Error));
    }

    const FileBlockSystem::Block* FileBlockSystem::Find(int Id, std::string* Error) const
    {
        if (!ValidId(Id, Error))
            return nullptr;
        const auto Found = Blocks_.find(Id);
        if (Found != Blocks_.end())
            return &Found->second;
        Fail(Error, "File block " + std::to_string(Id) + " does not exist");
        return nullptr;
    }

    const FileBlockSystem::Entry* FileBlockSystem::FindEntry(const Block& Owner, const std::string& EntryName)
    {
        const std::string Name = NormaliseEntryName(EntryName);
        for (const Entry& Item : Owner.Entries)
        {
            if (SameName(NormaliseEntryName(Item.Name), Name))
                return &Item;
        }
        return nullptr;
    }

    bool FileBlockSystem::Create(int Id, const std::string& Path, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        Block Created;
        Created.Path = Path;
        Created.Modified = true;
        Blocks_[Id] = std::move(Created);
        return true;
    }

    bool FileBlockSystem::Open(int Id, const std::string& Path, const std::string& Key, std::string* Error)
    {
        if (!ValidId(Id, Error))
            return false;
        std::vector<std::uint8_t> Bytes;
        if (!ReadWholeFile(Path, Bytes))
            return Fail(Error, "OPEN FILE BLOCK cannot read '" + Path + "'");
        Block Opened;
        Opened.Path = Path;
        // Opening gave the Enhancement Pack's archive the key, so files added afterwards are encrypted with it too.
        Opened.Key = Key;
        Opened.Keyed = !Key.empty();
        if (!ParseArchive(Bytes, Opened.Entries, Path, Error))
            return false;
        Blocks_[Id] = std::move(Opened);
        return true;
    }

    bool FileBlockSystem::Close(int Id, std::string* Error)
    {
        Block* Found = Find(Id, Error);
        if (!Found || (Found->Modified && !Write(*Found, Error)))
            return false;
        Blocks_.erase(Id);
        return true;
    }

    bool FileBlockSystem::Delete(int Id, std::string* Error)
    {
        if (!Find(Id, Error))
            return false;
        Blocks_.erase(Id);
        return true;
    }

    bool FileBlockSystem::Save(int Id, std::string* Error)
    {
        Block* Found = Find(Id, Error);
        return Found && Write(*Found, Error);
    }

    bool FileBlockSystem::SetKey(int Id, const std::string& Key, std::string* Error)
    {
        Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        Found->Key = Key;
        Found->Keyed = !Key.empty();
        return true;
    }

    bool FileBlockSystem::SetCompression(int Id, int Level, std::string* Error)
    {
        Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        if (Level < 0 || Level > 9)
            return Fail(Error, "File block compression must be between 0 and 9");
        Found->Compression = Level;
        return true;
    }

    bool FileBlockSystem::Compression(int Id, int& Output, std::string* Error) const
    {
        const Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output = Found->Compression;
        return true;
    }

    bool FileBlockSystem::AddFile(int Id, const std::string& EntryName, const std::string& SourcePath,
                                  std::string* Error)
    {
        if (!Find(Id, Error))
            return false;
        std::vector<std::uint8_t> Bytes;
        if (!ReadWholeFile(SourcePath, Bytes))
            return Fail(Error, "Cannot add '" + SourcePath + "' to file block " + std::to_string(Id));
        return AddBytes(Id, EntryName, Bytes, Error);
    }

    bool FileBlockSystem::AddBytes(int Id, const std::string& EntryName, const std::vector<std::uint8_t>& Bytes,
                                   std::string* Error)
    {
        Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        Entry Added;
        Added.Name = NormaliseEntryName(EntryName);
        if (Added.Name.empty())
            return Fail(Error, "A file block entry needs a name");
        if (Bytes.size() > 0xFFFFFFFFu)
            return Fail(Error, "'" + Added.Name + "' is too large for a file block");
        if (std::any_of(Added.Name.begin(), Added.Name.end(),
                        [](char C) { return static_cast<unsigned char>(C) >= 0x80; }))
            Added.Flags |= KUtf8Name;
        Added.Crc = Crc32(Bytes);
        Added.UncompressedSize = static_cast<std::uint32_t>(Bytes.size());
        DosNow(Added.Time, Added.Date);

        std::vector<std::uint8_t> Stored = Bytes;
        if (Found->Compression > 0 && !Bytes.empty())
        {
            std::size_t Length = 0;
            const int Flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(
                Found->Compression, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY));
            void* Deflated = tdefl_compress_mem_to_heap(Bytes.data(), Bytes.size(), &Length, Flags);
            if (!Deflated)
                return Fail(Error, "Cannot compress '" + Added.Name + "'");
            if (Length < Bytes.size())
            {
                const auto* Start = static_cast<const std::uint8_t*>(Deflated);
                Stored.assign(Start, Start + Length);
                Added.Method = MZ_DEFLATED;
            }
            mz_free(Deflated);
        }
        if (Found->Keyed)
        {
            Added.Flags |= KEncrypted;
            ZipCrypto Cipher(Found->Key);
            std::random_device Random;
            Added.Data.reserve(KEncryptionHeader + Stored.size());
            for (std::size_t Index = 0; Index + 1 < KEncryptionHeader; ++Index)
                Added.Data.push_back(Cipher.Encrypt(static_cast<std::uint8_t>(Random())));
            // The last header byte lets a reader tell a wrong key from a damaged file.
            Added.Data.push_back(Cipher.Encrypt(static_cast<std::uint8_t>(Added.Crc >> 24)));
            for (const std::uint8_t Byte : Stored)
                Added.Data.push_back(Cipher.Encrypt(Byte));
        }
        else
        {
            Added.Data = std::move(Stored);
        }

        auto& Entries = Found->Entries;
        Entries.erase(std::remove_if(Entries.begin(), Entries.end(), [&](const Entry& Item)
                                     { return SameName(NormaliseEntryName(Item.Name), Added.Name); }),
                      Entries.end());
        if (Entries.size() >= 0xFFFF)
            return Fail(Error, "A file block holds at most 65535 files");
        Entries.push_back(std::move(Added));
        Found->Modified = true;
        return true;
    }

    bool FileBlockSystem::Remove(int Id, const std::string& EntryName, std::string* Error)
    {
        Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        const Entry* Item = FindEntry(*Found, EntryName);
        if (!Item)
            return Fail(Error, "File block " + std::to_string(Id) + " has no file '" + EntryName + "'");
        Found->Entries.erase(Found->Entries.begin() + (Item - Found->Entries.data()));
        Found->Modified = true;
        return true;
    }

    bool FileBlockSystem::Decode(const Block& Owner, const Entry& Source, std::vector<std::uint8_t>& Output,
                                 std::string* Error)
    {
        std::vector<std::uint8_t> Stored;
        if ((Source.Flags & KStrongEncryption) != 0)
            return Fail(Error, "'" + Source.Name + "' uses strong encryption, which file blocks never wrote");
        if ((Source.Flags & KEncrypted) != 0)
        {
            if (!Owner.Keyed)
                return Fail(Error, "'" + Source.Name + "' is encrypted; give the file block its key");
            if (Source.Data.size() < KEncryptionHeader)
                return Fail(Error, "'" + Source.Name + "' is damaged");
            ZipCrypto Cipher(Owner.Key);
            std::uint8_t Check = 0;
            for (std::size_t Index = 0; Index < KEncryptionHeader; ++Index)
                Check = Cipher.Decrypt(Source.Data[Index]);
            const auto Expected =
                static_cast<std::uint8_t>((Source.Flags & KDataDescriptor) != 0 ? Source.Time >> 8 : Source.Crc >> 24);
            if (Check != Expected)
                return Fail(Error, "The file block's key does not open '" + Source.Name + "'");
            Stored.reserve(Source.Data.size() - KEncryptionHeader);
            for (std::size_t Index = KEncryptionHeader; Index < Source.Data.size(); ++Index)
                Stored.push_back(Cipher.Decrypt(Source.Data[Index]));
        }
        else
        {
            Stored = Source.Data;
        }

        if (Source.Method == 0)
        {
            Output = std::move(Stored);
        }
        else if (Source.Method == MZ_DEFLATED)
        {
            Output.assign(Source.UncompressedSize, 0);
            const std::size_t Inflated = Output.empty() ? 0
                                                        : tinfl_decompress_mem_to_mem(Output.data(), Output.size(),
                                                                                      Stored.data(), Stored.size(), 0);
            if (Inflated != Output.size())
                return Fail(Error, "'" + Source.Name + "' is damaged");
        }
        else
        {
            return Fail(Error, "'" + Source.Name + "' uses ZIP compression method " + std::to_string(Source.Method) +
                                   ", which OpenDBC does not read");
        }
        if (Output.size() != Source.UncompressedSize || Crc32(Output) != Source.Crc)
            return Fail(Error, "'" + Source.Name + "' is damaged");
        return true;
    }

    bool FileBlockSystem::Read(int Id, const std::string& EntryName, std::vector<std::uint8_t>& Output,
                               std::string* Error) const
    {
        const Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        const Entry* Item = FindEntry(*Found, EntryName);
        if (!Item)
            return Fail(Error, "File block " + std::to_string(Id) + " has no file '" + EntryName + "'");
        return Decode(*Found, *Item, Output, Error);
    }

    bool FileBlockSystem::Extract(int Id, const std::string& EntryName, const std::string& Directory,
                                  std::string* Error)
    {
        const Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        const Entry* Item = FindEntry(*Found, EntryName);
        if (!Item)
            return Fail(Error, "File block " + std::to_string(Id) + " has no file '" + EntryName + "'");
        const std::filesystem::path Relative = PathFromUtf8(NormaliseEntryName(Item->Name));
        for (const std::filesystem::path& Part : Relative)
        {
            if (Part == "..")
                return Fail(Error, "'" + Item->Name + "' would be extracted outside its folder");
        }
        const std::filesystem::path Target = PathFromUtf8(Directory) / Relative;
        std::error_code FileError;
        if (!Item->Name.empty() && (Item->Name.back() == '/' || Item->Name.back() == '\\'))
        {
            std::filesystem::create_directories(Target, FileError);
            return !FileError || Fail(Error, "Cannot create '" + PathToUtf8(Target) + "'");
        }
        std::vector<std::uint8_t> Bytes;
        if (!Decode(*Found, *Item, Bytes, Error))
            return false;
        if (Target.has_parent_path())
            std::filesystem::create_directories(Target.parent_path(), FileError);
        if (FileError || !WriteWholeFile(Target, Bytes))
            return Fail(Error, "Cannot write '" + PathToUtf8(Target) + "'");
        return true;
    }

    bool FileBlockSystem::TemporaryDirectory(std::filesystem::path& Output, std::string* Error)
    {
        if (Temporary_.empty())
        {
            std::error_code FileError;
            const std::filesystem::path Base = std::filesystem::temp_directory_path(FileError);
            if (FileError)
                return Fail(Error, "File blocks need a temporary directory");
            std::random_device Random;
            const std::filesystem::path Candidate =
                Base / ("opendbc-fileblocks-" + std::to_string(Random()) + std::to_string(Random()));
            if (!std::filesystem::create_directories(Candidate, FileError) || FileError)
                return Fail(Error, "Cannot create a temporary folder for file blocks");
            Temporary_ = Candidate;
        }
        Output = Temporary_;
        return true;
    }

    bool FileBlockSystem::TemporaryPathFor(const std::string& EntryName, std::string& Path, std::string* Error)
    {
        std::filesystem::path Directory;
        if (!TemporaryDirectory(Directory, Error))
            return false;
        // A folder per file keeps its name, whose extension tells the loaders and savers its format.
        Directory /= std::to_string(++TemporaryCount_);
        std::error_code FileError;
        std::filesystem::create_directories(Directory, FileError);
        const std::filesystem::path Name = PathFromUtf8(NormaliseEntryName(EntryName)).filename();
        if (FileError || Name.empty())
            return Fail(Error, "Cannot prepare a temporary file for '" + EntryName + "'");
        Path = PathToUtf8(Directory / Name);
        return true;
    }

    bool FileBlockSystem::ExtractToTemporary(int Id, const std::string& EntryName, std::string& Path,
                                             std::string* Error)
    {
        std::vector<std::uint8_t> Bytes;
        if (!Read(Id, EntryName, Bytes, Error) || !TemporaryPathFor(EntryName, Path, Error))
            return false;
        if (!WriteWholeFile(PathFromUtf8(Path), Bytes))
            return Fail(Error, "Cannot write a temporary copy of '" + EntryName + "'");
        return true;
    }

    bool FileBlockSystem::Exists(int Id, bool& Output, std::string* Error) const
    {
        if (!ValidId(Id, Error))
            return false;
        Output = Blocks_.count(Id) != 0;
        return true;
    }

    bool FileBlockSystem::Count(int Id, int& Output, std::string* Error) const
    {
        const Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output = static_cast<int>(Found->Entries.size());
        return true;
    }

    bool FileBlockSystem::Names(int Id, std::vector<std::string>& Output, std::string* Error) const
    {
        const Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output.clear();
        for (const Entry& Item : Found->Entries)
            Output.push_back(Item.Name);
        return true;
    }

    bool FileBlockSystem::Contains(int Id, const std::string& EntryName, bool& Output, std::string* Error) const
    {
        const Block* Found = Find(Id, Error);
        if (!Found)
            return false;
        Output = FindEntry(*Found, EntryName) != nullptr;
        return true;
    }

    bool FileBlockSystem::Size(int Id, int& Output, std::string* Error)
    {
        Block* Found = Find(Id, Error);
        if (!Found || !Write(*Found, Error))
            return false;
        std::error_code FileError;
        const std::uintmax_t Bytes = std::filesystem::file_size(PathFromUtf8(Found->Path), FileError);
        if (FileError)
            return Fail(Error, "Cannot measure '" + Found->Path + "'");
        Output = static_cast<int>(std::min<std::uintmax_t>(Bytes, 0x7FFFFFFF));
        return true;
    }

    bool FileBlockSystem::Write(Block& Target, std::string* Error)
    {
        std::vector<std::uint8_t> Archive;
        std::vector<std::uint8_t> Directory;
        for (const Entry& Item : Target.Entries)
        {
            const bool HasDescriptor = (Item.Flags & KDataDescriptor) != 0;
            if (static_cast<std::uint64_t>(Archive.size()) + Item.Data.size() + Item.Name.size() + 30 +
                    (HasDescriptor ? 16 : 0) >
                0xFFFFFFFFu)
                return Fail(Error, "File block '" + Target.Path + "' would exceed 4 GB");
            const auto Offset = static_cast<std::uint32_t>(Archive.size());
            const auto Compressed = static_cast<std::uint32_t>(Item.Data.size());
            // Bit 3 also selects the encrypted header's check byte. Preserve it when copying stored bytes.
            const std::uint16_t Flags = Item.Flags;
            Put32(Archive, KLocalHeader);
            Put16(Archive, 20);
            Put16(Archive, Flags);
            Put16(Archive, Item.Method);
            Put16(Archive, Item.Time);
            Put16(Archive, Item.Date);
            Put32(Archive, HasDescriptor ? 0 : Item.Crc);
            Put32(Archive, HasDescriptor ? 0 : Compressed);
            Put32(Archive, HasDescriptor ? 0 : Item.UncompressedSize);
            Put16(Archive, static_cast<std::uint32_t>(Item.Name.size()));
            Put16(Archive, 0);
            Archive.insert(Archive.end(), Item.Name.begin(), Item.Name.end());
            Archive.insert(Archive.end(), Item.Data.begin(), Item.Data.end());
            if (HasDescriptor)
            {
                Put32(Archive, 0x08074B50);
                Put32(Archive, Item.Crc);
                Put32(Archive, Compressed);
                Put32(Archive, Item.UncompressedSize);
            }

            Put32(Directory, KCentralHeader);
            Put16(Directory, 20);
            Put16(Directory, 20);
            Put16(Directory, Flags);
            Put16(Directory, Item.Method);
            Put16(Directory, Item.Time);
            Put16(Directory, Item.Date);
            Put32(Directory, Item.Crc);
            Put32(Directory, Compressed);
            Put32(Directory, Item.UncompressedSize);
            Put16(Directory, static_cast<std::uint32_t>(Item.Name.size()));
            Put32(Directory, 0); // extra and comment lengths
            Put32(Directory, 0); // disk and internal attributes
            Put32(Directory, 0); // external attributes
            Put32(Directory, Offset);
            Directory.insert(Directory.end(), Item.Name.begin(), Item.Name.end());
        }
        const auto DirectoryOffset = static_cast<std::uint32_t>(Archive.size());
        Archive.insert(Archive.end(), Directory.begin(), Directory.end());
        Put32(Archive, KEndOfDirectory);
        Put32(Archive, 0); // disk numbers
        Put16(Archive, static_cast<std::uint32_t>(Target.Entries.size()));
        Put16(Archive, static_cast<std::uint32_t>(Target.Entries.size()));
        Put32(Archive, static_cast<std::uint32_t>(Directory.size()));
        Put32(Archive, DirectoryOffset);
        Put16(Archive, 0);

        // A new file replaces the old one only once complete.
        const std::filesystem::path Path = PathFromUtf8(Target.Path);
        std::filesystem::path Partial = Path;
        Partial += ".opendbc-partial";
        std::error_code FileError;
        if (!WriteWholeFile(Partial, Archive))
        {
            std::filesystem::remove(Partial, FileError);
            return Fail(Error, "SAVE FILE BLOCK cannot write '" + Target.Path + "'");
        }
        std::filesystem::rename(Partial, Path, FileError);
        if (FileError)
        {
            std::filesystem::remove(Partial, FileError);
            return Fail(Error, "SAVE FILE BLOCK cannot replace '" + Target.Path + "'");
        }
        Target.Modified = false;
        return true;
    }

} // namespace dbc
