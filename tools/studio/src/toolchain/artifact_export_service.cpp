#include "artifact_export_service.hpp"
#include "interpreter/basic_path.hpp"
#include "artifact_runtime.hpp"

#include "interpreter/execution_service.hpp"
#include "language/language_profile.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdio>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::string_view ManifestName = "opendbc-artifact.json";

        std::string Lower(std::string Value)
        {
            std::transform(Value.begin(), Value.end(), Value.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Value;
        }

        class Sha256
        {
          public:
            void Update(const unsigned char* Bytes, std::size_t Size)
            {
                TotalBytes_ += Size;
                while (Size > 0)
                {
                    const std::size_t Count = std::min(Size, Buffer_.size() - BufferSize_);
                    std::copy_n(Bytes, Count, Buffer_.begin() + static_cast<std::ptrdiff_t>(BufferSize_));
                    BufferSize_ += Count;
                    Bytes += Count;
                    Size -= Count;
                    if (BufferSize_ == Buffer_.size())
                    {
                        Transform(Buffer_.data());
                        BufferSize_ = 0;
                    }
                }
            }

            std::string Final()
            {
                const std::uint64_t BitLength = TotalBytes_ * 8;
                Buffer_[BufferSize_++] = 0x80;
                if (BufferSize_ > 56)
                {
                    std::fill(Buffer_.begin() + static_cast<std::ptrdiff_t>(BufferSize_), Buffer_.end(), 0);
                    Transform(Buffer_.data());
                    BufferSize_ = 0;
                }
                std::fill(Buffer_.begin() + static_cast<std::ptrdiff_t>(BufferSize_), Buffer_.begin() + 56, 0);
                for (int Index = 0; Index < 8; ++Index)
                    Buffer_[56 + Index] = static_cast<unsigned char>(BitLength >> (56 - Index * 8));
                Transform(Buffer_.data());

                std::ostringstream Output;
                Output << std::hex << std::setfill('0');
                for (const std::uint32_t Value : State_)
                    Output << std::setw(8) << Value;
                return Output.str();
            }

          private:
            static constexpr std::array<std::uint32_t, 64> Constants_ = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

            void Transform(const unsigned char* Block)
            {
                std::array<std::uint32_t, 64> Words{};
                for (std::size_t Index = 0; Index < 16; ++Index)
                {
                    const std::size_t Offset = Index * 4;
                    Words[Index] = (static_cast<std::uint32_t>(Block[Offset]) << 24) |
                                   (static_cast<std::uint32_t>(Block[Offset + 1]) << 16) |
                                   (static_cast<std::uint32_t>(Block[Offset + 2]) << 8) |
                                   static_cast<std::uint32_t>(Block[Offset + 3]);
                }
                for (std::size_t Index = 16; Index < Words.size(); ++Index)
                {
                    const std::uint32_t S0 =
                        std::rotr(Words[Index - 15], 7) ^ std::rotr(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
                    const std::uint32_t S1 =
                        std::rotr(Words[Index - 2], 17) ^ std::rotr(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
                    Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
                }

                std::uint32_t A = State_[0];
                std::uint32_t B = State_[1];
                std::uint32_t C = State_[2];
                std::uint32_t D = State_[3];
                std::uint32_t E = State_[4];
                std::uint32_t F = State_[5];
                std::uint32_t G = State_[6];
                std::uint32_t H = State_[7];
                for (std::size_t Index = 0; Index < Words.size(); ++Index)
                {
                    const std::uint32_t Sum1 = std::rotr(E, 6) ^ std::rotr(E, 11) ^ std::rotr(E, 25);
                    const std::uint32_t Choice = (E & F) ^ (~E & G);
                    const std::uint32_t Temporary1 = H + Sum1 + Choice + Constants_[Index] + Words[Index];
                    const std::uint32_t Sum0 = std::rotr(A, 2) ^ std::rotr(A, 13) ^ std::rotr(A, 22);
                    const std::uint32_t Majority = (A & B) ^ (A & C) ^ (B & C);
                    const std::uint32_t Temporary2 = Sum0 + Majority;
                    H = G;
                    G = F;
                    F = E;
                    E = D + Temporary1;
                    D = C;
                    C = B;
                    B = A;
                    A = Temporary1 + Temporary2;
                }
                State_[0] += A;
                State_[1] += B;
                State_[2] += C;
                State_[3] += D;
                State_[4] += E;
                State_[5] += F;
                State_[6] += G;
                State_[7] += H;
            }

            std::array<std::uint32_t, 8> State_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
            std::array<unsigned char, 64> Buffer_{};
            std::size_t BufferSize_ = 0;
            std::uint64_t TotalBytes_ = 0;
        };

        std::optional<std::string> Sha256File(const fs::path& Path)
        {
            std::ifstream Input(Path, std::ios::binary);
            if (!Input)
                return std::nullopt;
            Sha256 Hash;
            std::array<unsigned char, 64 * 1024> Buffer{};
            while (Input)
            {
                Input.read(reinterpret_cast<char*>(Buffer.data()), static_cast<std::streamsize>(Buffer.size()));
                const std::streamsize Count = Input.gcount();
                if (Count > 0)
                    Hash.Update(Buffer.data(), static_cast<std::size_t>(Count));
            }
            if (!Input.eof())
                return std::nullopt;
            return Hash.Final();
        }

        bool IsExcludedDirectory(const fs::path& Path)
        {
            const std::string Name = Lower(Path.filename().string());
            return Name == ".git" || Name == ".svn" || Name == ".hg" || Name == ".idea" || Name == ".vs" ||
                   Name == ".codex" || Name == "node_modules" || Name == "exports" || Name == "export" ||
                   Name == "dist" || Name == "out" || Name == "cmakefiles" || Name == "__pycache__" ||
                   Name == "build" || Name.starts_with("build-") || Name.starts_with(".opendbc-") ||
                   Path.extension() == ".app";
        }

        bool IsToolingSource(const fs::path& Path)
        {
            static const std::set<std::string> Extensions = {
                ".c",   ".cc",      ".cpp",     ".cxx",       ".h",        ".hh",  ".hpp",   ".hxx",
                ".m",   ".mm",      ".swift",   ".java",      ".kt",       ".kts", ".cs",    ".go",
                ".rs",  ".py",      ".rb",      ".sh",        ".bat",      ".ps1", ".cmake", ".gradle",
                ".sln", ".vcxproj", ".pbxproj", ".xcodeproj", ".expected", ".apk", ".aab",   ".ipa"};
            const std::string Name = Lower(Path.filename().string());
            return Name == "cmakelists.txt" || Name == "makefile" || Name == ".gitignore" || Name == ".gitattributes" ||
                   Extensions.contains(Lower(Path.extension().string()));
        }

        bool IsGeneratedRuntimeOutput(const fs::path& Path)
        {
            const std::string Name = Lower(Path.filename().string());
            const std::string Extension = Lower(Path.extension().string());
            return Name == "open-dbc.log" || (Name.starts_with("open-dbc.") && Extension == ".log") ||
                   (Name.starts_with("open-dbc-crash-") && Extension == ".log") || Name == "open-dbc-dump-scene.txt" ||
                   Name == "open-dbc-dump-renderer.txt" || Name == "selftest-final.png";
        }

        bool IsSensitive(const fs::path& Path)
        {
            const std::string Name = Lower(Path.filename().string());
            const std::string Extension = Lower(Path.extension().string());
            return Name == ".env" || Name.starts_with(".env.") || Name.find("credential") != std::string::npos ||
                   Name.find("password") != std::string::npos || Name.find("passwd") != std::string::npos ||
                   Name.find("secret") != std::string::npos || Name.find("token") != std::string::npos ||
                   Extension == ".pem" || Extension == ".key" || Extension == ".p12" || Extension == ".pfx" ||
                   Extension == ".jks" || Extension == ".keystore";
        }

        bool IsProjectMetadata(const fs::path& Path)
        {
            return Lower(Path.filename().string()) == ".opendbc-project.json";
        }

        bool ValidateAndroidHostLibrary(const fs::path& Path, std::string& Error)
        {
            std::error_code FileError;
            const fs::file_status Status = fs::symlink_status(Path, FileError);
            if (FileError || !fs::is_regular_file(Status) || fs::is_symlink(Status))
            {
                Error = "The installed Android export host has no regular non-symlink libmain.so";
                return false;
            }
            std::array<unsigned char, 20> Header{};
            std::ifstream Input(Path, std::ios::binary);
            Input.read(reinterpret_cast<char*>(Header.data()), static_cast<std::streamsize>(Header.size()));
            if (Input.gcount() != static_cast<std::streamsize>(Header.size()) || Header[0] != 0x7f ||
                Header[1] != 'E' || Header[2] != 'L' || Header[3] != 'F' || Header[4] != 2 || Header[5] != 1 ||
                Header[6] != 1 || Header[18] != 0xb7 || Header[19] != 0)
            {
                Error = "The installed Android export host libmain.so is not ELF64 little-endian AArch64";
                return false;
            }
            return true;
        }

        bool IsSafeRelative(const fs::path& Path)
        {
            if (Path.empty() || Path.is_absolute() || Path.has_root_path())
                return false;
            return std::none_of(Path.begin(), Path.end(),
                                [](const fs::path& Part) { return Part == ".." || Part == "."; });
        }

        bool IsSameOrDescendant(const fs::path& Path, const fs::path& Root)
        {
            if (Path == Root)
                return true;
            const fs::path Relative = Path.lexically_relative(Root);
            return !Relative.empty() && *Relative.begin() != "..";
        }

        std::string SafeProductName(const core::ProjectSettings& Settings, const fs::path& Entry)
        {
            std::string Value = Settings.AppName.empty() ? Entry.stem().string() : Settings.AppName;
            Value.erase(
                std::remove_if(
                    Value.begin(), Value.end(), [](unsigned char Character)
                    { return !(std::isalnum(Character) || Character == '-' || Character == '_' || Character == ' '); }),
                Value.end());
            while (!Value.empty() && Value.back() == ' ')
                Value.pop_back();
            return Value.empty() ? "OpenDBC Artifact" : Value;
        }

        bool IsBundleIdentifier(std::string_view Value)
        {
            if (Value.empty() || Value.size() > 255 || Value.front() == '.' || Value.back() == '.')
                return false;
            std::size_t SegmentCount = 0;
            std::size_t Begin = 0;
            while (Begin < Value.size())
            {
                const std::size_t End = Value.find('.', Begin);
                const std::string_view Segment =
                    Value.substr(Begin, End == std::string_view::npos ? Value.size() - Begin : End - Begin);
                if (Segment.empty() || !std::isalpha(static_cast<unsigned char>(Segment.front())) ||
                    std::any_of(Segment.begin() + 1, Segment.end(),
                                [](unsigned char Character) { return !(std::isalnum(Character) || Character == '_'); }))
                    return false;
                ++SegmentCount;
                if (End == std::string_view::npos)
                    break;
                Begin = End + 1;
            }
            return SegmentCount >= 2;
        }

#if defined(__APPLE__)
        bool IsAppleVersion(std::string_view Value)
        {
            if (Value.empty() || Value.size() > 18 || Value.front() == '.' || Value.back() == '.')
                return false;
            std::size_t Components = 1;
            for (const unsigned char Character : Value)
            {
                if (Character == '.')
                    ++Components;
                else if (!std::isdigit(Character))
                    return false;
            }
            return Components <= 3 && Value.find("..") == std::string_view::npos;
        }

        bool IsSupportedIosDeployment(std::string_view Value)
        {
            if (!IsAppleVersion(Value))
                return false;
            const std::size_t Dot = Value.find('.');
            int Major = 0;
            const char* Begin = Value.data();
            const char* End = Value.data() + (Dot == std::string_view::npos ? Value.size() : Dot);
            const auto [After, ParseError] = std::from_chars(Begin, End, Major);
            return ParseError == std::errc{} && After == End && Major >= 16;
        }

        bool IsAppleTeamIdentifier(std::string_view Value)
        {
            return Value.empty() ||
                   (Value.size() == 10 && std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
                                                      { return std::isupper(Character) || std::isdigit(Character); }));
        }
#endif

        std::string PropertyValue(std::string_view Value)
        {
            std::string Result;
            Result.reserve(Value.size() + 8);
            for (std::size_t Index = 0; Index < Value.size(); ++Index)
            {
                const char Character = Value[Index];
                if (Character == '\\' || Character == '=' || Character == ':' || Character == '#' || Character == '!' ||
                    (Character == ' ' && Index == 0))
                    Result += '\\';
                if (Character == '\n')
                    Result += "\\n";
                else if (Character == '\r')
                    Result += "\\r";
                else
                    Result += Character;
            }
            return Result;
        }

        std::string XmlValue(std::string_view Value)
        {
            std::string Result;
            Result.reserve(Value.size() + 8);
            for (const char Character : Value)
            {
                switch (Character)
                {
                    case '&':
                        Result += "&amp;";
                        break;
                    case '<':
                        Result += "&lt;";
                        break;
                    case '>':
                        Result += "&gt;";
                        break;
                    case '\"':
                        Result += "&quot;";
                        break;
                    case '\'':
                        Result += "&apos;";
                        break;
                    default:
                        Result += Character;
                        break;
                }
            }
            return Result;
        }

        std::string StageSuffix()
        {
            static std::atomic_uint64_t Sequence{0};
            return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                   std::to_string(Sequence.fetch_add(1, std::memory_order_relaxed));
        }

        bool CreateStageDirectory(const fs::path& OutputDirectory, std::string_view Prefix, fs::path& Stage,
                                  std::string& Error)
        {
            for (int Attempt = 0; Attempt < 128; ++Attempt)
            {
                Stage = OutputDirectory / (std::string(Prefix) + StageSuffix());
                if (toolchain::detail::ClaimArtifactStageDirectory(Stage, Error))
                    return true;
                if (Error != "Artifact staging path already exists")
                    return false;
                Error.clear();
            }
            Error = "Cannot allocate a unique artifact staging directory";
            return false;
        }

        bool PublishNoReplace(const fs::path& Source, const fs::path& Destination, std::string& Error)
        {
            bool Published = false;
#if defined(_WIN32)
            Published =
                MoveFileExW(Source.wstring().c_str(), Destination.wstring().c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#elif defined(__APPLE__)
            Published = ::renamex_np(Source.c_str(), Destination.c_str(), RENAME_EXCL) == 0;
#elif defined(__linux__)
            Published = ::syscall(SYS_renameat2, AT_FDCWD, Source.c_str(), AT_FDCWD, Destination.c_str(),
                                  RENAME_NOREPLACE) == 0;
#else
            std::error_code FileError;
            if (!fs::exists(Destination, FileError) && !FileError)
            {
                fs::rename(Source, Destination, FileError);
                Published = !FileError;
            }
#endif
            if (!Published)
            {
                std::error_code FileError;
                Error =
                    fs::exists(Destination, FileError) && !FileError
                        ? "Export destination already exists: " + Destination.string()
                        : "Cannot publish artifact atomically without replacing a destination: " + Destination.string();
            }
            return Published;
        }

        bool CopyFile(const fs::path& Source, const fs::path& Destination, std::string& Error)
        {
            std::error_code FileError;
            fs::create_directories(Destination.parent_path(), FileError);
            if (FileError)
            {
                Error = "Cannot create artifact directory '" + Destination.parent_path().string() +
                        "': " + FileError.message();
                return false;
            }
            fs::copy_file(Source, Destination, fs::copy_options::overwrite_existing, FileError);
            if (FileError)
            {
                Error = "Cannot copy '" + Source.string() + "': " + FileError.message();
                return false;
            }
            const fs::perms Permissions = fs::status(Source, FileError).permissions();
            if (!FileError)
                fs::permissions(Destination, Permissions, fs::perm_options::replace, FileError);
            return true;
        }

        bool CopyTree(const fs::path& Source, const fs::path& Destination, std::string& Error)
        {
            std::error_code FileError;
            const fs::path CanonicalSource = fs::weakly_canonical(Source, FileError);
            if (FileError || !fs::is_directory(CanonicalSource, FileError))
            {
                Error = "Artifact host directory does not exist: " + Source.string();
                return false;
            }
            const fs::path CanonicalDestinationParent = fs::weakly_canonical(Destination.parent_path(), FileError);
            if (FileError)
            {
                Error = "Cannot resolve artifact staging parent: " + FileError.message();
                return false;
            }
            const fs::path CanonicalDestination =
                (CanonicalDestinationParent / Destination.filename()).lexically_normal();
            if (IsSameOrDescendant(CanonicalDestination, CanonicalSource))
            {
                Error = "Artifact staging directory cannot be inside its source host";
                return false;
            }
            fs::create_directories(Destination, FileError);
            if (FileError)
            {
                Error = "Cannot create artifact staging directory: " + FileError.message();
                return false;
            }
            for (fs::recursive_directory_iterator It(CanonicalSource, fs::directory_options::none, FileError), End;
                 It != End; It.increment(FileError))
            {
                if (FileError)
                {
                    Error = "Cannot inspect artifact host: " + FileError.message();
                    return false;
                }
                const fs::file_status Status = It->symlink_status(FileError);
                if (FileError)
                {
                    Error = "Cannot inspect artifact host entry: " + FileError.message();
                    return false;
                }
                if (fs::is_symlink(Status))
                {
                    Error = "Artifact hosts cannot contain symlinks: " + It->path().string();
                    return false;
                }
                const fs::path Relative = fs::relative(It->path(), CanonicalSource, FileError);
                if (FileError || !IsSafeRelative(Relative))
                {
                    Error = "Artifact host contains an unsafe path";
                    return false;
                }
                const fs::path Target = Destination / Relative;
                if (fs::is_directory(Status))
                {
                    fs::create_directories(Target, FileError);
                    if (FileError)
                    {
                        Error = "Cannot create artifact host directory: " + FileError.message();
                        return false;
                    }
                }
                else if (fs::is_regular_file(Status) && !CopyFile(It->path(), Target, Error))
                {
                    return false;
                }
            }
            if (FileError)
            {
                Error = "Cannot inspect artifact host: " + FileError.message();
                return false;
            }
            return true;
        }

        [[maybe_unused]] bool RemoveRequired(const fs::path& Path, std::string& Error)
        {
            std::error_code FileError;
            std::filesystem::remove_all(Path, FileError);
            if (FileError || std::filesystem::exists(Path, FileError))
            {
                Error = "Cannot remove artifact staging content: " + Path.string();
                return false;
            }
            return true;
        }

        bool PublishAndRemoveStage(const fs::path& Source, const fs::path& Destination, const fs::path& Stage,
                                   std::string& Error)
        {
            fs::path PublicationStage;
            if (!CreateStageDirectory(Destination.parent_path(), ".opendbc-publication-", PublicationStage, Error))
                return false;
            const fs::path PublicationSource = PublicationStage / "artifact";
            if (!PublishNoReplace(Source, PublicationSource, Error) || !RemoveRequired(Stage, Error) ||
                !PublishNoReplace(PublicationSource, Destination, Error))
            {
                std::error_code Ignored;
                fs::remove_all(PublicationStage, Ignored);
                return false;
            }
            return RemoveRequired(PublicationStage, Error);
        }

        std::optional<std::string> HashFile(const fs::path& Path)
        {
            return toolchain::detail::ArtifactContentHashFile(Path);
        }

        bool ValidateExportProgram(const toolchain::ArtifactExportRequest& Request, std::string& Error,
                                   bool ConfineSources = false)
        {
            if (!dbc::language::IsLanguageSource(Request.EntrySource))
            {
                Error = "BASIC artifact entry must use the .dba extension";
                return false;
            }
            dbc::interpreter::ExecutionRequest AnalysisRequest;
            AnalysisRequest.RootLogicalPath = Request.EntrySource;
            AnalysisRequest.Profile = Request.Profile;
            AnalysisRequest.OrderedProjectSources =
                Request.OrderedSources.empty() ? std::vector<fs::path>{Request.EntrySource} : Request.OrderedSources;
            for (const fs::path& Source : AnalysisRequest.OrderedProjectSources)
            {
                std::string LoadError;
                std::optional<dbc::interpreter::SourceUnit> Unit =
                    dbc::interpreter::LoadSourceUnitFromDisk(Source, Request.Profile, LoadError);
                if (!Unit)
                {
                    Error = std::move(LoadError);
                    return false;
                }
                AnalysisRequest.Sources.push_back(std::move(*Unit));
            }
            AnalysisRequest.LoadMissingSource =
                [Root = Request.ProjectRoot](const fs::path& Path, dbc::language::LanguageProfile Profile,
                                             std::string& LoadError) -> std::optional<dbc::interpreter::SourceUnit>
            {
                fs::path Canonical;
                if (!dbc::interpreter::ResolveConfinedBasicPath(Root, Path, Canonical, &LoadError))
                {
                    LoadError =
                        "#INCLUDE paths in an embedded artifact must be relative and remain inside its payload: " +
                        LoadError;
                    return std::nullopt;
                }
                return dbc::interpreter::LoadSourceUnitFromDisk(Path, Profile, LoadError);
            };
            if (ConfineSources)
                AnalysisRequest.ConfinedSourceRoot = Request.ProjectRoot;
            const dbc::interpreter::SourceAnalysis Analysis =
                dbc::interpreter::AnalyzeSources(std::move(AnalysisRequest));
            if (Analysis.Success())
                return true;
            if (Analysis.Diagnostics.empty())
                Error = "BASIC artifact validation failed";
            else
                Error = Analysis.Diagnostics.front().Code + ": " + Analysis.Diagnostics.front().Message;
            return false;
        }

        [[maybe_unused]] std::optional<std::string> ReadFile(const fs::path& Path)
        {
            std::ifstream Input(Path, std::ios::binary);
            if (!Input)
                return std::nullopt;
            return std::string(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
        }

        bool CopyPayload(const toolchain::ArtifactExportRequest& Request, const fs::path& PayloadRoot,
                         fs::path& RelativeEntry, fs::path& RelativeAssetRoot, std::vector<fs::path>& RelativeSources,
                         std::vector<toolchain::detail::ArtifactFile>& Files, std::string& Error)
        {
            std::error_code FileError;
            const fs::path ProjectRoot = fs::weakly_canonical(Request.ProjectRoot, FileError);
            if (FileError || !fs::is_directory(ProjectRoot, FileError))
            {
                Error = "Project root does not exist: " + Request.ProjectRoot.string();
                return false;
            }
            const fs::path Entry = fs::weakly_canonical(Request.EntrySource, FileError);
            if (FileError || !fs::is_regular_file(Entry, FileError))
            {
                Error = "BASIC entry does not exist: " + Request.EntrySource.string();
                return false;
            }
            fs::path OutputRoot = fs::weakly_canonical(Request.OutputDirectory, FileError);
            if (FileError)
            {
                FileError.clear();
                const fs::path OutputParent = fs::weakly_canonical(Request.OutputDirectory.parent_path(), FileError);
                if (FileError)
                {
                    Error = "Cannot resolve the artifact output directory";
                    return false;
                }
                OutputRoot = (OutputParent / Request.OutputDirectory.filename()).lexically_normal();
            }
            if (OutputRoot == ProjectRoot)
            {
                Error = "Artifact output directory cannot be the project root";
                return false;
            }
            if (Lower(Entry.extension().string()) != ".dba")
            {
                Error = "BASIC entry must be a .dba file";
                return false;
            }

            fs::create_directories(PayloadRoot, FileError);
            if (FileError)
            {
                Error = "Cannot create the artifact payload: " + FileError.message();
                return false;
            }
            std::optional<fs::path> ConfiguredKeystore;
            if (!Request.Settings.AndroidKeystorePath.empty())
            {
                fs::path Keystore = dbc::interpreter::NormalizeBasicPath(Request.Settings.AndroidKeystorePath);
                if (Keystore.is_relative())
                    Keystore = ProjectRoot / Keystore;
                Keystore = fs::weakly_canonical(Keystore, FileError);
                if (FileError)
                {
                    Error = "Cannot resolve the configured keystore: " + FileError.message();
                    return false;
                }
                ConfiguredKeystore = std::move(Keystore);
            }
            for (fs::recursive_directory_iterator It(ProjectRoot, fs::directory_options::none, FileError), End;
                 It != End; It.increment(FileError))
            {
                if (FileError)
                {
                    Error = "Cannot inspect project files: " + FileError.message();
                    return false;
                }
                const fs::file_status Status = It->symlink_status(FileError);
                if (FileError)
                {
                    Error = "Cannot inspect project entry: " + FileError.message();
                    return false;
                }
                if (fs::is_symlink(Status))
                {
                    Error = "Projects containing symlinks cannot be exported: " + It->path().string();
                    return false;
                }
                if (fs::is_directory(Status))
                {
                    const fs::path Directory = fs::weakly_canonical(It->path(), FileError);
                    if (FileError)
                    {
                        Error = "Cannot resolve a project directory: " + FileError.message();
                        return false;
                    }
                    if (IsExcludedDirectory(It->path()) || IsSameOrDescendant(Directory, OutputRoot))
                        It.disable_recursion_pending();
                    continue;
                }
                if (!fs::is_regular_file(Status) || IsProjectMetadata(It->path()) || IsToolingSource(It->path()) ||
                    IsGeneratedRuntimeOutput(It->path()))
                    continue;
                if (ConfiguredKeystore)
                {
                    const fs::path CanonicalFile = fs::weakly_canonical(It->path(), FileError);
                    if (FileError)
                    {
                        Error = "Cannot resolve a project file: " + FileError.message();
                        return false;
                    }
                    if (CanonicalFile == *ConfiguredKeystore)
                        continue;
                }
                if (IsSensitive(It->path()))
                {
                    Error = "Project contains a file that may contain secrets and must be excluded explicitly: " +
                            It->path().string();
                    return false;
                }
                const fs::path Relative = fs::relative(It->path(), ProjectRoot, FileError);
                if (FileError || !IsSafeRelative(Relative))
                {
                    Error = "Project contains an unsafe relative path";
                    return false;
                }
                if (!CopyFile(It->path(), PayloadRoot / Relative, Error))
                    return false;
                const std::optional<std::string> Hash = HashFile(PayloadRoot / Relative);
                if (!Hash)
                {
                    Error = "Cannot hash copied artifact payload file: " + (PayloadRoot / Relative).string();
                    return false;
                }
                Files.push_back({Relative, *Hash});
            }
            if (FileError)
            {
                Error = "Cannot inspect project files: " + FileError.message();
                return false;
            }
            const std::vector<fs::path> OrderedSources =
                Request.OrderedSources.empty() ? std::vector<fs::path>{Entry} : Request.OrderedSources;
            toolchain::ArtifactExportRequest CopiedRequest = Request;
            CopiedRequest.ProjectRoot = PayloadRoot;
            CopiedRequest.OrderedSources.clear();
            for (std::size_t Index = 0; Index < OrderedSources.size(); ++Index)
            {
                const fs::path Source = fs::weakly_canonical(OrderedSources[Index], FileError);
                const fs::file_status Status = fs::symlink_status(OrderedSources[Index], FileError);
                if (FileError || !fs::is_regular_file(Status) || fs::is_symlink(Status) ||
                    Lower(Source.extension().string()) != ".dba")
                {
                    Error = "DBPro project sources must be regular non-symlink .dba files";
                    return false;
                }
                fs::path Relative = fs::relative(Source, ProjectRoot, FileError);
                const bool External = FileError || !IsSafeRelative(Relative);
                FileError.clear();
                if (External)
                {
                    Relative = fs::path("__external/sources") / std::to_string(Index) / Source.filename();
                    if (IsSensitive(Source) || !CopyFile(Source, PayloadRoot / Relative, Error))
                    {
                        if (Error.empty())
                            Error = "External DBPro source looks sensitive: " + Source.string();
                        return false;
                    }
                    const std::optional<std::string> Hash = HashFile(PayloadRoot / Relative);
                    if (!Hash)
                    {
                        Error = "Cannot hash external DBPro source";
                        return false;
                    }
                    Files.push_back({Relative, *Hash});
                }
                if (!fs::is_regular_file(PayloadRoot / Relative, FileError))
                {
                    Error = "A DBPro project source was excluded from the artifact payload";
                    return false;
                }
                CopiedRequest.OrderedSources.push_back(PayloadRoot / Relative);
                RelativeSources.push_back(Relative);
                if (Source == Entry)
                {
                    RelativeEntry = Relative;
                    CopiedRequest.EntrySource = PayloadRoot / Relative;
                }
            }
            if (RelativeEntry.empty())
            {
                Error = "The BASIC entry is not present in the ordered project sources";
                return false;
            }

            const fs::path AssetRoot =
                fs::weakly_canonical(Request.AssetRoot.empty() ? ProjectRoot : Request.AssetRoot, FileError);
            if (FileError || !fs::is_directory(AssetRoot, FileError))
            {
                Error = "Artifact asset root does not exist: " + Request.AssetRoot.string();
                return false;
            }
            RelativeAssetRoot = fs::relative(AssetRoot, ProjectRoot, FileError);
            const bool ExternalAssets = FileError || (!RelativeAssetRoot.empty() && RelativeAssetRoot != "." &&
                                                      !IsSafeRelative(RelativeAssetRoot));
            FileError.clear();
            if (ExternalAssets)
            {
                RelativeAssetRoot = "__external/assets";
                for (fs::recursive_directory_iterator It(AssetRoot, fs::directory_options::none, FileError), End;
                     It != End; It.increment(FileError))
                {
                    if (FileError)
                    {
                        Error = "Cannot inspect remapped DBPro media: " + FileError.message();
                        return false;
                    }
                    const fs::file_status Status = It->symlink_status(FileError);
                    if (FileError || fs::is_symlink(Status))
                    {
                        Error = "Remapped DBPro media cannot contain symlinks: " + It->path().string();
                        return false;
                    }
                    const fs::path Relative = fs::relative(It->path(), AssetRoot, FileError);
                    if (FileError || !IsSafeRelative(Relative))
                    {
                        Error = "Remapped DBPro media contains an unsafe path";
                        return false;
                    }
                    if (fs::is_directory(Status))
                    {
                        if (IsExcludedDirectory(It->path()))
                        {
                            It.disable_recursion_pending();
                            continue;
                        }
                        fs::create_directories(PayloadRoot / RelativeAssetRoot / Relative, FileError);
                        if (FileError)
                        {
                            Error = "Cannot create remapped media directory: " + FileError.message();
                            return false;
                        }
                        continue;
                    }
                    if (!fs::is_regular_file(Status))
                        continue;
                    if (IsGeneratedRuntimeOutput(It->path()))
                        continue;
                    if (ConfiguredKeystore && It->path().lexically_normal() == *ConfiguredKeystore)
                        continue;
                    if (IsSensitive(It->path()))
                    {
                        Error = "Remapped DBPro media contains a file that may expose secrets: " + It->path().string();
                        return false;
                    }
                    const fs::path Destination = RelativeAssetRoot / Relative;
                    if (!CopyFile(It->path(), PayloadRoot / Destination, Error))
                        return false;
                    const std::optional<std::string> Hash = HashFile(PayloadRoot / Destination);
                    if (!Hash)
                    {
                        Error = "Cannot hash remapped DBPro media";
                        return false;
                    }
                    Files.push_back({Destination, *Hash});
                }
            }
            else if (RelativeAssetRoot == ".")
                RelativeAssetRoot.clear();
            if (FileError)
            {
                Error = "Cannot inspect remapped DBPro media: " + FileError.message();
                return false;
            }
            CopiedRequest.AssetRoot = RelativeAssetRoot.empty() ? PayloadRoot : PayloadRoot / RelativeAssetRoot;
            if (!ValidateExportProgram(CopiedRequest, Error, true))
            {
                Error = "Copied artifact payload is not self-contained: " + Error;
                return false;
            }
            std::sort(Files.begin(), Files.end(), [](const auto& Left, const auto& Right)
                      { return Left.RelativePath.generic_string() < Right.RelativePath.generic_string(); });
            return true;
        }

        bool WriteManifest(const toolchain::ArtifactExportRequest& Request, const fs::path& ResourceRoot,
                           const fs::path& RelativeEntry, const fs::path& RelativeAssetRoot,
                           const std::vector<fs::path>& RelativeSources,
                           const std::vector<toolchain::detail::ArtifactFile>& Files, std::string& Error)
        {
            nlohmann::json Manifest;
            Manifest["format"] = 5;
            Manifest["orderedSources"] = nlohmann::json::array();
            for (const fs::path& Source : RelativeSources)
                Manifest["orderedSources"].push_back(Source.generic_string());
            Manifest["entry"] = RelativeEntry.generic_string();
            Manifest["dialect"] = Request.Profile == dbc::language::LanguageProfile::Dbc ? "dbc" : "dbpro";
            Manifest["assetRoot"] = RelativeAssetRoot.generic_string();
            const std::string StorageSeed = Request.Settings.BundleId.empty()
                                                ? fs::absolute(Request.ProjectRoot).lexically_normal().generic_string()
                                                : Request.Settings.BundleId;
            Manifest["storage"] = "artifact-" + toolchain::detail::ArtifactContentHash(StorageSeed);
            Manifest["files"] = nlohmann::json::array();
            for (const toolchain::detail::ArtifactFile& File : Files)
                Manifest["files"].push_back({{"path", File.RelativePath.generic_string()}, {"hash", File.ContentHash}});
            std::ofstream Output(ResourceRoot / ManifestName, std::ios::binary | std::ios::trunc);
            if (!Output)
            {
                Error = "Cannot write the embedded artifact manifest";
                return false;
            }
            Output << Manifest.dump(2) << '\n';
            if (!Output.good())
            {
                Error = "Cannot finish the embedded artifact manifest";
                return false;
            }
            return true;
        }

        bool RunProcess(const std::vector<std::string>& Arguments, std::string& Error)
        {
            std::vector<const char*> Values;
            Values.reserve(Arguments.size() + 1);
            for (const std::string& Argument : Arguments)
                Values.push_back(Argument.c_str());
            Values.push_back(nullptr);
            SDL_Process* Process = SDL_CreateProcess(Values.data(), false);
            if (Process == nullptr)
            {
                Error = "Cannot start '" + Arguments.front() + "': " + SDL_GetError();
                return false;
            }
            int ExitCode = -1;
            const bool Waited = SDL_WaitProcess(Process, true, &ExitCode);
            SDL_DestroyProcess(Process);
            if (!Waited || ExitCode != 0)
            {
                Error = "Command failed (" + std::to_string(ExitCode) + "): " + Arguments.front();
                return false;
            }
            return true;
        }

        [[maybe_unused]] std::optional<std::string> RunProcessCapture(const std::vector<std::string>& Arguments,
                                                                      std::string& Error)
        {
            std::vector<const char*> Values;
            Values.reserve(Arguments.size() + 1);
            for (const std::string& Argument : Arguments)
                Values.push_back(Argument.c_str());
            Values.push_back(nullptr);
            const SDL_PropertiesID Properties = SDL_CreateProperties();
            if (Properties == 0)
            {
                Error = "Cannot prepare process '" + Arguments.front() + "': " + SDL_GetError();
                return std::nullopt;
            }
            SDL_SetPointerProperty(Properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, Values.data());
            SDL_SetNumberProperty(Properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(Properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
            SDL_Process* Process = SDL_CreateProcessWithProperties(Properties);
            SDL_DestroyProperties(Properties);
            if (Process == nullptr)
            {
                Error = "Cannot start '" + Arguments.front() + "': " + SDL_GetError();
                return std::nullopt;
            }
            std::size_t Size = 0;
            int ExitCode = -1;
            void* Bytes = SDL_ReadProcess(Process, &Size, &ExitCode);
            SDL_DestroyProcess(Process);
            const std::unique_ptr<void, decltype(&SDL_free)> Owner(Bytes, SDL_free);
            if (Bytes == nullptr || ExitCode != 0)
            {
                Error = "Command failed (" + std::to_string(ExitCode) + "): " + Arguments.front();
                return std::nullopt;
            }
            return std::string(static_cast<const char*>(Bytes), Size);
        }

        std::optional<fs::path> ResolveArtifactIcon(const toolchain::ArtifactExportRequest& Request, bool Required,
                                                    std::string& Error)
        {
            fs::path Source;
            if (!Request.Settings.IconSource.empty())
            {
                Source = Request.Settings.IconSource;
                if (Source.is_relative())
                    Source = Request.ProjectRoot / Source;
            }
            else
            {
                Source = Request.HostResources / "editor.png";
            }
            std::error_code FileError;
            const fs::file_status Status = fs::symlink_status(Source, FileError);
            if (FileError || !fs::is_regular_file(Status) || fs::is_symlink(Status))
            {
                if (!Request.Settings.IconSource.empty() || Required)
                    Error = "Artifact icon must be a regular non-symlink PNG file";
                return std::nullopt;
            }
            if (Lower(Source.extension().string()) != ".png")
            {
                Error = "Artifact icon source must be a PNG file";
                return std::nullopt;
            }
            Source = fs::weakly_canonical(Source, FileError);
            if (FileError)
            {
                Error = "Cannot resolve the artifact icon source";
                return std::nullopt;
            }
            return Source;
        }

        bool WriteScaledPng(const fs::path& Source, const fs::path& Destination, int Size, bool Opaque,
                            std::string& Error)
        {
            SDL_Surface* Original = IMG_Load(Source.string().c_str());
            if (Original == nullptr)
            {
                Error = "Cannot load artifact icon: " + Source.string();
                return false;
            }
            const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> OriginalOwner(Original,
                                                                                            SDL_DestroySurface);
            SDL_Surface* Scaled = SDL_ScaleSurface(Original, Size, Size, SDL_SCALEMODE_LINEAR);
            if (Scaled == nullptr)
            {
                Error = "Cannot scale the artifact icon";
                return false;
            }
            const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> ScaledOwner(Scaled, SDL_DestroySurface);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> OpaqueOwner(nullptr, SDL_DestroySurface);
            SDL_Surface* SurfaceToSave = Scaled;
            if (Opaque)
            {
                OpaqueOwner.reset(SDL_CreateSurface(Size, Size, SDL_PIXELFORMAT_XRGB8888));
                if (!OpaqueOwner ||
                    !SDL_FillSurfaceRect(OpaqueOwner.get(), nullptr,
                                         SDL_MapSurfaceRGB(OpaqueOwner.get(), 13, 18, 28)) ||
                    !SDL_SetSurfaceBlendMode(Scaled, SDL_BLENDMODE_BLEND) ||
                    !SDL_BlitSurface(Scaled, nullptr, OpaqueOwner.get(), nullptr))
                {
                    Error = "Cannot flatten the iOS artifact icon";
                    return false;
                }
                SurfaceToSave = OpaqueOwner.get();
            }
            std::error_code FileError;
            fs::create_directories(Destination.parent_path(), FileError);
            if (FileError)
            {
                Error = "Cannot write artifact icon: " + Destination.string();
                return false;
            }
#if defined(__APPLE__)
            if (Opaque)
            {
                fs::path Bitmap = Destination;
                Bitmap += ".opendbc.bmp";
                if (!SDL_SaveBMP(SurfaceToSave, Bitmap.string().c_str()) ||
                    !RunProcess(
                        {"/usr/bin/sips", "-s", "format", "png", Bitmap.string(), "--out", Destination.string()},
                        Error))
                {
                    std::error_code Ignored;
                    fs::remove(Bitmap, Ignored);
                    if (Error.empty())
                        Error = "Cannot write opaque artifact icon: " + Destination.string();
                    return false;
                }
                return RemoveRequired(Bitmap, Error);
            }
#else
            if (Opaque)
            {
                Error = "Opaque iOS artifact icons can only be produced on macOS";
                return false;
            }
#endif
            if (!IMG_SavePNG(SurfaceToSave, Destination.string().c_str()))
            {
                Error = "Cannot write artifact icon: " + Destination.string();
                return false;
            }
            return true;
        }

        bool PrepareAndroidIcon(const toolchain::ArtifactExportRequest& Request, const fs::path& Project,
                                std::string& Error)
        {
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            if (!Source)
                return false;
            static const std::array<std::pair<std::string_view, int>, 5> Icons = {{
                {"mipmap-mdpi", 48},
                {"mipmap-hdpi", 72},
                {"mipmap-xhdpi", 96},
                {"mipmap-xxhdpi", 144},
                {"mipmap-xxxhdpi", 192},
            }};
            for (const auto& [Directory, Size] : Icons)
            {
                if (!WriteScaledPng(*Source, Project / "app/src/main/res" / Directory / "ic_launcher.png", Size, false,
                                    Error))
                    return false;
            }
            return true;
        }

        bool SignAppleBundle(const fs::path& Bundle, bool Sign, std::string_view ExpectedTeamId,
                             const fs::path& Entitlements, std::string& Error)
        {
#if defined(__APPLE__)
            if (!Sign)
                return RunProcess(
                    {"/usr/bin/codesign", "--force", "--deep", "--sign", "-", "--timestamp=none", Bundle.string()},
                    Error);
            const char* IdentityValue = std::getenv("OPENDBC_IOS_SIGNING_IDENTITY");
            const std::string Identity = IdentityValue != nullptr ? IdentityValue : "-";
            if (!ExpectedTeamId.empty() && Identity == "-")
            {
                Error = "The configured iOS team requires OPENDBC_IOS_SIGNING_IDENTITY";
                return false;
            }
            std::vector<std::string> Arguments = {"/usr/bin/codesign", "--force", "--sign", Identity,
                                                  "--timestamp=none"};
            if (!Entitlements.empty())
            {
                Arguments.insert(Arguments.end(),
                                 {"--entitlements", Entitlements.string(), "--generate-entitlement-der"});
            }
            Arguments.push_back(Bundle.string());
            if (!RunProcess(Arguments, Error))
                return false;
            if (ExpectedTeamId.empty())
                return true;
            const std::optional<std::string> Details =
                RunProcessCapture({"/usr/bin/codesign", "--display", "--verbose=4", Bundle.string()}, Error);
            if (!Details || Details->find("TeamIdentifier=" + std::string(ExpectedTeamId)) == std::string::npos)
            {
                if (Error.empty())
                    Error = "The signing identity does not belong to iOS team " + std::string(ExpectedTeamId);
                return false;
            }
            return true;
#else
            (void)Bundle;
            (void)Sign;
            (void)ExpectedTeamId;
            (void)Entitlements;
            (void)Error;
            return true;
#endif
        }

#if defined(__APPLE__)
        bool PrepareMacIcon(const toolchain::ArtifactExportRequest& Request, const fs::path& App, std::string& Error)
        {
            if (Request.Settings.IconSource.empty())
                return true;
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            if (!Source)
                return false;
            const fs::path IconSet = App / "Contents/Resources/.opendbc.iconset";
            static const std::array<std::pair<std::string_view, int>, 10> Icons = {{
                {"icon_16x16.png", 16},
                {"icon_16x16@2x.png", 32},
                {"icon_32x32.png", 32},
                {"icon_32x32@2x.png", 64},
                {"icon_128x128.png", 128},
                {"icon_128x128@2x.png", 256},
                {"icon_256x256.png", 256},
                {"icon_256x256@2x.png", 512},
                {"icon_512x512.png", 512},
                {"icon_512x512@2x.png", 1024},
            }};
            for (const auto& [Name, Size] : Icons)
            {
                if (!WriteScaledPng(*Source, IconSet / Name, Size, false, Error))
                    return false;
            }
            const fs::path Icon = App / "Contents/Resources/artifact.icns";
            if (!RunProcess({"/usr/bin/iconutil", "-c", "icns", "-o", Icon.string(), IconSet.string()}, Error) ||
                !RemoveRequired(IconSet, Error))
                return false;
            const fs::path Plist = App / "Contents/Info.plist";
            return RunProcess(
                {"/usr/bin/plutil", "-replace", "CFBundleIconFile", "-string", "artifact.icns", Plist.string()}, Error);
        }

        bool ValidateIosHost(const fs::path& App, bool Simulator, std::string& Error)
        {
            const fs::path Executable = App / "opendbc";
            if (!fs::is_regular_file(Executable))
            {
                Error = "The iOS export host has no OpenDBC executable";
                return false;
            }
            const std::optional<std::string> Build =
                RunProcessCapture({"/usr/bin/xcrun", "vtool", "-show-build", Executable.string()}, Error);
            const bool IsSimulator = Build && Build->find("platform IOSSIMULATOR") != std::string::npos;
            const bool IsDevice = Build && Build->find("platform IOS\n") != std::string::npos && !IsSimulator;
            if ((Simulator && !IsSimulator) || (!Simulator && !IsDevice))
            {
                if (Error.empty())
                    Error = Simulator ? "The iOS Simulator export host is not an iphonesimulator binary"
                                      : "The iOS .ipa export host is not an iphoneos device binary";
                return false;
            }
            const std::optional<std::string> Architectures =
                RunProcessCapture({"/usr/bin/lipo", "-archs", Executable.string()}, Error);
            if (!Architectures || Architectures->find("arm64") == std::string::npos)
            {
                if (Error.empty())
                    Error = "The iOS .ipa export host does not contain arm64";
                return false;
            }
            return true;
        }

        bool PrepareIosProvisioning(const fs::path& App, const core::ProjectSettings& Settings, bool Simulator,
                                    bool Sign, fs::path& Entitlements, std::string& Error)
        {
            Entitlements.clear();
            if (!Sign || Simulator || Settings.IosTeamId.empty())
                return true;
            const char* ProfileValue = std::getenv("OPENDBC_IOS_PROVISIONING_PROFILE");
            if (ProfileValue == nullptr || *ProfileValue == '\0')
            {
                Error = "The configured iOS team requires OPENDBC_IOS_PROVISIONING_PROFILE";
                return false;
            }
            std::error_code FileError;
            const fs::path Profile = fs::weakly_canonical(ProfileValue, FileError);
            const fs::file_status Status = fs::symlink_status(ProfileValue, FileError);
            if (FileError || !fs::is_regular_file(Status) || fs::is_symlink(Status))
            {
                Error = "The iOS provisioning profile must be a regular non-symlink file";
                return false;
            }
            const fs::path Decoded = App.parent_path() / ".opendbc-provisioning.plist";
            Entitlements = App.parent_path() / ".opendbc-entitlements.plist";
            const auto RemoveTemporary = [&]()
            {
                std::error_code Ignored;
                fs::remove(Decoded, Ignored);
                if (!Entitlements.empty())
                    fs::remove(Entitlements, Ignored);
            };
            if (!CopyFile(Profile, App / "embedded.mobileprovision", Error) ||
                !RunProcess({"/usr/bin/security", "cms", "-D", "-i", Profile.string(), "-o", Decoded.string()}, Error))
            {
                RemoveTemporary();
                return false;
            }
            const std::optional<std::string> TeamOutput = RunProcessCapture(
                {"/usr/bin/plutil", "-extract", "TeamIdentifier.0", "raw", "-o", "-", Decoded.string()}, Error);
            const std::optional<std::string> ApplicationIdentifierOutput =
                RunProcessCapture({"/usr/bin/plutil", "-extract", "Entitlements.application-identifier", "raw", "-o",
                                   "-", Decoded.string()},
                                  Error);
            const auto Trimmed = [](std::optional<std::string> Value)
            {
                std::string Result = Value.value_or(std::string{});
                while (!Result.empty() && std::isspace(static_cast<unsigned char>(Result.back())))
                    Result.pop_back();
                return Result;
            };
            const std::string Team = Trimmed(TeamOutput);
            const std::string ApplicationIdentifier = Trimmed(ApplicationIdentifierOutput);
            const std::string BundleId =
                Settings.BundleId.empty() ? "net.demonixis.opendbc.artifact" : Settings.BundleId;
            const std::string RequiredPrefix = Settings.IosTeamId + ".";
            const bool MatchesBundle =
                ApplicationIdentifier.starts_with(RequiredPrefix) &&
                (ApplicationIdentifier.substr(RequiredPrefix.size()) == BundleId ||
                 (ApplicationIdentifier.ends_with(".*") &&
                  BundleId.starts_with(ApplicationIdentifier.substr(
                      RequiredPrefix.size(), ApplicationIdentifier.size() - RequiredPrefix.size() - 1))));
            if (Team != Settings.IosTeamId || !MatchesBundle ||
                !RunProcess({"/usr/bin/plutil", "-extract", "Entitlements", "xml1", "-o", Entitlements.string(),
                             Decoded.string()},
                            Error))
            {
                if (Error.empty())
                    Error = "The iOS provisioning profile does not match the configured team and bundle ID";
                RemoveTemporary();
                return false;
            }
            if (!RemoveRequired(Decoded, Error))
            {
                RemoveTemporary();
                return false;
            }
            return true;
        }

        bool PrepareIosIcon(const toolchain::ArtifactExportRequest& Request, const fs::path& App, bool Simulator,
                            std::string& Error)
        {
            const std::optional<fs::path> Source = ResolveArtifactIcon(Request, true, Error);
            if (!Source)
                return false;
            const fs::path Catalog = App.parent_path() / ".opendbc-AppIcon.xcassets";
            const fs::path IconSet = Catalog / "AppIcon.appiconset";
            if (!WriteScaledPng(*Source, IconSet / "AppIcon.png", 1024, true, Error))
                return false;
            nlohmann::json Contents;
            Contents["images"] = nlohmann::json::array(
                {{{"filename", "AppIcon.png"}, {"idiom", "universal"}, {"platform", "ios"}, {"size", "1024x1024"}}});
            Contents["info"] = {{"author", "OpenDBC"}, {"version", 1}};
            std::ofstream Json(IconSet / "Contents.json", std::ios::binary | std::ios::trunc);
            Json << Contents.dump(2) << '\n';
            Json.close();
            if (!Json)
            {
                Error = "Cannot write the iOS app icon catalogue";
                return false;
            }
            const fs::path PartialPlist = App.parent_path() / ".opendbc-icon-info.plist";
            const std::string Platform = Simulator ? "iphonesimulator" : "iphoneos";
            if (!RunProcess({"/usr/bin/xcrun",
                             "actool",
                             Catalog.string(),
                             "--compile",
                             App.string(),
                             "--app-icon",
                             "AppIcon",
                             "--platform",
                             Platform,
                             "--minimum-deployment-target",
                             Request.Settings.IosDeploymentTarget,
                             "--target-device",
                             "iphone",
                             "--target-device",
                             "ipad",
                             "--output-partial-info-plist",
                             PartialPlist.string(),
                             "--output-format",
                             "human-readable-text",
                             "--compress-pngs"},
                            Error))
                return false;
            if (!fs::is_regular_file(App / "Assets.car") || !fs::is_regular_file(App / "AppIcon60x60@2x.png") ||
                !fs::is_regular_file(App / "AppIcon76x76@2x~ipad.png"))
            {
                Error = "The iOS asset compiler did not produce a complete app icon set";
                return false;
            }
            const fs::path Plist = App / "Info.plist";
            const fs::path PhoneJson = App.parent_path() / ".opendbc-phone-icons.json";
            const fs::path IpadJson = App.parent_path() / ".opendbc-ipad-icons.json";
            if (!RunProcess({"/usr/bin/plutil", "-extract", "CFBundleIcons", "json", "-o", PhoneJson.string(),
                             PartialPlist.string()},
                            Error) ||
                !RunProcess({"/usr/bin/plutil", "-extract", "CFBundleIcons~ipad", "json", "-o", IpadJson.string(),
                             PartialPlist.string()},
                            Error))
                return false;
            const std::optional<std::string> PhoneMetadata = ReadFile(PhoneJson);
            const std::optional<std::string> IpadMetadata = ReadFile(IpadJson);
            if (!PhoneMetadata || !IpadMetadata ||
                !RunProcess({"/usr/bin/plutil", "-replace", "CFBundleIcons", "-json", *PhoneMetadata, Plist.string()},
                            Error) ||
                !RunProcess(
                    {"/usr/bin/plutil", "-replace", "CFBundleIcons~ipad", "-json", *IpadMetadata, Plist.string()},
                    Error) ||
                !RemoveRequired(Catalog, Error) || !RemoveRequired(PartialPlist, Error) ||
                !RemoveRequired(PhoneJson, Error) || !RemoveRequired(IpadJson, Error))
                return false;
            return true;
        }

        bool SetAppleMetadata(const fs::path& App, const core::ProjectSettings& Settings,
                              const std::string& ProductName, std::string& Error)
        {
            const std::string BundleId =
                Settings.BundleId.empty() ? "net.demonixis.opendbc.artifact" : Settings.BundleId;
            if (!IsBundleIdentifier(BundleId))
            {
                Error = "iOS bundle ID must contain at least two valid identifier segments";
                return false;
            }
            if (!IsAppleVersion(Settings.VersionName) || Settings.VersionCode < 1)
            {
                Error = "iOS versions require one to three numeric components and a positive build number";
                return false;
            }
            const fs::path RootPlist = App / "Info.plist";
            const fs::path Plist = fs::is_regular_file(RootPlist) ? RootPlist : App / "Contents/Info.plist";
            if (!fs::is_regular_file(Plist))
            {
                Error = "The Apple export host has no Info.plist";
                return false;
            }
            const auto ReplaceString = [&Plist, &Error](std::string_view Key, std::string_view Value)
            {
                return RunProcess(
                    {"/usr/bin/plutil", "-replace", std::string(Key), "-string", std::string(Value), Plist.string()},
                    Error);
            };
            if (!ReplaceString("CFBundleIdentifier", BundleId) || !ReplaceString("CFBundleDisplayName", ProductName) ||
                !ReplaceString("CFBundleName", ProductName) ||
                !ReplaceString("CFBundleShortVersionString", Settings.VersionName) ||
                !ReplaceString("CFBundleVersion", std::to_string(Settings.VersionCode)))
                return false;
            return true;
        }

        bool SetIosMetadata(const fs::path& App, const core::ProjectSettings& Settings, const std::string& ProductName,
                            bool RequireTeam, std::string& Error)
        {
            if (!SetAppleMetadata(App, Settings, ProductName, Error))
                return false;
            const fs::path Plist = App / "Info.plist";
            if (!IsSupportedIosDeployment(Settings.IosDeploymentTarget))
            {
                Error = "The iOS deployment target must be version 16.0 or newer";
                return false;
            }
            if (RequireTeam && !IsAppleTeamIdentifier(Settings.IosTeamId))
            {
                Error = "The iOS development team ID must contain 10 uppercase letters or digits";
                return false;
            }

            static const std::set<std::string> Orientations = {"LandscapeLeft", "LandscapeRight", "Portrait",
                                                               "PortraitUpsideDown"};
            nlohmann::json Values = nlohmann::json::array();
            for (const std::string& Orientation : Settings.IosOrientations)
            {
                if (!Orientations.contains(Orientation))
                {
                    Error = "Unsupported iOS orientation: " + Orientation;
                    return false;
                }
                Values.push_back("UIInterfaceOrientation" + Orientation);
            }
            if (Values.empty())
            {
                Error = "At least one iOS orientation is required";
                return false;
            }
            const std::string Json = Values.dump();
            if (!RunProcess(
                    {"/usr/bin/plutil", "-replace", "UISupportedInterfaceOrientations", "-json", Json, Plist.string()},
                    Error) ||
                !RunProcess({"/usr/bin/plutil", "-replace", "UISupportedInterfaceOrientations~ipad", "-json", Json,
                             Plist.string()},
                            Error) ||
                !RunProcess({"/usr/bin/plutil", "-replace", "MinimumOSVersion", "-string", Settings.IosDeploymentTarget,
                             Plist.string()},
                            Error))
                return false;

            const std::array<std::pair<std::string_view, const std::string*>, 4> Permissions = {{
                {"NSCameraUsageDescription", &Settings.IosCameraDesc},
                {"NSMicrophoneUsageDescription", &Settings.IosMicrophoneDesc},
                {"NSLocationWhenInUseUsageDescription", &Settings.IosLocationDesc},
                {"NSPhotoLibraryUsageDescription", &Settings.IosPhotoDesc},
            }};
            for (const auto& [Key, Value] : Permissions)
            {
                if (!Value->empty() &&
                    !RunProcess({"/usr/bin/plutil", "-insert", std::string(Key), "-string", *Value, Plist.string()},
                                Error))
                    return false;
            }
            return true;
        }

        bool PrepareIosApplication(const toolchain::ArtifactExportRequest& Request, const fs::path& Host,
                                   const fs::path& App, const std::string& ProductName, bool Simulator,
                                   std::vector<toolchain::detail::ArtifactFile>& Files, std::string& Error)
        {
            if (!toolchain::detail::ValidateArtifactShaders(Host / "shaders", Request.HostResources / "shaders",
                                                            Error) ||
                !CopyTree(Host, App, Error) || !ValidateIosHost(App, Simulator, Error))
                return false;
            fs::path RelativeEntry;
            std::vector<fs::path> RelativeSources;
            fs::path RelativeAssetRoot;
            fs::path Entitlements;
            if (!CopyPayload(Request, App / "artifact", RelativeEntry, RelativeAssetRoot, RelativeSources, Files,
                             Error) ||
                !WriteManifest(Request, App, RelativeEntry, RelativeAssetRoot, RelativeSources, Files, Error) ||
                !SetIosMetadata(App, Request.Settings, ProductName, Request.Sign && !Simulator, Error) ||
                !PrepareIosIcon(Request, App, Simulator, Error) ||
                !PrepareIosProvisioning(App, Request.Settings, Simulator, Request.Sign, Entitlements, Error))
                return false;
            const bool Signed = SignAppleBundle(
                App, Request.Sign, Simulator ? std::string_view{} : Request.Settings.IosTeamId, Entitlements, Error);
            if (!Entitlements.empty() && !RemoveRequired(Entitlements, Error))
                return false;
            return Signed;
        }
#endif

        toolchain::ArtifactExportResult ExportDesktop(const toolchain::ArtifactExportRequest& Request)
        {
            toolchain::ArtifactExportResult Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
#if defined(__APPLE__)
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".app");
#else
            Result.OutputPath = Request.OutputDirectory / ProductName;
#endif
            std::error_code FileError;
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-stage-", Stage, Result.Error))
                return Result;
            struct Cleanup
            {
                fs::path Path;
                ~Cleanup()
                {
                    std::error_code Ignored;
                    fs::remove_all(Path, Ignored);
                }
            } Cleanup{Stage};

#if defined(__APPLE__)
            if (!CopyTree(Request.HostProduct, Stage, Result.Error))
                return Result;
            const fs::path Resources = Stage / "Contents/Resources";
            if (!RemoveRequired(Resources / "export-hosts", Result.Error) ||
                !RemoveRequired(Resources / "samples", Result.Error))
                return Result;
            for (const std::string_view Reference :
                 {"darkbasic_reference.md", "dbpro_reference.md", "open-dbc_reference.md"})
            {
                if (!RemoveRequired(Resources / Reference, Result.Error))
                    return Result;
            }
#else
            if (!Request.Settings.IconSource.empty())
            {
                Result.Error = "Custom Desktop artifact icons are currently supported only on macOS";
                return Result;
            }
            if (fs::is_directory(Request.HostProduct, FileError))
            {
                if (!CopyTree(Request.HostProduct, Stage, Result.Error))
                    return Result;
            }
            else if (!CopyFile(Request.HostProduct, Stage / Request.HostProduct.filename(), Result.Error))
            {
                return Result;
            }
            const fs::path Resources = Stage;
#if defined(_WIN32)
            constexpr auto XrLoader = "openxr_loader.dll";
#else
            constexpr auto XrLoader = "libopenxr_loader.so.1";
#endif
            if (!CopyFile(Request.HostResources / XrLoader, Resources / XrLoader, Result.Error))
                return Result;
            const fs::path ShaderSource = Request.HostResources / "shaders";
            if (!CopyTree(ShaderSource, Resources / "shaders", Result.Error))
                return Result;
            if (!CopyFile(Request.HostResources / "NOTICE.md", Resources / "NOTICE.md", Result.Error) ||
                !CopyTree(Request.HostResources / "licenses", Resources / "licenses", Result.Error))
                return Result;
#endif
            fs::path RelativeEntry;
            std::vector<fs::path> RelativeSources;
            fs::path RelativeAssetRoot;
            std::vector<toolchain::detail::ArtifactFile> Files;
            if (!CopyPayload(Request, Resources / "artifact", RelativeEntry, RelativeAssetRoot, RelativeSources, Files,
                             Result.Error) ||
                !WriteManifest(Request, Resources, RelativeEntry, RelativeAssetRoot, RelativeSources, Files,
                               Result.Error))
                return Result;
#if defined(__APPLE__)
            if (!PrepareMacIcon(Request, Stage, Result.Error))
                return Result;
            if (!SetAppleMetadata(Stage, Request.Settings, ProductName, Result.Error))
                return Result;
#endif
            if (!SignAppleBundle(Stage, Request.Sign, {}, {}, Result.Error))
                return Result;
            if (!PublishNoReplace(Stage, Result.OutputPath, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported " + std::to_string(Files.size()) + " BASIC/source asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
        }

        toolchain::ArtifactExportResult ExportIos(const toolchain::ArtifactExportRequest& Request)
        {
            toolchain::ArtifactExportResult Result;
#if !defined(__APPLE__)
            (void)Request;
            Result.Error = "iOS artifacts can only be exported on macOS";
            return Result;
#else
            const fs::path Host = Request.HostResources / "export-hosts/ios/OpenDBC.app";
            std::error_code FileError;
            if (!fs::is_directory(Host, FileError))
            {
                Result.Error = "The installed product does not contain the qualified iOS export host";
                return Result;
            }
            if (!toolchain::detail::ValidateArtifactHostIdentity(
                    Host.parent_path() / "opendbc-host.json", Host / "opendbc",
                    Request.HostResources / "opendbc-runtime.json", Result.Error))
                return Result;
            if (Request.Sign && Request.Settings.IosTeamId.empty())
            {
                Result.Error = "Signed iOS device exports require a development team ID";
                return Result;
            }
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".ipa");
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-ios-stage-", Stage, Result.Error))
                return Result;
            struct Cleanup
            {
                fs::path Path;
                ~Cleanup()
                {
                    std::error_code Ignored;
                    fs::remove_all(Path, Ignored);
                }
            } Cleanup{Stage};
            const fs::path App = Stage / "Payload" / (ProductName + ".app");
            std::vector<toolchain::detail::ArtifactFile> Files;
            if (!PrepareIosApplication(Request, Host, App, ProductName, false, Files, Result.Error))
                return Result;
            const fs::path Archive = Stage / "artifact.ipa";
            if (!RunProcess({"/usr/bin/ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent",
                             (Stage / "Payload").string(), Archive.string()},
                            Result.Error))
                return Result;
            if (!PublishAndRemoveStage(Archive, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported " + std::to_string(Files.size()) + " BASIC/source asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
#endif
        }

        toolchain::ArtifactExportResult ExportIosSimulator(const toolchain::ArtifactExportRequest& Request)
        {
            toolchain::ArtifactExportResult Result;
#if !defined(__APPLE__)
            (void)Request;
            Result.Error = "iOS Simulator artifacts can only be exported on macOS";
            return Result;
#else
            const fs::path Host = Request.HostResources / "export-hosts/ios-simulator/OpenDBC.app";
            std::error_code FileError;
            if (!fs::is_directory(Host, FileError))
            {
                Result.Error = "The installed product does not contain the qualified iOS Simulator host";
                return Result;
            }
            if (!toolchain::detail::ValidateArtifactHostIdentity(
                    Host.parent_path() / "opendbc-host.json", Host / "opendbc",
                    Request.HostResources / "opendbc-runtime.json", Result.Error))
                return Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + ".app");
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-ios-simulator-stage-", Stage, Result.Error))
                return Result;
            struct Cleanup
            {
                fs::path Path;
                ~Cleanup()
                {
                    std::error_code Ignored;
                    fs::remove_all(Path, Ignored);
                }
            } Cleanup{Stage};
            const fs::path App = Stage / (ProductName + ".app");
            std::vector<toolchain::detail::ArtifactFile> Files;
            if (!PrepareIosApplication(Request, Host, App, ProductName, true, Files, Result.Error))
                return Result;
            if (!PublishAndRemoveStage(App, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported " + std::to_string(Files.size()) + " BASIC/source asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
#endif
        }

        toolchain::ArtifactExportResult ExportAndroid(const toolchain::ArtifactExportRequest& Request, bool Bundle)
        {
            toolchain::ArtifactExportResult Result;
            const fs::path Host = Request.HostResources / "export-hosts/android/template";
            std::error_code FileError;
            if (!fs::is_directory(Host, FileError))
            {
                Result.Error = "The installed product does not contain the qualified Android export host";
                return Result;
            }
            const fs::path ShaderRoot = Host / "app/src/main/assets/shaders";
            if (!toolchain::detail::ValidateArtifactShaders(ShaderRoot, Request.HostResources / "shaders",
                                                            Result.Error))
                return Result;
            if (!ValidateAndroidHostLibrary(Host / "app/src/main/jniLibs/arm64-v8a/libmain.so", Result.Error))
                return Result;
            if (!toolchain::detail::ValidateArtifactHostIdentity(
                    Host / "app/src/main/assets/opendbc-host.json", Host / "app/src/main/jniLibs/arm64-v8a/libmain.so",
                    Request.HostResources / "opendbc-runtime.json", Result.Error))
                return Result;
            if (!toolchain::detail::ValidateAndroidSystemDependencies(Host / "app/src/main/assets/opendbc-host.json",
                                                                      Result.Error))
                return Result;
            const std::string ProductName = SafeProductName(Request.Settings, Request.EntrySource);
            Result.OutputPath = Request.OutputDirectory / (ProductName + (Bundle ? ".aab" : ".apk"));
            if (fs::exists(Result.OutputPath, FileError))
            {
                Result.Error = "Export destination already exists: " + Result.OutputPath.string();
                Result.OutputPath.clear();
                return Result;
            }
            fs::path Stage;
            if (!CreateStageDirectory(Request.OutputDirectory, ".opendbc-android-stage-", Stage, Result.Error))
                return Result;
            struct Cleanup
            {
                fs::path Path;
                ~Cleanup()
                {
                    std::error_code Ignored;
                    fs::remove_all(Path, Ignored);
                }
            } Cleanup{Stage};
            const fs::path Project = Stage / "project";
            if (!CopyTree(Host, Project, Result.Error))
                return Result;
            const fs::path Injection = Project / "app/src/main/assets";
            fs::path RelativeEntry;
            std::vector<fs::path> RelativeSources;
            fs::path RelativeAssetRoot;
            std::vector<toolchain::detail::ArtifactFile> Files;
            if (!CopyPayload(Request, Injection / "artifact", RelativeEntry, RelativeAssetRoot, RelativeSources, Files,
                             Result.Error) ||
                !WriteManifest(Request, Injection, RelativeEntry, RelativeAssetRoot, RelativeSources, Files,
                               Result.Error))
                return Result;

            const std::string BundleId =
                Request.Settings.BundleId.empty() ? "net.demonixis.opendbc.artifact" : Request.Settings.BundleId;
            if (!IsBundleIdentifier(BundleId))
            {
                Result.Error = "Android bundle ID must contain at least two valid Java identifier segments";
                return Result;
            }
            static const std::set<std::string> AndroidOrientations = {"sensorLandscape", "landscape", "sensorPortrait",
                                                                      "portrait", "fullSensor"};
            if (!AndroidOrientations.contains(Request.Settings.AndroidOrientation))
            {
                Result.Error = "Unsupported Android screen orientation";
                return Result;
            }
            static const std::set<std::string> AndroidPermissions = {"INTERNET",
                                                                     "CAMERA",
                                                                     "RECORD_AUDIO",
                                                                     "ACCESS_FINE_LOCATION",
                                                                     "READ_EXTERNAL_STORAGE",
                                                                     "WRITE_EXTERNAL_STORAGE",
                                                                     "VIBRATE"};
            if (std::any_of(Request.Settings.AndroidPermissions.begin(), Request.Settings.AndroidPermissions.end(),
                            [](const std::string& Permission) { return !AndroidPermissions.contains(Permission); }))
            {
                Result.Error = "Unsupported Android permission";
                return Result;
            }
            constexpr int AndroidHostMinSdk = 29;
            constexpr int AndroidCompileSdk = 36;
            if (Request.Settings.VersionName.empty() || Request.Settings.VersionName.size() > 64 ||
                Request.Settings.VersionName.find_first_of("\r\n") != std::string::npos ||
                Request.Settings.VersionCode < 1 || Request.Settings.AndroidMinSdk < AndroidHostMinSdk ||
                Request.Settings.AndroidTargetSdk < Request.Settings.AndroidMinSdk ||
                Request.Settings.AndroidTargetSdk > AndroidCompileSdk)
            {
                Result.Error = "Android settings require minSdk 29 or newer and targetSdk no newer than 36";
                return Result;
            }
            fs::path Keystore;
            const bool UseSigning = Request.Sign && !Request.Settings.AndroidKeystorePath.empty();
            if (Request.Sign && !UseSigning)
            {
                Result.Error = "Android release export requires a configured keystore";
                return Result;
            }
            if (UseSigning)
            {
                Keystore = Request.Settings.AndroidKeystorePath;
                if (Keystore.is_relative())
                    Keystore = Request.ProjectRoot / Keystore;
                const fs::file_status KeystoreStatus = fs::symlink_status(Keystore, FileError);
                if (FileError || !fs::is_regular_file(KeystoreStatus) || fs::is_symlink(KeystoreStatus) ||
                    Request.Settings.AndroidKeyAlias.empty())
                {
                    Result.Error = "Android signing requires a regular non-symlink keystore and a key alias";
                    return Result;
                }
                Keystore = fs::weakly_canonical(Keystore, FileError);
                if (FileError)
                {
                    Result.Error = "Cannot resolve the Android keystore";
                    return Result;
                }
            }
            if (!PrepareAndroidIcon(Request, Project, Result.Error))
                return Result;
            std::ofstream Properties(Project / "artifact.properties", std::ios::binary | std::ios::trunc);
            if (!Properties)
            {
                Result.Error = "Cannot write Android artifact properties";
                return Result;
            }
            Properties << "applicationId=" << PropertyValue(BundleId) << '\n'
                       << "versionName=" << PropertyValue(Request.Settings.VersionName) << '\n'
                       << "versionCode=" << Request.Settings.VersionCode << '\n'
                       << "minSdk=" << Request.Settings.AndroidMinSdk << '\n'
                       << "targetSdk=" << Request.Settings.AndroidTargetSdk << '\n'
                       << "keystorePath=" << PropertyValue(UseSigning ? Keystore.string() : std::string{}) << '\n'
                       << "keyAlias=" << PropertyValue(UseSigning ? Request.Settings.AndroidKeyAlias : std::string{})
                       << '\n';
            if (!Properties.good())
            {
                Result.Error = "Cannot finish Android artifact properties";
                return Result;
            }
            Properties.close();

            std::ofstream Manifest(Project / "app/src/main/AndroidManifest.xml", std::ios::binary | std::ios::trunc);
            if (!Manifest)
            {
                Result.Error = "Cannot write the Android artifact manifest";
                return Result;
            }
            Manifest << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                     << "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n";
            for (const std::string& Permission : Request.Settings.AndroidPermissions)
                Manifest << "  <uses-permission android:name=\"android.permission." << Permission << "\"/>\n";
            Manifest << "  <uses-feature android:name=\"android.hardware.vulkan.level\" android:version=\"0\" "
                        "android:required=\"true\"/>\n"
                     << "  <application android:label=\"" << XmlValue(ProductName)
                     << "\" android:hasCode=\"true\" "
                        "android:allowBackup=\"false\" android:icon=\"@mipmap/ic_launcher\">\n"
                     << "    <activity android:name=\"net.demonixis.opendbc.host.OpenDbcActivity\" "
                        "android:configChanges=\"orientation|screenSize|screenLayout|keyboard|keyboardHidden\" "
                     << "android:screenOrientation=\"" << Request.Settings.AndroidOrientation
                     << "\" android:exported=\"true\">\n"
                     << "      <intent-filter><action android:name=\"android.intent.action.MAIN\"/>"
                        "<category android:name=\"android.intent.category.LAUNCHER\"/></intent-filter>\n"
                     << "    </activity>\n  </application>\n</manifest>\n";
            if (!Manifest.good())
            {
                Result.Error = "Cannot finish the Android artifact manifest";
                return Result;
            }
            Manifest.close();

            if (UseSigning && (std::getenv("OPENDBC_ANDROID_KEYSTORE_PASSWORD") == nullptr ||
                               std::getenv("OPENDBC_ANDROID_KEY_PASSWORD") == nullptr))
            {
                Result.Error = "Android signing requires OPENDBC_ANDROID_KEYSTORE_PASSWORD and "
                               "OPENDBC_ANDROID_KEY_PASSWORD";
                return Result;
            }

#if defined(_WIN32)
            const fs::path Gradle = Project / "gradlew.bat";
#else
            const fs::path Gradle = Project / "gradlew";
#endif
            const std::string Task = Bundle ? ":app:bundleRelease" : ":app:assembleRelease";
            if (!RunProcess({Gradle.string(), "-p", Project.string(), "--no-daemon", "--max-workers=2", Task},
                            Result.Error))
                return Result;
            const fs::path PublishSource =
                Project / (Bundle ? "app/build/outputs/bundle/release/app-release.aab"
                                  : (!UseSigning ? "app/build/outputs/apk/release/app-release-unsigned.apk"
                                                 : "app/build/outputs/apk/release/app-release.apk"));
            if (!fs::is_regular_file(PublishSource, FileError))
            {
                Result.Error = "Android packaging completed without producing the expected artifact";
                return Result;
            }
            if (Request.Settings.AndroidKeystorePath.empty() || !Request.Sign)
                Result.Messages.push_back("Android artifact is unsigned; configure a keystore for release signing");
            if (!PublishAndRemoveStage(PublishSource, Result.OutputPath, Stage, Result.Error))
            {
                Result.OutputPath.clear();
                return Result;
            }
            Cleanup.Path.clear();
            Result.Messages.push_back("Exported " + std::to_string(Files.size()) + " BASIC/source asset files");
            Result.Messages.push_back("Published " + Result.OutputPath.string());
            return Result;
        }
    } // namespace

    namespace toolchain
    {
        std::optional<std::string> detail::ArtifactSha256File(const std::filesystem::path& Path)
        {
            return Sha256File(Path);
        }

        bool detail::ValidateArtifactHostIdentity(const std::filesystem::path& HostManifest,
                                                  const std::filesystem::path& HostBinary,
                                                  const std::filesystem::path& ProductRuntimeManifest,
                                                  std::string& Error)
        {
            std::ifstream Input(HostManifest, std::ios::binary);
            const nlohmann::json Manifest = nlohmann::json::parse(Input, nullptr, false);
            const std::optional<std::string> RuntimeHash = Sha256File(ProductRuntimeManifest);
            const std::optional<std::string> BinaryHash = Sha256File(HostBinary);
            if (!Input || Manifest.is_discarded() || !Manifest.is_object() ||
                (!Manifest.contains("format") || !Manifest["format"].is_number_integer() || Manifest["format"] != 1) ||
                !Manifest.contains("runtime_manifest_sha256") || !Manifest["runtime_manifest_sha256"].is_string() ||
                !Manifest.contains("binary_sha256") || !Manifest["binary_sha256"].is_string() || !RuntimeHash ||
                !BinaryHash || Manifest["runtime_manifest_sha256"].get<std::string>() != *RuntimeHash ||
                Manifest["binary_sha256"].get<std::string>() != *BinaryHash)
            {
                Error = "Artifact host was not built from the current OpenDBC runtime sources";
                return false;
            }
            return true;
        }

        bool detail::ValidateArtifactShaders(const std::filesystem::path& HostShaderRoot,
                                             const std::filesystem::path& ProductShaderRoot, std::string& Error)
        {
            for (const std::string_view Name : {"dbclassic_tri.vert.spv", "dbclassic_tri.frag.spv",
                                                "vr_transfer.vert.spv", "vr_transfer.frag.spv"})
            {
                const std::optional<std::string> HostHash = Sha256File(HostShaderRoot / Name);
                const std::optional<std::string> ProductHash = Sha256File(ProductShaderRoot / Name);
                if (!HostHash || !ProductHash || *HostHash != *ProductHash)
                {
                    Error = "Artifact host does not contain the canonical shaders shipped by OpenDBC";
                    return false;
                }
            }
            return true;
        }

        bool detail::ValidateAndroidSystemDependencies(const std::filesystem::path& HostManifest, std::string& Error)
        {
            std::ifstream Input(HostManifest, std::ios::binary);
            const nlohmann::json Manifest = nlohmann::json::parse(Input, nullptr, false);
            if (!Input || Manifest.is_discarded() || !Manifest.is_object() || !Manifest.contains("needed") ||
                !Manifest["needed"].is_array())
            {
                Error = "Android artifact host has no dynamic dependency inventory";
                return false;
            }
            static const std::set<std::string> Allowed = {"libandroid.so", "libc.so",        "libdl.so", "liblog.so",
                                                          "libm.so",       "libOpenSLES.so", "libz.so"};
            for (const nlohmann::json& Dependency : Manifest["needed"])
            {
                if (!Dependency.is_string() || !Allowed.contains(Dependency.get<std::string>()))
                {
                    Error = "Android artifact host has a forbidden dynamic dependency";
                    return false;
                }
            }
            return true;
        }

        bool detail::WriteArtifactIconPng(const std::filesystem::path& Source, const std::filesystem::path& Destination,
                                          int Size, bool Opaque, std::string& Error)
        {
            return WriteScaledPng(Source, Destination, Size, Opaque, Error);
        }

        bool detail::ClaimArtifactStageDirectory(const std::filesystem::path& Path, std::string& Error)
        {
            std::error_code FileError;
            if (std::filesystem::create_directory(Path, FileError))
                return true;
            if (!FileError)
                Error = "Artifact staging path already exists";
            else
                Error = "Cannot create artifact staging directory: " + FileError.message();
            return false;
        }

        ArtifactExportResult ArtifactExportService::Export(const ArtifactExportRequest& Request) const
        {
            if (!Request.Settings.LoadError.empty())
                return {{}, {}, Request.Settings.LoadError};
            std::error_code Error;
            if (!fs::is_directory(Request.OutputDirectory, Error))
                return {{}, {}, "Export destination is not a directory: " + Request.OutputDirectory.string()};
            std::string ProgramError;
            if (!ValidateExportProgram(Request, ProgramError))
                return {{}, {}, std::move(ProgramError)};
            switch (Request.Target)
            {
                case ArtifactTarget::Desktop:
                    return ExportDesktop(Request);
                case ArtifactTarget::Ios:
                    return ExportIos(Request);
                case ArtifactTarget::IosSimulator:
                    return ExportIosSimulator(Request);
                case ArtifactTarget::AndroidApk:
                    return ExportAndroid(Request, false);
                case ArtifactTarget::AndroidBundle:
                    return ExportAndroid(Request, true);
            }
            return {{}, {}, "Unknown artifact target"};
        }
    } // namespace toolchain

} // namespace dbc::studio
