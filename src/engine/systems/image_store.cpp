#include "image_store.hpp"
#include "../engine_math.hpp"
#include "../engine_types.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "log.hpp"
#include "path_utf8.hpp"
#include "renderer_system.hpp"
#include "sprite_system.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <span>

#if __has_include(<SDL3_image/SDL_image.h>)
#include <SDL3_image/SDL_image.h>
#elif __has_include(<SDL_image.h>)
#include <SDL_image.h>
#endif

#include <assimp/scene.h>

#include "platform_io.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>

namespace dbc
{
    namespace
    {
        bool ValidateImageId(int Id, std::string* Error = nullptr)
        {
            if (Id < 1 || Id > 65535)
            {
                if (Error)
                    *Error = "Image number must be between 1 and 65535";
                return false;
            }
            return true;
        }

        bool ValidateBitmapId(int Id, std::string* Error = nullptr)
        {
            if (Id < 0 || Id > 32)
            {
                if (Error)
                    *Error = "Bitmap number must be between 0 and 32";
                return false;
            }
            return true;
        }

        SDL_Surface* ApplyTextureDivision(SDL_Surface* Surface, int DivideTextureSize)
        {
            if (!Surface || DivideTextureSize <= 1)
                return Surface;
            SDL_Surface* Scaled = SDL_ScaleSurface(Surface, std::max(1, Surface->w / DivideTextureSize),
                                                   std::max(1, Surface->h / DivideTextureSize), SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(Surface);
            return Scaled;
        }

        SDL_PixelFormat BitmapPixelFormat(int Format)
        {
            switch (Format)
            {
                case -1:
                case 21:
                    return SDL_PIXELFORMAT_ARGB8888;
                case 20:
                    return SDL_PIXELFORMAT_BGR24;
                case 22:
                    return SDL_PIXELFORMAT_XRGB8888;
                case 23:
                    return SDL_PIXELFORMAT_RGB565;
                case 24:
                    return SDL_PIXELFORMAT_XRGB1555;
                case 25:
                    return SDL_PIXELFORMAT_ARGB1555;
                case 26:
                    return SDL_PIXELFORMAT_ARGB4444;
                case 32:
                    return SDL_PIXELFORMAT_ABGR8888;
                case 33:
                    return SDL_PIXELFORMAT_XBGR8888;
                default:
                    return SDL_PIXELFORMAT_UNKNOWN;
            }
        }

        bool EnsureParentDirectory(const std::string& Path, std::string* Error = nullptr)
        {
            namespace fs = std::filesystem;
            const fs::path OutPath = dbc::PathFromUtf8(Path);
            const fs::path Parent = OutPath.parent_path();
            if (!Parent.empty())
            {
                std::error_code Ec;
                fs::create_directories(Parent, Ec);
                if (Ec)
                {
                    if (Error)
                        *Error = "Cannot create image output directory: " + Ec.message();
                    return false;
                }
            }
            return true;
        }

        bool DetectSurfaceAlpha(SDL_Surface* Surface, bool& HasAlpha, bool& HasPartialAlpha,
                                std::string* Error = nullptr)
        {
            HasAlpha = false;
            HasPartialAlpha = false;
            if (!Surface)
            {
                if (Error)
                    *Error = std::string("Cannot inspect image alpha: ") + SDL_GetError();
                return false;
            }

            SDL_Surface* Rgba = SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32);
            if (!Rgba)
            {
                if (Error)
                    *Error = std::string("Cannot inspect image alpha: ") + SDL_GetError();
                return false;
            }

            if (!SDL_LockSurface(Rgba))
            {
                SDL_DestroySurface(Rgba);
                if (Error)
                    *Error = std::string("Cannot inspect image alpha: ") + SDL_GetError();
                return false;
            }

            const auto* Pixels = static_cast<const Uint8*>(Rgba->pixels);
            for (int Y = 0; Y < Rgba->h; ++Y)
            {
                const Uint8* Row = Pixels + Y * Rgba->pitch;
                for (int X = 0; X < Rgba->w; ++X)
                {
                    const Uint8 A = Row[X * 4 + 3];
                    if (A < 255)
                    {
                        HasAlpha = true;
                    }
                    if (A > 0 && A < 255)
                    {
                        HasPartialAlpha = true;
                        break;
                    }
                }
                if (HasPartialAlpha)
                {
                    break;
                }
            }

            SDL_UnlockSurface(Rgba);
            SDL_DestroySurface(Rgba);
            return true;
        }

        bool ColorKeyExempt(const std::string& Path, int TextureFlag)
        {
            if (TextureFlag != 1)
                return false;
            std::string Extension = dbc::PathToUtf8(dbc::PathFromUtf8(Path).extension());
            std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                           [](unsigned char Value) { return static_cast<char>(std::tolower(Value)); });
            return Extension == ".png" || Extension == ".dds" || Extension == ".tga";
        }

        bool ApplyColorKey(SDL_Surface*& Surface, int ColorKey, std::string* Error = nullptr)
        {
            bool HasAlpha = false;
            bool HasPartialAlpha = false;
            if (!DetectSurfaceAlpha(Surface, HasAlpha, HasPartialAlpha, Error))
                return false;
            if (HasAlpha)
                return true;

            SDL_Surface* Rgba = SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32);
            if (!Rgba)
            {
                if (Error)
                    *Error = std::string("Image color-key conversion failed: ") + SDL_GetError();
                return false;
            }
            SDL_DestroySurface(Surface);
            Surface = Rgba;

