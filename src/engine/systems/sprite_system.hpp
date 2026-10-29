#pragma once
#include <string>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SDL_Surface;

namespace dbc
{
    class EngineKernel;

    struct SpriteData
    {
        struct TextureCoordinate
        {
            float U = 0.0f;
            float V = 0.0f;
        };

        int ImageId = 0;
        int ImageWidth = 0;
        int ImageHeight = 0;
        float X = 0.0f;
        float Y = 0.0f;
        float Angle = 0.0f; // degrees
        bool Visible = true;
        bool Mirrored = false;
        bool Flipped = false;
        float ScaleX = 100.0f; // percentage
        float ScaleY = 100.0f;
        int SizeX = 0;
        int SizeY = 0;
        bool CustomSize = false;
        int OffsetX = 0;
        int OffsetY = 0;
        int Backsave = 2;
        bool Transparent = true;
        bool Additive = false;
        float Alpha = 255.0f;
        float DiffuseR = 255.0f;
        float DiffuseG = 255.0f;
        float DiffuseB = 255.0f;
        int Priority = 0;
        std::array<TextureCoordinate, 4> TextureCoordinates{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        int Frame = 0;
        int LastFrame = 0;
        int FrameCount = 1;
        int FrameAcross = 1;
        int FrameDown = 1;
        int FrameWidth = 0;
        int FrameHeight = 0;
        bool AnimatedAtlas = false;
        bool Playing = false;
        int AnimationStart = 0;
        int AnimationEnd = 0;
        int AnimationDelayMs = 0;
        std::chrono::steady_clock::time_point AnimationTick{};
        int HitOverlapStore = 0;
    };

    class SpriteSystem
    {
      public:
        explicit SpriteSystem(EngineKernel& Kernel);
        ~SpriteSystem();

        // Commands
        bool CreateSprite(int Id, int X, int Y, int ImageId, std::string* Error = nullptr);
        bool DeleteSprite(int Id, std::string* Error = nullptr);
        bool HideSprite(int Id, std::string* Error = nullptr);
        bool ShowSprite(int Id, std::string* Error = nullptr);
        void HideAllSprites();
        void ShowAllSprites();
        bool MirrorSprite(int Id, std::string* Error = nullptr);
        bool FlipSprite(int Id, std::string* Error = nullptr);
        bool SizeSprite(int Id, int W, int H, std::string* Error = nullptr);
        bool ScaleSprite(int Id, int ScalePercent, std::string* Error = nullptr);
        bool StretchSprite(int Id, int Sx, int Sy, std::string* Error = nullptr);
        bool OffsetSprite(int Id, int Ox, int Oy, std::string* Error = nullptr);
        bool PasteSprite(int Id, int X, int Y, std::string* Error = nullptr);
        bool SetSprite(int Id, int Backsave, int Transparency, std::string* Error = nullptr);
        bool PositionSprite(int Id, int X, int Y, std::string* Error = nullptr);
        bool RotateSprite(int Id, float Angle, std::string* Error = nullptr);
        bool MoveSprite(int Id, float Distance, std::string* Error = nullptr);
        bool CloneSprite(int Id, int DestinationId, std::string* Error = nullptr);
        bool CreateAnimatedSprite(int Id, const std::string& Path, int Across, int Down, int ImageId,
                                  std::string* Error = nullptr);
        bool PlaySprite(int Id, int StartFrame, int EndFrame, int DelayMs, std::string* Error = nullptr);
        bool SetSpriteFrame(int Id, int Frame, std::string* Error = nullptr);
        bool SetSpriteImage(int Id, int ImageId, std::string* Error = nullptr);
        bool SetSpriteTextureCoord(int Id, int Vertex, float U, float V, std::string* Error = nullptr);
        bool SetSpriteResize(int Mode, float OffsetX = 0.0f, float ScaleX = 1.0f, std::string* Error = nullptr);
        void DrawSpritesFirst();
        void DrawSpritesLast();
        bool SetSpriteAlpha(int Id, float Alpha, std::string* Error = nullptr);
        bool SetSpriteDiffuse(int Id, float R, float G, float B, std::string* Error = nullptr);
        bool SetSpritePriority(int Id, int Priority, std::string* Error = nullptr);

