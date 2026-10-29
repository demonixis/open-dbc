#include "registry_file.hpp"
#include "atomic_file.hpp"
#include "path_utf8.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <memory>

namespace dbc::interpreter
{
    namespace
    {
        bool EqualsIgnoringCase(std::string_view Left, std::string_view Right)
        {
            if (Left.size() != Right.size())
                return false;
            for (std::size_t Index = 0; Index < Left.size(); ++Index)
            {
                if (SDL_toupper(static_cast<unsigned char>(Left[Index])) !=
                    SDL_toupper(static_cast<unsigned char>(Right[Index])))
                    return false;
            }
            return true;
        }

        nlohmann::json::iterator FindIgnoringCase(nlohmann::json& Object, std::string_view Name)
        {
            for (auto Item = Object.begin(); Item != Object.end(); ++Item)
            {
                if (EqualsIgnoringCase(Item.key(), Name))
                    return Item;
            }
            return Object.end();
        }

        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

        bool LoadRegistry(const std::filesystem::path& File, nlohmann::json& Registry, std::string* Error)
        {
            if (File.empty())
                return Fail(Error, "The registry file has no location in the user's storage");
            std::error_code StatusError;
            if (!std::filesystem::exists(File, StatusError))
            {
                if (StatusError)
                    return Fail(Error, "Cannot inspect registry file '" + dbc::PathToUtf8(File) +
                                           "': " + StatusError.message());
                Registry = nlohmann::json::object();
                return true;
            }
            std::ifstream Input(File, std::ios::binary);
            if (!Input)
                return Fail(Error, "Cannot open registry file '" + dbc::PathToUtf8(File) + "'");
            const std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
            Registry = nlohmann::json::parse(Text, nullptr, false);
            bool Valid = Registry.is_object();
            for (auto Folder = Registry.begin(); Valid && Folder != Registry.end(); ++Folder)
            {
                Valid = Folder->is_object();
                for (auto Key = Folder->begin(); Valid && Key != Folder->end(); ++Key)
                {
                    Valid = Key->is_number_integer() &&
                            Key->get<std::int64_t>() >= std::numeric_limits<std::int32_t>::min() &&
                            Key->get<std::int64_t>() <= std::numeric_limits<std::int32_t>::max();
                }
            }
            if (!Valid)
                return Fail(Error, "Registry file '" + dbc::PathToUtf8(File) +
                                       "' must hold an object of folders mapping key names to 32-bit integers");
            return true;
        }
    } // namespace

    std::filesystem::path DefaultRegistryPath()
    {
        char* Raw = SDL_GetPrefPath("OpenDBC", "OpenDBC");
        if (Raw == nullptr)
            return {};
        const std::unique_ptr<char, decltype(&SDL_free)> Owner(Raw, SDL_free);
        return dbc::PathFromUtf8(Raw) / "registry.json";
    }

    bool ReadRegistryValue(const std::filesystem::path& File, std::string_view Folder, std::string_view Key,
                           std::int32_t& Value, std::string* Error)
    {
        nlohmann::json Registry;
        if (!LoadRegistry(File, Registry, Error))
            return false;
        Value = 0;
        const auto FolderEntry = FindIgnoringCase(Registry, Folder);
        if (FolderEntry == Registry.end())
            return true;
        const auto KeyEntry = FindIgnoringCase(*FolderEntry, Key);
        if (KeyEntry != FolderEntry->end())
            Value = KeyEntry->get<std::int32_t>();
        return true;
    }

    bool WriteRegistryValue(const std::filesystem::path& File, std::string_view Folder, std::string_view Key,
                            std::int32_t Value, std::string* Error)
    {
        nlohmann::json Registry;
        if (!LoadRegistry(File, Registry, Error))
            return false;
        // An existing folder or key keeps its first spelling, as the registry does.
        auto FolderEntry = FindIgnoringCase(Registry, Folder);
        if (FolderEntry == Registry.end())
            FolderEntry = Registry.emplace(std::string(Folder), nlohmann::json::object()).first;
        const auto KeyEntry = FindIgnoringCase(*FolderEntry, Key);
        if (KeyEntry == FolderEntry->end())
            (*FolderEntry)[std::string(Key)] = Value;
        else
            *KeyEntry = Value;
        return AtomicWriteText(File, Registry.dump(2) + "\n", Error);
    }
} // namespace dbc::interpreter