            if (!SDL_LockSurface(Surface))
            {
                if (Error)
                    *Error = std::string("Image color-key lock failed: ") + SDL_GetError();
                return false;
            }
            auto* Pixels = static_cast<Uint8*>(Surface->pixels);
            const Uint8 Red = static_cast<Uint8>((ColorKey >> 16) & 0xFF);
            const Uint8 Green = static_cast<Uint8>((ColorKey >> 8) & 0xFF);
            const Uint8 Blue = static_cast<Uint8>(ColorKey & 0xFF);
            for (int Y = 0; Y < Surface->h; ++Y)
            {
                Uint8* Row = Pixels + Y * Surface->pitch;
                for (int X = 0; X < Surface->w; ++X)
                {
                    Uint8* Pixel = Row + X * 4;
                    if (Pixel[0] == Red && Pixel[1] == Green && Pixel[2] == Blue)
                        Pixel[3] = 0;
                }
            }
            SDL_UnlockSurface(Surface);
            return true;
        }
    } // anonymous namespace

    ImageStore::ImageStore(EngineKernel& Kernel) : Kernel_(Kernel) {}

    ImageStore::~ImageStore()
    {
        for (auto& [Id, Surface] : Bitmaps_)
        {
            if (Surface)
            {
                SDL_DestroySurface(Surface);
            }
        }
        Bitmaps_.clear();

        for (auto& [Id, Entry] : BitmapTextures_)
            ReleaseBitmapTexture(Entry);
        BitmapTextures_.clear();

        for (auto& [Id, ImageValue] : Images_)
        {
            DestroyImage(ImageValue);
        }
        Images_.clear();

        OverlayImageBlits_.clear();
        TexturePathToImageId_.clear();
    }

    // === Image operations ===

    bool ImageStore::LoadImageFile(const std::string& Path, int ImageId, int TextureFlag, int DivideTextureSize,
                                   std::string* Error)
    {
        if (!ValidateImageId(ImageId, Error))
            return false;
        if (Kernel_.RuntimeCompatibility().LoadImageTextureModeUsesClassicHints())
        {
            if (TextureFlag < 0 || TextureFlag > 2)
            {
                if (Error)
                    *Error = "LOAD IMAGE texture mode must be 0, 1, or 2";
                return false;
            }
            TextureFlag = 0;
        }
        else if (TextureFlag != 0 && TextureFlag != 1)
        {
            if (Error)
                *Error = "LOAD IMAGE cube and volume texture flags are not available";
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "LoadImage requires an initialized graphics device";
            return false;
        }

        std::string Resolved;
        if (!ResolveAssetPath(Resolved, Path, Error))
            return false;
        SDL_Surface* Surface = LoadSurface(Resolved);
        if (!Surface)
        {
            if (Error)
                *Error = "LOAD IMAGE failed for " + Path + ": " + SDL_GetError();
            return false;
        }

        if (TextureFlag != 1 && DivideTextureSize == 16384)
        {
            SDL_DestroySurface(Surface);
            Surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
            if (!Surface)
            {
                if (Error)
                    *Error = std::string("LOAD IMAGE placeholder allocation failed: ") + SDL_GetError();
                return false;
            }
            if (!SDL_FillSurfaceRect(Surface, nullptr, 0))
            {
                if (Error)
                    *Error = std::string("Bitmap initialization failed: ") + SDL_GetError();
                SDL_DestroySurface(Surface);
                return false;
            }
        }
        else if (TextureFlag != 1 && DivideTextureSize > 1)
        {
            SDL_Surface* Scaled = SDL_ScaleSurface(Surface, std::max(1, Surface->w / DivideTextureSize),
                                                   std::max(1, Surface->h / DivideTextureSize), SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(Surface);
            Surface = Scaled;
            if (!Surface)
            {
                if (Error)
                    *Error = std::string("LoadImage resize failed: ") + SDL_GetError();
                DBC_LOG_ERROR("LoadImage resize failed for {} -> {}", Resolved, SDL_GetError());
                return false;
            }
        }

        if (!ColorKeyExempt(Resolved, TextureFlag))
            if (!ApplyColorKey(Surface, ImageColorKey_, Error))
            {
                SDL_DestroySurface(Surface);
                return false;
            }

        auto& Image = Images_[ImageId];
        const bool Uploaded = FillImageFromSurface(Image, Surface, Resolved, Error);
        SDL_DestroySurface(Surface);

        // An upload that produced nothing leaves a slot every draw path would
        // have to special-case, so the slot goes with it.
        if (!Uploaded)
        {
            DBC_LOG_ERROR("LoadImage uploaded no texture for {} (image {})", Resolved, ImageId);
            if (!Image.Texture && !Image.GpuTexture)
                Images_.erase(ImageId);
            return false;
        }
        MissingImageWarned_.erase(ImageId);
        return true;
    }

    bool ImageStore::DeleteImage(int ImageId, std::string* Error)
    {
        if (!ValidateImageId(ImageId, Error))
            return false;
        const auto It = Images_.find(ImageId);
        if (It == Images_.end())
        {
            if (Error)
                *Error = "DELETE IMAGE requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (auto MapIt = TexturePathToImageId_.begin(); MapIt != TexturePathToImageId_.end();)
        {
            if (MapIt->second == ImageId)
            {
                MapIt = TexturePathToImageId_.erase(MapIt);
            }
            else
            {
                ++MapIt;
            }
        }
        DestroyImage(It->second);
        Images_.erase(It);
        return true;
    }

    bool ImageStore::CloneImageInternal(int& Output, int ImageId, std::string* Error)
    {
        const auto Source = Images_.find(ImageId);
        if (Source == Images_.end() || !Source->second.Surface)
        {
            if (Error)
                *Error = "Cannot snapshot image " + std::to_string(ImageId);
            return false;
        }

        SDL_Surface* Surface = Source->second.Surface;
        const std::string SourcePath = Source->second.SourcePath;
        const int DivideTextureSize = Source->second.DivideTextureSize;
        if (NextInternalImageId_ == std::numeric_limits<int>::min())
        {
            if (Error)
                *Error = "Internal image identifiers are exhausted";
            return false;
        }
        const int InternalId = NextInternalImageId_--;
        Image& Copy = Images_[InternalId];
        const bool Uploaded = FillImageFromSurface(Copy, Surface, SourcePath, Error);
        Copy.DivideTextureSize = DivideTextureSize;
        if (!Uploaded)
        {
            DestroyImage(Copy);
            Images_.erase(InternalId);
            return false;
        }
        {
            Output = InternalId;
            return true;
        }
    }

    void ImageStore::ReleaseImageInternal(int ImageId)
    {
        if (ImageId >= 0)
            return;
        const auto ImageValue = Images_.find(ImageId);
        if (ImageValue == Images_.end())
            return;
        DestroyImage(ImageValue->second);
        Images_.erase(ImageValue);
    }

    int ImageStore::ImageExist(int ImageId) const
    {
        const auto It = Images_.find(ImageId);
        return (It != Images_.end() && (It->second.Texture || It->second.GpuTexture)) ? 1 : 0;
    }

    int ImageStore::ImageFileExist(const std::string& Path) const
    {
        std::error_code Error;
        return std::filesystem::is_regular_file(Path, Error) ? 1 : 0;
    }

    bool ImageStore::PasteImage(int ImageId, int X, int Y, bool TransparentBlack, std::string* Error)
    {
        if (ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "PASTE IMAGE requires existing image " + std::to_string(ImageId);
            return false;
        }
        if (CurrentBitmapId_ == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        SDL_Surface* Destination =
            CurrentBitmapId_ > 0 ? GetBitmapSurface(CurrentBitmapId_) : EnsureScreenBitmap(Error);
        if (!Destination)
        {
            if (Error && CurrentBitmapId_ > 0)
                *Error = "PASTE IMAGE requires a valid current bitmap";
            return false;
        }
        {
            const Image* ImageValue = GetImage(ImageId);
            if (!Destination || !ImageValue || !ImageValue->Surface)
            {
                if (Error)
                    *Error = "PASTE IMAGE has no CPU surface for image " + std::to_string(ImageId);
                return false;
            }
            SDL_Rect DestinationRect{X, Y, ImageValue->Surface->w, ImageValue->Surface->h};
            const SDL_Rect Written = DestinationRect;
            if (TransparentBlack)
            {
                const Uint32 ColorKey = SDL_MapSurfaceRGBA(
                    ImageValue->Surface, static_cast<Uint8>((ImageColorKey_ >> 16) & 0xFF),
                    static_cast<Uint8>((ImageColorKey_ >> 8) & 0xFF), static_cast<Uint8>(ImageColorKey_ & 0xFF), 255);
                SDL_SetSurfaceColorKey(ImageValue->Surface, true, ColorKey);
            }
            if (!SDL_BlitSurface(ImageValue->Surface, nullptr, Destination, &DestinationRect))
            {
                if (TransparentBlack)
                    SDL_SetSurfaceColorKey(ImageValue->Surface, false, 0);
                {
                    if (Error)
                        *Error = std::string("PASTE IMAGE failed: ") + SDL_GetError();
                    return false;
                }
            }
            if (TransparentBlack)
                SDL_SetSurfaceColorKey(ImageValue->Surface, false, 0);
            InvalidateBitmapGpuTexture(CurrentBitmapId_, &Written);
            if (CurrentBitmapId_ > 0)
                return true;
        }
        OverlayImageBlits_.push_back(OverlayImageBlit{
            ImageId,
            static_cast<float>(X),
            static_cast<float>(Y),
            TransparentBlack,
        });
        return true;
    }

    bool ImageStore::GetImage(int ImageId, int Left, int Top, int Right, int Bottom, std::string* Error)
    {
        if (!ValidateImageId(ImageId, Error))
            return false;
        if (Right <= Left || Bottom <= Top)
        {
            if (Error)
                *Error = "GET IMAGE requires a positive rectangle";
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "GET IMAGE requires an initialized graphics device";
            return false;
        }

        struct SurfaceDeleter
        {
            void operator()(SDL_Surface* S) const
            {
                if (S)
                {
                    SDL_DestroySurface(S);
                }
            }
        };

        std::unique_ptr<SDL_Surface, SurfaceDeleter> Captured;

        SDL_Surface* Source = nullptr;
        const auto BmpIt = Bitmaps_.find(CurrentBitmapId_);
        // While a 3D scene is shown, the screen bitmap only holds the 2D layer: read the composed screen.
        const bool ComposedScreen = CurrentBitmapId_ <= 0 && Kernel_.Renderer().LastFrameShowedWorld();
        if (!ComposedScreen && BmpIt != Bitmaps_.end() && BmpIt->second)
        {
            Source = BmpIt->second;
        }
        else if (CurrentBitmapId_ <= 0)
        {
            // Bitmap 0 is the screen in DarkBASIC, and nothing mirrors it on the
            // CPU: render a frame into an offscreen target and read it back.
            // -1 means no bitmap was ever selected, which is also the screen.
            Captured.reset(Kernel_.Renderer().CaptureScreen());
            Source = Captured.get();
        }

        if (!Source)
        {
            if (Error)
                *Error = "GET IMAGE has no readable source bitmap";
            return false;
        }

        // Convert source to RGBA32 first so cropping and GPU upload work
        // regardless of the original bitmap format (e.g. 4-bit indexed BMP).
        SDL_Surface* RgbaSource = Source;
        bool OwnsRgbaSource = false;
        if (Source->format != SDL_PIXELFORMAT_RGBA32)
        {
            RgbaSource = SDL_ConvertSurface(Source, SDL_PIXELFORMAT_RGBA32);
            if (!RgbaSource)
            {
                if (Error)
                    *Error = std::string("GET IMAGE conversion failed: ") + SDL_GetError();
                return false;
            }
            OwnsRgbaSource = true;
        }

        const int X = std::clamp(Left, 0, RgbaSource->w - 1);
        const int Y = std::clamp(Top, 0, RgbaSource->h - 1);
        const int W =
            std::max(1, std::min(RgbaSource->w - X,
                                 static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(Right) - Left,
                                                                         std::numeric_limits<int>::max()))));
        const int H =
            std::max(1, std::min(RgbaSource->h - Y,
                                 static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(Bottom) - Top,
                                                                         std::numeric_limits<int>::max()))));

        SDL_Surface* Cropped = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
        if (!Cropped)
        {
            if (OwnsRgbaSource)
                SDL_DestroySurface(RgbaSource);
            {
                if (Error)
                    *Error = std::string("GET IMAGE allocation failed: ") + SDL_GetError();
                return false;
            }
        }

        SDL_Rect SrcRect{X, Y, W, H};
        SDL_Rect DstRect{0, 0, W, H};
        SDL_BlitSurface(RgbaSource, &SrcRect, Cropped, &DstRect);
        if (OwnsRgbaSource)
            SDL_DestroySurface(RgbaSource);

        // DarkBASIC-style sprite extraction: treat pure black as transparent.
        if (SDL_LockSurface(Cropped))
        {
            auto* Pixels = static_cast<Uint8*>(Cropped->pixels);
            for (int Yy = 0; Yy < Cropped->h; ++Yy)
            {
                Uint8* Row = Pixels + Yy * Cropped->pitch;
                for (int Xx = 0; Xx < Cropped->w; ++Xx)
                {
                    Uint8* P = Row + Xx * 4;
                    if (P[0] == ((ImageColorKey_ >> 16) & 0xFF) && P[1] == ((ImageColorKey_ >> 8) & 0xFF) &&
                        P[2] == (ImageColorKey_ & 0xFF))
                    {
                        P[3] = 0;
                    }
                }
            }
            SDL_UnlockSurface(Cropped);
        }

        auto& Image = Images_[ImageId];
        const bool Uploaded = FillImageFromSurface(Image, Cropped, "", Error);
        SDL_DestroySurface(Cropped);
        if (!Uploaded)
        {
            if (!Image.Texture && !Image.GpuTexture)
                Images_.erase(ImageId);
            return false;
        }
        MissingImageWarned_.erase(ImageId);
        return true;
    }

    bool ImageStore::SaveImage(const std::string& Path, int ImageId, std::string* Error)
    {
        const auto It = Images_.find(ImageId);
        if (It == Images_.end() || !It->second.Surface)
        {
            if (Error)
                *Error = "SAVE IMAGE requires existing image " + std::to_string(ImageId);
            return false;
        }

        if (!EnsureParentDirectory(Path, Error))
            return false;
        if (!SaveSurface(It->second.Surface, Path))
        {
            if (Error)
                *Error = std::string("SAVE IMAGE failed for '") + Path + "': " + SDL_GetError();
            return false;
        }
        return true;
    }

    void ImageStore::SetImageColorKey(int Red, int Green, int Blue)
    {
        ImageColorKey_ = (std::clamp(Red, 0, 255) << 16) | (std::clamp(Green, 0, 255) << 8) | std::clamp(Blue, 0, 255);
    }

    // === Bitmap operations ===

    bool ImageStore::LoadBitmapFile(const std::string& Path, int BitmapId, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
        {
            if (Error)
                Error->insert(0, "LOAD BITMAP failed: ");
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "LoadBitmap requires an initialized graphics device";
            return false;
        }

        std::string Resolved;
        if (!ResolveAssetPath(Resolved, Path, Error))
            return false;
        SDL_Surface* Surface = LoadSurface(Resolved);
        if (!Surface)
        {
            if (Error)
                *Error = "LOAD BITMAP failed for " + Path + ": " + SDL_GetError();
            return false;
        }

        const auto It = Bitmaps_.find(BitmapId);
        if (It != Bitmaps_.end() && It->second)
        {
            SDL_DestroySurface(It->second);
        }

        Bitmaps_[BitmapId] = Surface;
        MirroredBitmaps_.erase(BitmapId);
        FlippedBitmaps_.erase(BitmapId);
        CurrentBitmapId_ = BitmapId;
        if (BitmapId == 0)
            Kernel_.Sprites().ClearBobTrails();
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::DeleteBitmap(int BitmapId, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        const auto It = Bitmaps_.find(BitmapId);
        if (It == Bitmaps_.end())
        {
            if (BitmapId == 0)
            {
                Kernel_.Sprites().ClearBobTrails();
                return true;
            }
            {
                if (Error)
                    *Error = "DELETE BITMAP requires existing bitmap " + std::to_string(BitmapId);
                return false;
            }
        }

        if (It->second)
        {
            SDL_DestroySurface(It->second);
        }
        Bitmaps_.erase(It);
        MirroredBitmaps_.erase(BitmapId);
        FlippedBitmaps_.erase(BitmapId);
        if (BitmapId == 0)
            Kernel_.Sprites().ClearBobTrails();
        InvalidateBitmapGpuTexture(BitmapId);

        if (CurrentBitmapId_ == BitmapId)
        {
            CurrentBitmapId_ = 0;
        }
        return true;
    }

    bool ImageStore::SetCurrentBitmap(int BitmapId, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (BitmapId == 0)
        {
            CurrentBitmapId_ = 0;
            return true;
        }
        if (Bitmaps_.find(BitmapId) == Bitmaps_.end())
        {
            if (Error)
                *Error = "SET CURRENT BITMAP requires existing bitmap " + std::to_string(BitmapId);
            return false;
        }
        CurrentBitmapId_ = BitmapId;
        return true;
    }

    int ImageStore::CurrentBitmap() const
    {
        return CurrentBitmapId_;
    }

    SDL_Surface* ImageStore::GetBitmapSurface(int BitmapId) const
    {
        const auto It = Bitmaps_.find(BitmapId);
        return (It != Bitmaps_.end()) ? It->second : nullptr;
    }

    SDL_Surface* ImageStore::EnsureScreenBitmap(std::string* Error)
    {
        const int Width = std::max(1, Kernel_.Core().WindowWidth());
        const int Height = std::max(1, Kernel_.Core().WindowHeight());
        if (SDL_Surface* Existing = GetBitmapSurface(0))
        {
            if (Existing->w == Width && Existing->h == Height && Existing->format == SDL_PIXELFORMAT_RGBA32)
                return Existing;
        }

        SDL_Surface* Surface = SDL_CreateSurface(Width, Height, SDL_PIXELFORMAT_RGBA32);
        if (!Surface)
        {
            if (Error)
                *Error = std::string("Screen bitmap allocation failed: ") + SDL_GetError();
            return nullptr;
        }
        if (!SDL_FillSurfaceRect(Surface, nullptr, 0))
        {
            if (Error)
                *Error = std::string("Screen bitmap initialization failed: ") + SDL_GetError();
            SDL_DestroySurface(Surface);
            return nullptr;
        }
        // A picture loaded onto the screen keeps its file format; drawing over it must convert it, not erase it.
        if (SDL_Surface* Existing = GetBitmapSurface(0))
        {
            if (!SDL_SetSurfaceBlendMode(Existing, SDL_BLENDMODE_NONE) ||
                !SDL_BlitSurfaceScaled(Existing, nullptr, Surface, nullptr, SDL_SCALEMODE_LINEAR))
            {
                if (Error)
                    *Error = std::string("Screen bitmap conversion failed: ") + SDL_GetError();
                SDL_DestroySurface(Surface);
                return nullptr;
            }
        }
        const bool Mirrored = MirroredBitmaps_.contains(0);
        const bool Flipped = FlippedBitmaps_.contains(0);
        if (!DeleteBitmap(0, Error))
        {
            SDL_DestroySurface(Surface);
            return nullptr;
        }
        Bitmaps_[0] = Surface;
        InvalidateBitmapGpuTexture(0);
        if (Mirrored)
            MirroredBitmaps_.insert(0);
        if (Flipped)
            FlippedBitmaps_.insert(0);
        CurrentBitmapId_ = 0;
        return Surface;
    }

    namespace
    {
        [[nodiscard]] int64_t RectArea(const SDL_Rect& Rect)
        {
            return int64_t{Rect.w} * Rect.h;
        }
    } // namespace

    void ImageStore::BitmapArea::Add(const SDL_Rect* Area)
    {
        if (!Area)
        {
            Whole = true;
            Count = 0;
            return;
        }
        if (Whole || Area->w <= 0 || Area->h <= 0)
            return;
        // Absorb every rectangle the new one touches, so that none overlap.
        SDL_Rect Merged = *Area;
        for (std::size_t Index = 0; Index < Count;)
        {
            if (!SDL_HasRectIntersection(&Merged, &Rects[Index]))
            {
                ++Index;
                continue;
            }
            SDL_GetRectUnion(&Merged, &Rects[Index], &Merged);
            Rects[Index] = Rects[--Count];
            Index = 0;
        }
        if (Count < MaximumRects)
        {
            Rects[Count++] = Merged;
            return;
        }
        // Full: merge with the rectangle whose union adds the least area, then place the union.
        std::size_t Best = 0;
        int64_t BestGrowth = std::numeric_limits<int64_t>::max();
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            SDL_Rect Union{};
            SDL_GetRectUnion(&Merged, &Rects[Index], &Union);
            const int64_t Growth = RectArea(Union) - RectArea(Merged) - RectArea(Rects[Index]);
            if (Growth < BestGrowth)
            {
                Best = Index;
                BestGrowth = Growth;
            }
        }
        SDL_GetRectUnion(&Merged, &Rects[Best], &Merged);
        Rects[Best] = Rects[--Count];
        Add(&Merged);
    }

    void ImageStore::BitmapArea::Add(const BitmapArea& Other)
    {
        if (Other.Whole)
            Add(nullptr);
        for (std::size_t Index = 0; Index < Other.Count; ++Index)
            Add(&Other.Rects[Index]);
    }

    std::size_t ImageStore::BitmapArea::Clip(const SDL_Surface& Surface,
                                             std::array<SDL_Rect, MaximumRects>& Output) const
    {
        const SDL_Rect Full{0, 0, Surface.w, Surface.h};
        if (Whole)
        {
            Output[0] = Full;
            return Full.w > 0 && Full.h > 0 ? 1 : 0;
        }
        std::size_t Written = 0;
        for (std::size_t Index = 0; Index < Count; ++Index)
            if (SDL_GetRectIntersection(&Rects[Index], &Full, &Output[Written]))
                ++Written;
        return Written;
    }

    void ImageStore::ReleaseBitmapTexture(BitmapTexture& Entry)
    {
        if (Entry.Texture && Kernel_.Gpu().GpuDevice())
            SDL_ReleaseGPUTexture(Kernel_.Gpu().GpuDevice(), Entry.Texture);
        Entry.Texture = nullptr;
    }

    SDL_GPUTexture* ImageStore::GetBitmapGpuTexture(int BitmapId)
    {
        const auto BmpIt = Bitmaps_.find(BitmapId);
        if (BmpIt == Bitmaps_.end() || !BmpIt->second)
        {
            return nullptr;
        }
        SDL_Surface* Surface = BmpIt->second;
        BitmapTexture& Entry = BitmapTextures_[BitmapId];
        if (Entry.Drawn.Empty())
            return nullptr;
        if (!Entry.Texture || Entry.Width != Surface->w || Entry.Height != Surface->h)
        {
            ReleaseBitmapTexture(Entry);
            Entry.Texture = Kernel_.Gpu().CreateGpuTextureFromSurface(Surface);
            if (!Entry.Texture)
                return nullptr;
            Entry.Width = Surface->w;
            Entry.Height = Surface->h;
            Entry.Pending.Clear();
            return Entry.Texture;
        }
        std::array<SDL_Rect, BitmapArea::MaximumRects> Areas{};
        const std::size_t AreaCount = Entry.Pending.Clip(*Surface, Areas);
        if (AreaCount != 0 && !Kernel_.Gpu().UpdateGpuTextureFromSurface(
                                  Entry.Texture, Surface, std::span<const SDL_Rect>(Areas.data(), AreaCount)))
        {
            DBC_LOG_WARN("Bitmap {} texture update failed: {}", BitmapId, SDL_GetError());
            ReleaseBitmapTexture(Entry);
            return nullptr;
        }
        Entry.Pending.Clear();
        return Entry.Texture;
    }

    void ImageStore::ClearScreenBitmap()
    {
        SDL_Surface* Screen = GetBitmapSurface(0);
        if (!Screen)
            return;
        BitmapTexture& Entry = BitmapTextures_[0];
        // A picture loaded onto the screen may have no alpha channel: filling it with zero would paint
        // opaque black over the 3D scene instead of clearing the 2D layer.
        if (Screen->format != SDL_PIXELFORMAT_RGBA32)
        {
            SDL_Surface* Transparent = SDL_CreateSurface(Screen->w, Screen->h, SDL_PIXELFORMAT_RGBA32);
            if (!Transparent)
            {
                DBC_LOG_WARN("Screen bitmap clear failed: {}", SDL_GetError());
                return;
            }
            SDL_DestroySurface(Screen);
            Bitmaps_[0] = Transparent;
            Screen = Transparent;
            Entry.Drawn.Add(nullptr);
        }
        std::array<SDL_Rect, BitmapArea::MaximumRects> Areas{};
        const std::size_t AreaCount = Entry.Drawn.Clip(*Screen, Areas);
        if (AreaCount != 0 && !SDL_FillSurfaceRects(Screen, Areas.data(), static_cast<int>(AreaCount), 0))
        {
            DBC_LOG_WARN("Screen bitmap clear failed: {}", SDL_GetError());
            Entry.Pending.Add(nullptr);
            return;
        }
        // The texture still shows what was erased.
        Entry.Pending.Add(Entry.Drawn);
        Entry.Drawn.Clear();
    }

    void ImageStore::InvalidateBitmapGpuTexture(int BitmapId, const SDL_Rect* Area)
    {
        BitmapTexture& Entry = BitmapTextures_[BitmapId];
        // Clipped here, in 64 bits, so that no BASIC coordinate overflows the rectangle arithmetic.
        SDL_Rect Inside{};
        const SDL_Surface* Surface = GetBitmapSurface(BitmapId);
        if (Area && Surface)
        {
            const int64_t Left = std::max<int64_t>(Area->x, 0);
            const int64_t Top = std::max<int64_t>(Area->y, 0);
            const int64_t Right = std::min<int64_t>(int64_t{Area->x} + Area->w, Surface->w);
            const int64_t Bottom = std::min<int64_t>(int64_t{Area->y} + Area->h, Surface->h);
            if (Right <= Left || Bottom <= Top)
                return;
            Inside = SDL_Rect{static_cast<int>(Left), static_cast<int>(Top), static_cast<int>(Right - Left),
                              static_cast<int>(Bottom - Top)};
        }
        const SDL_Rect* Changed = Area && Surface ? &Inside : nullptr;
        Entry.Pending.Add(Changed);
        Entry.Drawn.Add(Changed);
    }

    int ImageStore::BitmapWidth(int BitmapId) const
    {
        if (BitmapId < 0 || BitmapId > 32)
            return 0;
        const auto It = Bitmaps_.find(BitmapId);
        if (It == Bitmaps_.end() || !It->second)
            return BitmapId == 0 ? Kernel_.Core().WindowWidth() : 0;
        return It->second->w;
    }

    int ImageStore::BitmapHeight(int BitmapId) const
    {
        if (BitmapId < 0 || BitmapId > 32)
            return 0;
        const auto It = Bitmaps_.find(BitmapId);
        if (It == Bitmaps_.end() || !It->second)
            return BitmapId == 0 ? Kernel_.Core().WindowHeight() : 0;
        return It->second->h;
    }

    int ImageStore::ImageWidth(int ImageId) const
    {
        if (ImageId < 1 || ImageId > 65535)
            return 0;
        const auto It = Images_.find(ImageId);
        return It == Images_.end() ? 0 : It->second.Width;
    }

    int ImageStore::ImageHeight(int ImageId) const
    {
        if (ImageId < 1 || ImageId > 65535)
            return 0;
        const auto It = Images_.find(ImageId);
        return It == Images_.end() ? 0 : It->second.Height;
    }

    int ImageStore::BitmapExist(int BitmapId) const
    {
        if (BitmapId < 0 || BitmapId > 32)
            return 0;
        if (BitmapId == 0)
            return 1;
        return Bitmaps_.find(BitmapId) != Bitmaps_.end() ? 1 : 0;
    }

    int ImageStore::BitmapDepth(int BitmapId) const
    {
        if (BitmapId < 0 || BitmapId > 32)
            return 0;
        if (BitmapId == 0)
            return Kernel_.Core().ScreenDepth();
        const auto It = Bitmaps_.find(BitmapId);
        if (It == Bitmaps_.end() || !It->second)
            return 0;
        return SDL_BITSPERPIXEL(It->second->format);
    }

    int ImageStore::BitmapMirrored(int BitmapId) const
    {
        if (BitmapExist(BitmapId) == 0)
            return 0;
        return MirroredBitmaps_.contains(BitmapId) ? 1 : 0;
    }

    int ImageStore::BitmapFlipped(int BitmapId) const
    {
        if (BitmapExist(BitmapId) == 0)
            return 0;
        return FlippedBitmaps_.contains(BitmapId) ? 1 : 0;
    }

    bool ImageStore::CreateBitmap(int BitmapId, int Width, int Height, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (BitmapId == 0)
        {
            if (Error)
                *Error = "CREATE BITMAP cannot replace the screen bitmap";
            return false;
        }
        if (Width == 0 && Height == 0 && Kernel_.RuntimeCompatibility().CreateBitmapAcceptsNeutralDimensions())
        {
            // Permanent media stubs return neutral dimensions; the official Classic example still selects this bitmap.
            Width = 1;
            Height = 1;
        }
        if (Width <= 0 || Height <= 0)
        {
            if (Error)
                *Error = "CREATE BITMAP requires positive dimensions";
            return false;
        }
        SDL_Surface* Surface = SDL_CreateSurface(Width, Height, BitmapPixelFormat(BitmapFormat_));
        if (!Surface)
        {
            if (Error)
                *Error = std::string("CREATE BITMAP failed: ") + SDL_GetError();
            return false;
        }

        if (!SDL_FillSurfaceRect(Surface, nullptr, 0))
        {
            if (Error)
                *Error = std::string("Bitmap initialization failed: ") + SDL_GetError();
            SDL_DestroySurface(Surface);
            return false;
        }
        const auto It = Bitmaps_.find(BitmapId);
        if (It != Bitmaps_.end() && It->second)
            SDL_DestroySurface(It->second);
        Bitmaps_[BitmapId] = Surface;
        MirroredBitmaps_.erase(BitmapId);
        FlippedBitmaps_.erase(BitmapId);
        CurrentBitmapId_ = BitmapId;
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::BlurBitmap(int BitmapId, int BlurLevel, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (BlurLevel > 9)
        {
            if (Error)
                *Error = "BLUR BITMAP supports blur levels up to 9";
            return false;
        }
        if (BitmapId == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        SDL_Surface* Bitmap = RequireBitmapSurface(BitmapId, "BLUR BITMAP", Error);
        if (!Bitmap)
            return false;
        if (BlurLevel <= 1)
            return true;
        auto It = Bitmaps_.find(BitmapId);

        SDL_Surface* Source = SDL_ConvertSurface(Bitmap, SDL_PIXELFORMAT_RGBA32);
        if (!Source)
        {
            if (Error)
                *Error = std::string("BLUR BITMAP conversion failed: ") + SDL_GetError();
            return false;
        }

        SDL_Surface* Out = SDL_CreateSurface(Source->w, Source->h, Source->format);
        if (!Out)
        {
            SDL_DestroySurface(Source);
            {
                if (Error)
                    *Error = std::string("BLUR BITMAP allocation failed: ") + SDL_GetError();
                return false;
            }
        }

        const bool SourceLocked = SDL_LockSurface(Source);
        const bool OutLocked = SourceLocked ? SDL_LockSurface(Out) : false;
        if (!SourceLocked || !OutLocked)
        {
            if (OutLocked)
            {
                SDL_UnlockSurface(Out);
            }
            if (SourceLocked)
            {
                SDL_UnlockSurface(Source);
            }
            SDL_DestroySurface(Out);
            SDL_DestroySurface(Source);
            {
                if (Error)
                    *Error = std::string("BLUR BITMAP lock failed: ") + SDL_GetError();
                return false;
            }
        }

        const auto* Src = static_cast<const Uint8*>(Source->pixels);
        auto* Dst = static_cast<Uint8*>(Out->pixels);
        const int RangeBegin = -(BlurLevel / 2);
        const int RangeEnd = BlurLevel + RangeBegin - 1;

        for (int Y = 0; Y < Source->h; ++Y)
        {
            for (int X = 0; X < Source->w; ++X)
            {
                int R = 0;
                int G = 0;
                int B = 0;
                int A = 0;
                int Count = 0;

                for (int Oy = RangeBegin; Oy <= RangeEnd; ++Oy)
                {
                    const int Yy = Y + Oy;
                    if (Yy < 0 || Yy >= Source->h)
                        continue;
                    for (int Ox = RangeBegin; Ox <= RangeEnd; ++Ox)
                    {
                        const int Xx = X + Ox;
                        if (Xx < 0 || Xx >= Source->w)
                            continue;
                        const Uint8* P = Src + Yy * Source->pitch + Xx * 4;
                        R += static_cast<int>(P[0]);
                        G += static_cast<int>(P[1]);
                        B += static_cast<int>(P[2]);
                        A += static_cast<int>(P[3]);
                        ++Count;
                    }
                }

                Uint8* Q = Dst + Y * Out->pitch + X * 4;
                Q[0] = static_cast<Uint8>(R / Count);
                Q[1] = static_cast<Uint8>(G / Count);
                Q[2] = static_cast<Uint8>(B / Count);
                Q[3] = static_cast<Uint8>(A / Count);
            }
        }

        SDL_UnlockSurface(Out);
        SDL_UnlockSurface(Source);
        SDL_DestroySurface(Source);
        if (It->second)
        {
            SDL_DestroySurface(It->second);
        }
        It->second = Out;
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::CopyBitmap(int SrcId, int DstId, std::string* Error)
    {
        if (!ValidateBitmapId(SrcId, Error))
            return false;
        if (!ValidateBitmapId(DstId, Error))
            return false;
        auto SrcIt = Bitmaps_.find(SrcId);
        SDL_Surface* Source = SrcIt == Bitmaps_.end() ? nullptr : SrcIt->second;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> Captured(nullptr, SDL_DestroySurface);
        if (!Source && SrcId == 0)
        {
            Captured.reset(Kernel_.Renderer().CaptureScreen());
            Source = Captured.get();
        }
        if (!Source)
        {
            if (Error)
                *Error = "COPY BITMAP requires existing source bitmap " + std::to_string(SrcId);
            return false;
        }
        const SDL_PixelFormat DestinationFormat =
            DstId == 0 ? SDL_PIXELFORMAT_RGBA32 : BitmapPixelFormat(BitmapFormat_);
        SDL_Surface* Copy = SDL_ConvertSurface(Source, DestinationFormat);
        if (!Copy)
        {
            if (Error)
                *Error = std::string("COPY BITMAP conversion failed: ") + SDL_GetError();
            return false;
        }
        auto DstIt = Bitmaps_.find(DstId);
        if (DstIt != Bitmaps_.end() && DstIt->second)
        {
            SDL_DestroySurface(DstIt->second);
        }
        Bitmaps_[DstId] = Copy;
        if (DstId == 0)
            Kernel_.Sprites().ClearBobTrails();
        if (MirroredBitmaps_.contains(SrcId))
            MirroredBitmaps_.insert(DstId);
        else
            MirroredBitmaps_.erase(DstId);
        if (FlippedBitmaps_.contains(SrcId))
            FlippedBitmaps_.insert(DstId);
        else
            FlippedBitmaps_.erase(DstId);
        InvalidateBitmapGpuTexture(DstId);
        return true;
    }

    bool ImageStore::CopyBitmapRegion(int SrcId, int Sx1, int Sy1, int Sx2, int Sy2, int DstId, int Dx1, int Dy1,
                                      int Dx2, int Dy2, std::string* Error)
    {
        const auto ValidExtent = [](int First, int Last)
        {
            const auto Extent = static_cast<std::int64_t>(Last) - First;
            return Extent > 0 && Extent <= std::numeric_limits<int>::max();
        };
        if (!ValidExtent(Sx1, Sx2) || !ValidExtent(Sy1, Sy2) || !ValidExtent(Dx1, Dx2) || !ValidExtent(Dy1, Dy2))
        {
            if (Error)
                *Error = "COPY BITMAP requires positive source and destination rectangles within the integer range";
            return false;
        }

        if (!ValidateBitmapId(SrcId, Error))
            return false;
        if (!ValidateBitmapId(DstId, Error))
            return false;
        if (DstId == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        auto SrcIt = Bitmaps_.find(SrcId);
        auto DstIt = Bitmaps_.find(DstId);
        SDL_Surface* Source = SrcIt == Bitmaps_.end() ? nullptr : SrcIt->second;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> Captured(nullptr, SDL_DestroySurface);
        if (!Source && SrcId == 0)
        {
            Captured.reset(Kernel_.Renderer().CaptureScreen());
            Source = Captured.get();
        }
        if (!Source)
        {
            if (Error)
                *Error = "COPY BITMAP requires existing source bitmap " + std::to_string(SrcId);
            return false;
        }
        if (DstIt == Bitmaps_.end() || !DstIt->second)
        {
            if (DstId != 0)
            {
                if (Error)
                    *Error = "COPY BITMAP requires existing destination bitmap " + std::to_string(DstId);
                return false;
            }
            SDL_Surface* Screen =
                SrcId == 0 && Source ? SDL_DuplicateSurface(Source) : Kernel_.Renderer().CaptureScreen();
            if (!Screen)
            {
                if (Error)
                    *Error = std::string("COPY BITMAP destination capture failed: ") + SDL_GetError();
                return false;
            }
            Bitmaps_[0] = Screen;
            DstIt = Bitmaps_.find(0);
        }
        SDL_Rect SrcRect{Sx1, Sy1, Sx2 - Sx1, Sy2 - Sy1};
        SDL_Rect DstRect{Dx1, Dy1, Dx2 - Dx1, Dy2 - Dy1};
        const SDL_PixelFormat DestinationFormat =
            DstId == 0 ? SDL_PIXELFORMAT_RGBA32 : BitmapPixelFormat(BitmapFormat_);
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> CompatibleSource(
            SDL_ConvertSurface(Source, DestinationFormat), SDL_DestroySurface);
        if (!CompatibleSource)
        {
            if (Error)
                *Error = std::string("COPY BITMAP source conversion failed: ") + SDL_GetError();
            return false;
        }
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> CompatibleDestination(
            SDL_ConvertSurface(DstIt->second, DestinationFormat), SDL_DestroySurface);
        if (!CompatibleDestination)
        {
            if (Error)
                *Error = std::string("COPY BITMAP destination conversion failed: ") + SDL_GetError();
            return false;
        }
        if (!SDL_BlitSurfaceScaled(CompatibleSource.get(), &SrcRect, CompatibleDestination.get(), &DstRect,
                                   SDL_SCALEMODE_LINEAR))
        {
            if (Error)
                *Error = std::string("COPY BITMAP failed: ") + SDL_GetError();
            return false;
        }
        SDL_DestroySurface(DstIt->second);
        DstIt->second = CompatibleDestination.release();
        InvalidateBitmapGpuTexture(DstId);
        return true;
    }

    bool ImageStore::MirrorBitmap(int BitmapId, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (BitmapId == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        SDL_Surface* Bitmap = RequireBitmapSurface(BitmapId, "MIRROR BITMAP", Error);
        if (!Bitmap)
            return false;
        auto It = Bitmaps_.find(BitmapId);
        SDL_Surface* Surf = SDL_ConvertSurface(Bitmap, SDL_PIXELFORMAT_RGBA32);
        if (!Surf)
        {
            if (Error)
                *Error = std::string("MIRROR BITMAP conversion failed: ") + SDL_GetError();
            return false;
        }
        if (!SDL_LockSurface(Surf))
        {
            SDL_DestroySurface(Surf);
            {
                if (Error)
                    *Error = std::string("MIRROR BITMAP lock failed: ") + SDL_GetError();
                return false;
            }
        }
        auto* Pixels = static_cast<Uint32*>(Surf->pixels);
        const int W = Surf->w;
        const int H = Surf->h;
        const int Pitch32 = Surf->pitch / 4;
        for (int Y = 0; Y < H; ++Y)
        {
            for (int X = 0; X < W / 2; ++X)
            {
                std::swap(Pixels[Y * Pitch32 + X], Pixels[Y * Pitch32 + (W - 1 - X)]);
            }
        }
        SDL_UnlockSurface(Surf);
        SDL_DestroySurface(It->second);
        It->second = Surf;
        if (!MirroredBitmaps_.erase(BitmapId))
            MirroredBitmaps_.insert(BitmapId);
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::SaveBitmap(const std::string& Path, int BitmapId, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;

        struct SurfaceDeleter
        {
            void operator()(SDL_Surface* Surface) const
            {
                if (Surface)
                {
                    SDL_DestroySurface(Surface);
                }
            }
        };

        std::unique_ptr<SDL_Surface, SurfaceDeleter> Captured;

        SDL_Surface* Source = nullptr;
        if (BitmapId == 0)
        {
            Captured.reset(Kernel_.Renderer().CaptureScreen());
            Source = Captured.get();
        }
        else if (const auto It = Bitmaps_.find(BitmapId); It != Bitmaps_.end())
        {
            Source = It->second;
        }
        if (!Source)
        {
            if (Error)
                *Error = "SAVE BITMAP requires existing bitmap " + std::to_string(BitmapId);
            return false;
        }

        if (!EnsureParentDirectory(Path, Error))
            return false;
        if (!SaveSurface(Source, Path))
        {
            if (Error)
                *Error = std::string("SAVE BITMAP failed for '") + Path + "': " + SDL_GetError();
            return false;
        }
        return true;
    }

    bool ImageStore::FlipBitmap(int BitmapId, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (BitmapId == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        SDL_Surface* Bitmap = RequireBitmapSurface(BitmapId, "FLIP BITMAP", Error);
        if (!Bitmap)
            return false;
        auto It = Bitmaps_.find(BitmapId);
        SDL_Surface* Surf = SDL_ConvertSurface(Bitmap, SDL_PIXELFORMAT_RGBA32);
        if (!Surf)
        {
            if (Error)
                *Error = std::string("FLIP BITMAP conversion failed: ") + SDL_GetError();
            return false;
        }
        if (!SDL_LockSurface(Surf))
        {
            SDL_DestroySurface(Surf);
            {
                if (Error)
                    *Error = std::string("FLIP BITMAP lock failed: ") + SDL_GetError();
                return false;
            }
        }
        auto* Pixels = static_cast<Uint32*>(Surf->pixels);
        const int W = Surf->w;
        const int H = Surf->h;
        const int Pitch32 = Surf->pitch / 4;
        for (int Y = 0; Y < H / 2; ++Y)
        {
            for (int X = 0; X < W; ++X)
            {
                std::swap(Pixels[Y * Pitch32 + X], Pixels[(H - 1 - Y) * Pitch32 + X]);
            }
        }
        SDL_UnlockSurface(Surf);
        SDL_DestroySurface(It->second);
        It->second = Surf;
        if (!FlippedBitmaps_.erase(BitmapId))
            FlippedBitmaps_.insert(BitmapId);
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::FadeBitmap(int BitmapId, int FadePercent, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (BitmapId == 0)
            if (!Kernel_.Sprites().MaterializePresentedBobTrails(Error))
                return false;
        SDL_Surface* Bitmap = RequireBitmapSurface(BitmapId, "FADE BITMAP", Error);
        if (!Bitmap)
            return false;
        auto It = Bitmaps_.find(BitmapId);
        const float Factor = std::clamp(static_cast<float>(FadePercent) / 100.0f, 0.0f, 1.0f);
        SDL_Surface* Surf = SDL_ConvertSurface(Bitmap, SDL_PIXELFORMAT_RGBA32);
        if (!Surf)
        {
            if (Error)
                *Error = std::string("FADE BITMAP conversion failed: ") + SDL_GetError();
            return false;
        }
        if (!SDL_LockSurface(Surf))
        {
            SDL_DestroySurface(Surf);
            {
                if (Error)
                    *Error = std::string("FADE BITMAP lock failed: ") + SDL_GetError();
                return false;
            }
        }
        auto* P = static_cast<Uint8*>(Surf->pixels);
        for (int Y = 0; Y < Surf->h; ++Y)
        {
            for (int X = 0; X < Surf->w; ++X)
            {
                Uint8* Px = P + Y * Surf->pitch + X * 4;
                Px[0] = static_cast<Uint8>(static_cast<float>(Px[0]) * Factor);
                Px[1] = static_cast<Uint8>(static_cast<float>(Px[1]) * Factor);
                Px[2] = static_cast<Uint8>(static_cast<float>(Px[2]) * Factor);
            }
        }
        SDL_UnlockSurface(Surf);
        SDL_DestroySurface(It->second);
        It->second = Surf;
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::SetBitmapFormat(int Format, std::string* Error)
    {
        if (BitmapPixelFormat(Format) == SDL_PIXELFORMAT_UNKNOWN)
        {
            if (Error)
                *Error = "Unsupported SET BITMAP FORMAT value " + std::to_string(Format);
            return false;
        }
        BitmapFormat_ = Format == -1 ? 21 : Format;
        return true;
    }

    SDL_Surface* ImageStore::RequireBitmapSurface(int BitmapId, const char* Command, std::string* Error)
    {
        if (const auto It = Bitmaps_.find(BitmapId); It != Bitmaps_.end() && It->second)
            return It->second;
        if (BitmapId != 0)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing bitmap " + std::to_string(BitmapId);
            return nullptr;
        }
        SDL_Surface* Captured = Kernel_.Renderer().CaptureScreen();
        if (!Captured)
        {
            if (Error)
                *Error = std::string(Command) + " could not capture bitmap 0: " + SDL_GetError();
            return nullptr;
        }
        Bitmaps_[0] = Captured;
        MirroredBitmaps_.erase(0);
        FlippedBitmaps_.erase(0);
        InvalidateBitmapGpuTexture(0);
        return Captured;
    }

    bool ImageStore::StoreBitmapFromSurface(int BitmapId, SDL_Surface* Surface, std::string* Error)
    {
        if (!ValidateBitmapId(BitmapId, Error))
            return false;
        if (!Surface)
        {
            if (Error)
                *Error = "Cannot store a null bitmap surface";
            return false;
        }
        SDL_Surface* Copy = SDL_ConvertSurface(Surface, BitmapPixelFormat(BitmapFormat_));
        if (!Copy)
        {
            if (Error)
                *Error = std::string("Bitmap surface conversion failed: ") + SDL_GetError();
            return false;
        }
        if (const auto It = Bitmaps_.find(BitmapId); It != Bitmaps_.end() && It->second)
            SDL_DestroySurface(It->second);
        Bitmaps_[BitmapId] = Copy;
        MirroredBitmaps_.erase(BitmapId);
        FlippedBitmaps_.erase(BitmapId);
        CurrentBitmapId_ = BitmapId;
        if (BitmapId == 0)
            Kernel_.Sprites().ClearBobTrails();
        InvalidateBitmapGpuTexture(BitmapId);
        return true;
    }

    bool ImageStore::ResolveAssetPath(std::string& Output, const std::string& Path, std::string* Error)
    {
        const std::string Normalized = NormalizePath(Path);
        (void)Error;

#if defined(DBC_MOBILE) && defined(__APPLE__)
        if (const char* BasePath = SDL_GetBasePath())
        {
            Output = dbc::PathToUtf8((dbc::PathFromUtf8(BasePath) / dbc::PathFromUtf8(Normalized)));
            return true;
        }
        {
            Output = Normalized;
            return true;
        }
#elif defined(__ANDROID__)
        // SDL_IOFromFile resolves APK assets natively.
        {
            Output = Normalized;
            return true;
        }
#endif

        namespace fs = std::filesystem;
        const fs::path Raw = dbc::PathFromUtf8(Normalized);

        std::vector<fs::path> SearchRoots = {dbc::PathFromUtf8("."), dbc::PathFromUtf8(".."),
                                             dbc::PathFromUtf8("../.."), dbc::PathFromUtf8("../../.."),
                                             dbc::PathFromUtf8("../../../..")};

        if (const char* BasePathC = SDL_GetBasePath())
        {
            const fs::path ExeBase = dbc::PathFromUtf8(BasePathC);
            SearchRoots.push_back(ExeBase);
            SearchRoots.push_back(ExeBase / "..");
            SearchRoots.push_back(ExeBase / "../..");
            SearchRoots.push_back(ExeBase / "../../..");
#ifdef __APPLE__
            SearchRoots.push_back(ExeBase / "../Resources");
#endif
        }

        for (const fs::path& Base : SearchRoots)
        {

            const fs::path Direct = Base / Raw;
            bool DirectExists = false;
            if (!PathExists(Direct, DirectExists, Error))
                return false;
            if (DirectExists)
            {
                {
                    Output = dbc::PathToUtf8(Direct);
                    return true;
                }
            }
            fs::path DirectCi;
            if (!ResolveCaseInsensitivePath(DirectCi, Base, Raw, Error))
                return false;
            if (!DirectCi.empty())
            {
                {
                    Output = dbc::PathToUtf8(DirectCi);
                    return true;
                }
            }

            const fs::path InAssets = Base / "assets" / Raw;
            bool InAssetsExists = false;
            if (!PathExists(InAssets, InAssetsExists, Error))
                return false;
            if (InAssetsExists)
            {
                {
                    Output = dbc::PathToUtf8(InAssets);
                    return true;
                }
            }
            fs::path InAssetsCi;
            if (!ResolveCaseInsensitivePath(InAssetsCi, Base / "assets", Raw, Error))
                return false;
            if (!InAssetsCi.empty())
            {
                {
                    Output = dbc::PathToUtf8(InAssetsCi);
                    return true;
                }
            }
        }

        {
            Output = dbc::PathToUtf8(Raw);
            return true;
        }
    }

    void ImageStore::DestroyImage(Image& ImageValue)
    {
        if (ImageValue.Texture)
        {
            SDL_DestroyTexture(ImageValue.Texture);
            ImageValue.Texture = nullptr;
        }
        if (ImageValue.ColorKeyTexture)
        {
            SDL_DestroyTexture(ImageValue.ColorKeyTexture);
            ImageValue.ColorKeyTexture = nullptr;
        }
        if (ImageValue.GpuTexture || ImageValue.GpuColorKeyTexture)
            ++ReleasedGpuTextures_;
        if (ImageValue.GpuTexture && Kernel_.Gpu().GpuDevice())
        {
            SDL_ReleaseGPUTexture(Kernel_.Gpu().GpuDevice(), ImageValue.GpuTexture);
            ImageValue.GpuTexture = nullptr;
        }
        if (ImageValue.GpuColorKeyTexture && Kernel_.Gpu().GpuDevice())
        {
            SDL_ReleaseGPUTexture(Kernel_.Gpu().GpuDevice(), ImageValue.GpuColorKeyTexture);
            ImageValue.GpuColorKeyTexture = nullptr;
        }
        if (ImageValue.Surface)
        {
            SDL_DestroySurface(ImageValue.Surface);
            ImageValue.Surface = nullptr;
        }
        ImageValue.Width = 0;
        ImageValue.Height = 0;
        ImageValue.HasAlpha = false;
        ImageValue.HasPartialAlpha = false;
        ImageValue.SourcePath.clear();
    }

    bool ImageStore::FillImageFromSurface(Image& ImageValue, SDL_Surface* Surface, const std::string& SourcePath,
                                          std::string* Error)
    {
        if (!Surface)
        {
            if (Error)
                *Error = "Cannot upload a null image surface";
            return false;
        }
        Image Loaded;
        Loaded.Width = Surface->w;
        Loaded.Height = Surface->h;
        Loaded.SourcePath = SourcePath;
        if (!DetectSurfaceAlpha(Surface, Loaded.HasAlpha, Loaded.HasPartialAlpha, Error))
            return false;
        Loaded.Surface = SDL_DuplicateSurface(Surface);
        if (!Loaded.Surface)
        {
            if (Error)
                *Error = std::string("Cannot retain image pixels: ") + SDL_GetError();
            return false;
        }
        if (Kernel_.Gpu().Renderer())
        {
            Loaded.Texture = SDL_CreateTextureFromSurface(Kernel_.Gpu().Renderer(), Surface);
            if (!Loaded.Texture || !SDL_SetTextureBlendMode(Loaded.Texture, SDL_BLENDMODE_BLEND))
            {
                if (Error)
                    *Error = std::string("Cannot create image texture: ") + SDL_GetError();
                DestroyImage(Loaded);
                return false;
            }
        }
        Loaded.GpuTexture = Kernel_.Gpu().CreateGpuTextureFromSurface(Surface);
        if (!Loaded.GpuTexture)
        {
            if (Error)
                *Error = std::string("Cannot upload image texture: ") + SDL_GetError();
            DestroyImage(Loaded);
            return false;
        }
        DestroyImage(ImageValue);
        ImageValue = std::move(Loaded);
        return true;
    }

    int ImageStore::LoadImageInternalFromPath(const std::string& Resolved, int DivideTextureSize)
    {
        std::string Error;
        if (!Kernel_.Core().EnsureInitialized())
            return 0;
        const std::string CacheKey = Resolved + "#divide=" + std::to_string(std::max(0, DivideTextureSize));
        const auto CacheIt = TexturePathToImageId_.find(CacheKey);
        if (CacheIt != TexturePathToImageId_.end())
        {
            const auto ImgIt = Images_.find(CacheIt->second);
            if (ImgIt != Images_.end() && (ImgIt->second.Texture || ImgIt->second.GpuTexture))
            {
                return CacheIt->second;
            }
        }

        SDL_Surface* Surface = LoadSurface(Resolved);
        if (!Surface)
        {
            return 0;
        }
        Surface = ApplyTextureDivision(Surface, DivideTextureSize);
        if (!Surface)
            return 0;

        if (NextInternalImageId_ == std::numeric_limits<int>::min())
        {
            DBC_LOG_ERROR("Internal image identifiers are exhausted");
            return 0;
        }
        const int ImageId = NextInternalImageId_--;
        auto& Image = Images_[ImageId];
        const bool Uploaded = FillImageFromSurface(Image, Surface, Resolved, &Error);
        Image.DivideTextureSize = DivideTextureSize;
        SDL_DestroySurface(Surface);
        if (!Uploaded)
        {
            DestroyImage(Image);
            Images_.erase(ImageId);
            DBC_LOG_WARN("Image upload failed: {}", Error);
            return 0;
        }
        TexturePathToImageId_[CacheKey] = ImageId;
        return ImageId;
    }

    int ImageStore::LoadImageInternalFromEmbeddedTexture(const aiTexture* Texture, const std::string& CacheKey,
                                                         int DivideTextureSize)
    {
        std::string Error;
        if (!Texture)
        {
            return 0;
        }
        if (!Kernel_.Core().EnsureInitialized())
            return 0;

        const std::string DividedCacheKey = CacheKey + "#divide=" + std::to_string(std::max(0, DivideTextureSize));
        const auto CacheIt = TexturePathToImageId_.find(DividedCacheKey);
        if (CacheIt != TexturePathToImageId_.end())
        {
            return CacheIt->second;
        }

        SDL_Surface* Surface = nullptr;
        if (Texture->mHeight == 0)
        {
            SDL_IOStream* Io = SDL_IOFromConstMem(Texture->pcData, Texture->mWidth);
            if (Io)
            {
                Surface = IMG_Load_IO(Io, true);
            }
        }
        else
        {
            const int W = static_cast<int>(Texture->mWidth);
            const int H = static_cast<int>(Texture->mHeight);
            Surface = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
            if (Surface && SDL_LockSurface(Surface))
            {
                auto* Dst = static_cast<Uint8*>(Surface->pixels);
                for (int Y = 0; Y < H; ++Y)
                {
                    Uint8* Row = Dst + Y * Surface->pitch;
                    for (int X = 0; X < W; ++X)
                    {
                        const aiTexel& S = Texture->pcData[static_cast<size_t>(Y) * static_cast<size_t>(W) + X];
                        Uint8* P = Row + X * 4;
                        P[0] = S.r;
                        P[1] = S.g;
                        P[2] = S.b;
                        P[3] = S.a;
                    }
                }
                SDL_UnlockSurface(Surface);
            }
        }

        if (!Surface)
        {
            return 0;
        }
        Surface = ApplyTextureDivision(Surface, DivideTextureSize);
        if (!Surface)
            return 0;

        if (NextInternalImageId_ == std::numeric_limits<int>::min())
        {
            DBC_LOG_ERROR("Internal image identifiers are exhausted");
            return 0;
        }
        const int ImageId = NextInternalImageId_--;
        auto& Image = Images_[ImageId];
        const bool Uploaded = FillImageFromSurface(Image, Surface, CacheKey, &Error);
        Image.DivideTextureSize = DivideTextureSize;
        SDL_DestroySurface(Surface);
        if (!Uploaded)
        {
            DestroyImage(Image);
            Images_.erase(ImageId);
            DBC_LOG_WARN("Image upload failed: {}", Error);
            return 0;
        }
        TexturePathToImageId_[DividedCacheKey] = ImageId;
        return ImageId;
    }

    SDL_Texture* ImageStore::EnsureBlackKeyTexture(Image& ImageValue)
    {
        if (ImageValue.ColorKeyTexture)
        {
            return ImageValue.ColorKeyTexture;
        }
        if (ImageValue.SourcePath.empty())
        {
            return ImageValue.Texture;
        }

        SDL_Surface* Surface = LoadSurface(ImageValue.SourcePath);
        if (!Surface)
        {
            return ImageValue.Texture;
        }
        Surface = ApplyTextureDivision(Surface, ImageValue.DivideTextureSize);
        if (!Surface)
            return ImageValue.Texture;

        SDL_Surface* Rgba = SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(Surface);
        if (!Rgba)
        {
            return ImageValue.Texture;
        }

        if (SDL_LockSurface(Rgba))
        {
            auto* Pixels = static_cast<Uint8*>(Rgba->pixels);
            for (int Y = 0; Y < Rgba->h; ++Y)
            {
                Uint8* Row = Pixels + Y * Rgba->pitch;
                for (int X = 0; X < Rgba->w; ++X)
                {
                    Uint8* P = Row + X * 4;
                    if (P[0] == 0 && P[1] == 0 && P[2] == 0)
                    {
                        P[3] = 0;
                    }
                }
            }
            SDL_UnlockSurface(Rgba);
        }

        if (Kernel_.Gpu().Renderer())
        {
            ImageValue.ColorKeyTexture = SDL_CreateTextureFromSurface(Kernel_.Gpu().Renderer(), Rgba);
        }
        ImageValue.GpuColorKeyTexture = Kernel_.Gpu().CreateGpuTextureFromSurface(Rgba);
        SDL_DestroySurface(Rgba);
        if (!ImageValue.ColorKeyTexture)
        {
            return ImageValue.Texture;
        }

        SDL_SetTextureBlendMode(ImageValue.ColorKeyTexture, SDL_BLENDMODE_BLEND);
        return ImageValue.ColorKeyTexture;
    }

    // === Store image from surface ===

    bool ImageStore::StoreImageFromSurface(int ImageId, SDL_Surface* Surface, std::string* Error)
    {
        if (!ValidateImageId(ImageId, Error))
            return false;
        if (!Surface)
        {
            if (Error)
                *Error = "Cannot store a null image surface";
            return false;
        }

        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "Cannot store an image without an initialized graphics device";
            return false;
        }
        auto& Image = Images_[ImageId];
        const bool Uploaded = FillImageFromSurface(Image, Surface, "memblock", Error);
        if (!Uploaded)
        {
            if (!Image.Texture && !Image.GpuTexture)
                Images_.erase(ImageId);
            return false;
        }
        MissingImageWarned_.erase(ImageId);
        return true;
    }

    void ImageStore::WarnMissingImage(const char* Command, int ImageId)
    {
        if (MissingImageWarned_.insert(ImageId).second)
        {
            DBC_LOG_WARN("{} ignored: image {} does not exist", Command, ImageId);
        }
    }

    // === Fine-grained access ===

    Image* ImageStore::GetImage(int ImageId)
    {
        const auto It = Images_.find(ImageId);
        return It != Images_.end() ? &It->second : nullptr;
    }

    const Image* ImageStore::GetImage(int ImageId) const
    {
        const auto It = Images_.find(ImageId);
        return It != Images_.end() ? &It->second : nullptr;
    }

    void ImageStore::ClearOverlayBlits()
    {
        OverlayImageBlits_.clear();
    }

} // namespace dbc
