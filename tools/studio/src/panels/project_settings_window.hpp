#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "core/project_settings.hpp"
#include "language/dbpro_project.hpp"

namespace dbc::studio
{
    namespace panels
    {
        // Build > Project Settings. Same tabs and fields as the Qt dialog, editing
        // the .opendbc-project.json of the open project.
        class ProjectSettingsWindow
        {
          public:
            void Open(const std::filesystem::path& ProjectRoot, const core::ProjectSettings& Settings,
                      const std::optional<std::filesystem::path>& DbproProject = std::nullopt);

            bool IsOpen() const
            {
                return Open_;
            }

            // Draws the window; returns true on the frame the user saved, with the
            // edited settings written back to disk.
            bool Draw(core::ProjectSettings& Out);

          private:
            bool Open_ = false;
            std::filesystem::path Root_;
            core::ProjectSettings Draft_;
            std::optional<dbc::language::DbProProjectDocument> DbproProject_;
            std::vector<char> RawProject_;

            void LoadRawProject();
            void SetProjectValue(std::string_view Key, std::string_view Value);
            bool SaveProject() const;
        };
    } // namespace panels

} // namespace dbc::studio
