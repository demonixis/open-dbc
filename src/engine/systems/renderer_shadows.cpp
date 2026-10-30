#include "engine_kernel.hpp"
#include "light_manager.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    void AppendShadowSources(EngineKernel& K, std::vector<Triangle>& Sources)
    {
        std::vector<Vec3> World;
        K.Objects().ForEachObject(
            [&](int /*id*/, const Object3D& Object)
            {
                if (!Object.Visible || !Object.ShadowShading)
                    return;
                // A locked object lives on the screen, not in the world: it casts from what it draws.
                if (Object.LockToScreen && Object.ShadowMeshes.empty())
                    return;
                Triangle Source;
                Source.Style.Fog = false;
                Source.Style.Ambient = false;
                Source.Style.ShadowCaster = true;
                Source.Style.ShadowRange = Object.ShadowRange;
                for (const Mesh& MeshValue : Object.ShadowMeshes.empty() ? Object.Meshes : Object.ShadowMeshes)
                {
                    if (!MeshValue.Visible)
                        continue;
                    if (!Object.CachedNodeGlobals.empty() && MeshValue.SourceMeshIndex >= 0)
                    {
                        const auto Node =
                            Object.MeshNodeByIndex.find(static_cast<unsigned int>(MeshValue.SourceMeshIndex));
                        if (Node != Object.MeshNodeByIndex.end() && NodeHiddenByLimb(Object, Node->second))
                            continue;
                    }
                    K.Renderer().WorldMeshVertices(Object, MeshValue, World);
                    const std::vector<uint32_t>& Indices = MeshValue.Geometry().Indices;
                    const std::size_t CornerCount = Indices.empty() ? World.size() : Indices.size();
                    for (std::size_t Corner = 0; Corner + 2 < CornerCount; Corner += 3)
                    {
                        bool Valid = true;
                        for (std::size_t Offset = 0; Offset < 3; ++Offset)
                        {
                            const std::size_t Index = Indices.empty() ? Corner + Offset : Indices[Corner + Offset];
                            if (Index >= World.size())
                            {
                                Valid = false;
                                break;
                            }
                            const Vec3 View = dbc::WorldToViewPoint(K, World[Index].X, World[Index].Y, World[Index].Z);
                            Source.Pos[Offset][0] = View.X;
                            Source.Pos[Offset][1] = View.Y;
                            Source.Pos[Offset][2] = View.Z;
                        }
                        if (Valid)
                            Sources.push_back(Source);
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
