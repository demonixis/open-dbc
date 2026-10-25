#include "artifact_runtime.hpp"
#include "interpreter/atomic_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

namespace dbc::studio
{
    namespace
    {
        constexpr std::string_view ManifestName = "opendbc-artifact.json";

        bool SafeRelativePath(const std::filesystem::path& Value)
        {
            if (Value.empty() || Value.is_absolute() || Value.has_root_path())
                return false;
            for (const std::filesystem::path& Part : Value)
            {
                if (Part == ".." || Part == ".")
                    return false;
            }
            return true;
        }

        std::optional<std::string> LoadBytes(const std::filesystem::path& Path)
        {
            std::size_t Size = 0;
            void* Bytes = SDL_LoadFile(Path.string().c_str(), &Size);
            if (Bytes == nullptr)
                return std::nullopt;
            const std::unique_ptr<void, decltype(&SDL_free)> Owner(Bytes, SDL_free);
            return std::string(static_cast<const char*>(Bytes), Size);
        }

        std::string Lower(std::string Value)
        {
            std::transform(Value.begin(), Value.end(), Value.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Value;
        }

        bool ValidContentHash(std::string_view Value)
        {
            return Value.size() == 16 &&
                   std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
                               { return std::isdigit(Character) || (Character >= 'a' && Character <= 'f'); });
        }

