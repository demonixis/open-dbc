#include "export_internal.hpp"
#include "interpreter/basic_path.hpp"
#include "interpreter/execution_service.hpp"
#include "interpreter/program_image.hpp"
#include "language/language_profile.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>

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
    namespace toolchain::internal
    {
        constexpr std::string_view ProgramImageName = "program.obc";

        void ExportContext::Note(std::string_view Line) const
        {
            if (Progress != nullptr && Progress->Log)
                Progress->Log(Line);
        }

        bool ExportContext::Cancelled(std::string& Error) const
        {
            if (Progress == nullptr || Progress->Cancel == nullptr || !Progress->Cancel->load())
                return false;
            Error = "Export cancelled";
            return true;
        }

        bool ExportContext::Run(const ProcessRequest& Request, std::string& Error) const
        {
            if (Cancelled(Error))
                return false;
            static const ProcessLineSink Silent;
            ProcessResult Result = RunProcess(Request, Progress != nullptr ? Progress->Log : Silent,
                                              Progress != nullptr ? Progress->Cancel : nullptr, false);
            if (!Result)
                Error = std::move(Result.Error);
            return !!Result;
        }

        bool ExportContext::Run(const std::vector<std::string>& Arguments, std::string& Error) const
        {
            return Run(ProcessRequest{Arguments, {}, {}}, Error);
        }

        std::optional<std::string> ExportContext::Capture(const std::vector<std::string>& Arguments,
                                                          std::string& Error) const
        {
            if (Cancelled(Error))
                return std::nullopt;
            ProcessResult Result = RunProcess(ProcessRequest{Arguments, {}, {}}, {},
                                              Progress != nullptr ? Progress->Cancel : nullptr, true);
            if (!Result)
            {
                Error = std::move(Result.Error);
                return std::nullopt;
            }
            return std::move(Result.Output);
        }

        std::optional<std::string> ExportContext::RunCapturing(const ProcessRequest& Request, std::string& Error) const
        {
            if (Cancelled(Error))
                return std::nullopt;
            static const ProcessLineSink Silent;
            ProcessResult Result = RunProcess(Request, Progress != nullptr ? Progress->Log : Silent,
                                              Progress != nullptr ? Progress->Cancel : nullptr, true);
            if (!Result)
            {
                Error = std::move(Result.Error);
                return std::nullopt;
            }
            return std::move(Result.Output);
        }

        StageCleanup::~StageCleanup()
        {
            std::error_code Ignored;
            fs::remove_all(Path, Ignored);
        }

        std::string Lower(std::string Value)
        {
            std::transform(Value.begin(), Value.end(), Value.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Value;
        }

        namespace
        {
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
                        const std::uint32_t S0 = std::rotr(Words[Index - 15], 7) ^ std::rotr(Words[Index - 15], 18) ^
                                                 (Words[Index - 15] >> 3);
                        const std::uint32_t S1 = std::rotr(Words[Index - 2], 17) ^ std::rotr(Words[Index - 2], 19) ^
                                                 (Words[Index - 2] >> 10);
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
        } // namespace

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

        static bool IsExcludedDirectory(const fs::path& Path)
        {
            const std::string Name = Lower(dbc::PathToUtf8(Path.filename()));
            return Name == ".git" || Name == ".svn" || Name == ".hg" || Name == ".idea" || Name == ".vs" ||
                   Name == ".codex" || Name == "node_modules" || Name == "exports" || Name == "export" ||
                   Name == "dist" || Name == "out" || Name == "cmakefiles" || Name == "__pycache__" ||
                   Name == "build" || Name.starts_with("build-") || Name.starts_with(".opendbc-") ||
                   Path.extension() == ".app";
        }

        static bool IsToolingSource(const fs::path& Path)
        {
            static const std::set<std::string> Extensions = {
                ".c",   ".cc",      ".cpp",     ".cxx",       ".h",        ".hh",  ".hpp",   ".hxx",
                ".m",   ".mm",      ".swift",   ".java",      ".kt",       ".kts", ".cs",    ".go",
                ".rs",  ".py",      ".rb",      ".sh",        ".bat",      ".ps1", ".cmake", ".gradle",
                ".sln", ".vcxproj", ".pbxproj", ".xcodeproj", ".expected", ".apk", ".aab",   ".ipa"};
            const std::string Name = Lower(dbc::PathToUtf8(Path.filename()));
            return Name == "cmakelists.txt" || Name == "makefile" || Name == ".gitignore" || Name == ".gitattributes" ||
                   Extensions.contains(Lower(dbc::PathToUtf8(Path.extension())));
        }

        static bool IsGeneratedRuntimeOutput(const fs::path& Path)
        {
            const std::string Name = Lower(dbc::PathToUtf8(Path.filename()));
            const std::string Extension = Lower(dbc::PathToUtf8(Path.extension()));
            return Name == "open-dbc.log" || (Name.starts_with("open-dbc.") && Extension == ".log") ||
                   (Name.starts_with("open-dbc-crash-") && Extension == ".log") || Name == "open-dbc-dump-scene.txt" ||
                   Name == "open-dbc-dump-renderer.txt" || Name == "selftest-final.png";
        }

        static bool IsSensitive(const fs::path& Path)
        {
            const std::string Name = Lower(dbc::PathToUtf8(Path.filename()));
            const std::string Extension = Lower(dbc::PathToUtf8(Path.extension()));
            return Name == ".env" || Name.starts_with(".env.") || Name.find("credential") != std::string::npos ||
                   Name.find("password") != std::string::npos || Name.find("passwd") != std::string::npos ||
                   Name.find("secret") != std::string::npos || Name.find("token") != std::string::npos ||
                   Extension == ".pem" || Extension == ".key" || Extension == ".p12" || Extension == ".pfx" ||
                   Extension == ".jks" || Extension == ".keystore";
        }

        static bool IsProjectMetadata(const fs::path& Path)
        {
            return Lower(dbc::PathToUtf8(Path.filename())) == ".opendbc-project.json";
        }

        // Exports ship the program image they compile, never BASIC sources, project descriptors or
        // other images.
        static bool IsBasicSource(const fs::path& Path)
        {
            const std::string Extension = Lower(dbc::PathToUtf8(Path.extension()));
            return Extension == ".dba" || Extension == ".dbpro" || Extension == ".obc";
        }

        static bool IsSafeRelative(const fs::path& Path)
        {
            if (Path.empty() || Path.is_absolute() || Path.has_root_path())
                return false;
            return std::none_of(Path.begin(), Path.end(),
                                [](const fs::path& Part) { return Part == ".." || Part == "."; });
        }

        static bool IsSameOrDescendant(const fs::path& Path, const fs::path& Root)
        {
            if (Path == Root)
                return true;
            const fs::path Relative = Path.lexically_relative(Root);
            return !Relative.empty() && *Relative.begin() != "..";
        }

        std::string SafeProductName(const core::ProjectSettings& Settings, const fs::path& Entry)
        {
            std::string Value = Settings.AppName.empty() ? dbc::PathToUtf8(Entry.stem()) : Settings.AppName;
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

        static std::string StageSuffix()
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
                const detail::ArtifactStageClaim Claim = detail::ClaimArtifactStageDirectory(Stage, Error);
                if (Claim != detail::ArtifactStageClaim::AlreadyExists)
                    return Claim == detail::ArtifactStageClaim::Claimed;
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
                Error = fs::exists(Destination, FileError) && !FileError
                            ? "Export destination already exists: " + dbc::PathToUtf8(Destination)
                            : "Cannot publish artifact atomically without replacing a destination: " +
                                  dbc::PathToUtf8(Destination);
            }
            return Published;
        }

        bool CopyArtifactFile(const fs::path& Source, const fs::path& Destination, std::string& Error)
        {
            std::error_code FileError;
            fs::create_directories(Destination.parent_path(), FileError);
            if (FileError)
            {
                Error = "Cannot create artifact directory '" + dbc::PathToUtf8(Destination.parent_path()) +
                        "': " + FileError.message();
                return false;
            }
            fs::copy_file(Source, Destination, fs::copy_options::overwrite_existing, FileError);
            if (FileError)
            {
                Error = "Cannot copy '" + dbc::PathToUtf8(Source) + "': " + FileError.message();
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
                Error = "Artifact host directory does not exist: " + dbc::PathToUtf8(Source);
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
                    Error = "Artifact hosts cannot contain symlinks: " + dbc::PathToUtf8(It->path());
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
                else if (fs::is_regular_file(Status) && !CopyArtifactFile(It->path(), Target, Error))
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

        bool RemoveRequired(const fs::path& Path, std::string& Error)
        {
            std::error_code FileError;
            std::filesystem::remove_all(Path, FileError);
            if (FileError || std::filesystem::exists(Path, FileError))
            {
                Error = "Cannot remove artifact staging content: " + dbc::PathToUtf8(Path);
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

        static std::optional<std::string> HashFile(const fs::path& Path)
        {
            return dbc::interpreter::detail::ArtifactContentHashFile(Path);
        }

        // Compiles the project in place. Its ordered sources may live anywhere, but #INCLUDE files read
        // from disk must stay inside the project.
        bool BuildExportProgram(const ArtifactExportRequest& Request, std::vector<std::uint8_t>& Image,
                                std::string& Error)
        {
            if (!dbc::language::IsLanguageSource(Request.EntrySource))
            {
                Error = "BASIC artifact entry must use the .dba extension";
                return false;
            }
            dbc::interpreter::ExecutionRequest BuildRequest;
            BuildRequest.RootLogicalPath = Request.EntrySource;
            BuildRequest.Profile = Request.Profile;
            BuildRequest.OrderedProjectSources =
                Request.OrderedSources.empty() ? std::vector<fs::path>{Request.EntrySource} : Request.OrderedSources;
            for (const fs::path& Source : BuildRequest.OrderedProjectSources)
            {
                std::string LoadError;
                std::optional<dbc::interpreter::SourceUnit> Unit =
                    dbc::interpreter::LoadSourceUnitFromDisk(Source, Request.Profile, LoadError);
                if (!Unit)
                {
                    Error = std::move(LoadError);
                    return false;
                }
                BuildRequest.Sources.push_back(std::move(*Unit));
            }
            BuildRequest.LoadMissingSource =
                [Root = Request.ProjectRoot](const fs::path& Path, dbc::language::LanguageProfile Profile,
                                             std::string& LoadError) -> std::optional<dbc::interpreter::SourceUnit>
            {
                fs::path Canonical;
                if (!dbc::interpreter::ResolveConfinedBasicPath(Root, Path, Canonical, &LoadError))
                {
                    LoadError = "#INCLUDE in an exported artifact must remain inside its project: " + LoadError;
                    return std::nullopt;
                }
                return dbc::interpreter::LoadSourceUnitFromDisk(Path, Profile, LoadError);
            };
            const dbc::interpreter::ProgramBuild Build = dbc::interpreter::BuildProgram(std::move(BuildRequest));
            if (!Build.Bytecode)
            {
                const auto First = std::find_if(Build.Diagnostics.begin(), Build.Diagnostics.end(),
                                                [](const dbc::interpreter::ExecutionDiagnostic& Value)
                                                { return Value.Severity == dbc::language::DiagnosticSeverity::Error; });
                Error = First == Build.Diagnostics.end() ? "BASIC artifact compilation failed"
                                                         : First->Code + ": " + First->Message;
                return false;
            }
            Image = dbc::interpreter::WriteProgramImage(*Build.Bytecode, Request.Startup, Request.ProjectRoot);
            return true;
        }

        std::optional<std::string> ReadFile(const fs::path& Path)
        {
            std::ifstream Input(Path, std::ios::binary);
            if (!Input)
                return std::nullopt;
            return std::string(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
        }

        static bool CopyPayload(const ArtifactExportRequest& Request, const fs::path& PayloadRoot,
                                fs::path& RelativeAssetRoot, std::vector<dbc::interpreter::detail::ArtifactFile>& Files,
                                std::string& Error)
        {
            std::error_code FileError;
            const fs::path ProjectRoot = fs::weakly_canonical(Request.ProjectRoot, FileError);
            if (FileError || !fs::is_directory(ProjectRoot, FileError))
            {
                Error = "Project root does not exist: " + dbc::PathToUtf8(Request.ProjectRoot);
                return false;
            }
            const fs::path Entry = fs::weakly_canonical(Request.EntrySource, FileError);
            if (FileError || !fs::is_regular_file(Entry, FileError))
            {
                Error = "BASIC entry does not exist: " + dbc::PathToUtf8(Request.EntrySource);
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
            if (Lower(dbc::PathToUtf8(Entry.extension())) != ".dba")
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
                fs::path Keystore =
                    dbc::interpreter::NormalizeBasicPath(dbc::PathFromUtf8(Request.Settings.AndroidKeystorePath));
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
                    Error = "Projects containing symlinks cannot be exported: " + dbc::PathToUtf8(It->path());
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
                    IsGeneratedRuntimeOutput(It->path()) || IsBasicSource(It->path()))
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
                            dbc::PathToUtf8(It->path());
                    return false;
                }
                const fs::path Relative = fs::relative(It->path(), ProjectRoot, FileError);
                if (FileError || !IsSafeRelative(Relative))
                {
                    Error = "Project contains an unsafe relative path";
                    return false;
                }
                if (!CopyArtifactFile(It->path(), PayloadRoot / Relative, Error))
                    return false;
                const std::optional<std::string> Hash = HashFile(PayloadRoot / Relative);
                if (!Hash)
                {
                    Error = "Cannot hash copied artifact payload file: " + dbc::PathToUtf8((PayloadRoot / Relative));
                    return false;
                }
                Files.push_back({Relative, *Hash});
            }
            if (FileError)
            {
                Error = "Cannot inspect project files: " + FileError.message();
                return false;
            }
            const fs::path AssetRoot =
                fs::weakly_canonical(Request.AssetRoot.empty() ? ProjectRoot : Request.AssetRoot, FileError);
            if (FileError || !fs::is_directory(AssetRoot, FileError))
            {
                Error = "Artifact asset root does not exist: " + dbc::PathToUtf8(Request.AssetRoot);
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
                        Error = "Remapped DBPro media cannot contain symlinks: " + dbc::PathToUtf8(It->path());
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
                    if (IsGeneratedRuntimeOutput(It->path()) || IsBasicSource(It->path()))
                        continue;
                    if (ConfiguredKeystore && It->path().lexically_normal() == *ConfiguredKeystore)
                        continue;
                    if (IsSensitive(It->path()))
                    {
                        Error = "Remapped DBPro media contains a file that may expose secrets: " +
                                dbc::PathToUtf8(It->path());
                        return false;
                    }
                    const fs::path Destination = RelativeAssetRoot / Relative;
                    if (!CopyArtifactFile(It->path(), PayloadRoot / Destination, Error))
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
            std::sort(
                Files.begin(), Files.end(), [](const auto& Left, const auto& Right)
                { return dbc::GenericPathToUtf8(Left.RelativePath) < dbc::GenericPathToUtf8(Right.RelativePath); });
            return true;
        }

        static bool WriteManifest(const ArtifactExportRequest& Request, const fs::path& ResourceRoot,
                                  const fs::path& RelativeAssetRoot,
                                  const std::vector<dbc::interpreter::detail::ArtifactFile>& Files, std::string& Error)
        {
            nlohmann::json Manifest;
            Manifest["format"] = dbc::interpreter::EmbeddedArtifactFormat;
            Manifest["program"] = ProgramImageName;
            Manifest["assetRoot"] = dbc::GenericPathToUtf8(RelativeAssetRoot);
            const std::string StorageSeed =
                Request.Settings.BundleId.empty()
                    ? dbc::GenericPathToUtf8(fs::absolute(Request.ProjectRoot).lexically_normal())
                    : Request.Settings.BundleId;
            Manifest["storage"] = "artifact-" + dbc::interpreter::detail::ArtifactContentHash(StorageSeed);
            Manifest["files"] = nlohmann::json::array();
            for (const dbc::interpreter::detail::ArtifactFile& File : Files)
                Manifest["files"].push_back(
                    {{"path", dbc::GenericPathToUtf8(File.RelativePath)}, {"hash", File.ContentHash}});
            std::ofstream Output(ResourceRoot / dbc::interpreter::EmbeddedArtifactManifestName,
                                 std::ios::binary | std::ios::trunc);
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

        // Writes program.obc, the assets and the manifest into ResourceRoot.
        bool WriteArtifact(const ArtifactExportRequest& Request, const std::vector<std::uint8_t>& Image,
                           const fs::path& ResourceRoot, std::vector<dbc::interpreter::detail::ArtifactFile>& Files,
                           std::string& Error)
        {
            fs::path RelativeAssetRoot;
            if (!CopyPayload(Request, ResourceRoot / "artifact", RelativeAssetRoot, Files, Error))
                return false;
            // The image travels with the payload to writable storage, where the run log is written too.
            const std::string_view Bytes(reinterpret_cast<const char*>(Image.data()), Image.size());
            std::ofstream Output(ResourceRoot / "artifact" / ProgramImageName, std::ios::binary | std::ios::trunc);
            Output.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
            Output.close();
            if (!Output)
            {
                Error = "Cannot write the artifact program image";
                return false;
            }
            Files.push_back(
                {dbc::PathFromUtf8(ProgramImageName), dbc::interpreter::detail::ArtifactContentHash(Bytes)});
            std::sort(
                Files.begin(), Files.end(), [](const auto& Left, const auto& Right)
                { return dbc::GenericPathToUtf8(Left.RelativePath) < dbc::GenericPathToUtf8(Right.RelativePath); });
            return WriteManifest(Request, ResourceRoot, RelativeAssetRoot, Files, Error);
        }

        std::optional<fs::path> ResolveArtifactIcon(const ArtifactExportRequest& Request, bool Required,
                                                    std::string& Error)
        {
            fs::path Source;
            if (!Request.Settings.IconSource.empty())
            {
                Source = dbc::PathFromUtf8(Request.Settings.IconSource);
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
            if (Lower(dbc::PathToUtf8(Source.extension())) != ".png")
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

        bool WriteScaledPng(const ExportContext& Context, const fs::path& Source, const fs::path& Destination, int Size,
                            bool Opaque, std::string& Error)
        {
            SDL_Surface* Original = IMG_Load(dbc::PathToUtf8(Source).c_str());
            if (Original == nullptr)
            {
                Error = "Cannot load artifact icon: " + dbc::PathToUtf8(Source);
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
                Error = "Cannot write artifact icon: " + dbc::PathToUtf8(Destination);
                return false;
            }
#if defined(__APPLE__)
            if (Opaque)
            {
                fs::path Bitmap = Destination;
                Bitmap += ".opendbc.bmp";
                if (!SDL_SaveBMP(SurfaceToSave, dbc::PathToUtf8(Bitmap).c_str()) ||
                    !Context.Run({"/usr/bin/sips", "-s", "format", "png", dbc::PathToUtf8(Bitmap), "--out",
                                  dbc::PathToUtf8(Destination)},
                                 Error))
                {
                    std::error_code Ignored;
                    fs::remove(Bitmap, Ignored);
                    if (Error.empty())
                        Error = "Cannot write opaque artifact icon: " + dbc::PathToUtf8(Destination);
                    return false;
                }
                return RemoveRequired(Bitmap, Error);
            }
#else
            (void)Context;
            if (Opaque)
            {
                Error = "Opaque iOS artifact icons can only be produced on macOS";
                return false;
            }
#endif
            if (!IMG_SavePNG(SurfaceToSave, dbc::PathToUtf8(Destination).c_str()))
            {
                Error = "Cannot write artifact icon: " + dbc::PathToUtf8(Destination);
                return false;
            }
            return true;
        }

#if defined(__APPLE__)
        bool AdHocSignAppleBundle(const ExportContext& Context, const fs::path& Bundle, std::string& Error)
        {
            return Context.Run(
                {"/usr/bin/codesign", "--force", "--deep", "--sign", "-", "--timestamp=none", dbc::PathToUtf8(Bundle)},
                Error);
        }

        bool SetAppleMetadata(const ExportContext& Context, const fs::path& App, const core::ProjectSettings& Settings,
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
            const auto ReplaceString = [&Context, &Plist, &Error](std::string_view Key, std::string_view Value)
            {
                return Context.Run({"/usr/bin/plutil", "-replace", std::string(Key), "-string", std::string(Value),
                                    dbc::PathToUtf8(Plist)},
                                   Error);
            };
            if (!ReplaceString("CFBundleIdentifier", BundleId) || !ReplaceString("CFBundleDisplayName", ProductName) ||
                !ReplaceString("CFBundleName", ProductName) ||
                !ReplaceString("CFBundleShortVersionString", Settings.VersionName) ||
                !ReplaceString("CFBundleVersion", std::to_string(Settings.VersionCode)))
                return false;
            return true;
        }
#endif

#if defined(__APPLE__)
        static bool IsMachO(const fs::path& Path)
        {
            std::array<unsigned char, 4> Magic{};
            std::ifstream Input(Path, std::ios::binary);
            Input.read(reinterpret_cast<char*>(Magic.data()), static_cast<std::streamsize>(Magic.size()));
            if (Input.gcount() != static_cast<std::streamsize>(Magic.size()))
                return false;
            const std::uint32_t Value = (std::uint32_t{Magic[0]} << 24) | (std::uint32_t{Magic[1]} << 16) |
                                        (std::uint32_t{Magic[2]} << 8) | std::uint32_t{Magic[3]};
            return Value == 0xfeedfacfu || Value == 0xcffaedfeu || Value == 0xfeedfaceu || Value == 0xcefaedfeu ||
                   Value == 0xcafebabeu || Value == 0xbebafecau;
        }

        bool SignMacDistribution(const ExportContext& Context, const fs::path& App, const std::string& Identity,
                                 std::string& Error)
        {
            if (Identity.empty())
            {
                Error = "macOS distribution builds need a Developer ID Application identity";
                return false;
            }
            // Plugins loaded by LOAD DLL, OpenXR runtimes and MoltenVK come from other teams or are unsigned;
            // RECORD SOUND needs the microphone under the hardened runtime.
            const fs::path Entitlements = App.parent_path() / ".opendbc-entitlements.plist";
            {
                std::ofstream Output(Entitlements, std::ios::binary | std::ios::trunc);
                Output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                          "<plist version=\"1.0\"><dict>\n"
                          "  <key>com.apple.security.cs.disable-library-validation</key><true/>\n"
                          "  <key>com.apple.security.device.audio-input</key><true/>\n"
                          "</dict></plist>\n";
                if (!Output)
                {
                    Error = "Cannot write the macOS entitlements";
                    return false;
                }
            }
            const StageCleanup EntitlementsCleanup{Entitlements};
            // Nested code is signed first: signing the bundle seals what it contains.
            const fs::path MainExecutables = App / "Contents/MacOS";
            std::vector<fs::path> Nested;
            std::error_code FileError;
            for (fs::recursive_directory_iterator It(App / "Contents", FileError), End; !FileError && It != End;
                 It.increment(FileError))
            {
                if (It->is_regular_file(FileError) && It->path().parent_path() != MainExecutables &&
                    IsMachO(It->path()))
                    Nested.push_back(It->path());
            }
            if (FileError)
            {
                Error = "Cannot inspect the macOS bundle: " + FileError.message();
                return false;
            }
            const std::vector<std::string> Signing = {"/usr/bin/codesign", "--force", "--timestamp", "--options",
                                                      "runtime",           "--sign",  Identity};
            for (const fs::path& Code : Nested)
            {
                std::vector<std::string> Arguments = Signing;
                Arguments.push_back(dbc::PathToUtf8(Code));
                if (!Context.Run(Arguments, Error))
                    return false;
            }
            std::vector<std::string> Arguments = Signing;
            Arguments.insert(Arguments.end(), {"--entitlements", dbc::PathToUtf8(Entitlements), dbc::PathToUtf8(App)});
            return Context.Run(Arguments, Error) && Context.Run({"/usr/bin/codesign", "--verify", "--strict", "--deep",
                                                                 "--verbose=2", dbc::PathToUtf8(App)},
                                                                Error);
        }

        bool NotarizeMacApp(const ExportContext& Context, const fs::path& App, const std::string& Profile,
                            std::string& Error)
        {
            const fs::path Archive = App.parent_path() / ".opendbc-notarization.zip";
            const StageCleanup ArchiveCleanup{Archive};
            if (!Context.Run(
                    {"/usr/bin/ditto", "-c", "-k", "--keepParent", dbc::PathToUtf8(App), dbc::PathToUtf8(Archive)},
                    Error))
                return false;
            Context.Note("Submitting to Apple notarization; this usually takes a few minutes");
            const std::optional<std::string> Output =
                Context.RunCapturing({{"/usr/bin/xcrun", "notarytool", "submit", dbc::PathToUtf8(Archive),
                                       "--keychain-profile", Profile, "--wait", "--output-format", "json"},
                                      {},
                                      {}},
                                     Error);
            if (!Output)
                return false;
            // notarytool reports a rejected submission in its verdict, not always in its exit code.
            const nlohmann::json Verdict = nlohmann::json::parse(*Output, nullptr, false);
            const std::string Status = Verdict.is_object() ? Verdict.value("status", std::string{}) : std::string{};
            if (Status != "Accepted")
            {
                const std::string Id = Verdict.is_object() ? Verdict.value("id", std::string{}) : std::string{};
                Error = "Apple notarization did not accept the app (" + (Status.empty() ? "no verdict" : Status) + ")" +
                        (Id.empty() ? std::string{}
                                    : ". Details: xcrun notarytool log " + Id + " --keychain-profile " + Profile);
                return false;
            }
            return Context.Run({"/usr/bin/xcrun", "stapler", "staple", dbc::PathToUtf8(App)}, Error) &&
                   Context.Run(
                       {"/usr/sbin/spctl", "--assess", "--type", "execute", "--verbose=2", dbc::PathToUtf8(App)},
                       Error);
        }
#endif
    } // namespace toolchain::internal

    namespace toolchain
    {
        std::optional<std::string> detail::ArtifactSha256File(const std::filesystem::path& Path)
        {
            return internal::Sha256File(Path);
        }

        bool detail::ValidateArtifactHostIdentity(const std::filesystem::path& HostManifest,
                                                  const std::filesystem::path& HostBinary,
                                                  const std::filesystem::path& ProductRuntimeManifest,
                                                  std::string& Error)
        {
            std::ifstream Input(HostManifest, std::ios::binary);
            const nlohmann::json Manifest = nlohmann::json::parse(Input, nullptr, false);
            const std::optional<std::string> RuntimeHash = internal::Sha256File(ProductRuntimeManifest);
            const std::optional<std::string> BinaryHash = internal::Sha256File(HostBinary);
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
            for (const std::string_view Name :
                 {"dbclassic_tri.vert.spv", "dbclassic_tri.frag.spv", "dbclassic_tri.vert.msl",
                  "dbclassic_tri.frag.msl", "dbclassic_mesh.vert.spv", "dbclassic_mesh.vert.msl",
                  "shadow_volume.vert.spv", "shadow_volume.frag.spv", "shadow_volume.vert.msl",
                  "shadow_volume.frag.msl", "vr_transfer.vert.spv", "vr_transfer.frag.spv", "vr_transfer.vert.msl",
                  "vr_transfer.frag.msl"})
            {
                const std::optional<std::string> HostHash = internal::Sha256File(HostShaderRoot / Name);
                const std::optional<std::string> ProductHash = internal::Sha256File(ProductShaderRoot / Name);
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
            return internal::WriteScaledPng(internal::ExportContext{}, Source, Destination, Size, Opaque, Error);
        }

        detail::ArtifactStageClaim detail::ClaimArtifactStageDirectory(const std::filesystem::path& Path,
                                                                       std::string& Error)
        {
            std::error_code FileError;
            if (std::filesystem::create_directory(Path, FileError))
                return ArtifactStageClaim::Claimed;
            if (!FileError)
                return ArtifactStageClaim::AlreadyExists;
            Error = "Cannot create artifact staging directory: " + FileError.message();
            return ArtifactStageClaim::Failed;
        }
    } // namespace toolchain

} // namespace dbc::studio
