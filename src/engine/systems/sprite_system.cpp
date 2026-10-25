#include "sprite_system.hpp"
#include "../../language/numeric_conversion.hpp"
#include "engine_kernel.hpp"
#include "image_store.hpp"
#include "log.hpp"

#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace dbc
{
    SpriteSystem::SpriteSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    SpriteSystem::~SpriteSystem() = default;

    void SpriteSystem::Shutdown()
    {
        Sprites_.clear();
        Bobs_.clear();
        PresentedBobs_.clear();
        PresentedBobImages_.clear();
        BobHitContacts_.clear();
        PastedSprites_.clear();
    }

    const SpriteData* SpriteSystem::Find(int Id) const
    {
        auto It = Sprites_.find(Id);
        return (It != Sprites_.end()) ? &It->second : nullptr;
    }

    SpriteData* SpriteSystem::Find(int Id)
    {
        auto It = Sprites_.find(Id);
        return (It != Sprites_.end()) ? &It->second : nullptr;
    }

    SpriteData* SpriteSystem::FindForCommand(int Id, const char* Command, std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return nullptr;
        SpriteData* Value = Find(Id);
        if (!Value)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing sprite " + std::to_string(Id);
            return nullptr;
        }
        return Value;
    }

    SpriteData* SpriteSystem::FindBobForCommand(int Id, const char* Command, std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return nullptr;
        SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing bob " + std::to_string(Id);
            return nullptr;
        }
        return Value;
    }

    const SpriteData* SpriteSystem::Require(int Id, const char* Command, std::string* Error) const
    {
        if (!ValidateSpriteId(Id, Error))
            return nullptr;
        const SpriteData* Value = Find(Id);
        if (!Value)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing sprite " + std::to_string(Id);
            return nullptr;
        }
        return Value;
    }

    bool SpriteSystem::ValidateSpriteId(int Id, std::string* Error)
    {
        if (Id < 1 || Id > 65535)
        {
            if (Error)
                *Error = "Sprite number must be between 1 and 65535";
            return false;
        }
        return true;
    }

    bool SpriteSystem::ValidateImage(int ImageId, const char* Command, std::string* Error) const
    {
        if (ImageId < 1 || ImageId > 65535)
        {
            if (Error)
                *Error = std::string(Command) + " image number must be between 1 and 65535";
            return false;
        }
        return true;
    }

    // --- Commands ---

    bool SpriteSystem::CreateSprite(int Id, int X, int Y, int ImageId, std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return false;
        if (!ValidateImage(ImageId, "SPRITE", Error))
            return false;
        if (SpriteData* Existing = Find(Id))
        {
            Existing->X = static_cast<float>(X);
            Existing->Y = static_cast<float>(Y);
            if (Existing->AnimatedAtlas)
                return true;
            if (Existing->ImageId != ImageId)
            {
                Existing->ImageId = ImageId;
                if (Image* ImageValue = Kernel_.ImageStoreRef().GetImage(ImageId))
                {
                    Existing->ImageWidth = ImageValue->Width;
                    Existing->ImageHeight = ImageValue->Height;
                }
                Existing->TextureCoordinates = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
            }
            return true;
        }
        SpriteData Value;
        Value.ImageId = ImageId;
        Value.X = static_cast<float>(X);
        Value.Y = static_cast<float>(Y);
        Value.Backsave = 1;
        if (Image* ImageValue = Kernel_.ImageStoreRef().GetImage(ImageId))
        {
            Value.ImageWidth = ImageValue->Width;
            Value.ImageHeight = ImageValue->Height;
        }
        Sprites_.emplace(Id, std::move(Value));
        return true;
    }

    bool SpriteSystem::DeleteSprite(int Id, std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return false;
        if (!Sprites_.erase(Id))
        {
            if (Error)
                *Error = "DELETE SPRITE requires existing sprite " + std::to_string(Id);
            return false;
        }
        return true;
    }

    bool SpriteSystem::HideSprite(int Id, std::string* Error)
    {
        auto* S = FindForCommand(Id, "HideSprite", Error);
        if (!S)
            return false;
        S->Visible = false;
        return true;
    }

    bool SpriteSystem::ShowSprite(int Id, std::string* Error)
    {
        auto* S = FindForCommand(Id, "ShowSprite", Error);
        if (!S)
            return false;
        S->Visible = true;
        return true;
    }

    void SpriteSystem::HideAllSprites()
    {
        for (auto& [UnusedId, Sprite] : Sprites_)
            Sprite.Visible = false;
    }

    void SpriteSystem::ShowAllSprites()
    {
        for (auto& [UnusedId, Sprite] : Sprites_)
            Sprite.Visible = true;
    }

    bool SpriteSystem::MirrorSprite(int Id, std::string* Error)
    {
        auto* S = FindForCommand(Id, "MirrorSprite", Error);
        if (!S)
            return false;
        S->Mirrored = !S->Mirrored;
        return true;
    }

    bool SpriteSystem::FlipSprite(int Id, std::string* Error)
    {
        auto* S = FindForCommand(Id, "FlipSprite", Error);
        if (!S)
            return false;
        S->Flipped = !S->Flipped;
        return true;
    }

    bool SpriteSystem::SizeSprite(int Id, int W, int H, std::string* Error)
    {
        if (W < 0 || H < 0)
        {
            if (Error)
                *Error = "SIZE SPRITE requires non-negative dimensions";
            return false;
        }
        auto* S = FindForCommand(Id, "SizeSprite", Error);
        if (!S)
            return false;
        {
            S->SizeX = W;
            S->SizeY = H;
            S->CustomSize = true;
            S->ScaleX = static_cast<float>(W);
            S->ScaleY = static_cast<float>(H);
        }
        return true;
    }

    bool SpriteSystem::ScaleSprite(int Id, int ScalePercent, std::string* Error)
    {
        if (ScalePercent < 0 || ScalePercent > 250000)
        {
            if (Error)
                *Error = "SCALE SPRITE percentage must be between 0 and 250000";
            return false;
        }
        auto* S = FindForCommand(Id, "ScaleSprite", Error);
        if (!S)
            return false;
        {
            S->CustomSize = false;
            S->ScaleX = static_cast<float>(ScalePercent);
            S->ScaleY = static_cast<float>(ScalePercent);
        }
        return true;
    }

    bool SpriteSystem::StretchSprite(int Id, int Sx, int Sy, std::string* Error)
    {
        if (Sx < 0 || Sy < 0 || Sx > 250000 || Sy > 250000)
        {
            if (Error)
                *Error = "STRETCH SPRITE percentages must be between 0 and 250000";
            return false;
        }
        auto* S = FindForCommand(Id, "StretchSprite", Error);
        if (!S)
            return false;
        {
            S->CustomSize = false;
            S->ScaleX = static_cast<float>(Sx);
            S->ScaleY = static_cast<float>(Sy);
        }
        return true;
    }

    bool SpriteSystem::OffsetSprite(int Id, int Ox, int Oy, std::string* Error)
    {
        auto* S = FindForCommand(Id, "OffsetSprite", Error);
        if (!S)
            return false;
        {
            S->OffsetX = Ox;
            S->OffsetY = Oy;
        }
        return true;
    }

    bool SpriteSystem::PasteSprite(int Id, int X, int Y, std::string* Error)
    {
        SpriteData* ValuePointer = FindForCommand(Id, "PasteSprite", Error);
        if (!ValuePointer)
            return false;
        SpriteData Value = *ValuePointer;
        Value.X = static_cast<float>(X);
        Value.Y = static_cast<float>(Y);
        Value.Visible = true;
        PastedSprites_.push_back(std::move(Value));
        return true;
    }

    bool SpriteSystem::SetSprite(int Id, int Backsave, int Transparency, std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return false;
        if (Backsave < 0 || Backsave > 2)
        {
            if (Error)
                *Error = "SET SPRITE backsave mode must be between zero and two";
            return false;
        }
        if (Transparency < 0 || Transparency > 2)
        {
            if (Error)
                *Error = "SET SPRITE transparency mode must be between zero and two";
            return false;
        }
        SpriteData& Value = Sprites_[Id];
        Value.Backsave = Backsave;
        Value.Transparent = Transparency != 0;
        Value.Additive = Transparency == 2;
        return true;
    }

    bool SpriteSystem::PositionSprite(int Id, int X, int Y, std::string* Error)
    {
        auto* S = FindForCommand(Id, "PositionSprite", Error);
        if (!S)
            return false;
        {
            S->X = static_cast<float>(X);
            S->Y = static_cast<float>(Y);
        }
        return true;
    }

    bool SpriteSystem::RotateSprite(int Id, float Angle, std::string* Error)
    {
        auto* S = FindForCommand(Id, "RotateSprite", Error);
        if (!S)
            return false;
        S->Angle = Angle;
        return true;
    }

    bool SpriteSystem::MoveSprite(int Id, float Distance, std::string* Error)
    {
        auto* S = FindForCommand(Id, "MoveSprite", Error);
        if (!S)
            return false;
        {
            constexpr float Deg2rad = 3.14159265358979323846f / 180.0f;
            S->X += std::sin(S->Angle * Deg2rad) * Distance;
            S->Y -= std::cos(S->Angle * Deg2rad) * Distance;
        }
        return true;
    }

    bool SpriteSystem::CloneSprite(int Id, int DestinationId, std::string* Error)
    {
        const SpriteData* SourcePointer = Require(Id, "CLONE SPRITE", Error);
        if (!SourcePointer)
            return false;
        const SpriteData Source = *SourcePointer;
        if (!ValidateSpriteId(DestinationId, Error))
            return false;
        if (Sprites_.contains(DestinationId))
        {
            if (Error)
                *Error = "CLONE SPRITE destination already exists " + std::to_string(DestinationId);
            return false;
        }
        Sprites_.emplace(DestinationId, Source);
        return true;
    }

    bool SpriteSystem::CreateAnimatedSprite(int Id, const std::string& Path, int Across, int Down, int ImageId,
                                            std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return false;
        if (Sprites_.contains(Id))
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE cannot replace sprite " + std::to_string(Id);
            return false;
        }
        if (Across < 1 || Down < 1 || Across > std::numeric_limits<int>::max() / Down)
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE requires positive frame dimensions";
            return false;
        }
        if (ImageId < 1 || ImageId > 65535)
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE image number must be between 1 and 65535";
            return false;
        }
        if (Kernel_.ImageStoreRef().ImageExist(ImageId))
            if (!Kernel_.ImageStoreRef().DeleteImage(ImageId, Error))
                return false;
        if (!Kernel_.ImageStoreRef().LoadImage(Path, ImageId, 0, 0, Error))
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE could not load '" + Path + "'";
            return false;
        }
        Image* ImageValue = Kernel_.ImageStoreRef().GetImage(ImageId);
        if (!ImageValue)
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE did not create its image";
            return false;
        }
        if (ImageValue->Width / Across < 1 || ImageValue->Height / Down < 1)
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE frame grid exceeds the image dimensions";
            return false;
        }
        if (!CreateSprite(Id, -1000000, -1000000, ImageId, Error))
            return false;
        SpriteData& Value = Sprites_.at(Id);
        Value.AnimatedAtlas = true;
        Value.FrameAcross = Across;
        Value.FrameDown = Down;
        Value.FrameCount = Across * Down;
        Value.FrameWidth = ImageValue->Width / Across;
        Value.FrameHeight = ImageValue->Height / Down;
        if (Value.FrameWidth < 1 || Value.FrameHeight < 1)
        {
            if (Error)
                *Error = "CREATE ANIMATED SPRITE frame grid exceeds the image dimensions";
            return false;
        }
        Value.CustomSize = true;
        Value.SizeX = Value.FrameWidth;
        Value.SizeY = Value.FrameHeight;
        Value.ScaleX = static_cast<float>(Value.FrameWidth);
        Value.ScaleY = static_cast<float>(Value.FrameHeight);
        if (!ApplyFrame(Value, 0, Error))
            return false;
        return true;
    }

    bool SpriteSystem::ApplyFrame(SpriteData& Sprite, int Frame, std::string* Error)
    {
        if (Frame < 0 || (Sprite.AnimatedAtlas ? Frame >= Sprite.FrameCount : Frame >= 65535))
        {
            if (Error)
                *Error = "SET SPRITE FRAME is outside the sprite frame range";
            return false;
        }
        Sprite.LastFrame = Sprite.Frame;
        Sprite.Frame = Frame;
        if (!Sprite.AnimatedAtlas)
        {
            if (!ValidateImage(Frame + 1, "SET SPRITE FRAME", Error))
                return false;
            Sprite.ImageId = Frame + 1;
            Sprite.TextureCoordinates = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
            return true;
        }
        const int Column = Frame % Sprite.FrameAcross;
        const int Row = Frame / Sprite.FrameAcross;
        const float U0 = static_cast<float>(Column) / static_cast<float>(Sprite.FrameAcross);
        const float V0 = static_cast<float>(Row) / static_cast<float>(Sprite.FrameDown);
        const float U1 = static_cast<float>(Column + 1) / static_cast<float>(Sprite.FrameAcross);
        const float V1 = static_cast<float>(Row + 1) / static_cast<float>(Sprite.FrameDown);
        Sprite.TextureCoordinates = {{{U0, V0}, {U1, V0}, {U1, V1}, {U0, V1}}};
        return true;
    }

    bool SpriteSystem::PlaySprite(int Id, int StartFrame, int EndFrame, int DelayMs, std::string* Error)
    {
        SpriteData* ValuePointer = FindForCommand(Id, "PLAY SPRITE", Error);
        if (!ValuePointer)
            return false;
        SpriteData& Value = *ValuePointer;
        if (StartFrame < 1 || EndFrame < 1 || StartFrame > EndFrame || EndFrame > Value.FrameCount)
        {
            if (Error)
                *Error = "PLAY SPRITE frame range is invalid";
            return false;
        }
        if (DelayMs < 0)
        {
            if (Error)
                *Error = "PLAY SPRITE delay must be non-negative";
            return false;
        }
        const auto Now = std::chrono::steady_clock::now();
        const int Start = StartFrame - 1;
        const int End = EndFrame - 1;
        if (!Value.Playing || Value.AnimationStart != Start || Value.AnimationEnd != End)
        {
            Value.AnimationStart = Start;
            Value.AnimationEnd = End;
            Value.AnimationDelayMs = DelayMs;
            Value.Playing = true;
            if (!ApplyFrame(Value, Start, Error))
                return false;
            Value.AnimationTick = Now;
            return true;
        }
        Value.AnimationDelayMs = DelayMs;
        if (DelayMs == 0 || Now - Value.AnimationTick >= std::chrono::milliseconds(DelayMs))
        {
            if (!ApplyFrame(Value, Value.Frame < End ? Value.Frame + 1 : Start, Error))
                return false;
            Value.AnimationTick = Now;
        }
        return true;
    }

    bool SpriteSystem::SetSpriteFrame(int Id, int Frame, std::string* Error)
    {
        SpriteData* ValuePointer = FindForCommand(Id, "SET SPRITE FRAME", Error);
        if (!ValuePointer)
            return false;
        SpriteData& Value = *ValuePointer;
        Value.Playing = false;
        if (!ApplyFrame(Value, Frame > 0 ? Frame - 1 : 0, Error))
            return false;
        return true;
    }

    bool SpriteSystem::SetSpriteImage(int Id, int ImageId, std::string* Error)
    {
        if (!ValidateImage(ImageId, "SET SPRITE IMAGE", Error))
            return false;
        SpriteData* ValuePointer = FindForCommand(Id, "SET SPRITE IMAGE", Error);
        if (!ValuePointer)
            return false;
        SpriteData& Value = *ValuePointer;
        Value.ImageId = ImageId;
        Value.AnimatedAtlas = false;
        Value.Playing = false;
        Value.Frame = ImageId - 1;
        Value.TextureCoordinates = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
        if (ResizeMode_ != 0)
        {
            if (Image* ImageValue = Kernel_.ImageStoreRef().GetImage(ImageId))
            {
                Value.ImageWidth = ImageValue->Width;
                Value.ImageHeight = ImageValue->Height;
            }
        }
        return true;
    }

    bool SpriteSystem::SetSpriteTextureCoord(int Id, int Vertex, float U, float V, std::string* Error)
    {
        if (Vertex < 0 || Vertex > 3)
        {
            if (Error)
                *Error = "SET SPRITE TEXTURE COORD vertex must be between zero and three";
            return false;
        }
        SpriteData* ValuePointer = FindForCommand(Id, "SET SPRITE TEXTURE COORD", Error);
        if (!ValuePointer)
            return false;
        SpriteData& Value = *ValuePointer;
        Value.TextureCoordinates[static_cast<std::size_t>(Vertex)] = {U, V};
        return true;
    }

    bool SpriteSystem::SetSpriteResize(int Mode, float OffsetX, float ScaleX, std::string* Error)
    {
        if (Mode < 0 || Mode > 1)
        {
            if (Error)
                *Error = "SET SPRITE RESIZE mode must be zero or one";
            return false;
        }
        if (ScaleX <= 0.0f)
        {
            if (Error)
                *Error = "SET SPRITE RESIZE global X scale must be positive";
            return false;
        }
        ResizeMode_ = Mode;
        ResizeOffsetX_ = OffsetX;
        ResizeScaleX_ = ScaleX;
        return true;
    }

    void SpriteSystem::DrawSpritesFirst()
    {
        SpritesDrawFirst_ = true;
    }
    void SpriteSystem::DrawSpritesLast()
    {
        SpritesDrawFirst_ = false;
    }

    void SpriteSystem::ClearPastedSprites()
    {
        PastedSprites_.clear();
    }

    bool SpriteSystem::SetSpriteAlpha(int Id, float Alpha, std::string* Error)
    {
        if (Alpha < 0.0f || Alpha > 255.0f)
        {
            if (Error)
                *Error = "SET SPRITE ALPHA must be between zero and 255";
            return false;
        }
        auto* S = FindForCommand(Id, "SetSpriteAlpha", Error);
        if (!S)
            return false;
        S->Alpha = Alpha;
        return true;
    }

    bool SpriteSystem::SetSpriteDiffuse(int Id, float R, float G, float B, std::string* Error)
    {
        if (R < 0.0f || R > 255.0f || G < 0.0f || G > 255.0f || B < 0.0f || B > 255.0f)
        {
            if (Error)
                *Error = "SET SPRITE DIFFUSE components must be between zero and 255";
            return false;
        }
        auto* S = FindForCommand(Id, "SetSpriteDiffuse", Error);
        if (!S)
            return false;
        {
            S->DiffuseR = R;
            S->DiffuseG = G;
            S->DiffuseB = B;
        }
        return true;
    }

    bool SpriteSystem::SetSpritePriority(int Id, int Priority, std::string* Error)
    {
        auto* S = FindForCommand(Id, "SetSpritePriority", Error);
        if (!S)
            return false;
        S->Priority = Priority;
        return true;
    }

    // --- Queries ---

    int SpriteSystem::SpriteExist(int Id) const
    {
        return (Sprites_.count(Id) > 0) ? 1 : 0;
    }

    bool SpriteSystem::SpriteX(int& Output, int Id, std::string* Error) const
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        auto* CheckedSprite = Require(Id, "SPRITE X", &Diagnostic);
        if (!CheckedSprite)
            return false;
        {
            if (!language::ConvertInteger(CheckedSprite->X, Output, &Diagnostic))
            {
                Diagnostic = "SpriteX is outside the BASIC integer range";
                return false;
            }
            return true;
        }
    }

    bool SpriteSystem::SpriteY(int& Output, int Id, std::string* Error) const
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        auto* CheckedSprite = Require(Id, "SPRITE Y", &Diagnostic);
        if (!CheckedSprite)
            return false;
        {
            if (!language::ConvertInteger(CheckedSprite->Y, Output, &Diagnostic))
            {
                Diagnostic = "SpriteY is outside the BASIC integer range";
                return false;
            }
            return true;
        }
    }

    bool SpriteSystem::SpriteWidth(int& Output, int Id, std::string* Error) const
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        const SpriteData* ValuePointer = Require(Id, "SPRITE WIDTH", &Diagnostic);
        if (!ValuePointer)
            return false;
        const SpriteData& Value = *ValuePointer;
        if (Value.CustomSize)
        {
            Output = Value.SizeX;
            return true;
        }
        if (!language::ConvertInteger(static_cast<float>(BaseWidth(Value)) * Value.ScaleX / 100.0f, Output,
                                      &Diagnostic))
        {
            Diagnostic = "SpriteWidth is outside the BASIC integer range";
            return false;
        }
        return true;
    }

    bool SpriteSystem::SpriteHeight(int& Output, int Id, std::string* Error) const
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        const SpriteData* ValuePointer = Require(Id, "SPRITE HEIGHT", &Diagnostic);
        if (!ValuePointer)
            return false;
        const SpriteData& Value = *ValuePointer;
        if (Value.CustomSize)
        {
            Output = Value.SizeY;
            return true;
        }
        if (!language::ConvertInteger(static_cast<float>(BaseHeight(Value)) * Value.ScaleY / 100.0f, Output,
                                      &Diagnostic))
        {
            Diagnostic = "SpriteHeight is outside the BASIC integer range";
            return false;
        }
        return true;
    }

    int SpriteSystem::BaseWidth(const SpriteData& Sprite) const
    {
        if (Sprite.CustomSize)
            return Sprite.SizeX;
        if (Sprite.AnimatedAtlas)
            return Sprite.FrameWidth;
        if (Sprite.ImageWidth > 0)
            return Sprite.ImageWidth;
        Image* Value = Kernel_.ImageStoreRef().GetImage(Sprite.ImageId);
        return Value ? Value->Width : 0;
    }

    int SpriteSystem::BaseHeight(const SpriteData& Sprite) const
    {
        if (Sprite.CustomSize)
            return Sprite.SizeY;
        if (Sprite.AnimatedAtlas)
            return Sprite.FrameHeight;
        if (Sprite.ImageHeight > 0)
            return Sprite.ImageHeight;
        Image* Value = Kernel_.ImageStoreRef().GetImage(Sprite.ImageId);
        return Value ? Value->Height : 0;
    }

    bool SpriteSystem::SpriteImage(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE IMAGE", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->ImageId;
            return true;
        }
    }

    bool SpriteSystem::SpriteMirrored(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE MIRRORED", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->Mirrored ? 1 : 0;
            return true;
        }
    }

    bool SpriteSystem::SpriteFlipped(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE FLIPPED", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->Flipped ? 1 : 0;
            return true;
        }
    }

    bool SpriteSystem::SpriteScaleX(int& Output, int Id, std::string* Error) const
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        auto* CheckedSprite = Require(Id, "SPRITE SCALE X", &Diagnostic);
        if (!CheckedSprite)
            return false;
        {
            if (!language::ConvertInteger(CheckedSprite->ScaleX, Output, &Diagnostic))
            {
                Diagnostic = "SpriteScaleX is outside the BASIC integer range";
                return false;
            }
            return true;
        }
    }

    bool SpriteSystem::SpriteScaleY(int& Output, int Id, std::string* Error) const
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        auto* CheckedSprite = Require(Id, "SPRITE SCALE Y", &Diagnostic);
        if (!CheckedSprite)
            return false;
        {
            if (!language::ConvertInteger(CheckedSprite->ScaleY, Output, &Diagnostic))
            {
                Diagnostic = "SpriteScaleY is outside the BASIC integer range";
                return false;
            }
            return true;
        }
    }

    bool SpriteSystem::SpriteOffsetX(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE OFFSET X", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->OffsetX;
            return true;
        }
    }

    bool SpriteSystem::SpriteOffsetY(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE OFFSET Y", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->OffsetY;
            return true;
        }
    }

    bool SpriteSystem::SpriteAngle(float& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE ANGLE", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->Angle;
            return true;
        }
    }

    bool SpriteSystem::SpriteAlpha(float& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE ALPHA", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->Alpha;
            return true;
        }
    }

    bool SpriteSystem::SpriteRed(float& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE RED", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->DiffuseR;
            return true;
        }
    }

    bool SpriteSystem::SpriteGreen(float& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE GREEN", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->DiffuseG;
            return true;
        }
    }

    bool SpriteSystem::SpriteBlue(float& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE BLUE", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->DiffuseB;
            return true;
        }
    }

    bool SpriteSystem::SpritePriority(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE PRIORITY", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->Priority;
            return true;
        }
    }

    bool SpriteSystem::SpriteFrame(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* ValuePointer = Require(Id, "SPRITE FRAME", Error);
        if (!ValuePointer)
            return false;
        const SpriteData& Value = *ValuePointer;
        {
            Output = Value.AnimatedAtlas ? (Value.Playing ? Value.LastFrame + 1 : Value.Frame + 1) : Value.ImageId;
            return true;
        }
    }

    bool SpriteSystem::SpriteVisible(int& Output, int Id, std::string* Error) const
    {
        auto* CheckedSprite = Require(Id, "SPRITE VISIBLE", Error);
        if (!CheckedSprite)
            return false;
        {
            Output = CheckedSprite->Visible ? 1 : 0;
            return true;
        }
    }

    // --- Collision (simple AABB overlap) ---

    struct SpriteBounds
    {
        float MinX = 0.0f;
        float MinY = 0.0f;
        float MaxX = 0.0f;
        float MaxY = 0.0f;
    };

    static SpriteBounds SpriteBoundsFor(const SpriteData& Sprite, int Width, int Height, float GlobalOffsetX = 0.0f,
                                        float GlobalScaleX = 1.0f)
    {
        const float PositionX = Sprite.X * GlobalScaleX + GlobalOffsetX;
        const float PositionY = Sprite.Y;
        const float SpriteScaleX = Sprite.CustomSize ? 1.0f : Sprite.ScaleX / 100.0f;
        const float SpriteScaleY = Sprite.CustomSize ? 1.0f : Sprite.ScaleY / 100.0f;
        const float Left = PositionX - static_cast<float>(Sprite.OffsetX) * SpriteScaleX * GlobalScaleX;
        const float Top = PositionY - static_cast<float>(Sprite.OffsetY) * SpriteScaleY;
        const float Right = Left + static_cast<float>(Width) * GlobalScaleX;
        const float Bottom = Top + static_cast<float>(Height);
        std::array<std::array<float, 2>, 4> Points{{{Left, Top}, {Right, Top}, {Right, Bottom}, {Left, Bottom}}};
        const float Radians = Sprite.Angle * 3.14159265358979323846f / 180.0f;
        const float Cosine = std::cos(Radians);
        const float Sine = std::sin(Radians);
        SpriteBounds Result{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        for (auto& Point : Points)
        {
            const float DeltaX = Point[0] - PositionX;
            const float DeltaY = Point[1] - PositionY;
            Point[0] = PositionX + DeltaX * Cosine - DeltaY * Sine;
            Point[1] = PositionY + DeltaX * Sine + DeltaY * Cosine;
            Result.MinX = std::min(Result.MinX, Point[0]);
            Result.MinY = std::min(Result.MinY, Point[1]);
            Result.MaxX = std::max(Result.MaxX, Point[0]);
            Result.MaxY = std::max(Result.MaxY, Point[1]);
        }
        return Result;
    }

    static bool SpriteAabbOverlap(const SpriteData& A, int AWidth, int AHeight, const SpriteData& B, int BWidth,
                                  int BHeight, float GlobalOffsetX = 0.0f, float GlobalScaleX = 1.0f)
    {
        const SpriteBounds First = SpriteBoundsFor(A, AWidth, AHeight, GlobalOffsetX, GlobalScaleX);
        const SpriteBounds Second = SpriteBoundsFor(B, BWidth, BHeight, GlobalOffsetX, GlobalScaleX);
        return First.MinX < Second.MaxX && First.MaxX > Second.MinX && First.MinY < Second.MaxY &&
               First.MaxY > Second.MinY;
    }

    bool SpriteSystem::SpriteHit(int& Output, int Id, int TargetId, std::string* Error)
    {
        SpriteData* APointer = FindForCommand(Id, "SPRITE HIT", Error);
        if (!APointer)
            return false;
        SpriteData* A = APointer;
        if (TargetId < 0 || TargetId > 65535)
        {
            if (Error)
                *Error = "SPRITE HIT target must be between zero and 65535";
            return false;
        }
        if (!A->Visible)
        {
            Output = 0;
            return true;
        }
        int Aw{};
        if (!SpriteWidth(Aw, Id, Error))
            return false;
        int Ah{};
        if (!SpriteHeight(Ah, Id, Error))
            return false;

        if (TargetId == 0)
        {
            // Check all sprites.
            for (const auto& [Sid, Sprite] : Sprites_)
            {
                if (Sid == Id || !Sprite.Visible)
                    continue;
                int Sw{};
                if (!SpriteWidth(Sw, Sid, Error))
                    return false;
                int Sh{};
                if (!SpriteHeight(Sh, Sid, Error))
                    return false;
                if (SpriteAabbOverlap(*A, Aw, Ah, Sprite, Sw, Sh, ResizeOffsetX_, ResizeScaleX_))
                {
                    const int Result = A->HitOverlapStore == 0 ? Sid : 0;
                    A->HitOverlapStore = Sid;
                    {
                        Output = Result;
                        return true;
                    }
                }
            }
            A->HitOverlapStore = 0;
            {
                Output = 0;
                return true;
            }
        }

        const auto* B = Find(TargetId);
        if (!B)
        {
            if (Error)
                *Error = "SPRITE HIT requires existing target sprite " + std::to_string(TargetId);
            return false;
        }
        int TargetWidth{}, TargetHeight{};
        if (!SpriteWidth(TargetWidth, TargetId, Error) || !SpriteHeight(TargetHeight, TargetId, Error))
            return false;
        const int Overlap =
            B->Visible && SpriteAabbOverlap(*A, Aw, Ah, *B, TargetWidth, TargetHeight, ResizeOffsetX_, ResizeScaleX_)
                ? 1
                : 0;
        const int Result = Overlap != 0 && A->HitOverlapStore == 0 ? Overlap : 0;
        A->HitOverlapStore = Overlap;
        {
            Output = Result;
            return true;
        }
    }

    bool SpriteSystem::SpriteCollision(int& Output, int Id, int TargetId, std::string* Error) const
    {
        const SpriteData* SourcePointer = Require(Id, "SPRITE COLLISION", Error);
        if (!SourcePointer)
            return false;
        const SpriteData& Source = *SourcePointer;
        if (TargetId < 0 || TargetId > 65535)
        {
            if (Error)
                *Error = "SPRITE COLLISION target must be between zero and 65535";
            return false;
        }
        if (!Source.Visible)
        {
            Output = 0;
            return true;
        }
        int Width{};
        if (!SpriteWidth(Width, Id, Error))
            return false;
        int Height{};
        if (!SpriteHeight(Height, Id, Error))
            return false;
        if (TargetId == 0)
        {
            for (const auto& [CandidateId, Candidate] : Sprites_)
            {
                int CandidateWidth{}, CandidateHeight{};
                if (!SpriteWidth(CandidateWidth, CandidateId, Error) ||
                    !SpriteHeight(CandidateHeight, CandidateId, Error))
                    return false;
                if (CandidateId != Id && Candidate.Visible &&
                    SpriteAabbOverlap(Source, Width, Height, Candidate, CandidateWidth, CandidateHeight, ResizeOffsetX_,
                                      ResizeScaleX_))
                {
                    Output = CandidateId;
                    return true;
                }
            }
            {
                Output = 0;
                return true;
            }
        }
        const SpriteData* TargetPointer = Require(TargetId, "SPRITE COLLISION", Error);
        if (!TargetPointer)
            return false;
        const SpriteData& Target = *TargetPointer;
        int TargetWidth{}, TargetHeight{};
        if (!SpriteWidth(TargetWidth, TargetId, Error) || !SpriteHeight(TargetHeight, TargetId, Error))
            return false;
        {
            Output = Target.Visible && SpriteAabbOverlap(Source, Width, Height, Target, TargetWidth, TargetHeight,
                                                         ResizeOffsetX_, ResizeScaleX_)
                         ? 1
                         : 0;
            return true;
        }
    }

    // --- BOB support (delegates to same SpriteData, separate map) ---

    const SpriteData* SpriteSystem::FindBob(int Id) const
    {
        const auto It = Bobs_.find(Id);
        return It != Bobs_.end() ? &It->second : nullptr;
    }

    SpriteData* SpriteSystem::FindBob(int Id)
    {
        const auto It = Bobs_.find(Id);
        return It != Bobs_.end() ? &It->second : nullptr;
    }

    bool SpriteSystem::CreateBob(int Id, int X, int Y, int ImageId, std::string* Error)
    {
        if (!ValidateSpriteId(Id, Error))
            return false;
        if (!ValidateImage(ImageId, "BOB", Error))
            return false;
        if (SpriteData* Existing = FindBob(Id))
        {
            if (Existing->X != static_cast<float>(X) || Existing->Y != static_cast<float>(Y) ||
                Existing->ImageId != ImageId)
                if (!PreserveBobTrail(Id, *Existing, Error))
                    return false;
            Existing->X = static_cast<float>(X);
            Existing->Y = static_cast<float>(Y);
            if (Existing->ImageId != ImageId)
            {
                Existing->ImageId = ImageId;
                Existing->ImageWidth = 0;
                Existing->ImageHeight = 0;
                if (Image* ImageValue = Kernel_.ImageStoreRef().GetImage(ImageId))
                {
                    Existing->ImageWidth = ImageValue->Width;
                    Existing->ImageHeight = ImageValue->Height;
                }
            }
            Existing->Visible = true;
            return true;
        }

        SpriteData Value;
        Value.ImageId = ImageId;
        Value.X = static_cast<float>(X);
        Value.Y = static_cast<float>(Y);
        Value.Backsave = 1;
        if (Image* ImageValue = Kernel_.ImageStoreRef().GetImage(ImageId))
        {
            Value.ImageWidth = ImageValue->Width;
            Value.ImageHeight = ImageValue->Height;
        }
        Bobs_.emplace(Id, std::move(Value));
        return true;
    }

    bool SpriteSystem::DeleteBob(int Id, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "DELETE BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (!PreserveBobTrail(Id, *Value, Error))
            return false;
        Bobs_.erase(Id);
        PresentedBobs_.erase(Id);
        BobHitContacts_.erase(Id);
        for (auto& [UnusedId, Contacts] : BobHitContacts_)
            Contacts.erase(Id);
        return true;
    }

    bool SpriteSystem::FlipBob(int Id, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "FLIP BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (!PreserveBobTrail(Id, *Value, Error))
            return false;
        Value->Flipped = !Value->Flipped;
        return true;
    }

    bool SpriteSystem::MirrorBob(int Id, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "MIRROR BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (!PreserveBobTrail(Id, *Value, Error))
            return false;
        Value->Mirrored = !Value->Mirrored;
        return true;
    }

    bool SpriteSystem::OffsetBob(int Id, int Ox, int Oy, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "OFFSET BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Value->OffsetX != Ox || Value->OffsetY != Oy)
            if (!PreserveBobTrail(Id, *Value, Error))
                return false;
        Value->OffsetX = Ox;
        Value->OffsetY = Oy;
        return true;
    }

    bool SpriteSystem::PasteBob(int Id, int X, int Y, std::string* Error)
    {
        if (!MaterializePresentedBobTrails(Error))
            return false;
        SpriteData* CopyPointer = FindBobForCommand(Id, "PASTE BOB", Error);
        if (!CopyPointer)
            return false;
        SpriteData Copy = *CopyPointer;
        Copy.X = static_cast<float>(X);
        Copy.Y = static_cast<float>(Y);
        const Image* SourceImage = Kernel_.ImageStoreRef().GetImage(Copy.ImageId);
        if (!SourceImage || !SourceImage->Surface)
        {
            if (Error)
                *Error = "PASTE BOB image has no CPU surface";
            return false;
        }
        if (!RasterizeBobTrail(Copy, SourceImage->Surface, Error))
            return false;
        return true;
    }

    bool SpriteSystem::ScaleBob(int Id, int ScalePercent, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "SCALE BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Value->ScaleX != static_cast<float>(ScalePercent) || Value->ScaleY != static_cast<float>(ScalePercent))
            if (!PreserveBobTrail(Id, *Value, Error))
                return false;
        Value->ScaleX = static_cast<float>(ScalePercent);
        Value->ScaleY = static_cast<float>(ScalePercent);
        return true;
    }

    bool SpriteSystem::SetBob(int Id, int Backsave, int Transparency, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "SET BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Backsave < 0 || Backsave > 1)
        {
            if (Error)
                *Error = "SET BOB backsave state must be zero or one";
            return false;
        }
        if (Transparency < 0 || Transparency > 1)
        {
            if (Error)
                *Error = "SET BOB transparency state must be zero or one";
            return false;
        }
        Value->Backsave = Backsave;
        Value->Transparent = Transparency != 0;
        Value->Additive = false;
        return true;
    }

    bool SpriteSystem::StretchBob(int Id, int Sx, int Sy, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "STRETCH BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Value->ScaleX != static_cast<float>(Sx) || Value->ScaleY != static_cast<float>(Sy))
            if (!PreserveBobTrail(Id, *Value, Error))
                return false;
        Value->ScaleX = static_cast<float>(Sx);
        Value->ScaleY = static_cast<float>(Sy);
        return true;
    }

    bool SpriteSystem::HideBob(int Id, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "HIDE BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (!PreserveBobTrail(Id, *Value, Error))
            return false;
        Value->Visible = false;
        BobHitContacts_.erase(Id);
        for (auto& [UnusedId, Contacts] : BobHitContacts_)
            Contacts.erase(Id);
        return true;
    }
    bool SpriteSystem::ShowBob(int Id, std::string* Error)
    {
        auto* CheckedSprite = FindBobForCommand(Id, "SHOW BOB", Error);
        if (!CheckedSprite)
            return false;
        CheckedSprite->Visible = true;
        return true;
    }

    bool SpriteSystem::PositionBob(int Id, int X, int Y, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "POSITION BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Value->X != static_cast<float>(X) || Value->Y != static_cast<float>(Y))
            if (!PreserveBobTrail(Id, *Value, Error))
                return false;
        Value->X = static_cast<float>(X);
        Value->Y = static_cast<float>(Y);
        return true;
    }

    bool SpriteSystem::RotateBob(int Id, float Angle, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "ROTATE BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Value->Angle != Angle)
            if (!PreserveBobTrail(Id, *Value, Error))
                return false;
        Value->Angle = Angle;
        return true;
    }

    bool SpriteSystem::MoveBob(int Id, float Distance, std::string* Error)
    {
        SpriteData* ValuePointer = FindBobForCommand(Id, "MOVE BOB", Error);
        if (!ValuePointer)
            return false;
        SpriteData* Value = ValuePointer;
        if (Distance != 0.0f)
            if (!PreserveBobTrail(Id, *Value, Error))
                return false;
        constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;
        Value->X += std::sin(Value->Angle * DegreesToRadians) * Distance;
        Value->Y -= std::cos(Value->Angle * DegreesToRadians) * Distance;
        return true;
    }

    int SpriteSystem::BobExist(int Id) const
    {
        return (Bobs_.count(Id) > 0) ? 1 : 0;
    }
    bool SpriteSystem::BobX(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            Output = 0;
            return true;
        }
        std::string Diagnostic;
        if (!language::ConvertInteger((Value->X), Output, &Diagnostic))
        {
            if (Error)
                *Error = "BobX is outside the BASIC integer range";
            return false;
        }
        return true;
    }
    bool SpriteSystem::BobY(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            Output = 0;
            return true;
        }
        std::string Diagnostic;
        if (!language::ConvertInteger((Value->Y), Output, &Diagnostic))
        {
            if (Error)
                *Error = "BobY is outside the BASIC integer range";
            return false;
        }
        return true;
    }

    bool SpriteSystem::BobWidth(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            Output = 0;
            return true;
        }
        std::string Diagnostic;
        if (!language::ConvertInteger((static_cast<float>(BaseWidth(*Value)) * Value->ScaleX / 100.0f), Output,
                                      &Diagnostic))
        {
            if (Error)
                *Error = "BobWidth is outside the BASIC integer range";
            return false;
        }
        return true;
    }

    bool SpriteSystem::BobHeight(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            Output = 0;
            return true;
        }
        std::string Diagnostic;
        if (!language::ConvertInteger((static_cast<float>(BaseHeight(*Value)) * Value->ScaleY / 100.0f), Output,
                                      &Diagnostic))
        {
            if (Error)
                *Error = "BobHeight is outside the BASIC integer range";
            return false;
        }
        return true;
    }

    int SpriteSystem::BobImage(int Id) const
    {
        const SpriteData* Value = FindBob(Id);
        return Value ? Value->ImageId : 0;
    }
    int SpriteSystem::BobMirrored(int Id) const
    {
        const SpriteData* Value = FindBob(Id);
        return Value && Value->Mirrored ? 1 : 0;
    }
    int SpriteSystem::BobFlipped(int Id) const
    {
        const SpriteData* Value = FindBob(Id);
        return Value && Value->Flipped ? 1 : 0;
    }
    bool SpriteSystem::BobScaleX(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            Output = 100;
            return true;
        }
        std::string Diagnostic;
        if (!language::ConvertInteger((Value->ScaleX), Output, &Diagnostic))
        {
            if (Error)
                *Error = "BobScaleX is outside the BASIC integer range";
            return false;
        }
        return true;
    }
    bool SpriteSystem::BobScaleY(int& Output, int Id, std::string* Error) const
    {
        const SpriteData* Value = FindBob(Id);
        if (!Value)
        {
            Output = 100;
            return true;
        }
        std::string Diagnostic;
        if (!language::ConvertInteger((Value->ScaleY), Output, &Diagnostic))
        {
            if (Error)
                *Error = "BobScaleY is outside the BASIC integer range";
            return false;
        }
        return true;
    }
    int SpriteSystem::BobOffsetX(int Id) const
    {
        const SpriteData* Value = FindBob(Id);
        return Value ? Value->OffsetX : 0;
    }
    int SpriteSystem::BobOffsetY(int Id) const
    {
        const SpriteData* Value = FindBob(Id);
        return Value ? Value->OffsetY : 0;
    }
    float SpriteSystem::BobAngle(int Id) const
    {
        const SpriteData* Value = FindBob(Id);
        return Value ? Value->Angle : 0.0f;
    }

    bool SpriteSystem::BobHit(int& Output, int Id, int TargetId, std::string* Error)
    {
        SpriteData* SourcePointer = FindBobForCommand(Id, "BOB HIT", Error);
        if (!SourcePointer)
            return false;
        SpriteData* Source = SourcePointer;
        auto& Contacts = BobHitContacts_[Id];
        if (!Source->Visible)
        {
            Contacts.clear();
            {
                Output = 0;
                return true;
            }
        }
        int Width{}, Height{};
        if (!BobWidth(Width, Id, Error) || !BobHeight(Height, Id, Error))
            return false;

        if (TargetId == 0)
        {
            std::unordered_set<int> Current;
            for (const auto& [CandidateId, Candidate] : Bobs_)
            {
                int CandidateWidth{}, CandidateHeight{};
                if (!BobWidth(CandidateWidth, CandidateId, Error) || !BobHeight(CandidateHeight, CandidateId, Error))
                    return false;
                if (CandidateId == Id || !Candidate.Visible)
                    continue;
                if (SpriteAabbOverlap(*Source, Width, Height, Candidate, CandidateWidth, CandidateHeight))
                    Current.insert(CandidateId);
            }
            int Result = 0;
            for (const int CandidateId : Current)
            {
                if (!Contacts.contains(CandidateId))
                    Result = Result == 0 ? CandidateId : std::min(Result, CandidateId);
            }
            Contacts = std::move(Current);
            {
                Output = Result;
                return true;
            }
        }

        const SpriteData* Target = FindBob(TargetId);
        int TargetWidth{}, TargetHeight{};
        if (!BobWidth(TargetWidth, TargetId, Error) || !BobHeight(TargetHeight, TargetId, Error))
            return false;
        if (!Target)
        {
            if (Error)
                *Error = "BOB HIT requires existing target bob " + std::to_string(TargetId);
            return false;
        }
        const bool Overlap =
            Target->Visible && SpriteAabbOverlap(*Source, Width, Height, *Target, TargetWidth, TargetHeight);
        const int Result = Overlap && !Contacts.contains(TargetId) ? 1 : 0;
        if (Overlap)
            Contacts.insert(TargetId);
        else
            Contacts.erase(TargetId);
        {
            Output = Result;
            return true;
        }
    }

    bool SpriteSystem::BobCollision(int& Output, int Id, int TargetId, std::string* Error) const
    {
        const SpriteData* Source = FindBob(Id);
        if (!Source || !Source->Visible)
        {
            Output = 0;
            return true;
        }
        int Width{}, Height{};
        if (!BobWidth(Width, Id, Error) || !BobHeight(Height, Id, Error))
            return false;
        if (TargetId == 0)
        {
            for (const auto& [CandidateId, Candidate] : Bobs_)
            {
                int CandidateWidth{}, CandidateHeight{};
                if (!BobWidth(CandidateWidth, CandidateId, Error) || !BobHeight(CandidateHeight, CandidateId, Error))
                    return false;
                if (CandidateId != Id && Candidate.Visible &&
                    SpriteAabbOverlap(*Source, Width, Height, Candidate, CandidateWidth, CandidateHeight))
                {
                    Output = CandidateId;
                    return true;
                }
            }
            {
                Output = 0;
                return true;
            }
        }
        const SpriteData* Target = FindBob(TargetId);
        int TargetWidth{}, TargetHeight{};
        if (!BobWidth(TargetWidth, TargetId, Error) || !BobHeight(TargetHeight, TargetId, Error))
            return false;
        {
            Output = Target && Target->Visible &&
                             SpriteAabbOverlap(*Source, Width, Height, *Target, TargetWidth, TargetHeight)
                         ? 1
                         : 0;
            return true;
        }
    }

    bool SpriteSystem::PreserveBobTrail(int Id, const SpriteData& Bob, std::string* Error)
    {
        (void)Bob;
        if (!PresentedBobs_.contains(Id))
            return true;
        if (!MaterializePresentedBobTrails(Error))
            return false;
        return true;
    }

    bool SpriteSystem::RasterizeBobTrail(const SpriteData& Bob, SDL_Surface* Source, std::string* Error)
    {
        if (!Source)
            return true;
        const int PreviousBitmap = Kernel_.ImageStoreRef().CurrentBitmap();
        SDL_Surface* Destination = Kernel_.ImageStoreRef().EnsureScreenBitmap(Error);
        if (PreviousBitmap > 0)
            if (!Kernel_.ImageStoreRef().SetCurrentBitmap(PreviousBitmap, Error))
                return false;
        if (!Destination)
            return false;

        const double GlobalScaleX = SpriteGlobalScaleX();
        const double PivotX = Bob.X * GlobalScaleX + SpriteGlobalOffsetX();
        const double PivotY = Bob.Y;
        const double Width = static_cast<double>(BaseWidth(Bob)) * Bob.ScaleX / 100.0f * GlobalScaleX;
        const double Height = static_cast<double>(BaseHeight(Bob)) * Bob.ScaleY / 100.0f;
        if (Width <= 0.0f || Height <= 0.0f)
            return true;
        const double Left = -std::trunc(static_cast<double>(Bob.OffsetX) * Bob.ScaleX / 100.0f * GlobalScaleX);
        const double Top = -std::trunc(static_cast<double>(Bob.OffsetY) * Bob.ScaleY / 100.0f);
        const double Radians = Bob.Angle * 3.14159265358979323846f / 180.0f;
        const double Cosine = std::cos(Radians);
        const double Sine = std::sin(Radians);
        const auto WorldPoint = [&](double LocalX, double LocalY)
        {
            return std::array<double, 2>{PivotX + LocalX * Cosine - LocalY * Sine,
                                         PivotY + LocalX * Sine + LocalY * Cosine};
        };
        const std::array<std::array<double, 2>, 4> Corners{WorldPoint(Left, Top), WorldPoint(Left + Width, Top),
                                                           WorldPoint(Left + Width, Top + Height),
                                                           WorldPoint(Left, Top + Height)};
        double MinX = Corners[0][0];
        double MinY = Corners[0][1];
        double MaxX = Corners[0][0];
        double MaxY = Corners[0][1];
        for (const auto& Corner : Corners)
        {
            MinX = std::min(MinX, Corner[0]);
            MinY = std::min(MinY, Corner[1]);
            MaxX = std::max(MaxX, Corner[0]);
            MaxY = std::max(MaxY, Corner[1]);
        }
        if (!std::isfinite(MinX) || !std::isfinite(MinY) || !std::isfinite(MaxX) || !std::isfinite(MaxY))
        {
            if (Error)
                *Error = "BOB raster coordinates must be finite";
            return false;
        }
        const int StartX = static_cast<int>(std::clamp(std::floor(MinX), 0.0, static_cast<double>(Destination->w)));
        const int StartY = static_cast<int>(std::clamp(std::floor(MinY), 0.0, static_cast<double>(Destination->h)));
        const int EndX = static_cast<int>(std::clamp(std::ceil(MaxX), 0.0, static_cast<double>(Destination->w)));
        const int EndY = static_cast<int>(std::clamp(std::ceil(MaxY), 0.0, static_cast<double>(Destination->h)));
        bool WrotePixel = false;
        for (int Y = StartY; Y < EndY; ++Y)
        {
            for (int X = StartX; X < EndX; ++X)
            {
                const double DeltaX = static_cast<double>(X) + 0.5f - PivotX;
                const double DeltaY = static_cast<double>(Y) + 0.5f - PivotY;
                const double LocalX = DeltaX * Cosine + DeltaY * Sine;
                const double LocalY = -DeltaX * Sine + DeltaY * Cosine;
                double U = (LocalX - Left) / Width;
                double V = (LocalY - Top) / Height;
                if (U < 0.0f || U >= 1.0f || V < 0.0f || V >= 1.0f)
                    continue;
                if (Bob.Mirrored)
                    U = 1.0f - U;
                if (Bob.Flipped)
                    V = 1.0f - V;
                const int SourceX = std::clamp(static_cast<int>(U * Source->w), 0, Source->w - 1);
                const int SourceY = std::clamp(static_cast<int>(V * Source->h), 0, Source->h - 1);
                Uint8 SourceRed = 0;
                Uint8 SourceGreen = 0;
                Uint8 SourceBlue = 0;
                Uint8 SourceAlpha = 0;
                if (!SDL_ReadSurfacePixel(Source, SourceX, SourceY, &SourceRed, &SourceGreen, &SourceBlue,
                                          &SourceAlpha))
                {
                    if (Error)
                        *Error = std::string("BOB source read failed: ") + SDL_GetError();
                    return false;
                }
                float Alpha = Bob.Transparent ? static_cast<float>(SourceAlpha) / 255.0f : 1.0f;
                Alpha *= std::clamp(Bob.Alpha / 255.0f, 0.0f, 1.0f);
                if (Alpha <= 0.0f)
                    continue;
                SourceRed =
                    static_cast<Uint8>(std::clamp(static_cast<float>(SourceRed) * Bob.DiffuseR / 255.0f, 0.0f, 255.0f));
                SourceGreen = static_cast<Uint8>(
                    std::clamp(static_cast<float>(SourceGreen) * Bob.DiffuseG / 255.0f, 0.0f, 255.0f));
                SourceBlue = static_cast<Uint8>(
                    std::clamp(static_cast<float>(SourceBlue) * Bob.DiffuseB / 255.0f, 0.0f, 255.0f));
                Uint8 DestinationRed = 0;
                Uint8 DestinationGreen = 0;
                Uint8 DestinationBlue = 0;
                Uint8 DestinationAlpha = 0;
                if (!SDL_ReadSurfacePixel(Destination, X, Y, &DestinationRed, &DestinationGreen, &DestinationBlue,
                                          &DestinationAlpha))
                {
                    if (Error)
                        *Error = std::string("BOB destination read failed: ") + SDL_GetError();
                    return false;
                }
                const float DestinationAlphaUnit = static_cast<float>(DestinationAlpha) / 255.0f;
                const float OutputAlphaUnit = Alpha + DestinationAlphaUnit * (1.0f - Alpha);
                const auto Blend = [&](Uint8 SourceChannel, Uint8 DestinationChannel)
                {
                    if (OutputAlphaUnit <= 0.0f)
                        return Uint8{0};
                    const float Straight =
                        (static_cast<float>(SourceChannel) * Alpha +
                         static_cast<float>(DestinationChannel) * DestinationAlphaUnit * (1.0f - Alpha)) /
                        OutputAlphaUnit;
                    return static_cast<Uint8>(std::clamp(std::lround(Straight), 0L, 255L));
                };
                const Uint8 OutputAlpha =
                    static_cast<Uint8>(std::clamp(std::lround(255.0f * OutputAlphaUnit), 0L, 255L));
                if (!SDL_WriteSurfacePixel(Destination, X, Y, Blend(SourceRed, DestinationRed),
                                           Blend(SourceGreen, DestinationGreen), Blend(SourceBlue, DestinationBlue),
                                           OutputAlpha))
                {
                    if (Error)
                        *Error = std::string("BOB destination write failed: ") + SDL_GetError();
                    return false;
                }
                WrotePixel = true;
            }
        }
        if (WrotePixel)
            Kernel_.ImageStoreRef().InvalidateBitmapGpuTexture(0);
        return true;
    }

    void SpriteSystem::ClearBobTrails()
    {
        PresentedBobs_.clear();
        PresentedBobImages_.clear();
    }

    bool SpriteSystem::MaterializePresentedBobTrails(std::string* Error)
    {
        auto Presented = std::move(PresentedBobs_);
        auto Images = std::move(PresentedBobImages_);
        PresentedBobs_.clear();
        PresentedBobImages_.clear();
        std::vector<std::pair<int, const SpriteData*>> Ordered;
        Ordered.reserve(Presented.size());
        for (const auto& [Id, Snapshot] : Presented)
        {
            const SpriteData* Current = FindBob(Id);
            if (Current && Current->Backsave == 0 && Current->Visible && Snapshot.ImageId > 0)
                Ordered.emplace_back(Id, &Snapshot);
        }
        std::stable_sort(Ordered.begin(), Ordered.end(),
                         [](const auto& Left, const auto& Right)
                         {
                             return Left.second->Priority == Right.second->Priority
                                        ? Left.first < Right.first
                                        : Left.second->Priority < Right.second->Priority;
                         });
        for (const auto& [UnusedId, Snapshot] : Ordered)
        {
            const auto Image = Images.find(Snapshot->ImageId);
            if (Image != Images.end())
                if (!RasterizeBobTrail(*Snapshot, Image->second.get(), Error))
                    return false;
        }
        return true;
    }

    bool SpriteSystem::CommitPresentedBobs(std::string* Error)
    {
        decltype(PresentedBobs_) Presented;
        decltype(PresentedBobImages_) Images;
        for (const auto& [Id, Bob] : Bobs_)
        {
            if (Bob.Visible && Bob.ImageId > 0)
            {
                Presented.emplace(Id, Bob);
                if (!Images.contains(Bob.ImageId))
                {
                    const Image* ImageValue = Kernel_.ImageStoreRef().GetImage(Bob.ImageId);
                    if (ImageValue && ImageValue->Surface)
                    {
                        SDL_Surface* Copy = SDL_DuplicateSurface(ImageValue->Surface);
                        if (!Copy)
                        {
                            if (Error)
                                *Error = std::string("Cannot retain presented BOB pixels: ") + SDL_GetError();
                            return false;
                        }
                        Images.emplace(Bob.ImageId, std::shared_ptr<SDL_Surface>(Copy, SDL_DestroySurface));
                    }
                }
            }
        }
        PresentedBobs_ = std::move(Presented);
        PresentedBobImages_ = std::move(Images);
        return true;
    }

} // namespace dbc
