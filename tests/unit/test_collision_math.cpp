#include "engine/systems/collision_math.hpp"
#include "engine/systems/ellipsoid_collision.hpp"
#include "engine/systems/polygon_collision.hpp"
#include <doctest/doctest.h>

using namespace dbc;

TEST_CASE("aabb_intersect - overlapping")
{
    AABB A{0, 0, 0, 10, 10, 10};
    AABB B{5, 5, 5, 15, 15, 15};
    CHECK(AabbIntersect(A, B));
}

TEST_CASE("aabb_intersect - separated")
{
    AABB A{0, 0, 0, 10, 10, 10};
    AABB B{20, 20, 20, 30, 30, 30};
    CHECK_FALSE(AabbIntersect(A, B));
}

TEST_CASE("aabb_intersect - touching edge")
{
    AABB A{0, 0, 0, 10, 10, 10};
    AABB B{10, 0, 0, 20, 10, 10};
    CHECK(AabbIntersect(A, B));
}

TEST_CASE("aabb_intersect - one inside another")
{
    AABB Outer{0, 0, 0, 100, 100, 100};
    AABB Inner{10, 10, 10, 20, 20, 20};
    CHECK(AabbIntersect(Outer, Inner));
    CHECK(AabbIntersect(Inner, Outer));
}

TEST_CASE("sphere_aabb_intersect - center inside box")
{
    AABB Box{-5, -5, -5, 5, 5, 5};
    CHECK(SphereAabbIntersect({0, 0, 0}, 1.0f, Box));
}

TEST_CASE("sphere_aabb_intersect - sphere touching box")
{
    AABB Box{0, 0, 0, 10, 10, 10};
    CHECK(SphereAabbIntersect({-1, 5, 5}, 1.0f, Box));
}

TEST_CASE("sphere_aabb_intersect - sphere far away")
{
    AABB Box{0, 0, 0, 10, 10, 10};
    CHECK_FALSE(SphereAabbIntersect({100, 100, 100}, 5.0f, Box));
}

TEST_CASE("sphere_aabb_intersect - sphere overlaps corner")
{
    AABB Box{0, 0, 0, 10, 10, 10};
    // Distance to corner (0,0,0) from (-1,-1,-1) is sqrt(3) ≈ 1.732
    CHECK(SphereAabbIntersect({-1, -1, -1}, 2.0f, Box));
    CHECK_FALSE(SphereAabbIntersect({-1, -1, -1}, 1.0f, Box));
}

TEST_CASE("spatial_hash_key - distinct for different cells")
{
    CHECK(SpatialHashKey(0, 0) != SpatialHashKey(1, 0));
    CHECK(SpatialHashKey(0, 0) != SpatialHashKey(0, 1));
    CHECK(SpatialHashKey(1, 2) != SpatialHashKey(2, 1));
}

TEST_CASE("spatial_hash_key - deterministic")
{
    CHECK(SpatialHashKey(5, 10) == SpatialHashKey(5, 10));
}

TEST_CASE("spatial_hash_key - negative coordinates preserve both bit patterns")
{
    const uint64_t Key = static_cast<uint64_t>(SpatialHashKey(-17, -23));
    CHECK(static_cast<uint32_t>(Key >> 32) == static_cast<uint32_t>(-17));
    CHECK(static_cast<uint32_t>(Key) == static_cast<uint32_t>(-23));
    CHECK(SpatialHashKey(-17, -23) != SpatialHashKey(-17, 23));
    CHECK(SpatialHashKey(-17, -23) != SpatialHashKey(17, -23));
}

TEST_CASE("segment_aabb_intersect - hit through center")
{
    AABB Box{-5, -5, -5, 5, 5, 5};
    float T;
    Vec3 Hit;
    CHECK(SegmentAabbIntersect({-10, 0, 0}, {10, 0, 0}, Box, T, Hit));
    CHECK(T == doctest::Approx(0.25f));
    CHECK(Hit.X == doctest::Approx(-5.0f));
}

