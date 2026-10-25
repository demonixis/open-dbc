#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "dba/program_info.hpp"
#include "interpreter/project_resolver.hpp"

namespace dbc::studio
{
    namespace dba
    {
        class CommandDatabase;
    }

    namespace core
    {
        class FileDialog;
        class Workspace;
    } // namespace core

    namespace panels
    {
        // The launcher, back as a window inside the studio: pick a folder, see the
        // DBA programs it holds, and read what each one is before running it.
        class LauncherWindow
        {
          public:
            using PathHandler = std::function<void(const std::filesystem::path&)>;

            void Initialize(const dba::CommandDatabase* Commands, const core::Workspace* WorkspaceValue,
                            core::FileDialog* Dialog);

            void SetHandlers(PathHandler OnRun, PathHandler OnEdit)
            {
                OnRun_ = std::move(OnRun);
                OnEdit_ = std::move(OnEdit);
            }

            void Open()
            {
                Open_ = true;
            }

            bool IsOpen() const
            {
                return Open_;
            }

            // Points the launcher at a folder and rescans it.
            void SetFolder(const std::filesystem::path& Folder, dbc::language::LanguageProfile Profile);
            void SetProject(const dbc::interpreter::ResolvedProject& Project);
            void SetProfile(dbc::language::LanguageProfile Profile);

            void Draw(SDL_Window* Parent);

          private:
            void DrawToolbar(SDL_Window* Parent);
            void DrawList();
            void DrawDetails();
            void Select(int Index);

            bool Open_ = false;
            std::filesystem::path Folder_;
            std::vector<dba::ProgramInfo> Programs_;
            int Selected_ = -1;
            std::string Filter_;
            dbc::language::LanguageProfile Profile_ = dbc::language::LanguageProfile::Dbc;

            const dba::CommandDatabase* Commands_ = nullptr;
            const core::Workspace* Workspace_ = nullptr;
            core::FileDialog* Dialog_ = nullptr;

            PathHandler OnRun_;
            PathHandler OnEdit_;
        };
    } // namespace panels

} // namespace dbc::studio
