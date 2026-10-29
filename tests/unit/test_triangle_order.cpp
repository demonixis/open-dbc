#include "engine/systems/triangle_order.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    // Stand-ins for GPU textures: the order only compares the pointers.
    SDL_GPUTexture* FakeTexture(std::uintptr_t Value)
    {
        return reinterpret_cast<SDL_GPUTexture*>(Value * 16);
    }

    dbc::Triangle Opaque(float Depth, std::uintptr_t Texture)
    {
        dbc::Triangle Value;
        Value.Depth = Depth;
        Value.GpuTexture = FakeTexture(Texture);
        return Value;
    }

    dbc::Triangle Blended(float Depth, std::uintptr_t Texture)
    {
        dbc::Triangle Value = Opaque(Depth, Texture);
        Value.Transparent = true;
        Value.DepthWrite = false;
        return Value;
    }

    uint32_t StateHash(const dbc::Triangle& Value)
    {
        return dbc::DrawStateHash(Value, dbc::SurfaceStyleHash(Value.Style));
    }

    // Records each triangle's state hash, as the renderer does when it builds them.
    std::vector<uint32_t> Ordered(std::vector<dbc::Triangle> Triangles)
    {
        dbc::OrderedTriangles List;
        List.Triangles = std::move(Triangles);
        for (dbc::Triangle& Value : List.Triangles)
            Value.StateHash = StateHash(Value);
        dbc::OrderWorldTriangles(List);
        return List.Order;
    }
} // namespace

TEST_CASE("Opaque triangles are grouped by draw state, front to back within each state")
{
    // Two objects interleaved in depth: sorting by depth alone alternates their textures.
    const auto Order = Ordered({Opaque(4.0f, 1), Opaque(1.0f, 2), Opaque(3.0f, 1), Opaque(2.0f, 2)});
    CHECK(Order == std::vector<uint32_t>{1, 3, 2, 0});
}

TEST_CASE("Differing textures, styles and depth states hash to separate draw states")
{
    dbc::Triangle Wire = Opaque(2.0f, 1);
    Wire.Style.Wireframe = true;
    const auto Order = Ordered({Opaque(1.0f, 1), Wire, Opaque(3.0f, 1)});
    CHECK(Order == std::vector<uint32_t>{0, 2, 1});

    const dbc::Triangle A = Opaque(1.0f, 1);
    dbc::Triangle B = Opaque(5.0f, 1);
    B.Color[0] = SDL_FColor{0.5f, 0.5f, 0.5f, 1.0f};
    CHECK(StateHash(A) == StateHash(B));
    B.AuxiliaryGpuTextures[3] = FakeTexture(9);
    CHECK(StateHash(A) != StateHash(B));
    B = A;
    B.Style.EffectId = 4;
    CHECK(StateHash(A) != StateHash(B));
    B = A;
    B.Style.TextureBlendModes[2] = 7;
    CHECK(StateHash(A) != StateHash(B));
    B = A;
    B.DepthWrite = false;
    CHECK(StateHash(A) != StateHash(B));
}

TEST_CASE("Triangles whose result depends on draw order keep their depth position")
{
    // Without an order-dependent triangle, the far texture-1 triangle joins the near one.
    CHECK(Ordered({Opaque(1.0f, 1), Opaque(2.0f, 2), Opaque(3.0f, 1)}) == std::vector<uint32_t>{0, 2, 1});

    dbc::Triangle NoDepthWrite = Opaque(2.5f, 3);
    NoDepthWrite.DepthWrite = false;
    dbc::Triangle NoDepthTest = Opaque(2.5f, 3);
    NoDepthTest.DepthTest = false;
    dbc::Triangle AlphaWrite = Opaque(2.5f, 3);
    AlphaWrite.AlphaDepthWrite = true;
    dbc::Triangle GhostMatrix = Opaque(2.5f, 3);
    GhostMatrix.MatrixGhostMode = 0;
    // An order-dependent triangle splits the opaque run: no triangle crosses it.
    for (const dbc::Triangle& Pinned : {NoDepthWrite, NoDepthTest, AlphaWrite, GhostMatrix})
    {
        CHECK_FALSE(dbc::OrderIndependent(Pinned));
        CHECK(Ordered({Opaque(1.0f, 1), Opaque(2.0f, 2), Pinned, Opaque(3.0f, 1)}) ==
              std::vector<uint32_t>{0, 1, 2, 3});
    }
}

