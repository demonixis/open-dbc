#include "shadow_mesh.hpp"

#include "../engine_math.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

namespace dbc
{
    namespace
    {
        // Position, then bone indices and weights.
        using WeldKey = std::array<uint32_t, 11>;

        struct WeldVertex
        {
            WeldKey Key{};
            uint32_t Vertex = 0;
        };

        struct DirectedEdge
        {
            uint64_t Key = 0;
            uint32_t Face = 0;
            int32_t Direction = 0;
        };

        [[nodiscard]] WeldKey MakeWeldKey(const MeshGeometry& Geometry, std::size_t Vertex)
        {
            const Vec3& Position = Geometry.Vertices[Vertex];
            // Bit-exact: two vertices that differ, even by the sign of a zero, simply stay apart, and the sides
            // between them then cancel out in the stencil as they would anyway.
            WeldKey Key{std::bit_cast<uint32_t>(Position.X), std::bit_cast<uint32_t>(Position.Y),
                        std::bit_cast<uint32_t>(Position.Z)};
            if (Vertex < Geometry.BoneInfluences.size())
            {
                const BoneInfluence& Influence = Geometry.BoneInfluences[Vertex];
                for (std::size_t Slot = 0; Slot < Influence.Indices.size(); ++Slot)
                {
                    Key[3 + Slot] = std::bit_cast<uint32_t>(Influence.Indices[Slot]);
                    Key[7 + Slot] = std::bit_cast<uint32_t>(Influence.Weights[Slot]);
                }
            }
            return Key;
        }
    } // namespace

    ShadowTopology BuildShadowTopology(const MeshGeometry& Geometry)
    {
        ShadowTopology Topology;
        const std::size_t VertexCount = Geometry.Vertices.size();
        std::vector<WeldVertex> Welds(VertexCount);
        for (std::size_t Vertex = 0; Vertex < VertexCount; ++Vertex)
            Welds[Vertex] = WeldVertex{MakeWeldKey(Geometry, Vertex), static_cast<uint32_t>(Vertex)};
        std::sort(Welds.begin(), Welds.end(), [](const WeldVertex& A, const WeldVertex& B) { return A.Key < B.Key; });
        std::vector<uint32_t> PointOfVertex(VertexCount);
        for (std::size_t Index = 0; Index < VertexCount; ++Index)
        {
            if (Index == 0 || Welds[Index].Key != Welds[Index - 1].Key)
                Topology.Points.push_back(Welds[Index].Vertex);
            PointOfVertex[Welds[Index].Vertex] = static_cast<uint32_t>(Topology.Points.size() - 1);
        }

        const std::vector<uint32_t>& Indices = Geometry.Indices;
        const std::size_t CornerCount = Indices.empty() ? VertexCount : Indices.size();
        Topology.Faces.reserve(CornerCount);
        for (std::size_t Corner = 0; Corner + 2 < CornerCount; Corner += 3)
        {
            std::array<uint32_t, 3> Face{};
            bool Valid = true;
            for (std::size_t Offset = 0; Offset < 3 && Valid; ++Offset)
            {
                const std::size_t Vertex = Indices.empty() ? Corner + Offset : Indices[Corner + Offset];
                Valid = Vertex < VertexCount;
                if (Valid)
                    Face[Offset] = PointOfVertex[Vertex];
            }
            if (Valid && Face[0] != Face[1] && Face[1] != Face[2] && Face[2] != Face[0])
                Topology.Faces.insert(Topology.Faces.end(), Face.begin(), Face.end());
        }

        const std::size_t FaceCount = Topology.Faces.size() / 3;
        std::vector<DirectedEdge> Directed;
        Directed.reserve(FaceCount * 3);
        for (std::size_t Face = 0; Face < FaceCount; ++Face)
        {
            for (std::size_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t From = Topology.Faces[Face * 3 + Corner];
                const uint32_t To = Topology.Faces[Face * 3 + (Corner + 1) % 3];
                const uint64_t Key = (uint64_t{std::min(From, To)} << 32) | std::max(From, To);
                Directed.push_back(DirectedEdge{Key, static_cast<uint32_t>(Face), From < To ? 1 : -1});
            }
        }
        std::sort(Directed.begin(), Directed.end(),
                  [](const DirectedEdge& A, const DirectedEdge& B) { return A.Key < B.Key; });
        Topology.Uses.reserve(Directed.size());
        for (std::size_t Index = 0; Index < Directed.size(); ++Index)
        {
            if (Index == 0 || Directed[Index].Key != Directed[Index - 1].Key)
                Topology.Edges.push_back(ShadowTopology::Edge{static_cast<uint32_t>(Directed[Index].Key >> 32),
                                                              static_cast<uint32_t>(Directed[Index].Key),
                                                              static_cast<uint32_t>(Index), 0});
            ++Topology.Edges.back().UseCount;
            Topology.Uses.push_back(ShadowTopology::EdgeUse{Directed[Index].Face, Directed[Index].Direction});
        }
        return Topology;
    }

