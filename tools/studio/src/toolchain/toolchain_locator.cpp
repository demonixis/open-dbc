#include "toolchain_locator.hpp"
#include "path_utf8.hpp"
#include "process_runner.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <system_error>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        // The Android Gradle Plugin the export template uses needs Java 17.
        constexpr int MinimumJavaVersion = 17;

        std::string Executable(std::string_view Name)
        {
#if defined(_WIN32)
            return std::string(Name) + ".exe";
#else
            return std::string(Name);
#endif
        }

        // Reads JAVA_VERSION="21.0.1" (or "1.8.0_402") from the JDK's release file.
        std::optional<int> JavaMajorVersion(const fs::path& JavaHome)
        {
            std::ifstream Input(JavaHome / "release");
            std::string Line;
            while (std::getline(Input, Line))
            {
                constexpr std::string_view Key = "JAVA_VERSION=\"";
                if (!Line.starts_with(Key))
                    continue;
                std::string_view Value = std::string_view(Line).substr(Key.size());
                if (Value.starts_with("1."))
                    Value.remove_prefix(2);
                int Major = 0;
                const auto [End, Error] = std::from_chars(Value.data(), Value.data() + Value.size(), Major);
                if (Error != std::errc{} || End == Value.data())
                    return std::nullopt;
                return Major;
            }
            return std::nullopt;
        }

        // Candidate JDK homes below a folder that holds several JDKs, newest name first.
        std::vector<fs::path> JdkChildren(const fs::path& Root)
        {
            std::vector<fs::path> Children;
            std::error_code Error;
            for (fs::directory_iterator It(Root, Error), End; !Error && It != End; It.increment(Error))
            {
                const fs::path Child = It->path();
                Children.push_back(Child);
                Children.push_back(Child / "Contents/Home");
                Children.push_back(Child / "libexec/openjdk.jdk/Contents/Home");
            }
            std::sort(Children.begin(), Children.end(), std::greater<>());
            return Children;
        }

        std::optional<fs::path> FirstExisting(const std::vector<fs::path>& Candidates, bool (*Valid)(const fs::path&))
        {
            for (const fs::path& Candidate : Candidates)
            {
                if (!Candidate.empty() && Valid(Candidate))
                {
                    std::error_code Error;
                    const fs::path Canonical = fs::weakly_canonical(Candidate, Error);
                    return Error ? Candidate : Canonical;
                }
            }
            return std::nullopt;
        }
    } // namespace

    namespace toolchain
    {
        ToolchainSearch ToolchainSearch::Host()
        {
            ToolchainSearch Search;
            Search.Variable = [](std::string_view Name) -> std::optional<std::string>
            {
                const char* Value = SDL_getenv(std::string(Name).c_str());
                if (Value == nullptr || *Value == '\0')
                    return std::nullopt;
                return std::string(Value);
            };
            const auto Home = Search.Variable("HOME");
#if defined(__APPLE__)
            Search.JdkRoots = {"/Applications/Android Studio.app/Contents/jbr/Contents/Home",
                               "/Library/Java/JavaVirtualMachines", "/opt/homebrew/opt", "/usr/local/opt"};
            if (Home)
                Search.AndroidSdkRoots.push_back(dbc::PathFromUtf8(*Home) / "Library/Android/sdk");
            Search.AndroidSdkRoots.push_back("/opt/homebrew/share/android-commandlinetools");
            Search.AndroidSdkRoots.push_back("/usr/local/share/android-commandlinetools");
#elif defined(_WIN32)
            Search.JdkRoots = {"C:/Program Files/Android/Android Studio/jbr", "C:/Program Files/Java",
                               "C:/Program Files/Eclipse Adoptium", "C:/Program Files/Microsoft"};
            Search.WindowsSdkBinRoots = {"C:/Program Files (x86)/Windows Kits/10/bin"};
            if (const auto LocalAppData = Search.Variable("LOCALAPPDATA"))
                Search.AndroidSdkRoots.push_back(dbc::PathFromUtf8(*LocalAppData) / "Android/Sdk");
#else
            Search.JdkRoots = {"/opt/android-studio/jbr", "/usr/lib/jvm"};
            if (Home)
            {
                Search.JdkRoots.insert(Search.JdkRoots.begin(), dbc::PathFromUtf8(*Home) / "android-studio/jbr");
                Search.AndroidSdkRoots.push_back(dbc::PathFromUtf8(*Home) / "Android/Sdk");
            }
#endif
            return Search;
        }

        bool IsJavaHome(const fs::path& Path)
        {
            std::error_code Error;
            if (!fs::is_regular_file(Path / "bin" / Executable("java"), Error) ||
                !fs::is_regular_file(Path / "bin" / Executable("keytool"), Error))
                return false;
            const std::optional<int> Major = JavaMajorVersion(Path);
            return Major && *Major >= MinimumJavaVersion;
        }

        bool IsAndroidSdk(const fs::path& Path)
        {
            std::error_code Error;
            return fs::is_directory(Path / "platforms", Error) || fs::is_directory(Path / "build-tools", Error);
        }

        std::optional<fs::path> LocateJavaHome(const fs::path& Configured, const ToolchainSearch& Search)
        {
            if (!Configured.empty())
                return IsJavaHome(Configured) ? std::optional<fs::path>(Configured) : std::nullopt;
            std::vector<fs::path> Candidates;
            if (Search.Variable)
            {
                if (const auto JavaHome = Search.Variable("JAVA_HOME"))
                    Candidates.emplace_back(dbc::PathFromUtf8(*JavaHome));
            }
            for (const fs::path& Root : Search.JdkRoots)
            {
                Candidates.push_back(Root);
                const std::vector<fs::path> Children = JdkChildren(Root);
                Candidates.insert(Candidates.end(), Children.begin(), Children.end());
            }
            return FirstExisting(Candidates, IsJavaHome);
        }

        std::optional<fs::path> LocateAndroidSdk(const fs::path& Configured, const ToolchainSearch& Search)
        {
            if (!Configured.empty())
                return IsAndroidSdk(Configured) ? std::optional<fs::path>(Configured) : std::nullopt;
            std::vector<fs::path> Candidates;
            if (Search.Variable)
            {
                for (const std::string_view Name : {"ANDROID_HOME", "ANDROID_SDK_ROOT"})
                {
                    if (const auto Value = Search.Variable(Name))
                        Candidates.emplace_back(dbc::PathFromUtf8(*Value));
                }
            }
            Candidates.insert(Candidates.end(), Search.AndroidSdkRoots.begin(), Search.AndroidSdkRoots.end());
            return FirstExisting(Candidates, IsAndroidSdk);
        }

        std::optional<fs::path> LatestAndroidBuildTools(const fs::path& Sdk)
        {
            std::optional<fs::path> Latest;
            std::vector<int> LatestVersion;
            std::error_code Error;
            for (fs::directory_iterator It(Sdk / "build-tools", Error), End; !Error && It != End; It.increment(Error))
            {
                // Compare 36.0.0 > 35.0.1 numerically; preview folders such as 36.0.0-rc1 rank by their numbers.
                std::vector<int> Version;
                const std::string Name = dbc::PathToUtf8(It->path().filename());
                const char* Cursor = Name.data();
                const char* NameEnd = Name.data() + Name.size();
                while (Cursor < NameEnd)
                {
                    int Part = 0;
                    const auto [Next, ParseError] = std::from_chars(Cursor, NameEnd, Part);
                    if (ParseError != std::errc{})
                        break;
                    Version.push_back(Part);
                    Cursor = Next;
                    if (Cursor < NameEnd && *Cursor == '.')
                        ++Cursor;
                    else
                        break;
                }
                if (!Version.empty() && (!Latest || Version > LatestVersion))
                {
                    Latest = It->path();
                    LatestVersion = std::move(Version);
                }
            }
            return Latest;
        }

        std::vector<SigningIdentity> ParseSigningIdentities(std::string_view Output)
        {
            // Lines look like:   1) <40 hex digits> "Developer ID Application: Name (TEAMID1234)"
            std::vector<SigningIdentity> Identities;
            std::size_t Begin = 0;
            while (Begin < Output.size())
            {
                std::size_t End = Output.find('\n', Begin);
                if (End == std::string_view::npos)
                    End = Output.size();
                const std::string_view Line = Output.substr(Begin, End - Begin);
                Begin = End + 1;
                const std::size_t Paren = Line.find(") ");
                const std::size_t Quote = Line.find('"');
                const std::size_t Close = Line.rfind('"');
                if (Paren == std::string_view::npos || Quote == std::string_view::npos || Close <= Quote)
                    continue;
                const std::string_view Sha1 = Line.substr(Paren + 2, Quote > Paren + 3 ? Quote - Paren - 3 : 0);
                if (Sha1.size() != 40)
                    continue;
                SigningIdentity Identity{std::string(Sha1), std::string(Line.substr(Quote + 1, Close - Quote - 1))};
                const bool Duplicate = std::any_of(Identities.begin(), Identities.end(),
                                                   [&Identity](const SigningIdentity& Known)
                                                   { return Known.Sha1 == Identity.Sha1; });
                if (!Duplicate)
                    Identities.push_back(std::move(Identity));
            }
            return Identities;
        }

        std::vector<SigningIdentity> ListSigningIdentities()
        {
#if defined(__APPLE__)
            const ProcessResult Result =
                RunProcess({{"/usr/bin/security", "find-identity", "-v", "-p", "codesigning"}, {}, {}}, {}, nullptr, true);
            return Result ? ParseSigningIdentities(Result.Output) : std::vector<SigningIdentity>{};
#else
            return {};
#endif
        }

        std::optional<std::string> MacMinimumSystem(const fs::path& Binary)
        {
#if defined(__APPLE__)
            const ProcessResult Result = RunProcess(
                {{"/usr/bin/xcrun", "vtool", "-show-build", dbc::PathToUtf8(Binary)}, {}, {}}, {}, nullptr, true);
            const std::size_t At = Result ? Result.Output.find("minos ") : std::string::npos;
            if (At == std::string::npos)
                return std::nullopt;
            const std::size_t Begin = At + 6;
            const std::size_t End = Result.Output.find_first_of(" \n", Begin);
            return Result.Output.substr(Begin, End == std::string::npos ? std::string::npos : End - Begin);
#else
            (void)Binary;
            return std::nullopt;
#endif
        }

        std::optional<fs::path> LocateSigntool(const ToolchainSearch& Search)
        {
            if (Search.Variable)
            {
                if (const auto Configured = Search.Variable("OPENDBC_SIGNTOOL"))
                    return dbc::PathFromUtf8(*Configured);
            }
            std::optional<fs::path> Latest;
            for (const fs::path& Root : Search.WindowsSdkBinRoots)
            {
                std::error_code Error;
                for (fs::directory_iterator It(Root, Error), End; !Error && It != End; It.increment(Error))
                {
                    const fs::path Candidate = It->path() / "x64" / "signtool.exe";
                    // SDK folders are named 10.0.<build>.0, so the name order is the version order.
                    std::error_code FileError;
                    if (fs::is_regular_file(Candidate, FileError) &&
                        (!Latest || It->path().filename() > Latest->parent_path().parent_path().filename()))
                        Latest = Candidate;
                }
            }
            return Latest;
        }

        std::optional<std::string> NormalizeCertificateThumbprint(std::string_view Value)
        {
            std::string Result;
            for (const char Character : Value)
            {
                if (std::isxdigit(static_cast<unsigned char>(Character)))
                    Result += static_cast<char>(std::toupper(static_cast<unsigned char>(Character)));
                else if (!std::isspace(static_cast<unsigned char>(Character)) &&
                         static_cast<unsigned char>(Character) < 0x80)
                    return std::nullopt;
            }
            if (Result.size() != 40)
                return std::nullopt;
            return Result;
        }

        std::string AndroidSdkInstallCommand(int CompileSdk)
        {
            const std::string Platform = std::to_string(CompileSdk);
            return "sdkmanager \"platforms;android-" + Platform + "\" \"build-tools;" + Platform + ".0.0\"";
        }
    } // namespace toolchain

} // namespace dbc::studio
