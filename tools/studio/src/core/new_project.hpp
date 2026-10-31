#pragma once

#include "language/language_profile.hpp"

#include <filesystem>
#include <string>
#include <string_view>

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
    } // namespace core

} // namespace dbc::studio
