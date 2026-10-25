#pragma once

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>

#if __has_include(<SDL3_image/SDL_image.h>)
#include <SDL3_image/SDL_image.h>
#elif __has_include(<SDL_image.h>)
#include <SDL_image.h>
#endif

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace dbc
{
    inline std::string NormalizePath(const std::string& Path)
    {
        std::string Result = Path;
        std::replace(Result.begin(), Result.end(), '\\', '/');
        return Result;
    }

    inline std::string FileExtensionLower(const std::string& Path)
    {
        const auto Dot = Path.find_last_of('.');
        if (Dot == std::string::npos)
        {
            return std::string();
        }
        std::string Ext = Path.substr(Dot);
        std::transform(Ext.begin(), Ext.end(), Ext.begin(),
                       [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        return Ext;
    }

    SDL_Surface* LoadSurface(const std::string& Path);

    // IMG_Save dispatches on the extension, but this build of SDL_image has no
    // BMP writer. SDL_SaveBMP is part of SDL core and always available, and the
    // DarkBASIC examples save .bmp.
    inline bool SaveSurface(SDL_Surface* Surface, const std::string& Path)
    {
        const std::string Ext = FileExtensionLower(Path);
        if (Ext == ".bmp")
        {
            return SDL_SaveBMP(Surface, Path.c_str());
        }
        if (Ext == ".png")
        {
            return IMG_SavePNG(Surface, Path.c_str());
        }
        if (Ext == ".jpg" || Ext == ".jpeg")
        {
            return IMG_SaveJPG(Surface, Path.c_str(), 90);
        }
        return IMG_Save(Surface, Path.c_str());
    }

    // -----------------------------------------------------------------------
    // Assimp IOStream backed by SDL_IOStream
    // -----------------------------------------------------------------------

    class SdlAssimpIOStream final : public Assimp::IOStream
    {
      public:
        explicit SdlAssimpIOStream(SDL_IOStream* Io, bool Own) : Io_(Io), Own_(Own) {}

        ~SdlAssimpIOStream() override
        {
            if (Io_ && Own_)
            {
                SDL_CloseIO(Io_);
            }
        }

        size_t Read(void* Buffer, size_t Size, size_t Count) override
        {
            return SDL_ReadIO(Io_, Buffer, Size * Count) / (Size ? Size : 1);
        }

        size_t Write(const void* Buffer, size_t Size, size_t Count) override
        {
            return SDL_WriteIO(Io_, Buffer, Size * Count) / (Size ? Size : 1);
        }

        aiReturn Seek(size_t Offset, aiOrigin Origin) override
        {
            SDL_IOWhence Whence = SDL_IO_SEEK_SET;
            if (Origin == aiOrigin_CUR)
                Whence = SDL_IO_SEEK_CUR;
            else if (Origin == aiOrigin_END)
                Whence = SDL_IO_SEEK_END;
            return SDL_SeekIO(Io_, static_cast<Sint64>(Offset), Whence) >= 0 ? aiReturn_SUCCESS : aiReturn_FAILURE;
        }

        size_t Tell() const override
        {
            return static_cast<size_t>(SDL_TellIO(Io_));
        }

        size_t FileSize() const override
        {
            const Sint64 Cur = SDL_TellIO(Io_);
            SDL_SeekIO(Io_, 0, SDL_IO_SEEK_END);
            const Sint64 Size = SDL_TellIO(Io_);
            SDL_SeekIO(Io_, Cur, SDL_IO_SEEK_SET);
            return static_cast<size_t>(Size);
        }

        void Flush() override {}

      private:
        SDL_IOStream* Io_ = nullptr;
        bool Own_ = true;
    };

    // -----------------------------------------------------------------------
    // Assimp IOSystem backed by SDL_IOFromFile (Android APK-compatible)
    // -----------------------------------------------------------------------

    class SdlAssimpIOSystem final : public Assimp::IOSystem
    {
      public:
        bool Exists(const char* Path) const override
        {
            SDL_IOStream* Io = SDL_IOFromFile(Path, "rb");
            if (Io)
            {
                SDL_CloseIO(Io);
                return true;
            }
            return false;
        }

        char getOsSeparator() const override
        {
            return '/';
        }

        Assimp::IOStream* Open(const char* Path, const char* Mode = "rb") override
        {
            SDL_IOStream* Io = SDL_IOFromFile(Path, Mode);
            if (!Io)
            {
                return nullptr;
            }
            return new SdlAssimpIOStream(Io, true);
        }

        void Close(Assimp::IOStream* Stream) override
        {
            delete Stream;
        }
    };

} // namespace dbc