        bool PublishTemporary(const std::filesystem::path& Temporary, const std::filesystem::path& Destination,
                              std::string& Error)
        {
#if defined(_WIN32)
            if (!MoveFileExW(Temporary.wstring().c_str(), Destination.wstring().c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
            if (::rename(Temporary.c_str(), Destination.c_str()) != 0)
#endif
            {
                std::error_code Ignored;
                std::filesystem::remove(Temporary, Ignored);
                Error = "Cannot publish embedded artifact file: " + Destination.string();
                return false;
            }
            return true;
        }

        bool WriteAtomic(const std::filesystem::path& Destination, std::string_view Bytes, std::string& Error)
        {
            std::filesystem::path Temporary;
            std::FILE* Output = nullptr;
            if (!dbc::interpreter::CreateExclusiveTemporary(Destination, Temporary, Output, &Error))
                return false;
            const bool Written =
                std::fwrite(Bytes.data(), 1, Bytes.size(), Output) == Bytes.size() && std::fflush(Output) == 0;
            const bool Closed = std::fclose(Output) == 0;
            if (!Written || !Closed)
            {
                std::error_code Ignored;
                std::filesystem::remove(Temporary, Ignored);
                Error = "Cannot finish embedded artifact file: " + Destination.string();
                return false;
            }
            return PublishTemporary(Temporary, Destination, Error);
        }

        bool CopyAtomic(const std::filesystem::path& Source, const std::filesystem::path& Destination,
                        std::string& Error)
        {
            SDL_IOStream* Input = SDL_IOFromFile(Source.string().c_str(), "rb");
            if (Input == nullptr)
            {
                Error = "Cannot open embedded artifact file: " + Source.string();
                return false;
            }
            const std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> InputOwner(Input, SDL_CloseIO);
            std::filesystem::path Temporary;
            std::FILE* Output = nullptr;
            if (!dbc::interpreter::CreateExclusiveTemporary(Destination, Temporary, Output, &Error))
                return false;
            std::array<char, 64 * 1024> Buffer{};
            bool WriteOk = true;
            while (WriteOk)
            {
                const std::size_t Count = SDL_ReadIO(Input, Buffer.data(), Buffer.size());
                if (Count == 0)
                    break;
                WriteOk = std::fwrite(Buffer.data(), 1, Count, Output) == Count;
            }
            const bool ReadOk = SDL_GetIOStatus(Input) == SDL_IO_STATUS_EOF;
            const bool Flushed = std::fflush(Output) == 0;
            const bool Closed = std::fclose(Output) == 0;
            if (!ReadOk || !WriteOk || !Flushed || !Closed)
            {
                std::error_code Ignored;
                std::filesystem::remove(Temporary, Ignored);
                Error = "Cannot copy embedded artifact file: " + Source.string();
                return false;
            }
            return PublishTemporary(Temporary, Destination, Error);
        }

        bool EnsurePayloadDirectory(const std::filesystem::path& Root, const std::filesystem::path& Relative,
                                    std::string& Error)
        {
            std::filesystem::path Current = Root;
            std::vector<std::filesystem::path> Components = {std::filesystem::path{}};
            for (const std::filesystem::path& Part : Relative)
                Components.push_back(Part);
            for (const std::filesystem::path& Part : Components)
            {
                Current /= Part;
                std::error_code FileError;
                const std::filesystem::file_status Status = std::filesystem::symlink_status(Current, FileError);
                if (!FileError && std::filesystem::exists(Status))
                {
                    if (std::filesystem::is_symlink(Status) || !std::filesystem::is_directory(Status))
                    {
                        Error = "Writable artifact payload contains an unsafe directory: " + Current.string();
                        return false;
                    }
                    continue;
                }
                FileError.clear();
                if (!std::filesystem::create_directory(Current, FileError) && FileError)
                {
                    Error = "Cannot create an embedded artifact directory: " + FileError.message();
                    return false;
                }
            }
            return true;
        }
    } // namespace

    namespace toolchain
    {
        std::string detail::ArtifactContentHash(std::string_view Bytes)
        {
            std::uint64_t Hash = 14695981039346656037ull;
            for (const unsigned char Byte : Bytes)
            {
                Hash ^= Byte;
                Hash *= 1099511628211ull;
            }
            std::ostringstream Text;
            Text << std::hex << std::setfill('0') << std::setw(16) << Hash;
            return Text.str();
        }

        std::optional<std::string> detail::ArtifactContentHashFile(const std::filesystem::path& Path)
        {
            SDL_IOStream* Input = SDL_IOFromFile(Path.string().c_str(), "rb");
            if (Input == nullptr)
                return std::nullopt;
            const std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> InputOwner(Input, SDL_CloseIO);
            std::uint64_t Hash = 14695981039346656037ull;
            std::array<unsigned char, 64 * 1024> Buffer{};
            while (true)
            {
                const std::size_t Count = SDL_ReadIO(Input, Buffer.data(), Buffer.size());
                for (std::size_t Index = 0; Index < Count; ++Index)
                {
                    Hash ^= Buffer[Index];
                    Hash *= 1099511628211ull;
                }
                if (Count == 0)
                    break;
            }
            if (SDL_GetIOStatus(Input) != SDL_IO_STATUS_EOF)
                return std::nullopt;
            std::ostringstream Text;
            Text << std::hex << std::setfill('0') << std::setw(16) << Hash;
            return Text.str();
        }

        bool detail::SynchronizeArtifactPayload(const std::filesystem::path& SourceRoot,
                                                const std::filesystem::path& DestinationRoot,
                                                const std::vector<ArtifactFile>& Files, std::string& Error)
        {
            if (!EnsurePayloadDirectory(DestinationRoot, {}, Error))
                return false;
            const std::filesystem::path StatePath = DestinationRoot / ".opendbc-payload-state.json";
            std::map<std::string, std::string> Previous;
            if (const std::optional<std::string> StateText = LoadBytes(StatePath))
            {
                const nlohmann::json State = nlohmann::json::parse(*StateText, nullptr, false);
                if (State.is_discarded() || !State.is_object() ||
                    (!State.contains("format") || !State["format"].is_number_integer() || State["format"] != 1) ||
                    !State.contains("files") || !State["files"].is_object())
                {
                    Error = "The writable artifact payload state is invalid";
                    return false;
                }
                for (auto It = State["files"].begin(); It != State["files"].end(); ++It)
                {
                    if (!It.value().is_string() || !SafeRelativePath(It.key()) ||
                        !ValidContentHash(It.value().get_ref<const std::string&>()))
                    {
                        Error = "The writable artifact payload state has an invalid file entry";
                        return false;
                    }
                    Previous.emplace(It.key(), It.value().get<std::string>());
                }
            }

            std::map<std::string, std::string> Current;
            for (const ArtifactFile& File : Files)
                Current.emplace(File.RelativePath.generic_string(), File.ContentHash);

            std::error_code FileError;
            for (const auto& [Path, OldHash] : Previous)
            {
                if (Current.contains(Path))
                    continue;
                const std::filesystem::path Destination = DestinationRoot / Path;
                if (!EnsurePayloadDirectory(DestinationRoot, std::filesystem::path(Path).parent_path(), Error))
                    return false;
                const std::optional<std::string> ExistingHash = ArtifactContentHashFile(Destination);
                if (ExistingHash && *ExistingHash == OldHash)
                {
                    FileError.clear();
                    if (!std::filesystem::remove(Destination, FileError) || FileError)
                    {
                        Error = "Cannot remove an obsolete embedded artifact file: " + Path;
                        return false;
                    }
                }
            }

            for (const ArtifactFile& File : Files)
            {
                const std::string Key = File.RelativePath.generic_string();
                const std::filesystem::path Source = SourceRoot / File.RelativePath;
                const std::optional<std::string> SourceHash = ArtifactContentHashFile(Source);
                if (!SourceHash)
                {
                    Error = "Embedded artifact file is missing: " + Key;
                    return false;
                }
                if (*SourceHash != File.ContentHash)
                {
                    Error = "Embedded artifact file does not match its manifest: " + Key;
                    return false;
                }
                const std::filesystem::path Destination = DestinationRoot / File.RelativePath;
                if (!EnsurePayloadDirectory(DestinationRoot, File.RelativePath.parent_path(), Error))
                    return false;
                const std::filesystem::file_status Status = std::filesystem::symlink_status(Destination, FileError);
                if (!FileError && std::filesystem::is_symlink(Status))
                {
                    Error = "Writable artifact payload contains a symlink: " + Key;
                    return false;
                }
                FileError.clear();
                const std::optional<std::string> ExistingHash = ArtifactContentHashFile(Destination);
                const auto Old = Previous.find(Key);
                const bool MayReplace = !ExistingHash || (Old != Previous.end() && *ExistingHash == Old->second);
                if (MayReplace && (!ExistingHash || *ExistingHash != File.ContentHash) &&
                    !CopyAtomic(Source, Destination, Error))
                    return false;
            }

            nlohmann::json State;
            State["format"] = 1;
            State["files"] = Current;
            const std::string StateBytes = State.dump(2) + '\n';
            return WriteAtomic(StatePath, StateBytes, Error);
        }

        EmbeddedArtifactResult FindEmbeddedArtifact(const std::filesystem::path& ResourceRoot)
        {
            std::filesystem::path ManifestPath = ResourceRoot / ManifestName;
            std::optional<std::string> Text = LoadBytes(ManifestPath);
#if defined(__ANDROID__)
            if (!Text)
            {
                ManifestPath = ManifestName;
                Text = LoadBytes(ManifestPath);
            }
#endif
            if (!Text)
                return {};

            const nlohmann::json Manifest = nlohmann::json::parse(*Text, nullptr, false);
            if (Manifest.is_discarded() || !Manifest.is_object())
                return {{}, "The embedded OpenDBC artifact manifest is not valid JSON"};
            if (!Manifest.contains("format") || !Manifest["format"].is_number_integer() ||
                (Manifest["format"] != 4 && Manifest["format"] != 5))
                return {{}, "The embedded OpenDBC artifact manifest has an unsupported format"};
            const auto EntryValue = Manifest.find("entry");
            if (EntryValue == Manifest.end() || !EntryValue->is_string())
                return {{}, "The embedded OpenDBC artifact manifest has no BASIC entry"};

            const std::filesystem::path RelativeEntry = EntryValue->get<std::string>();
            if (!SafeRelativePath(RelativeEntry))
                return {{}, "The embedded OpenDBC artifact entry is not a safe relative path"};
            const std::string Extension = Lower(RelativeEntry.extension().string());
            if (Extension != ".dba")
                return {{}, "The embedded OpenDBC artifact entry must end in .dba"};
            const auto DialectValue = Manifest.find("dialect");
            if (DialectValue == Manifest.end() || !DialectValue->is_string())
                return {{}, "The embedded OpenDBC artifact manifest has no dialect"};
            const std::optional<dbc::language::LanguageProfile> Profile =
                dbc::language::ParseProfile(DialectValue->get<std::string>());
            if (!Profile)
                return {{}, "The embedded OpenDBC artifact dialect must be dbc or dbpro"};
            std::filesystem::path RelativeAssetRoot;
            if (const auto AssetValue = Manifest.find("assetRoot"); AssetValue != Manifest.end())
            {
                if (!AssetValue->is_string())
                    return {{}, "The embedded OpenDBC artifact asset root is invalid"};
                RelativeAssetRoot = AssetValue->get<std::string>();
                if (!RelativeAssetRoot.empty() && !SafeRelativePath(RelativeAssetRoot))
                    return {{}, "The embedded OpenDBC artifact asset root is not a safe relative path"};
            }

            const auto StorageValue = Manifest.find("storage");
            if (StorageValue == Manifest.end() || !StorageValue->is_string())
                return {{}, "The embedded OpenDBC artifact manifest has no storage identity"};
            const std::string Storage = StorageValue->get<std::string>();
            if (Storage.empty() || Storage.size() > 64 ||
                !std::all_of(Storage.begin(), Storage.end(), [](unsigned char Character)
                             { return std::isalnum(Character) || Character == '-' || Character == '_'; }))
                return {{}, "The embedded OpenDBC artifact storage identity is invalid"};

            const auto FileValues = Manifest.find("files");
            if (FileValues == Manifest.end() || !FileValues->is_array() || FileValues->empty())
                return {{}, "The embedded OpenDBC artifact manifest has no payload inventory"};
            std::vector<detail::ArtifactFile> Files;
            std::set<std::string> FileKeys;
            bool EntryPresent = false;
            Files.reserve(FileValues->size());
            for (const nlohmann::json& Value : *FileValues)
            {
                if (!Value.is_object() || !Value.contains("path") || !Value["path"].is_string() ||
                    !Value.contains("hash") || !Value["hash"].is_string())
                    return {{}, "The embedded OpenDBC artifact payload inventory is invalid"};
                const std::filesystem::path Relative = Value["path"].get<std::string>();
                const std::string Hash = Value["hash"].get<std::string>();
                if (!SafeRelativePath(Relative))
                    return {{}, "The embedded OpenDBC artifact payload contains an unsafe path"};
                if (!ValidContentHash(Hash))
                    return {{}, "The embedded OpenDBC artifact payload contains an invalid content hash"};
                const std::string Key = Relative.generic_string();
                if (!FileKeys.insert(Key).second)
                    return {{}, "The embedded OpenDBC artifact payload contains duplicate paths"};
                EntryPresent = EntryPresent || Relative == RelativeEntry;
                Files.push_back({Relative, Hash});
            }
            if (!EntryPresent)
                return {{}, "The embedded OpenDBC artifact payload does not contain its BASIC entry"};

            std::vector<std::filesystem::path> RelativeSources;
            if (Manifest["format"] == 4)
                RelativeSources.push_back(RelativeEntry);
            else
            {
                const auto Sources = Manifest.find("orderedSources");
                if (Sources == Manifest.end() || !Sources->is_array() || Sources->empty())
                    return {{}, "The embedded OpenDBC artifact has no ordered sources"};
                std::set<std::string> SourceKeys;
                for (const auto& Value : *Sources)
                {
                    if (!Value.is_string())
                        return {{}, "The embedded OpenDBC artifact source path is invalid"};
                    const std::filesystem::path Source = Value.get<std::string>();
                    const std::string Key = Source.generic_string();
                    if (!SafeRelativePath(Source) || Lower(Source.extension().string()) != ".dba" ||
                        !FileKeys.contains(Key) || !SourceKeys.insert(Key).second)
                        return {{}, "The embedded OpenDBC artifact sources must be unique inventoried BASIC files"};
                    RelativeSources.push_back(Source);
                }
                if (!SourceKeys.contains(RelativeEntry.generic_string()))
                    return {{}, "The embedded OpenDBC artifact sources do not contain the entry"};
            }

#if defined(__ANDROID__)
            const std::filesystem::path SourcePayloadRoot = "artifact";
#else
            const std::filesystem::path SourcePayloadRoot = ResourceRoot / "artifact";
#endif
#if defined(OPENDBC_TESTING)
            const char* TestDataRoot = SDL_getenv("OPENDBC_TEST_ARTIFACT_DATA_ROOT");
            const std::filesystem::path PayloadRoot =
                TestDataRoot != nullptr ? std::filesystem::path(TestDataRoot) / Storage : std::filesystem::path{};
            if (TestDataRoot == nullptr)
                return {{}, "OPENDBC_TEST_ARTIFACT_DATA_ROOT is required by artifact runtime tests"};
#else
            char* RawPrefPath = SDL_GetPrefPath("OpenDBC", Storage.c_str());
            if (RawPrefPath == nullptr)
                return {{}, "Cannot locate writable storage for the embedded artifact"};
            const std::unique_ptr<char, decltype(&SDL_free)> PrefOwner(RawPrefPath, SDL_free);
            const std::filesystem::path PayloadRoot = std::filesystem::path(RawPrefPath) / "payload";
#endif
            std::string ExtractError;
            if (!detail::SynchronizeArtifactPayload(SourcePayloadRoot, PayloadRoot, Files, ExtractError))
                return {{}, std::move(ExtractError)};
            EmbeddedArtifact Artifact;
            Artifact.EntryPath = PayloadRoot / RelativeEntry;
            Artifact.AssetRoot = RelativeAssetRoot.empty() ? PayloadRoot : PayloadRoot / RelativeAssetRoot;
            Artifact.Profile = *Profile;
            Artifact.PayloadRoot = PayloadRoot;
            for (const auto& Source : RelativeSources)
                Artifact.OrderedSources.push_back(PayloadRoot / Source);
            return {std::move(Artifact), {}};
        }
    } // namespace toolchain

} // namespace dbc::studio
