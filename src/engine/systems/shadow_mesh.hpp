#pragma once

#include "../engine_types.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace dbc
{
    // A mesh's faces and how they share edges, on points welded from its vertices: those at the same place and, in
    // a skinned mesh, with the same bone influences, which therefore stay together in every pose.
    struct ShadowTopology
    {
        // Two points, A < B, and the faces that use the edge between them.
        struct Edge
        {
            uint32_t A = 0;
            uint32_t B = 0;
            uint32_t FirstUse = 0;
            uint32_t UseCount = 0;
        };

        // A face going around an edge: +1 from A to B, -1 from B to A.
        struct EdgeUse
        {
            uint32_t Face = 0;
            int32_t Direction = 0;
        };

        // The mesh vertex of each point, and three points per face. Faces with two corners on one point have no
        // area in any pose and are left out.
        std::vector<uint32_t> Points;
        std::vector<uint32_t> Faces;
        std::vector<Edge> Edges;
        std::vector<EdgeUse> Uses;
    };

    [[nodiscard]] ShadowTopology BuildShadowTopology(const MeshGeometry& Geometry);

    // Appends the stencil volume a mesh casts from Light out to Range, given its vertices in the space of Light: its
    // lit faces as the near cap, in NearCapIndices since they lie on the mesh's own surface, and in VolumeIndices
    // their copy pushed away as the far cap, and the sides. Sides between two lit faces would cancel out in the
    // stencil, so an edge has only as many as the faces around it leave unmatched. That gives the same stencil on
    // any mesh, open or not, however many faces share an edge and whichever way they turn. Each point takes two
    // vertices; Lit is scratch space.
    void ExtrudeShadowMesh(const ShadowTopology& Topology, std::span<const Vec3> Vertices, const Vec3& Light,
                           float Range, std::vector<uint8_t>& Lit, std::vector<Vec3>& VolumeVertices,
                           std::vector<uint32_t>& NearCapIndices, std::vector<uint32_t>& VolumeIndices);
} // namespace dbc