TEST_CASE("Very early, opaque and blended phases keep their order and blended triangles go back to front")
{
    dbc::Triangle Early = Opaque(9.0f, 5);
    Early.VeryEarly = true;
    dbc::Triangle Additive = Opaque(3.0f, 1);
    Additive.Additive = true;
    const auto Order = Ordered({Blended(1.0f, 1), Opaque(2.0f, 1), Early, Blended(5.0f, 1), Additive, Opaque(1.0f, 2)});
    CHECK(Order == std::vector<uint32_t>{2, 5, 1, 3, 4, 0});
}

TEST_CASE("Render priority comes before depth and bounds the opaque runs")
{
    dbc::Triangle Late = Opaque(0.5f, 1);
    Late.RenderPriority = 1;
    const auto Order = Ordered({Opaque(3.0f, 1), Late, Opaque(1.0f, 2), Opaque(2.0f, 1)});
    CHECK(Order == std::vector<uint32_t>{2, 3, 0, 1});
}

TEST_CASE("Triangle order is a permutation even with NaN depths")
{
    std::vector<dbc::Triangle> Triangles;
    for (int Index = 0; Index < 64; ++Index)
    {
        dbc::Triangle Value = Index % 3 == 0 ? Blended(static_cast<float>(Index % 7), 1)
                                             : Opaque(static_cast<float>(Index % 5), Index % 4);
        if (Index % 11 == 0)
            Value.Depth = std::numeric_limits<float>::quiet_NaN();
        Triangles.push_back(Value);
    }
    auto Order = Ordered(Triangles);
    REQUIRE(Order.size() == Triangles.size());
    std::sort(Order.begin(), Order.end());
    for (uint32_t Index = 0; Index < Order.size(); ++Index)
        CHECK(Order[Index] == Index);
}

TEST_CASE("Overlay triangles go back to front and keep their order on equal depth")
{
    dbc::OrderedTriangles List;
    List.Triangles = {Opaque(1.0f, 1), Opaque(3.0f, 2), Opaque(1.0f, 3), Opaque(2.0f, 1)};
    dbc::OrderOverlayTriangles(List);
    CHECK(List.Order == std::vector<uint32_t>{1, 3, 0, 2});
}

TEST_CASE("Packed order keys sort like the phase, priority and depth comparison")
{
    // Order-dependent triangles are never grouped, so their order is the sort alone.
    std::vector<dbc::Triangle> Triangles;
    const float Depths[] = {-3.5f, -0.0f, 0.0f, 0.25f, 1.0f, 1.0f, 7.5f, 1e30f, -1e-30f};
    uint32_t Seed = 12345;
    const auto Next = [&Seed](uint32_t Range)
    {
        Seed = Seed * 1664525u + 1013904223u;
        return (Seed >> 8) % Range;
    };
    for (int Index = 0; Index < 400; ++Index)
    {
        dbc::Triangle Value = Opaque(Depths[Next(9)], 1);
        Value.DepthWrite = false;
        Value.VeryEarly = Next(5) == 0;
        Value.Transparent = Next(3) == 0;
        Value.Additive = Next(7) == 0;
        Value.RenderPriority = static_cast<int>(Next(3)) - 1;
        Triangles.push_back(Value);
    }
    std::vector<uint32_t> Expected(Triangles.size());
    for (uint32_t Index = 0; Index < Expected.size(); ++Index)
        Expected[Index] = Index;
    const auto Phase = [&](uint32_t Index)
    {
        const dbc::Triangle& Value = Triangles[Index];
        if (Value.VeryEarly)
            return 0;
        return (Value.Transparent || Value.Additive) ? 2 : 1;
    };
    std::stable_sort(Expected.begin(), Expected.end(),
                     [&](uint32_t A, uint32_t B)
                     {
                         if (Phase(A) != Phase(B))
                             return Phase(A) < Phase(B);
                         if (Triangles[A].RenderPriority != Triangles[B].RenderPriority)
                             return Triangles[A].RenderPriority < Triangles[B].RenderPriority;
                         return Phase(A) == 2 ? Triangles[A].Depth > Triangles[B].Depth
                                              : Triangles[A].Depth < Triangles[B].Depth;
                     });
    CHECK(Ordered(Triangles) == Expected);
}
