#include "new_project.hpp"
#include "path_utf8.hpp"

#include "core/i18n.hpp"
#include "core/project_settings.hpp"
#include "interpreter/atomic_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

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
                return Tr("Enter a project name.");
            if (Name.size() > 64)
                return Tr("The project name is longer than 64 characters.");
            if (Name.front() == ' ' || Name.back() == ' ' || Name.back() == '.')
                return Tr("The project name cannot start with a space or end with a space or a dot.");
            if (Name == "." || Name == "..")
                return Tr("The project name cannot be . or ..");
            constexpr std::string_view Forbidden = "<>:\"/\\|?*";
            for (const char Character : Name)
            {
                if (static_cast<unsigned char>(Character) < 0x20 || Forbidden.find(Character) != std::string_view::npos)
                    return Tr("The project name cannot contain < > : \" / \\ | ? * or control characters.");
            }
            // Windows refuses these as folder names whatever the extension.
            const std::string Stem = Upper(Name.substr(0, Name.find('.')));
            static constexpr std::array<std::string_view, 4> Devices = {"CON", "PRN", "AUX", "NUL"};
            const bool Numbered = Stem.size() == 4 && (Stem.starts_with("COM") || Stem.starts_with("LPT")) &&
                                  Stem[3] >= '1' && Stem[3] <= '9';
            if (Numbered || std::find(Devices.begin(), Devices.end(), Stem) != Devices.end())
                return Tr("The project name is reserved by Windows.");
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
                Result.Error = TrFmt("The location is not a folder: {0}", {dbc::PathToUtf8(Request.Location)});
                return Result;
            }
            const std::filesystem::path Root = (Request.Location / dbc::PathFromUtf8(Request.Name)).lexically_normal();
            if (std::filesystem::exists(Root, Error) || Error)
            {
                Result.Error = Error ? TrFmt("Cannot inspect {0}: {1}", {dbc::PathToUtf8(Root), Error.message()})
                                     : TrFmt("A file or folder named {0} already exists there.", {Request.Name});
                return Result;
            }
            if (!std::filesystem::create_directory(Root, Error))
            {
                Result.Error = TrFmt("Cannot create {0}: {1}", {dbc::PathToUtf8(Root), Error.message()});
                return Result;
            }

            const std::filesystem::path Main = Root / KMainSource;
            bool Written = dbc::interpreter::AtomicWriteText(
                Main, ProjectTemplateSource(Request.Template, Request.Name), &Result.Error);
            if (Written && Request.Profile == dbc::language::LanguageProfile::DbPro)
            {
                Written = dbc::interpreter::AtomicWriteText(
                    Root / (Request.Name + ".dbpro"), DbProDescriptor(Request.Name, Request.Template), &Result.Error);
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
                    Result.Error = TrFmt("Cannot write {0}", {dbc::PathToUtf8(ProjectSettings::FilePath(Root))});
            }
            if (!Written)
            {
                // The folder did not exist before, so nothing of the user's is lost.
                std::filesystem::remove_all(Root, Error);
                if (Result.Error.empty())
                    Result.Error = Tr("Cannot write the project files");
                return Result;
            }
            Result.Root = Root;
            Result.MainSource = Main;
            return Result;
        }

        std::vector<std::filesystem::path> ProjectFolders(const std::filesystem::path& Location)
        {
            std::vector<std::filesystem::path> Folders;
            std::error_code Error;
            for (std::filesystem::directory_iterator It(Location, Error), End; !Error && It != End; It.increment(Error))
            {
                const std::string Name = dbc::PathToUtf8(It->path().filename());
                if (!Name.empty() && Name.front() != '.' && It->is_directory(Error))
                    Folders.push_back(It->path());
            }
            std::sort(Folders.begin(), Folders.end(),
                      [](const std::filesystem::path& Left, const std::filesystem::path& Right)
                      { return Upper(dbc::PathToUtf8(Left.filename())) < Upper(dbc::PathToUtf8(Right.filename())); });
            return Folders;
        }

        namespace
        {
            bool SameBytes(const std::filesystem::path& Left, const std::filesystem::path& Right)
            {
                std::error_code Error;
                if (!std::filesystem::is_regular_file(Right, Error) ||
                    std::filesystem::file_size(Left, Error) != std::filesystem::file_size(Right, Error) || Error)
                    return false;
                std::ifstream First(Left, std::ios::binary);
                std::ifstream Second(Right, std::ios::binary);
                std::array<char, 16384> Block;
                std::array<char, 16384> Other;
                while (First && Second)
                {
                    First.read(Block.data(), static_cast<std::streamsize>(Block.size()));
                    Second.read(Other.data(), static_cast<std::streamsize>(Other.size()));
                    if (First.gcount() != Second.gcount() ||
                        !std::equal(Block.begin(), Block.begin() + First.gcount(), Other.begin()))
                        return false;
                }
                return First.eof() && Second.eof();
            }

            // Whether `Copy` still has every file of `Sample` as it is. What a run adds, such as its
            // log, does not make the copy the user's.
            bool HoldsSample(const std::filesystem::path& Sample, const std::filesystem::path& Copy)
            {
                std::error_code Error;
                for (std::filesystem::recursive_directory_iterator It(Sample, Error), End; !Error && It != End;
                     It.increment(Error))
                {
                    if (It->is_regular_file(Error) && !SameBytes(It->path(), Copy / It->path().lexically_relative(Sample)))
                        return false;
                }
                return !Error;
            }

            // `Folder`, or the first of `Folder-2`, `Folder-3`... that `Taken` leaves free.
            template <typename TakenFn> std::filesystem::path FreeFolder(const std::filesystem::path& Folder, TakenFn Taken)
            {
                std::filesystem::path Candidate = Folder;
                for (int Number = 2; Taken(Candidate); ++Number)
                    Candidate = Folder.parent_path() / (dbc::PathToUtf8(Folder.filename()) + "-" + std::to_string(Number));
                return Candidate;
            }

            NewProjectResult CopyFolder(const std::filesystem::path& From, const std::filesystem::path& To)
            {
                NewProjectResult Result;
                Result.Root = To;
                std::error_code Error;
                std::filesystem::create_directories(To.parent_path(), Error);
                if (!Error)
                    std::filesystem::copy(From, To, std::filesystem::copy_options::recursive, Error);
                if (Error)
                {
                    // A partial copy would pass for a project the next time.
                    std::error_code Ignored;
                    std::filesystem::remove_all(To, Ignored);
                    Result.Error = TrFmt("Cannot copy {0}: {1}", {dbc::PathToUtf8(From.filename()), Error.message()});
                    Result.Root.clear();
                }
                return Result;
            }
        } // namespace

        NewProjectResult CopySampleProject(const std::filesystem::path& Sample, const std::filesystem::path& Location)
        {
            NewProjectResult Result;
            std::error_code Error;
            if (!std::filesystem::is_directory(Sample, Error) || Sample.filename().empty())
            {
                Result.Error = TrFmt("The sample {0} is missing.", {dbc::PathToUtf8(Sample.filename())});
                return Result;
            }
            const std::filesystem::path Target =
                FreeFolder(Location / Sample.filename(),
                           [&](const std::filesystem::path& Candidate)
                           {
                               std::error_code Ignored;
                               return std::filesystem::exists(Candidate, Ignored) && !HoldsSample(Sample, Candidate);
                           });
            if (std::filesystem::exists(Target, Error))
            {
                Result.Root = Target;
                return Result;
            }
            return CopyFolder(Sample, Target);
        }

        NewProjectResult DuplicateProject(const std::filesystem::path& Folder)
        {
            std::error_code Error;
            if (!std::filesystem::is_directory(Folder, Error) || Folder.filename().empty())
            {
                NewProjectResult Result;
                Result.Error = TrFmt("The project {0} is missing.", {dbc::PathToUtf8(Folder.filename())});
                return Result;
            }
            return CopyFolder(Folder, FreeFolder(Folder, [](const std::filesystem::path& Candidate)
                                                 {
                                                     std::error_code Ignored;
                                                     return std::filesystem::exists(Candidate, Ignored);
                                                 }));
        }

        NewProjectResult RenameProject(const std::filesystem::path& Folder, std::string_view Name)
        {
            NewProjectResult Result;
            Result.Error = ValidateProjectName(Name);
            if (!Result.Error.empty())
                return Result;
            const std::filesystem::path Target = Folder.parent_path() / dbc::PathFromUtf8(std::string(Name));
            std::error_code Error;
            if (std::filesystem::exists(Target, Error))
            {
                Result.Error = TrFmt("{0} already exists.", {std::string(Name)});
                return Result;
            }
            std::filesystem::rename(Folder, Target, Error);
            if (Error)
                Result.Error = TrFmt("Cannot rename {0}: {1}", {dbc::PathToUtf8(Folder.filename()), Error.message()});
            else
                Result.Root = Target;
            return Result;
        }

        std::string DeleteProject(const std::filesystem::path& Folder, const std::filesystem::path& Location)
        {
            std::error_code Error;
            // One of the location's own folders, and nothing a path could reach from there.
            if (Folder.filename().empty() || !std::filesystem::is_directory(Folder, Error) ||
                !std::filesystem::equivalent(Folder.parent_path(), Location, Error))
                return TrFmt("{0} is not a project of this location.", {dbc::PathToUtf8(Folder.filename())});
            std::filesystem::remove_all(Folder, Error);
            return Error ? TrFmt("Cannot delete {0}: {1}", {dbc::PathToUtf8(Folder.filename()), Error.message()})
                         : std::string();
        }

        std::filesystem::path RelocateUnder(const std::filesystem::path& Stored, const std::filesystem::path& Root)
        {
            std::error_code Error;
            if (Stored.empty() || Root.filename().empty() || std::filesystem::exists(Stored, Error))
                return Stored;
            std::vector<std::filesystem::path> Parts(Stored.begin(), Stored.end());
            const auto Last = std::find(Parts.rbegin(), Parts.rend(), Root.filename());
            if (Last == Parts.rend())
                return Stored;
            std::filesystem::path Moved = Root;
            for (auto Part = Last.base(); Part != Parts.end(); ++Part)
                Moved /= *Part;
            return Moved;
        }
    } // namespace core

} // namespace dbc::studio
