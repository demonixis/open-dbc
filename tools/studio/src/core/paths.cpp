#include "paths.hpp"
#include "path_utf8.hpp"

#include <SDL3/SDL.h>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path PathFromSdl(const char* Raw)
        {
            return (Raw != nullptr) ? dbc::PathFromUtf8(Raw) : fs::path();
        }
    } // namespace

    namespace core::paths
    {
        const fs::path& ExecutableDir()
        {
            static const fs::path Dir = PathFromSdl(SDL_GetBasePath());
            return Dir;
        }

        const fs::path& ResourceDir()
        {
            static const fs::path Dir = []
            {
                std::error_code Ec;
#ifdef __APPLE__
                // .../Contents/MacOS/opendbc -> .../Contents/Resources
                const fs::path Bundle = ExecutableDir().parent_path() / "Resources";
                if (fs::is_directory(Bundle, Ec))
                {
                    return Bundle;
                }
#endif
                return ExecutableDir();
            }();
            return Dir;
        }

        fs::path ProductPath()
        {
#ifdef __APPLE__
            std::error_code Error;
            fs::path Candidate = ExecutableDir();
            while (!Candidate.empty())
            {
                if (Candidate.extension() == ".app" && fs::is_directory(Candidate, Error))
                    return Candidate;
                const fs::path Parent = Candidate.parent_path();
                if (Parent == Candidate)
                    break;
                Candidate = Parent;
            }
#endif
#ifdef _WIN32
            return ExecutableDir() / "opendbc.exe";
#else
            return ExecutableDir() / "opendbc";
#endif
        }

        fs::path RunnerProduct()
        {
#if defined(__APPLE__)
            return ProductPath() / "Contents/Helpers/opendbc-runner.app";
#elif defined(_WIN32)
            return ExecutableDir() / "opendbc-runner.exe";
#else
            return ExecutableDir() / "opendbc-runner";
#endif
        }

        const fs::path& PrefDir()
        {
            static const fs::path Dir = []
            {
                char* Pref = SDL_GetPrefPath("OpenDBC", "Studio");
                const fs::path P = PathFromSdl(Pref);
                SDL_free(Pref);
                return P;
            }();
            return Dir;
        }

        fs::path FindReferenceDoc()
        {
            static const char* KName = "darkbasic_reference.md";
            std::error_code Ec;

            const fs::path Candidates[] = {
                ExecutableDir() / KName,
                ResourceDir() / KName,
            };

            for (const fs::path& Candidate : Candidates)
            {
                if (!Candidate.empty() && fs::is_regular_file(Candidate, Ec))
                {
                    return Candidate;
                }
            }
            return fs::path();
        }

    } // namespace core::paths

} // namespace dbc::studio
