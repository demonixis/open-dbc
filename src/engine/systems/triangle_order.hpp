#pragma once

#include "../engine_types.hpp"

#include <cstdint>
#include <vector>

namespace dbc
{
    // A triangle list and the order to draw it in; sorting indices never moves the large Triangle records.
    struct OrderedTriangles
    {
        std::vector<Triangle> Triangles;
        std::vector<uint32_t> Order;

        void Clear()
        {
            Triangles.clear();
            Order.clear();
        }
    };

    // Hash of everything in a surface style that splits batches; a mesh computes it once for all its triangles.
    [[nodiscard]] uint32_t SurfaceStyleHash(const SurfaceStyle& Style);

    // Hash of the state that splits batches: textures, blending, depth state and StyleHash. Producers record it in
    // Triangle::StateHash. Grouping by it only reorders order-independent triangles, so a collision can cost batches
    // but never correctness.
    [[nodiscard]] uint32_t DrawStateHash(const Triangle& TriangleValue, uint32_t StyleHash);

    // True when the image does not depend on where the triangle is drawn among the opaque ones: it is depth
    // tested and written and not blended. Coplanar overlaps are the exception and stay arbitrary.
    [[nodiscard]] bool OrderIndependent(const Triangle& TriangleValue);

    // Very early triangles first, then opaque ones, then blended ones, each phase by render priority. Blended
    // triangles go back to front. Opaque triangles go front to back, except that each run of order-independent
    // triangles is grouped by StateHash, in order of first appearance, so it batches into few draw calls; the
    // triangles that depend on order keep their place between the runs.
    void OrderWorldTriangles(OrderedTriangles& List);

    // Screen overlay triangles: back to front.
    void OrderOverlayTriangles(OrderedTriangles& List);
} // namespace dbc
