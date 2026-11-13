#include "bundled_resources.hpp"

#if defined(DBC_STUDIO_ANDROID)
#include "path_utf8.hpp"
#include "paths.hpp"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#endif

namespace dbc::studio
{
    namespace core
    {
#if defined(DBC_STUDIO_ANDROID)
        namespace
        {
            namespace fs = std::filesystem;

            bool ReadAsset(const std::string& Name, std::string& Out)
            {
                std::size_t Size = 0;
                void* Data = SDL_LoadFile(Name.c_str(), &Size);
                if (Data == nullptr)
                    return false;
                Out.assign(static_cast<const char*>(Data), Size);
                SDL_free(Data);
                return true;
            }
        } // namespace

        bool PrepareBundledResources(std::string& Error)
        {
            const fs::path Root = paths::ResourceDir();
            const fs::path StampFile = Root / "resources.stamp";
            std::string Stamp;
            if (!ReadAsset("resources.stamp", Stamp))
            {
                Error = "the package has no resources.stamp";
                return false;
            }
            std::error_code Failure;
            {
                std::ifstream Known(StampFile, std::ios::binary);
                std::ostringstream Text;
                Text << Known.rdbuf();
                if (Known && Text.str() == Stamp)
                    return true;
            }

            std::string List;
            if (!ReadAsset("resources.list", List))
            {
                Error = "the package has no resources.list";
                return false;
            }
            // Another build's files go first: a file it no longer ships must not stay.
            fs::remove_all(Root, Failure);
            std::istringstream Lines(List);
            std::string Name;
            std::string Content;
            while (std::getline(Lines, Name))
            {
                if (Name.empty())
                    continue;
                const fs::path Target = Root / dbc::PathFromUtf8(Name);
                fs::create_directories(Target.parent_path(), Failure);
                if (!ReadAsset(Name, Content))
                {
                    Error = "the package lacks " + Name;
                    return false;
                }
                std::ofstream Out(Target, std::ios::binary | std::ios::trunc);
                Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
                if (!Out)
                {
                    Error = "cannot write " + dbc::PathToUtf8(Target);
                    return false;
                }
            }
            // Written last: a copy cut short starts again.
            std::ofstream(StampFile, std::ios::binary | std::ios::trunc) << Stamp;
            return true;
        }
#else
        bool PrepareBundledResources(std::string&)
        {
            return true;
        }
#endif
    } // namespace core

} // namespace dbc::studio
