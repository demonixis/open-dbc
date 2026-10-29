#include "workspace.hpp"
#include "path_utf8.hpp"

#include <SDL3/SDL.h>

#include "paths.hpp"
#include "platform.hpp"

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

#if !defined(SDL_PLATFORM_ANDROID)
        fs::path UserDocuments()
        {
            // On iOS this is the app's own Documents directory, which is what the
            // Files app shows once the bundle opts in to file sharing.
            const char* Documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
            return (Documents != nullptr) ? dbc::PathFromUtf8(Documents) : fs::path();
        }
#endif

        bool IsUnder(const fs::path& Path, const fs::path& Root)
        {
            if (Root.empty())
            {
                return false;
            }

            std::error_code Ec;
            const fs::path A = fs::weakly_canonical(Path, Ec);
            const fs::path B = fs::weakly_canonical(Root, Ec);

            auto Ai = A.begin();
            auto Bi = B.begin();
            for (; Bi != B.end(); ++Ai, ++Bi)
            {
                if (Ai == A.end() || *Ai != *Bi)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    namespace core
    {
        void Workspace::AddLocation(Location LocationValue)
        {
            if (LocationValue.Path.empty())
            {
                return;
            }

            std::error_code Ec;
            if (LocationValue.Writable)
            {
                fs::create_directories(LocationValue.Path, Ec);
            }
            else if (!fs::is_directory(LocationValue.Path, Ec))
            {
                return;
            }

            Locations_.push_back(std::move(LocationValue));
        }

        void Workspace::Initialize()
        {
            Locations_.clear();

#if defined(SDL_PLATFORM_ANDROID)
            {
                // App-specific external storage: no permission needed, and it shows
                // up as Android/data/<package>/files when the device is plugged in.
                const char* external = SDL_GetAndroidExternalStoragePath();
                AddLocation(
                    Location{"My Projects", external != nullptr ? dbc::PathFromUtf8(external) / "projects" : fs::path(),
                             Kind::AppDocuments, true, "Visible over USB in Android/data/<package>/files/projects"});
            }
#else
            if constexpr (platform::IsIos())
            {
                AddLocation(Location{"My Projects", UserDocuments() / "projects", Kind::AppDocuments, true,
                                     "Visible in the Files app under On My iPad > OpenDBC"});
            }
            else
            {
                AddLocation(Location{"My Projects", UserDocuments() / "OpenDBC", Kind::AppDocuments, true,
                                     "Default place for downloaded projects"});
            }
#endif

            ImportRoot_ = Locations_.empty() ? paths::PrefDir() / "projects" : Locations_.front().Path;

            // Samples shipped with the app, read-only so a tap cannot ruin them.
            AddLocation(Location{"Samples", paths::ResourceDir() / "samples", Kind::BundledSamples, false,
                                 "Bundled examples - copy one to My Projects to edit it"});

            if constexpr (platform::HasFreeformFileSystem())
            {
                AddLocation(Location{"File System", dbc::PathFromUtf8("/"), Kind::FileSystem, true,
                                     "Open any folder on this machine"});
            }
        }

        const Workspace::Location* Workspace::LocationOf(const fs::path& Path) const
        {
            const Location* Best = nullptr;
            size_t BestDepth = 0;

            for (const Location& LocationValue : Locations_)
            {
                if (LocationValue.Type == Kind::FileSystem || !IsUnder(Path, LocationValue.Path))
                {
                    continue;
                }

                // Longest match wins, so Samples inside Documents still resolves.
                const size_t Depth = std::distance(LocationValue.Path.begin(), LocationValue.Path.end());
                if (Best == nullptr || Depth > BestDepth)
                {
                    Best = &LocationValue;
                    BestDepth = Depth;
                }
            }

            if (Best != nullptr)
            {
                return Best;
            }

            for (const Location& LocationValue : Locations_)
            {
                if (LocationValue.Type == Kind::FileSystem)
                {
                    return &LocationValue;
                }
            }
            return nullptr;
        }

        bool Workspace::IsWritable(const fs::path& Path) const
        {
            const Location* LocationValue = LocationOf(Path);
            return (LocationValue != nullptr) && LocationValue->Writable;
        }
    } // namespace core

} // namespace dbc::studio
