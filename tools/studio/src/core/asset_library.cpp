#include "asset_library.hpp"

#include "path_utf8.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <string_view>

namespace dbc::studio
{
    namespace core
    {
        namespace
        {
            namespace fs = std::filesystem;

            constexpr std::array<std::string_view, 6> KCubeFaces = {"_px", "_nx", "_py", "_ny", "_pz", "_nz"};
            constexpr std::string_view KCubeMark = "_cube";
            constexpr std::string_view KNormalMark = "_normal";

            bool IsTexture(const fs::path& Path)
            {
                std::string Extension = dbc::PathToUtf8(Path.extension());
                std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                               [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
                return Extension == ".jpg" || Extension == ".jpeg" || Extension == ".png";
            }

            // "day_cube" for "day_cube_px", empty for a name that is not a cube map's face.
            std::string CubeMapOf(std::string_view Stem)
            {
                if (Stem.size() <= 3 + KCubeMark.size())
                    return {};
                const std::string_view Face = Stem.substr(Stem.size() - 3);
                const std::string_view Base = Stem.substr(0, Stem.size() - 3);
                if (std::find(KCubeFaces.begin(), KCubeFaces.end(), Face) == KCubeFaces.end() ||
                    !Base.ends_with(KCubeMark))
                    return {};
                return std::string(Base);
            }

            // "grass" for "grass_2".
            std::string CategoryOf(std::string_view Name)
            {
                const std::size_t Split = Name.find_last_of('_');
                if (Split == std::string_view::npos || Split + 1 >= Name.size())
                    return std::string(Name);
                const std::string_view Tail = Name.substr(Split + 1);
                const bool Number = std::all_of(Tail.begin(), Tail.end(),
                                                [](unsigned char C) { return std::isdigit(C) != 0; });
                return std::string(Number ? Name.substr(0, Split) : Name);
            }
        } // namespace

        std::vector<LibraryTexture> ScanTextureLibrary(const fs::path& Folder)
        {
            std::map<std::string, LibraryTexture> ByName;
            std::vector<fs::path> NormalMaps;
            std::error_code Error;
            for (fs::directory_iterator It(Folder, Error), End; !Error && It != End; It.increment(Error))
            {
                if (!It->is_regular_file(Error) || !IsTexture(It->path()))
                    continue;
                const std::string Stem = dbc::PathToUtf8(It->path().stem());
                if (Stem.size() > KNormalMark.size() && Stem.ends_with(KNormalMark))
                {
                    NormalMaps.push_back(It->path());
                    continue;
                }
                const std::string Cube = CubeMapOf(Stem);
                LibraryTexture& Texture = ByName[Cube.empty() ? Stem : Cube];
                Texture.CubeMap = !Cube.empty();
                Texture.Files.push_back(It->path());
            }
            for (const fs::path& Normal : NormalMaps)
            {
                const std::string Stem = dbc::PathToUtf8(Normal.stem());
                const std::string Base = Stem.substr(0, Stem.size() - KNormalMark.size());
                // A normal map without its texture is a texture of its own.
                const auto Owner = ByName.find(Base);
                LibraryTexture& Texture = Owner != ByName.end() ? Owner->second : ByName[Stem];
                Texture.NormalMap = Owner != ByName.end();
                Texture.Files.push_back(Normal);
            }
            std::vector<LibraryTexture> Textures;
            for (auto& [Name, Texture] : ByName)
            {
                Texture.Name = Name;
                Texture.Category = CategoryOf(Name);
                std::sort(Texture.Files.begin(), Texture.Files.end());
                // The face ahead says most about a sky.
                const auto Front = std::find_if(Texture.Files.begin(), Texture.Files.end(), [](const fs::path& File)
                                                { return dbc::PathToUtf8(File.stem()).ends_with("_pz"); });
                Texture.Preview = Front != Texture.Files.end() ? *Front : Texture.Files.front();
                if (Texture.NormalMap)
                {
                    // The colour texture, not its normal map, which sorts after it only by chance.
                    const auto Colour = std::find_if(Texture.Files.begin(), Texture.Files.end(), [&](const fs::path& F)
                                                     { return dbc::PathToUtf8(F.stem()) == Name; });
                    if (Colour != Texture.Files.end())
                        Texture.Preview = *Colour;
                }
                Textures.push_back(std::move(Texture));
            }
            return Textures;
        }

        fs::path LibraryTextureFolder(const fs::path& ProjectRoot)
        {
            return ProjectRoot / "textures";
        }

        bool HasLibraryTexture(const LibraryTexture& Texture, const fs::path& ProjectRoot)
        {
            const fs::path Folder = LibraryTextureFolder(ProjectRoot);
            std::error_code Error;
            return !Texture.Files.empty() &&
                   std::all_of(Texture.Files.begin(), Texture.Files.end(), [&](const fs::path& File)
                               { return fs::exists(Folder / File.filename(), Error); });
        }

        bool AddLibraryTexture(const LibraryTexture& Texture, const fs::path& ProjectRoot, std::string& Error)
        {
            const fs::path Folder = LibraryTextureFolder(ProjectRoot);
            std::error_code Failure;
            fs::create_directories(Folder, Failure);
            if (Failure)
            {
                Error = dbc::PathToUtf8(Folder) + ": " + Failure.message();
                return false;
            }
            for (const fs::path& File : Texture.Files)
            {
                const fs::path Target = Folder / File.filename();
                if (fs::exists(Target, Failure))
                    continue;
                fs::copy_file(File, Target, Failure);
                if (Failure)
                {
                    Error = dbc::PathToUtf8(Target) + ": " + Failure.message();
                    return false;
                }
            }
            return true;
        }
    } // namespace core

} // namespace dbc::studio
