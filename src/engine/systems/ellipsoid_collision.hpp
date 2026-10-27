#pragma once

#include "../engine_math.hpp"
#include "collision_math.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace dbc
{
    // One-sided triangle for ellipsoid sliding: only movement against the side Cross(B - A, C - A)
    // points to can collide, as with DBPro's BSP collision.
    struct SlideTriangle
    {
        Vec3 A{};
        Vec3 B{};
        Vec3 C{};
        AABB Bounds{};
    };

    struct EllipsoidSlideResult
    {
        Vec3 Position{};
        bool Clipped = false;
    };

    // DBPro's BSP slide contract (Shared/CustomBSP/Collision.cpp): positions are scaled by the radii
    // so the ellipsoid becomes a unit sphere, each contact stops the sphere a 0.001 skin short and
    // slides the rest of the move along the contact plane, for at most MaxSteps contacts. A remaining
    // move shorter than MinimumMove (in scaled units) is dropped.
    struct EllipsoidSlideSettings
    {
        int MaxSteps = 5;
        float MinimumMove = 0.003f;
        float Skin = 0.001f;
    };

    [[nodiscard]] inline SlideTriangle MakeSlideTriangle(const Vec3& A, const Vec3& B, const Vec3& C)
    {
        SlideTriangle Result{A, B, C, {}};
        Result.Bounds = AABB{std::min({A.X, B.X, C.X}), std::min({A.Y, B.Y, C.Y}), std::min({A.Z, B.Z, C.Z}),
                             std::max({A.X, B.X, C.X}), std::max({A.Y, B.Y, C.Y}), std::max({A.Z, B.Z, C.Z})};
        return Result;
    }

    namespace detail
    {
        [[nodiscard]] inline Vec3 SlideAdd(const Vec3& A, const Vec3& B)
        {
            return {A.X + B.X, A.Y + B.Y, A.Z + B.Z};
        }

        [[nodiscard]] inline Vec3 SlideScale(const Vec3& V, float S)
        {
            return {V.X * S, V.Y * S, V.Z * S};
        }

        [[nodiscard]] inline Vec3 SlideDivide(const Vec3& V, const Vec3& Radii)
        {
            return {V.X / Radii.X, V.Y / Radii.Y, V.Z / Radii.Z};
        }

        [[nodiscard]] inline float SlideLength(const Vec3& V)
        {
            return std::sqrt(Vec3Dot(V, V));
        }

        // Smallest root of A t^2 + B t + C = 0 in [0, Limit].
        [[nodiscard]] inline bool LowestRoot(float A, float B, float C, float Limit, float& Root)
        {
            const float Determinant = B * B - 4.0f * A * C;
            if (Determinant < 0.0f || std::fabs(A) < 1e-12f)
                return false;
            const float SquareRoot = std::sqrt(Determinant);
            float R1 = (-B - SquareRoot) / (2.0f * A);
            float R2 = (-B + SquareRoot) / (2.0f * A);
            if (R1 > R2)
                std::swap(R1, R2);
            if (R1 > 0.0f && R1 < Limit)
            {
                Root = R1;
                return true;
            }
            if (R2 > 0.0f && R2 < Limit)
            {
                Root = R2;
                return true;
            }
            return false;
        }

        [[nodiscard]] inline bool PointInTriangle(const Vec3& P, const Vec3& A, const Vec3& B, const Vec3& C)
        {
            const Vec3 V0 = Vec3Sub(C, A);
            const Vec3 V1 = Vec3Sub(B, A);
            const Vec3 V2 = Vec3Sub(P, A);
            const float Dot00 = Vec3Dot(V0, V0);
            const float Dot01 = Vec3Dot(V0, V1);
            const float Dot02 = Vec3Dot(V0, V2);
            const float Dot11 = Vec3Dot(V1, V1);
            const float Dot12 = Vec3Dot(V1, V2);
            const float Denominator = Dot00 * Dot11 - Dot01 * Dot01;
            if (std::fabs(Denominator) < 1e-12f)
                return false;
            const float U = (Dot11 * Dot02 - Dot01 * Dot12) / Denominator;
            const float V = (Dot00 * Dot12 - Dot01 * Dot02) / Denominator;
            return U >= 0.0f && V >= 0.0f && U + V <= 1.0f;
        }

        // Swept unit sphere from Base along Velocity against a scaled triangle: keeps the earliest
        // contact time in Time (a fraction of Velocity) and its point.
        inline bool SweepUnitSphere(const Vec3& Base, const Vec3& Velocity, const Vec3& A, const Vec3& B,
                                    const Vec3& C, float& Time, Vec3& Contact)
        {
            const Vec3 Cross = Vec3Cross(Vec3Sub(B, A), Vec3Sub(C, A));
            const float CrossLength = SlideLength(Cross);
            if (CrossLength < 1e-12f)
                return false;
            const Vec3 Normal = SlideScale(Cross, 1.0f / CrossLength);
            const float NormalDotVelocity = Vec3Dot(Normal, Velocity);
            if (NormalDotVelocity > 0.0f)
                return false;
            const float Distance = Vec3Dot(Normal, Vec3Sub(Base, A));
            float T0 = 0.0f;
            bool Embedded = false;
            if (NormalDotVelocity == 0.0f)
            {
                if (std::fabs(Distance) >= 1.0f)
                    return false;
                Embedded = true;
            }
            else
            {
                T0 = (1.0f - Distance) / NormalDotVelocity;
                float T1 = (-1.0f - Distance) / NormalDotVelocity;
                if (T0 > T1)
                    std::swap(T0, T1);
                if (T0 > 1.0f || T1 < 0.0f)
                    return false;
                T0 = std::clamp(T0, 0.0f, 1.0f);
            }

            bool Found = false;
            if (!Embedded)
            {
                const Vec3 PlanePoint = SlideAdd(Vec3Sub(Base, Normal), SlideScale(Velocity, T0));
                if (PointInTriangle(PlanePoint, A, B, C))
                {
                    if (T0 < Time)
                    {
                        Time = T0;
                        Contact = PlanePoint;
                    }
                    return true;
                }
            }

            const float VelocitySquared = Vec3Dot(Velocity, Velocity);
            for (const Vec3* Corner : {&A, &B, &C})
            {
                const Vec3 BaseToCorner = Vec3Sub(Base, *Corner);
                float Root = 0.0f;
                if (LowestRoot(VelocitySquared, 2.0f * Vec3Dot(Velocity, BaseToCorner),
                               Vec3Dot(BaseToCorner, BaseToCorner) - 1.0f, Time, Root))
                {
                    Time = Root;
                    Contact = *Corner;
                    Found = true;
                }
            }
            const Vec3* Edges[3][2] = {{&A, &B}, {&B, &C}, {&C, &A}};
            for (const auto& Edge : Edges)
            {
                const Vec3 Along = Vec3Sub(*Edge[1], *Edge[0]);
                const Vec3 BaseToStart = Vec3Sub(*Edge[0], Base);
                const float AlongSquared = Vec3Dot(Along, Along);
                const float AlongDotVelocity = Vec3Dot(Along, Velocity);
                const float AlongDotBase = Vec3Dot(Along, BaseToStart);
                float Root = 0.0f;
                if (!LowestRoot(AlongSquared * -VelocitySquared + AlongDotVelocity * AlongDotVelocity,
                                AlongSquared * 2.0f * Vec3Dot(Velocity, BaseToStart) -
                                    2.0f * AlongDotVelocity * AlongDotBase,
                                AlongSquared * (1.0f - Vec3Dot(BaseToStart, BaseToStart)) +
                                    AlongDotBase * AlongDotBase,
                                Time, Root))
                    continue;
                const float Fraction = (AlongDotVelocity * Root - AlongDotBase) / AlongSquared;
                if (Fraction >= 0.0f && Fraction <= 1.0f)
                {
                    Time = Root;
                    Contact = SlideAdd(*Edge[0], SlideScale(Along, Fraction));
                    Found = true;
                }
            }
            return Found;
        }
    } // namespace detail

    [[nodiscard]] inline EllipsoidSlideResult SlideEllipsoid(std::span<const SlideTriangle> Triangles,
                                                             const Vec3& From, const Vec3& To, Vec3 Radii,
                                                             const EllipsoidSlideSettings& Settings = {})
    {
        using namespace detail;
        // A zero radius would collapse the scaled space; DBPro's point-sized entities stay tiny spheres.
        Radii = {std::max(Radii.X, 0.001f), std::max(Radii.Y, 0.001f), std::max(Radii.Z, 0.001f)};
        EllipsoidSlideResult Result{To, false};
        Vec3 Position = SlideDivide(From, Radii);
        Vec3 Velocity = Vec3Sub(SlideDivide(To, Radii), Position);
        // A move below the minimum is not tested at all, so the entity keeps its requested position.
        if (SlideLength(Velocity) < Settings.MinimumMove)
            return Result;

        for (int Step = 0; Step < Settings.MaxSteps; ++Step)
        {
            const float Length = SlideLength(Velocity);
            if (Length < Settings.MinimumMove)
                break;
            const Vec3 Destination = SlideAdd(Position, Velocity);
            AABB Swept{std::min(Position.X, Destination.X) - 1.0f, std::min(Position.Y, Destination.Y) - 1.0f,
                       std::min(Position.Z, Destination.Z) - 1.0f, std::max(Position.X, Destination.X) + 1.0f,
                       std::max(Position.Y, Destination.Y) + 1.0f, std::max(Position.Z, Destination.Z) + 1.0f};
            float Time = 1.0f;
            Vec3 Contact{};
            bool Found = false;
            for (const SlideTriangle& Triangle : Triangles)
            {
                const AABB Bounds{Triangle.Bounds.MinX / Radii.X, Triangle.Bounds.MinY / Radii.Y,
                                  Triangle.Bounds.MinZ / Radii.Z, Triangle.Bounds.MaxX / Radii.X,
                                  Triangle.Bounds.MaxY / Radii.Y, Triangle.Bounds.MaxZ / Radii.Z};
                if (!AabbIntersect(Swept, Bounds))
                    continue;
                Found |= SweepUnitSphere(Position, Velocity, SlideDivide(Triangle.A, Radii),
                                         SlideDivide(Triangle.B, Radii), SlideDivide(Triangle.C, Radii), Time,
                                         Contact);
            }
            if (!Found)
            {
                Position = Destination;
                break;
            }

            Result.Clipped = true;
            const Vec3 Direction = SlideScale(Velocity, 1.0f / Length);
            const float Travel = Time * Length;
            if (Travel >= Settings.Skin)
            {
                Position = SlideAdd(Position, SlideScale(Direction, Travel - Settings.Skin));
                Contact = Vec3Sub(Contact, SlideScale(Direction, Settings.Skin));
            }
            Vec3 PlaneNormal = Vec3Sub(Position, Contact);
            const float NormalLength = SlideLength(PlaneNormal);
            if (NormalLength < 1e-6f)
                break;
            PlaneNormal = SlideScale(PlaneNormal, 1.0f / NormalLength);
            const float Above = Vec3Dot(Vec3Sub(Destination, Contact), PlaneNormal);
            const Vec3 SlideDestination = Vec3Sub(Destination, SlideScale(PlaneNormal, Above));
            Velocity = Vec3Sub(SlideDestination, Contact);
        }

        if (Result.Clipped)
            Result.Position = {Position.X * Radii.X, Position.Y * Radii.Y, Position.Z * Radii.Z};
        return Result;
    }
} // namespace dbc