TEST_CASE("segment_aabb_intersect - miss")
{
    AABB Box{-5, -5, -5, 5, 5, 5};
    float T;
    Vec3 Hit;
    CHECK_FALSE(SegmentAabbIntersect({-10, 20, 0}, {10, 20, 0}, Box, T, Hit));
}

TEST_CASE("segment_aabb_intersect - starts inside")
{
    AABB Box{-5, -5, -5, 5, 5, 5};
    float T;
    Vec3 Hit;
    CHECK(SegmentAabbIntersect({0, 0, 0}, {10, 0, 0}, Box, T, Hit));
    CHECK(T == doctest::Approx(0.0f));
}

TEST_CASE("segment_aabb_intersect - segment too short")
{
    AABB Box{10, 10, 10, 20, 20, 20};
    float T;
    Vec3 Hit;
    // Segment from origin, not long enough to reach the box
    CHECK_FALSE(SegmentAabbIntersect({0, 0, 0}, {5, 5, 5}, Box, T, Hit));
}

TEST_CASE("polygon primitives classify and intersect degenerate geometry")
{
    const CollisionPrimitive Point = ClassifyCollisionPrimitive({1, 2, 3}, {1, 2, 3}, {1, 2, 3});
    const CollisionPrimitive Segment = ClassifyCollisionPrimitive({0, -10, 0}, {0, 10, 0}, {0, 10, 0});
    const CollisionPrimitive Triangle = ClassifyCollisionPrimitive({-5, 0, -5}, {5, 0, -5}, {0, 0, 5});
    CHECK(Point.Kind == CollisionPrimitiveKind::Point);
    CHECK(Segment.Kind == CollisionPrimitiveKind::Segment);
    CHECK(Triangle.Kind == CollisionPrimitiveKind::Triangle);
    CHECK(CollisionPrimitivesIntersect(Segment, Triangle));

    const CollisionPrimitive Coplanar = ClassifyCollisionPrimitive({-2, 0, 0}, {2, 0, 0}, {0, 0, 8});
    CHECK(CollisionPrimitivesIntersect(Triangle, Coplanar));

    const CollisionPrimitive Miss = ClassifyCollisionPrimitive({20, -10, 0}, {20, 10, 0}, {20, 10, 0});
    CHECK_FALSE(CollisionPrimitivesIntersect(Miss, Triangle));
}

TEST_CASE("triangle box SAT includes contact and rejects an AABB-only false positive")
{
    const AABB Box{-1, -1, -1, 1, 1, 1};
    const CollisionPrimitive Crossing = ClassifyCollisionPrimitive({-2, 0, 0}, {2, 0, 0}, {0, 2, 0});
    CHECK(TriangleAabbIntersects(Crossing, Box));

    const CollisionPrimitive Outside =
        ClassifyCollisionPrimitive({0.9f, 0.9f, 2.0f}, {2.0f, 0.9f, 0.9f}, {0.9f, 2.0f, 0.9f});
    CHECK(AabbIntersect(Outside.Bounds, Box));
    CHECK_FALSE(TriangleAabbIntersects(Outside, Box));
}

TEST_CASE("sphere primitive contacts return a separating correction")
{
    const CollisionPrimitive Floor = ClassifyCollisionPrimitive({-10, 0, -10}, {10, 0, -10}, {0, 0, 10});
    const CollisionContact Hit = SpherePrimitiveCollisionContact({0, 0.25f, 0}, 1.0f, Floor);
    REQUIRE(Hit.Hit);
    CHECK(Hit.Depth == doctest::Approx(0.75f));
    CHECK(Hit.Normal.Y == doctest::Approx(1.0f));
    CHECK(Hit.Correction.Y == doctest::Approx(0.75f));
    CHECK_FALSE(SpherePrimitiveCollisionContact({0, 2.0f, 0}, 1.0f, Floor).Hit);
}

