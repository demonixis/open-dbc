#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../engine_types.hpp"

struct SDL_Surface;
struct SDL_Texture;
struct SDL_GPUTexture;
struct aiTexture;

namespace dbc
{
    class EngineKernel;

    class ImageStore
    {
      public:
        explicit ImageStore(EngineKernel& Kernel);
        ~ImageStore();

        // Image operations
        bool LoadImageFile(const std::string& Path, int ImageId, int TextureFlag = 0, int DivideTextureSize = 0,
                           std::string* Error = nullptr);
        bool DeleteImage(int ImageId, std::string* Error = nullptr);
        int ImageExist(int ImageId) const;
        int ImageFileExist(const std::string& Path) const;
        bool PasteImage(int ImageId, int X, int Y, bool TransparentBlack = false, std::string* Error = nullptr);
        bool GetImage(int ImageId, int Left, int Top, int Right, int Bottom, std::string* Error = nullptr);
        bool SaveImage(const std::string& Path, int ImageId, std::string* Error = nullptr);
        void SetImageColorKey(int Red, int Green, int Blue);

        // Bitmap operations
        bool LoadBitmapFile(const std::string& Path, int BitmapId, std::string* Error = nullptr);
        bool DeleteBitmap(int BitmapId, std::string* Error = nullptr);
        bool SetCurrentBitmap(int BitmapId, std::string* Error = nullptr);
        int CurrentBitmap() const;
        int BitmapWidth(int BitmapId) const;
        int BitmapHeight(int BitmapId) const;
        int BitmapDepth(int BitmapId) const;
        int BitmapMirrored(int BitmapId) const;
        int BitmapFlipped(int BitmapId) const;
        int ImageWidth(int ImageId) const;
        int ImageHeight(int ImageId) const;
        int BitmapExist(int BitmapId) const;
        bool CreateBitmap(int BitmapId, int Width, int Height, std::string* Error = nullptr);
        bool BlurBitmap(int BitmapId, int BlurLevel, std::string* Error = nullptr);
        bool SaveBitmap(const std::string& Path, int BitmapId, std::string* Error = nullptr);
        bool CopyBitmap(int SrcId, int DstId, std::string* Error = nullptr);
        bool CopyBitmapRegion(int SrcId, int Sx1, int Sy1, int Sx2, int Sy2, int DstId, int Dx1, int Dy1, int Dx2,
                              int Dy2, std::string* Error = nullptr);
        bool MirrorBitmap(int BitmapId, std::string* Error = nullptr);
        bool FlipBitmap(int BitmapId, std::string* Error = nullptr);
        bool FadeBitmap(int BitmapId, int FadePercent, std::string* Error = nullptr);
        bool SetBitmapFormat(int Format, std::string* Error = nullptr);
        SDL_Surface* GetBitmapSurface(int BitmapId) const;
        SDL_Surface* EnsureScreenBitmap(std::string* Error = nullptr);
        SDL_Surface* RequireBitmapSurface(int BitmapId, const char* Command, std::string* Error = nullptr);
        // The bitmap's GPU texture, brought up to date. Null when the bitmap does not exist or has shown nothing
        // since it was last cleared, so there is nothing to draw.
        SDL_GPUTexture* GetBitmapGpuTexture(int BitmapId);
        // Records that the bitmap's pixels changed, within Area or anywhere. Every write to a bitmap reports it.
        void InvalidateBitmapGpuTexture(int BitmapId, const SDL_Rect* Area = nullptr);
        // Makes the screen bitmap transparent again, keeping it allocated and current. Only what was drawn since
        // the last clear is erased, and the GPU texture catches up once something is drawn again.
        void ClearScreenBitmap();

        // Asset path resolution (static — no kernel needed).
        static bool ResolveAssetPath(std::string& Output, const std::string& Path, std::string* Error = nullptr);

        // Image detail helpers (public so renderer / objects can call via kernel).
        void DestroyImage(Image& ImageValue);

