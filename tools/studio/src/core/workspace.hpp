#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        // Where projects can live, per platform.
        //
        // On desktop this is barely a concept: any folder works. On mobile it is
        // the whole story, because the app only ever sees a handful of roots and
        // the user needs to know which one they are editing.
        class Workspace
        {
          public:
            enum class Kind
            {
                // Owned by the app, survives updates, safe to write.
                // iOS: Documents (visible in the Files app when the bundle sets
                //      UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace).
                // Android: getExternalFilesDir(), which shows up over USB.
                AppDocuments,

                // Read-only samples shipped inside the bundle or APK.
                BundledSamples,

                // Anywhere on disk. Desktop only.
                FileSystem,
            };

            struct Location
            {
                std::string Name;
                std::filesystem::path Path;
                Kind Type = Kind::AppDocuments;
                bool Writable = true;

                // Shown under the name in the picker, e.g. how to get files in.
                std::string Hint;
            };

            void Initialize();

            // Roots the user may pick a project from, in display order.
            const std::vector<Location>& Locations() const
            {
                return Locations_;
            }

            // Where downloaded or imported projects are written.
            const std::filesystem::path& ImportRoot() const
            {
                return ImportRoot_;
            }

            // The location a path belongs to, or nullptr when it is outside all of
            // them (only possible on desktop).
            const Location* LocationOf(const std::filesystem::path& Path) const;

            // True when the studio may write to this path.
            bool IsWritable(const std::filesystem::path& Path) const;

          private:
            void AddLocation(Location LocationValue);

            std::vector<Location> Locations_;
            std::filesystem::path ImportRoot_;
        };
    } // namespace core

} // namespace dbc::studio
