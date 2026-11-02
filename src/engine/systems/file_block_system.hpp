#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbc
{
    // The Enhancement Pack's file blocks: ZIP archives whose entries may be encrypted with a key (traditional PKWARE
    // encryption, as its ZIP library wrote). Changes stay in memory until SAVE FILE BLOCK writes the archive.
    class FileBlockSystem
    {
      public:
        FileBlockSystem();
        ~FileBlockSystem();

        FileBlockSystem(const FileBlockSystem&) = delete;
        FileBlockSystem& operator=(const FileBlockSystem&) = delete;

        bool Create(int Id, const std::string& Path, std::string* Error = nullptr);
        bool Open(int Id, const std::string& Path, const std::string& Key, std::string* Error = nullptr);
        // Close writes unsaved changes first; Delete drops them.
        bool Close(int Id, std::string* Error = nullptr);
        bool Delete(int Id, std::string* Error = nullptr);
        bool Save(int Id, std::string* Error = nullptr);

        // The key encrypts entries added afterwards and decrypts encrypted entries.
        bool SetKey(int Id, const std::string& Key, std::string* Error = nullptr);
        bool SetCompression(int Id, int Level, std::string* Error = nullptr);
        bool Compression(int Id, int& Output, std::string* Error = nullptr) const;

        // Entry names are the given path with forward slashes and without leading "./" or a root, and match
        // without regard to case. Adding an entry replaces one of the same name.
        bool AddFile(int Id, const std::string& EntryName, const std::string& SourcePath, std::string* Error = nullptr);
        bool AddBytes(int Id, const std::string& EntryName, const std::vector<std::uint8_t>& Bytes,
                      std::string* Error = nullptr);
        bool Remove(int Id, const std::string& EntryName, std::string* Error = nullptr);
        bool Read(int Id, const std::string& EntryName, std::vector<std::uint8_t>& Output,
                  std::string* Error = nullptr) const;
        // Writes the entry under Directory, recreating its folders.
        bool Extract(int Id, const std::string& EntryName, const std::string& Directory, std::string* Error = nullptr);
        // Writes the entry to a private temporary file, kept until shutdown, for the loaders that read files.
        bool ExtractToTemporary(int Id, const std::string& EntryName, std::string& Path, std::string* Error = nullptr);
        // A private temporary path ending in the entry's file name, for the savers that write files.
        bool TemporaryPathFor(const std::string& EntryName, std::string& Path, std::string* Error = nullptr);

        bool Exists(int Id, bool& Output, std::string* Error = nullptr) const;
        bool Count(int Id, int& Output, std::string* Error = nullptr) const;
        bool Names(int Id, std::vector<std::string>& Output, std::string* Error = nullptr) const;
        bool Contains(int Id, const std::string& EntryName, bool& Output, std::string* Error = nullptr) const;
        // Saves the block, then measures its file.
        bool Size(int Id, int& Output, std::string* Error = nullptr);

        static std::string NormaliseEntryName(const std::string& Name);

      private:
        struct Entry
        {
            std::string Name;
            std::uint16_t Flags = 0;
            std::uint16_t Method = 0;
            std::uint16_t Time = 0;
            std::uint16_t Date = 0;
            std::uint32_t Crc = 0;
            std::uint32_t UncompressedSize = 0;
            // As stored in the archive, with the encryption header when encrypted.
            std::vector<std::uint8_t> Data;
        };

        struct Block
        {
            std::string Path;
            std::string Key;
            bool Keyed = false;
            int Compression = 9;
            bool Modified = false;
            std::vector<Entry> Entries;
        };

        Block* Find(int Id, std::string* Error);
        const Block* Find(int Id, std::string* Error) const;
        static const Entry* FindEntry(const Block& Owner, const std::string& EntryName);
        static bool Decode(const Block& Owner, const Entry& Source, std::vector<std::uint8_t>& Output,
                           std::string* Error);
        bool Write(Block& Target, std::string* Error);
        bool TemporaryDirectory(std::filesystem::path& Output, std::string* Error);

        std::unordered_map<int, Block> Blocks_;
        std::filesystem::path Temporary_;
        std::uint64_t TemporaryCount_ = 0;
    };

} // namespace dbc