        // Counts the images whose textures were released: a renderer that cached a scene naming textures rebuilds it
        // when this changes.
        [[nodiscard]] uint64_t ReleasedGpuTextures() const
        {
            return ReleasedGpuTextures_;
        }

        bool FillImageFromSurface(Image& ImageValue, SDL_Surface* Surface, const std::string& SourcePath,
                                  std::string* Error = nullptr);
        [[nodiscard]] int LoadImageInternalFromPath(const std::string& Resolved, int DivideTextureSize = 0);
        [[nodiscard]] int LoadImageInternalFromEmbeddedTexture(const aiTexture* Texture, const std::string& CacheKey,
                                                               int DivideTextureSize = 0);
        SDL_Texture* EnsureBlackKeyTexture(Image& ImageValue);

        // Insert / replace an image slot from a raw surface (caller retains ownership).
        bool StoreImageFromSurface(int ImageId, SDL_Surface* Surface, std::string* Error = nullptr);
        bool StoreBitmapFromSurface(int BitmapId, SDL_Surface* Surface, std::string* Error = nullptr);

        // Reports a command that had to skip a missing image, once per slot.
        void WarnMissingImage(const char* Command, int ImageId);

        // Fine-grained access (replaces raw container exposure).
        [[nodiscard]] Image* GetImage(int ImageId);
        [[nodiscard]] const Image* GetImage(int ImageId) const;

        template <typename Fn> void ForEachOverlayBlit(Fn&& FnValue) const
        {
            for (const auto& Blit : OverlayImageBlits_)
                FnValue(Blit);
        }

        void ClearOverlayBlits();

      private:
        friend class MatrixManager;

        uint64_t ReleasedGpuTextures_ = 0;

        [[nodiscard]] bool CloneImageInternal(int& Output, int ImageId, std::string* Error = nullptr);
        void ReleaseImageInternal(int ImageId);

        EngineKernel& Kernel_;
        std::unordered_map<int, Image> Images_;
        std::unordered_map<int, SDL_Surface*> Bitmaps_;

        // Changed pixels as a few rectangles, clipped to the bitmap when used. Overlapping rectangles merge, and
        // past MaximumRects the two whose union adds the least area do.
        struct BitmapArea
        {
            static constexpr std::size_t MaximumRects = 8;

            bool Whole = false;
            std::array<SDL_Rect, MaximumRects> Rects{};
            std::size_t Count = 0;

            [[nodiscard]] bool Empty() const
            {
                return !Whole && Count == 0;
            }

            void Add(const SDL_Rect* Area);
            void Add(const BitmapArea& Other);
            // The rectangles inside Surface, or the whole surface; returns how many were written.
            [[nodiscard]] std::size_t Clip(const SDL_Surface& Surface,
                                           std::array<SDL_Rect, MaximumRects>& Output) const;

            void Clear()
            {
                *this = {};
            }
        };

        struct BitmapTexture
        {
            SDL_GPUTexture* Texture = nullptr;
            int Width = 0;
            int Height = 0;
            // Where the texture differs from the bitmap.
            BitmapArea Pending{true};
            // Where the bitmap may hold visible pixels; unknown, so whole, until it is cleared.
            BitmapArea Drawn{true};
        };

        void ReleaseBitmapTexture(BitmapTexture& Entry);

        std::unordered_map<int, BitmapTexture> BitmapTextures_;
        std::unordered_set<int> MirroredBitmaps_;
        std::unordered_set<int> FlippedBitmaps_;
        std::unordered_map<std::string, int> TexturePathToImageId_;
        std::unordered_set<int> MissingImageWarned_;
        std::vector<OverlayImageBlit> OverlayImageBlits_;
        int CurrentBitmapId_ = 0;
        int BitmapFormat_ = 21;
        int ImageColorKey_ = 0;
        int NextInternalImageId_ = -1;
    };

} // namespace dbc
