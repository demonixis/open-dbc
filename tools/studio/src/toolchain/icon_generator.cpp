#include "icon_generator.hpp"

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
                WriteLog("  Error: no icon source configured");
                return false;
            }

            fs::path Source(IconSource);
            if (Source.is_relative())
                Source = ProjectRoot / Source;

            SDL_Surface* Original = IMG_Load(Source.string().c_str());
            if (Original == nullptr)
            {
                WriteLog("  Cannot load icon: " + Source.string());
                return false;
            }

            const fs::path IconsDirectory = ProjectRoot / "icons";
            std::error_code Error;
            fs::create_directories(IconsDirectory, Error);
            if (Error)
            {
                SDL_DestroySurface(Original);
                WriteLog("  Cannot create icon directory: " + IconsDirectory.string());
                return false;
            }

            if (!IMG_SavePNG(Original, (IconsDirectory / "source.png").string().c_str()))
            {
                SDL_DestroySurface(Original);
                WriteLog("  Cannot write source icon");
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
                    WriteLog("  Cannot create icon directory: " + Target.parent_path().string());
                    Success = false;
                    Error.clear();
                    continue;
                }

                SDL_Surface* Scaled = SDL_ScaleSurface(Original, Size, Size, SDL_SCALEMODE_LINEAR);
                if (Scaled == nullptr)
                {
                    WriteLog("  Cannot scale icon to " + std::to_string(Size));
                    Success = false;
                    continue;
                }

                if (IMG_SavePNG(Scaled, Target.string().c_str()))
                    Generated.push_back(RelativePath);
                else
                {
                    WriteLog("  Cannot write " + Target.string());
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
                WriteLog("  Cannot write icon manifest");
                return false;
            }
            Manifest << Metadata.dump(4) << '\n';
            Success = Manifest.good() && Success;

            WriteLog("> Icons generated in " + IconsDirectory.string());
            return Success;
        }
    } // namespace toolchain

} // namespace dbc::studio
