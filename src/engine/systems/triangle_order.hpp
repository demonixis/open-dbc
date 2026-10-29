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

    // The view-space triangles of one view, shared by the views that draw them and the renderer's cache.
    struct SceneTriangles
    {
        OrderedTriangles World;
        OrderedTriangles Reflected;
        OrderedTriangles Overlay;
    };

    // True when both triangles can be drawn by one batch.
    [[nodiscard]] bool SameDrawState(const Triangle& A, const Triangle& B);

    // True when the image does not depend on where the triangle is drawn among the opaque ones: it is depth
    // tested and written and not blended. Coplanar overlaps are the exception and stay arbitrary.
    [[nodiscard]] bool OrderIndependent(const Triangle& TriangleValue);

    // Very early triangles first, then opaque ones, then blended ones, each phase by render priority. Blended
    // triangles go back to front. Opaque triangles go front to back, except that each run of order-independent
    // triangles is grouped by draw state, in order of first appearance, so it batches into few draw calls; the
    // triangles that depend on order keep their place between the runs.
    void OrderWorldTriangles(OrderedTriangles& List);

    // Screen overlay triangles: back to front.
    void OrderOverlayTriangles(OrderedTriangles& List);
} // namespace dbc
