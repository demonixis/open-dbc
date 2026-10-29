#pragma once

#include "engine_types.hpp"

#include <cstdint>

namespace dbc
{
    enum class ScreenTextLifetime : std::uint8_t
    {
        CurrentFrame,
        PersistentBitmap,
    };

    enum class MeshMemblockLayout : std::uint8_t
    {
        Classic32ByteHeader,
        ProfessionalFvf,
    };

    enum class SoundMemblockLayout : std::uint8_t
    {
        Classic12ByteHeader,
        ProfessionalWaveFormatEx,
    };

    class RuntimeCompatibilityPolicy final
    {
      private:
        struct Settings
        {
            ScreenTextLifetime ScreenText;
            bool CreateBitmapAcceptsNeutralDimensions;
            CollisionShape DefaultObjectCollisionShape;
            bool CloneUsesDefaultCollisionShape;
            int DefaultPlainOrientation;
            bool PlainOrientationUsesObjectRotation;
            bool AddSyntheticObjectRoot;
            bool IncludeSyntheticRootInLimbChecklist;
            bool RecursiveLimbVisibility;
            bool LoadImageTextureModeUsesClassicHints;
            bool SetObjectZeroUsesTransparentBlack;
            bool NeutralOutOfRangeMemblockReads;
            bool ImageMemblockAlpha;
            MeshMemblockLayout MeshMemblocks;
            SoundMemblockLayout SoundMemblocks;
        };

      public:
        RuntimeCompatibilityPolicy(const RuntimeCompatibilityPolicy&) = default;
        RuntimeCompatibilityPolicy(RuntimeCompatibilityPolicy&&) = default;

        [[nodiscard]] static constexpr RuntimeCompatibilityPolicy Classic()
        {
            return RuntimeCompatibilityPolicy(Settings{
                .ScreenText = ScreenTextLifetime::CurrentFrame,
                .CreateBitmapAcceptsNeutralDimensions = true,
                .DefaultObjectCollisionShape = CollisionShape::Polygon,
                .CloneUsesDefaultCollisionShape = true,
                .DefaultPlainOrientation = 0,
                .PlainOrientationUsesObjectRotation = false,
                .AddSyntheticObjectRoot = true,
                .IncludeSyntheticRootInLimbChecklist = false,
                .RecursiveLimbVisibility = false,
                .LoadImageTextureModeUsesClassicHints = true,
                .SetObjectZeroUsesTransparentBlack = true,
                .NeutralOutOfRangeMemblockReads = true,
                .ImageMemblockAlpha = false,
                .MeshMemblocks = MeshMemblockLayout::Classic32ByteHeader,
                .SoundMemblocks = SoundMemblockLayout::Classic12ByteHeader,
            });
        }

        [[nodiscard]] static constexpr RuntimeCompatibilityPolicy Professional()
        {
            return RuntimeCompatibilityPolicy(Settings{
                .ScreenText = ScreenTextLifetime::PersistentBitmap,
                .CreateBitmapAcceptsNeutralDimensions = false,
                .DefaultObjectCollisionShape = CollisionShape::Sphere,
                .CloneUsesDefaultCollisionShape = false,
                .DefaultPlainOrientation = 0,
                .PlainOrientationUsesObjectRotation = true,
                .AddSyntheticObjectRoot = false,
                .IncludeSyntheticRootInLimbChecklist = true,
                .RecursiveLimbVisibility = true,
                .LoadImageTextureModeUsesClassicHints = false,
                .SetObjectZeroUsesTransparentBlack = false,
                .NeutralOutOfRangeMemblockReads = false,
                .ImageMemblockAlpha = true,
                .MeshMemblocks = MeshMemblockLayout::ProfessionalFvf,
                .SoundMemblocks = SoundMemblockLayout::ProfessionalWaveFormatEx,
            });
        }

        [[nodiscard]] constexpr ScreenTextLifetime ScreenText() const
        {
            return ScreenText_;
        }

        [[nodiscard]] constexpr bool CreateBitmapAcceptsNeutralDimensions() const
        {
            return CreateBitmapAcceptsNeutralDimensions_;
        }

        [[nodiscard]] constexpr CollisionShape DefaultObjectCollisionShape() const
        {
            return DefaultObjectCollisionShape_;
        }