    void ExtrudeShadowMesh(const ShadowTopology& Topology, std::span<const Vec3> Vertices, const Vec3& Light,
                           float Range, std::vector<uint8_t>& Lit, std::vector<Vec3>& VolumeVertices,
                           std::vector<uint32_t>& VolumeIndices)
    {
        const auto First = static_cast<uint32_t>(VolumeVertices.size());
        const auto Near = [First](uint32_t Point) { return First + Point * 2; };
        const auto Far = [First](uint32_t Point) { return First + Point * 2 + 1; };
        const auto Add = [&VolumeIndices](uint32_t A, uint32_t B, uint32_t C)
        { VolumeIndices.insert(VolumeIndices.end(), {A, B, C}); };

        VolumeVertices.reserve(VolumeVertices.size() + Topology.Points.size() * 2);
        for (const uint32_t Vertex : Topology.Points)
        {
            const Vec3& Point = Vertices[Vertex];
            const Vec3 Away = Vec3Sub(Point, Light);
            // A point on the light has no direction away from it: any will do.
            const Vec3 Direction = Vec3Dot(Away, Away) > 0.000001f ? Vec3Normalize(Away) : Vec3{0.0f, -1.0f, 0.0f};
            // Pushed slightly away so the near cap does not fight with the surface it comes from.
            const Vec3 NearPoint{Point.X + Direction.X * 0.1f, Point.Y + Direction.Y * 0.1f,
                                 Point.Z + Direction.Z * 0.1f};
            VolumeVertices.push_back(NearPoint);
            VolumeVertices.push_back(Vec3{NearPoint.X + Direction.X * Range, NearPoint.Y + Direction.Y * Range,
                                          NearPoint.Z + Direction.Z * Range});
        }

        const std::size_t FaceCount = Topology.Faces.size() / 3;
        Lit.assign(FaceCount, 0);
        for (std::size_t Face = 0; Face < FaceCount; ++Face)
        {
            const uint32_t P0 = Topology.Faces[Face * 3];
            const uint32_t P1 = Topology.Faces[Face * 3 + 1];
            const uint32_t P2 = Topology.Faces[Face * 3 + 2];
            const Vec3& V0 = Vertices[Topology.Points[P0]];
            const Vec3& V1 = Vertices[Topology.Points[P1]];
            const Vec3& V2 = Vertices[Topology.Points[P2]];
            const Vec3 Normal = Vec3Cross(Vec3Sub(V1, V0), Vec3Sub(V2, V0));
            const Vec3 Center{(V0.X + V1.X + V2.X) / 3.0f, (V0.Y + V1.Y + V2.Y) / 3.0f, (V0.Z + V1.Z + V2.Z) / 3.0f};
            if (Vec3Dot(Normal, Vec3Sub(Light, Center)) <= 0.000001f)
                continue;
            Lit[Face] = 1;
            Add(Near(P0), Near(P1), Near(P2));
            Add(Far(P2), Far(P1), Far(P0));
        }

        for (const ShadowTopology::Edge& Edge : Topology.Edges)
        {
            int Unmatched = 0;
            for (uint32_t Use = Edge.FirstUse; Use < Edge.FirstUse + Edge.UseCount; ++Use)
                Unmatched += Lit[Topology.Uses[Use].Face] ? Topology.Uses[Use].Direction : 0;
            const uint32_t From = Unmatched > 0 ? Edge.A : Edge.B;
            const uint32_t To = Unmatched > 0 ? Edge.B : Edge.A;
            for (int Side = std::abs(Unmatched); Side > 0; --Side)
            {
                Add(Near(From), Far(To), Near(To));
                Add(Near(From), Far(From), Far(To));
            }
        }
    }
} // namespace dbc
