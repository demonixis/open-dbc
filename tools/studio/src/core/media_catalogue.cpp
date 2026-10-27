#include "media_catalogue.hpp"

#include <algorithm>
#include <cctype>

namespace dbc::studio
{
    namespace core
    {
        namespace fs = std::filesystem;

        std::string_view MediaKindLabel(MediaKind Kind)
        {
            switch (Kind)
            {
                case MediaKind::Image:
                    return "Images";
                case MediaKind::Audio:
                    return "Audio";
                case MediaKind::Model:
                    return "Models";
                case MediaKind::Effect:
                    return "Effects";
                case MediaKind::Video:
                    return "Videos";
                case MediaKind::Font:
                    return "Fonts";
            }
            return "Media";
        }

        std::optional<MediaKind> ClassifyMedia(const fs::path& Path)
        {
            struct Extension
            {
                std::string_view Name;
                MediaKind Kind;
            };
            // The formats DBC/DBPro programs load, plus the engine's own effect outputs.
            static constexpr Extension KExtensions[] = {
                {".png", MediaKind::Image},  {".jpg", MediaKind::Image},   {".jpeg", MediaKind::Image},
                {".bmp", MediaKind::Image},  {".tga", MediaKind::Image},   {".gif", MediaKind::Image},
                {".webp", MediaKind::Image}, {".dds", MediaKind::Image},   {".wav", MediaKind::Audio},
                {".ogg", MediaKind::Audio},  {".mp3", MediaKind::Audio},   {".flac", MediaKind::Audio},
                {".mid", MediaKind::Audio},  {".midi", MediaKind::Audio},  {".mod", MediaKind::Audio},
                {".xm", MediaKind::Audio},   {".s3m", MediaKind::Audio},   {".it", MediaKind::Audio},
                {".x", MediaKind::Model},    {".dbo", MediaKind::Model},   {".3ds", MediaKind::Model},
                {".obj", MediaKind::Model},  {".fbx", MediaKind::Model},   {".md2", MediaKind::Model},
                {".md3", MediaKind::Model},  {".mdl", MediaKind::Model},   {".b3d", MediaKind::Model},
                {".dae", MediaKind::Model},  {".gltf", MediaKind::Model},  {".glb", MediaKind::Model},
                {".fx", MediaKind::Effect},  {".hlsl", MediaKind::Effect}, {".spv", MediaKind::Effect},
                {".msl", MediaKind::Effect}, {".avi", MediaKind::Video},   {".mpg", MediaKind::Video},
                {".mpeg", MediaKind::Video}, {".wmv", MediaKind::Video},   {".mp4", MediaKind::Video},
                {".ttf", MediaKind::Font},   {".otf", MediaKind::Font},
            };

            std::string Name = Path.extension().string();
            std::transform(Name.begin(), Name.end(), Name.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            for (const Extension& Value : KExtensions)
            {
                if (Value.Name == Name)
                    return Value.Kind;
            }
            return std::nullopt;
        }

        MediaScan ScanMedia(const fs::path& Root, std::size_t Limit)
        {
            MediaScan Result;
            if (Root.empty())
                return Result;

            std::error_code Error;
            for (fs::recursive_directory_iterator It(Root, fs::directory_options::skip_permission_denied, Error), End;
                 It != End; It.increment(Error))
            {
                if (Error)
                    break;
                const fs::path& Path = It->path();
                const std::string Name = Path.filename().string();
                if (!Name.empty() && Name.front() == '.')
                {
                    if (It->is_directory(Error))
                        It.disable_recursion_pending();
                    continue;
                }
                if (!It->is_regular_file(Error))
                    continue;
                const std::optional<MediaKind> Kind = ClassifyMedia(Path);
                if (!Kind)
                    continue;
                if (Result.Files.size() == Limit)
                {
                    Result.Truncated = true;
                    break;
                }

                MediaFile File;
                File.Path = Path;
                File.Kind = *Kind;
                const fs::path Relative = fs::relative(Path, Root, Error);
                File.RelativeName = Error ? Name : Relative.generic_string();
                File.Size = It->file_size(Error);
                if (Error)
                    File.Size = 0;
                Result.Files.push_back(std::move(File));
            }

            std::sort(Result.Files.begin(), Result.Files.end(),
                      [](const MediaFile& Left, const MediaFile& Right) { return Left.RelativeName < Right.RelativeName; });
            return Result;
        }
    } // namespace core

} // namespace dbc::studio
