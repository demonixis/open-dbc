#pragma once

#include "language/language_profile.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace core
    {
        enum class ProjectTemplate
        {
            // A main loop and nothing else.
            Empty,
            // A 1280x720 window running a lit, spinning cube.
            BasicScene,
        };

        struct NewProjectRequest
        {
            // Existing folder the project folder is created in.
            std::filesystem::path Location;
            std::string Name;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::DbPro;
            ProjectTemplate Template = ProjectTemplate::BasicScene;
        };

        struct NewProjectResult
        {
            std::filesystem::path Root;
            std::filesystem::path MainSource;
            std::string Error;

            explicit operator bool() const
            {
                return Error.empty();
            }
        };

        // Why `Name` cannot name a project folder on every desktop host, or empty
        // when it can.
        std::string ValidateProjectName(std::string_view Name);

        std::string ProjectTemplateSource(ProjectTemplate Template, std::string_view Name);

        // Creates `Location/Name` with main.dba and the descriptor its profile
        // reads: a historical .dbpro for DBPro, .opendbc-project.json for DBC.
        // Never touches an existing folder.
        NewProjectResult CreateProject(const NewProjectRequest& Request);

        // The folders directly under `Location`, one per project, by name. Hidden ones are skipped.
        std::vector<std::filesystem::path> ProjectFolders(const std::filesystem::path& Location);

        // Copies the read-only `Sample` folder to `Location` under its own name, so that it can be edited
        // and run, and returns the copy. A folder of that name is reused as it is: it holds the user's edits.
        NewProjectResult CopySampleProject(const std::filesystem::path& Sample, const std::filesystem::path& Location);

        // Where `Stored`, an absolute path an earlier run saved, is now that `Root`, the folder it was
        // under, may have moved: a sandboxed app's container changes path when the app is updated.
        // `Stored` itself when it still exists or was never under a folder of `Root`'s name.
        std::filesystem::path RelocateUnder(const std::filesystem::path& Stored, const std::filesystem::path& Root);
    } // namespace core

} // namespace dbc::studio