        [[nodiscard]] constexpr bool CloneUsesDefaultCollisionShape() const
        {
            return CloneUsesDefaultCollisionShape_;
        }

        [[nodiscard]] constexpr int DefaultPlainOrientation() const
        {
            return DefaultPlainOrientation_;
        }

        [[nodiscard]] constexpr bool PlainOrientationUsesObjectRotation() const
        {
            return PlainOrientationUsesObjectRotation_;
        }

        [[nodiscard]] constexpr bool AddSyntheticObjectRoot() const
        {
            return AddSyntheticObjectRoot_;
        }

        [[nodiscard]] constexpr bool IncludeSyntheticRootInLimbChecklist() const
        {
            return IncludeSyntheticRootInLimbChecklist_;
        }

        [[nodiscard]] constexpr bool RecursiveLimbVisibility() const
        {
            return RecursiveLimbVisibility_;
        }

        [[nodiscard]] constexpr bool LoadImageTextureModeUsesClassicHints() const
        {
            return LoadImageTextureModeUsesClassicHints_;
        }

        [[nodiscard]] constexpr bool SetObjectZeroUsesTransparentBlack() const
        {
            return SetObjectZeroUsesTransparentBlack_;
        }

        [[nodiscard]] constexpr bool NeutralOutOfRangeMemblockReads() const
        {
            return NeutralOutOfRangeMemblockReads_;
        }

        // Classic images are X8R8G8B8: the fourth byte of an image memblock pixel is ignored.
        [[nodiscard]] constexpr bool ImageMemblockAlpha() const
        {
            return ImageMemblockAlpha_;
        }

        [[nodiscard]] constexpr MeshMemblockLayout MeshMemblocks() const
        {
            return MeshMemblocks_;
        }

        [[nodiscard]] constexpr SoundMemblockLayout SoundMemblocks() const
        {
            return SoundMemblocks_;
        }

      private:
        constexpr explicit RuntimeCompatibilityPolicy(Settings Values)
            : ScreenText_(Values.ScreenText),
              CreateBitmapAcceptsNeutralDimensions_(Values.CreateBitmapAcceptsNeutralDimensions),
              DefaultObjectCollisionShape_(Values.DefaultObjectCollisionShape),
              CloneUsesDefaultCollisionShape_(Values.CloneUsesDefaultCollisionShape),
              DefaultPlainOrientation_(Values.DefaultPlainOrientation),
              PlainOrientationUsesObjectRotation_(Values.PlainOrientationUsesObjectRotation),
              AddSyntheticObjectRoot_(Values.AddSyntheticObjectRoot),
              IncludeSyntheticRootInLimbChecklist_(Values.IncludeSyntheticRootInLimbChecklist),
              RecursiveLimbVisibility_(Values.RecursiveLimbVisibility),
              LoadImageTextureModeUsesClassicHints_(Values.LoadImageTextureModeUsesClassicHints),
              SetObjectZeroUsesTransparentBlack_(Values.SetObjectZeroUsesTransparentBlack),
              NeutralOutOfRangeMemblockReads_(Values.NeutralOutOfRangeMemblockReads),
              ImageMemblockAlpha_(Values.ImageMemblockAlpha), MeshMemblocks_(Values.MeshMemblocks),
              SoundMemblocks_(Values.SoundMemblocks)
        {
        }

        const ScreenTextLifetime ScreenText_;
        const bool CreateBitmapAcceptsNeutralDimensions_;
        const CollisionShape DefaultObjectCollisionShape_;
        const bool CloneUsesDefaultCollisionShape_;
        const int DefaultPlainOrientation_;
        const bool PlainOrientationUsesObjectRotation_;
        const bool AddSyntheticObjectRoot_;
        const bool IncludeSyntheticRootInLimbChecklist_;
        const bool RecursiveLimbVisibility_;
        const bool LoadImageTextureModeUsesClassicHints_;
        const bool SetObjectZeroUsesTransparentBlack_;
        const bool NeutralOutOfRangeMemblockReads_;
        const bool ImageMemblockAlpha_;
        const MeshMemblockLayout MeshMemblocks_;
        const SoundMemblockLayout SoundMemblocks_;
    };
} // namespace dbc
