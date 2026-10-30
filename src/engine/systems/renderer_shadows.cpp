#include "engine_kernel.hpp"
#include "light_manager.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    void AppendShadowMeshSources(EngineKernel& K, std::vector<Triangle>& Sources)
    {
        K.Objects().ForEachObject(
            [&](int /*id*/, const Object3D& Object)
            {
                if (!Object.Visible || !Object.ShadowShading || Object.ShadowMeshes.empty())
                    return;
                SurfaceStyle Style{};
                Style.Fog = false;
                Style.Ambient = false;
                Style.ShadowCaster = true;
                Style.ShadowRange = Object.ShadowRange;
                for (const Mesh& MeshValue : Object.ShadowMeshes)
                {
                    if (!MeshValue.Visible)
                        continue;
                    const auto VertexAt = [&](uint32_t Index) -> const Vec3*
                    {
                        return Index < MeshValue.Geometry().Vertices.size() ? &MeshValue.Geometry().Vertices[Index]
                                                                            : nullptr;
                    };
                    const std::size_t TriangleCount = MeshValue.Geometry().Indices.empty()
                                                          ? MeshValue.Geometry().Vertices.size() / 3
                                                          : MeshValue.Geometry().Indices.size() / 3;
                    for (std::size_t TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
                    {
                        const uint32_t I0 = MeshValue.Geometry().Indices.empty()
                                                ? static_cast<uint32_t>(TriangleIndex * 3)
                                                : MeshValue.Geometry().Indices[TriangleIndex * 3];
                        const uint32_t I1 = MeshValue.Geometry().Indices.empty()
                                                ? static_cast<uint32_t>(TriangleIndex * 3 + 1)
                                                : MeshValue.Geometry().Indices[TriangleIndex * 3 + 1];
                        const uint32_t I2 = MeshValue.Geometry().Indices.empty()
                                                ? static_cast<uint32_t>(TriangleIndex * 3 + 2)
                                                : MeshValue.Geometry().Indices[TriangleIndex * 3 + 2];
                        const Vec3* Local0 = VertexAt(I0);
                        const Vec3* Local1 = VertexAt(I1);
                        const Vec3* Local2 = VertexAt(I2);
                        if (!Local0 || !Local1 || !Local2)
                            continue;
                        const Vec3 World0 = K.Renderer().WorldMeshPoint(Object, MeshValue, *Local0);
                        const Vec3 World1 = K.Renderer().WorldMeshPoint(Object, MeshValue, *Local1);
                        const Vec3 World2 = K.Renderer().WorldMeshPoint(Object, MeshValue, *Local2);
                        const Vec3 View0 = dbc::WorldToViewPoint(K, World0.X, World0.Y, World0.Z);
                        const Vec3 View1 = dbc::WorldToViewPoint(K, World1.X, World1.Y, World1.Z);
                        const Vec3 View2 = dbc::WorldToViewPoint(K, World2.X, World2.Y, World2.Z);
                        ViewVertex V0{};
                        V0.X = View0.X;
                        V0.Y = View0.Y;
                        V0.Z = View0.Z;
                        ViewVertex V1{};
                        V1.X = View1.X;
                        V1.Y = View1.Y;
                        V1.Z = View1.Z;
                        ViewVertex V2{};
                        V2.X = View2.X;
                        V2.Y = View2.Y;
                        V2.Z = View2.Z;
                        dbc::AppendClippedViewTriangle(K, V0, V1, V2, nullptr, false, Sources, false, Style);
                    }
                }
            });
    }

    void BuildShadowVolumes(EngineKernel& K, const std::vector<Triangle>& Sources, std::vector<Triangle>& Volumes)
    {
        const int LightMode = K.Renderer().ShadowPositionMode();
        const Vec3 FixedLight = K.Renderer().ShadowPosition();
        const Vec3 WorldLight = LightMode == -1
                                    ? FixedLight
                                    : Vec3{K.Lights().LightPositionX(LightMode), K.Lights().LightPositionY(LightMode),
                                           K.Lights().LightPositionZ(LightMode)};
        const Vec3 Light = dbc::WorldToViewPoint(K, WorldLight.X, WorldLight.Y, WorldLight.Z);
        SurfaceStyle VolumeStyle{};
        VolumeStyle.Fog = false;
        VolumeStyle.Ambient = false;
        VolumeStyle.ShadowVolume = true;
        const auto MakeVertex = [](const Vec3& Point)
        {
            ViewVertex Vertex{};
            Vertex.X = Point.X;
            Vertex.Y = Point.Y;
            Vertex.Z = Point.Z;
            Vertex.Color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
            return Vertex;
        };
        const auto DirectionAwayFromLight = [&](const Vec3& Point, const Vec3& Fallback)
        {
            Vec3 Direction = Vec3Sub(Point, Light);
            const float LengthSquared = Vec3Dot(Direction, Direction);
            return LengthSquared > 0.000001f ? Vec3Normalize(Direction) : Fallback;
        };
        for (const Triangle& Source : Sources)
        {
            if (!Source.Style.ShadowCaster || Source.Is2d || Source.Style.ReflectionPass)
                continue;
            const float Range = std::max(0.0f, Source.Style.ShadowRange);
            if (Range <= 0.0f)
                continue;
            Vec3 Base[3]{{Source.Pos[0][0], Source.Pos[0][1], Source.Pos[0][2]},
                         {Source.Pos[1][0], Source.Pos[1][1], Source.Pos[1][2]},
                         {Source.Pos[2][0], Source.Pos[2][1], Source.Pos[2][2]}};
            const Vec3 FaceNormal = Vec3Cross(Vec3Sub(Base[1], Base[0]), Vec3Sub(Base[2], Base[0]));
            const Vec3 Center{(Base[0].X + Base[1].X + Base[2].X) / 3.0f, (Base[0].Y + Base[1].Y + Base[2].Y) / 3.0f,
                              (Base[0].Z + Base[1].Z + Base[2].Z) / 3.0f};
            if (Vec3Dot(FaceNormal, Vec3Sub(Light, Center)) <= 0.000001f)
                continue;
            Vec3 Far[3]{};
            const Vec3 Fallback = Vec3Normalize(Vec3{FaceNormal.X * -1.0f, FaceNormal.Y * -1.0f, FaceNormal.Z * -1.0f});
            for (int VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
            {
                const Vec3 Direction = DirectionAwayFromLight(Base[VertexIndex], Fallback);
                Base[VertexIndex].X += Direction.X * 0.1f;
                Base[VertexIndex].Y += Direction.Y * 0.1f;
                Base[VertexIndex].Z += Direction.Z * 0.1f;
                Far[VertexIndex] =
                    Vec3{Base[VertexIndex].X + Direction.X * Range, Base[VertexIndex].Y + Direction.Y * Range,
                         Base[VertexIndex].Z + Direction.Z * Range};
            }
            const auto Append = [&](const Vec3& A, const Vec3& B, const Vec3& C)
            {
                dbc::AppendClippedViewTriangle(K, MakeVertex(A), MakeVertex(B), MakeVertex(C), nullptr, false, Volumes,
                                               false, VolumeStyle, -1, false, 0.0f, -1, 0, false, true);
            };
            Append(Base[0], Base[1], Base[2]);
            Append(Far[2], Far[1], Far[0]);
            for (int Edge = 0; Edge < 3; ++Edge)
            {
                const int Next = (Edge + 1) % 3;
                Append(Base[Edge], Far[Next], Base[Next]);
                Append(Base[Edge], Far[Edge], Far[Next]);
            }
        }
    }
} // namespace dbc