TEST_CASE("AABB contacts return minimum translation vectors")
{
    const CollisionContact Contact = AabbCollisionContact({0, 0, 0, 2, 2, 2}, {1.5f, -5, -5, 5, 5, 5});
    REQUIRE(Contact.Hit);
    CHECK(Contact.Correction.X == doctest::Approx(-0.5f));
    CHECK(Contact.Correction.Y == doctest::Approx(0.0f));
    CHECK(Contact.Correction.Z == doctest::Approx(0.0f));
}

TEST_CASE("box triangle contact follows the intersected plane instead of its AABB")
{
    const AABB Box{-1, -1, -1, 1, 1, 1};
    const CollisionPrimitive Plane = ClassifyCollisionPrimitive({-10, 10, -10}, {10, -10, -10}, {0, 0, 10});
    const CollisionContact Geometric = AabbPrimitiveCollisionContact(Box, Plane);
    const CollisionContact BoundsOnly = AabbCollisionContact(Box, Plane.Bounds);
    REQUIRE(Geometric.Hit);
    REQUIRE(BoundsOnly.Hit);
    CHECK(std::abs(Geometric.Correction.X) > 0.5f);
    CHECK(std::abs(Geometric.Correction.Y) > 0.5f);
    CHECK(std::abs(Geometric.Correction.Z) < std::abs(Geometric.Correction.X) * 0.2f);
    CHECK(Geometric.Depth < BoundsOnly.Depth);
    CHECK(BoundsOnly.Correction.Y == doctest::Approx(0.0f));
}

TEST_CASE("polygon contact MTV comes from the intersecting triangle pair")
{
    const CollisionPrimitive A = ClassifyCollisionPrimitive({-2, -2, 0}, {2, -2, 0}, {0, 2, 0});
    const CollisionPrimitive B = ClassifyCollisionPrimitive({-2, 2, -10}, {2, -2, -10}, {0, 0, 10});
    const CollisionContact Geometric = PrimitiveCollisionContact(A, B);
    const CollisionContact Repeated = PrimitiveCollisionContact(A, B);
    const CollisionContact Reversed = PrimitiveCollisionContact(B, A);
    const CollisionContact BoundsOnly = AabbCollisionContact(A.Bounds, B.Bounds);
    REQUIRE(Geometric.Hit);
    REQUIRE(Reversed.Hit);
    REQUIRE(BoundsOnly.Hit);
    CHECK(std::abs(Geometric.Correction.X) > 0.5f);
    CHECK(std::abs(Geometric.Correction.Y) > 0.5f);
    CHECK(std::abs(Geometric.Correction.Z) < std::abs(Geometric.Correction.X) * 0.2f);
    CHECK(Geometric.Depth < BoundsOnly.Depth);
    CHECK(BoundsOnly.Correction.Y == doctest::Approx(0.0f));
    CHECK(Repeated.Correction.X == doctest::Approx(Geometric.Correction.X));
    CHECK(Repeated.Correction.Y == doctest::Approx(Geometric.Correction.Y));
    CHECK(Repeated.Correction.Z == doctest::Approx(Geometric.Correction.Z));
    CHECK(Reversed.Depth == doctest::Approx(Geometric.Depth));
    CHECK(Vec3Dot(Reversed.Correction, Geometric.Correction) < 0.0f);
}

namespace
{
    // A 20 x 20 floor at y = 0 whose collision side faces up.
    std::vector<SlideTriangle> SlideFloor()
    {
        return {MakeSlideTriangle({-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}),
                MakeSlideTriangle({-10, 0, -10}, {10, 0, 10}, {10, 0, -10})};
    }
} // namespace

