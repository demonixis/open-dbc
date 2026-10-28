#include "toolchain_locator.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
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
                Search.AndroidSdkRoots.push_back(fs::path(*Home) / "Library/Android/sdk");
            Search.AndroidSdkRoots.push_back("/opt/homebrew/share/android-commandlinetools");
            Search.AndroidSdkRoots.push_back("/usr/local/share/android-commandlinetools");
#elif defined(_WIN32)
            Search.JdkRoots = {"C:/Program Files/Android/Android Studio/jbr", "C:/Program Files/Java",
                               "C:/Program Files/Eclipse Adoptium", "C:/Program Files/Microsoft"};
            if (const auto LocalAppData = Search.Variable("LOCALAPPDATA"))
                Search.AndroidSdkRoots.push_back(fs::path(*LocalAppData) / "Android/Sdk");
#else
            Search.JdkRoots = {"/opt/android-studio/jbr", "/usr/lib/jvm"};
            if (Home)
            {
                Search.JdkRoots.insert(Search.JdkRoots.begin(), fs::path(*Home) / "android-studio/jbr");
                Search.AndroidSdkRoots.push_back(fs::path(*Home) / "Android/Sdk");
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
                    Candidates.emplace_back(*JavaHome);
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
                        Candidates.emplace_back(*Value);
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
                const std::string Name = It->path().filename().string();
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

        std::string AndroidSdkInstallCommand(int CompileSdk)
        {
            const std::string Platform = std::to_string(CompileSdk);
            return "sdkmanager \"platforms;android-" + Platform + "\" \"build-tools;" + Platform + ".0.0\"";
        }
    } // namespace toolchain

} // namespace dbc::studio