        // Queries
        int SpriteExist(int Id) const;
        bool SpriteX(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteY(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteWidth(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteHeight(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteImage(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteMirrored(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteFlipped(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteScaleX(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteScaleY(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteOffsetX(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteOffsetY(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteAngle(float& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteAlpha(float& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteRed(float& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteGreen(float& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteBlue(float& Output, int Id, std::string* Error = nullptr) const;
        bool SpritePriority(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteFrame(int& Output, int Id, std::string* Error = nullptr) const;
        bool SpriteVisible(int& Output, int Id, std::string* Error = nullptr) const;

        // Collision
        bool SpriteHit(int& Output, int Id, int TargetId, std::string* Error = nullptr);
        bool SpriteCollision(int& Output, int Id, int TargetId, std::string* Error = nullptr) const;

        // BOB commands
        bool CreateBob(int Id, int X, int Y, int ImageId, std::string* Error = nullptr);
        bool DeleteBob(int Id, std::string* Error = nullptr);
        bool FlipBob(int Id, std::string* Error = nullptr);
        bool MirrorBob(int Id, std::string* Error = nullptr);
        bool OffsetBob(int Id, int Ox, int Oy, std::string* Error = nullptr);
        bool PasteBob(int Id, int X, int Y, std::string* Error = nullptr);
        bool ScaleBob(int Id, int ScalePercent, std::string* Error = nullptr);
        bool SetBob(int Id, int Backsave, int Transparency, std::string* Error = nullptr);
        bool StretchBob(int Id, int Sx, int Sy, std::string* Error = nullptr);
        bool HideBob(int Id, std::string* Error = nullptr);
        bool ShowBob(int Id, std::string* Error = nullptr);
        bool PositionBob(int Id, int X, int Y, std::string* Error = nullptr);
        bool RotateBob(int Id, float Angle, std::string* Error = nullptr);
        bool MoveBob(int Id, float Distance, std::string* Error = nullptr);

        // BOB queries
        int BobExist(int Id) const;
        bool BobX(int& Output, int Id, std::string* Error = nullptr) const;
        bool BobY(int& Output, int Id, std::string* Error = nullptr) const;
        bool BobWidth(int& Output, int Id, std::string* Error = nullptr) const;
        bool BobHeight(int& Output, int Id, std::string* Error = nullptr) const;
        int BobImage(int Id) const;
        int BobMirrored(int Id) const;
        int BobFlipped(int Id) const;
        bool BobScaleX(int& Output, int Id, std::string* Error = nullptr) const;
        bool BobScaleY(int& Output, int Id, std::string* Error = nullptr) const;
        int BobOffsetX(int Id) const;
        int BobOffsetY(int Id) const;
        float BobAngle(int Id) const;
        bool BobHit(int& Output, int Id, int TargetId, std::string* Error = nullptr);
        bool BobCollision(int& Output, int Id, int TargetId, std::string* Error = nullptr) const;

        // Iteration (renderer)
        template <typename Fn> void ForEachVisibleSprite(Fn&& FnValue) const
        {
            std::vector<std::pair<int, const SpriteData*>> Visible;
            for (const auto& [Id, Sprite] : Sprites_)
            {
                if (Sprite.Visible && Sprite.ImageId > 0)
                    Visible.emplace_back(Id, &Sprite);
            }
            for (const auto& [Id, Bob] : Bobs_)
            {
                if (Bob.Visible && Bob.ImageId > 0)
                    Visible.emplace_back(Id, &Bob);
            }
            std::stable_sort(Visible.begin(), Visible.end(),
                             [](const auto& Left, const auto& Right)
                             {
                                 return Left.second->Priority == Right.second->Priority
                                            ? Left.first < Right.first
                                            : Left.second->Priority < Right.second->Priority;
                             });
            for (const auto& [UnusedId, Value] : Visible)
                FnValue(*Value);
        }

        template <typename Fn> void ForEachPastedSprite(Fn&& FnValue) const
        {
            for (const SpriteData& Value : PastedSprites_)
                FnValue(Value);
        }

        void ClearPastedSprites();
        void ClearBobTrails();
        bool MaterializePresentedBobTrails(std::string* Error = nullptr);
        bool CommitPresentedBobs(std::string* Error = nullptr);

        bool SpritesDrawFirst() const
        {
            return SpritesDrawFirst_;
        }

        float SpriteGlobalOffsetX() const
        {
            return ResizeOffsetX_;
        }

        float SpriteGlobalScaleX() const
        {
            return ResizeScaleX_;
        }

        void Shutdown();

      private:
        EngineKernel& Kernel_;
        std::unordered_map<int, SpriteData> Sprites_;
        std::unordered_map<int, SpriteData> Bobs_;
        std::unordered_map<int, SpriteData> PresentedBobs_;
        std::unordered_map<int, std::shared_ptr<SDL_Surface>> PresentedBobImages_;
        std::unordered_map<int, std::unordered_set<int>> BobHitContacts_;
        std::vector<SpriteData> PastedSprites_;
        bool SpritesDrawFirst_ = false;
        int ResizeMode_ = 0;
        float ResizeOffsetX_ = 0.0f;
        float ResizeScaleX_ = 1.0f;

        const SpriteData* Find(int Id) const;
        SpriteData* Find(int Id);
        const SpriteData* FindBob(int Id) const;
        SpriteData* FindBob(int Id);

        SpriteData* FindForCommand(int Id, const char* Command, std::string* Error = nullptr);
        SpriteData* FindBobForCommand(int Id, const char* Command, std::string* Error = nullptr);
        const SpriteData* Require(int Id, const char* Command, std::string* Error = nullptr) const;
        static bool ValidateSpriteId(int Id, std::string* Error = nullptr);
        bool ValidateImage(int ImageId, const char* Command, std::string* Error = nullptr) const;
        bool ApplyFrame(SpriteData& Sprite, int Frame, std::string* Error = nullptr);
        int BaseWidth(const SpriteData& Sprite) const;
        int BaseHeight(const SpriteData& Sprite) const;
        bool PreserveBobTrail(int Id, const SpriteData& Bob, std::string* Error = nullptr);
        bool RasterizeBobTrail(const SpriteData& Bob, SDL_Surface* Source, std::string* Error = nullptr);
    };

} // namespace dbc