TEST_CASE("ellipsoid slide - a falling ellipsoid rests one skin above the floor")
{
    const std::vector<SlideTriangle> Floor = SlideFloor();
    const EllipsoidSlideResult Result = SlideEllipsoid(Floor, {0, 5, 0}, {0, -5, 0}, {0.75f, 1.5f, 0.75f});
    CHECK(Result.Clipped);
    CHECK(Result.Position.X == doctest::Approx(0.0f));
    CHECK(Result.Position.Y == doctest::Approx(1.5015f).epsilon(0.0005));
    CHECK(Result.Position.Z == doctest::Approx(0.0f));
}

TEST_CASE("ellipsoid slide - the move continues along the contact plane")
{
    const std::vector<SlideTriangle> Floor = SlideFloor();
    const EllipsoidSlideResult Result = SlideEllipsoid(Floor, {0, 1.6f, 0}, {3, 1.0f, 0}, {0.75f, 1.5f, 0.75f});
    CHECK(Result.Clipped);
    CHECK(Result.Position.X == doctest::Approx(3.0f).epsilon(0.01));
    CHECK(Result.Position.Y == doctest::Approx(1.5f).epsilon(0.01));
}

TEST_CASE("ellipsoid slide - triangles only block their front side and tiny moves are not tested")
{
    const std::vector<SlideTriangle> Floor = SlideFloor();
    const EllipsoidSlideResult FromBelow = SlideEllipsoid(Floor, {0, -5, 0}, {0, 5, 0}, {1, 1, 1});
    CHECK_FALSE(FromBelow.Clipped);
    CHECK(FromBelow.Position.Y == doctest::Approx(5.0f));

    // 0.002 units over a radius of 1 is below the 0.003 minimum move: the requested position stands.
    const EllipsoidSlideResult Tiny = SlideEllipsoid(Floor, {0, 1.001f, 0}, {0, 0.999f, 0}, {1, 1, 1});
    CHECK_FALSE(Tiny.Clipped);
    CHECK(Tiny.Position.Y == doctest::Approx(0.999f));
    EllipsoidSlideSettings Fine;
    Fine.MinimumMove = 0.001f;
    CHECK(SlideEllipsoid(Floor, {0, 1.001f, 0}, {0, 0.999f, 0}, {1, 1, 1}, Fine).Clipped);
}

TEST_CASE("ellipsoid slide - edges catch a sphere moving parallel to their plane and lift it over")
{
    // The sphere centre passes 0.5 above the plane of a floor whose edge lies along x = 0, y = 0.
    const std::vector<SlideTriangle> Floor{MakeSlideTriangle({0, 0, -5}, {0, 0, 5}, {5, 0, 5})};
    const EllipsoidSlideResult Result = SlideEllipsoid(Floor, {-3, 0.5f, 0}, {-0.5f, 0.5f, 0}, {1, 1, 1});
    CHECK(Result.Clipped);
    const float EdgeDistance = std::sqrt(Result.Position.X * Result.Position.X + Result.Position.Y * Result.Position.Y);
    // The slide follows the tangent plane at the contact, so it never comes closer than the radius.
    CHECK(EdgeDistance >= 1.0f);
    CHECK(EdgeDistance < 1.05f);
    CHECK(Result.Position.X < 0.0f);
    CHECK(Result.Position.Y > 0.5f);
}

TEST_CASE("ellipsoid slide - a wall keeps the ellipsoid at its radius while it slides sideways")
{
    // Wall at x = 5 facing -X.
    const std::vector<SlideTriangle> Wall{MakeSlideTriangle({5, -10, -10}, {5, 10, 10}, {5, 10, -10}),
                                          MakeSlideTriangle({5, -10, -10}, {5, -10, 10}, {5, 10, 10})};
    const EllipsoidSlideResult Result = SlideEllipsoid(Wall, {0, 0, 0}, {8, 0, 4}, {0.75f, 1.5f, 0.75f});
    CHECK(Result.Clipped);
    CHECK(Result.Position.X == doctest::Approx(4.25f).epsilon(0.002));
    CHECK(Result.Position.Z == doctest::Approx(4.0f).epsilon(0.01));
}
