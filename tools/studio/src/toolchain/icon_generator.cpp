#include "icon_generator.hpp"
#include "core/i18n.hpp"
#include "path_utf8.hpp"

#include <fstream>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>

namespace dbc::studio
{
    namespace toolchain
    {
        void IconGenerator::WriteLog(const std::string& Message) const
        {
            if (Log_)
                Log_(Message);
        }

        bool IconGenerator::Generate(const std::filesystem::path& ProjectRoot, const std::string& IconSource)
        {
            namespace fs = std::filesystem;

            if (IconSource.empty())
            {
                WriteLog("  " + std::string(Tr("Error: no icon source configured")));
                return false;
            }

            fs::path Source = dbc::PathFromUtf8(IconSource);
            if (Source.is_relative())
                Source = ProjectRoot / Source;

            SDL_Surface* Original = IMG_Load(dbc::PathToUtf8(Source).c_str());
            if (Original == nullptr)
            {
                WriteLog("  " + TrFmt("Cannot load icon: {0}", {dbc::PathToUtf8(Source)}));
                return false;
            }

            const fs::path IconsDirectory = ProjectRoot / "icons";
            std::error_code Error;
            fs::create_directories(IconsDirectory, Error);
            if (Error)
            {
                SDL_DestroySurface(Original);
                WriteLog("  " + TrFmt("Cannot create icon directory: {0}", {dbc::PathToUtf8(IconsDirectory)}));
                return false;
            }

            if (!IMG_SavePNG(Original, dbc::PathToUtf8((IconsDirectory / "source.png")).c_str()))
            {
                SDL_DestroySurface(Original);
                WriteLog("  " + std::string(Tr("Cannot write source icon")));
                return false;
            }

            static const std::pair<const char*, int> Icons[] = {
                {"ios/AppIcon-1024.png", 1024},
                {"android/mipmap-mdpi/ic_launcher.png", 48},
                {"android/mipmap-hdpi/ic_launcher.png", 72},
                {"android/mipmap-xhdpi/ic_launcher.png", 96},
                {"android/mipmap-xxhdpi/ic_launcher.png", 144},
                {"android/mipmap-xxxhdpi/ic_launcher.png", 192},
                {"windows/icon.png", 256},
            };

            bool Success = true;
            nlohmann::json Generated = nlohmann::json::array();
            for (const auto& [RelativePath, Size] : Icons)
            {
                const fs::path Target = IconsDirectory / RelativePath;
                fs::create_directories(Target.parent_path(), Error);
                if (Error)
                {
                    WriteLog("  " +
                             TrFmt("Cannot create icon directory: {0}", {dbc::PathToUtf8(Target.parent_path())}));
                    Success = false;
                    Error.clear();
                    continue;
                }

                SDL_Surface* Scaled = SDL_ScaleSurface(Original, Size, Size, SDL_SCALEMODE_LINEAR);
                if (Scaled == nullptr)
                {
                    WriteLog("  " + TrFmt("Cannot scale icon to {0}", {std::to_string(Size)}));
                    Success = false;
                    continue;
                }

                if (IMG_SavePNG(Scaled, dbc::PathToUtf8(Target).c_str()))
                    Generated.push_back(RelativePath);
                else
                {
                    WriteLog("  " + TrFmt("Cannot write {0}", {dbc::PathToUtf8(Target)}));
                    Success = false;
                }
                SDL_DestroySurface(Scaled);
            }
            SDL_DestroySurface(Original);

            nlohmann::json Metadata;
            Metadata["source"] = IconSource;
            Metadata["generated"] = Generated;
            std::ofstream Manifest(IconsDirectory / "icons.json", std::ios::binary | std::ios::trunc);
            if (!Manifest)
            {
                WriteLog("  " + std::string(Tr("Cannot write icon manifest")));
                return false;
            }
            Manifest << Metadata.dump(4) << '\n';
            Success = Manifest.good() && Success;

            WriteLog("> " + TrFmt("Icons generated in {0}", {dbc::PathToUtf8(IconsDirectory)}));
            return Success;
        }
    } // namespace toolchain

} // namespace dbc::studio
