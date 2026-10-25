#pragma once

#include "../engine_math.hpp"
#include "collision_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace dbc
{

    enum class CollisionPrimitiveKind
    {
        Point,
        Segment,
        Triangle,
    };

    struct CollisionPrimitive
    {
        CollisionPrimitiveKind Kind = CollisionPrimitiveKind::Point;
        Vec3 A{};
        Vec3 B{};
        Vec3 C{};
        Vec3 Normal{0.0f, 1.0f, 0.0f};
        AABB Bounds{};
        float Epsilon = 1e-5f;
    };

    struct CollisionContact
    {
        bool Hit = false;
        Vec3 Correction{};
        Vec3 Normal{};
        float Depth = 0.0f;
    };

    [[nodiscard]] inline Vec3 CollisionAdd(const Vec3& A, const Vec3& B)
    {
        return {A.X + B.X, A.Y + B.Y, A.Z + B.Z};
    }

    [[nodiscard]] inline Vec3 CollisionScale(const Vec3& Value, float Scale)
    {
        return {Value.X * Scale, Value.Y * Scale, Value.Z * Scale};
    }

    [[nodiscard]] inline float CollisionLengthSquared(const Vec3& Value)
    {
        return Vec3Dot(Value, Value);
    }

    [[nodiscard]] inline float CollisionDistanceSquared(const Vec3& A, const Vec3& B)
    {
        return CollisionLengthSquared(Vec3Sub(A, B));
    }

    [[nodiscard]] inline AABB CollisionBounds(const Vec3& A, const Vec3& B, const Vec3& C)
    {
        return {
            std::min({A.X, B.X, C.X}), std::min({A.Y, B.Y, C.Y}), std::min({A.Z, B.Z, C.Z}),
            std::max({A.X, B.X, C.X}), std::max({A.Y, B.Y, C.Y}), std::max({A.Z, B.Z, C.Z}),
        };
    }

    [[nodiscard]] inline bool CollisionBoundsIntersect(const AABB& A, const AABB& B, float Epsilon = 0.0f)
    {
        return A.MinX <= B.MaxX + Epsilon && A.MaxX + Epsilon >= B.MinX && A.MinY <= B.MaxY + Epsilon &&
               A.MaxY + Epsilon >= B.MinY && A.MinZ <= B.MaxZ + Epsilon && A.MaxZ + Epsilon >= B.MinZ;
    }

    [[nodiscard]] inline CollisionPrimitive ClassifyCollisionPrimitive(const Vec3& A, const Vec3& B, const Vec3& C)
    {
        CollisionPrimitive Result{};
        Result.A = A;
        Result.B = B;
        Result.C = C;
        Result.Bounds = CollisionBounds(A, B, C);

        const std::array<std::pair<Vec3, Vec3>, 3> Edges{{{A, B}, {B, C}, {C, A}}};
        float LongestSquared = 0.0f;
        std::pair<Vec3, Vec3> Longest{A, B};
        for (const auto& Edge : Edges)
        {
            const float LengthSquared = CollisionDistanceSquared(Edge.first, Edge.second);
            if (LengthSquared > LongestSquared)
            {
                LongestSquared = LengthSquared;
                Longest = Edge;
            }
        }

        const float Scale = std::max(1.0f, std::sqrt(LongestSquared));
        Result.Epsilon = Scale * 1e-5f;
        if (LongestSquared <= Result.Epsilon * Result.Epsilon)
        {
            Result.Kind = CollisionPrimitiveKind::Point;
            Result.B = A;
            Result.C = A;
            return Result;
        }

        const Vec3 Cross = Vec3Cross(Vec3Sub(B, A), Vec3Sub(C, A));
        if (CollisionLengthSquared(Cross) <= Result.Epsilon * Result.Epsilon * LongestSquared)
        {
            Result.Kind = CollisionPrimitiveKind::Segment;
            Result.A = Longest.first;
            Result.B = Longest.second;
            Result.C = Longest.second;
            return Result;
        }

        Result.Kind = CollisionPrimitiveKind::Triangle;
        Result.Normal = Vec3Normalize(Cross);
        return Result;
    }

    [[nodiscard]] inline Vec3 ClosestPointOnSegment(const Vec3& Point, const Vec3& A, const Vec3& B)
    {
        const Vec3 Segment = Vec3Sub(B, A);
        const float Denominator = CollisionLengthSquared(Segment);
        if (Denominator <= 1e-20f)
            return A;
        const float T = std::clamp(Vec3Dot(Vec3Sub(Point, A), Segment) / Denominator, 0.0f, 1.0f);
        return CollisionAdd(A, CollisionScale(Segment, T));
    }

    // Voronoi-region method from Ericson, Real-Time Collision Detection (2005), section 5.1.5.
    [[nodiscard]] inline Vec3 ClosestPointOnTriangle(const Vec3& Point, const CollisionPrimitive& Primitive)
    {
        if (Primitive.Kind == CollisionPrimitiveKind::Point)
            return Primitive.A;
        if (Primitive.Kind == CollisionPrimitiveKind::Segment)
            return ClosestPointOnSegment(Point, Primitive.A, Primitive.B);

        const Vec3 Ab = Vec3Sub(Primitive.B, Primitive.A);
        const Vec3 Ac = Vec3Sub(Primitive.C, Primitive.A);
        const Vec3 Ap = Vec3Sub(Point, Primitive.A);
        const float D1 = Vec3Dot(Ab, Ap);
        const float D2 = Vec3Dot(Ac, Ap);
        if (D1 <= 0.0f && D2 <= 0.0f)
            return Primitive.A;

        const Vec3 Bp = Vec3Sub(Point, Primitive.B);
        const float D3 = Vec3Dot(Ab, Bp);
        const float D4 = Vec3Dot(Ac, Bp);
        if (D3 >= 0.0f && D4 <= D3)
            return Primitive.B;

        const float Vc = D1 * D4 - D3 * D2;
        if (Vc <= 0.0f && D1 >= 0.0f && D3 <= 0.0f)
        {
            const float V = D1 / (D1 - D3);
            return CollisionAdd(Primitive.A, CollisionScale(Ab, V));
        }

        const Vec3 Cp = Vec3Sub(Point, Primitive.C);
        const float D5 = Vec3Dot(Ab, Cp);
        const float D6 = Vec3Dot(Ac, Cp);
        if (D6 >= 0.0f && D5 <= D6)
            return Primitive.C;

        const float Vb = D5 * D2 - D1 * D6;
        if (Vb <= 0.0f && D2 >= 0.0f && D6 <= 0.0f)
        {
            const float W = D2 / (D2 - D6);
            return CollisionAdd(Primitive.A, CollisionScale(Ac, W));
        }

        const float Va = D3 * D6 - D5 * D4;
        if (Va <= 0.0f && (D4 - D3) >= 0.0f && (D5 - D6) >= 0.0f)
        {
            const Vec3 Bc = Vec3Sub(Primitive.C, Primitive.B);
            const float W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
            return CollisionAdd(Primitive.B, CollisionScale(Bc, W));
        }

        const float Denominator = 1.0f / (Va + Vb + Vc);
        const float V = Vb * Denominator;
        const float W = Vc * Denominator;
        return CollisionAdd(Primitive.A, CollisionAdd(CollisionScale(Ab, V), CollisionScale(Ac, W)));
    }

    [[nodiscard]] inline float SegmentSegmentDistanceSquared(const Vec3& P1, const Vec3& Q1, const Vec3& P2,
                                                             const Vec3& Q2)
    {
        constexpr float Tiny = 1e-20f;
        const Vec3 D1 = Vec3Sub(Q1, P1);
        const Vec3 D2 = Vec3Sub(Q2, P2);
        const Vec3 R = Vec3Sub(P1, P2);
        const float A = CollisionLengthSquared(D1);
        const float E = CollisionLengthSquared(D2);
        const float F = Vec3Dot(D2, R);
        float S = 0.0f;
        float T = 0.0f;
        if (A <= Tiny && E <= Tiny)
            return CollisionDistanceSquared(P1, P2);
        if (A <= Tiny)
        {
            T = std::clamp(F / E, 0.0f, 1.0f);
        }
        else
        {
            const float C = Vec3Dot(D1, R);
            if (E <= Tiny)
            {
                S = std::clamp(-C / A, 0.0f, 1.0f);
            }
            else
            {
                const float B = Vec3Dot(D1, D2);
                const float Denominator = A * E - B * B;
                if (std::fabs(Denominator) > Tiny)
                    S = std::clamp((B * F - C * E) / Denominator, 0.0f, 1.0f);
                T = (B * S + F) / E;
                if (T < 0.0f)
                {
                    T = 0.0f;
                    S = std::clamp(-C / A, 0.0f, 1.0f);
                }
                else if (T > 1.0f)
                {
                    T = 1.0f;
                    S = std::clamp((B - C) / A, 0.0f, 1.0f);
                }
            }
        }
        const Vec3 Closest1 = CollisionAdd(P1, CollisionScale(D1, S));
        const Vec3 Closest2 = CollisionAdd(P2, CollisionScale(D2, T));
        return CollisionDistanceSquared(Closest1, Closest2);
    }

    [[nodiscard]] inline bool SegmentTriangleIntersects(const Vec3& Start, const Vec3& End,
                                                        const CollisionPrimitive& Triangle, float Epsilon)
    {
        if (Triangle.Kind == CollisionPrimitiveKind::Point)
            return CollisionDistanceSquared(Triangle.A, ClosestPointOnSegment(Triangle.A, Start, End)) <=
                   Epsilon * Epsilon;
        if (Triangle.Kind == CollisionPrimitiveKind::Segment)
            return SegmentSegmentDistanceSquared(Start, End, Triangle.A, Triangle.B) <= Epsilon * Epsilon;

        const Vec3 Direction = Vec3Sub(End, Start);
        const Vec3 Edge1 = Vec3Sub(Triangle.B, Triangle.A);
        const Vec3 Edge2 = Vec3Sub(Triangle.C, Triangle.A);
        const Vec3 P = Vec3Cross(Direction, Edge2);
        const float Determinant = Vec3Dot(Edge1, P);
        if (std::fabs(Determinant) > Epsilon)
        {
            const float Inverse = 1.0f / Determinant;
            const Vec3 T = Vec3Sub(Start, Triangle.A);
            const float U = Vec3Dot(T, P) * Inverse;
            const Vec3 Q = Vec3Cross(T, Edge1);
            const float V = Vec3Dot(Direction, Q) * Inverse;
            const float Distance = Vec3Dot(Edge2, Q) * Inverse;
            if (U >= -Epsilon && V >= -Epsilon && U + V <= 1.0f + Epsilon && Distance >= -Epsilon &&
                Distance <= 1.0f + Epsilon)
                return true;
        }

        if (CollisionDistanceSquared(Start, ClosestPointOnTriangle(Start, Triangle)) <= Epsilon * Epsilon ||
            CollisionDistanceSquared(End, ClosestPointOnTriangle(End, Triangle)) <= Epsilon * Epsilon)
            return true;
        return SegmentSegmentDistanceSquared(Start, End, Triangle.A, Triangle.B) <= Epsilon * Epsilon ||
               SegmentSegmentDistanceSquared(Start, End, Triangle.B, Triangle.C) <= Epsilon * Epsilon ||
               SegmentSegmentDistanceSquared(Start, End, Triangle.C, Triangle.A) <= Epsilon * Epsilon;
    }

    [[nodiscard]] inline bool CollisionPrimitivesIntersect(const CollisionPrimitive& A, const CollisionPrimitive& B)
    {
        const float Epsilon = std::max(A.Epsilon, B.Epsilon);
        if (!CollisionBoundsIntersect(A.Bounds, B.Bounds, Epsilon))
            return false;
        if (A.Kind == CollisionPrimitiveKind::Point)
            return CollisionDistanceSquared(A.A, ClosestPointOnTriangle(A.A, B)) <= Epsilon * Epsilon;
        if (B.Kind == CollisionPrimitiveKind::Point)
            return CollisionDistanceSquared(B.A, ClosestPointOnTriangle(B.A, A)) <= Epsilon * Epsilon;
        if (A.Kind == CollisionPrimitiveKind::Segment)
            return SegmentTriangleIntersects(A.A, A.B, B, Epsilon);
        if (B.Kind == CollisionPrimitiveKind::Segment)
            return SegmentTriangleIntersects(B.A, B.B, A, Epsilon);

        return SegmentTriangleIntersects(A.A, A.B, B, Epsilon) || SegmentTriangleIntersects(A.B, A.C, B, Epsilon) ||
               SegmentTriangleIntersects(A.C, A.A, B, Epsilon) || SegmentTriangleIntersects(B.A, B.B, A, Epsilon) ||
               SegmentTriangleIntersects(B.B, B.C, A, Epsilon) || SegmentTriangleIntersects(B.C, B.A, A, Epsilon);
    }

    [[nodiscard]] inline bool PointInsideAabb(const Vec3& Point, const AABB& Box, float Epsilon = 0.0f)
    {
        return Point.X >= Box.MinX - Epsilon && Point.X <= Box.MaxX + Epsilon && Point.Y >= Box.MinY - Epsilon &&
               Point.Y <= Box.MaxY + Epsilon && Point.Z >= Box.MinZ - Epsilon && Point.Z <= Box.MaxZ + Epsilon;
    }

    [[nodiscard]] inline std::size_t CollisionPrimitiveVertexCount(const CollisionPrimitive& Primitive)
    {
        if (Primitive.Kind == CollisionPrimitiveKind::Point)
            return 1;
        if (Primitive.Kind == CollisionPrimitiveKind::Segment)
            return 2;
        return 3;
    }

    [[nodiscard]] inline const Vec3& CollisionPrimitiveVertex(const CollisionPrimitive& Primitive, std::size_t Index)
    {
        if (Index == 0)
            return Primitive.A;
        if (Index == 1)
            return Primitive.B;
        return Primitive.C;
    }

    inline void ProjectCollisionPrimitive(const CollisionPrimitive& Primitive, const Vec3& Axis, float& Minimum,
                                          float& Maximum)
    {
        Minimum = std::numeric_limits<float>::max();
        Maximum = std::numeric_limits<float>::lowest();
        for (std::size_t Index = 0; Index < CollisionPrimitiveVertexCount(Primitive); ++Index)
        {
            const float Projection = Vec3Dot(CollisionPrimitiveVertex(Primitive, Index), Axis);
            Minimum = std::min(Minimum, Projection);
            Maximum = std::max(Maximum, Projection);
        }
    }

    inline void ProjectAabb(const AABB& Box, const Vec3& Axis, float& Minimum, float& Maximum)
    {
        const Vec3 Center{(Box.MinX + Box.MaxX) * 0.5f, (Box.MinY + Box.MaxY) * 0.5f, (Box.MinZ + Box.MaxZ) * 0.5f};
        const Vec3 Extents{(Box.MaxX - Box.MinX) * 0.5f, (Box.MaxY - Box.MinY) * 0.5f, (Box.MaxZ - Box.MinZ) * 0.5f};
        const float Projection = Vec3Dot(Center, Axis);
        const float Radius =
            Extents.X * std::fabs(Axis.X) + Extents.Y * std::fabs(Axis.Y) + Extents.Z * std::fabs(Axis.Z);
        Minimum = Projection - Radius;
        Maximum = Projection + Radius;
    }

    inline bool UpdateCollisionAxisContact(const Vec3& AxisValue, float MinimumA, float MaximumA, float MinimumB,
                                           float MaximumB, float Epsilon, CollisionContact& Contact,
                                           bool& HasContactAxis)
    {
        const float LengthSquared = CollisionLengthSquared(AxisValue);
        if (LengthSquared <= 1e-20f)
            return true;
        const float Length = std::sqrt(LengthSquared);
        const Vec3 Axis = CollisionScale(AxisValue, 1.0f / Length);
        MinimumA /= Length;
        MaximumA /= Length;
        MinimumB /= Length;
        MaximumB /= Length;
        if (MaximumA < MinimumB - Epsilon || MaximumB < MinimumA - Epsilon)
            return false;

        const float NegativeDepth = std::max(0.0f, MaximumA - MinimumB);
        const float PositiveDepth = std::max(0.0f, MaximumB - MinimumA);
        const bool MoveNegative = NegativeDepth <= PositiveDepth;
        const float Depth = MoveNegative ? NegativeDepth : PositiveDepth;
        if (!HasContactAxis || Depth < Contact.Depth)
        {
            HasContactAxis = true;
            Contact.Depth = Depth;
            Contact.Normal = MoveNegative ? CollisionScale(Axis, -1.0f) : Axis;
            Contact.Correction = CollisionScale(Contact.Normal, Depth);
        }
        return true;
    }

    [[nodiscard]] inline bool TriangleAabbIntersects(const CollisionPrimitive& Primitive, const AABB& Box);

    [[nodiscard]] inline CollisionContact AabbPrimitiveCollisionContact(const AABB& Box,
                                                                        const CollisionPrimitive& Primitive)
    {
        CollisionContact Result{};
        if (!TriangleAabbIntersects(Primitive, Box))
            return Result;
        Result.Hit = true;
        Result.Depth = std::numeric_limits<float>::max();
        bool HasContactAxis = false;
        const auto TestAxis = [&](const Vec3& Axis)
        {
            float MinimumA = 0.0f;
            float MaximumA = 0.0f;
            float MinimumB = 0.0f;
            float MaximumB = 0.0f;
            ProjectAabb(Box, Axis, MinimumA, MaximumA);
            ProjectCollisionPrimitive(Primitive, Axis, MinimumB, MaximumB);
            return UpdateCollisionAxisContact(Axis, MinimumA, MaximumA, MinimumB, MaximumB, Primitive.Epsilon, Result,
                                              HasContactAxis);
        };

        constexpr std::array<Vec3, 3> BoxAxes{{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
        for (const Vec3& Axis : BoxAxes)
            if (!TestAxis(Axis))
                return {};

        std::array<Vec3, 3> Edges{};
        std::size_t EdgeCount = 0;
        if (Primitive.Kind == CollisionPrimitiveKind::Segment)
        {
            Edges[EdgeCount++] = Vec3Sub(Primitive.B, Primitive.A);
        }
        else if (Primitive.Kind == CollisionPrimitiveKind::Triangle)
        {
            Edges[EdgeCount++] = Vec3Sub(Primitive.B, Primitive.A);
            Edges[EdgeCount++] = Vec3Sub(Primitive.C, Primitive.B);
            Edges[EdgeCount++] = Vec3Sub(Primitive.A, Primitive.C);
            if (!TestAxis(Primitive.Normal))
                return {};
        }
        for (std::size_t EdgeIndex = 0; EdgeIndex < EdgeCount; ++EdgeIndex)
            for (const Vec3& Axis : BoxAxes)
                if (!TestAxis(Vec3Cross(Edges[EdgeIndex], Axis)))
                    return {};
        if (!HasContactAxis)
        {
            Result.Depth = 0.0f;
            Result.Normal = {1.0f, 0.0f, 0.0f};
            Result.Correction = {};
        }
        return Result;
    }

    [[nodiscard]] inline CollisionContact PrimitiveCollisionContact(const CollisionPrimitive& A,
                                                                    const CollisionPrimitive& B)
    {
        CollisionContact Result{};
        if (!CollisionPrimitivesIntersect(A, B))
            return Result;
        Result.Hit = true;
        Result.Depth = std::numeric_limits<float>::max();
        bool HasContactAxis = false;
        const float Epsilon = std::max(A.Epsilon, B.Epsilon);
        const auto TestAxis = [&](const Vec3& Axis)
        {
            float MinimumA = 0.0f;
            float MaximumA = 0.0f;
            float MinimumB = 0.0f;
            float MaximumB = 0.0f;
            ProjectCollisionPrimitive(A, Axis, MinimumA, MaximumA);
            ProjectCollisionPrimitive(B, Axis, MinimumB, MaximumB);
            return UpdateCollisionAxisContact(Axis, MinimumA, MaximumA, MinimumB, MaximumB, Epsilon, Result,
                                              HasContactAxis);
        };

        if (A.Kind == CollisionPrimitiveKind::Triangle && !TestAxis(A.Normal))
            return {};
        if (B.Kind == CollisionPrimitiveKind::Triangle && !TestAxis(B.Normal))
            return {};

        std::array<Vec3, 3> EdgesA{};
        std::array<Vec3, 3> EdgesB{};
        const auto FillEdges = [](const CollisionPrimitive& Primitive, std::array<Vec3, 3>& Edges)
        {
            if (Primitive.Kind == CollisionPrimitiveKind::Point)
                return std::size_t{0};
            Edges[0] = Vec3Sub(Primitive.B, Primitive.A);
            if (Primitive.Kind == CollisionPrimitiveKind::Segment)
                return std::size_t{1};
            Edges[1] = Vec3Sub(Primitive.C, Primitive.B);
            Edges[2] = Vec3Sub(Primitive.A, Primitive.C);
            return std::size_t{3};
        };
        const std::size_t EdgeCountA = FillEdges(A, EdgesA);
        const std::size_t EdgeCountB = FillEdges(B, EdgesB);
        for (std::size_t IndexA = 0; IndexA < EdgeCountA; ++IndexA)
            for (std::size_t IndexB = 0; IndexB < EdgeCountB; ++IndexB)
                if (!TestAxis(Vec3Cross(EdgesA[IndexA], EdgesB[IndexB])))
                    return {};

        if (A.Kind == CollisionPrimitiveKind::Triangle && B.Kind == CollisionPrimitiveKind::Triangle &&
            std::fabs(Vec3Dot(A.Normal, B.Normal)) >= 1.0f - Epsilon)
        {
            for (std::size_t Index = 0; Index < EdgeCountA; ++Index)
                if (!TestAxis(Vec3Cross(A.Normal, EdgesA[Index])))
                    return {};
            for (std::size_t Index = 0; Index < EdgeCountB; ++Index)
                if (!TestAxis(Vec3Cross(B.Normal, EdgesB[Index])))
                    return {};
        }

        if (!HasContactAxis)
        {
            Vec3 Direction{1.0f, 0.0f, 0.0f};
            if (EdgeCountA > 0)
            {
                Direction = Vec3Cross(EdgesA[0], {1.0f, 0.0f, 0.0f});
                if (CollisionLengthSquared(Direction) <= 1e-20f)
                    Direction = Vec3Cross(EdgesA[0], {0.0f, 1.0f, 0.0f});
                Direction = Vec3Normalize(Direction);
            }
            Result.Depth = 0.0f;
            Result.Normal = Direction;
            Result.Correction = {};
        }
        return Result;
    }

    [[nodiscard]] inline bool TriangleAabbIntersects(const CollisionPrimitive& Primitive, const AABB& Box)
    {
        if (!CollisionBoundsIntersect(Primitive.Bounds, Box, Primitive.Epsilon))
            return false;
        if (Primitive.Kind == CollisionPrimitiveKind::Point)
            return PointInsideAabb(Primitive.A, Box, Primitive.Epsilon);
        if (Primitive.Kind == CollisionPrimitiveKind::Segment)
        {
            float T = 0.0f;
            Vec3 Hit{};
            const AABB Expanded{Box.MinX - Primitive.Epsilon, Box.MinY - Primitive.Epsilon,
                                Box.MinZ - Primitive.Epsilon, Box.MaxX + Primitive.Epsilon,
                                Box.MaxY + Primitive.Epsilon, Box.MaxZ + Primitive.Epsilon};
            return SegmentAabbIntersect(Primitive.A, Primitive.B, Expanded, T, Hit);
        }

        const Vec3 Center{(Box.MinX + Box.MaxX) * 0.5f, (Box.MinY + Box.MaxY) * 0.5f, (Box.MinZ + Box.MaxZ) * 0.5f};
        const Vec3 Extents{(Box.MaxX - Box.MinX) * 0.5f, (Box.MaxY - Box.MinY) * 0.5f, (Box.MaxZ - Box.MinZ) * 0.5f};
        const std::array<Vec3, 3> Vertices{
            {Vec3Sub(Primitive.A, Center), Vec3Sub(Primitive.B, Center), Vec3Sub(Primitive.C, Center)}};
        const std::array<Vec3, 3> Edges{
            {Vec3Sub(Vertices[1], Vertices[0]), Vec3Sub(Vertices[2], Vertices[1]), Vec3Sub(Vertices[0], Vertices[2])}};
        const auto AxisOverlaps = [&](const Vec3& Axis)
        {
            const float LengthSquared = CollisionLengthSquared(Axis);
            if (LengthSquared <= 1e-20f)
                return true;
            const float P0 = Vec3Dot(Vertices[0], Axis);
            const float P1 = Vec3Dot(Vertices[1], Axis);
            const float P2 = Vec3Dot(Vertices[2], Axis);
            const float Radius =
                Extents.X * std::fabs(Axis.X) + Extents.Y * std::fabs(Axis.Y) + Extents.Z * std::fabs(Axis.Z);
            return std::min({P0, P1, P2}) <= Radius + Primitive.Epsilon &&
                   std::max({P0, P1, P2}) >= -Radius - Primitive.Epsilon;
        };

        constexpr std::array<Vec3, 3> BoxAxes{{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
        for (const Vec3& Edge : Edges)
            for (const Vec3& Axis : BoxAxes)
                if (!AxisOverlaps(Vec3Cross(Edge, Axis)))
                    return false;
        for (const Vec3& Axis : BoxAxes)
            if (!AxisOverlaps(Axis))
                return false;
        return AxisOverlaps(Vec3Cross(Edges[0], Edges[1]));
    }

    [[nodiscard]] inline CollisionContact AabbCollisionContact(const AABB& A, const AABB& B)
    {
        CollisionContact Result{};
        if (!AabbIntersect(A, B))
            return Result;
        const std::array<std::pair<float, Vec3>, 6> Candidates{{
            {A.MaxX - B.MinX, {-1.0f, 0.0f, 0.0f}},
            {B.MaxX - A.MinX, {1.0f, 0.0f, 0.0f}},
            {A.MaxY - B.MinY, {0.0f, -1.0f, 0.0f}},
            {B.MaxY - A.MinY, {0.0f, 1.0f, 0.0f}},
            {A.MaxZ - B.MinZ, {0.0f, 0.0f, -1.0f}},
            {B.MaxZ - A.MinZ, {0.0f, 0.0f, 1.0f}},
        }};
        const auto Best = std::min_element(Candidates.begin(), Candidates.end(), [](const auto& Left, const auto& Right)
                                           { return Left.first < Right.first; });
        Result.Hit = true;
        Result.Depth = std::max(0.0f, Best->first);
        Result.Normal = Best->second;
        Result.Correction = CollisionScale(Result.Normal, Result.Depth);
        return Result;
    }

    [[nodiscard]] inline CollisionContact SphereAabbCollisionContact(const Vec3& Center, float Radius, const AABB& Box)
    {
        CollisionContact Result{};
        const Vec3 Closest{std::clamp(Center.X, Box.MinX, Box.MaxX), std::clamp(Center.Y, Box.MinY, Box.MaxY),
                           std::clamp(Center.Z, Box.MinZ, Box.MaxZ)};
        Vec3 Direction = Vec3Sub(Center, Closest);
        const float DistanceSquared = CollisionLengthSquared(Direction);
        if (DistanceSquared > Radius * Radius)
            return Result;
        Result.Hit = true;
        if (DistanceSquared > 1e-20f)
        {
            const float Distance = std::sqrt(DistanceSquared);
            Result.Normal = CollisionScale(Direction, 1.0f / Distance);
            Result.Depth = std::max(0.0f, Radius - Distance);
        }
        else
        {
            const std::array<std::pair<float, Vec3>, 6> Faces{{
                {Center.X - Box.MinX, {-1.0f, 0.0f, 0.0f}},
                {Box.MaxX - Center.X, {1.0f, 0.0f, 0.0f}},
                {Center.Y - Box.MinY, {0.0f, -1.0f, 0.0f}},
                {Box.MaxY - Center.Y, {0.0f, 1.0f, 0.0f}},
                {Center.Z - Box.MinZ, {0.0f, 0.0f, -1.0f}},
                {Box.MaxZ - Center.Z, {0.0f, 0.0f, 1.0f}},
            }};
            const auto Best = std::min_element(Faces.begin(), Faces.end(), [](const auto& Left, const auto& Right)
                                               { return Left.first < Right.first; });
            Result.Normal = Best->second;
            Result.Depth = Radius + std::max(0.0f, Best->first);
        }
        Result.Correction = CollisionScale(Result.Normal, Result.Depth);
        return Result;
    }

    [[nodiscard]] inline CollisionContact SpherePrimitiveCollisionContact(const Vec3& Center, float Radius,
                                                                          const CollisionPrimitive& Primitive)
    {
        CollisionContact Result{};
        const Vec3 Closest = ClosestPointOnTriangle(Center, Primitive);
        Vec3 Direction = Vec3Sub(Center, Closest);
        const float DistanceSquared = CollisionLengthSquared(Direction);
        if (DistanceSquared > Radius * Radius + Primitive.Epsilon * Primitive.Epsilon)
            return Result;
        Result.Hit = true;
        if (DistanceSquared > 1e-20f)
        {
            const float Distance = std::sqrt(DistanceSquared);
            Result.Normal = CollisionScale(Direction, 1.0f / Distance);
            Result.Depth = std::max(0.0f, Radius - Distance);
        }
        else
        {
            Result.Normal =
                Primitive.Kind == CollisionPrimitiveKind::Triangle ? Primitive.Normal : Vec3{1.0f, 0.0f, 0.0f};
            Result.Depth = Radius;
        }
        Result.Correction = CollisionScale(Result.Normal, Result.Depth);
        return Result;
    }

    [[nodiscard]] inline bool RayTriangleParameter(const Vec3& Origin, const Vec3& Direction,
                                                   const CollisionPrimitive& Primitive, float& Parameter)
    {
        if (Primitive.Kind != CollisionPrimitiveKind::Triangle)
            return false;
        const Vec3 Edge1 = Vec3Sub(Primitive.B, Primitive.A);
        const Vec3 Edge2 = Vec3Sub(Primitive.C, Primitive.A);
        const Vec3 P = Vec3Cross(Direction, Edge2);
        const float Determinant = Vec3Dot(Edge1, P);
        if (std::fabs(Determinant) <= Primitive.Epsilon)
            return false;
        const float Inverse = 1.0f / Determinant;
        const Vec3 T = Vec3Sub(Origin, Primitive.A);
        const float U = Vec3Dot(T, P) * Inverse;
        if (U < -Primitive.Epsilon || U > 1.0f + Primitive.Epsilon)
            return false;
        const Vec3 Q = Vec3Cross(T, Edge1);
        const float V = Vec3Dot(Direction, Q) * Inverse;
        if (V < -Primitive.Epsilon || U + V > 1.0f + Primitive.Epsilon)
            return false;
        Parameter = Vec3Dot(Edge2, Q) * Inverse;
        return Parameter > Primitive.Epsilon;
    }

} // namespace dbc
