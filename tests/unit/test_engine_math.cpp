#include "engine/engine_math.hpp"
#include <doctest/doctest.h>

using namespace dbc;

TEST_CASE("DegToRad")
{
    CHECK(DegToRad(0.0f) == doctest::Approx(0.0f));
    CHECK(DegToRad(90.0f) == doctest::Approx(static_cast<float>(M_PI / 2.0)));
    CHECK(DegToRad(180.0f) == doctest::Approx(static_cast<float>(M_PI)));
    CHECK(DegToRad(360.0f) == doctest::Approx(static_cast<float>(2.0 * M_PI)));
    CHECK(DegToRad(-90.0f) == doctest::Approx(static_cast<float>(-M_PI / 2.0)));
}

TEST_CASE("Vec3Sub")
{
    Vec3 A{3.0f, 5.0f, 7.0f};
    Vec3 B{1.0f, 2.0f, 3.0f};
    Vec3 R = Vec3Sub(A, B);
    CHECK(R.X == doctest::Approx(2.0f));
    CHECK(R.Y == doctest::Approx(3.0f));
    CHECK(R.Z == doctest::Approx(4.0f));
}

TEST_CASE("Vec3Dot")
{
    CHECK(Vec3Dot({1, 0, 0}, {0, 1, 0}) == doctest::Approx(0.0f));
    CHECK(Vec3Dot({1, 0, 0}, {1, 0, 0}) == doctest::Approx(1.0f));
    CHECK(Vec3Dot({2, 3, 4}, {5, 6, 7}) == doctest::Approx(56.0f));
}

TEST_CASE("Vec3Cross")
{
    Vec3 R = Vec3Cross({1, 0, 0}, {0, 1, 0});
    CHECK(R.X == doctest::Approx(0.0f));
    CHECK(R.Y == doctest::Approx(0.0f));
    CHECK(R.Z == doctest::Approx(1.0f));

    Vec3 R2 = Vec3Cross({0, 1, 0}, {1, 0, 0});
    CHECK(R2.Z == doctest::Approx(-1.0f));
}

TEST_CASE("Vec3Normalize")
{
    SUBCASE("unit vector stays the same")
    {
        Vec3 R = Vec3Normalize({1, 0, 0});
        CHECK(R.X == doctest::Approx(1.0f));
        CHECK(R.Y == doctest::Approx(0.0f));
        CHECK(R.Z == doctest::Approx(0.0f));
    }

    SUBCASE("non-unit vector")
    {
        Vec3 R = Vec3Normalize({3, 4, 0});
        CHECK(R.X == doctest::Approx(0.6f));
        CHECK(R.Y == doctest::Approx(0.8f));
        CHECK(R.Z == doctest::Approx(0.0f));
    }

    SUBCASE("zero vector returns (0,0,1)")
    {
        Vec3 R = Vec3Normalize({0, 0, 0});
        CHECK(R.X == doctest::Approx(0.0f));
        CHECK(R.Y == doctest::Approx(0.0f));
        CHECK(R.Z == doctest::Approx(1.0f));
    }
}

TEST_CASE("RotateX - 90 degrees")
{
    Vec3 P{0, 1, 0};
    Vec3 R = RotateX(P, 90.0f);
    CHECK(R.X == doctest::Approx(0.0f));
    CHECK(R.Y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(R.Z == doctest::Approx(1.0f));
}

TEST_CASE("RotateY - 90 degrees")
{
    Vec3 P{1, 0, 0};
    Vec3 R = RotateY(P, 90.0f);
    CHECK(R.X == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(R.Y == doctest::Approx(0.0f));
    CHECK(R.Z == doctest::Approx(-1.0f));
}

TEST_CASE("RotateZ - 90 degrees")
{
    Vec3 P{1, 0, 0};
    Vec3 R = RotateZ(P, 90.0f);
    CHECK(R.X == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(R.Y == doctest::Approx(1.0f));
    CHECK(R.Z == doctest::Approx(0.0f));
}

TEST_CASE("Mat3 identity multiplication")
{
    Mat3 Identity{};
    Mat3 Result = Mat3Mul(Identity, Identity);
    for (int R = 0; R < 3; ++R)
        for (int C = 0; C < 3; ++C)
            CHECK(Result.M[R][C] == doctest::Approx(Identity.M[R][C]));
}

TEST_CASE("Mat3Apply with identity")
{
    Mat3 Identity{};
    Vec3 V{3, 5, 7};
    Vec3 R = Mat3Apply(V, Identity);
    CHECK(R.X == doctest::Approx(3.0f));
    CHECK(R.Y == doctest::Approx(5.0f));
    CHECK(R.Z == doctest::Approx(7.0f));
}

TEST_CASE("Mat3RotateX 0 degrees is identity")
{
    Mat3 R = Mat3RotateX(0.0f);
    CHECK(R.M[0][0] == doctest::Approx(1.0f));
    CHECK(R.M[1][1] == doctest::Approx(1.0f));
    CHECK(R.M[2][2] == doctest::Approx(1.0f));
}

TEST_CASE("Mat3FromEulerXyz composition")
{
    Mat3 Composed = Mat3FromEulerXyz(90.0f, 0.0f, 0.0f);
    Mat3 Direct = Mat3RotateX(90.0f);
    for (int R = 0; R < 3; ++R)
        for (int C = 0; C < 3; ++C)
            CHECK(Composed.M[R][C] == doctest::Approx(Direct.M[R][C]).epsilon(1e-5));
}

TEST_CASE("UnpackColor")
{
    SDL_FColor C = UnpackColor(0x00FF00); // green = 0xFF in bits 8-15
    CHECK(C.r == doctest::Approx(0.0f));
    CHECK(C.g == doctest::Approx(1.0f));
    CHECK(C.b == doctest::Approx(0.0f));
    CHECK(C.a == doctest::Approx(1.0f));
}

TEST_CASE("ToLowerAscii")
{
    CHECK(ToLowerAscii("HELLO") == "hello");
    CHECK(ToLowerAscii("Hello World") == "hello world");
    CHECK(ToLowerAscii("") == "");
    CHECK(ToLowerAscii("already_lower") == "already_lower");
}
