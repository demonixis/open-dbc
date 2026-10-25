#pragma once

// Pure collision math helpers (no engine/GPU dependencies).
// Extracted from the anonymous namespace in object_manager.cpp
// so they can be unit-tested independently.

#include "../engine_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dbc
{

    [[nodiscard]] inline bool AabbIntersect(const AABB& A, const AABB& B)
    {
        return A.MinX <= B.MaxX && A.MaxX >= B.MinX && A.MinY <= B.MaxY && A.MaxY >= B.MinY && A.MinZ <= B.MaxZ &&
               A.MaxZ >= B.MinZ;
    }

    [[nodiscard]] inline bool SphereAabbIntersect(const Vec3& Center, float Radius, const AABB& Box)
    {
        float Dx = 0.0f, Dy = 0.0f, Dz = 0.0f;
        if (Center.X < Box.MinX)
            Dx = Box.MinX - Center.X;
        else if (Center.X > Box.MaxX)
            Dx = Center.X - Box.MaxX;
        if (Center.Y < Box.MinY)
            Dy = Box.MinY - Center.Y;
        else if (Center.Y > Box.MaxY)
            Dy = Center.Y - Box.MaxY;
        if (Center.Z < Box.MinZ)
            Dz = Box.MinZ - Center.Z;
        else if (Center.Z > Box.MaxZ)
            Dz = Center.Z - Box.MaxZ;
        return (Dx * Dx + Dy * Dy + Dz * Dz) <= (Radius * Radius);
    }

    [[nodiscard]] inline int64_t SpatialHashKey(int Cx, int Cz)
    {
        const uint64_t X = static_cast<uint32_t>(Cx);
        const uint64_t Z = static_cast<uint32_t>(Cz);
        return static_cast<int64_t>((X << 32) | Z);
    }

    [[nodiscard]] inline bool SegmentAabbIntersect(const Vec3& P0, const Vec3& P1, const AABB& Box, float& OutT,
                                                   Vec3& OutHit)
    {
        const float Dx = P1.X - P0.X;
        const float Dy = P1.Y - P0.Y;
        const float Dz = P1.Z - P0.Z;
        float Tmin = 0.0f, Tmax = 1.0f;

        auto Slab = [&](float Origin, float Dir, float Bmin, float Bmax) -> bool
        {
            if (std::fabs(Dir) < 1e-8f)
                return (Origin >= Bmin && Origin <= Bmax);
            float T1 = (Bmin - Origin) / Dir;
            float T2 = (Bmax - Origin) / Dir;
            if (T1 > T2)
                std::swap(T1, T2);
            Tmin = std::max(Tmin, T1);
            Tmax = std::min(Tmax, T2);
            return Tmin <= Tmax;
        };

        if (!Slab(P0.X, Dx, Box.MinX, Box.MaxX))
            return false;
        if (!Slab(P0.Y, Dy, Box.MinY, Box.MaxY))
            return false;
        if (!Slab(P0.Z, Dz, Box.MinZ, Box.MaxZ))
            return false;

        OutT = Tmin;
        OutHit = {P0.X + Dx * Tmin, P0.Y + Dy * Tmin, P0.Z + Dz * Tmin};
        return true;
    }

} // namespace dbc
