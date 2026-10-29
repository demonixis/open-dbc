#include "new_project.hpp"
#include "path_utf8.hpp"

#include "core/project_settings.hpp"
#include "interpreter/atomic_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace dbc::studio
{
    namespace
    {
        constexpr const char* KMainSource = "main.dba";

        std::string Upper(std::string_view Text)
        {
            std::string Result(Text);
            std::transform(Result.begin(), Result.end(), Result.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::toupper(Character)); });
            return Result;
        }

        // Laid out like the descriptors the DBPro editors write, so the project
        // still opens in them. The media root is left out: it defaults to the
        // project folder, whereas the editors store an absolute path.
        std::string DbProDescriptor(std::string_view Name, core::ProjectTemplate Template)
        {
            const std::string Title(Name);
            const char* Window = Template == core::ProjectTemplate::BasicScene ? "1280x720" : "640x480";
            std::string Text;
            Text += "; **** Dark BASIC Professional Project File ****\n";
            Text += "; **** Written by OpenDBC Studio ****\n";
            Text += "version=DBP1.00\n";
            Text += "project name=" + Title + "\n\n";
            Text += "; **** source file information ****\n";
            Text += std::string("main=") + KMainSource + "\n\n";
            Text += "; **** Executable Information ****\n";
            Text += "; build types: exe, media, installer, alone\n";
            Text += "executable=" + Title + ".exe\n";
            Text += "build type=exe\n\n";
            Text += "; **** display mode information ****\n";
            Text += "app title=" + Title + "\n\n";
            Text += "; graphics mode options: fullscreen, window, desktop, fulldesktop, hidden\n";
            Text += "graphics mode=window\n";
            Text += std::string("fullscreen resolution=") + Window + "x32\n\n";
            Text += "; arbitrary sizes are valid for windowed mode\n";
            Text += std::string("window resolution=") + Window + "\n";
            return Text;
        }
    } // namespace

    namespace core
    {
        std::string ValidateProjectName(std::string_view Name)
        {
            if (Name.empty())
                return "Enter a project name.";
            if (Name.size() > 64)
                return "The project name is longer than 64 characters.";
            if (Name.front() == ' ' || Name.back() == ' ' || Name.back() == '.')
                return "The project name cannot start with a space or end with a space or a dot.";
            if (Name == "." || Name == "..")
                return "The project name cannot be . or ..";
            constexpr std::string_view Forbidden = "<>:\"/\\|?*";
            for (const char Character : Name)
            {
                if (static_cast<unsigned char>(Character) < 0x20 || Forbidden.find(Character) != std::string_view::npos)
                    return "The project name cannot contain < > : \" / \\ | ? * or control characters.";
            }
            // Windows refuses these as folder names whatever the extension.
            const std::string Stem = Upper(Name.substr(0, Name.find('.')));
            static constexpr std::array<std::string_view, 4> Devices = {"CON", "PRN", "AUX", "NUL"};
            const bool Numbered = Stem.size() == 4 && (Stem.starts_with("COM") || Stem.starts_with("LPT")) &&
                                  Stem[3] >= '1' && Stem[3] <= '9';
            if (Numbered || std::find(Devices.begin(), Devices.end(), Stem) != Devices.end())
                return "The project name is reserved by Windows.";
            return {};
        }

        std::string ProjectTemplateSource(ProjectTemplate Template, std::string_view Name)
        {
            std::string Text = "rem " + std::string(Name) + "\n\n";
            if (Template == ProjectTemplate::BasicScene)
            {
                Text += "rem Display: a 1280x720 window refreshed at 60 frames per second\n"
                        "set display mode 1280, 720, 32\n"
                        "set window on\n"
                        "set window title \"" +
                        std::string(Name) +
                        "\"\n"
                        "sync on\n"
                        "sync rate 60\n"
                        "autocam off\n"
                        "backdrop on\n"
                        "color backdrop rgb(32, 32, 48)\n\n"
                        "rem Scene: a cube in front of the camera\n"
                        "make object cube 1, 10\n"
                        "position camera 0, 10, -30\n"
                        "point camera 0, 0, 0\n\n"
                        "do\n"
                        "   yrotate object 1, wrapvalue(object angle y(1) + 1)\n"
                        "   sync\n"
                        "loop\n";
            }
            else
            {
                Text += "sync on\n"
                        "do\n"
                        "   sync\n"
                        "loop\n";
            }
            return Text;
        }

        NewProjectResult CreateProject(const NewProjectRequest& Request)
        {
            NewProjectResult Result;
            Result.Error = ValidateProjectName(Request.Name);
            if (!Result.Error.empty())
                return Result;
            std::error_code Error;
            if (!std::filesystem::is_directory(Request.Location, Error))
            {
                Result.Error = "The location is not a folder: " + dbc::PathToUtf8(Request.Location);
                return Result;
            }
            const std::filesystem::path Root = (Request.Location / dbc::PathFromUtf8(Request.Name)).lexically_normal();
            if (std::filesystem::exists(Root, Error) || Error)
            {
                Result.Error = Error ? "Cannot inspect " + dbc::PathToUtf8(Root) + ": " + Error.message()
                                     : "A file or folder named " + Request.Name + " already exists there.";
                return Result;
            }
            if (!std::filesystem::create_directory(Root, Error))
            {
                Result.Error = "Cannot create " + dbc::PathToUtf8(Root) + ": " + Error.message();
                return Result;
            }

            const std::filesystem::path Main = Root / KMainSource;
            bool Written = dbc::interpreter::AtomicWriteText(
                Main, ProjectTemplateSource(Request.Template, Request.Name), &Result.Error);
            if (Written && Request.Profile == dbc::language::LanguageProfile::DbPro)
            {
                Written = dbc::interpreter::AtomicWriteText(Root / (Request.Name + ".dbpro"),
                                                            DbProDescriptor(Request.Name, Request.Template),
                                                            &Result.Error);
            }
            else if (Written)
            {
                // DBC has no descriptor of its own; the project configuration pins
                // the profile and the entry so detection never has to guess.
                ProjectSettings Settings;
                Settings.AppName = Request.Name;
                Settings.LanguageDialect = dbc::language::LanguageProfile::Dbc;
                Settings.LanguageEntry = KMainSource;
                Written = Settings.Save(Root);
                if (!Written)
                    Result.Error = "Cannot write " + dbc::PathToUtf8(ProjectSettings::FilePath(Root));
            }
            if (!Written)
            {
                // The folder did not exist before, so nothing of the user's is lost.
                std::filesystem::remove_all(Root, Error);
                if (Result.Error.empty())
                    Result.Error = "Cannot write the project files";
                return Result;
            }
            Result.Root = Root;
            Result.MainSource = Main;
            return Result;
        }
    } // namespace core

} // namespace dbc::studio
