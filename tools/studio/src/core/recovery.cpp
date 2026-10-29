#include "recovery.hpp"

#include "interpreter/atomic_file.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

namespace dbc::studio::core
{
    namespace
    {
        std::string Utf8(const std::u8string& Text)
        {
            return std::string(Text.begin(), Text.end());
        }

        std::filesystem::path PathFromUtf8(const std::string& Text)
        {
            return std::filesystem::path(std::u8string(Text.begin(), Text.end()));
        }

        // A stable name per tab: the same file keeps its copy across refreshes.
        std::string CopyName(const UnsavedText& Value)
        {
            const std::string Key =
                Value.Path.empty() ? "untitled:" + Value.Title : "file:" + Utf8(Value.Path.generic_u8string());
            std::uint64_t Hash = 0xcbf29ce484222325ull;
            for (const char Character : Key)
            {
                Hash ^= static_cast<unsigned char>(Character);
                Hash *= 0x100000001b3ull;
            }
            char Name[32];
            std::snprintf(Name, sizeof(Name), "%016llx.json", static_cast<unsigned long long>(Hash));
            return Name;
        }
    } // namespace

    bool Recovery::Update(const std::vector<UnsavedText>& Texts, std::string& Error)
    {
        if (Directory_.empty())
            return true;
        bool Success = true;
        std::set<std::string> Current;
        for (const UnsavedText& Value : Texts)
        {
            const std::string Name = CopyName(Value);
            Current.insert(Name);
            const std::size_t Hash = std::hash<std::string>{}(Value.Text);
            if (const auto Found = Written_.find(Name); Found != Written_.end() && Found->second == Hash)
                continue;
            nlohmann::json Json;
            Json["path"] = Utf8(Value.Path.u8string());
            Json["title"] = Value.Title;
            Json["text"] = Value.Text;
            // Replacing invalid UTF-8 instead of throwing; the editor only holds valid UTF-8.
            const std::string Bytes = Json.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + '\n';
            if (!dbc::interpreter::AtomicWriteText(Directory_ / Name, Bytes, &Error))
            {
                Success = false;
                continue;
            }
            Written_[Name] = Hash;
        }
        for (auto It = Written_.begin(); It != Written_.end();)
        {
            if (Current.contains(It->first))
            {
                ++It;
                continue;
            }
            std::error_code RemoveError;
            std::filesystem::remove(Directory_ / It->first, RemoveError);
            It = Written_.erase(It);
        }
        return Success;
    }

    void Recovery::Clear()
    {
        std::string Ignored;
        Update({}, Ignored);
    }

    std::vector<RecoveryCopy> Recovery::Pending() const
    {
        std::vector<RecoveryCopy> Result;
        std::error_code FileError;
        if (Directory_.empty() || !std::filesystem::is_directory(Directory_, FileError))
            return Result;
        for (const auto& Entry : std::filesystem::directory_iterator(Directory_, FileError))
        {
            const std::filesystem::path& File = Entry.path();
            if (File.extension() != ".json" || Written_.contains(File.filename().string()))
                continue;
            std::ifstream Input(File, std::ios::binary);
            const nlohmann::json Json =
                nlohmann::json::parse(std::istreambuf_iterator<char>(Input), {}, nullptr, false);
            if (Json.is_discarded() || !Json.is_object() || !Json.contains("text") || !Json["text"].is_string())
                continue;
            RecoveryCopy Copy;
            Copy.File = File;
            Copy.Content.Text = Json["text"].get<std::string>();
            if (const auto Path = Json.find("path"); Path != Json.end() && Path->is_string())
                Copy.Content.Path = PathFromUtf8(Path->get<std::string>());
            if (const auto Title = Json.find("title"); Title != Json.end() && Title->is_string())
                Copy.Content.Title = Title->get<std::string>();
            Result.push_back(std::move(Copy));
        }
        return Result;
    }

    void Recovery::Remove(const std::filesystem::path& File)
    {
        std::error_code RemoveError;
        std::filesystem::remove(File, RemoveError);
        Written_.erase(File.filename().string());
    }
} // namespace dbc::studio::core
