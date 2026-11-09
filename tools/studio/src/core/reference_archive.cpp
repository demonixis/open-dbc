#include "reference_archive.hpp"

#include "interpreter/atomic_file.hpp"

#include <miniz.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <utility>

namespace dbc::studio::core
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::string_view KMagic{"DBCREF1\0", 8};
        constexpr std::string_view KIndex = "index.html";
        constexpr std::string_view KStyles = "styles.css";
        constexpr std::string_view KSearch = "search.json";
        constexpr std::size_t KMaxSize = 64 * 1024 * 1024;

        std::uint32_t TakeU32(std::string_view& Data)
        {
            std::uint32_t Value = 0;
            for (unsigned Shift = 0; Shift < 32; Shift += 8)
            {
                Value |= static_cast<std::uint32_t>(static_cast<unsigned char>(Data.front())) << Shift;
                Data.remove_prefix(1);
            }
            return Value;
        }

        bool IsEntryName(std::string_view Name)
        {
            if (Name == KStyles || Name == KSearch)
                return true;
            constexpr std::string_view KSuffix = ".html";
            if (!Name.ends_with(KSuffix) || Name.size() == KSuffix.size())
                return false;
            Name.remove_suffix(KSuffix.size());
            // These names are devices on Windows even with a file extension.
            if (Name == "con" || Name == "prn" || Name == "aux" || Name == "nul" ||
                (Name.size() == 4 && (Name.starts_with("com") || Name.starts_with("lpt")) && Name.back() >= '0' &&
                 Name.back() <= '9'))
                return false;
            return std::ranges::all_of(
                Name, [](char Value)
                { return (Value >= 'a' && Value <= 'z') || (Value >= '0' && Value <= '9') || Value == '-'; });
        }

        bool EnsureDirectory(const fs::path& Directory)
        {
            std::error_code Error;
            fs::create_directories(Directory, Error);
            return !Error && fs::is_directory(fs::symlink_status(Directory, Error)) && !Error;
        }

        bool FileMatches(const fs::path& File, std::string_view Expected)
        {
            std::ifstream Input(File, std::ios::binary | std::ios::ate);
            if (!Input || Input.tellg() != static_cast<std::streamoff>(Expected.size()))
                return false;
            std::string Contents(Expected.size(), '\0');
            Input.seekg(0);
            return Input.read(Contents.data(), static_cast<std::streamsize>(Contents.size())) && Contents == Expected;
        }
    } // namespace

    bool ReferenceArchive::Open(std::string_view Data)
    {
        Data_ = {};
        Entries_.clear();
        if (Data.size() < KMagic.size() + 4 || Data.size() > KMaxSize || !Data.starts_with(KMagic))
            return false;

        std::string_view Rest = Data.substr(KMagic.size());
        const std::uint32_t Count = TakeU32(Rest);
        if (Count > 128)
            return false;

        std::vector<Entry> Entries;
        Entries.reserve(Count);
        std::size_t TotalSize = 0;
        for (std::uint32_t Index = 0; Index < Count; ++Index)
        {
            if (Rest.size() < 12)
                return false;
            const std::uint32_t NameSize = TakeU32(Rest);
            const std::uint32_t Size = TakeU32(Rest);
            const std::uint32_t CompressedSize = TakeU32(Rest);
            if (NameSize > 64 || NameSize > Rest.size() || CompressedSize > Rest.size() - NameSize || Size == 0 ||
                Size > KMaxSize - TotalSize || CompressedSize == 0)
                return false;
            const std::string_view Name = Rest.substr(0, NameSize);
            if (!IsEntryName(Name) || std::ranges::find(Entries, Name, &Entry::Name) != Entries.end())
                return false;
            Entries.push_back({Name, Rest.substr(NameSize, CompressedSize), Size});
            Rest.remove_prefix(static_cast<std::size_t>(NameSize) + CompressedSize);
            TotalSize += Size;
        }

        if (!Rest.empty() || std::ranges::find(Entries, KIndex, &Entry::Name) == Entries.end() ||
            std::ranges::find(Entries, KStyles, &Entry::Name) == Entries.end() ||
            std::ranges::find(Entries, KSearch, &Entry::Name) == Entries.end())
            return false;
        Entries_ = std::move(Entries);
        Data_ = Data;
        return true;
    }

    bool ReferenceArchive::IsOpen() const
    {
        return !Data_.empty();
    }

    const ReferenceArchive::Entry* ReferenceArchive::Find(std::string_view Name) const
    {
        const auto Found = std::ranges::find(Entries_, Name, &Entry::Name);
        return Found == Entries_.end() ? nullptr : &*Found;
    }

    std::optional<std::string> ReferenceArchive::Expand(const Entry& Item)
    {
        std::string Content(Item.Size, '\0');
        mz_ulong OutputSize = static_cast<mz_ulong>(Item.Size);
        mz_ulong InputSize = static_cast<mz_ulong>(Item.Compressed.size());
        const auto* Input = reinterpret_cast<const unsigned char*>(Item.Compressed.data());
        auto* Output = reinterpret_cast<unsigned char*>(Content.data());
        if (mz_uncompress2(Output, &OutputSize, Input, &InputSize) != MZ_OK || OutputSize != Item.Size ||
            InputSize != Item.Compressed.size())
            return std::nullopt;
        return Content;
    }

    std::optional<std::string> ReferenceArchive::Read(std::string_view Name) const
    {
        const Entry* Item = Find(Name);
        return Item ? Expand(*Item) : std::nullopt;
    }

    std::optional<std::filesystem::path> ReferenceArchive::Materialize(const fs::path& CacheDirectory) const
    {
        if (!IsOpen() || !CacheDirectory.is_absolute() || !EnsureDirectory(CacheDirectory))
            return std::nullopt;

        // A browser tab from a different Studio build can keep using its own local files.
        const auto Hash = mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(Data_.data()), Data_.size());
        const fs::path Directory = CacheDirectory / (std::to_string(Hash) + "-" + std::to_string(Data_.size()));
        if (!EnsureDirectory(Directory))
            return std::nullopt;

        for (const Entry& Item : Entries_)
        {
            if (Item.Name == KSearch)
                continue;
            const fs::path Destination = Directory / Item.Name;
            std::error_code Error;
            const fs::file_status Status = fs::symlink_status(Destination, Error);
            const bool Exists = fs::is_regular_file(Status);
            if (Status.type() != fs::file_type::not_found && (Error || !Exists))
                return std::nullopt;

            const auto Contents = Expand(Item);
            if (!Contents)
                return std::nullopt;
            if (Exists && FileMatches(Destination, *Contents))
                continue;
            if (!dbc::interpreter::AtomicWriteText(Destination, *Contents) && !FileMatches(Destination, *Contents))
                return std::nullopt;
        }
        return Directory / KIndex;
    }
} // namespace dbc::studio::core
