#include "object_manager.hpp"
#include "camera_system.hpp"
#include "collision_math.hpp"
#include "core_system.hpp"
#include "ellipsoid_collision.hpp"
#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "log.hpp"
#include "path_utf8.hpp"
#include "platform_io.hpp"
#include "polygon_collision.hpp"
#include "renderer_system.hpp"

#include <assimp/postprocess.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <unordered_set>

#ifdef _WIN32
#undef GetObject
#endif

namespace dbc
{

    // ── File-local animation helpers ─────────────────────────────────

    namespace
    {
        [[nodiscard]] AnimationClip MakeAnimationClip(const aiAnimation* Anim, int StartFrame)
        {
            AnimationClip Clip{};
            Clip.StartFrame = std::max(0, StartFrame);
            if (!Anim)
            {
                Clip.EndFrame = Clip.StartFrame;
                return Clip;
            }

            Clip.DurationTicks = std::isfinite(Anim->mDuration) ? std::max(0.0, Anim->mDuration) : 0.0;
            Clip.TicksPerSecond =
                std::isfinite(Anim->mTicksPerSecond) && Anim->mTicksPerSecond > 0.0 ? Anim->mTicksPerSecond : 25.0;
            const double DurationFrames = Clip.DurationTicks * 25.0 / Clip.TicksPerSecond;
            const double MaximumFrames = static_cast<double>(std::numeric_limits<int>::max() - Clip.StartFrame);
            Clip.EndFrame =
                Clip.StartFrame + static_cast<int>(std::lround(std::clamp(DurationFrames, 0.0, MaximumFrames)));
            for (unsigned int I = 0; I < Anim->mNumChannels; ++I)
            {
                const aiNodeAnim* Channel = Anim->mChannels[I];
                if (Channel)
                {
                    Clip.Channels[Channel->mNodeName.C_Str()] = Channel;
                }
            }
            return Clip;
        }

        void ApplyAnimationClip(Object3D& Object, int ClipIndex, int TargetFrame)
        {
            if (Object.AnimationClips.empty())
            {
                return;
            }
            ClipIndex = std::clamp(ClipIndex, 0, static_cast<int>(Object.AnimationClips.size()) - 1);
            Object.ActiveAnimationClip = ClipIndex;

            const AnimationClip& Clip = Object.AnimationClips[ClipIndex];
            Object.AnimChannelByNode = Clip.Channels;
            Object.ActiveAnimDurationTicks = Clip.DurationTicks;
            Object.ActiveAnimTicksPerSec = Clip.TicksPerSecond;

            const int LocalFrame =
                std::clamp(TargetFrame - Clip.StartFrame, 0, std::max(0, Clip.EndFrame - Clip.StartFrame));
            Object.AnimTimeSeconds = static_cast<double>(LocalFrame) / 25.0;
            Object.Frame = Clip.StartFrame + LocalFrame;
        }

        [[nodiscard]] int AnimationClipIndexForFrame(const std::vector<AnimationClip>& Clips, int Frame)
        {
            int Chosen = 0;
            for (int Index = 0; Index < static_cast<int>(Clips.size()); ++Index)
            {
                const AnimationClip& Clip = Clips[static_cast<std::size_t>(Index)];
                if (Frame >= Clip.StartFrame && Frame <= Clip.EndFrame)
                    return Index;
                if (Frame >= Clip.StartFrame)
                    Chosen = Index;
            }
            return Chosen;
        }

        void SelectAnimationClipForFrame(Object3D& Object, int Frame)
        {
            if (Object.AnimationClips.empty())
            {
                return;
            }
            ApplyAnimationClip(Object, AnimationClipIndexForFrame(Object.AnimationClips, Frame), Frame);
        }

        [[nodiscard]] bool HasAssimpSyntheticRoot(const aiNode* Node)
        {
            if (!Node)
                return false;
            const std::string_view Name = Node->mName.C_Str();
            return Name == "$dummy_root" || Name == "$dummy_node";
        }

        void BuildImportedLimbMap(const aiScene* Scene, Object3D& Object, bool AddSyntheticRoot)
        {
            if (!Scene || !Scene->mRootNode)
                return;

            if (!AddSyntheticRoot)
            {
                RendererSystem::BuildLimbMapRecursive(Scene->mRootNode, Object.LimbNodes, Object.LimbIndexByNode);
                return;
            }

            Object.CompatibilityRootNode = std::make_unique<aiNode>();
            const aiNode* CompatibilityRoot = Object.CompatibilityRootNode.get();
            Object.LimbNodes.push_back(CompatibilityRoot);
            Object.LimbIndexByNode[CompatibilityRoot] = 0;

            if (HasAssimpSyntheticRoot(Scene->mRootNode))
            {
                Object.CompatibilitySkippedRootNode = Scene->mRootNode;
                for (unsigned int Child = 0; Child < Scene->mRootNode->mNumChildren; ++Child)
                {
                    RendererSystem::BuildLimbMapRecursive(Scene->mRootNode->mChildren[Child], Object.LimbNodes,
                                                          Object.LimbIndexByNode);
                }
                return;
            }

            RendererSystem::BuildLimbMapRecursive(Scene->mRootNode, Object.LimbNodes, Object.LimbIndexByNode);
        }

        // Limb mesh iteration helper
        template <typename Fn> void ForEachLimbMesh(Object3D& Object, int LimbId, Fn&& FnValue)
        {
            if (LimbId < 0)
            {
                return;
            }
            const size_t Li = static_cast<size_t>(LimbId);
            if (Li < Object.LimbNodes.size())
            {
                const aiNode* Node = Object.LimbNodes[Li];
                if (!Node)
                {
                    return;
                }
                for (unsigned int Nm = 0; Nm < Node->mNumMeshes; ++Nm)
                {
                    const int Target = static_cast<int>(Node->mMeshes[Nm]);
                    for (Mesh& M : Object.Meshes)
                    {
                        if (M.SourceMeshIndex == Target)
                        {
                            FnValue(M);
                        }
                    }
                }
                return;
            }
            // Fallback for procedural objects.
            if (Li < Object.Meshes.size())
            {
                FnValue(Object.Meshes[Li]);
            }
        }

        void SmoothMeshNormals(Mesh& MeshValue, float Percentage)
        {
            if (MeshValue.Geometry().Normals.size() != MeshValue.Geometry().Vertices.size())
                return;

            const std::vector<Vec3> Source = MeshValue.Geometry().Normals;
            const float Threshold = std::clamp(Percentage, 0.0f, 100.0f) / 100.0f;
            MeshGeometry& Geometry = MeshValue.EditGeometry();
            for (std::size_t VertexIndex = 0; VertexIndex < Geometry.Vertices.size(); ++VertexIndex)
            {
                Vec3 Average = Source[VertexIndex];
                unsigned AverageCount = 1;
                unsigned SharedCount = 0;
                for (std::size_t ScanIndex = 0; ScanIndex < Geometry.Vertices.size() && SharedCount < 32; ++ScanIndex)
                {
                    if (VertexIndex == ScanIndex)
                        continue;
                    const Vec3& Vertex = Geometry.Vertices[VertexIndex];
                    const Vec3& ScanVertex = Geometry.Vertices[ScanIndex];
                    if (Vertex.X != ScanVertex.X || Vertex.Y != ScanVertex.Y || Vertex.Z != ScanVertex.Z)
                        continue;

                    ++SharedCount;
                    const Vec3& Other = Source[ScanIndex];
                    const float Difference =
                        (std::abs(Source[VertexIndex].X - Other.X) + std::abs(Source[VertexIndex].Y - Other.Y) +
                         std::abs(Source[VertexIndex].Z - Other.Z)) /
                        3.0f;
                    if (Difference <= Threshold)
                    {
                        Average.X += Other.X;
                        Average.Y += Other.Y;
                        Average.Z += Other.Z;
                        ++AverageCount;
                    }
                }
                const float Divisor = static_cast<float>(AverageCount);
                Geometry.Normals[VertexIndex] = {Average.X / Divisor, Average.Y / Divisor, Average.Z / Divisor};
            }
        }

        // AABB helpers (file-local)
        [[nodiscard]] AABB ComputeObjectAabb(EngineKernel& Kernel, const Object3D& Object)
        {
            AABB Box{};
            bool First = true;
            for (const Mesh& MeshValue : Object.Meshes)
            {
                std::vector<Vec3> Vertices;
                Kernel.Renderer().ObjectMeshVertices(Object, MeshValue, Vertices);
                for (const Vec3& Local : Vertices)
                {
                    if (First)
                    {
                        Box = {Local.X, Local.Y, Local.Z, Local.X, Local.Y, Local.Z};
                        First = false;
                    }
                    else
                    {
                        Box.MinX = std::min(Box.MinX, Local.X);
                        Box.MinY = std::min(Box.MinY, Local.Y);
                        Box.MinZ = std::min(Box.MinZ, Local.Z);
                        Box.MaxX = std::max(Box.MaxX, Local.X);
                        Box.MaxY = std::max(Box.MaxY, Local.Y);
                        Box.MaxZ = std::max(Box.MaxZ, Local.Z);
                    }
                }
            }
            return Box;
        }

        [[nodiscard]] AABB WorldObjectAabb(EngineKernel& K, const Object3D& Object)
        {
            AABB Local = Object.CollisionAabbExplicit ? Object.CollisionAabb : ComputeObjectAabb(K, Object);
            if (Object.CollisionBoxRotates)
            {
                AABB World{};
                bool First = true;
                for (const float X : {Local.MinX, Local.MaxX})
                {
                    for (const float Y : {Local.MinY, Local.MaxY})
                    {
                        for (const float Z : {Local.MinZ, Local.MaxZ})
                        {
                            const Vec3 Point = K.Renderer().WorldObjectPoint(Object, {X, Y, Z});
                            if (First)
                            {
                                World = {Point.X, Point.Y, Point.Z, Point.X, Point.Y, Point.Z};
                                First = false;
                            }
                            else
                            {
                                World.MinX = std::min(World.MinX, Point.X);
                                World.MinY = std::min(World.MinY, Point.Y);
                                World.MinZ = std::min(World.MinZ, Point.Z);
                                World.MaxX = std::max(World.MaxX, Point.X);
                                World.MaxY = std::max(World.MaxY, Point.Y);
                                World.MaxZ = std::max(World.MaxZ, Point.Z);
                            }
                        }
                    }
                }
                return World;
            }
            const Vec3 Origin = K.Renderer().WorldObjectOrigin(Object);
            const float X1 = Local.MinX * Object.ScaleX;
            const float Y1 = Local.MinY * Object.ScaleY;
            const float Z1 = Local.MinZ * Object.ScaleZ;
            const float X2 = Local.MaxX * Object.ScaleX;
            const float Y2 = Local.MaxY * Object.ScaleY;
            const float Z2 = Local.MaxZ * Object.ScaleZ;
            return {
                Origin.X + std::min(X1, X2), Origin.Y + std::min(Y1, Y2), Origin.Z + std::min(Z1, Z2),
                Origin.X + std::max(X1, X2), Origin.Y + std::max(Y1, Y2), Origin.Z + std::max(Z1, Z2),
            };
        }

        [[nodiscard]] AABB WorldMeshAabb(EngineKernel& K, const Object3D& Object, const Mesh& MeshValue)
        {
            AABB Box{};
            bool First = true;
            for (const Vec3& Vertex : MeshValue.Geometry().Vertices)
            {
                const Vec3 World = K.Renderer().WorldMeshPoint(Object, MeshValue, Vertex);
                if (First)
                {
                    Box = {World.X, World.Y, World.Z, World.X, World.Y, World.Z};
                    First = false;
                }
                else
                {
                    Box.MinX = std::min(Box.MinX, World.X);
                    Box.MinY = std::min(Box.MinY, World.Y);
                    Box.MinZ = std::min(Box.MinZ, World.Z);
                    Box.MaxX = std::max(Box.MaxX, World.X);
                    Box.MaxY = std::max(Box.MaxY, World.Y);
                    Box.MaxZ = std::max(Box.MaxZ, World.Z);
                }
            }
            return Box;
        }

        [[nodiscard]] bool RayTriangleIntersect(const Vec3& Origin, const Vec3& Direction, const Vec3& A, const Vec3& B,
                                                const Vec3& C, float& OutT)
        {
            constexpr float Epsilon = 1e-7f;
            const Vec3 Edge1 = Vec3Sub(B, A);
            const Vec3 Edge2 = Vec3Sub(C, A);
            const Vec3 P = Vec3Cross(Direction, Edge2);
            const float Determinant = Vec3Dot(Edge1, P);
            if (std::fabs(Determinant) < Epsilon)
                return false;
            const float Inverse = 1.0f / Determinant;
            const Vec3 T = Vec3Sub(Origin, A);
            const float U = Vec3Dot(T, P) * Inverse;
            if (U < 0.0f || U > 1.0f)
                return false;
            const Vec3 Q = Vec3Cross(T, Edge1);
            const float V = Vec3Dot(Direction, Q) * Inverse;
            if (V < 0.0f || U + V > 1.0f)
                return false;
            const float Distance = Vec3Dot(Edge2, Q) * Inverse;
            if (Distance < 0.0f || Distance > 1.0f)
                return false;
            OutT = Distance;
            return true;
        }

        [[nodiscard]] float RayObjectDistance(EngineKernel& K, const Object3D& Object, const Vec3& Origin,
                                              const Vec3& End)
        {
            const Vec3 Direction = Vec3Sub(End, Origin);
            const float Length = std::sqrt(Vec3Dot(Direction, Direction));
            if (Length < 1e-6f)
                return 0.0f;
            float Best = 2.0f;
            for (const Mesh& MeshValue : Object.Meshes)
            {
                if (!MeshValue.Visible)
                    continue;
                for (std::size_t I = 0; I + 2 < MeshValue.Geometry().Indices.size(); I += 3)
                {
                    const uint32_t I0 = MeshValue.Geometry().Indices[I];
                    const uint32_t I1 = MeshValue.Geometry().Indices[I + 1];
                    const uint32_t I2 = MeshValue.Geometry().Indices[I + 2];
                    if (I0 >= MeshValue.Geometry().Vertices.size() || I1 >= MeshValue.Geometry().Vertices.size() ||
                        I2 >= MeshValue.Geometry().Vertices.size())
                        continue;
                    const Vec3 A = K.Renderer().WorldMeshPoint(Object, MeshValue, MeshValue.Geometry().Vertices[I0]);
                    const Vec3 B = K.Renderer().WorldMeshPoint(Object, MeshValue, MeshValue.Geometry().Vertices[I1]);
                    const Vec3 C = K.Renderer().WorldMeshPoint(Object, MeshValue, MeshValue.Geometry().Vertices[I2]);
                    float T = 0.0f;
                    if (RayTriangleIntersect(Origin, Direction, A, B, C, T))
                        Best = std::min(Best, T);
                }
            }
            return Best <= 1.0f ? Best * Length : 0.0f;
        }

        [[nodiscard]] Vec3 ScreenPickDirection(EngineKernel& K, int ScreenX, int ScreenY, bool IgnoreCamera = false)
        {
            const CameraSystem& Camera = K.Camera();
            const int Left = Camera.HasCameraView() ? Camera.ViewLeft() : 0;
            const int Top = Camera.HasCameraView() ? Camera.ViewTop() : 0;
            const int Right = Camera.HasCameraView() ? Camera.ViewRight() : K.Core().WindowWidth();
            const int Bottom = Camera.HasCameraView() ? Camera.ViewBottom() : K.Core().WindowHeight();
            const float HalfWidth = static_cast<float>(std::max(1, Right - Left)) * 0.5f;
            const float HalfHeight = static_cast<float>(std::max(1, Bottom - Top)) * 0.5f;
            const float TanHalf = std::tan(Camera.Fov() * static_cast<float>(M_PI / 360.0));
            const float FocalX = HalfWidth / (TanHalf * Camera.Aspect());
            const float FocalY = HalfHeight / TanHalf;
            Vec3 Direction{(static_cast<float>(ScreenX - Left) - HalfWidth) / FocalX,
                           (HalfHeight - static_cast<float>(ScreenY - Top)) / FocalY, 1.0f};
            Direction = Vec3Normalize(Direction);
            if (IgnoreCamera)
                return Direction;
            if (Camera.IsZyxRotation())
            {
                Direction = RotateZ(Direction, Camera.AngleZ());
                Direction = RotateY(Direction, Camera.AngleY());
                Direction = RotateX(Direction, Camera.AngleX());
            }
            else
            {
                Direction = RotateZ(Direction, Camera.AngleZ());
                Direction = RotateX(Direction, Camera.AngleX());
                Direction = RotateY(Direction, Camera.AngleY());
            }
            return Vec3Normalize(Direction);
        }

        // aabb_intersect, sphere_aabb_intersect — see collision_math.hpp

        struct CollisionSphereSnapshot
        {
            Vec3 Center{};
            float Radius = 0.0f;
            AABB Bounds{};
        };

        struct CollisionMeshSnapshot
        {
            std::vector<Vec3> Vertices;
            std::vector<CollisionPrimitive> Primitives;
            AABB Bounds{};
            bool BoundsValid = false;
            bool Closed = false;
        };

        struct CollisionSnapshot
        {
            CollisionShape Shape = CollisionShape::Sphere;
            std::vector<CollisionSphereSnapshot> Spheres;
            std::vector<CollisionMeshSnapshot> Meshes;
            AABB Bounds{};
            bool BoundsValid = false;
        };

        void IncludeCollisionPoint(AABB& Bounds, bool& Valid, const Vec3& Point)
        {
            if (!Valid)
            {
                Bounds = {Point.X, Point.Y, Point.Z, Point.X, Point.Y, Point.Z};
                Valid = true;
                return;
            }
            Bounds.MinX = std::min(Bounds.MinX, Point.X);
            Bounds.MinY = std::min(Bounds.MinY, Point.Y);
            Bounds.MinZ = std::min(Bounds.MinZ, Point.Z);
            Bounds.MaxX = std::max(Bounds.MaxX, Point.X);
            Bounds.MaxY = std::max(Bounds.MaxY, Point.Y);
            Bounds.MaxZ = std::max(Bounds.MaxZ, Point.Z);
        }

        void IncludeCollisionBounds(AABB& Bounds, bool& Valid, const AABB& Added)
        {
            IncludeCollisionPoint(Bounds, Valid, {Added.MinX, Added.MinY, Added.MinZ});
            IncludeCollisionPoint(Bounds, Valid, {Added.MaxX, Added.MaxY, Added.MaxZ});
        }

        struct WeldedPointKey
        {
            int64_t X = 0;
            int64_t Y = 0;
            int64_t Z = 0;

            bool operator==(const WeldedPointKey&) const = default;
        };

        struct WeldedPointKeyHash
        {
            std::size_t operator()(const WeldedPointKey& Value) const
            {
                std::size_t Result = std::hash<int64_t>{}(Value.X);
                Result ^= std::hash<int64_t>{}(Value.Y) + 0x9e3779b9 + (Result << 6) + (Result >> 2);
                Result ^= std::hash<int64_t>{}(Value.Z) + 0x9e3779b9 + (Result << 6) + (Result >> 2);
                return Result;
            }
        };

        [[nodiscard]] bool IsClosedCollisionMesh(const std::vector<Vec3>& Vertices,
                                                 const std::vector<std::array<std::size_t, 3>>& Faces,
                                                 const AABB& Bounds)
        {
            if (Faces.empty())
                return false;
            const float Dx = Bounds.MaxX - Bounds.MinX;
            const float Dy = Bounds.MaxY - Bounds.MinY;
            const float Dz = Bounds.MaxZ - Bounds.MinZ;
            const float Epsilon = std::max(1e-5f, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) * 1e-5f);
            std::unordered_map<WeldedPointKey, int, WeldedPointKeyHash> Welded;
            std::vector<int> WeldedIndices(Vertices.size(), -1);
            for (std::size_t Index = 0; Index < Vertices.size(); ++Index)
            {
                const Vec3& Vertex = Vertices[Index];
                const WeldedPointKey Key{static_cast<int64_t>(std::llround(Vertex.X / Epsilon)),
                                         static_cast<int64_t>(std::llround(Vertex.Y / Epsilon)),
                                         static_cast<int64_t>(std::llround(Vertex.Z / Epsilon))};
                const int Candidate = static_cast<int>(Welded.size());
                WeldedIndices[Index] = Welded.try_emplace(Key, Candidate).first->second;
            }

            std::unordered_map<uint64_t, unsigned int> EdgeCounts;
            for (const auto& Face : Faces)
            {
                const std::array<int, 3> Indices{
                    {WeldedIndices[Face[0]], WeldedIndices[Face[1]], WeldedIndices[Face[2]]}};
                if (Indices[0] == Indices[1] || Indices[1] == Indices[2] || Indices[2] == Indices[0])
                    return false;
                for (int Edge = 0; Edge < 3; ++Edge)
                {
                    const uint32_t First = static_cast<uint32_t>(std::min(Indices[Edge], Indices[(Edge + 1) % 3]));
                    const uint32_t Second = static_cast<uint32_t>(std::max(Indices[Edge], Indices[(Edge + 1) % 3]));
                    ++EdgeCounts[(static_cast<uint64_t>(First) << 32) | Second];
                }
            }
            return !EdgeCounts.empty() && std::all_of(EdgeCounts.begin(), EdgeCounts.end(),
                                                      [](const auto& Entry) { return Entry.second == 2; });
        }

        [[nodiscard]] CollisionSnapshot BuildCollisionSnapshot(EngineKernel& Kernel, const Object3D& Object)
        {
            CollisionSnapshot Result{};
            Result.Shape = Object.CollisionShapeValue;
            if (Result.Shape == CollisionShape::Aabb)
            {
                if (!Object.Meshes.empty() || Object.CollisionAabbExplicit)
                {
                    Result.Bounds = WorldObjectAabb(Kernel, Object);
                    Result.BoundsValid = true;
                }
                return Result;
            }

            for (const Mesh& MeshValue : Object.Meshes)
            {
                if (MeshValue.Geometry().Vertices.empty())
                    continue;
                CollisionMeshSnapshot MeshSnapshot{};
                Kernel.Renderer().WorldMeshVertices(Object, MeshValue, MeshSnapshot.Vertices);
                for (const Vec3& World : MeshSnapshot.Vertices)
                    IncludeCollisionPoint(MeshSnapshot.Bounds, MeshSnapshot.BoundsValid, World);

                if (Result.Shape == CollisionShape::Sphere)
                {
                    const Vec3 Center{(MeshSnapshot.Bounds.MinX + MeshSnapshot.Bounds.MaxX) * 0.5f,
                                      (MeshSnapshot.Bounds.MinY + MeshSnapshot.Bounds.MaxY) * 0.5f,
                                      (MeshSnapshot.Bounds.MinZ + MeshSnapshot.Bounds.MaxZ) * 0.5f};
                    float RadiusSquared = 0.0f;
                    for (const Vec3& Vertex : MeshSnapshot.Vertices)
                        RadiusSquared = std::max(RadiusSquared, CollisionDistanceSquared(Vertex, Center));
                    const float Radius = std::sqrt(RadiusSquared);
                    const AABB SphereBounds{Center.X - Radius, Center.Y - Radius, Center.Z - Radius,
                                            Center.X + Radius, Center.Y + Radius, Center.Z + Radius};
                    Result.Spheres.push_back({Center, Radius, SphereBounds});
                    IncludeCollisionBounds(Result.Bounds, Result.BoundsValid, SphereBounds);
                    continue;
                }

                std::vector<std::array<std::size_t, 3>> Faces;
                const auto AddFace = [&](std::size_t A, std::size_t B, std::size_t C)
                {
                    if (A >= MeshSnapshot.Vertices.size() || B >= MeshSnapshot.Vertices.size() ||
                        C >= MeshSnapshot.Vertices.size())
                        return;
                    CollisionPrimitive Primitive = ClassifyCollisionPrimitive(
                        MeshSnapshot.Vertices[A], MeshSnapshot.Vertices[B], MeshSnapshot.Vertices[C]);
                    if (Primitive.Kind == CollisionPrimitiveKind::Triangle)
                        Faces.push_back({A, B, C});
                    MeshSnapshot.Primitives.push_back(std::move(Primitive));
                };
                if (MeshValue.Geometry().Indices.empty())
                {
                    for (std::size_t Index = 0; Index + 2 < MeshSnapshot.Vertices.size(); Index += 3)
                        AddFace(Index, Index + 1, Index + 2);
                }
                else
                {
                    for (std::size_t Index = 0; Index + 2 < MeshValue.Geometry().Indices.size(); Index += 3)
                        AddFace(MeshValue.Geometry().Indices[Index], MeshValue.Geometry().Indices[Index + 1],
                                MeshValue.Geometry().Indices[Index + 2]);
                }
                if (MeshSnapshot.Primitives.empty())
                    continue;
                MeshSnapshot.Closed = IsClosedCollisionMesh(MeshSnapshot.Vertices, Faces, MeshSnapshot.Bounds);
                IncludeCollisionBounds(Result.Bounds, Result.BoundsValid, MeshSnapshot.Bounds);
                Result.Meshes.push_back(std::move(MeshSnapshot));
            }
            return Result;
        }

        struct CollisionBroadphaseBounds
        {
            AABB Bounds{};
            bool Valid = false;
        };

        [[nodiscard]] CollisionBroadphaseBounds BuildCollisionBroadphaseBounds(EngineKernel& Kernel,
                                                                               const Object3D& Object)
        {
            CollisionBroadphaseBounds Result{};
            if (Object.CollisionShapeValue == CollisionShape::Aabb)
            {
                if (!Object.Meshes.empty() || Object.CollisionAabbExplicit)
                {
                    Result.Bounds = WorldObjectAabb(Kernel, Object);
                    Result.Valid = true;
                }
                return Result;
            }

            for (const Mesh& MeshValue : Object.Meshes)
            {
                if (MeshValue.Geometry().Vertices.empty())
                    continue;
                AABB MeshBounds{};
                bool MeshBoundsValid = false;
                std::vector<Vec3> Vertices;
                Kernel.Renderer().WorldMeshVertices(Object, MeshValue, Vertices);
                for (const Vec3& World : Vertices)
                    IncludeCollisionPoint(MeshBounds, MeshBoundsValid, World);
                if (!MeshBoundsValid)
                    continue;
                if (Object.CollisionShapeValue == CollisionShape::Polygon)
                {
                    IncludeCollisionBounds(Result.Bounds, Result.Valid, MeshBounds);
                    continue;
                }

                const Vec3 Center{(MeshBounds.MinX + MeshBounds.MaxX) * 0.5f,
                                  (MeshBounds.MinY + MeshBounds.MaxY) * 0.5f,
                                  (MeshBounds.MinZ + MeshBounds.MaxZ) * 0.5f};
                float RadiusSquared = 0.0f;
                for (const Vec3& Vertex : Vertices)
                    RadiusSquared = std::max(RadiusSquared, CollisionDistanceSquared(Vertex, Center));
                const float Radius = std::sqrt(RadiusSquared);
                IncludeCollisionBounds(Result.Bounds, Result.Valid,
                                       {Center.X - Radius, Center.Y - Radius, Center.Z - Radius, Center.X + Radius,
                                        Center.Y + Radius, Center.Z + Radius});
            }
            return Result;
        }

        [[nodiscard]] bool PointInsideClosedCollisionMesh(const Vec3& Point, const CollisionMeshSnapshot& MeshSnapshot)
        {
            if (!MeshSnapshot.Closed || !PointInsideAabb(Point, MeshSnapshot.Bounds))
                return false;
            for (const CollisionPrimitive& Primitive : MeshSnapshot.Primitives)
                if (CollisionDistanceSquared(Point, ClosestPointOnTriangle(Point, Primitive)) <=
                    Primitive.Epsilon * Primitive.Epsilon)
                    return true;

            const Vec3 Direction = Vec3Normalize({1.0f, 0.37139067f, 0.52917343f});
            std::vector<float> Parameters;
            for (const CollisionPrimitive& Primitive : MeshSnapshot.Primitives)
            {
                float Parameter = 0.0f;
                if (RayTriangleParameter(Point, Direction, Primitive, Parameter))
                    Parameters.push_back(Parameter);
            }
            std::sort(Parameters.begin(), Parameters.end());
            std::size_t UniqueCount = 0;
            float Previous = -std::numeric_limits<float>::infinity();
            for (const float Parameter : Parameters)
            {
                const float Tolerance = std::max(1e-5f, std::fabs(Parameter) * 1e-5f);
                if (UniqueCount == 0 || std::fabs(Parameter - Previous) > Tolerance)
                {
                    ++UniqueCount;
                    Previous = Parameter;
                }
            }
            return (UniqueCount % 2) == 1;
        }

        [[nodiscard]] AABB TranslateCollisionBounds(const AABB& Bounds, const Vec3& Translation)
        {
            return {Bounds.MinX + Translation.X, Bounds.MinY + Translation.Y, Bounds.MinZ + Translation.Z,
                    Bounds.MaxX + Translation.X, Bounds.MaxY + Translation.Y, Bounds.MaxZ + Translation.Z};
        }

        [[nodiscard]] CollisionPrimitive TranslateCollisionPrimitive(const CollisionPrimitive& Primitive,
                                                                     const Vec3& Translation)
        {
            CollisionPrimitive Result = Primitive;
            Result.A = CollisionAdd(Result.A, Translation);
            Result.B = CollisionAdd(Result.B, Translation);
            Result.C = CollisionAdd(Result.C, Translation);
            Result.Bounds = TranslateCollisionBounds(Result.Bounds, Translation);
            return Result;
        }

        [[nodiscard]] Vec3 CollisionTranslationDifference(const Vec3& A, const Vec3& B)
        {
            return {A.X - B.X, A.Y - B.Y, A.Z - B.Z};
        }

        [[nodiscard]] bool SphereSnapshotsOverlap(const CollisionSnapshot& A, const CollisionSnapshot& B,
                                                  const Vec3& TranslationA = {}, const Vec3& TranslationB = {})
        {
            for (const CollisionSphereSnapshot& SphereA : A.Spheres)
            {
                const Vec3 CenterA = CollisionAdd(SphereA.Center, TranslationA);
                const AABB BoundsA = TranslateCollisionBounds(SphereA.Bounds, TranslationA);
                for (const CollisionSphereSnapshot& SphereB : B.Spheres)
                {
                    if (!AabbIntersect(BoundsA, TranslateCollisionBounds(SphereB.Bounds, TranslationB)))
                        continue;
                    const Vec3 CenterB = CollisionAdd(SphereB.Center, TranslationB);
                    const float Radius = SphereA.Radius + SphereB.Radius;
                    if (CollisionDistanceSquared(CenterA, CenterB) <= Radius * Radius)
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool SphereAabbSnapshotsOverlap(const CollisionSnapshot& Sphere, const AABB& Box,
                                                      const Vec3& SphereTranslation = {},
                                                      const Vec3& BoxTranslation = {})
        {
            const AABB TranslatedBox = TranslateCollisionBounds(Box, BoxTranslation);
            for (const CollisionSphereSnapshot& SphereValue : Sphere.Spheres)
                if (SphereAabbIntersect(CollisionAdd(SphereValue.Center, SphereTranslation), SphereValue.Radius,
                                        TranslatedBox))
                    return true;
            return false;
        }

        [[nodiscard]] bool SpherePolygonSnapshotsOverlap(const CollisionSnapshot& Sphere,
                                                         const CollisionSnapshot& Polygon,
                                                         const Vec3& SphereTranslation = {},
                                                         const Vec3& PolygonTranslation = {})
        {
            const Vec3 RelativeSphere = CollisionTranslationDifference(SphereTranslation, PolygonTranslation);
            if (!AabbIntersect(TranslateCollisionBounds(Sphere.Bounds, RelativeSphere), Polygon.Bounds))
                return false;
            for (const CollisionSphereSnapshot& SphereValue : Sphere.Spheres)
            {
                const Vec3 Center = CollisionAdd(SphereValue.Center, RelativeSphere);
                const AABB SphereBounds = TranslateCollisionBounds(SphereValue.Bounds, RelativeSphere);
                for (const CollisionMeshSnapshot& MeshSnapshot : Polygon.Meshes)
                {
                    if (!AabbIntersect(SphereBounds, MeshSnapshot.Bounds))
                        continue;
                    for (const CollisionPrimitive& Primitive : MeshSnapshot.Primitives)
                    {
                        if (!CollisionBoundsIntersect(SphereBounds, Primitive.Bounds, Primitive.Epsilon))
                            continue;
                        if (SpherePrimitiveCollisionContact(Center, SphereValue.Radius, Primitive).Hit)
                            return true;
                    }
                    if (PointInsideClosedCollisionMesh(Center, MeshSnapshot))
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool AabbPolygonSnapshotsOverlap(const AABB& Box, const CollisionSnapshot& Polygon,
                                                       const Vec3& BoxTranslation = {},
                                                       const Vec3& PolygonTranslation = {})
        {
            const AABB RelativeBox =
                TranslateCollisionBounds(Box, CollisionTranslationDifference(BoxTranslation, PolygonTranslation));
            if (!AabbIntersect(RelativeBox, Polygon.Bounds))
                return false;
            for (const CollisionMeshSnapshot& MeshSnapshot : Polygon.Meshes)
            {
                if (!AabbIntersect(RelativeBox, MeshSnapshot.Bounds))
                    continue;
                for (const CollisionPrimitive& Primitive : MeshSnapshot.Primitives)
                    if (TriangleAabbIntersects(Primitive, RelativeBox))
                        return true;
                if (!MeshSnapshot.Closed)
                    continue;
                for (const float X : {RelativeBox.MinX, RelativeBox.MaxX})
                    for (const float Y : {RelativeBox.MinY, RelativeBox.MaxY})
                        for (const float Z : {RelativeBox.MinZ, RelativeBox.MaxZ})
                            if (PointInsideClosedCollisionMesh({X, Y, Z}, MeshSnapshot))
                                return true;
            }
            return false;
        }

        [[nodiscard]] bool PolygonSnapshotsOverlap(const CollisionSnapshot& A, const CollisionSnapshot& B,
                                                   const Vec3& TranslationA = {}, const Vec3& TranslationB = {})
        {
            const Vec3 RelativeA = CollisionTranslationDifference(TranslationA, TranslationB);
            const AABB BoundsA = TranslateCollisionBounds(A.Bounds, RelativeA);
            if (!AabbIntersect(BoundsA, B.Bounds))
                return false;
            for (const CollisionMeshSnapshot& MeshA : A.Meshes)
            {
                const AABB MeshBoundsA = TranslateCollisionBounds(MeshA.Bounds, RelativeA);
                for (const CollisionMeshSnapshot& MeshB : B.Meshes)
                {
                    if (!AabbIntersect(MeshBoundsA, MeshB.Bounds))
                        continue;
                    for (const CollisionPrimitive& PrimitiveA : MeshA.Primitives)
                    {
                        const CollisionPrimitive TranslatedA = TranslateCollisionPrimitive(PrimitiveA, RelativeA);
                        for (const CollisionPrimitive& PrimitiveB : MeshB.Primitives)
                            if (CollisionPrimitivesIntersect(TranslatedA, PrimitiveB))
                                return true;
                    }
                    if (MeshB.Closed)
                        for (const Vec3& Point : MeshA.Vertices)
                            if (PointInsideClosedCollisionMesh(CollisionAdd(Point, RelativeA), MeshB))
                                return true;
                    if (MeshA.Closed)
                        for (const Vec3& Point : MeshB.Vertices)
                            if (PointInsideClosedCollisionMesh(Vec3Sub(Point, RelativeA), MeshA))
                                return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool CollisionSnapshotsOverlap(const CollisionSnapshot& A, const CollisionSnapshot& B,
                                                     const Vec3& TranslationA = {})
        {
            if (!A.BoundsValid || !B.BoundsValid ||
                !AabbIntersect(TranslateCollisionBounds(A.Bounds, TranslationA), B.Bounds))
                return false;
            if (A.Shape == CollisionShape::Sphere && B.Shape == CollisionShape::Sphere)
                return SphereSnapshotsOverlap(A, B, TranslationA);
            if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Aabb)
                return AabbIntersect(TranslateCollisionBounds(A.Bounds, TranslationA), B.Bounds);
            if (A.Shape == CollisionShape::Sphere && B.Shape == CollisionShape::Aabb)
                return SphereAabbSnapshotsOverlap(A, B.Bounds, TranslationA);
            if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Sphere)
                return SphereAabbSnapshotsOverlap(B, A.Bounds, {}, TranslationA);
            if (A.Shape == CollisionShape::Sphere && B.Shape == CollisionShape::Polygon)
                return SpherePolygonSnapshotsOverlap(A, B, TranslationA);
            if (A.Shape == CollisionShape::Polygon && B.Shape == CollisionShape::Sphere)
                return SpherePolygonSnapshotsOverlap(B, A, {}, TranslationA);
            if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Polygon)
                return AabbPolygonSnapshotsOverlap(A.Bounds, B, TranslationA);
            if (A.Shape == CollisionShape::Polygon && B.Shape == CollisionShape::Aabb)
                return AabbPolygonSnapshotsOverlap(B.Bounds, A, {}, TranslationA);
            return PolygonSnapshotsOverlap(A, B, TranslationA);
        }

        void AddSeparationAxis(std::vector<Vec3>& Axes, Vec3 Axis, std::size_t Maximum)
        {
            const float LengthSquared = CollisionLengthSquared(Axis);
            if (LengthSquared <= 1e-20f || Axes.size() >= Maximum)
                return;
            Axis = CollisionScale(Axis, 1.0f / std::sqrt(LengthSquared));
            if (std::fabs(Axis.X) > 1e-6f ? Axis.X < 0.0f : std::fabs(Axis.Y) > 1e-6f ? Axis.Y < 0.0f : Axis.Z < 0.0f)
                Axis = CollisionScale(Axis, -1.0f);
            for (const Vec3& Existing : Axes)
                if (std::fabs(Vec3Dot(Existing, Axis)) >= 0.9999f)
                    return;
            Axes.push_back(Axis);
        }

        void AddPolygonNormals(std::vector<Vec3>& Axes, const CollisionSnapshot& Polygon, std::size_t Maximum)
        {
            for (const CollisionMeshSnapshot& Mesh : Polygon.Meshes)
                for (const CollisionPrimitive& Primitive : Mesh.Primitives)
                    if (Primitive.Kind == CollisionPrimitiveKind::Triangle)
                        AddSeparationAxis(Axes, Primitive.Normal, Maximum);
        }

        [[nodiscard]] std::vector<Vec3> CollisionContactAxes(const CollisionSnapshot& A, const CollisionSnapshot& B,
                                                             std::vector<CollisionContact>& LocalContacts)
        {
            constexpr std::size_t MaximumFeatureAxes = 8;
            std::vector<Vec3> FeatureAxes;
            const auto AddContact = [&](CollisionContact Contact, bool Invert)
            {
                if (!Contact.Hit)
                    return;
                if (Invert)
                {
                    Contact.Normal = CollisionScale(Contact.Normal, -1.0f);
                    Contact.Correction = CollisionScale(Contact.Correction, -1.0f);
                }
                if (LocalContacts.size() < 64)
                    LocalContacts.push_back(Contact);
                AddSeparationAxis(FeatureAxes, Contact.Normal, MaximumFeatureAxes);
            };
            if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Aabb)
            {
                AddContact(AabbCollisionContact(A.Bounds, B.Bounds), false);
            }
            else if (A.Shape == CollisionShape::Sphere && B.Shape == CollisionShape::Sphere)
            {
                for (const CollisionSphereSnapshot& SphereA : A.Spheres)
                    for (const CollisionSphereSnapshot& SphereB : B.Spheres)
                        if (AabbIntersect(SphereA.Bounds, SphereB.Bounds))
                        {
                            const Vec3 Difference = Vec3Sub(SphereA.Center, SphereB.Center);
                            const float DistanceSquared = CollisionLengthSquared(Difference);
                            const float Radius = SphereA.Radius + SphereB.Radius;
                            if (DistanceSquared > Radius * Radius)
                                continue;
                            CollisionContact Contact{};
                            Contact.Hit = true;
                            if (DistanceSquared > 1e-20f)
                            {
                                const float Distance = std::sqrt(DistanceSquared);
                                Contact.Normal = CollisionScale(Difference, 1.0f / Distance);
                                Contact.Depth = std::max(0.0f, Radius - Distance);
                            }
                            else
                            {
                                Contact.Normal = {1.0f, 0.0f, 0.0f};
                                Contact.Depth = Radius;
                            }
                            Contact.Correction = CollisionScale(Contact.Normal, Contact.Depth);
                            AddContact(Contact, false);
                        }
            }
            else if (A.Shape == CollisionShape::Sphere && B.Shape == CollisionShape::Aabb)
            {
                for (const CollisionSphereSnapshot& Sphere : A.Spheres)
                    AddContact(SphereAabbCollisionContact(Sphere.Center, Sphere.Radius, B.Bounds), false);
            }
            else if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Sphere)
            {
                for (const CollisionSphereSnapshot& Sphere : B.Spheres)
                    AddContact(SphereAabbCollisionContact(Sphere.Center, Sphere.Radius, A.Bounds), true);
            }
            else if (A.Shape == CollisionShape::Sphere && B.Shape == CollisionShape::Polygon)
            {
                for (const CollisionSphereSnapshot& Sphere : A.Spheres)
                    for (const CollisionMeshSnapshot& Mesh : B.Meshes)
                        for (const CollisionPrimitive& Primitive : Mesh.Primitives)
                        {
                            AddContact(SpherePrimitiveCollisionContact(Sphere.Center, Sphere.Radius, Primitive), false);
                        }
                AddPolygonNormals(FeatureAxes, B, MaximumFeatureAxes);
            }
            else if (A.Shape == CollisionShape::Polygon && B.Shape == CollisionShape::Sphere)
            {
                for (const CollisionSphereSnapshot& Sphere : B.Spheres)
                    for (const CollisionMeshSnapshot& Mesh : A.Meshes)
                        for (const CollisionPrimitive& Primitive : Mesh.Primitives)
                        {
                            AddContact(SpherePrimitiveCollisionContact(Sphere.Center, Sphere.Radius, Primitive), true);
                        }
                AddPolygonNormals(FeatureAxes, A, MaximumFeatureAxes);
            }
            else if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Polygon)
            {
                for (const CollisionMeshSnapshot& Mesh : B.Meshes)
                    for (const CollisionPrimitive& Primitive : Mesh.Primitives)
                    {
                        AddContact(AabbPrimitiveCollisionContact(A.Bounds, Primitive), false);
                    }
                AddPolygonNormals(FeatureAxes, B, MaximumFeatureAxes);
            }
            else if (A.Shape == CollisionShape::Polygon && B.Shape == CollisionShape::Aabb)
            {
                for (const CollisionMeshSnapshot& Mesh : A.Meshes)
                    for (const CollisionPrimitive& Primitive : Mesh.Primitives)
                    {
                        AddContact(AabbPrimitiveCollisionContact(B.Bounds, Primitive), true);
                    }
                AddPolygonNormals(FeatureAxes, A, MaximumFeatureAxes);
            }
            else if (A.Shape == CollisionShape::Polygon && B.Shape == CollisionShape::Polygon)
            {
                for (const CollisionMeshSnapshot& MeshA : A.Meshes)
                    for (const CollisionMeshSnapshot& MeshB : B.Meshes)
                    {
                        if (!AabbIntersect(MeshA.Bounds, MeshB.Bounds))
                            continue;
                        for (const CollisionPrimitive& PrimitiveA : MeshA.Primitives)
                            for (const CollisionPrimitive& PrimitiveB : MeshB.Primitives)
                            {
                                AddContact(PrimitiveCollisionContact(PrimitiveA, PrimitiveB), false);
                            }
                    }
                AddPolygonNormals(FeatureAxes, A, MaximumFeatureAxes);
                AddPolygonNormals(FeatureAxes, B, MaximumFeatureAxes);
            }

            constexpr std::size_t MaximumSearchAxes = 14;
            std::vector<Vec3> SearchAxes = FeatureAxes;
            const Vec3 CenterA{(A.Bounds.MinX + A.Bounds.MaxX) * 0.5f, (A.Bounds.MinY + A.Bounds.MaxY) * 0.5f,
                               (A.Bounds.MinZ + A.Bounds.MaxZ) * 0.5f};
            const Vec3 CenterB{(B.Bounds.MinX + B.Bounds.MaxX) * 0.5f, (B.Bounds.MinY + B.Bounds.MaxY) * 0.5f,
                               (B.Bounds.MinZ + B.Bounds.MaxZ) * 0.5f};
            AddSeparationAxis(SearchAxes, Vec3Sub(CenterA, CenterB), MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {1.0f, 0.0f, 0.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {0.0f, 1.0f, 0.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {0.0f, 0.0f, 1.0f}, MaximumSearchAxes);
            const std::size_t CombinationCount = std::min<std::size_t>(FeatureAxes.size(), 4);
            for (std::size_t First = 0; First < CombinationCount; ++First)
                for (std::size_t Second = First + 1; Second < CombinationCount; ++Second)
                {
                    AddSeparationAxis(SearchAxes, CollisionAdd(FeatureAxes[First], FeatureAxes[Second]),
                                      MaximumSearchAxes);
                    AddSeparationAxis(SearchAxes, Vec3Sub(FeatureAxes[First], FeatureAxes[Second]), MaximumSearchAxes);
                }
            AddSeparationAxis(SearchAxes, {1.0f, 1.0f, 0.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {1.0f, -1.0f, 0.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {1.0f, 0.0f, 1.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {1.0f, 0.0f, -1.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {0.0f, 1.0f, 1.0f}, MaximumSearchAxes);
            AddSeparationAxis(SearchAxes, {0.0f, 1.0f, -1.0f}, MaximumSearchAxes);
            return SearchAxes;
        }

        [[nodiscard]] CollisionContact FindSeparatingSnapshotContact(const CollisionSnapshot& A,
                                                                     const CollisionSnapshot& B)
        {
            if (!CollisionSnapshotsOverlap(A, B))
                return {};
            if (A.Shape == CollisionShape::Aabb && B.Shape == CollisionShape::Aabb)
                return AabbCollisionContact(A.Bounds, B.Bounds);

            CollisionContact Best{};
            const float DiagonalA = std::sqrt((A.Bounds.MaxX - A.Bounds.MinX) * (A.Bounds.MaxX - A.Bounds.MinX) +
                                              (A.Bounds.MaxY - A.Bounds.MinY) * (A.Bounds.MaxY - A.Bounds.MinY) +
                                              (A.Bounds.MaxZ - A.Bounds.MinZ) * (A.Bounds.MaxZ - A.Bounds.MinZ));
            const float DiagonalB = std::sqrt((B.Bounds.MaxX - B.Bounds.MinX) * (B.Bounds.MaxX - B.Bounds.MinX) +
                                              (B.Bounds.MaxY - B.Bounds.MinY) * (B.Bounds.MaxY - B.Bounds.MinY) +
                                              (B.Bounds.MaxZ - B.Bounds.MinZ) * (B.Bounds.MaxZ - B.Bounds.MinZ));
            const float Epsilon = std::max(1e-5f, std::max(DiagonalA, DiagonalB) * 1e-6f);
            std::vector<CollisionContact> LocalContacts;
            std::vector<Vec3> Axes = CollisionContactAxes(A, B, LocalContacts);
            std::vector<Vec3> LocalAxes;
            std::vector<CollisionContact> AdjustedLocalContacts;
            Vec3 AccumulatedCorrection{};
            AdjustedLocalContacts.reserve(LocalContacts.size());
            for (CollisionContact Contact : LocalContacts)
            {
                const float NormalLengthSquared = CollisionLengthSquared(Contact.Normal);
                if (NormalLengthSquared <= 1e-20f)
                    continue;
                Contact.Normal = CollisionScale(Contact.Normal, 1.0f / std::sqrt(NormalLengthSquared));
                Contact.Depth =
                    std::max(Contact.Depth, std::sqrt(CollisionLengthSquared(Contact.Correction))) + Epsilon;
                Contact.Correction = CollisionScale(Contact.Normal, Contact.Depth);
                AddSeparationAxis(LocalAxes, Contact.Normal, 2);
                AccumulatedCorrection = CollisionAdd(AccumulatedCorrection, Contact.Correction);
                AdjustedLocalContacts.push_back(Contact);
            }
            AddSeparationAxis(Axes, AccumulatedCorrection, 16);
            std::sort(AdjustedLocalContacts.begin(), AdjustedLocalContacts.end(),
                      [](const CollisionContact& Left, const CollisionContact& Right)
                      { return Left.Depth < Right.Depth; });
            for (const CollisionContact& Contact : AdjustedLocalContacts)
                if (!CollisionSnapshotsOverlap(A, B, Contact.Correction))
                {
                    Best = Contact;
                    break;
                }
            if (Best.Hit && LocalAxes.size() <= 1)
                return Best;

            for (const Vec3& Axis : Axes)
            {
                for (const float Sign : {-1.0f, 1.0f})
                {
                    const Vec3 Direction = CollisionScale(Axis, Sign);
                    float MinimumA = 0.0f;
                    float MaximumA = 0.0f;
                    float MinimumB = 0.0f;
                    float MaximumB = 0.0f;
                    ProjectAabb(A.Bounds, Direction, MinimumA, MaximumA);
                    ProjectAabb(B.Bounds, Direction, MinimumB, MaximumB);
                    float Low = 0.0f;
                    float High = std::max(0.0f, MaximumB - MinimumA) + Epsilon;
                    Vec3 Correction = CollisionScale(Direction, High);
                    for (int Expansion = 0; Expansion < 4 && CollisionSnapshotsOverlap(A, B, Correction); ++Expansion)
                    {
                        High = std::max(High * 2.0f, Epsilon);
                        Correction = CollisionScale(Direction, High);
                    }
                    if (CollisionSnapshotsOverlap(A, B, Correction))
                        continue;
                    for (int Iteration = 0; Iteration < 18; ++Iteration)
                    {
                        const float Middle = (Low + High) * 0.5f;
                        if (CollisionSnapshotsOverlap(A, B, CollisionScale(Direction, Middle)))
                            Low = Middle;
                        else
                            High = Middle;
                    }
                    Correction = CollisionScale(Direction, High);
                    if (CollisionSnapshotsOverlap(A, B, Correction))
                        continue;
                    if (!Best.Hit || High < Best.Depth)
                    {
                        Best.Hit = true;
                        Best.Normal = Direction;
                        Best.Depth = High;
                        Best.Correction = Correction;
                    }
                }
            }
            return Best;
        }

        [[nodiscard]] CollisionContact CollisionSnapshotContact(const CollisionSnapshot& A, const CollisionSnapshot& B)
        {
            return FindSeparatingSnapshotContact(A, B);
        }

        // DBO block codes from the MIT DarkBASIC Professional source (Dark-Basic-Pro repository,
        // see docs/NOTICE.md).
        constexpr uint32_t DboRootFrame = 1;
        constexpr uint32_t DboRootAnimationSet = 2;
        constexpr uint32_t DboFrameName = 101;
        constexpr uint32_t DboFrameMatrix = 102;
        constexpr uint32_t DboFrameMesh = 103;
        constexpr uint32_t DboFrameChild = 104;
        constexpr uint32_t DboFrameSibling = 105;
        constexpr uint32_t DboFrameOffset = 106;
        constexpr uint32_t DboFrameRotation = 107;
        constexpr uint32_t DboFrameScale = 108;
        constexpr uint32_t DboMeshFvf = 111;
        constexpr uint32_t DboMeshFvfSize = 112;
        constexpr uint32_t DboMeshVertexCount = 113;
        constexpr uint32_t DboMeshIndexCount = 114;
        constexpr uint32_t DboMeshVertexData = 115;
        constexpr uint32_t DboMeshIndexData = 116;
        constexpr uint32_t DboMeshPrimitiveType = 117;
        constexpr uint32_t DboMeshDrawVertexCount = 118;
        constexpr uint32_t DboMeshDrawPrimitiveCount = 119;
        constexpr uint32_t DboMeshVisible = 140;
        constexpr uint32_t DboObjectCustomData = 406;

        class DboBuffer
        {
          public:
            void AppendU32(uint32_t Value)
            {
                if (!Status())
                    return;
                const auto* Bytes = reinterpret_cast<const uint8_t*>(&Value);
                Data_.insert(Data_.end(), Bytes, Bytes + sizeof(Value));
            }

            void AppendU16(uint16_t Value)
            {
                if (!Status())
                    return;
                const auto* Bytes = reinterpret_cast<const uint8_t*>(&Value);
                Data_.insert(Data_.end(), Bytes, Bytes + sizeof(Value));
            }

            void AppendFloat(float Value)
            {
                if (!Status())
                    return;
                if (!std::isfinite(Value))
                {
                    Fail("SAVE OBJECT DBO contains a non-finite value");
                    return;
                }
                const auto* Bytes = reinterpret_cast<const uint8_t*>(&Value);
                Data_.insert(Data_.end(), Bytes, Bytes + sizeof(Value));
            }

            void AppendBytes(const void* Source, std::size_t Size)
            {
                if (!Status())
                    return;
                const auto* Bytes = static_cast<const uint8_t*>(Source);
                Data_.insert(Data_.end(), Bytes, Bytes + Size);
            }

            void AppendString(const std::string& Value)
            {
                if (!Status())
                    return;
                if (Value.size() > std::numeric_limits<uint32_t>::max())
                {
                    Fail("SAVE OBJECT DBO string is too large");
                    return;
                }
                AppendU32(static_cast<uint32_t>(Value.size()));
                AppendBytes(Value.data(), Value.size());
            }

            void AppendBlock(uint32_t Code, const DboBuffer& Payload)
            {
                if (!Status())
                    return;
                if (!Payload.Status(&Error_))
                    return;
                if (Payload.Data_.size() > std::numeric_limits<uint32_t>::max())
                {
                    Fail("SAVE OBJECT DBO block is too large");
                    return;
                }
                AppendU32(Code);
                AppendU32(static_cast<uint32_t>(Payload.Data_.size()));
                Data_.insert(Data_.end(), Payload.Data_.begin(), Payload.Data_.end());
            }

            void AppendU32Block(uint32_t Code, uint32_t Value)
            {
                DboBuffer Payload;
                Payload.AppendU32(Value);
                AppendBlock(Code, Payload);
            }

            void AppendEnd()
            {
                AppendU32(0);
                AppendU32(0);
            }

            [[nodiscard]] const std::vector<uint8_t>& Data() const
            {
                return Data_;
            }

            [[nodiscard]] bool Status(std::string* Error = nullptr) const
            {
                if (Error_.empty())
                    return true;
                if (Error)
                    *Error = Error_;
                return false;
            }

          private:
            void Fail(std::string Message)
            {
                if (Error_.empty())
                    Error_ = std::move(Message);
            }

            std::string Error_;

            std::vector<uint8_t> Data_;
        };

        class DboReader
        {
          public:
            explicit DboReader(const std::vector<uint8_t>& Data) : Data_(Data), End_(Data.size()) {}

            DboReader(const std::vector<uint8_t>& Data, std::size_t Begin, std::size_t End)
                : Data_(Data), Position_(Begin), End_(End)
            {
                if (Begin > End || End > Data.size())
                {
                    Fail("LOAD OBJECT found an invalid DBO block range");
                    Position_ = 0;
                    End_ = 0;
                }
            }

            [[nodiscard]] uint32_t ReadU32()
            {
                if (!Require(4))
                    return {};
                uint32_t Value = 0;
                std::memcpy(&Value, Data_.data() + Position_, 4);
                Position_ += 4;
                return Value;
            }

            [[nodiscard]] uint16_t ReadU16()
            {
                if (!Require(2))
                    return {};
                uint16_t Value = 0;
                std::memcpy(&Value, Data_.data() + Position_, 2);
                Position_ += 2;
                return Value;
            }

            [[nodiscard]] float ReadFloat()
            {
                if (!Require(4))
                    return {};
                float Value = 0.0f;
                std::memcpy(&Value, Data_.data() + Position_, 4);
                Position_ += 4;
                if (!std::isfinite(Value))
                {
                    Fail("LOAD OBJECT DBO contains a non-finite value");
                    return {};
                }
                return Value;
            }

            [[nodiscard]] std::string ReadString()
            {
                const uint32_t Size = ReadU32();
                if (!Require(Size))
                    return {};
                std::string Value(reinterpret_cast<const char*>(Data_.data() + Position_), Size);
                Position_ += Size;
                return Value;
            }

            [[nodiscard]] std::vector<uint8_t> ReadBytes(std::size_t Size)
            {
                if (!Require(Size))
                    return {};
                std::vector<uint8_t> Value(Data_.begin() + static_cast<std::ptrdiff_t>(Position_),
                                           Data_.begin() + static_cast<std::ptrdiff_t>(Position_ + Size));
                Position_ += Size;
                return Value;
            }

            void Seek(std::size_t Position)
            {
                if (Position > End_)
                {
                    Fail("LOAD OBJECT read past a DBO block");
                    Position_ = End_;
                    return;
                }
                Position_ = Position;
            }

            [[nodiscard]] std::size_t Position() const
            {
                return Position_;
            }

            [[nodiscard]] std::size_t End() const
            {
                return End_;
            }

            [[nodiscard]] const std::vector<uint8_t>& Data() const
            {
                return Data_;
            }

            [[nodiscard]] bool Status(std::string* Error = nullptr) const
            {
                if (Error_.empty())
                    return true;
                if (Error)
                    *Error = Error_;
                return false;
            }

          private:
            void Fail(std::string Message)
            {
                if (Error_.empty())
                    Error_ = std::move(Message);
            }

            std::string Error_;

            bool Require(std::size_t Size)
            {
                if (!Status())
                    return false;
                if (Position_ > End_ || Size > End_ - Position_)
                {
                    Fail("LOAD OBJECT found truncated DBO data");
                    Position_ = End_;
                    return false;
                }
                return true;
            }

            const std::vector<uint8_t>& Data_;
            std::size_t Position_ = 0;
            std::size_t End_ = 0;
        };

        [[nodiscard]] DboBuffer DboFloatPayload(std::initializer_list<float> Values)
        {
            DboBuffer Result;
            for (float Value : Values)
                Result.AppendFloat(Value);
            return Result;
        }

        bool EncodeDboMesh(DboBuffer& Output, const Mesh& Value, std::string* Error = nullptr)
        {
            constexpr uint32_t Fvf = 0x0152u;
            constexpr uint32_t Stride = 36u;
            DboBuffer Result;
            if (Value.Geometry().Vertices.size() > std::numeric_limits<uint32_t>::max())
            {
                if (Error)
                    *Error = "SAVE OBJECT mesh has too many vertices";
                return false;
            }
            bool UseIndices = !Value.Geometry().Indices.empty();
            for (uint32_t Index : Value.Geometry().Indices)
            {
                if (Index >= Value.Geometry().Vertices.size())
                {
                    if (Error)
                        *Error = "SAVE OBJECT found an invalid mesh index";
                    return false;
                }
                UseIndices = UseIndices && Index <= std::numeric_limits<uint16_t>::max();
            }

            std::vector<uint32_t> VertexOrder;
            if (!UseIndices && !Value.Geometry().Indices.empty())
                VertexOrder = Value.Geometry().Indices;
            else
            {
                VertexOrder.reserve(Value.Geometry().Vertices.size());
                for (std::size_t Index = 0; Index < Value.Geometry().Vertices.size(); ++Index)
                    VertexOrder.push_back(static_cast<uint32_t>(Index));
            }
            if (VertexOrder.size() > std::numeric_limits<uint32_t>::max())
            {
                if (Error)
                    *Error = "SAVE OBJECT expanded mesh has too many vertices";
                return false;
            }

            Result.AppendU32Block(DboMeshFvf, Fvf);
            Result.AppendU32Block(DboMeshFvfSize, Stride);
            Result.AppendU32Block(DboMeshVertexCount, static_cast<uint32_t>(VertexOrder.size()));
            Result.AppendU32Block(DboMeshIndexCount,
                                  UseIndices ? static_cast<uint32_t>(Value.Geometry().Indices.size()) : 0u);

            DboBuffer VertexData;
            for (uint32_t Index : VertexOrder)
            {
                VertexData.AppendBytes(&Value.Geometry().Vertices[Index], sizeof(Vec3));
                const Vec3 Normal = Index < Value.Geometry().Normals.size() ? Value.Geometry().Normals[Index] : Vec3{};
                VertexData.AppendBytes(&Normal, sizeof(Vec3));
                const uint32_t Diffuse =
                    Index < Value.Geometry().DiffuseColors.size() ? Value.Geometry().DiffuseColors[Index] : 0xFFFFFFFFu;
                VertexData.AppendU32(Diffuse);
                const SDL_FPoint Uv = Index < Value.Geometry().Uvs.size() ? Value.Geometry().Uvs[Index] : SDL_FPoint{};
                VertexData.AppendBytes(&Uv, sizeof(SDL_FPoint));
            }
            Result.AppendBlock(DboMeshVertexData, VertexData);
            if (UseIndices)
            {
                DboBuffer IndexData;
                for (uint32_t Index : Value.Geometry().Indices)
                    IndexData.AppendU16(static_cast<uint16_t>(Index));
                Result.AppendBlock(DboMeshIndexData, IndexData);
            }
            Result.AppendU32Block(DboMeshPrimitiveType, 4u);
            Result.AppendU32Block(DboMeshDrawVertexCount, static_cast<uint32_t>(VertexOrder.size()));
            const std::size_t ElementCount = UseIndices ? Value.Geometry().Indices.size() : VertexOrder.size();
            Result.AppendU32Block(DboMeshDrawPrimitiveCount, static_cast<uint32_t>(ElementCount / 3u));
            DboBuffer Visible;
            const uint8_t Flag = Value.Visible ? 1u : 0u;
            Visible.AppendBytes(&Flag, 1);
            Result.AppendBlock(DboMeshVisible, Visible);
            Result.AppendEnd();
            {
                if (!Result.Status(Error))
                    return false;
                Output = std::move(Result);
                return true;
            }
        }

        bool EncodeDboFrame(DboBuffer& Output, const Object3D& Object, std::size_t MeshIndex,
                            std::string* Error = nullptr)
        {
            assert(MeshIndex < Object.Meshes.size());
            DboBuffer Sibling;
            Sibling.AppendEnd();
            // Build the sibling chain from its tail without consuming the C++ call stack.
            for (std::size_t Index = Object.Meshes.size(); Index-- > MeshIndex;)
            {
                DboBuffer Result;
                DboBuffer Name;
                Name.AppendString("OpenDBC Limb " + std::to_string(Index));
                Result.AppendBlock(DboFrameName, Name);

                const aiMatrix4x4& Matrix = Object.Meshes[Index].Geometry().BindTransform;
                DboBuffer MatrixPayload = DboFloatPayload(
                    {Matrix.a1, Matrix.a2, Matrix.a3, Matrix.a4, Matrix.b1, Matrix.b2, Matrix.b3, Matrix.b4, Matrix.c1,
                     Matrix.c2, Matrix.c3, Matrix.c4, Matrix.d1, Matrix.d2, Matrix.d3, Matrix.d4});
                Result.AppendBlock(DboFrameMatrix, MatrixPayload);
                DboBuffer MeshPayload;
                if (!EncodeDboMesh(MeshPayload, Object.Meshes[Index], Error))
                    return false;
                Result.AppendBlock(DboFrameMesh, MeshPayload);
                DboBuffer EmptyFrame;
                EmptyFrame.AppendEnd();
                Result.AppendBlock(DboFrameChild, EmptyFrame);
                Result.AppendBlock(DboFrameSibling, Sibling);
                Result.AppendBlock(DboFrameOffset, DboFloatPayload({0.0f, 0.0f, 0.0f}));
                Result.AppendBlock(DboFrameRotation, DboFloatPayload({0.0f, 0.0f, 0.0f}));
                Result.AppendBlock(DboFrameScale, DboFloatPayload({1.0f, 1.0f, 1.0f}));
                Result.AppendEnd();

                if (!Result.Status(Error))
                    return false;
                Sibling = std::move(Result);
            }
            Output = std::move(Sibling);
            return true;
        }

        bool EncodeDboObject(std::vector<uint8_t>& Output, const Object3D& Object, std::string* Error = nullptr)
        {
            if (Object.Meshes.empty())
            {
                if (Error)
                    *Error = "SAVE OBJECT requires object geometry";
                return false;
            }
            DboBuffer Result;
            Result.AppendString("MAGICDBO");
            Result.AppendU32(1062u);
            Result.AppendU32(0u);
            Result.AppendU32(0u);
            DboBuffer RootFrame;
            if (!EncodeDboFrame(RootFrame, Object, 0, Error))
                return false;
            Result.AppendBlock(DboRootFrame, RootFrame);
            DboBuffer AnimationSet;
            AnimationSet.AppendEnd();
            Result.AppendBlock(DboRootAnimationSet, AnimationSet);
            DboBuffer CustomData;
            CustomData.AppendU32(0u);
            Result.AppendBlock(DboObjectCustomData, CustomData);
            Result.AppendEnd();
            {
                if (!Result.Status(Error))
                    return false;
                Output = Result.Data();
                return true;
            }
        }

        struct DboMeshFields
        {
            uint32_t Fvf = 0;
            uint32_t Stride = 0;
            uint32_t VertexCount = 0;
            uint32_t IndexCount = 0;
            std::vector<uint8_t> VertexData;
            std::vector<uint8_t> IndexData;
            bool Visible = true;
        };

        bool DecodeDboMesh(Mesh& Output, const DboMeshFields& Fields, const aiMatrix4x4& Transform,
                           std::string* Error = nullptr)
        {
            if ((Fields.Fvf & 0x400Eu) != 0x0002u)
            {
                if (Error)
                    *Error = "LOAD OBJECT supports DBO FVF XYZ vertex positions";
                return false;
            }
            if (Fields.Stride < 12 || Fields.VertexCount > Fields.VertexData.size() / Fields.Stride ||
                static_cast<std::size_t>(Fields.VertexCount) * Fields.Stride != Fields.VertexData.size())
            {
                if (Error)
                    *Error = "LOAD OBJECT found invalid DBO vertex data";
                return false;
            }

            std::size_t Offset = 12;
            const std::size_t NormalOffset = (Fields.Fvf & 0x0010u) != 0 ? Offset : 0;
            if (NormalOffset != 0)
                Offset += 12;
            if ((Fields.Fvf & 0x0020u) != 0)
                Offset += 4;
            const std::size_t DiffuseOffset = (Fields.Fvf & 0x0040u) != 0 ? Offset : 0;
            if (DiffuseOffset != 0)
                Offset += 4;
            if ((Fields.Fvf & 0x0080u) != 0)
                Offset += 4;
            const uint32_t TextureCount = (Fields.Fvf >> 8u) & 0xFu;
            const std::size_t UvOffset = TextureCount > 0 ? Offset : 0;
            if (UvOffset != 0)
            {
                const uint32_t DimensionCode = (Fields.Fvf >> 16u) & 0x3u;
                static constexpr std::array<std::size_t, 4> Dimensions{2, 3, 4, 1};
                Offset += Dimensions[DimensionCode] * 4u;
            }
            if (Offset > Fields.Stride)
            {
                if (Error)
                    *Error = "LOAD OBJECT found DBO FVF fields beyond the vertex stride";
                return false;
            }

            Mesh Result;
            MeshGeometry& Geometry = Result.EditGeometry();
            Geometry.BindTransform = Transform;
            Result.Visible = Fields.Visible;
            Geometry.Vertices.reserve(Fields.VertexCount);
            Geometry.Normals.reserve(Fields.VertexCount);
            Geometry.Uvs.reserve(Fields.VertexCount);
            Geometry.DiffuseColors.reserve(Fields.VertexCount);
            for (uint32_t Index = 0; Index < Fields.VertexCount; ++Index)
            {
                const uint8_t* Vertex = Fields.VertexData.data() + static_cast<std::size_t>(Index) * Fields.Stride;
                Vec3 Position{};
                std::memcpy(&Position, Vertex, sizeof(Position));
                Geometry.Vertices.push_back(Position);
                Vec3 Normal{};
                if (NormalOffset != 0)
                    std::memcpy(&Normal, Vertex + NormalOffset, sizeof(Normal));
                Geometry.Normals.push_back(Normal);
                uint32_t Diffuse = 0xFFFFFFFFu;
                if (DiffuseOffset != 0)
                    std::memcpy(&Diffuse, Vertex + DiffuseOffset, 4);
                Geometry.DiffuseColors.push_back(Diffuse);
                SDL_FPoint Uv{};
                if (UvOffset != 0 && UvOffset + sizeof(Uv) <= Fields.Stride)
                    std::memcpy(&Uv, Vertex + UvOffset, sizeof(Uv));
                if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) ||
                    !std::isfinite(Normal.X) || !std::isfinite(Normal.Y) || !std::isfinite(Normal.Z) ||
                    !std::isfinite(Uv.x) || !std::isfinite(Uv.y))
                {
                    if (Error)
                        *Error = "LOAD OBJECT DBO contains non-finite vertex data";
                    return false;
                }
                Geometry.Uvs.push_back(Uv);
            }
            if (Fields.IndexCount > 0)
            {
                if (Fields.IndexData.size() != static_cast<std::size_t>(Fields.IndexCount) * sizeof(uint16_t))
                {
                    if (Error)
                        *Error = "LOAD OBJECT found invalid DBO index data";
                    return false;
                }
                DboReader Indices(Fields.IndexData);
                for (uint32_t Index = 0; Index < Fields.IndexCount; ++Index)
                {
                    const uint32_t Value = Indices.ReadU16();
                    if (Value >= Fields.VertexCount)
                    {
                        if (Error)
                            *Error = "LOAD OBJECT found a DBO index outside the vertex data";
                        return false;
                    }
                    Geometry.Indices.push_back(Value);
                }
            }
            else
            {
                for (uint32_t Index = 0; Index < Fields.VertexCount; ++Index)
                    Geometry.Indices.push_back(Index);
            }
            {
                Output = std::move(Result);
                return true;
            }
        }

        bool DecodeDboMeshBlocks(DboReader& Reader, const aiMatrix4x4& Transform, std::vector<Mesh>& Meshes,
                                 std::string* Error = nullptr)
        {
            DboMeshFields Fields;
            while (Reader.Position() + 8 <= Reader.End())
            {
                const uint32_t Code = Reader.ReadU32();
                const uint32_t DeclaredSize = Reader.ReadU32();
                if (Code == 0)
                    break;
                std::size_t Size = DeclaredSize;
                if (Code == DboMeshVertexData && Size == 0)
                    Size = static_cast<std::size_t>(Fields.Stride) * Fields.VertexCount;
                else if (Code == DboMeshIndexData && Size == 0)
                    Size = static_cast<std::size_t>(Fields.IndexCount) * sizeof(uint16_t);
                else if (Code == DboMeshPrimitiveType || Code == DboMeshDrawVertexCount ||
                         Code == DboMeshDrawPrimitiveCount)
                    Size = 4;
                const std::size_t End = Reader.Position() + Size;
                if (End < Reader.Position() || End > Reader.End())
                {
                    if (Error)
                        *Error = "LOAD OBJECT found invalid DBO mesh block " + std::to_string(Code) + " at byte " +
                                 std::to_string(Reader.Position()) + " with size " + std::to_string(Size);
                    return false;
                }
                DboReader Payload(Reader.Data(), Reader.Position(), End);
                switch (Code)
                {
                    case DboMeshFvf:
                        Fields.Fvf = Payload.ReadU32();
                        break;
                    case DboMeshFvfSize:
                        Fields.Stride = Payload.ReadU32();
                        break;
                    case DboMeshVertexCount:
                        Fields.VertexCount = Payload.ReadU32();
                        break;
                    case DboMeshIndexCount:
                        Fields.IndexCount = Payload.ReadU32();
                        break;
                    case DboMeshVertexData:
                        Fields.VertexData = Payload.ReadBytes(Size);
                        break;
                    case DboMeshIndexData:
                        Fields.IndexData = Payload.ReadBytes(Size);
                        break;
                    case DboMeshVisible:
                        Fields.Visible = Size == 0 || Payload.ReadBytes(1).front() != 0;
                        break;
                    default:
                        break;
                }
                if (!Payload.Status(Error))
                    return false;
                Reader.Seek(End);
            }
            if (Fields.VertexCount > 0)
            {
                Mesh Value;
                if (!DecodeDboMesh(Value, Fields, Transform, Error))
                    return false;
                Meshes.push_back(std::move(Value));
            }
            if (!Reader.Status(Error))
                return false;
            return true;
        }

        bool DecodeDboFrame(DboReader& Reader, const aiMatrix4x4& Parent, std::vector<Mesh>& Meshes,
                            std::string* Error = nullptr)
        {
            struct PendingFrame
            {
                DboReader Reader;
                aiMatrix4x4 Parent;
                aiMatrix4x4 World;
            };

            // A file controls its frame depth. Keep traversal state on the heap, preserving depth-first order.
            std::vector<PendingFrame> Pending;
            Pending.push_back({Reader, Parent, Parent});
            while (!Pending.empty())
            {
                auto& Frame = Pending.back();
                auto& Cursor = Frame.Reader;
                if (!Cursor.Status(Error))
                    return false;
                if (Cursor.End() - Cursor.Position() < 8)
                {
                    if (Cursor.Position() != Cursor.End())
                    {
                        if (Error)
                            *Error = "LOAD OBJECT found truncated DBO frame data";
                        return false;
                    }
                    Pending.pop_back();
                    continue;
                }
                const uint32_t Code = Cursor.ReadU32();
                const uint32_t Size = Cursor.ReadU32();
                if (Code == 0)
                {
                    Pending.pop_back();
                    continue;
                }
                if (Size > Cursor.End() - Cursor.Position())
                {
                    if (Error)
                        *Error = "LOAD OBJECT found an invalid DBO frame block";
                    return false;
                }
                const std::size_t End = Cursor.Position() + Size;
                DboReader Payload(Cursor.Data(), Cursor.Position(), End);
                Cursor.Seek(End);
                if (Code == DboFrameMatrix)
                {
                    aiMatrix4x4 Local;
                    Local.a1 = Payload.ReadFloat();
                    Local.a2 = Payload.ReadFloat();
                    Local.a3 = Payload.ReadFloat();
                    Local.a4 = Payload.ReadFloat();
                    Local.b1 = Payload.ReadFloat();
                    Local.b2 = Payload.ReadFloat();
                    Local.b3 = Payload.ReadFloat();
                    Local.b4 = Payload.ReadFloat();
                    Local.c1 = Payload.ReadFloat();
                    Local.c2 = Payload.ReadFloat();
                    Local.c3 = Payload.ReadFloat();
                    Local.c4 = Payload.ReadFloat();
                    Local.d1 = Payload.ReadFloat();
                    Local.d2 = Payload.ReadFloat();
                    Local.d3 = Payload.ReadFloat();
                    Local.d4 = Payload.ReadFloat();

                    if (!Payload.Status(Error))
                        return false;
                    Frame.World = Frame.Parent * Local;
                }
                else if (Code == DboFrameMesh)
                {
                    if (!DecodeDboMeshBlocks(Payload, Frame.World, Meshes, Error))
                        return false;
                }
                else if (Code == DboFrameChild || Code == DboFrameSibling)
                {
                    const aiMatrix4x4 Transform = Code == DboFrameChild ? Frame.World : Frame.Parent;
                    Pending.push_back({std::move(Payload), Transform, Transform});
                }
            }
            Reader.Seek(Reader.End());
            return true;
        }

        bool DecodeDboObject(Object3D& Output, const std::vector<uint8_t>& Data, std::string* Error = nullptr)
        {
            DboReader Reader(Data);
            if (Reader.ReadString() != "MAGICDBO")
            {
                if (Error)
                    *Error = "LOAD OBJECT file is not a DBO object";
                return false;
            }
            const uint32_t Version = Reader.ReadU32();
            (void)Reader.ReadU32();
            (void)Reader.ReadU32();
            if (Version == 0 || Version > 1062)
            {
                if (Error)
                    *Error = "LOAD OBJECT DBO version is unsupported";
                return false;
            }

            Object3D Result;
            Result.Visible = true;
            while (Reader.Position() + 8 <= Reader.End())
            {
                const uint32_t Code = Reader.ReadU32();
                const uint32_t Size = Reader.ReadU32();
                if (Code == 0)
                    break;
                const std::size_t End = Reader.Position() + Size;
                if (End < Reader.Position() || End > Reader.End())
                {
                    if (Error)
                        *Error = "LOAD OBJECT found an invalid root DBO block";
                    return false;
                }
                if (Code == DboRootFrame)
                {
                    DboReader Payload(Reader.Data(), Reader.Position(), End);
                    if (!DecodeDboFrame(Payload, aiMatrix4x4{}, Result.Meshes, Error))
                        return false;
                }
                Reader.Seek(End);
            }
            if (Result.Meshes.empty())
            {
                if (Error)
                    *Error = "LOAD OBJECT DBO contains no triangle geometry";
                return false;
            }
            Result.LimbTransforms.resize(Result.Meshes.size());
            for (std::size_t Index = 0; Index < Result.Meshes.size(); ++Index)
                Result.Meshes[Index].SourceMeshIndex = static_cast<int>(Index);
            {
                if (!Reader.Status(Error))
                    return false;
                Output = std::move(Result);
                return true;
            }
        }

        constexpr std::size_t PortableFileLimit = 256u * 1024u * 1024u;
        constexpr uint32_t PortableObjectLimit = 65535u;
        constexpr uint32_t PortableMeshLimit = 4096u;
        constexpr uint32_t PortableVertexLimit = 2u * 1024u * 1024u;
        constexpr uint32_t PortableIndexLimit = 6u * 1024u * 1024u;
        constexpr uint32_t PortableKeyframeLimit = 100000u;
        constexpr uint32_t PortableLimbLimit = 65535u;
        constexpr uint32_t PortableStringLimit = 1024u * 1024u;

        class PortableWriter
        {
          public:
            void U8(uint8_t Value)
            {
                if (!Require(1))
                    return;
                Data_.push_back(Value);
            }

            void U32(uint32_t Value)
            {
                if (!Require(4))
                    return;
                for (unsigned Shift = 0; Shift < 32; Shift += 8)
                    Data_.push_back(static_cast<uint8_t>((Value >> Shift) & 0xFFu));
            }

            void I32(int32_t Value)
            {
                U32(static_cast<uint32_t>(Value));
            }

            void Float(float Value)
            {
                if (!std::isfinite(Value))
                {
                    Fail("portable 3D data contains a non-finite number");
                    return;
                }
                U32(std::bit_cast<uint32_t>(Value));
            }

            void Bytes(const void* Source, std::size_t Size)
            {
                if (!Require(Size))
                    return;
                const auto* First = static_cast<const uint8_t*>(Source);
                Data_.insert(Data_.end(), First, First + Size);
            }

            void String(const std::string& Value)
            {
                if (Value.size() > PortableStringLimit)
                {
                    Fail("portable 3D string exceeds the 1 MiB limit");
                    return;
                }
                U32(static_cast<uint32_t>(Value.size()));
                Bytes(Value.data(), Value.size());
            }

            [[nodiscard]] const std::vector<uint8_t>& Data() const
            {
                return Data_;
            }

            [[nodiscard]] bool Status(std::string* Error = nullptr) const
            {
                if (Error_.empty())
                    return true;
                if (Error)
                    *Error = Error_;
                return false;
            }

          private:
            void Fail(std::string Message)
            {
                if (Error_.empty())
                    Error_ = std::move(Message);
            }

            std::string Error_;

            bool Require(std::size_t Size)
            {
                if (!Status())
                    return false;
                if (Size > PortableFileLimit || Data_.size() > PortableFileLimit - Size)
                {
                    Fail("portable 3D data exceeds the 256 MiB limit");
                    return false;
                }
                return true;
            }

            std::vector<uint8_t> Data_;
        };

        class PortableReader
        {
          public:
            PortableReader(const std::vector<uint8_t>& Data, std::string Command)
                : Data_(Data), Command_(std::move(Command))
            {
            }

            uint8_t U8()
            {
                if (!Require(1))
                    return {};
                return Data_[Position_++];
            }

            uint32_t U32()
            {
                if (!Require(4))
                    return {};
                uint32_t Value = 0;
                for (unsigned Shift = 0; Shift < 32; Shift += 8)
                    Value |= static_cast<uint32_t>(Data_[Position_++]) << Shift;
                return Value;
            }

            int32_t I32()
            {
                return static_cast<int32_t>(U32());
            }

            float Float()
            {
                const float Value = std::bit_cast<float>(U32());
                if (!std::isfinite(Value))
                {
                    Fail(Command_ + " found a non-finite number");
                    return {};
                }
                return Value;
            }

            std::string String()
            {
                const uint32_t Size = U32();
                if (Size > PortableStringLimit)
                {
                    Fail(Command_ + " found an oversized string");
                    return {};
                }
                if (!Require(Size))
                    return {};
                std::string Value(reinterpret_cast<const char*>(Data_.data() + Position_), Size);
                Position_ += Size;
                return Value;
            }

            void Expect(const std::array<char, 8>& Magic)
            {
                if (!Require(Magic.size()))
                    return;
                if (!std::equal(Magic.begin(), Magic.end(), Data_.begin() + static_cast<std::ptrdiff_t>(Position_)))
                {
                    Fail(Command_ + " found an invalid file signature");
                    return;
                }
                Position_ += Magic.size();
            }

            [[nodiscard]] bool AtEnd() const
            {
                return Status() && Position_ == Data_.size();
            }

            [[nodiscard]] bool Status(std::string* Error = nullptr) const
            {
                if (Error_.empty())
                    return true;
                if (Error)
                    *Error = Error_;
                return false;
            }

          private:
            void Fail(std::string Message)
            {
                if (Error_.empty())
                    Error_ = std::move(Message);
            }

            std::string Error_;

            bool Require(std::size_t Size)
            {
                if (!Status())
                    return false;
                if (Position_ > Data_.size() || Size > Data_.size() - Position_)
                {
                    Fail(Command_ + " found truncated data");
                    Position_ = Data_.size();
                    return false;
                }
                return true;
            }

            const std::vector<uint8_t>& Data_;
            std::string Command_;
            std::size_t Position_ = 0;
        };

        bool ReadPortableFile(std::vector<uint8_t>& Output, const std::string& Path, const char* Command,
                              std::string* Error = nullptr)
        {
            std::ifstream Input(dbc::PathFromUtf8(Path), std::ios::binary | std::ios::ate);
            if (!Input)
            {
                if (Error)
                    *Error = std::string(Command) + " cannot open '" + Path + "'";
                return false;
            }
            const std::streamoff Size = Input.tellg();
            if (Size < 0 || static_cast<uint64_t>(Size) > PortableFileLimit)
            {
                if (Error)
                    *Error = std::string(Command) + " file exceeds the 256 MiB limit";
                return false;
            }
            Input.seekg(0);
            std::vector<uint8_t> Data(static_cast<std::size_t>(Size));
            if (!Data.empty())
                Input.read(reinterpret_cast<char*>(Data.data()), Size);
            if (!Input && !Data.empty())
            {
                if (Error)
                    *Error = std::string(Command) + " failed while reading '" + Path + "'";
                return false;
            }
            {
                Output = std::move(Data);
                return true;
            }
        }

        bool WritePortableFile(const std::string& Path, const std::vector<uint8_t>& Data, const char* Command,
                               std::string* Error = nullptr)
        {
            if (Data.size() > PortableFileLimit)
            {
                if (Error)
                    *Error = std::string(Command) + " data exceeds the 256 MiB limit";
                return false;
            }
            std::ofstream Output(dbc::PathFromUtf8(Path), std::ios::binary | std::ios::trunc);
            if (!Output)
            {
                if (Error)
                    *Error = std::string(Command) + " cannot create '" + Path + "'";
                return false;
            }
            Output.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
            Output.close();
            if (!Output)
            {
                if (Error)
                    *Error = std::string(Command) + " failed while writing '" + Path + "'";
                return false;
            }
            return true;
        }

        bool WriteLimbTransform(PortableWriter& Writer, const LimbTransform& Value, std::string* Error = nullptr)
        {
            const std::array<float, 9> Components{Value.RotX, Value.RotY,   Value.RotZ,   Value.OffX,  Value.OffY,
                                                  Value.OffZ, Value.ScaleX, Value.ScaleY, Value.ScaleZ};
            if (!std::all_of(Components.begin(), Components.end(),
                             [](float Component) { return std::isfinite(Component); }))
            {
                if (Error)
                    *Error = "SAVE OBJECT ANIMATION found a non-finite limb transform";
                return false;
            }
            Writer.Float(Value.RotX);
            Writer.Float(Value.RotY);
            Writer.Float(Value.RotZ);
            Writer.Float(Value.OffX);
            Writer.Float(Value.OffY);
            Writer.Float(Value.OffZ);
            Writer.Float(Value.ScaleX);
            Writer.Float(Value.ScaleY);
            Writer.Float(Value.ScaleZ);
            Writer.U8(Value.Hidden ? 1u : 0u);
            Writer.I32(Value.ParentLimb);
            if (!Writer.Status(Error))
                return false;
            return true;
        }

        [[nodiscard]] LimbTransform ReadLimbTransform(PortableReader& Reader)
        {
            LimbTransform Value;
            Value.RotX = Reader.Float();
            Value.RotY = Reader.Float();
            Value.RotZ = Reader.Float();
            Value.OffX = Reader.Float();
            Value.OffY = Reader.Float();
            Value.OffZ = Reader.Float();
            Value.ScaleX = Reader.Float();
            Value.ScaleY = Reader.Float();
            Value.ScaleZ = Reader.Float();
            Value.Hidden = Reader.U8() != 0;
            Value.ParentLimb = Reader.I32();
            return Value;
        }

        bool AnimationFileSize(std::size_t& Output, std::size_t LimbCount, std::size_t KeyframeCount,
                               const char* Command, std::string* Error = nullptr)
        {
            constexpr uint64_t HeaderSize = 8u + 3u * 4u;
            constexpr uint64_t LimbRecordSize = 9u * 4u + 1u + 4u;
            const uint64_t FrameSize = 4u + static_cast<uint64_t>(LimbCount) * LimbRecordSize;
            if (LimbCount > PortableLimbLimit || KeyframeCount > PortableKeyframeLimit ||
                FrameSize > PortableFileLimit ||
                static_cast<uint64_t>(KeyframeCount) > (PortableFileLimit - HeaderSize) / FrameSize)
            {
                if (Error)
                    *Error = std::string(Command) + " exceeds the portable 256 MiB limit";
                return false;
            }
            {
                Output = static_cast<std::size_t>(HeaderSize + static_cast<uint64_t>(KeyframeCount) * FrameSize);
                return true;
            }
        }

        bool ValidateLimbHierarchy(const std::vector<LimbTransform>& Limbs, const char* Command,
                                   std::string* Error = nullptr)
        {
            std::vector<uint8_t> State(Limbs.size(), 0);
            for (std::size_t Index = 0; Index < Limbs.size(); ++Index)
            {
                const int Parent = Limbs[Index].ParentLimb;
                if (Parent < -1 || Parent >= static_cast<int>(Limbs.size()) || Parent == static_cast<int>(Index))
                {
                    if (Error)
                        *Error = std::string(Command) + " found an invalid or self-parented limb";
                    return false;
                }
            }

            for (std::size_t Start = 0; Start < Limbs.size(); ++Start)
            {
                if (State[Start] != 0)
                    continue;
                int Current = static_cast<int>(Start);
                while (Current >= 0 && State[static_cast<std::size_t>(Current)] == 0)
                {
                    State[static_cast<std::size_t>(Current)] = 1;
                    Current = Limbs[static_cast<std::size_t>(Current)].ParentLimb;
                }
                if (Current >= 0 && State[static_cast<std::size_t>(Current)] == 1)
                {
                    if (Error)
                        *Error = std::string(Command) + " found a limb hierarchy cycle";
                    return false;
                }

                Current = static_cast<int>(Start);
                while (Current >= 0 && State[static_cast<std::size_t>(Current)] == 1)
                {
                    State[static_cast<std::size_t>(Current)] = 2;
                    Current = Limbs[static_cast<std::size_t>(Current)].ParentLimb;
                }
            }
            return true;
        }

        [[nodiscard]] std::array<float, 3> LimbEulerDegrees(const aiQuaternion& Rotation)
        {
            const aiMatrix3x3 Matrix = Rotation.GetMatrix();
            const double StandardY = std::asin(std::clamp(static_cast<double>(Matrix.a3), -1.0, 1.0));
            const double CosY = std::cos(StandardY);
            double StandardX = 0.0;
            double StandardZ = 0.0;
            if (std::abs(CosY) > 1e-6)
            {
                StandardX = std::atan2(-static_cast<double>(Matrix.b3), static_cast<double>(Matrix.c3));
                StandardZ = std::atan2(-static_cast<double>(Matrix.a2), static_cast<double>(Matrix.a1));
            }
            else
            {
                StandardX = std::atan2(static_cast<double>(Matrix.c2), static_cast<double>(Matrix.b2));
            }
            constexpr double ToDegrees = 180.0 / M_PI;
            return {static_cast<float>(-StandardX * ToDegrees), static_cast<float>(-StandardY * ToDegrees),
                    static_cast<float>(-StandardZ * ToDegrees)};
        }

        bool SampleImportedLimb(LimbTransform& Output, const Object3D& Object, const AnimationClip& Clip,
                                const aiNode* Node, std::size_t LimbIndex, double TimeTicks,
                                std::string* Error = nullptr)
        {
            LimbTransform Result =
                LimbIndex < Object.LimbTransforms.size() ? Object.LimbTransforms[LimbIndex] : LimbTransform{};
            if (!Node)
            {
                Output = std::move(Result);
                return true;
            }

            aiMatrix4x4 BindInverse = Node->mTransformation;
            const auto Determinant = BindInverse.Determinant();
            if (!std::isfinite(static_cast<double>(Determinant)) || std::abs(Determinant) < 1e-8)
            {
                if (Error)
                    *Error = "SAVE OBJECT ANIMATION found a non-invertible limb bind transform";
                return false;
            }
            BindInverse.Inverse();
            aiMatrix4x4 Animated = RendererSystem::EvaluateImportedNodeLocalTransform(Object, Node, Clip, TimeTicks);
            if (Node == Object.CompatibilityRootNode.get() && Object.CompatibilitySkippedRootNode)
            {
                aiMatrix4x4 SkippedBindInverse = Object.CompatibilitySkippedRootNode->mTransformation;
                const auto SkippedDeterminant = SkippedBindInverse.Determinant();
                if (!std::isfinite(static_cast<double>(SkippedDeterminant)) || std::abs(SkippedDeterminant) < 1e-8)
                {
                    if (Error)
                        *Error = "SAVE OBJECT ANIMATION found a non-invertible skipped root transform";
                    return false;
                }
                SkippedBindInverse.Inverse();
                Animated = Animated *
                           RendererSystem::EvaluateImportedNodeLocalTransform(
                               Object, Object.CompatibilitySkippedRootNode, Clip, TimeTicks) *
                           SkippedBindInverse;
            }
            const aiMatrix4x4 Delta = BindInverse * Animated;

            aiVector3D Scale;
            aiVector3D Offset;
            aiQuaternion Rotation;
            Delta.Decompose(Scale, Rotation, Offset);
            Rotation.Normalize();
            const std::array<float, 3> Euler = LimbEulerDegrees(Rotation);
            const std::array<double, 9> Values{Euler[0], Euler[1], Euler[2], Offset.x, Offset.y,
                                               Offset.z, Scale.x,  Scale.y,  Scale.z};
            if (!std::all_of(Values.begin(), Values.end(), [](double Value) { return std::isfinite(Value); }))
            {
                if (Error)
                    *Error = "SAVE OBJECT ANIMATION found a non-finite imported limb transform";
                return false;
            }

            Result.RotX = Euler[0];
            Result.RotY = Euler[1];
            Result.RotZ = Euler[2];
            Result.OffX = static_cast<float>(Offset.x);
            Result.OffY = static_cast<float>(Offset.y);
            Result.OffZ = static_cast<float>(Offset.z);
            Result.ScaleX = static_cast<float>(Scale.x);
            Result.ScaleY = static_cast<float>(Scale.y);
            Result.ScaleZ = static_cast<float>(Scale.z);
            const auto Parent =
                Node->mParent ? Object.LimbIndexByNode.find(Node->mParent) : Object.LimbIndexByNode.end();
            if (Parent != Object.LimbIndexByNode.end())
                Result.ParentLimb = Parent->second;
            else if (Object.CompatibilityRootNode && Node != Object.CompatibilityRootNode.get())
                Result.ParentLimb = 0;
            else
                Result.ParentLimb = -1;
            {
                Output = std::move(Result);
                return true;
            }
        }

        [[nodiscard]] float UnwrapLimbAngle(float Value, float Previous)
        {
            while (Value - Previous > 180.0f)
                Value -= 360.0f;
            while (Value - Previous < -180.0f)
                Value += 360.0f;
            return Value;
        }

        bool SampleImportedKeyframes(std::map<int, ObjectKeyframe>& Output, const Object3D& Object,
                                     std::string* Error = nullptr)
        {
            if (!Object.AssimpScene || Object.AnimationClips.empty())
            {
                Output = {};
                return true;
            }
            const int FirstFrame = std::min_element(Object.AnimationClips.begin(), Object.AnimationClips.end(),
                                                    [](const AnimationClip& A, const AnimationClip& B)
                                                    { return A.StartFrame < B.StartFrame; })
                                       ->StartFrame;
            const int LastFrame =
                std::max_element(Object.AnimationClips.begin(), Object.AnimationClips.end(),
                                 [](const AnimationClip& A, const AnimationClip& B) { return A.EndFrame < B.EndFrame; })
                    ->EndFrame;
            const int64_t FrameCount = static_cast<int64_t>(LastFrame) - FirstFrame + 1;
            if (FirstFrame < 0 || LastFrame < FirstFrame || FrameCount > PortableKeyframeLimit)
            {
                if (Error)
                    *Error = "SAVE OBJECT ANIMATION exceeds the imported keyframe limit";
                return false;
            }
            {
                std::size_t ValidatedSize = 0;
                if (!AnimationFileSize(ValidatedSize, Object.LimbTransforms.size(),
                                       static_cast<std::size_t>(FrameCount), "SAVE OBJECT ANIMATION", Error))
                    return false;
            }

            std::map<int, ObjectKeyframe> Keyframes;
            std::vector<LimbTransform> PreviousLimbs;
            for (int64_t FrameValue = FirstFrame; FrameValue <= LastFrame; ++FrameValue)
            {
                const int Frame = static_cast<int>(FrameValue);
                const AnimationClip& Clip = Object.AnimationClips[static_cast<std::size_t>(
                    AnimationClipIndexForFrame(Object.AnimationClips, Frame))];
                const double TimeTicks =
                    std::clamp(static_cast<double>(Frame - Clip.StartFrame) * Clip.TicksPerSecond / 25.0, 0.0,
                               std::max(0.0, Clip.DurationTicks));
                ObjectKeyframe Keyframe;
                Keyframe.Limbs.reserve(Object.LimbTransforms.size());
                for (std::size_t LimbIndex = 0; LimbIndex < Object.LimbTransforms.size(); ++LimbIndex)
                {
                    const aiNode* Node = LimbIndex < Object.LimbNodes.size() ? Object.LimbNodes[LimbIndex] : nullptr;
                    LimbTransform Transform;
                    if (!SampleImportedLimb(Transform, Object, Clip, Node, LimbIndex, TimeTicks, Error))
                        return false;
                    if (LimbIndex < PreviousLimbs.size())
                    {
                        Transform.RotX = UnwrapLimbAngle(Transform.RotX, PreviousLimbs[LimbIndex].RotX);
                        Transform.RotY = UnwrapLimbAngle(Transform.RotY, PreviousLimbs[LimbIndex].RotY);
                        Transform.RotZ = UnwrapLimbAngle(Transform.RotZ, PreviousLimbs[LimbIndex].RotZ);
                    }
                    Keyframe.Limbs.push_back(Transform);
                }
                PreviousLimbs = Keyframe.Limbs;
                Keyframes.emplace(Frame, std::move(Keyframe));
            }
            {
                Output = std::move(Keyframes);
                return true;
            }
        }

        constexpr unsigned int ObjectImportFlags =
            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
            aiProcess_ImproveCacheLocality | aiProcess_ConvertToLeftHanded | aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
            aiProcess_FindInvalidData | aiProcess_FindDegenerates;
        constexpr unsigned int PretransformedObjectImportFlags =
            ObjectImportFlags | aiProcess_PreTransformVertices | aiProcess_OptimizeMeshes;

        [[nodiscard]] AssimpImportResult ImportObjectModel(const std::string& Path, std::string& Error)
        {
            AssimpImportResult Primary = RendererSystem::ImportSceneWithFlags(Path, ObjectImportFlags);
            AssimpImportResult Pretransformed =
                RendererSystem::ImportSceneWithFlags(Path, PretransformedObjectImportFlags);
            Error = Primary.ImporterValue && *Primary.ImporterValue->GetErrorString()
                        ? Primary.ImporterValue->GetErrorString()
                        : "unsupported or unreadable model";

            bool UsePrimary = Primary.Scene && Primary.Scene->HasMeshes();
            if ((!UsePrimary || Primary.TriangleCount == 0) && Pretransformed.Scene &&
                Pretransformed.Scene->HasMeshes())
                UsePrimary = false;
            if (UsePrimary && Pretransformed.Scene && !Primary.Scene->HasAnimations() &&
                Pretransformed.TriangleCount > Primary.TriangleCount)
                UsePrimary = false;
            return UsePrimary ? std::move(Primary) : std::move(Pretransformed);
        }

        class StructureHasher
        {
          public:
            void U32(uint32_t Value)
            {
                for (unsigned Shift = 0; Shift < 32; Shift += 8)
                    Byte(static_cast<uint8_t>((Value >> Shift) & 0xFFu));
            }

            void I32(int32_t Value)
            {
                U32(static_cast<uint32_t>(Value));
            }

            void Float(float Value)
            {
                U32(std::bit_cast<uint32_t>(Value));
            }

            void String(std::string_view Value)
            {
                U32(static_cast<uint32_t>(Value.size()));
                for (const char Character : Value)
                    Byte(static_cast<uint8_t>(Character));
            }

            [[nodiscard]] std::string Finish() const
            {
                return std::to_string(First_) + ":" + std::to_string(Second_) + ":" + std::to_string(Size_);
            }

          private:
            void Byte(uint8_t Value)
            {
                First_ ^= Value;
                First_ *= 1099511628211ull;
                Second_ += Value + 0x9e3779b97f4a7c15ull + (Second_ << 6u) + (Second_ >> 2u);
                ++Size_;
            }

            uint64_t First_ = 1469598103934665603ull;
            uint64_t Second_ = 0x6a09e667f3bcc909ull;
            uint64_t Size_ = 0;
        };

        void HashMeshStructure(StructureHasher& Hasher, const aiMesh& MeshValue)
        {
            Hasher.U32(MeshValue.mPrimitiveTypes);
            Hasher.U32(MeshValue.mNumVertices);
            Hasher.U32(MeshValue.mNumFaces);
            for (unsigned int Index = 0; Index < MeshValue.mNumVertices; ++Index)
            {
                const aiVector3D& Vertex = MeshValue.mVertices[Index];
                Hasher.Float(Vertex.x);
                Hasher.Float(Vertex.y);
                Hasher.Float(Vertex.z);
            }
            for (unsigned int FaceIndex = 0; FaceIndex < MeshValue.mNumFaces; ++FaceIndex)
            {
                const aiFace& Face = MeshValue.mFaces[FaceIndex];
                Hasher.U32(Face.mNumIndices);
                for (unsigned int Index = 0; Index < Face.mNumIndices; ++Index)
                    Hasher.U32(Face.mIndices[Index]);
            }
        }

        [[nodiscard]] std::string ImportedObjectStructureSignature(const Object3D& Shape)
        {
            const aiScene* Scene = Shape.AssimpScene;
            if (!Scene || !Scene->mRootNode || !Scene->HasMeshes())
                return {};

            StructureHasher Hasher;
            Hasher.String("OpenDBC model structure v1");
            Hasher.U32(static_cast<uint32_t>(Shape.LimbNodes.size()));
            for (std::size_t LimbIndex = 0; LimbIndex < Shape.LimbNodes.size(); ++LimbIndex)
            {
                const aiNode* Node = Shape.LimbNodes[LimbIndex];
                Hasher.String(Node ? std::string_view(Node->mName.C_Str()) : std::string_view{});
                int Parent = -1;
                if (Node && Node != Shape.CompatibilityRootNode.get())
                {
                    const auto Found =
                        Node->mParent ? Shape.LimbIndexByNode.find(Node->mParent) : Shape.LimbIndexByNode.end();
                    if (Found != Shape.LimbIndexByNode.end())
                        Parent = Found->second;
                    else if (Shape.CompatibilityRootNode)
                        Parent = 0;
                }
                Hasher.I32(Parent);
                Hasher.U32(Node ? Node->mNumMeshes : 0u);
                if (Node)
                {
                    for (unsigned int MeshIndex = 0; MeshIndex < Node->mNumMeshes; ++MeshIndex)
                        Hasher.U32(Node->mMeshes[MeshIndex]);
                }
            }
            Hasher.U32(Scene->mNumMeshes);
            for (unsigned int MeshIndex = 0; MeshIndex < Scene->mNumMeshes; ++MeshIndex)
            {
                if (!Scene->mMeshes[MeshIndex])
                {
                    Hasher.U32(0);
                    continue;
                }
                Hasher.U32(1);
                HashMeshStructure(Hasher, *Scene->mMeshes[MeshIndex]);
            }
            return Hasher.Finish();
        }

        [[nodiscard]] std::string SceneStructureSignature(const aiScene* Scene, bool AddSyntheticRoot)
        {
            if (!Scene || !Scene->mRootNode || !Scene->HasMeshes())
                return {};
            Object3D Shape;
            Shape.AssimpScene = Scene;
            BuildImportedLimbMap(Scene, Shape, AddSyntheticRoot);
            return ImportedObjectStructureSignature(Shape);
        }

        [[nodiscard]] std::string ObjectStructureSignature(const Object3D& Object)
        {
            if (Object.AssimpScene)
                return ImportedObjectStructureSignature(Object);

            StructureHasher Hasher;
            Hasher.String("OpenDBC model structure v1");
            Hasher.U32(static_cast<uint32_t>(Object.Meshes.size()));
            for (std::size_t LimbIndex = 0; LimbIndex < Object.Meshes.size(); ++LimbIndex)
            {
                Hasher.String(std::string_view{});
                Hasher.I32(-1);
                Hasher.U32(1);
                Hasher.U32(static_cast<uint32_t>(LimbIndex));
            }
            Hasher.U32(static_cast<uint32_t>(Object.Meshes.size()));
            for (const Mesh& MeshValue : Object.Meshes)
            {
                Hasher.U32(1);
                Hasher.U32(static_cast<uint32_t>(MeshValue.Geometry().Indices.size()));
                for (const uint32_t Index : MeshValue.Geometry().Indices)
                {
                    if (Index >= MeshValue.Geometry().Vertices.size())
                    {
                        Hasher.U32(std::numeric_limits<uint32_t>::max());
                        continue;
                    }
                    const Vec3& Vertex = MeshValue.Geometry().Vertices[Index];
                    Hasher.Float(Vertex.X);
                    Hasher.Float(Vertex.Y);
                    Hasher.Float(Vertex.Z);
                }
                const aiMatrix4x4& Matrix = MeshValue.Geometry().BindTransform;
                for (const float Component :
                     {Matrix.a1, Matrix.a2, Matrix.a3, Matrix.a4, Matrix.b1, Matrix.b2, Matrix.b3, Matrix.b4, Matrix.c1,
                      Matrix.c2, Matrix.c3, Matrix.c4, Matrix.d1, Matrix.d2, Matrix.d3, Matrix.d4})
                    Hasher.Float(Component);
            }
            return Hasher.Finish();
        }

        bool LoadModelStructureSignature(std::string& Output, const std::string& Path, bool AddSyntheticRoot,
                                         std::string* Error = nullptr)
        {
            std::string Extension = dbc::PathToUtf8(dbc::PathFromUtf8(Path).extension());
            std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            if (Extension == ".dbo")
            {
                std::ifstream Input(dbc::PathFromUtf8(Path), std::ios::binary);
                if (!Input)
                {
                    if (Error)
                        *Error = "APPEND OBJECT ANIMATION cannot open model '" + Path + "'";
                    return false;
                }
                std::vector<uint8_t> Data(std::istreambuf_iterator<char>(Input), {});
                if (Input.bad())
                {
                    if (Error)
                        *Error = "Could not read the complete DBO file";
                    return false;
                }
                {
                    Object3D Object;
                    if (!DecodeDboObject(Object, Data, Error))
                        return false;
                    Output = ObjectStructureSignature(Object);
                    return true;
                }
            }

            std::string OperationError;
            AssimpImportResult Imported = ImportObjectModel(Path, OperationError);
            if (!Imported.Scene || !Imported.Scene->HasMeshes())
            {
                if (Error)
                    *Error = "APPEND OBJECT ANIMATION cannot import model '" + Path + "': " + OperationError;
                return false;
            }
            {
                Output = SceneStructureSignature(Imported.Scene, AddSyntheticRoot);
                return true;
            }
        }

        [[nodiscard]] Object3D StaticSnapshot(const Object3D& Source)
        {
            Object3D Result;
            Result.Meshes = Source.Meshes;
            Result.PosX = Source.PosX;
            Result.PosY = Source.PosY;
            Result.PosZ = Source.PosZ;
            Result.RotX = Source.RotX;
            Result.RotY = Source.RotY;
            Result.RotZ = Source.RotZ;
            Result.ScaleX = Source.ScaleX;
            Result.ScaleY = Source.ScaleY;
            Result.ScaleZ = Source.ScaleZ;
            Result.TintR = Source.TintR;
            Result.TintG = Source.TintG;
            Result.TintB = Source.TintB;
            Result.Alpha = Source.Alpha;
            Result.FadeNormalScale = Source.FadeNormalScale;
            Result.TexScrollU = Source.TexScrollU;
            Result.TexScrollV = Source.TexScrollV;
            Result.TextureImageId = Source.TextureImageId;
            Result.EffectId = Source.EffectId;
            Result.CubeGpuTexture = Source.CubeGpuTexture;
            Result.LockToScreen = Source.LockToScreen;
            Result.Ghosted = Source.Ghosted;
            Result.GhostMode = Source.GhostMode;
            Result.CollisionEnabled = false;
            Result.Visible = Source.Visible;
            Result.Wireframe = Source.Wireframe;
            Result.TransparentBlack = Source.TransparentBlack;
            Result.TransparencyMode = Source.TransparencyMode;
            Result.Cull = Source.Cull;
            Result.ZdepthDisabled = Source.ZdepthDisabled;
            Result.DepthTest = Source.DepthTest;
            Result.DepthWrite = Source.DepthWrite;
            Result.AlphaMapping = Source.AlphaMapping;
            Result.AlphaMappingAlpha = Source.AlphaMappingAlpha;
            Result.UseLighting = Source.UseLighting;
            Result.UseFog = Source.UseFog;
            Result.UseAmbient = Source.UseAmbient;
            Result.TextureFilter = Source.TextureFilter;
            Result.TexScaleU = Source.TexScaleU;
            Result.TexScaleV = Source.TexScaleV;
            Result.ApplyPivot = Source.ApplyPivot;
            Result.UseZyxRotation = Source.UseZyxRotation;
            Result.PivotMatrix = Source.PivotMatrix;
            Result.LimbTransforms = Source.LimbTransforms;
            Result.DiffuseColor = Source.DiffuseColor;
            Result.AmbientColor = Source.AmbientColor;
            Result.SpecularColor = Source.SpecularColor;
            Result.EmissiveColor = Source.EmissiveColor;
            Result.SpecularPower = Source.SpecularPower;
            Result.BoundingRadius = Source.BoundingRadius;
            Result.RenderBoundingRadius = Source.RenderBoundingRadius;
            Result.TransformDirty = true;
            return Result;
        }

        [[nodiscard]] std::string TextureSourcePath(const EngineKernel& Kernel, int ImageId)
        {
            const Image* Value = Kernel.ImageStoreRef().GetImage(ImageId);
            return Value ? Value->SourcePath : std::string{};
        }

        void SampleObjectKeyframe(Object3D& Object, double Frame)
        {
            if (Object.ObjectKeyframes.empty())
                return;
            const auto Upper = Object.ObjectKeyframes.lower_bound(static_cast<int>(std::ceil(Frame)));
            if (Upper == Object.ObjectKeyframes.begin())
            {
                Object.LimbTransforms = Upper->second.Limbs;
            }
            else if (Upper == Object.ObjectKeyframes.end())
            {
                Object.LimbTransforms = std::prev(Upper)->second.Limbs;
            }
            else
            {
                const auto Lower = std::prev(Upper);
                if (Upper->first == Lower->first || Frame <= static_cast<double>(Lower->first))
                {
                    Object.LimbTransforms = Lower->second.Limbs;
                }
                else
                {
                    const float T = static_cast<float>((Frame - Lower->first) / (Upper->first - Lower->first));
                    const std::size_t Count = std::min(Lower->second.Limbs.size(), Upper->second.Limbs.size());
                    Object.LimbTransforms.resize(Count);
                    const auto Lerp = [T](float A, float B) { return A + (B - A) * T; };
                    for (std::size_t Index = 0; Index < Count; ++Index)
                    {
                        const LimbTransform& A = Lower->second.Limbs[Index];
                        const LimbTransform& B = Upper->second.Limbs[Index];
                        LimbTransform& Out = Object.LimbTransforms[Index];
                        Out.RotX = Lerp(A.RotX, B.RotX);
                        Out.RotY = Lerp(A.RotY, B.RotY);
                        Out.RotZ = Lerp(A.RotZ, B.RotZ);
                        Out.OffX = Lerp(A.OffX, B.OffX);
                        Out.OffY = Lerp(A.OffY, B.OffY);
                        Out.OffZ = Lerp(A.OffZ, B.OffZ);
                        Out.ScaleX = Lerp(A.ScaleX, B.ScaleX);
                        Out.ScaleY = Lerp(A.ScaleY, B.ScaleY);
                        Out.ScaleZ = Lerp(A.ScaleZ, B.ScaleZ);
                        Out.Hidden = T < 0.5f ? A.Hidden : B.Hidden;
                        Out.ParentLimb = T < 0.5f ? A.ParentLimb : B.ParentLimb;
                    }
                }
            }
            const std::size_t Count =
                std::min(Object.LimbTransforms.size(),
                         Object.LimbNodes.empty() ? Object.Meshes.size() : Object.LimbNodes.size());
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                const LimbTransform& Transform = Object.LimbTransforms[Index];
                ForEachLimbMesh(Object, static_cast<int>(Index),
                                [&](Mesh& MeshValue)
                                {
                                    if (Object.LimbNodes.empty())
                                    {
                                        MeshValue.LimbRotX = Transform.RotX;
                                        MeshValue.LimbRotY = Transform.RotY;
                                        MeshValue.LimbRotZ = Transform.RotZ;
                                        MeshValue.LimbOffX = Transform.OffX;
                                        MeshValue.LimbOffY = Transform.OffY;
                                        MeshValue.LimbOffZ = Transform.OffZ;
                                        MeshValue.LimbScaleX = Transform.ScaleX;
                                        MeshValue.LimbScaleY = Transform.ScaleY;
                                        MeshValue.LimbScaleZ = Transform.ScaleZ;
                                    }
                                    MeshValue.Visible = !Transform.Hidden;
                                });
            }
            Object.CachedAnimTicks = std::numeric_limits<double>::quiet_NaN();
            Object.CachedNodeGlobals.clear();
            Object.TransformDirty = true;
        }

        // spatial_hash_key, segment_aabb_intersect — see collision_math.hpp
        template <typename T> [[nodiscard]] bool SameBytes(const std::vector<T>& A, const std::vector<T>& B)
        {
            return A.size() == B.size() && (A.empty() || std::memcmp(A.data(), B.data(), A.size() * sizeof(T)) == 0);
        }

        // Bit for bit, so that sharing never changes what an object draws.
        [[nodiscard]] bool SameGeometryContents(const MeshGeometry& A, const MeshGeometry& B)
        {
            if (!SameBytes(A.Vertices, B.Vertices) || !SameBytes(A.Normals, B.Normals) || !SameBytes(A.Uvs, B.Uvs) ||
                !SameBytes(A.DiffuseColors, B.DiffuseColors) || !SameBytes(A.Indices, B.Indices) ||
                std::memcmp(&A.BindTransform, &B.BindTransform, sizeof(A.BindTransform)) != 0 ||
                !SameBytes(A.BoneInfluences, B.BoneInfluences) || !SameBytes(A.BoneNodes, B.BoneNodes) ||
                !SameBytes(A.BoneOffsets, B.BoneOffsets) || A.UvStages.size() != B.UvStages.size())
                return false;
            return std::all_of(A.UvStages.begin(), A.UvStages.end(),
                               [&](const auto& Stage)
                               {
                                   const auto Other = B.UvStages.find(Stage.first);
                                   return Other != B.UvStages.end() && SameBytes(Stage.second, Other->second);
                               });
        }
    } // anonymous namespace

    // ── Constructor / destructor ─────────────────────────────────────

    ObjectManager::ObjectManager(EngineKernel& Kernel) : Kernel_(Kernel), SpatialHash_(std::make_unique<SpatialHash>())
    {
    }

    ObjectManager::~ObjectManager()
    {
        Objects_.clear();
        StaticObjects_.clear();
        AttachedStaticObjectIds_.clear();
        Meshes_.clear();
        StaticBoxes_.clear();
        if (SpatialHash_)
        {
            SpatialHash_->Cells.clear();
        }
    }

    // ── Mutable accessors (transitional) ─────────────────────────────

    Object3D ObjectManager::NewObjectState() const
    {
        Object3D Object;
        Object.CollisionShapeValue = Kernel_.RuntimeCompatibility().DefaultObjectCollisionShape();
        return Object;
    }

    Object3D* ObjectManager::GetObject(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return It != Objects_.end() ? &It->second : nullptr;
    }

    const Object3D* ObjectManager::GetObject(int ObjectId) const
    {
        const auto It = Objects_.find(ObjectId);
        return It != Objects_.end() ? &It->second : nullptr;
    }

    ParticleEmitter* ObjectManager::GetParticles(int ParticleId)
    {
        const auto It = Particles_.find(ParticleId);
        return It != Particles_.end() ? &It->second : nullptr;
    }

    const ParticleEmitter* ObjectManager::GetParticles(int ParticleId) const
    {
        const auto It = Particles_.find(ParticleId);
        return It != Particles_.end() ? &It->second : nullptr;
    }

    std::vector<Mesh>* ObjectManager::GetMeshes(int MeshId)
    {
        const auto It = Meshes_.find(MeshId);
        return It != Meshes_.end() ? &It->second : nullptr;
    }

    const std::vector<Mesh>* ObjectManager::GetMeshes(int MeshId) const
    {
        const auto It = Meshes_.find(MeshId);
        return It != Meshes_.end() ? &It->second : nullptr;
    }

    bool ObjectManager::AnyObjectDirty() const
    {
        for (const auto& [Id, Object] : Objects_)
        {
            if (Object.TransformDirty)
                return true;
        }
        for (const Object3D& Object : StaticObjects_)
        {
            if (Object.TransformDirty)
                return true;
        }
        for (const auto& [Id, Particles] : Particles_)
        {
            (void)Id;
            if (Particles.Visible)
                return true;
        }
        return false;
    }

    void ObjectManager::ClearAllDirtyFlags()
    {
        for (auto& [Id, Object] : Objects_)
            Object.TransformDirty = false;
        for (Object3D& Object : StaticObjects_)
            Object.TransformDirty = false;
    }

    void ObjectManager::UpdateParticles()
    {
        constexpr std::size_t ParticleLimit = 2048;
        constexpr std::size_t TotalLimit = ParticleLimit * 4;
        constexpr float Pi = 3.14159265358979323846f;
        // In simulated seconds, at most half a second for fire and snow alike: SET PARTICLE LIFE scales it.
        constexpr float VolumeFadeRate = 2.0f;
        for (auto& [Id, Emitter] : Particles_)
        {
            (void)Id;
            if (!Emitter.Visible)
                continue;

            const auto RandomUnit = [&Emitter]()
            {
                Emitter.RandomState = Emitter.RandomState * 1664525u + 1013904223u;
                return static_cast<float>(Emitter.RandomState & 0x00FFFFFFu) / 16777215.0f;
            };
            const auto RandomSign = [&Emitter]()
            {
                Emitter.RandomState = Emitter.RandomState * 1664525u + 1013904223u;
                return (Emitter.RandomState & 1u) != 0u ? 1.0f : -1.0f;
            };

            const float Step = Emitter.SecondsPerFrame + 0.00003f;
            Emitter.SimulationTime += Step;
            std::vector<ParticleInstance> Sparks;
            for (ParticleInstance& Particle : Emitter.Particles)
            {
                const float Age = Emitter.SimulationTime - Particle.BirthTime;
                const float Gravity = Particle.Spark ? -Emitter.Gravity : -(Emitter.Gravity * 2.0f);
                const float FadeRate =
                    Particle.Spark ? 2.25f : (Emitter.Kind == ParticleKind::Stream ? 0.25f : VolumeFadeRate);
                Particle.Fade = std::max(0.0f, Particle.Fade - Step * FadeRate);
                Particle.Position.X = Particle.InitialPosition.X + Particle.InitialVelocity.X * Age;
                Particle.Position.Y =
                    Particle.InitialPosition.Y + Particle.InitialVelocity.Y * Age + 0.5f * Gravity * Age * Age;
                Particle.Position.Z = Particle.InitialPosition.Z + Particle.InitialVelocity.Z * Age;
                Particle.Velocity = Particle.InitialVelocity;
                Particle.Velocity.Y += Gravity * Age;
                if (Emitter.Chaos != 0.0f)
                {
                    Particle.Position.X += RandomSign() * Emitter.Chaos;
                    Particle.Position.Y += RandomSign() * Emitter.Chaos;
                    Particle.Position.Z += RandomSign() * Emitter.Chaos;
                }

                const bool HitFloor = Emitter.FloorEnabled != 0 && Particle.Position.Y < 0.0f;
                if (!Particle.Spark && HitFloor && Emitter.Particles.size() + Sparks.size() < TotalLimit)
                {
                    for (int Index = 0; Index < 4 && Emitter.Particles.size() + Sparks.size() < TotalLimit; ++Index)
                    {
                        const float Azimuth = RandomUnit() * Pi * 2.0f;
                        const float Elevation = RandomUnit() * Pi * 0.25f;
                        ParticleInstance Spark;
                        Spark.Spark = true;
                        Spark.InitialPosition = Particle.Position;
                        Spark.InitialPosition.Y = 0.0f;
                        Spark.InitialVelocity.X = Particle.Velocity.X * 0.25f + std::cos(Azimuth) * std::sin(Elevation);
                        Spark.InitialVelocity.Z = Particle.Velocity.Z * 0.25f + std::sin(Azimuth) * std::sin(Elevation);
                        Spark.InitialVelocity.Y = std::cos(Elevation) * RandomUnit() * 1.5f;
                        Spark.Position = Spark.InitialPosition;
                        Spark.Velocity = Spark.InitialVelocity;
                        Spark.Diffuse = Emitter.EmissionColor;
                        Spark.Fade = static_cast<float>(Emitter.LifePercent) / 100.0f;
                        Spark.BirthTime = Emitter.SimulationTime;
                        Sparks.push_back(Spark);
                    }
                }
            }

            std::erase_if(
                Emitter.Particles,
                [&Emitter](const ParticleInstance& Particle)
                {
                    // Snow leaves through the bottom of its box, with time still to live.
                    return Particle.Fade <= 0.0f || (Emitter.FloorEnabled != 0 && Particle.Position.Y < 0.0f) ||
                           (Emitter.Kind == ParticleKind::Snow && Particle.Position.Y < -Emitter.Volume.Y * 0.5f);
                });
            Emitter.Particles.insert(Emitter.Particles.end(), Sparks.begin(), Sparks.end());

            const std::size_t LiveParticles = static_cast<std::size_t>(
                std::count_if(Emitter.Particles.begin(), Emitter.Particles.end(),
                              [](const ParticleInstance& Particle) { return !Particle.Spark; }));
            const std::size_t Requested = static_cast<std::size_t>(std::max(0, Emitter.EmissionsPerFrame));
            const std::size_t EmitCount = std::min(Requested, ParticleLimit - std::min(LiveParticles, ParticleLimit));
            for (std::size_t Index = 0; Index < EmitCount; ++Index)
            {
                const float Azimuth = RandomUnit() * Pi * 2.0f;
                const float Elevation = RandomUnit() * Pi * 0.25f;
                ParticleInstance Particle;
                if (Emitter.Kind == ParticleKind::Stream)
                {
                    Particle.InitialPosition = Emitter.EmissionPosition;
                    Particle.InitialVelocity.X = std::cos(Azimuth) * std::sin(Elevation) * 2.5f;
                    Particle.InitialVelocity.Z = std::sin(Azimuth) * std::sin(Elevation) * 2.5f;
                    Particle.InitialVelocity.Y = std::cos(Elevation) * RandomUnit() * Emitter.EmissionVelocity;
                }
                else
                {
                    const bool Fire = Emitter.Kind == ParticleKind::Fire;
                    Particle.InitialPosition = {(RandomUnit() - 0.5f) * Emitter.Volume.X,
                                                Fire ? 0.0f : Emitter.Volume.Y * 0.5f,
                                                (RandomUnit() - 0.5f) * Emitter.Volume.Z};
                    // Snow starts anywhere in the box while the box fills, and at its top once it is full.
                    if (!Fire && Emitter.SimulationTime < Emitter.Volume.Y / Emitter.EmissionVelocity)
                        Particle.InitialPosition.Y = (RandomUnit() - 0.5f) * Emitter.Volume.Y;
                    const float Drift = Emitter.EmissionVelocity * 0.2f;
                    Particle.InitialVelocity = {(RandomUnit() - 0.5f) * Drift,
                                                Emitter.EmissionVelocity * (Fire ? 0.6f + RandomUnit() * 0.4f : -1.0f),
                                                (RandomUnit() - 0.5f) * Drift};
                }
                Particle.Position = Particle.InitialPosition;
                Particle.Velocity = Particle.InitialVelocity;
                Particle.Diffuse = Emitter.EmissionColor;
                Particle.Fade = static_cast<float>(Emitter.LifePercent) / 100.0f;
                Particle.BirthTime = Emitter.SimulationTime;
                Emitter.Particles.push_back(Particle);
            }
        }
    }

    void ObjectManager::StoreMesh(int MeshId, Mesh MeshValue)
    {
        auto& Slot = Meshes_[MeshId];
        if (Slot.empty())
            Slot.push_back(std::move(MeshValue));
        else
            Slot[0] = std::move(MeshValue);
    }

    bool ObjectManager::LoadMesh(const std::string& Path, int MeshId, std::string* Error)
    {
        if (MeshId < 1 || MeshId > 65535)
        {
            if (Error)
                *Error = "LOAD MESH number must be between 1 and 65535";
            return false;
        }
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        constexpr unsigned int Flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                                       aiProcess_ConvertToLeftHanded | aiProcess_GenSmoothNormals |
                                       aiProcess_GenUVCoords | aiProcess_PreTransformVertices;
        AssimpImportResult Imported = RendererSystem::ImportSceneWithFlags(Resolved, Flags);
        if (!Imported.Scene || !Imported.Scene->HasMeshes())
        {
            const std::string Detail = Imported.ImporterValue && *Imported.ImporterValue->GetErrorString()
                                           ? Imported.ImporterValue->GetErrorString()
                                           : "unsupported or unreadable mesh";
            {
                if (Error)
                    *Error = "LOAD MESH failed for '" + Resolved + "': " + Detail;
                return false;
            }
        }

        std::vector<Mesh> Loaded;
        for (unsigned int MeshIndex = 0; MeshIndex < Imported.Scene->mNumMeshes; ++MeshIndex)
        {
            const aiMesh* Source = Imported.Scene->mMeshes[MeshIndex];
            if (!Source || Source->mNumVertices == 0)
                continue;
            Mesh Result;
            MeshGeometry& Geometry = Result.EditGeometry();
            Geometry.Vertices.reserve(Source->mNumVertices);
            Geometry.Normals.reserve(Source->mNumVertices);
            Geometry.Uvs.reserve(Source->mNumVertices);
            Geometry.DiffuseColors.reserve(Source->mNumVertices);
            for (unsigned int VertexIndex = 0; VertexIndex < Source->mNumVertices; ++VertexIndex)
            {
                const aiVector3D& Position = Source->mVertices[VertexIndex];
                Geometry.Vertices.push_back({Position.x, Position.y, Position.z});
                const aiVector3D Normal =
                    Source->HasNormals() ? Source->mNormals[VertexIndex] : aiVector3D(0.0f, 0.0f, 0.0f);
                Geometry.Normals.push_back(Vec3Normalize({Normal.x, Normal.y, Normal.z}));
                const aiVector3D Uv =
                    Source->HasTextureCoords(0) ? Source->mTextureCoords[0][VertexIndex] : aiVector3D(0.0f, 0.0f, 0.0f);
                Geometry.Uvs.push_back({Uv.x, Uv.y});
                Geometry.DiffuseColors.push_back(0xFFFFFFFFu);
            }
            for (unsigned int FaceIndex = 0; FaceIndex < Source->mNumFaces; ++FaceIndex)
            {
                const aiFace& Face = Source->mFaces[FaceIndex];
                if (Face.mNumIndices == 3)
                    Geometry.Indices.insert(Geometry.Indices.end(),
                                            {Face.mIndices[0], Face.mIndices[1], Face.mIndices[2]});
            }
            if (!Geometry.Indices.empty())
                Loaded.push_back(std::move(Result));
        }
        if (Loaded.empty())
        {
            if (Error)
                *Error = "LOAD MESH failed for '" + Resolved + "': mesh contains no triangles";
            return false;
        }
        Meshes_[MeshId] = std::move(Loaded);
        return true;
    }

    bool ObjectManager::SaveMesh(const std::string& Path, int MeshId, std::string* Error) const
    {
        const auto It = Meshes_.find(MeshId);
        if (It == Meshes_.end() || It->second.empty())
        {
            if (Error)
                *Error = "SAVE MESH requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        std::ofstream Output(dbc::PathFromUtf8(Resolved), std::ios::binary | std::ios::trunc);
        if (!Output)
        {
            if (Error)
                *Error = "SAVE MESH cannot create '" + Resolved + "'";
            return false;
        }
        const Mesh& Value = It->second.front();
        Output << "xof 0303txt 0032\nMesh {\n" << Value.Geometry().Vertices.size() << ";\n";
        for (std::size_t Index = 0; Index < Value.Geometry().Vertices.size(); ++Index)
        {
            const Vec3& Position = Value.Geometry().Vertices[Index];
            Output << Position.X << ';' << Position.Y << ';' << Position.Z << ';'
                   << (Index + 1 == Value.Geometry().Vertices.size() ? ";\n" : ",\n");
        }
        Output << Value.Geometry().Indices.size() / 3 << ";\n";
        for (std::size_t Index = 0; Index + 2 < Value.Geometry().Indices.size(); Index += 3)
        {
            Output << "3;" << Value.Geometry().Indices[Index] << ',' << Value.Geometry().Indices[Index + 1] << ','
                   << Value.Geometry().Indices[Index + 2] << ';'
                   << (Index + 3 == Value.Geometry().Indices.size() ? ";\n" : ",\n");
        }
        Output << "}\n";
        Output.close();
        if (!Output)
        {
            if (Error)
                *Error = "SAVE MESH failed while writing '" + Resolved + "'";
            return false;
        }
        return true;
    }

    bool ObjectManager::DeleteMesh(int MeshId, std::string* Error)
    {
        if (Meshes_.erase(MeshId) == 0)
        {
            if (Error)
                *Error = "DELETE MESH requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        if (VertexLockKind_ == VertexLockKind::Mesh && VertexLockId_ == MeshId)
            UnlockVertexdata();
        return true;
    }

    int ObjectManager::MeshExist(int MeshId) const
    {
        return Meshes_.contains(MeshId) ? 1 : 0;
    }

    SpatialHash& ObjectManager::SpatialHashRef()
    {
        return *SpatialHash_;
    }

    std::vector<AABB>& ObjectManager::StaticBoxes()
    {
        return StaticBoxes_;
    }

    float& ObjectManager::CollisionSlideX()
    {
        return CollisionSlideX_;
    }

    float& ObjectManager::CollisionSlideY()
    {
        return CollisionSlideY_;
    }

    float& ObjectManager::CollisionSlideZ()
    {
        return CollisionSlideZ_;
    }

    float& ObjectManager::StaticHitX()
    {
        return StaticHitX_;
    }

    float& ObjectManager::StaticHitY()
    {
        return StaticHitY_;
    }

    float& ObjectManager::StaticHitZ()
    {
        return StaticHitZ_;
    }

    // ── Object CRUD ──────────────────────────────────────────────────

    bool ObjectManager::LoadObject(const std::string& Path, int ObjectId, int LoadMode, int DivideTextureSize,
                                   std::string* Error)
    {
        namespace fs = std::filesystem;
        if (ObjectId < 1 || ObjectId > 65535)
        {
            if (Error)
                *Error = "LOAD OBJECT number must be between 1 and 65535";
            return false;
        }
        if (LoadMode < 0 || LoadMode > 5)
        {
            if (Error)
                *Error = "LOAD OBJECT mode must be between zero and five";
            return false;
        }
        if (DivideTextureSize < 0)
        {
            if (Error)
                *Error = "LOAD OBJECT texture divisor cannot be negative";
            return false;
        }
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        const fs::path ResolvedPath = dbc::PathFromUtf8(Resolved);
        std::string Extension = dbc::PathToUtf8(ResolvedPath.extension());
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        if (Extension == ".dbo")
        {
            std::ifstream Input(dbc::PathFromUtf8(Resolved), std::ios::binary);
            if (!Input)
            {
                if (Error)
                    *Error = "LOAD OBJECT cannot open '" + Resolved + "'";
                return false;
            }
            std::vector<uint8_t> Data(std::istreambuf_iterator<char>(Input), {});
            if (Input.bad())
            {
                if (Error)
                    *Error = "Could not read the complete DBO file";
                return false;
            }

            Object3D Object;
            if (!DecodeDboObject(Object, Data, Error))
                return false;
            for (std::size_t Index = 0; Index < Object.Meshes.size(); ++Index)
                ShareLoadedGeometry(Resolved, Index, Object.Meshes[Index]);
            Object.CollisionShapeValue = Kernel_.RuntimeCompatibility().DefaultObjectCollisionShape();
            Object.ModelStructureSignature = ObjectStructureSignature(Object);
            Objects_[ObjectId] = std::move(Object);

            RecalculateObjectBounds(Objects_[ObjectId]);
            NotifyObjectCreated(ObjectId);
            return true;
        }

        std::string ImportError;
        AssimpImportResult Imported = ImportObjectModel(Resolved, ImportError);
        std::unique_ptr<Assimp::Importer> ImporterValue = std::move(Imported.ImporterValue);
        const aiScene* Scene = Imported.Scene;

        if (!Scene || !Scene->HasMeshes())
        {
            DBC_LOG_ERROR("LoadObject failed for {} (resolved: {}) -> {}", Path, Resolved, ImportError);
            {
                if (Error)
                    *Error = "LOAD OBJECT failed for '" + Resolved + "': " + ImportError;
                return false;
            }
        }

        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.ImporterValue = std::move(ImporterValue);
        Object.AssimpScene = Scene;
        Object.ModelStructureSignature =
            SceneStructureSignature(Scene, Kernel_.RuntimeCompatibility().AddSyntheticObjectRoot());
        Object.AnimIndex = 0;
        Object.ActiveAnimationClip = 0;
        Object.ActiveAnimDurationTicks = 0.0;
        Object.ActiveAnimTicksPerSec = 25.0;
        if (Scene->mRootNode)
        {
            Object.RootInverseTransform = Scene->mRootNode->mTransformation;
            Object.RootInverseTransform.Inverse();
        }
        if (Scene->mNumAnimations > 0 && Scene->mAnimations[0])
        {
            Object.AnimationClips.push_back(MakeAnimationClip(Scene->mAnimations[0], 0));
            ApplyAnimationClip(Object, 0, 0);
        }
        if (Scene->mRootNode)
        {
            RendererSystem::BuildMeshNodeMapRecursive(Scene->mRootNode, Object.MeshNodeByIndex);
            BuildImportedLimbMap(Scene, Object, Kernel_.RuntimeCompatibility().AddSyntheticObjectRoot());
            Object.LimbTransforms.resize(Object.LimbNodes.size());
        }

        Object.Meshes.reserve(Scene->mNumMeshes);
        for (unsigned int Mi = 0; Mi < Scene->mNumMeshes; ++Mi)
        {
            const aiMesh* ImportedMesh = Scene->mMeshes[Mi];
            if (!ImportedMesh || ImportedMesh->mNumVertices == 0 || ImportedMesh->mNumFaces == 0)
            {
                continue;
            }

            Mesh Out;
            MeshGeometry& Geometry = Out.EditGeometry();
            Out.SourceMeshIndex = static_cast<int>(Mi);
            const bool HasVertexNormals = ImportedMesh->HasNormals();
            Geometry.Vertices.reserve(ImportedMesh->mNumVertices);
            Geometry.Normals.reserve(ImportedMesh->mNumVertices);
            Geometry.Uvs.reserve(ImportedMesh->mNumVertices);
            for (unsigned int Vi = 0; Vi < ImportedMesh->mNumVertices; ++Vi)
            {
                const aiVector3D& V = ImportedMesh->mVertices[Vi];
                Geometry.Vertices.push_back(Vec3{V.x, V.y, V.z});
                if (HasVertexNormals)
                {
                    const aiVector3D& N = ImportedMesh->mNormals[Vi];
                    Geometry.Normals.push_back(Vec3Normalize(Vec3{N.x, N.y, N.z}));
                }
                else
                {
                    Geometry.Normals.push_back(Vec3{0.0f, 0.0f, 0.0f});
                }
                if (ImportedMesh->HasTextureCoords(0))
                {
                    const aiVector3D& Uv = ImportedMesh->mTextureCoords[0][Vi];
                    Geometry.Uvs.push_back(SDL_FPoint{Uv.x, Uv.y});
                }
                else
                {
                    Geometry.Uvs.push_back(SDL_FPoint{0.0f, 0.0f});
                }
            }

            if (ImportedMesh->HasBones() && Scene->mRootNode)
            {
                Geometry.BoneInfluences.resize(ImportedMesh->mNumVertices);
                Geometry.BoneNodes.reserve(ImportedMesh->mNumBones);
                Geometry.BoneOffsets.reserve(ImportedMesh->mNumBones);
                for (unsigned int BoneSourceIndex = 0; BoneSourceIndex < ImportedMesh->mNumBones; ++BoneSourceIndex)
                {
                    const aiBone* Bone = ImportedMesh->mBones[BoneSourceIndex];
                    if (!Bone)
                        continue;
                    const aiNode* BoneNode = Scene->mRootNode->FindNode(Bone->mName);
                    if (!BoneNode)
                        continue;

                    const int BoneIndex = static_cast<int>(Geometry.BoneNodes.size());
                    Geometry.BoneNodes.push_back(BoneNode);
                    Geometry.BoneOffsets.push_back(Bone->mOffsetMatrix);
                    for (unsigned int WeightIndex = 0; WeightIndex < Bone->mNumWeights; ++WeightIndex)
                    {
                        const aiVertexWeight& SourceWeight = Bone->mWeights[WeightIndex];
                        if (SourceWeight.mVertexId >= Geometry.BoneInfluences.size() ||
                            !std::isfinite(SourceWeight.mWeight) || SourceWeight.mWeight <= 0.0f)
                            continue;
                        BoneInfluence& Influence = Geometry.BoneInfluences[SourceWeight.mVertexId];
                        for (std::size_t Slot = 0; Slot < Influence.Weights.size(); ++Slot)
                        {
                            if (SourceWeight.mWeight <= Influence.Weights[Slot])
                                continue;
                            for (std::size_t Shift = Influence.Weights.size() - 1; Shift > Slot; --Shift)
                            {
                                Influence.Weights[Shift] = Influence.Weights[Shift - 1];
                                Influence.Indices[Shift] = Influence.Indices[Shift - 1];
                            }
                            Influence.Weights[Slot] = SourceWeight.mWeight;
                            Influence.Indices[Slot] = BoneIndex;
                            break;
                        }
                    }
                }
                for (BoneInfluence& Influence : Geometry.BoneInfluences)
                {
                    float Total = 0.0f;
                    for (float Weight : Influence.Weights)
                        Total += Weight;
                    if (Total > 0.0f)
                    {
                        for (float& Weight : Influence.Weights)
                            Weight /= Total;
                    }
                }
            }

            Geometry.Indices.reserve(ImportedMesh->mNumFaces * 3);
            for (unsigned int Fi = 0; Fi < ImportedMesh->mNumFaces; ++Fi)
            {
                const aiFace& F = ImportedMesh->mFaces[Fi];
                if (F.mNumIndices != 3)
                {
                    continue;
                }
                uint32_t I0 = F.mIndices[0];
                uint32_t I1 = F.mIndices[1];
                uint32_t I2 = F.mIndices[2];
                if (HasVertexNormals && I0 < Geometry.Vertices.size() && I1 < Geometry.Vertices.size() &&
                    I2 < Geometry.Vertices.size())
                {
                    const Vec3 Face = Vec3Cross(Vec3Sub(Geometry.Vertices[I1], Geometry.Vertices[I0]),
                                                Vec3Sub(Geometry.Vertices[I2], Geometry.Vertices[I0]));
                    const Vec3 Avg{
                        Geometry.Normals[I0].X + Geometry.Normals[I1].X + Geometry.Normals[I2].X,
                        Geometry.Normals[I0].Y + Geometry.Normals[I1].Y + Geometry.Normals[I2].Y,
                        Geometry.Normals[I0].Z + Geometry.Normals[I1].Z + Geometry.Normals[I2].Z,
                    };
                    if (Vec3Dot(Face, Avg) < 0.0f)
                    {
                        std::swap(I1, I2);
                    }
                }
                Geometry.Indices.push_back(I0);
                Geometry.Indices.push_back(I1);
                Geometry.Indices.push_back(I2);
            }

            bool HasValidNormals = false;
            for (const Vec3& N : Geometry.Normals)
            {
                if (std::fabs(N.X) + std::fabs(N.Y) + std::fabs(N.Z) > 0.01f)
                {
                    HasValidNormals = true;
                    break;
                }
            }
            if (!HasValidNormals)
            {
                RendererSystem::RegenerateMeshNormals(Out);
            }
            else
            {
                for (Vec3& N : Geometry.Normals)
                {
                    N = Vec3Normalize(N);
                }
            }

            if (!Geometry.Vertices.empty() && !Geometry.Indices.empty())
            {
                const auto NodeIt = Object.MeshNodeByIndex.find(Mi);
                if (NodeIt != Object.MeshNodeByIndex.end())
                {
                    const aiNode* Node = NodeIt->second;
                    if (Node)
                    {
                        std::vector<const aiNode*> Chain;
                        const aiNode* Cur = Node;
                        while (Cur)
                        {
                            Chain.push_back(Cur);
                            Cur = Cur->mParent;
                        }
                        aiMatrix4x4 Bind;
                        for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
                        {
                            Bind = Bind * (*It)->mTransformation;
                        }
                        Geometry.BindTransform = Bind;
                    }
                }
                if (ImportedMesh->mMaterialIndex < Scene->mNumMaterials)
                {
                    const aiMaterial* Mat = Scene->mMaterials[ImportedMesh->mMaterialIndex];
                    if (Mat)
                    {
                        aiColor4D DiffuseColor(1.0f, 1.0f, 1.0f, 1.0f);
                        if (Mat->Get(AI_MATKEY_COLOR_DIFFUSE, DiffuseColor) == aiReturn_SUCCESS)
                        {
                            Out.MaterialColor =
                                SDL_FColor{DiffuseColor.r, DiffuseColor.g, DiffuseColor.b, DiffuseColor.a};
                        }

                        static constexpr std::array<aiTextureType, 8> KTextureSearchOrder = {
                            aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR, aiTextureType_EMISSIVE,
                            aiTextureType_NORMALS, aiTextureType_HEIGHT,     aiTextureType_SPECULAR,
                            aiTextureType_AMBIENT, aiTextureType_UNKNOWN,
                        };

                        for (aiTextureType TexType : KTextureSearchOrder)
                        {
                            if (Mat->GetTextureCount(TexType) == 0)
                            {
                                continue;
                            }
                            aiString TexRel;
                            if (Mat->GetTexture(TexType, 0, &TexRel) != aiReturn_SUCCESS)
                            {
                                continue;
                            }

                            std::string TexName = TexRel.C_Str();
                            std::replace(TexName.begin(), TexName.end(), '\\', '/');

                            if (!TexName.empty() && TexName.front() == '*')
                            {
                                if (const aiTexture* Embedded = Scene->GetEmbeddedTexture(TexName.c_str()))
                                {
                                    Out.TextureImageId = Kernel_.ImageStoreRef().LoadImageInternalFromEmbeddedTexture(
                                        Embedded, Resolved + "::embedded:" + TexName, DivideTextureSize);
                                }
                            }
                            else
                            {
                                fs::path TexturePath = ResolvedPath.parent_path() / dbc::PathFromUtf8(TexName);
                                bool Exists = false;
                                if (!PathExists(TexturePath, Exists, Error))
                                    return false;
                                if (!Exists)
                                {
                                    if (!ResolveCaseInsensitivePath(TexturePath, ResolvedPath.parent_path(),
                                                                    dbc::PathFromUtf8(TexName), Error))
                                        return false;
                                    Exists = !TexturePath.empty();
                                }
                                if (!Exists)
                                {
                                    std::string GlobalPath;
                                    if (!ImageStore::ResolveAssetPath(GlobalPath, TexName, Error))
                                        return false;
                                    TexturePath = dbc::PathFromUtf8(GlobalPath);
                                    if (!PathExists(TexturePath, Exists, Error))
                                        return false;
                                }
                                if (Exists)
                                    Out.TextureImageId = Kernel_.ImageStoreRef().LoadImageInternalFromPath(
                                        dbc::PathToUtf8(TexturePath), DivideTextureSize);
                            }

                            if (Out.TextureImageId != 0)
                            {
                                Out.TextureUsesMaterialColor = LoadMode >= 2;
                                break;
                            }
                        }
                    }
                }
                ShareLoadedGeometry(Resolved, Mi, Out);
                Object.Meshes.push_back(std::move(Out));
            }
        }

        if (Object.Meshes.empty())
        {
            DBC_LOG_ERROR("LoadObject failed for {} (resolved: {}) -> no triangles", Path, Resolved);
            {
                if (Error)
                    *Error = "LOAD OBJECT failed for '" + Resolved + "': model contains no triangles";
                return false;
            }
        }

        Objects_[ObjectId] = std::move(Object);
        RecalculateObjectBounds(Objects_[ObjectId]);
        NotifyObjectCreated(ObjectId);
        return true;
    }

    bool ObjectManager::SaveObject(const std::string& Path, int ObjectId, std::string* Error) const
    {
        const Object3D* ObjectPointer = RequireObject(ObjectId, "SAVE OBJECT", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        std::string Extension = dbc::PathToUtf8(dbc::PathFromUtf8(Resolved).extension());
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        if (Extension != ".dbo")
        {
            if (Error)
                *Error = "SAVE OBJECT requires a .dbo filename";
            return false;
        }
        std::vector<uint8_t> Data;
        if (!EncodeDboObject(Data, Object, Error))
            return false;
        std::ofstream Output(dbc::PathFromUtf8(Resolved), std::ios::binary | std::ios::trunc);
        if (!Output)
        {
            if (Error)
                *Error = "SAVE OBJECT cannot create '" + Resolved + "'";
            return false;
        }
        Output.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
        Output.close();
        if (!Output)
        {
            if (Error)
                *Error = "SAVE OBJECT failed while writing '" + Resolved + "'";
            return false;
        }
        return true;
    }

    bool ObjectManager::AppendObject(const std::string& Path, int ObjectId, int StartFrame, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "APPEND OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (StartFrame < 0)
        {
            if (Error)
                *Error = "APPEND OBJECT start frame cannot be negative";
            return false;
        }

        constexpr unsigned int KAppendImportFlags =
            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
            aiProcess_ImproveCacheLocality | aiProcess_ConvertToLeftHanded | aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace | aiProcess_GenUVCoords | aiProcess_TransformUVCoords |
            aiProcess_FindInvalidData | aiProcess_FindDegenerates;

        std::unique_ptr<Assimp::Importer> ImporterValue = std::make_unique<Assimp::Importer>();
        ImporterValue->SetIOHandler(new SdlAssimpIOSystem());
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        const aiScene* Scene = ImporterValue->ReadFile(Resolved, KAppendImportFlags);
        if (!Scene)
        {
            DBC_LOG_ERROR("AppendObject failed for {} (resolved: {}) -> {}", Path, Resolved,
                          ImporterValue ? ImporterValue->GetErrorString() : "assimp import failed");
            const std::string ImportError = ImporterValue && *ImporterValue->GetErrorString()
                                                ? ImporterValue->GetErrorString()
                                                : "unsupported or unreadable animation";
            {
                if (Error)
                    *Error = "APPEND OBJECT failed for '" + Resolved + "': " + ImportError;
                return false;
            }
        }
        if (!Scene->HasAnimations() || !Scene->mAnimations[0])
        {
            if (Error)
                *Error = "APPEND OBJECT failed for '" + Resolved + "': source contains no animation";
            return false;
        }
        if (!Object.AssimpScene || !Object.AssimpScene->mRootNode)
        {
            if (Error)
                *Error = "APPEND OBJECT requires a destination object with an imported skeleton";
            return false;
        }

        const aiAnimation* Animation = Scene->mAnimations[0];
        for (unsigned int Index = 0; Index < Animation->mNumChannels; ++Index)
        {
            const aiNodeAnim* Channel = Animation->mChannels[Index];
            if (Channel && !Object.AssimpScene->mRootNode->FindNode(Channel->mNodeName))
            {
                if (Error)
                    *Error = "APPEND OBJECT animation node '" + std::string(Channel->mNodeName.C_Str()) +
                             "' is not present in destination object " + std::to_string(ObjectId);
                return false;
            }
        }

        Object.AnimationImporters.push_back(std::move(ImporterValue));
        Object.AnimationClips.push_back(MakeAnimationClip(Animation, StartFrame));
        Object.ImportedAnimationEnabled = true;
        std::sort(Object.AnimationClips.begin(), Object.AnimationClips.end(),
                  [](const AnimationClip& A, const AnimationClip& B) { return A.StartFrame < B.StartFrame; });
        SelectAnimationClipForFrame(Object, Object.Frame);
        return true;
    }

    int ObjectManager::TotalObjectFrames(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
        {
            return 0;
        }
        const Object3D& Object = It->second;
        if (!Object.ObjectKeyframes.empty())
            return Object.ObjectKeyframes.rbegin()->first;
        if (!Object.AnimationClips.empty())
        {
            int EndFrame = 0;
            for (const AnimationClip& Clip : Object.AnimationClips)
            {
                EndFrame = std::max(EndFrame, Clip.EndFrame);
            }
            return EndFrame;
        }
        if (Object.ImportedAnimationEnabled && Object.AssimpScene && Object.AssimpScene->mNumAnimations > 0 &&
            Object.AssimpScene->mAnimations[0])
        {
            const aiAnimation* Animation = Object.AssimpScene->mAnimations[0];
            const double TicksPerSecond = std::isfinite(Animation->mTicksPerSecond) && Animation->mTicksPerSecond > 0.0
                                              ? Animation->mTicksPerSecond
                                              : 25.0;
            const double Duration = std::isfinite(Animation->mDuration) ? std::max(0.0, Animation->mDuration) : 0.0;
            return static_cast<int>(std::lround(
                std::min(Duration * 25.0 / TicksPerSecond, static_cast<double>(std::numeric_limits<int>::max()))));
        }
        return 0;
    }

    std::shared_ptr<const MeshGeometry> ObjectManager::FindSharedGeometry(const GeometryKey& Key) const
    {
        const auto Found = SharedGeometries_.find(Key);
        if (Found == SharedGeometries_.end())
            return nullptr;
        std::shared_ptr<const MeshGeometry> Geometry = Found->second.Geometry.lock();
        return Geometry && Geometry->Revision == Found->second.Revision ? Geometry : nullptr;
    }

    void ObjectManager::RememberSharedGeometry(const GeometryKey& Key, const Mesh& Value)
    {
        if (SharedGeometries_.size() >= SharedGeometryPruneSize_)
        {
            std::erase_if(SharedGeometries_, [](const auto& Entry) { return Entry.second.Geometry.expired(); });
            SharedGeometryPruneSize_ = std::max<std::size_t>(256, SharedGeometries_.size() * 2);
        }
        SharedGeometries_[Key] = SharedGeometryEntry{Value.SharedGeometry(), Value.Geometry().Revision};
    }

    template <typename Build> Mesh ObjectManager::PrimitiveMesh(const GeometryKey& Key, Build&& Make)
    {
        Mesh Result;
        if (std::shared_ptr<const MeshGeometry> Geometry = FindSharedGeometry(Key))
        {
            Result.ShareGeometry(std::move(Geometry));
            return Result;
        }
        Result = Make();
        RememberSharedGeometry(Key, Result);
        return Result;
    }

    void ObjectManager::ShareLoadedGeometry(const std::string& Path, std::size_t MeshIndex, Mesh& Loaded)
    {
        // Bones name the nodes of each load's own scene.
        if (!Loaded.Geometry().BoneNodes.empty() || !Loaded.Geometry().BoneInfluences.empty())
            return;
        const uint64_t PathHash = std::hash<std::string>{}(Path);
        const GeometryKey Key{5, static_cast<uint32_t>(PathHash), static_cast<uint32_t>(PathHash >> 32u),
                              static_cast<uint32_t>(MeshIndex), 0};
        // The file may have changed since, and different paths may hash alike: only equal contents are shared.
        std::shared_ptr<const MeshGeometry> Earlier = FindSharedGeometry(Key);
        if (Earlier && SameGeometryContents(*Earlier, Loaded.Geometry()))
            Loaded.ShareGeometry(std::move(Earlier));
        else
            RememberSharedGeometry(Key, Loaded);
    }

    bool ObjectManager::MakeObjectPlain(int ObjectId, float Width, float Height, int Orientation, std::string* Error)
    {
        if (Orientation != 0 && Orientation != 1)
        {
            if (Error)
                *Error = "MAKE OBJECT PLAIN orientation must be zero or one";
            return false;
        }
        Object3D Object = NewObjectState();
        Object.Visible = true;
        const bool RotatedByObject = Kernel_.RuntimeCompatibility().PlainOrientationUsesObjectRotation();
        const bool Flipped = !RotatedByObject && Orientation == 0;
        Object.Meshes.push_back(
            PrimitiveMesh({4, std::bit_cast<uint32_t>(Width), std::bit_cast<uint32_t>(Height), Flipped ? 1u : 0u, 0},
                          [&]
                          {
                              Mesh Plain = RendererSystem::MakePlainMesh(Width, Height);
                              if (Flipped)
                              {
                                  std::vector<uint32_t>& Indices = Plain.EditGeometry().Indices;
                                  for (std::size_t Index = 0; Index + 2 < Indices.size(); Index += 3)
                                      std::swap(Indices[Index + 1], Indices[Index + 2]);
                                  RendererSystem::RegenerateMeshNormals(Plain);
                              }
                              return Plain;
                          }));
        if (!RotatedByObject)
            Object.RotY = 0.0f;
        else
            Object.RotY = Orientation == 0 ? 180.0f : 0.0f;
        Object.BoundingRadius = (std::abs(Width) + std::abs(Height)) * 0.25f;
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
        return true;
    }

    void ObjectManager::MakeObjectCube(int ObjectId, float Size)
    {
        Object3D Object = NewObjectState();
        Object.Visible = true;
        const uint32_t SizeBits = std::bit_cast<uint32_t>(Size);
        Object.Meshes.push_back(PrimitiveMesh({0, SizeBits, SizeBits, SizeBits, 0},
                                              [&] { return RendererSystem::MakeBoxMesh(Size, Size, Size); }));
        Object.BoundingRadius = std::abs(Size) * 0.5f;
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    void ObjectManager::MakeObjectBox(int ObjectId, float Width, float Height, float Depth)
    {
        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.Meshes.push_back(PrimitiveMesh(
            {0, std::bit_cast<uint32_t>(Width), std::bit_cast<uint32_t>(Height), std::bit_cast<uint32_t>(Depth), 0},
            [&] { return RendererSystem::MakeBoxMesh(Width, Height, Depth); }));
        Object.BoundingRadius = (std::abs(Width) + std::abs(Height)) * 0.25f;
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    void ObjectManager::MakeObjectSphere(int ObjectId, float Radius, int Rings, int Segments)
    {
        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.Meshes.push_back(PrimitiveMesh(
            {1, std::bit_cast<uint32_t>(Radius), static_cast<uint32_t>(Rings), static_cast<uint32_t>(Segments), 0},
            [&] { return RendererSystem::MakeSphereMesh(Radius * 0.5f, Rings, Segments); }));
        Object.BoundingRadius = std::abs(Radius) * 0.5f;
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    void ObjectManager::MakeObjectCylinder(int ObjectId, float Radius, float Height, int Segments)
    {
        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.Meshes.push_back(PrimitiveMesh(
            {2, std::bit_cast<uint32_t>(Radius), std::bit_cast<uint32_t>(Height), static_cast<uint32_t>(Segments), 0},
            [&] { return RendererSystem::MakeCylinderMesh(Radius, Height, Segments, true, true); }));
        Object.BoundingRadius = std::max(std::abs(Radius), std::abs(Height));
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    void ObjectManager::MakeObjectCone(int ObjectId, float Radius, float Height, int Segments)
    {
        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.Meshes.push_back(PrimitiveMesh(
            {3, std::bit_cast<uint32_t>(Radius), std::bit_cast<uint32_t>(Height), static_cast<uint32_t>(Segments), 0},
            [&] { return RendererSystem::MakeConeMesh(Radius, Height, Segments); }));
        Object.BoundingRadius = std::max(std::abs(Radius), std::abs(Height));
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    void ObjectManager::MakeObjectTriangle(int ObjectId, float X1, float Y1, float Z1, float X2, float Y2, float Z2,
                                           float X3, float Y3, float Z3)
    {
        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.Meshes.push_back(RendererSystem::MakeTriangleMesh(Vec3{X1, Y1, Z1}, Vec3{X2, Y2, Z2}, Vec3{X3, Y3, Z3}));
        Object.BoundingRadius = std::abs(X2 - X1);
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    bool ObjectManager::MakeMeshFromObject(int MeshId, int ObjectId, int IgnoreMode, std::string* Error)
    {
        if (IgnoreMode != 0 && IgnoreMode != 1)
        {
            if (Error)
                *Error = "MAKE MESH FROM OBJECT ignore mode must be zero or one";
            return false;
        }
        const Object3D* ObjectPointer = RequireObject(ObjectId, "MAKE MESH FROM OBJECT", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        Mesh Result;
        MeshGeometry& Geometry = Result.EditGeometry();
        for (const Mesh& Source : Object.Meshes)
        {
            if (IgnoreMode == 1 && !Source.Visible)
                continue;
            const uint32_t VertexBase = static_cast<uint32_t>(Geometry.Vertices.size());
            for (const Vec3& Position : Source.Geometry().Vertices)
            {
                const aiVector3D Transformed =
                    Source.Geometry().BindTransform * aiVector3D(Position.X, Position.Y, Position.Z);
                Geometry.Vertices.push_back({Transformed.x, Transformed.y, Transformed.z});
            }
            Geometry.Normals.resize(VertexBase, Vec3{});
            Geometry.Normals.insert(Geometry.Normals.end(), Source.Geometry().Normals.begin(),
                                    Source.Geometry().Normals.end());
            Geometry.Normals.resize(Geometry.Vertices.size(), Vec3{});
            Geometry.Uvs.resize(VertexBase, SDL_FPoint{});
            Geometry.Uvs.insert(Geometry.Uvs.end(), Source.Geometry().Uvs.begin(), Source.Geometry().Uvs.end());
            Geometry.Uvs.resize(Geometry.Vertices.size(), SDL_FPoint{});
            Geometry.DiffuseColors.resize(VertexBase, 0xFFFFFFFFu);
            Geometry.DiffuseColors.insert(Geometry.DiffuseColors.end(), Source.Geometry().DiffuseColors.begin(),
                                          Source.Geometry().DiffuseColors.end());
            Geometry.DiffuseColors.resize(Geometry.Vertices.size(), 0xFFFFFFFFu);
            for (const auto& [Stage, Values] : Source.Geometry().UvStages)
            {
                auto& Target = Geometry.UvStages[Stage];
                Target.resize(VertexBase, SDL_FPoint{});
                Target.insert(Target.end(), Values.begin(), Values.end());
                Target.resize(Geometry.Vertices.size(), SDL_FPoint{});
            }
            for (uint32_t Index : Source.Geometry().Indices)
            {
                if (Index >= Source.Geometry().Vertices.size())
                {
                    if (Error)
                        *Error = "MAKE MESH FROM OBJECT found an invalid object mesh index";
                    return false;
                }
                Geometry.Indices.push_back(VertexBase + Index);
            }
        }
        if (Geometry.Vertices.empty() || Geometry.Indices.empty())
        {
            if (Error)
                *Error = "MAKE MESH FROM OBJECT found no triangle geometry";
            return false;
        }
        Meshes_[MeshId] = {std::move(Result)};
        return true;
    }

    void ObjectManager::MakeObject(int ObjectId, int MeshId, int ImageId)
    {
        const auto MeshIt = Meshes_.find(MeshId);
        if (MeshIt == Meshes_.end())
            return;
        Object3D Object = NewObjectState();
        Object.Visible = true;
        Object.Meshes = MeshIt->second;
        if (ImageId > 0 && Kernel_.ImageStoreRef().ImageExist(ImageId) == 1)
        {
            Object.TextureImageId = ImageId;
        }
        Objects_[ObjectId] = std::move(Object);
        NotifyObjectCreated(ObjectId);
    }

    bool ObjectManager::MakeObjectFromLimb(int ObjectId, int SourceObjectId, int LimbId, int AllLimbs,
                                           std::string* Error)
    {
        if (AllLimbs != 0 && AllLimbs != 1)
        {
            if (Error)
                *Error = "MAKE OBJECT FROM LIMB all-limbs flag must be zero or one";
            return false;
        }
        const Object3D* SourcePointer = RequireObject(SourceObjectId, "MAKE OBJECT FROM LIMB", Error);
        if (!SourcePointer)
            return false;
        const Object3D& Source = *SourcePointer;
        if (!RequireLimb(Source, SourceObjectId, LimbId, "MAKE OBJECT FROM LIMB", Error))
            return false;

        std::vector<int> Selected{LimbId};
        if (AllLimbs != 0)
        {
            for (std::size_t Candidate = 0; Candidate < Source.LimbTransforms.size(); ++Candidate)
            {
                for (int Parent = Source.LimbTransforms[Candidate].ParentLimb;
                     Parent >= 0 && Parent < static_cast<int>(Source.LimbTransforms.size());
                     Parent = Source.LimbTransforms[static_cast<std::size_t>(Parent)].ParentLimb)
                {
                    if (Parent == LimbId)
                    {
                        Selected.push_back(static_cast<int>(Candidate));
                        break;
                    }
                    if (Source.LimbTransforms[static_cast<std::size_t>(Parent)].ParentLimb == Parent)
                        break;
                }
            }
        }

        Object3D Result = NewObjectState();
        Result.Visible = true;
        for (int SelectedLimb : Selected)
        {
            if (!Source.LimbNodes.empty())
            {
                const aiNode* Node = Source.LimbNodes[static_cast<std::size_t>(SelectedLimb)];
                if (!Node)
                    continue;
                for (unsigned int Index = 0; Index < Node->mNumMeshes; ++Index)
                {
                    const int SourceIndex = static_cast<int>(Node->mMeshes[Index]);
                    const auto MeshIt =
                        std::find_if(Source.Meshes.begin(), Source.Meshes.end(),
                                     [SourceIndex](const Mesh& Value) { return Value.SourceMeshIndex == SourceIndex; });
                    if (MeshIt != Source.Meshes.end())
                        Result.Meshes.push_back(*MeshIt);
                }
            }
            else if (SelectedLimb < static_cast<int>(Source.Meshes.size()))
            {
                Result.Meshes.push_back(Source.Meshes[static_cast<std::size_t>(SelectedLimb)]);
            }
        }
        if (Result.Meshes.empty())
        {
            if (Error)
                *Error = "MAKE OBJECT FROM LIMB selected a limb without geometry";
            return false;
        }
        Result.LimbTransforms.resize(Result.Meshes.size());
        Objects_[ObjectId] = std::move(Result);
        RecalculateObjectBounds(Objects_[ObjectId]);
        NotifyObjectCreated(ObjectId);
        return true;
    }

    bool ObjectManager::AddLimb(int ObjectId, int LimbId, int SourceMeshId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "ADD LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        const auto MeshStoreIt = Meshes_.find(SourceMeshId);
        if (MeshStoreIt == Meshes_.end() || MeshStoreIt->second.empty())
        {
            if (Error)
                *Error = "ADD LIMB requires existing mesh " + std::to_string(SourceMeshId);
            return false;
        }
        const int NextLimb = static_cast<int>(std::max(Object.Meshes.size(), Object.LimbNodes.size()));
        if (LimbId == -1)
            LimbId = NextLimb;
        if (LimbId != NextLimb)
        {
            if (Error)
                *Error = "ADD LIMB number must be the next consecutive limb " + std::to_string(NextLimb);
            return false;
        }
        Object.Meshes.resize(static_cast<std::size_t>(LimbId));
        Mesh Added = MeshStoreIt->second.front();
        Added.SourceMeshIndex = -1;
        // An added limb has no skeleton; it shares the stored geometry unless that has one to drop.
        if (const MeshGeometry& Stored = Added.Geometry();
            !Stored.BoneInfluences.empty() || !Stored.BoneNodes.empty() || !Stored.BoneOffsets.empty())
        {
            MeshGeometry& Geometry = Added.EditGeometry();
            Geometry.BoneInfluences.clear();
            Geometry.BoneNodes.clear();
            Geometry.BoneOffsets.clear();
        }
        Object.Meshes.push_back(std::move(Added));
        Object.LimbTransforms.resize(static_cast<std::size_t>(LimbId) + 1);
        Object.TransformDirty = true;
        RecalculateObjectBounds(Object);
        return true;
    }

    bool ObjectManager::RemoveLimb(int ObjectId, int LimbId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "REMOVE LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "REMOVE LIMB", Error))
            return false;
        if (LimbId == 0)
        {
            if (Error)
                *Error = "REMOVE LIMB cannot remove limb zero";
            return false;
        }

        if (!Object.LimbNodes.empty() && static_cast<std::size_t>(LimbId) < Object.LimbNodes.size())
        {
            const aiNode* Node = Object.LimbNodes[static_cast<std::size_t>(LimbId)];
            std::unordered_set<int> SourceMeshes;
            if (Node)
            {
                for (unsigned int Index = 0; Index < Node->mNumMeshes; ++Index)
                    SourceMeshes.insert(static_cast<int>(Node->mMeshes[Index]));
            }
            Object.Meshes.erase(std::remove_if(Object.Meshes.begin(), Object.Meshes.end(), [&](const Mesh& MeshValue)
                                               { return SourceMeshes.contains(MeshValue.SourceMeshIndex); }),
                                Object.Meshes.end());
            Object.LimbIndexByNode.erase(Node);
            Object.LimbNodes[static_cast<std::size_t>(LimbId)] = nullptr;
        }
        else
        {
            Object.Meshes.erase(Object.Meshes.begin() + LimbId);
            Object.LimbTransforms.erase(Object.LimbTransforms.begin() + LimbId);
            for (LimbTransform& Transform : Object.LimbTransforms)
            {
                if (Transform.ParentLimb == LimbId)
                    Transform.ParentLimb = 0;
                else if (Transform.ParentLimb > LimbId)
                    --Transform.ParentLimb;
            }
        }
        Object.TransformDirty = true;
        RecalculateObjectBounds(Object);
        return true;
    }

    void ObjectManager::CloneObject(int DestId, int SourceId)
    {
        auto SrcIt = Objects_.find(SourceId);
        if (SrcIt == Objects_.end())
            return;
        const Object3D& Src = SrcIt->second;

        Object3D Dst = NewObjectState();
        // Deep-copy all mesh data.
        Dst.Meshes = Src.Meshes;
        // Copy transform.
        Dst.PosX = Src.PosX;
        Dst.PosY = Src.PosY;
        Dst.PosZ = Src.PosZ;
        Dst.RotX = Src.RotX;
        Dst.RotY = Src.RotY;
        Dst.RotZ = Src.RotZ;
        Dst.ScaleX = Src.ScaleX;
        Dst.ScaleY = Src.ScaleY;
        Dst.ScaleZ = Src.ScaleZ;
        // Copy appearance.
        Dst.TintR = Src.TintR;
        Dst.TintG = Src.TintG;
        Dst.TintB = Src.TintB;
        Dst.Alpha = Src.Alpha;
        Dst.FadeNormalScale = Src.FadeNormalScale;
        Dst.TexScrollU = Src.TexScrollU;
        Dst.TexScrollV = Src.TexScrollV;
        Dst.TextureImageId = Src.TextureImageId;
        Dst.CubeGpuTexture = Src.CubeGpuTexture;
        // Copy flags.
        Dst.LockToScreen = Src.LockToScreen;
        Dst.Ghosted = Src.Ghosted;
        Dst.GhostMode = Src.GhostMode;
        Dst.CollisionEnabled = Src.CollisionEnabled;
        if (!Kernel_.RuntimeCompatibility().CloneUsesDefaultCollisionShape())
            Dst.CollisionShapeValue = Src.CollisionShapeValue;
        Dst.CollisionAabb = Src.CollisionAabb;
        Dst.CollisionAabbExplicit = Src.CollisionAabbExplicit;
        Dst.CollisionBoxRotates = Src.CollisionBoxRotates;
        Dst.Visible = Src.Visible;
        Dst.Wireframe = Src.Wireframe;
        Dst.TransparentBlack = Src.TransparentBlack;
        Dst.TransparencyMode = Src.TransparencyMode;
        Dst.Cull = Src.Cull;
        Dst.ZdepthDisabled = Src.ZdepthDisabled;
        Dst.DepthTest = Src.DepthTest;
        Dst.DepthWrite = Src.DepthWrite;
        Dst.DrawBounds = Src.DrawBounds;
        Dst.BoundsBoxOnly = Src.BoundsBoxOnly;
        Dst.AlphaMapping = Src.AlphaMapping;
        Dst.AlphaMappingAlpha = Src.AlphaMappingAlpha;
        Dst.UseLighting = Src.UseLighting;
        Dst.UseFog = Src.UseFog;
        Dst.UseAmbient = Src.UseAmbient;
        Dst.TextureFilter = Src.TextureFilter;
        Dst.ReflectionShading = Src.ReflectionShading;
        Dst.ShadowShading = Src.ShadowShading;
        Dst.ShadowConfigured = Src.ShadowConfigured;
        Dst.ShadowMeshId = Src.ShadowMeshId;
        Dst.ShadowRange = Src.ShadowRange;
        Dst.ShadowUseShader = Src.ShadowUseShader;
        Dst.ShadowMeshes = Src.ShadowMeshes;
        Dst.ApplyPivot = Src.ApplyPivot;
        Dst.PivotMatrix = Src.PivotMatrix;
        Dst.DiffuseColor = Src.DiffuseColor;
        Dst.AmbientColor = Src.AmbientColor;
        Dst.SpecularColor = Src.SpecularColor;
        Dst.EmissiveColor = Src.EmissiveColor;
        Dst.SpecularPower = Src.SpecularPower;
        // Copy limb transforms (independent per clone).
        Dst.LimbTransforms = Src.LimbTransforms;
        // Animation playback state.
        Dst.Speed = Src.Speed;
        Dst.Frame = Src.Frame;
        Dst.Interpolation = Src.Interpolation;
        Dst.AnimTimeSeconds = Src.AnimTimeSeconds;
        Dst.AnimIndex = Src.AnimIndex;
        Dst.ActiveAnimationClip = Src.ActiveAnimationClip;
        Dst.ActiveAnimDurationTicks = Src.ActiveAnimDurationTicks;
        Dst.ActiveAnimTicksPerSec = Src.ActiveAnimTicksPerSec;
        Dst.ImportedAnimationEnabled = Src.ImportedAnimationEnabled;
        Dst.ObjectKeyframes = Src.ObjectKeyframes;
        Dst.ModelStructureSignature = Src.ModelStructureSignature;
        // The imported scene drives skinning, limbs and imported animation.
        Dst.ImporterValue = Src.ImporterValue;
        Dst.AssimpScene = Src.AssimpScene;
        Dst.CompatibilityRootNode = Src.CompatibilityRootNode;
        Dst.CompatibilitySkippedRootNode = Src.CompatibilitySkippedRootNode;
        Dst.AnimationImporters = Src.AnimationImporters;
        Dst.MeshNodeByIndex = Src.MeshNodeByIndex;
        Dst.RootInverseTransform = Src.RootInverseTransform;
        Dst.LimbNodes = Src.LimbNodes;
        Dst.LimbIndexByNode = Src.LimbIndexByNode;
        Dst.AnimChannelByNode = Src.AnimChannelByNode;
        Dst.AnimationClips = Src.AnimationClips;
        // Bounding radius carries over (same mesh data).
        Dst.BoundingRadius = Src.BoundingRadius;
        Dst.RenderBoundingRadius = Src.RenderBoundingRadius;
        Dst.TransformDirty = true;

        Objects_[DestId] = std::move(Dst);
        NotifyObjectCreated(DestId);
    }

    void ObjectManager::InstanceObject(int DestId, int SourceId)
    {
        // DBC: instance shares mesh data; clone deep-copies.
        // For now both deep-copy; shared-mesh optimization is deferred.
        CloneObject(DestId, SourceId);
    }

    void ObjectManager::DeleteObject(int ObjectId)
    {
        Kernel_.Gpu().DeleteObjectEffect(ObjectId);
        AttachedStaticObjectIds_.erase(ObjectId);
        // No object is left to carry a dirty flag, so the cached scene goes.
        if (Objects_.erase(ObjectId) != 0)
            Kernel_.Renderer().InvalidateTriCache();
        ObjectHitHistory_.erase(ObjectId);
        LimbHitHistory_.erase(ObjectId);
        CollisionFeedback_.erase(ObjectId);
        if (LastCollisionObject_ == ObjectId)
        {
            LastCollisionObject_ = 0;
            CollisionSlideX_ = 0.0f;
            CollisionSlideY_ = 0.0f;
            CollisionSlideZ_ = 0.0f;
        }
    }

    bool ObjectManager::DeleteObjects(int FirstObjectId, int LastObjectId, std::string* Error)
    {
        if (FirstObjectId > LastObjectId)
        {
            if (Error)
                *Error = "DELETE OBJECTS requires the first object number to be no greater than the last";
            return false;
        }
        std::vector<int> DeletedIds;
        for (const auto& [Id, Object] : Objects_)
        {
            (void)Object;
            if (Id >= FirstObjectId && Id <= LastObjectId)
                DeletedIds.push_back(Id);
        }
        for (const int Id : DeletedIds)
            DeleteObject(Id);
        return true;
    }

    int ObjectManager::ObjectExist(int ObjectId)
    {
        return Objects_.find(ObjectId) != Objects_.end() ? 1 : 0;
    }

    // ── Position / rotation / scale ──────────────────────────────────

    void ObjectManager::PositionObject(int ObjectId, float X, float Y, float Z)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.PosX = X;
        It->second.PosY = Y;
        It->second.PosZ = Z;
        It->second.TransformDirty = true;
    }

    void ObjectManager::RotateObject(int ObjectId, float XDegrees, float YDegrees, float ZDegrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.RotX = XDegrees;
        It->second.RotY = YDegrees;
        It->second.RotZ = ZDegrees;
        It->second.TransformDirty = true;
    }

    void ObjectManager::XrotateObject(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.RotX = Degrees;
        It->second.TransformDirty = true;
    }

    void ObjectManager::YrotateObject(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.RotY = Degrees;
        It->second.TransformDirty = true;
    }

    void ObjectManager::ZrotateObject(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.RotZ = Degrees;
        It->second.TransformDirty = true;
    }

    void ObjectManager::ScaleObject(int ObjectId, float XPercent, float YPercent, float ZPercent)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.ScaleX = XPercent / 100.0f;
        It->second.ScaleY = YPercent / 100.0f;
        It->second.ScaleZ = ZPercent / 100.0f;
        It->second.TransformDirty = true;
    }

    float ObjectManager::ObjectPositionX(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        return Kernel_.Renderer().WorldObjectOrigin(It->second).X;
    }

    float ObjectManager::ObjectPositionY(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        return Kernel_.Renderer().WorldObjectOrigin(It->second).Y;
    }

    float ObjectManager::ObjectPositionZ(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        return Kernel_.Renderer().WorldObjectOrigin(It->second).Z;
    }

    float ObjectManager::ObjectAngleX(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0.0f : It->second.RotX;
    }

    float ObjectManager::ObjectAngleY(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0.0f : It->second.RotY;
    }

    float ObjectManager::ObjectAngleZ(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0.0f : It->second.RotZ;
    }

    // ── Movement ─────────────────────────────────────────────────────

    void ObjectManager::MoveObject(int ObjectId, float Step)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        const float Yaw = DegToRad(It->second.RotY);
        const float Pitch = DegToRad(It->second.RotX);
        const float Cx = std::cos(Pitch);
        const float Nx = It->second.PosX + std::sin(Yaw) * Cx * Step;
        const float Ny = It->second.PosY - std::sin(Pitch) * Step;
        const float Nz = It->second.PosZ + std::cos(Yaw) * Cx * Step;
        PositionObject(ObjectId, Nx, Ny, Nz);
    }

    void ObjectManager::MoveObjectUp(int ObjectId, float Step)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        PositionObject(ObjectId, It->second.PosX, It->second.PosY + Step, It->second.PosZ);
    }

    void ObjectManager::MoveObjectDown(int ObjectId, float Step)
    {
        MoveObjectUp(ObjectId, -Step);
    }

    void ObjectManager::MoveObjectLeft(int ObjectId, float Step)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        const float Yaw = DegToRad(It->second.RotY - 90.0f);
        const float Nx = It->second.PosX + std::sin(Yaw) * Step;
        const float Nz = It->second.PosZ + std::cos(Yaw) * Step;
        PositionObject(ObjectId, Nx, It->second.PosY, Nz);
    }

    void ObjectManager::MoveObjectRight(int ObjectId, float Step)
    {
        MoveObjectLeft(ObjectId, -Step);
    }

    void ObjectManager::PointObject(int ObjectId, float X, float Y, float Z)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        const float Dx = X - It->second.PosX;
        const float Dy = Y - It->second.PosY;
        const float Dz = Z - It->second.PosZ;
        const float Flat = std::sqrt(Dx * Dx + Dz * Dz);
        float Yaw = static_cast<float>(std::atan2(Dx, Dz) * 180.0 / M_PI);
        if (Yaw < 0.0f)
            Yaw += 360.0f;
        It->second.RotY = Yaw;
        It->second.RotX = static_cast<float>(-std::atan2(Dy, std::max(0.0001f, Flat)) * 180.0 / M_PI);
        It->second.TransformDirty = true;
    }

    void ObjectManager::TurnObjectLeft(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        float Out = std::fmod(It->second.RotY - Degrees, 360.0f);
        if (Out < 0.0f)
            Out += 360.0f;
        It->second.RotY = Out;
        It->second.TransformDirty = true;
    }

    void ObjectManager::TurnObjectRight(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        float Out = std::fmod(It->second.RotY + Degrees, 360.0f);
        if (Out < 0.0f)
            Out += 360.0f;
        It->second.RotY = Out;
        It->second.TransformDirty = true;
    }

    void ObjectManager::PitchObjectUp(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.RotX -= Degrees;
        It->second.TransformDirty = true;
    }

    void ObjectManager::PitchObjectDown(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.RotX += Degrees;
        It->second.TransformDirty = true;
    }

    void ObjectManager::RollObjectLeft(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        float Out = std::fmod(It->second.RotZ - Degrees, 360.0f);
        if (Out < 0.0f)
            Out += 360.0f;
        It->second.RotZ = Out;
        It->second.TransformDirty = true;
    }

    void ObjectManager::RollObjectRight(int ObjectId, float Degrees)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        float Out = std::fmod(It->second.RotZ + Degrees, 360.0f);
        if (Out < 0.0f)
            Out += 360.0f;
        It->second.RotZ = Out;
        It->second.TransformDirty = true;
    }

    bool ObjectManager::SetObjectToObjectOrientation(int DestId, int SrcId, int IncludePivot, std::string* Error)
    {
        if (IncludePivot != 0 && IncludePivot != 1)
        {
            if (Error)
                *Error = "SET OBJECT TO OBJECT ORIENTATION pivot flag must be zero or one";
            return false;
        }
        Object3D* DestinationPointer = RequireObject(DestId, "SET OBJECT TO OBJECT ORIENTATION", Error);
        if (!DestinationPointer)
            return false;
        Object3D& Destination = *DestinationPointer;
        const Object3D* SourcePointer = RequireObject(SrcId, "SET OBJECT TO OBJECT ORIENTATION", Error);
        if (!SourcePointer)
            return false;
        const Object3D& Source = *SourcePointer;
        Destination.RotX = Source.RotX;
        Destination.RotY = Source.RotY;
        Destination.RotZ = Source.RotZ;
        Destination.UseZyxRotation = Source.UseZyxRotation;
        if (IncludePivot == 1)
        {
            Destination.ApplyPivot = Source.ApplyPivot;
            Destination.PivotMatrix = Source.PivotMatrix;
        }
        Destination.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectToCameraOrientation(int ObjectId, std::string* Error)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
        {
            if (Error)
                *Error = "SET OBJECT TO CAMERA ORIENTATION requires existing object " + std::to_string(ObjectId);
            return false;
        }
        It->second.RotX = Kernel_.Camera().AngleX();
        It->second.RotY = Kernel_.Camera().AngleY();
        It->second.RotZ = Kernel_.Camera().AngleZ();
        It->second.TransformDirty = true;
        return true;
    }

    // ── Collision ────────────────────────────────────────────────────

    int ObjectManager::ObjectCollision(int ObjectId, int TargetObjectId)
    {
        LastCollisionObject_ = ObjectId;
        CollisionFeedback_[ObjectId] = {};
        CollisionSlideX_ = 0.0f;
        CollisionSlideY_ = 0.0f;
        CollisionSlideZ_ = 0.0f;

        const auto IsEligible = [&](const Object3D& Object)
        { return GlobalCollisionEnabled_ || Object.CollisionEnabled; };
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || !IsEligible(It->second))
            return 0;

        const CollisionSnapshot ObjectSnapshot = BuildCollisionSnapshot(Kernel_, It->second);
        const bool NeedsFeedback = It->second.CollisionShapeValue == CollisionShape::Aabb &&
                                   It->second.CollisionAabbExplicit && !It->second.CollisionBoxRotates;
        const auto StoreFeedback = [&](const CollisionContact& Contact)
        {
            Vec3 Feedback{};
            if (Contact.Hit && It->second.CollisionShapeValue == CollisionShape::Aabb &&
                It->second.CollisionAabbExplicit && !It->second.CollisionBoxRotates)
                Feedback = CollisionScale(Contact.Correction, -1.0f);
            CollisionFeedback_[ObjectId] = Feedback;
            CollisionSlideX_ = Feedback.X;
            CollisionSlideY_ = Feedback.Y;
            CollisionSlideZ_ = Feedback.Z;
        };
        const auto CollidesWith = [&](const CollisionSnapshot& TargetSnapshot)
        {
            if (!NeedsFeedback)
                return CollisionSnapshotsOverlap(ObjectSnapshot, TargetSnapshot);
            const CollisionContact Contact = CollisionSnapshotContact(ObjectSnapshot, TargetSnapshot);
            if (!Contact.Hit)
                return false;
            StoreFeedback(Contact);
            return true;
        };

        if (!ObjectSnapshot.BoundsValid)
            return 0;

        if (TargetObjectId > 0)
        {
            if (TargetObjectId == ObjectId)
                return 0;
            const auto Jt = Objects_.find(TargetObjectId);
            if (Jt == Objects_.end() || !IsEligible(Jt->second))
                return 0;
            if (CollidesWith(BuildCollisionSnapshot(Kernel_, Jt->second)))
                return 1;
            return 0;
        }

        SpatialHash_->Cells.clear();
        const float Cs = SpatialHash_->CellSize;
        const auto IndexBounds = [&](int Id, const AABB& Bounds)
        {
            const int MinCellX = static_cast<int>(std::floor(Bounds.MinX / Cs));
            const int MaxCellX = static_cast<int>(std::floor(Bounds.MaxX / Cs));
            const int MinCellZ = static_cast<int>(std::floor(Bounds.MinZ / Cs));
            const int MaxCellZ = static_cast<int>(std::floor(Bounds.MaxZ / Cs));
            for (int CellX = MinCellX; CellX <= MaxCellX; ++CellX)
                for (int CellZ = MinCellZ; CellZ <= MaxCellZ; ++CellZ)
                    SpatialHash_->Cells[SpatialHashKey(CellX, CellZ)].push_back(Id);
        };
        for (const auto& [Id, Object] : Objects_)
        {
            if (!IsEligible(Object))
                continue;
            if (Id == ObjectId)
            {
                IndexBounds(Id, ObjectSnapshot.Bounds);
                continue;
            }
            const CollisionBroadphaseBounds Bounds = BuildCollisionBroadphaseBounds(Kernel_, Object);
            if (!Bounds.Valid)
                continue;
            IndexBounds(Id, Bounds.Bounds);
        }

        std::vector<int> Candidates;
        {
            const int MinCx = static_cast<int>(std::floor(ObjectSnapshot.Bounds.MinX / Cs));
            const int MaxCx = static_cast<int>(std::floor(ObjectSnapshot.Bounds.MaxX / Cs));
            const int MinCz = static_cast<int>(std::floor(ObjectSnapshot.Bounds.MinZ / Cs));
            const int MaxCz = static_cast<int>(std::floor(ObjectSnapshot.Bounds.MaxZ / Cs));
            std::unordered_set<int> Seen;
            for (int Cx = MinCx; Cx <= MaxCx; ++Cx)
            {
                for (int Cz = MinCz; Cz <= MaxCz; ++Cz)
                {
                    auto Sit = SpatialHash_->Cells.find(SpatialHashKey(Cx, Cz));
                    if (Sit != SpatialHash_->Cells.end())
                        for (int Id : Sit->second)
                            if (Seen.insert(Id).second)
                                Candidates.push_back(Id);
                }
            }
        }

        std::sort(Candidates.begin(), Candidates.end());

        for (int CandId : Candidates)
        {
            if (CandId == ObjectId)
                continue;
            const auto Jt = Objects_.find(CandId);
            if (Jt == Objects_.end() || !IsEligible(Jt->second))
                continue;
            if (CollidesWith(BuildCollisionSnapshot(Kernel_, Jt->second)))
                return CandId;
        }
        return 0;
    }

    int ObjectManager::ObjectHit(int ObjectId, int TargetObjectId)
    {
        if (Objects_.find(ObjectId) == Objects_.end())
            return 0;
        const int Overlap = ObjectCollision(ObjectId, TargetObjectId);
        int& Stored = ObjectHitHistory_[ObjectId];
        if (Stored == 0)
        {
            if (Overlap > 0)
            {
                Stored = TargetObjectId > 0 ? TargetObjectId : Overlap;
                return Overlap;
            }
        }
        else if ((TargetObjectId == 0 || Stored == TargetObjectId) && Overlap == 0)
        {
            Stored = 0;
        }
        return 0;
    }

    int ObjectManager::LimbCollision(int ObjectA, int LimbA, int ObjectB, int LimbB)
    {
        const auto AIt = Objects_.find(ObjectA);
        const auto BIt = Objects_.find(ObjectB);
        if (AIt == Objects_.end() || BIt == Objects_.end())
            return 0;
        if ((!GlobalCollisionEnabled_ && !AIt->second.CollisionEnabled) ||
            (!GlobalCollisionEnabled_ && !BIt->second.CollisionEnabled))
            return -1;

        const auto Candidates = [&](const Object3D& Object, int Requested)
        {
            std::vector<std::pair<int, const Mesh*>> Result;
            if (Requested >= 0)
            {
                if (const Mesh* Value = Kernel_.Renderer().FindLimbMesh(Object, Requested))
                    Result.emplace_back(Requested, Value);
                return Result;
            }
            const int Count =
                std::max(static_cast<int>(Object.LimbTransforms.size()), static_cast<int>(Object.Meshes.size()));
            std::unordered_set<const Mesh*> Seen;
            for (int Limb = 0; Limb < Count; ++Limb)
            {
                if (const Mesh* Value = Kernel_.Renderer().FindLimbMesh(Object, Limb);
                    Value && Seen.insert(Value).second)
                    Result.emplace_back(Limb, Value);
            }
            return Result;
        };

        const auto LimbsA = Candidates(AIt->second, LimbA);
        const auto LimbsB = Candidates(BIt->second, LimbB);
        for (const auto& [IndexA, MeshA] : LimbsA)
        {
            (void)IndexA;
            const AABB BoxA = WorldMeshAabb(Kernel_, AIt->second, *MeshA);
            for (const auto& [IndexB, MeshB] : LimbsB)
            {
                if (AabbIntersect(BoxA, WorldMeshAabb(Kernel_, BIt->second, *MeshB)))
                    return LimbB < 0 ? IndexB : 1;
            }
        }
        return -1;
    }

    int ObjectManager::LimbHit(int ObjectA, int LimbA, int ObjectB, int LimbB)
    {
        if (Objects_.find(ObjectA) == Objects_.end())
            return -1;
        const int Overlap = LimbCollision(ObjectA, LimbA, ObjectB, LimbB);
        int& Stored = LimbHitHistory_[ObjectA];
        const int Previous = Stored - 1;
        int Result = -1;
        if (Overlap != -1 && Previous != Overlap)
            Result = Overlap;
        Stored = Overlap + 1;
        return Result;
    }

    bool ObjectManager::SetObjectCollisionOff(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "SET OBJECT COLLISION OFF", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->CollisionEnabled = false;
        ObjectHitHistory_.erase(ObjectId);
        return true;
    }

    bool ObjectManager::SetObjectCollisionOn(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "SET OBJECT COLLISION ON", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->CollisionEnabled = true;
        return true;
    }

    bool ObjectManager::SetObjectCollisionToBoxes(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT COLLISION TO BOXES", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.CollisionShapeValue = CollisionShape::Aabb;
        Object.CollisionBoxRotates = true;
        if (!Object.CollisionAabbExplicit)
            Object.CollisionAabb = ComputeObjectAabb(Kernel_, Object);
        return true;
    }

    bool ObjectManager::SetObjectCollisionToSpheres(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "SET OBJECT COLLISION TO SPHERES", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->CollisionShapeValue = CollisionShape::Sphere;
        return true;
    }

    bool ObjectManager::SetObjectCollisionToPolygons(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "SET OBJECT COLLISION TO POLYGONS", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->CollisionShapeValue = CollisionShape::Polygon;
        return true;
    }

    bool ObjectManager::MakeObjectCollisionBox(int ObjectId, float X1, float Y1, float Z1, float X2, float Y2, float Z2,
                                               int RotatedFlag, std::string* Error)
    {
        if (RotatedFlag != 0 && RotatedFlag != 1)
        {
            if (Error)
                *Error = "MAKE OBJECT COLLISION BOX rotated flag must be zero or one";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "MAKE OBJECT COLLISION BOX", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.CollisionAabb = {
            std::min(X1, X2), std::min(Y1, Y2), std::min(Z1, Z2), std::max(X1, X2), std::max(Y1, Y2), std::max(Z1, Z2),
        };
        Object.CollisionAabbExplicit = true;
        Object.CollisionBoxRotates = RotatedFlag == 1;
        Object.CollisionShapeValue = CollisionShape::Aabb;
        return true;
    }

    bool ObjectManager::DeleteObjectCollisionBox(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "DELETE OBJECT COLLISION BOX", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.CollisionAabbExplicit = false;
        Object.CollisionAabb = {};
        Object.CollisionBoxRotates = false;
        return true;
    }

    float ObjectManager::GetObjectCollisionX(int ObjectId)
    {
        const int Requested = ObjectId > 0 ? ObjectId : LastCollisionObject_;
        const auto Feedback = CollisionFeedback_.find(Requested);
        return Feedback != CollisionFeedback_.end() ? Feedback->second.X : 0.0f;
    }

    float ObjectManager::GetObjectCollisionY(int ObjectId)
    {
        const int Requested = ObjectId > 0 ? ObjectId : LastCollisionObject_;
        const auto Feedback = CollisionFeedback_.find(Requested);
        return Feedback != CollisionFeedback_.end() ? Feedback->second.Y : 0.0f;
    }

    float ObjectManager::GetObjectCollisionZ(int ObjectId)
    {
        const int Requested = ObjectId > 0 ? ObjectId : LastCollisionObject_;
        const auto Feedback = CollisionFeedback_.find(Requested);
        return Feedback != CollisionFeedback_.end() ? Feedback->second.Z : 0.0f;
    }

    bool ObjectManager::PickObject(int& Output, int ScreenX, int ScreenY, int ObjectStart, int ObjectEnd,
                                   int IgnoreCamera, std::string* Error)
    {
        if (ObjectStart > ObjectEnd)
        {
            if (Error)
                *Error = "PICK OBJECT requires the first object number to be no greater than the last";
            return false;
        }
        if (IgnoreCamera != 0 && IgnoreCamera != 1)
        {
            if (Error)
                *Error = "PICK OBJECT ignore-camera flag must be zero or one";
            return false;
        }

        const Vec3 Direction = ScreenPickDirection(Kernel_, ScreenX, ScreenY, IgnoreCamera == 1);
        const Vec3 Origin = IgnoreCamera == 1 ? Vec3{}
                                              : Vec3{Kernel_.Camera().PositionX(), Kernel_.Camera().PositionY(),
                                                     Kernel_.Camera().PositionZ()};
        const Vec3 End{Origin.X + Direction.X * 15000.0f, Origin.Y + Direction.Y * 15000.0f,
                       Origin.Z + Direction.Z * 15000.0f};
        int BestObject = 0;
        float BestDistance = std::numeric_limits<float>::max();
        for (const auto& [Id, Object] : Objects_)
        {
            if (Id < ObjectStart || Id > ObjectEnd || !Object.Visible || Object.LockToScreen != (IgnoreCamera == 1))
                continue;
            const float Distance = RayObjectDistance(Kernel_, Object, Origin, End);
            if (Distance > 0.0f && Distance < BestDistance)
            {
                BestDistance = Distance;
                BestObject = Id;
            }
        }
        if (BestObject > 0)
        {
            PickDistance_ = BestDistance;
            PickVectorX_ = Direction.X * BestDistance;
            PickVectorY_ = Direction.Y * BestDistance;
            PickVectorZ_ = Direction.Z * BestDistance;
        }
        {
            Output = BestObject;
            return true;
        }
    }

    void ObjectManager::PickScreen(int ScreenX, int ScreenY, float Distance)
    {
        const Vec3 Direction = ScreenPickDirection(Kernel_, ScreenX, ScreenY);
        PickDistance_ = Distance;
        PickVectorX_ = Direction.X * Distance;
        PickVectorY_ = Direction.Y * Distance;
        PickVectorZ_ = Direction.Z * Distance;
    }

    float ObjectManager::GetPickDistance() const
    {
        return PickDistance_;
    }

    float ObjectManager::GetPickVectorX() const
    {
        return PickVectorX_;
    }

    float ObjectManager::GetPickVectorY() const
    {
        return PickVectorY_;
    }

    float ObjectManager::GetPickVectorZ() const
    {
        return PickVectorZ_;
    }

    // ── Static collision ─────────────────────────────────────────────

    bool ObjectManager::MakeStaticCollisionBox(float X1, float Y1, float Z1, float X2, float Y2, float Z2,
                                               std::string* Error)
    {
        if (StaticBoxes_.size() >= PortableObjectLimit)
        {
            if (Error)
                *Error = "MAKE STATIC COLLISION BOX exceeded the 65535 box limit";
            return false;
        }
        if (!std::isfinite(X1) || !std::isfinite(Y1) || !std::isfinite(Z1) || !std::isfinite(X2) ||
            !std::isfinite(Y2) || !std::isfinite(Z2))
        {
            if (Error)
                *Error = "MAKE STATIC COLLISION BOX requires finite coordinates";
            return false;
        }
        StaticBoxes_.push_back({
            std::min(X1, X2),
            std::min(Y1, Y2),
            std::min(Z1, Z2),
            std::max(X1, X2),
            std::max(Y1, Y2),
            std::max(Z1, Z2),
        });
        return true;
    }

    bool ObjectManager::MakeStaticObject(int ObjectId, std::string* Error)
    {
        const Object3D* ObjectPointer = RequireObject(ObjectId, "MAKE STATIC OBJECT", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        if (StaticObjects_.size() >= PortableObjectLimit)
        {
            if (Error)
                *Error = "MAKE STATIC OBJECT exceeded the 65535 object limit";
            return false;
        }
        StaticObjects_.push_back(StaticSnapshot(Object));
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    std::vector<AABB> ObjectManager::CurrentStaticCollisionBoxes() const
    {
        std::vector<AABB> Boxes = StaticBoxes_;
        Boxes.reserve(Boxes.size() + StaticObjects_.size() + AttachedStaticObjectIds_.size());
        for (const Object3D& Object : StaticObjects_)
            Boxes.push_back(WorldObjectAabb(Kernel_, Object));
        for (const int Id : AttachedStaticObjectIds_)
        {
            if (const auto It = Objects_.find(Id); It != Objects_.end())
                Boxes.push_back(WorldObjectAabb(Kernel_, It->second));
        }
        return Boxes;
    }

    bool ObjectManager::AttachObjectToStatic(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "ATTACH OBJECT TO STATIC", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        AttachedStaticObjectIds_.insert(ObjectId);
        Object.TransformDirty = true;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::DetachObjectFromStatic(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "DETACH OBJECT FROM STATIC", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        AttachedStaticObjectIds_.erase(ObjectId);
        Object.TransformDirty = true;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    void ObjectManager::DeleteStaticObjects()
    {
        StaticBoxes_.clear();
        StaticObjects_.clear();
        AttachedStaticObjectIds_.clear();
        Kernel_.Renderer().InvalidateTriCache();
    }

    void ObjectManager::EnableStaticOcclusion(bool Enabled)
    {
        if (StaticOcclusionEnabled_ == Enabled)
            return;
        StaticOcclusionEnabled_ = Enabled;
        Kernel_.Renderer().InvalidateTriCache();
    }

    bool ObjectManager::StaticOcclusionEnabled() const
    {
        return StaticOcclusionEnabled_;
    }

    bool ObjectManager::IsStaticRenderObject(int ObjectId) const
    {
        return ObjectId < 0 || AttachedStaticObjectIds_.contains(ObjectId);
    }

    bool ObjectManager::SetStaticObjectsTexture(int AddressMode, std::string* Error)
    {
        if (AddressMode < 0 || AddressMode > 2)
        {
            if (Error)
                *Error = "SET STATIC OBJECTS TEXTURE mode must be between zero and two";
            return false;
        }
        const auto Apply = [&](Object3D& Object)
        {
            for (Mesh& MeshValue : Object.Meshes)
                MeshValue.TextureStageAddressModes[0] = AddressMode;
            Object.TransformDirty = true;
        };
        for (Object3D& Object : StaticObjects_)
            Apply(Object);
        for (const int Id : AttachedStaticObjectIds_)
        {
            if (auto It = Objects_.find(Id); It != Objects_.end())
                Apply(It->second);
        }
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    void ObjectManager::SetStaticObjectsWireframe(bool Enabled)
    {
        const auto Apply = [&](Object3D& Object)
        {
            Object.Wireframe = Enabled;
            Object.TransformDirty = true;
        };
        for (Object3D& Object : StaticObjects_)
            Apply(Object);
        for (const int Id : AttachedStaticObjectIds_)
        {
            if (auto It = Objects_.find(Id); It != Objects_.end())
                Apply(It->second);
        }
        Kernel_.Renderer().InvalidateTriCache();
    }

    void ObjectManager::SetGlobalCollision(bool Enabled)
    {
        GlobalCollisionEnabled_ = Enabled;
        ObjectHitHistory_.clear();
        LimbHitHistory_.clear();
    }

    bool ObjectManager::GlobalCollisionEnabled() const
    {
        return GlobalCollisionEnabled_;
    }

    bool ObjectManager::StaticLineOfSight(float& Output, float SourceX, float SourceY, float SourceZ,
                                          float DestinationX, float DestinationY, float DestinationZ, float Width,
                                          float Accuracy, std::string* Error)
    {
        if (!std::isfinite(Width) || Width < 0.0f)
        {
            if (Error)
                *Error = "STATIC LINE OF SIGHT width must be a finite non-negative value";
            return false;
        }
        if (!std::isfinite(Accuracy) || Accuracy <= 0.0f)
        {
            if (Error)
                *Error = "STATIC LINE OF SIGHT accuracy must be a finite positive value";
            return false;
        }
        const Vec3 Source{SourceX, SourceY, SourceZ};
        const Vec3 Destination{DestinationX, DestinationY, DestinationZ};
        float BestT = 2.0f;
        Vec3 BestHit{};
        for (const AABB& SourceBox : CurrentStaticCollisionBoxes())
        {
            const AABB SweptBox{SourceBox.MinX - Width, SourceBox.MinY - Width, SourceBox.MinZ - Width,
                                SourceBox.MaxX + Width, SourceBox.MaxY + Width, SourceBox.MaxZ + Width};
            float T = 0.0f;
            Vec3 Hit{};
            if (SegmentAabbIntersect(Source, Destination, SweptBox, T, Hit) && T < BestT)
            {
                BestT = T;
                BestHit = Hit;
            }
        }
        if (BestT > 1.0f)
        {
            Output = 0.0f;
            return true;
        }
        StaticHitX_ = BestHit.X;
        StaticHitY_ = BestHit.Y;
        StaticHitZ_ = BestHit.Z;
        {
            Output = 1.0f;
            return true;
        }
    }

    float ObjectManager::StaticLineOfSightX() const
    {
        return StaticHitX_;
    }

    float ObjectManager::StaticLineOfSightY() const
    {
        return StaticHitY_;
    }

    float ObjectManager::StaticLineOfSightZ() const
    {
        return StaticHitZ_;
    }

    bool ObjectManager::SaveStaticObjects(const std::string& Path, std::string* Error) const
    {
        static constexpr std::array<char, 8> Magic{'O', 'D', 'B', 'S', 'T', 'A', '0', '1'};
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        const std::filesystem::path ArchiveDirectory = dbc::PathFromUtf8(Resolved).parent_path();
        const auto RelativeTexture = [&](int ImageId)
        {
            const std::string Source = TextureSourcePath(Kernel_, ImageId);
            if (Source.empty())
                return std::string{};
            std::error_code OperationError;
            const auto Root = std::filesystem::weakly_canonical(ArchiveDirectory, OperationError);
            if (OperationError)
                return std::string{};
            const auto Candidate = std::filesystem::weakly_canonical(dbc::PathFromUtf8(Source), OperationError);
            if (OperationError)
                return std::string{};
            const auto Relative = Candidate.lexically_relative(Root);
            if (Relative.empty() || Relative.is_absolute())
                return std::string{};
            for (const auto& Part : Relative)
                if (Part == "..")
                    return std::string{};
            return dbc::GenericPathToUtf8(Relative);
        };

        std::vector<Object3D> Objects;
        Objects.reserve(StaticObjects_.size() + AttachedStaticObjectIds_.size());
        for (const Object3D& Object : StaticObjects_)
            Objects.push_back(StaticSnapshot(Object));
        for (const int Id : AttachedStaticObjectIds_)
            if (const auto It = Objects_.find(Id); It != Objects_.end())
                Objects.push_back(StaticSnapshot(It->second));
        if (Objects.size() > PortableObjectLimit || StaticBoxes_.size() > PortableObjectLimit)
        {
            if (Error)
                *Error = "SAVE STATIC OBJECTS exceeded the 65535 record limit";
            return false;
        }

        PortableWriter Writer;
        Writer.Bytes(Magic.data(), Magic.size());
        Writer.U32(1);
        Writer.U32(static_cast<uint32_t>(StaticBoxes_.size()));
        for (const AABB& Box : StaticBoxes_)
        {
            Writer.Float(Box.MinX);
            Writer.Float(Box.MinY);
            Writer.Float(Box.MinZ);
            Writer.Float(Box.MaxX);
            Writer.Float(Box.MaxY);
            Writer.Float(Box.MaxZ);
        }
        Writer.U32(static_cast<uint32_t>(Objects.size()));
        for (const Object3D& Object : Objects)
        {
            Writer.Float(Object.PosX);
            Writer.Float(Object.PosY);
            Writer.Float(Object.PosZ);
            Writer.Float(Object.RotX);
            Writer.Float(Object.RotY);
            Writer.Float(Object.RotZ);
            Writer.Float(Object.ScaleX);
            Writer.Float(Object.ScaleY);
            Writer.Float(Object.ScaleZ);
            Writer.Float(Object.TintR);
            Writer.Float(Object.TintG);
            Writer.Float(Object.TintB);
            Writer.Float(Object.Alpha);
            Writer.Float(Object.FadeNormalScale);
            Writer.Float(Object.TexScrollU);
            Writer.Float(Object.TexScrollV);
            Writer.Float(Object.TexScaleU);
            Writer.Float(Object.TexScaleV);
            Writer.U8(Object.Visible);
            Writer.U8(Object.Wireframe);
            Writer.U8(Object.TransparentBlack);
            Writer.U8(Object.Cull);
            Writer.U8(Object.Ghosted);
            Writer.U8(Object.DepthTest);
            Writer.U8(Object.DepthWrite);
            Writer.U8(Object.UseLighting);
            Writer.U8(Object.UseFog);
            Writer.U8(Object.UseAmbient);
            Writer.U8(Object.TextureFilter);
            Writer.U8(Object.AlphaMapping);
            Writer.I32(Object.TransparencyMode);
            Writer.I32(Object.GhostMode);
            Writer.Float(Object.AlphaMappingAlpha);
            Writer.U32(Object.DiffuseColor);
            Writer.U32(Object.AmbientColor);
            Writer.U32(Object.SpecularColor);
            Writer.U32(Object.EmissiveColor);
            Writer.Float(Object.SpecularPower);
            Writer.String(RelativeTexture(Object.TextureImageId));

            if (Object.LimbTransforms.size() > PortableLimbLimit || Object.Meshes.size() > PortableMeshLimit)
            {
                if (Error)
                    *Error = "SAVE STATIC OBJECTS object exceeds mesh or limb limits";
                return false;
            }
            Writer.U32(static_cast<uint32_t>(Object.LimbTransforms.size()));
            for (const LimbTransform& Transform : Object.LimbTransforms)
                if (!WriteLimbTransform(Writer, Transform, Error))
                    return false;
            Writer.U32(static_cast<uint32_t>(Object.Meshes.size()));
            for (const Mesh& MeshValue : Object.Meshes)
            {
                if (MeshValue.Geometry().Vertices.size() > PortableVertexLimit ||
                    MeshValue.Geometry().Indices.size() > PortableIndexLimit)
                {
                    if (Error)
                        *Error = "SAVE STATIC OBJECTS mesh exceeds vertex or index limits";
                    return false;
                }
                const aiMatrix4x4& Matrix = MeshValue.Geometry().BindTransform;
                for (float Value :
                     {Matrix.a1, Matrix.a2, Matrix.a3, Matrix.a4, Matrix.b1, Matrix.b2, Matrix.b3, Matrix.b4, Matrix.c1,
                      Matrix.c2, Matrix.c3, Matrix.c4, Matrix.d1, Matrix.d2, Matrix.d3, Matrix.d4})
                    Writer.Float(Value);
                Writer.I32(MeshValue.SourceMeshIndex);
                Writer.U8(MeshValue.Visible);
                Writer.String(RelativeTexture(MeshValue.TextureImageId));
                const auto Address = MeshValue.TextureStageAddressModes.find(0);
                const auto Mip = MeshValue.TextureStageMipModes.find(0);
                Writer.I32(Address == MeshValue.TextureStageAddressModes.end() ? 0 : Address->second);
                Writer.I32(Mip == MeshValue.TextureStageMipModes.end() ? 2 : Mip->second);
                Writer.Float(MeshValue.MaterialColor.r);
                Writer.Float(MeshValue.MaterialColor.g);
                Writer.Float(MeshValue.MaterialColor.b);
                Writer.Float(MeshValue.MaterialColor.a);
                Writer.Float(MeshValue.LimbTint.r);
                Writer.Float(MeshValue.LimbTint.g);
                Writer.Float(MeshValue.LimbTint.b);
                Writer.Float(MeshValue.LimbTint.a);
                Writer.Float(MeshValue.LimbRotX);
                Writer.Float(MeshValue.LimbRotY);
                Writer.Float(MeshValue.LimbRotZ);
                Writer.Float(MeshValue.LimbOffX);
                Writer.Float(MeshValue.LimbOffY);
                Writer.Float(MeshValue.LimbOffZ);
                Writer.Float(MeshValue.LimbScaleX);
                Writer.Float(MeshValue.LimbScaleY);
                Writer.Float(MeshValue.LimbScaleZ);
                Writer.U32(static_cast<uint32_t>(MeshValue.Geometry().Vertices.size()));
                for (std::size_t Index = 0; Index < MeshValue.Geometry().Vertices.size(); ++Index)
                {
                    const Vec3 Normal =
                        Index < MeshValue.Geometry().Normals.size() ? MeshValue.Geometry().Normals[Index] : Vec3{};
                    const SDL_FPoint Uv =
                        Index < MeshValue.Geometry().Uvs.size() ? MeshValue.Geometry().Uvs[Index] : SDL_FPoint{};
                    const uint32_t Diffuse = Index < MeshValue.Geometry().DiffuseColors.size()
                                                 ? MeshValue.Geometry().DiffuseColors[Index]
                                                 : 0xFFFFFFFFu;
                    Writer.Float(MeshValue.Geometry().Vertices[Index].X);
                    Writer.Float(MeshValue.Geometry().Vertices[Index].Y);
                    Writer.Float(MeshValue.Geometry().Vertices[Index].Z);
                    Writer.Float(Normal.X);
                    Writer.Float(Normal.Y);
                    Writer.Float(Normal.Z);
                    Writer.Float(Uv.x);
                    Writer.Float(Uv.y);
                    Writer.U32(Diffuse);
                }
                Writer.U32(static_cast<uint32_t>(MeshValue.Geometry().Indices.size()));
                for (uint32_t Index : MeshValue.Geometry().Indices)
                {
                    if (Index >= MeshValue.Geometry().Vertices.size())
                    {
                        if (Error)
                            *Error = "SAVE STATIC OBJECTS found an invalid mesh index";
                        return false;
                    }
                    Writer.U32(Index);
                }
            }
        }
        if (!Writer.Status(Error))
            return false;
        if (!WritePortableFile(Resolved, Writer.Data(), "SAVE STATIC OBJECTS", Error))
            return false;
        return true;
    }

    bool ObjectManager::LoadStaticObjects(const std::string& Path, std::string* Error)
    {
        static constexpr std::array<char, 8> Magic{'O', 'D', 'B', 'S', 'T', 'A', '0', '1'};
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        const std::filesystem::path ArchiveDirectory = dbc::PathFromUtf8(Resolved).parent_path();
        const auto LoadTexture = [&](const std::string& Relative, int& TextureId)
        {
            if (Relative.empty())
            {
                TextureId = 0;
                return true;
            }
            const std::filesystem::path Input = dbc::PathFromUtf8(Relative);
            if (Input.is_absolute())
            {
                if (Error)
                    *Error = "LOAD STATIC OBJECTS rejects absolute texture paths";
                return false;
            }
            const std::filesystem::path Candidate = (ArchiveDirectory / Input).lexically_normal();
            std::error_code OperationError;
            const std::filesystem::path Root = std::filesystem::weakly_canonical(ArchiveDirectory, OperationError);
            if (OperationError)
            {
                if (Error)
                    *Error = "LOAD STATIC OBJECTS cannot resolve the archive directory";
                return false;
            }
            const std::filesystem::path CanonicalCandidate =
                std::filesystem::weakly_canonical(Candidate, OperationError);
            if (OperationError)
            {
                TextureId = 0;
                return true;
            }
            const std::filesystem::path Rel = CanonicalCandidate.lexically_relative(Root);
            if (Rel.empty() || Rel.is_absolute())
            {
                if (Error)
                    *Error = "LOAD STATIC OBJECTS texture path escapes the archive directory";
                return false;
            }
            for (const auto& Part : Rel)
                if (Part == "..")
                {
                    if (Error)
                        *Error = "LOAD STATIC OBJECTS texture path escapes the archive directory";
                    return false;
                }
            if (!std::filesystem::is_regular_file(CanonicalCandidate, OperationError) || OperationError)
            {
                TextureId = 0;
                return true;
            }
            TextureId = Kernel_.ImageStoreRef().LoadImageInternalFromPath(dbc::PathToUtf8(CanonicalCandidate));
            return true;
        };

        std::vector<uint8_t> Data;
        if (!ReadPortableFile(Data, Resolved, "LOAD STATIC OBJECTS", Error))
            return false;
        PortableReader Reader(Data, "LOAD STATIC OBJECTS");
        Reader.Expect(Magic);
        if (!Reader.Status(Error))
            return false;
        if (Reader.U32() != 1)
        {
            if (Error)
                *Error = "LOAD STATIC OBJECTS version is unsupported";
            return false;
        }
        const uint32_t BoxCount = Reader.U32();
        if (BoxCount > PortableObjectLimit)
        {
            if (Error)
                *Error = "LOAD STATIC OBJECTS has too many collision boxes";
            return false;
        }
        std::vector<AABB> Boxes;
        Boxes.reserve(BoxCount);
        for (uint32_t Index = 0; Index < BoxCount; ++Index)
        {
            const AABB Box{Reader.Float(), Reader.Float(), Reader.Float(),
                           Reader.Float(), Reader.Float(), Reader.Float()};
            if (Box.MinX > Box.MaxX || Box.MinY > Box.MaxY || Box.MinZ > Box.MaxZ)
            {
                if (Error)
                    *Error = "LOAD STATIC OBJECTS found an inverted collision box";
                return false;
            }
            Boxes.push_back(Box);
        }

        const uint32_t ObjectCount = Reader.U32();
        if (ObjectCount > PortableObjectLimit)
        {
            if (Error)
                *Error = "LOAD STATIC OBJECTS has too many objects";
            return false;
        }
        std::vector<Object3D> Objects;
        Objects.reserve(ObjectCount);
        for (uint32_t ObjectIndex = 0; ObjectIndex < ObjectCount; ++ObjectIndex)
        {
            Object3D Object = NewObjectState();
            Object.PosX = Reader.Float();
            Object.PosY = Reader.Float();
            Object.PosZ = Reader.Float();
            Object.RotX = Reader.Float();
            Object.RotY = Reader.Float();
            Object.RotZ = Reader.Float();
            Object.ScaleX = Reader.Float();
            Object.ScaleY = Reader.Float();
            Object.ScaleZ = Reader.Float();
            Object.TintR = Reader.Float();
            Object.TintG = Reader.Float();
            Object.TintB = Reader.Float();
            Object.Alpha = Reader.Float();
            Object.FadeNormalScale = Reader.Float();
            Object.TexScrollU = Reader.Float();
            Object.TexScrollV = Reader.Float();
            Object.TexScaleU = Reader.Float();
            Object.TexScaleV = Reader.Float();
            Object.Visible = Reader.U8();
            Object.Wireframe = Reader.U8();
            Object.TransparentBlack = Reader.U8();
            Object.Cull = Reader.U8();
            Object.Ghosted = Reader.U8();
            Object.DepthTest = Reader.U8();
            Object.DepthWrite = Reader.U8();
            Object.UseLighting = Reader.U8();
            Object.UseFog = Reader.U8();
            Object.UseAmbient = Reader.U8();
            Object.TextureFilter = Reader.U8();
            Object.AlphaMapping = Reader.U8();
            Object.TransparencyMode = Reader.I32();
            Object.GhostMode = Reader.I32();
            Object.AlphaMappingAlpha = Reader.Float();
            Object.DiffuseColor = Reader.U32();
            Object.AmbientColor = Reader.U32();
            Object.SpecularColor = Reader.U32();
            Object.EmissiveColor = Reader.U32();
            Object.SpecularPower = Reader.Float();
            if (!LoadTexture(Reader.String(), Object.TextureImageId))
                return false;
            Object.CollisionEnabled = false;

            const uint32_t LimbCount = Reader.U32();
            if (LimbCount > PortableLimbLimit)
            {
                if (Error)
                    *Error = "LOAD STATIC OBJECTS has too many limbs";
                return false;
            }
            Object.LimbTransforms.reserve(LimbCount);
            for (uint32_t Index = 0; Index < LimbCount; ++Index)
            {
                LimbTransform Transform = ReadLimbTransform(Reader);
                if (Transform.ParentLimb >= static_cast<int>(LimbCount))
                {
                    if (Error)
                        *Error = "LOAD STATIC OBJECTS found an invalid limb parent";
                    return false;
                }
                Object.LimbTransforms.push_back(Transform);
            }
            const uint32_t MeshCount = Reader.U32();
            if (MeshCount > PortableMeshLimit)
            {
                if (Error)
                    *Error = "LOAD STATIC OBJECTS has too many meshes";
                return false;
            }
            Object.Meshes.reserve(MeshCount);
            for (uint32_t MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex)
            {
                Mesh MeshValue;
                MeshGeometry& Geometry = MeshValue.EditGeometry();
                Geometry.BindTransform.a1 = Reader.Float();
                Geometry.BindTransform.a2 = Reader.Float();
                Geometry.BindTransform.a3 = Reader.Float();
                Geometry.BindTransform.a4 = Reader.Float();
                Geometry.BindTransform.b1 = Reader.Float();
                Geometry.BindTransform.b2 = Reader.Float();
                Geometry.BindTransform.b3 = Reader.Float();
                Geometry.BindTransform.b4 = Reader.Float();
                Geometry.BindTransform.c1 = Reader.Float();
                Geometry.BindTransform.c2 = Reader.Float();
                Geometry.BindTransform.c3 = Reader.Float();
                Geometry.BindTransform.c4 = Reader.Float();
                Geometry.BindTransform.d1 = Reader.Float();
                Geometry.BindTransform.d2 = Reader.Float();
                Geometry.BindTransform.d3 = Reader.Float();
                Geometry.BindTransform.d4 = Reader.Float();
                MeshValue.SourceMeshIndex = Reader.I32();
                MeshValue.Visible = Reader.U8();
                if (!LoadTexture(Reader.String(), MeshValue.TextureImageId))
                    return false;
                const int AddressMode = Reader.I32();
                const int MipMode = Reader.I32();
                if (AddressMode < 0 || AddressMode > 5 ||
                    (MipMode != 0 && MipMode != 1 && MipMode != 2 && MipMode != 3 && MipMode != 6 && MipMode != 7))
                {
                    if (Error)
                        *Error = "LOAD STATIC OBJECTS found invalid texture sampler state";
                    return false;
                }
                MeshValue.TextureStageAddressModes[0] = AddressMode;
                MeshValue.TextureStageMipModes[0] = MipMode;
                MeshValue.MaterialColor = {Reader.Float(), Reader.Float(), Reader.Float(), Reader.Float()};
                MeshValue.LimbTint = {Reader.Float(), Reader.Float(), Reader.Float(), Reader.Float()};
                MeshValue.LimbRotX = Reader.Float();
                MeshValue.LimbRotY = Reader.Float();
                MeshValue.LimbRotZ = Reader.Float();
                MeshValue.LimbOffX = Reader.Float();
                MeshValue.LimbOffY = Reader.Float();
                MeshValue.LimbOffZ = Reader.Float();
                MeshValue.LimbScaleX = Reader.Float();
                MeshValue.LimbScaleY = Reader.Float();
                MeshValue.LimbScaleZ = Reader.Float();
                const uint32_t VertexCount = Reader.U32();
                if (VertexCount > PortableVertexLimit)
                {
                    if (Error)
                        *Error = "LOAD STATIC OBJECTS mesh has too many vertices";
                    return false;
                }
                Geometry.Vertices.reserve(VertexCount);
                Geometry.Normals.reserve(VertexCount);
                Geometry.Uvs.reserve(VertexCount);
                Geometry.DiffuseColors.reserve(VertexCount);
                for (uint32_t Index = 0; Index < VertexCount; ++Index)
                {
                    Geometry.Vertices.push_back({Reader.Float(), Reader.Float(), Reader.Float()});
                    Geometry.Normals.push_back({Reader.Float(), Reader.Float(), Reader.Float()});
                    Geometry.Uvs.push_back({Reader.Float(), Reader.Float()});
                    Geometry.DiffuseColors.push_back(Reader.U32());
                }
                const uint32_t IndexCount = Reader.U32();
                if (IndexCount > PortableIndexLimit)
                {
                    if (Error)
                        *Error = "LOAD STATIC OBJECTS mesh has too many indices";
                    return false;
                }
                Geometry.Indices.reserve(IndexCount);
                for (uint32_t Index = 0; Index < IndexCount; ++Index)
                {
                    const uint32_t Value = Reader.U32();
                    if (Value >= VertexCount)
                    {
                        if (Error)
                            *Error = "LOAD STATIC OBJECTS found an invalid mesh index";
                        return false;
                    }
                    Geometry.Indices.push_back(Value);
                }
                Object.Meshes.push_back(std::move(MeshValue));
            }
            Object.TransformDirty = true;
            Objects.push_back(std::move(Object));
        }
        if (!Reader.Status(Error))
            return false;
        if (!Reader.AtEnd())
        {
            if (Error)
                *Error = "LOAD STATIC OBJECTS found trailing data";
            return false;
        }
        StaticBoxes_ = std::move(Boxes);
        StaticObjects_ = std::move(Objects);
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    float ObjectManager::GetStaticCollisionHit(float OldX, float OldY, float OldZ, float NewX, float NewY, float NewZ)
    {
        const Vec3 P0 = {OldX, OldY, OldZ};
        const Vec3 P1 = {NewX, NewY, NewZ};
        float BestT = 2.0f;
        Vec3 BestHit{};
        for (const AABB& Box : CurrentStaticCollisionBoxes())
        {
            float T = 0.0f;
            Vec3 Hit{};
            if (SegmentAabbIntersect(P0, P1, Box, T, Hit) && T < BestT)
            {
                BestT = T;
                BestHit = Hit;
            }
        }
        if (BestT <= 1.0f)
        {
            StaticHitX_ = BestHit.X;
            StaticHitY_ = BestHit.Y;
            StaticHitZ_ = BestHit.Z;
            return 1.0f;
        }
        return 0.0f;
    }

    float ObjectManager::GetStaticCollisionHitBox(float OldX1, float OldY1, float OldZ1, float OldX2, float OldY2,
                                                  float OldZ2, float NewX1, float NewY1, float NewZ1, float NewX2,
                                                  float NewY2, float NewZ2)
    {
        const AABB OldBox{std::min(OldX1, OldX2), std::min(OldY1, OldY2), std::min(OldZ1, OldZ2),
                          std::max(OldX1, OldX2), std::max(OldY1, OldY2), std::max(OldZ1, OldZ2)};
        const AABB NewBox{std::min(NewX1, NewX2), std::min(NewY1, NewY2), std::min(NewZ1, NewZ2),
                          std::max(NewX1, NewX2), std::max(NewY1, NewY2), std::max(NewZ1, NewZ2)};
        const auto Overlaps = [](float AMin, float AMax, float BMin, float BMax) { return AMin < BMax && AMax > BMin; };

        bool Hit = false;
        float PushX = 0.0f, PushY = 0.0f, PushZ = 0.0f;
        for (const AABB& Box : CurrentStaticCollisionBoxes())
        {
            if (!Overlaps(NewBox.MinX, NewBox.MaxX, Box.MinX, Box.MaxX) ||
                !Overlaps(NewBox.MinY, NewBox.MaxY, Box.MinY, Box.MaxY) ||
                !Overlaps(NewBox.MinZ, NewBox.MaxZ, Box.MinZ, Box.MaxZ))
            {
                continue;
            }
            Hit = true;
            // Push back only along the axes the move itself brought into contact, in the direction
            // the box came from; an axis that already overlapped before the move is left alone.
            const auto AxisPush =
                [&](float OldMin, float OldMax, float NewMin, float NewMax, float BMin, float BMax, float& Push)
            {
                if (Overlaps(OldMin, OldMax, BMin, BMax))
                    return;
                const float Moved = NewMin - OldMin;
                if (Moved > 0.0f)
                    Push = std::min(Push, BMin - NewMax);
                else if (Moved < 0.0f)
                    Push = std::max(Push, BMax - NewMin);
            };
            AxisPush(OldBox.MinX, OldBox.MaxX, NewBox.MinX, NewBox.MaxX, Box.MinX, Box.MaxX, PushX);
            AxisPush(OldBox.MinY, OldBox.MaxY, NewBox.MinY, NewBox.MaxY, Box.MinY, Box.MaxY, PushY);
            AxisPush(OldBox.MinZ, OldBox.MaxZ, NewBox.MinZ, NewBox.MaxZ, Box.MinZ, Box.MaxZ, PushZ);
        }
        if (!Hit)
            return 0.0f;
        StaticHitX_ = PushX;
        StaticHitY_ = PushY;
        StaticHitZ_ = PushZ;
        return 1.0f;
    }

    float ObjectManager::GetStaticCollisionX()
    {
        return StaticHitX_;
    }

    float ObjectManager::GetStaticCollisionY()
    {
        return StaticHitY_;
    }

    float ObjectManager::GetStaticCollisionZ()
    {
        return StaticHitZ_;
    }

    // ── Texture / appearance ─────────────────────────────────────────

    void ObjectManager::TextureObject(int ObjectId, int ImageId)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.TransformDirty = true;
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (ImageId > 0)
            {
                Kernel_.ImageStoreRef().WarnMissingImage("TextureObject", ImageId);
            }
            It->second.TextureImageId = 0;
            return;
        }
        It->second.TextureImageId = ImageId;
    }

    bool ObjectManager::SetObjectTexture(int ObjectId, int Stage, int AddressMode, int MipMode, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT TEXTURE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (Stage < 0)
        {
            if (Error)
                *Error = "SET OBJECT TEXTURE stage must not be negative";
            return false;
        }
        if (AddressMode < 0 || AddressMode > 5)
        {
            if (Error)
                *Error = "SET OBJECT TEXTURE address mode must be between 0 and 5";
            return false;
        }
        if (MipMode != 0 && MipMode != 1 && MipMode != 2 && MipMode != 3 && MipMode != 6 && MipMode != 7)
        {
            if (Error)
                *Error = "SET OBJECT TEXTURE mip mode must be 0, 1, 2, 3, 6 or 7";
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageAddressModes[Stage] = AddressMode;
            MeshValue.TextureStageMipModes[Stage] = MipMode;
        }
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetLightMappingOn(int ObjectId, int ImageId, int ReverseStageMode, std::string* Error)
    {
        if (ReverseStageMode != 0 && ReverseStageMode != 1)
        {
            if (Error)
                *Error = "SET LIGHT MAPPING ON reverse stage mode must be zero or one";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET LIGHT MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "SET LIGHT MAPPING ON requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[1] = ImageId;
            MeshValue.TextureStageBlendModes[1] = ReverseStageMode == 0 ? 4 : 27;
            MeshValue.TextureStageCoordinateModes[1] = 3;
            MeshValue.TextureStageCubeModes.erase(1);
        }
        Object.CubeGpuTexture.reset();
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetDetailMappingOn(int ObjectId, int ImageId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET DETAIL MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "SET DETAIL MAPPING ON requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[1] = ImageId;
            MeshValue.TextureStageBlendModes[1] = 7;
            MeshValue.TextureStageCoordinateModes[1] = 3;
            MeshValue.TextureStageCubeModes.erase(1);
        }
        Object.CubeGpuTexture.reset();
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetSphereMappingOn(int ObjectId, int ImageId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET SPHERE MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "SET SPHERE MAPPING ON requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[1] = ImageId;
            MeshValue.TextureStageBlendModes[1] = 4;
            MeshValue.TextureStageCoordinateModes[1] = 1;
            MeshValue.TextureStageCubeModes.erase(1);
        }
        Object.CubeGpuTexture.reset();
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetBlendMappingOn(int ObjectId, int Stage, int ImageId, int CoordinateMode, int BlendMode,
                                          std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET BLEND MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (Stage < 0 || Stage > 7)
        {
            if (Error)
                *Error = "SET BLEND MAPPING ON stage must be between zero and seven";
            return false;
        }
        const bool ValidCoordinateMode =
            (CoordinateMode >= 0 && CoordinateMode <= 3) || (CoordinateMode >= 10 && CoordinateMode <= 17);
        if (!ValidCoordinateMode)
        {
            if (Error)
                *Error = "SET BLEND MAPPING ON texture coordinate mode is invalid";
            return false;
        }
        if (BlendMode < 1 || BlendMode > 26)
        {
            if (Error)
                *Error = "SET BLEND MAPPING ON blend mode must be between one and twenty-six";
            return false;
        }
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "SET BLEND MAPPING ON requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[Stage] = ImageId;
            MeshValue.TextureStageBlendModes[Stage] = BlendMode;
            MeshValue.TextureStageCoordinateModes[Stage] = CoordinateMode;
            MeshValue.TextureStageCubeModes.erase(Stage);
        }
        if (Stage == 1)
            Object.CubeGpuTexture.reset();
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetCubeMappingOn(int ObjectId, const std::array<int, 6>& ImageIds, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET CUBE MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        std::array<SDL_Surface*, 6> Surfaces{};
        for (std::size_t Index = 0; Index < ImageIds.size(); ++Index)
        {
            const Image* CubeImage = Kernel_.ImageStoreRef().GetImage(ImageIds[Index]);
            if (ImageIds[Index] <= 0 || !CubeImage || !CubeImage->Surface)
            {
                if (Error)
                    *Error = "SET CUBE MAPPING ON requires existing image " + std::to_string(ImageIds[Index]);
                return false;
            }
            Surfaces[Index] = CubeImage->Surface;
        }
        SDL_GPUTexture* CubeTexture = Kernel_.Gpu().CreateGpuCubeTextureFromSurfaces(Surfaces);
        if (!CubeTexture)
        {
            if (Error)
                *Error = "SET CUBE MAPPING ON could not create the cube texture";
            return false;
        }
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        Object.CubeGpuTexture = std::shared_ptr<SDL_GPUTexture>(CubeTexture,
                                                                [Device](SDL_GPUTexture* Texture)
                                                                {
                                                                    if (Device && Texture)
                                                                        SDL_ReleaseGPUTexture(Device, Texture);
                                                                });
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds.erase(1);
            MeshValue.TextureStageBlendModes[1] = 4;
            MeshValue.TextureStageCoordinateModes[1] = 2;
            MeshValue.TextureStageCubeModes[1] = true;
        }
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetBumpMappingOn(int ObjectId, int ImageId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET BUMP MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "SET BUMP MAPPING ON requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[1] = ImageId;
            MeshValue.TextureStageBlendModes[1] = 0;
            MeshValue.TextureStageCoordinateModes[1] = 3;
            MeshValue.TextureStageCubeModes.erase(1);
            MeshValue.BuiltInEffect = 1;
            MeshValue.EffectId = -1;
        }
        Object.CubeGpuTexture.reset();
        Object.EffectId = 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetSurfaceMap(int ObjectId, int LimbId, SurfaceMap Map, int ImageId, const char* Command,
                                      std::string* Error, float NormalStrength)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, Command, Error);
        if (!ObjectPointer)
            return false;
        if (ImageId < 0 || (ImageId != 0 && Kernel_.ImageStoreRef().ImageExist(ImageId) == 0))
        {
            if (Error)
                *Error = std::string(Command) + " requires existing image " + std::to_string(ImageId);
            return false;
        }
        if (!std::isfinite(NormalStrength) || NormalStrength < 0.0f)
        {
            if (Error)
                *Error = std::string(Command) + " requires a strength that is not negative";
            return false;
        }
        const auto Apply = [&](Mesh& MeshValue)
        {
            MeshSurfaceMaps Maps = MeshValue.Maps.value_or(MeshSurfaceMaps{});
            switch (Map)
            {
                case SurfaceMap::Normal:
                    Maps.NormalImage = ImageId;
                    Maps.NormalStrength = NormalStrength;
                    break;
                case SurfaceMap::Specular:
                    Maps.SpecularImage = ImageId;
                    break;
                case SurfaceMap::Emissive:
                    Maps.EmissiveImage = ImageId;
                    break;
                case SurfaceMap::Reflection:
                    Maps.ReflectionImage = ImageId;
                    break;
            }
            MeshValue.Maps = Maps;
            if (!Maps.NormalImage && !Maps.SpecularImage && !Maps.EmissiveImage && !Maps.ReflectionImage &&
                !Maps.CubeMap)
                MeshValue.Maps.reset();
        };
        if (LimbId < 0)
            std::for_each(ObjectPointer->Meshes.begin(), ObjectPointer->Meshes.end(), Apply);
        else if (Mesh* Limb = Kernel_.Renderer().FindLimbMesh(*ObjectPointer, LimbId))
            Apply(*Limb);
        else
        {
            if (Error)
                *Error = std::string(Command) + " requires existing limb " + std::to_string(LimbId);
            return false;
        }
        ObjectPointer->TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetSurfaceCubeMap(int ObjectId, int LimbId, int CubeMapId, float Amount, float Fresnel,
                                          const char* Command, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, Command, Error);
        if (!ObjectPointer)
            return false;
        if (CubeMapId < 0 || (CubeMapId != 0 && CubeMapExist(CubeMapId) == 0))
        {
            if (Error)
                *Error = std::string(Command) + " requires existing cube map " + std::to_string(CubeMapId);
            return false;
        }
        if (!(Amount >= 0.0f && Amount <= 1.0f) || !(Fresnel >= 0.0f && Fresnel <= 1.0f))
        {
            if (Error)
                *Error = std::string(Command) + " requires an amount and a Fresnel term from 0 to 1";
            return false;
        }
        const auto Apply = [&](Mesh& MeshValue)
        {
            MeshSurfaceMaps Maps = MeshValue.Maps.value_or(MeshSurfaceMaps{});
            Maps.CubeMap = CubeMapId;
            Maps.ReflectionAmount = Amount;
            Maps.ReflectionFresnel = Fresnel;
            MeshValue.Maps = Maps;
            if (!Maps.NormalImage && !Maps.SpecularImage && !Maps.EmissiveImage && !Maps.ReflectionImage &&
                !Maps.CubeMap)
                MeshValue.Maps.reset();
        };
        if (LimbId < 0)
            std::for_each(ObjectPointer->Meshes.begin(), ObjectPointer->Meshes.end(), Apply);
        else if (Mesh* Limb = Kernel_.Renderer().FindLimbMesh(*ObjectPointer, LimbId))
            Apply(*Limb);
        else
        {
            if (Error)
                *Error = std::string(Command) + " requires existing limb " + std::to_string(LimbId);
            return false;
        }
        ObjectPointer->TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetNormalMapScroll(int ObjectId, std::span<const SDL_FPoint> Layers, std::string* Error)
    {
        constexpr const char* Command = "SET OBJECT NORMAL MAP SCROLL";
        Object3D* ObjectPointer = RequireObject(ObjectId, Command, Error);
        if (!ObjectPointer)
            return false;
        bool Found = false;
        for (Mesh& MeshValue : ObjectPointer->Meshes)
        {
            if (!MeshValue.Maps || MeshValue.Maps->NormalImage == 0)
                continue;
            Found = true;
            std::copy(Layers.begin(), Layers.end(), MeshValue.Maps->NormalScroll.begin());
            MeshValue.Maps->TwoNormalLayers = MeshValue.Maps->TwoNormalLayers || Layers.size() > 1;
        }
        if (!Found)
        {
            if (Error)
                *Error = std::string(Command) + " requires an object with a normal map";
            return false;
        }
        ObjectPointer->TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetNormalMapScale(int ObjectId, std::span<const SDL_FPoint> Layers, std::string* Error)
    {
        constexpr const char* Command = "SET OBJECT NORMAL MAP SCALE";
        Object3D* ObjectPointer = RequireObject(ObjectId, Command, Error);
        if (!ObjectPointer)
            return false;
        bool Found = false;
        for (Mesh& MeshValue : ObjectPointer->Meshes)
        {
            if (!MeshValue.Maps || MeshValue.Maps->NormalImage == 0)
                continue;
            Found = true;
            std::copy(Layers.begin(), Layers.end(), MeshValue.Maps->NormalScale.begin());
            MeshValue.Maps->TwoNormalLayers = MeshValue.Maps->TwoNormalLayers || Layers.size() > 1;
        }
        if (!Found)
        {
            if (Error)
                *Error = std::string(Command) + " requires an object with a normal map";
            return false;
        }
        ObjectPointer->TransformDirty = true;
        return true;
    }

    bool ObjectManager::ClearObjectMaps(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "CLEAR OBJECT MAPS", Error);
        if (!ObjectPointer)
            return false;
        for (Mesh& MeshValue : ObjectPointer->Meshes)
            MeshValue.Maps.reset();
        ObjectPointer->TransformDirty = true;
        return true;
    }

    bool ObjectManager::MakeCubeMap(int CubeMapId, const std::array<int, 6>& ImageIds, std::string* Error)
    {
        constexpr const char* Command = "MAKE CUBE MAP";
        if (CubeMapId <= 0 || CubeMaps_.contains(CubeMapId))
        {
            if (Error)
                *Error =
                    std::string(Command) + " requires a free cube map number above 0, not " + std::to_string(CubeMapId);
            return false;
        }
        std::array<SDL_Surface*, 6> Surfaces{};
        for (std::size_t Index = 0; Index < ImageIds.size(); ++Index)
        {
            const Image* Face = ImageIds[Index] > 0 ? Kernel_.ImageStoreRef().GetImage(ImageIds[Index]) : nullptr;
            if (!Face || !Face->Surface)
            {
                if (Error)
                    *Error = std::string(Command) + " requires existing image " + std::to_string(ImageIds[Index]);
                return false;
            }
            Surfaces[Index] = Face->Surface;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = std::string(Command) + " could not initialize the GPU renderer";
            return false;
        }
        return AddCubeMap(CubeMapId, Surfaces, Command, Error);
    }

    bool ObjectManager::LoadCubeMap(const std::string& Path, int CubeMapId, std::string* Error)
    {
        constexpr const char* Command = "LOAD CUBE MAP";
        if (CubeMapId <= 0 || CubeMaps_.contains(CubeMapId))
        {
            if (Error)
                *Error =
                    std::string(Command) + " requires a free cube map number above 0, not " + std::to_string(CubeMapId);
            return false;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = std::string(Command) + " could not initialize the GPU renderer";
            return false;
        }
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        using SurfacePointer = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
        const SurfacePointer Loaded(LoadSurface(Resolved), SDL_DestroySurface);
        if (!Loaded)
        {
            if (Error)
                *Error = std::string(Command) + " failed for " + Path + ": " + SDL_GetError();
            return false;
        }
        // Column and row of +X, -X, +Y, -Y, +Z, -Z. The cross unfolds the cube around +Z, seen from inside.
        const bool Cross = Loaded->w * 3 == Loaded->h * 4;
        const bool Strip = Loaded->w == Loaded->h * 6;
        if ((!Cross && !Strip) || Loaded->h <= 0)
        {
            if (Error)
                *Error = std::string(Command) + " requires a 4:3 cross or a 6:1 strip, not " +
                         std::to_string(Loaded->w) + "x" + std::to_string(Loaded->h);
            return false;
        }
        static constexpr std::array<std::array<int, 2>, 6> CrossCells{
            std::array<int, 2>{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}};
        const int Side = Cross ? Loaded->w / 4 : Loaded->h;
        std::vector<SurfacePointer> Faces;
        std::array<SDL_Surface*, 6> Surfaces{};
        for (std::size_t Face = 0; Face < Surfaces.size(); ++Face)
        {
            SDL_Rect Cell{static_cast<int>(Face) * Side, 0, Side, Side};
            if (Cross)
            {
                Cell.x = CrossCells[Face][0] * Side;
                Cell.y = CrossCells[Face][1] * Side;
            }
            Faces.emplace_back(SDL_CreateSurface(Side, Side, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            if (!Faces.back() || !SDL_SetSurfaceBlendMode(Loaded.get(), SDL_BLENDMODE_NONE) ||
                !SDL_BlitSurface(Loaded.get(), &Cell, Faces.back().get(), nullptr))
            {
                if (Error)
                    *Error = std::string(Command) + " failed for " + Path + ": " + SDL_GetError();
                return false;
            }
            Surfaces[Face] = Faces.back().get();
        }
        return AddCubeMap(CubeMapId, Surfaces, Command, Error);
    }

    bool ObjectManager::AddCubeMap(int CubeMapId, const std::array<SDL_Surface*, 6>& Faces, const char* Command,
                                   std::string* Error)
    {
        Uint32 Levels = 1;
        SDL_GPUTexture* Texture = Kernel_.Gpu().CreateGpuCubeTextureFromSurfaces(Faces, &Levels);
        if (!Texture)
        {
            if (Error)
                *Error = std::string(Command) + " could not create the cube texture: " + SDL_GetError();
            return false;
        }
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        CubeMap& Added = CubeMaps_[CubeMapId];
        Added.Levels = Levels;
        Added.Texture = std::shared_ptr<SDL_GPUTexture>(Texture,
                                                        [Device](SDL_GPUTexture* Value)
                                                        {
                                                            if (Device && Value)
                                                                SDL_ReleaseGPUTexture(Device, Value);
                                                        });
        return true;
    }

    bool ObjectManager::DeleteCubeMap(int CubeMapId, std::string* Error)
    {
        if (CubeMaps_.erase(CubeMapId) == 0)
        {
            if (Error)
                *Error = "DELETE CUBE MAP requires existing cube map " + std::to_string(CubeMapId);
            return false;
        }
        // The cached scene must not bind the released texture.
        ForEachObject(
            [&](int, Object3D& Object)
            {
                for (const Mesh& MeshValue : Object.Meshes)
                    if (MeshValue.Maps && MeshValue.Maps->CubeMap == CubeMapId)
                        Object.TransformDirty = true;
            });
        return true;
    }

    int ObjectManager::CubeMapExist(int CubeMapId) const
    {
        return CubeMaps_.contains(CubeMapId) ? 1 : 0;
    }

    const CubeMap* ObjectManager::FindCubeMap(int CubeMapId) const
    {
        const auto Found = CubeMaps_.find(CubeMapId);
        return Found != CubeMaps_.end() ? &Found->second : nullptr;
    }

    bool ObjectManager::SetCartoonShadingOn(int ObjectId, int ShadeImageId, int EdgeImageId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET CARTOON SHADING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (ShadeImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ShadeImageId) == 0)
        {
            if (Error)
                *Error = "SET CARTOON SHADING ON requires existing shade image " + std::to_string(ShadeImageId);
            return false;
        }
        if (EdgeImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(EdgeImageId) == 0)
        {
            if (Error)
                *Error = "SET CARTOON SHADING ON requires existing edge image " + std::to_string(EdgeImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[0] = ShadeImageId;
            MeshValue.TextureStageImageIds[1] = EdgeImageId;
            MeshValue.TextureStageBlendModes[0] = 2;
            MeshValue.TextureStageBlendModes[1] = 4;
            MeshValue.TextureStageAddressModes[0] = 3;
            MeshValue.TextureStageAddressModes[1] = 3;
            MeshValue.TextureStageCubeModes.erase(0);
            MeshValue.TextureStageCubeModes.erase(1);
            MeshValue.BuiltInEffect = 2;
            MeshValue.EffectId = -1;
        }
        Object.CubeGpuTexture.reset();
        Object.EffectId = 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetRainbowShadingOn(int ObjectId, int ImageId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET RAINBOW SHADING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (ImageId <= 0 || Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "SET RAINBOW SHADING ON requires existing image " + std::to_string(ImageId);
            return false;
        }
        for (Mesh& MeshValue : Object.Meshes)
        {
            MeshValue.TextureStageImageIds[0] = ImageId;
            MeshValue.TextureStageImageIds[1] = ImageId;
            MeshValue.TextureStageBlendModes[0] = 5;
            MeshValue.TextureStageBlendModes[1] = 8;
            MeshValue.TextureStageAddressModes[0] = 0;
            MeshValue.TextureStageAddressModes[1] = 0;
            MeshValue.TextureStageCubeModes.erase(0);
            MeshValue.TextureStageCubeModes.erase(1);
            MeshValue.BuiltInEffect = 3;
            MeshValue.EffectId = -1;
        }
        Object.CubeGpuTexture.reset();
        Object.EffectId = 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetReflectionShadingOn(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET REFLECTION SHADING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "SET REFLECTION SHADING ON could not initialize the GPU renderer";
            return false;
        }
        if (!Kernel_.Gpu().EnsureStencilPipelines())
        {
            if (Error)
                *Error = "SET REFLECTION SHADING ON requires a stencil-capable GPU depth target";
            return false;
        }
        Object.ShadowShading = false;
        Object.ShadowConfigured = false;
        Object.ShadowMeshId = 0;
        Object.ShadowRange = 2000.0f;
        Object.ShadowUseShader = false;
        Object.ShadowMeshes.clear();
        Object.ReflectionShading = true;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetShadowShadingOn(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET SHADOW SHADING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "SET SHADOW SHADING ON could not initialize the GPU renderer";
            return false;
        }
        if (!Kernel_.Gpu().EnsureStencilPipelines())
        {
            if (Error)
                *Error = "SET SHADOW SHADING ON requires a stencil-capable GPU depth target";
            return false;
        }
        if (!Object.ShadowConfigured)
        {
            Object.ShadowMeshId = 0;
            Object.ShadowRange = 2000.0f;
            Object.ShadowUseShader = false;
            Object.ShadowMeshes.clear();
            Object.ShadowConfigured = true;
        }
        Object.ReflectionShading = false;
        Object.ShadowShading = true;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetShadowShadingOn(int ObjectId, int MeshId, int ShadowRange, int UseShader, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET SHADOW SHADING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (MeshId < 0)
        {
            if (Error)
                *Error = "SET SHADOW SHADING ON mesh number must not be negative";
            return false;
        }
        if (ShadowRange < -1)
        {
            if (Error)
                *Error = "SET SHADOW SHADING ON range must be -1 or greater";
            return false;
        }
        std::vector<Mesh> ShadowMeshes;
        if (MeshId > 0)
        {
            const std::vector<Mesh>* Source = GetMeshes(MeshId);
            if (!Source || Source->empty())
            {
                if (Error)
                    *Error = "SET SHADOW SHADING ON requires existing mesh " + std::to_string(MeshId);
                return false;
            }
            ShadowMeshes = *Source;
        }
        if (!Kernel_.Core().EnsureInitialized())
        {
            if (Error)
                *Error = "SET SHADOW SHADING ON could not initialize the GPU renderer";
            return false;
        }
        if (!Kernel_.Gpu().EnsureStencilPipelines())
        {
            if (Error)
                *Error = "SET SHADOW SHADING ON requires a stencil-capable GPU depth target";
            return false;
        }
        Object.ReflectionShading = false;
        Object.ShadowShading = true;
        Object.ShadowConfigured = true;
        Object.ShadowMeshId = MeshId;
        Object.ShadowRange = ShadowRange == -1 ? (UseShader == 1 ? 1000.0f : 2000.0f) : static_cast<float>(ShadowRange);
        Object.ShadowUseShader = UseShader == 1;
        Object.ShadowMeshes = std::move(ShadowMeshes);
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetShadowShadingOff(int ObjectId, std::string* Error)
    {
        if (!SetShadingOff(ObjectId, Error))
            return false;
        return true;
    }

    bool ObjectManager::SetShadingOff(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET SHADING OFF", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.ReflectionShading = false;
        Object.ShadowShading = false;
        Object.ShadowConfigured = false;
        Object.ShadowMeshId = 0;
        Object.ShadowRange = 2000.0f;
        Object.ShadowUseShader = false;
        Object.ShadowMeshes.clear();
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectEffect(int ObjectId, int EffectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT EFFECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (EffectId != 0 && !Kernel_.Gpu().HasEffect(EffectId))
        {
            if (Error)
                *Error = "SET OBJECT EFFECT requires existing effect " + std::to_string(EffectId);
            return false;
        }
        Object.EffectId = EffectId;
        if (EffectId != 0)
        {
            for (Mesh& MeshValue : Object.Meshes)
                MeshValue.BuiltInEffect = 0;
        }
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetLimbEffect(int ObjectId, int LimbId, int EffectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET LIMB EFFECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SET LIMB EFFECT", Error))
            return false;
        if (EffectId != 0 && !Kernel_.Gpu().HasEffect(EffectId))
        {
            if (Error)
                *Error = "SET LIMB EFFECT requires existing effect " + std::to_string(EffectId);
            return false;
        }
        ForEachLimbMesh(Object, LimbId,
                        [&](Mesh& Value)
                        {
                            Value.EffectId = EffectId;
                            if (EffectId != 0)
                                Value.BuiltInEffect = 0;
                        });
        Object.TransformDirty = true;
        return true;
    }

    void ObjectManager::ClearEffectReferences(int EffectId)
    {
        for (auto& [ObjectId, Object] : Objects_)
        {
            (void)ObjectId;
            if (Object.EffectId == EffectId)
                Object.EffectId = 0;
            for (Mesh& Value : Object.Meshes)
            {
                if (Value.EffectId == EffectId)
                    Value.EffectId = -1;
            }
            Object.TransformDirty = true;
        }
    }

    bool ObjectManager::ObjectEffect(int& Output, int ObjectId, std::string* Error) const
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "OBJECT EFFECT", Error);
        if (!CheckedObject1)
            return false;
        {
            Output = CheckedObject1->EffectId;
            return true;
        }
    }

    bool ObjectManager::LimbEffect(int& Output, int ObjectId, int LimbId, std::string* Error) const
    {
        const Object3D* ObjectPointer = RequireObject(ObjectId, "LIMB EFFECT", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "LIMB EFFECT", Error))
            return false;
        if (const Mesh* Value = Kernel_.Renderer().FindLimbMesh(Object, LimbId))
        {
            Output = Value->EffectId;
            return true;
        }
        {
            Output = -1;
            return true;
        }
    }

    bool ObjectManager::SetObjectFlags(int ObjectId, int Wireframe, int Transparency, int Cull, int Filter, int Light,
                                       int Fog, int Ambient, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.Wireframe = (Wireframe == 0);
        Object.TransparencyMode = Transparency;
        Object.TransparentBlack = Kernel_.RuntimeCompatibility().SetObjectZeroUsesTransparentBlack()
                                      ? Transparency == 0
                                      : Transparency != 0 && Transparency != 7;
        Object.Cull = (Cull != 0);
        Object.TextureFilter = (Filter != 0);
        Object.UseLighting = (Light != 0);
        Object.UseFog = (Fog != 0);
        Object.UseAmbient = (Ambient != 0);
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObject(int Oid, int W, int T, int C, std::string* Error)
    {
        if (!SetObjectFlags(Oid, W, T, C, 1, 1, 1, 1, Error))
            return false;
        return true;
    }

    bool ObjectManager::SetObject(int Oid, int W, int T, int C, int F, std::string* Error)
    {
        if (!SetObjectFlags(Oid, W, T, C, F, 1, 1, 1, Error))
            return false;
        return true;
    }

    bool ObjectManager::SetObject(int Oid, int W, int T, int C, int F, int L, std::string* Error)
    {
        if (!SetObjectFlags(Oid, W, T, C, F, L, 1, 1, Error))
            return false;
        return true;
    }

    bool ObjectManager::SetObject(int Oid, int W, int T, int C, int F, int L, int Fo, std::string* Error)
    {
        if (!SetObjectFlags(Oid, W, T, C, F, L, Fo, 1, Error))
            return false;
        return true;
    }

    bool ObjectManager::SetObject(int Oid, int W, int T, int C, int F, int L, int Fo, int A, std::string* Error)
    {
        if (!SetObjectFlags(Oid, W, T, C, F, L, Fo, A, Error))
            return false;
        return true;
    }

    bool ObjectManager::SetObjectWireframe(int ObjectId, int Enabled, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT WIREFRAME", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.Wireframe = Enabled != 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectCull(int ObjectId, int Enabled, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT CULL", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.Cull = Enabled != 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectFilter(int ObjectId, int Filter, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT FILTER", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.TextureFilter = Filter != 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectLight(int ObjectId, int Enabled, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT LIGHT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.UseLighting = Enabled != 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectFog(int ObjectId, int Enabled, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT FOG", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.UseFog = Enabled != 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectAmbient(int ObjectId, int Enabled, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT AMBIENT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.UseAmbient = Enabled != 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ScrollObjectTexture(int ObjectId, float U, float V, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SCROLL OBJECT TEXTURE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.TexScrollU += U;
        Object.TexScrollV += V;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ScrollObjectTexture(int ObjectId, int Stage, float U, float V, std::string* Error)
    {
        if (Stage == 0)
            return ScrollObjectTexture(ObjectId, U, V, Error);
        if (Stage < 0)
        {
            if (Error)
                *Error = "SCROLL OBJECT TEXTURE stage must be non-negative";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SCROLL OBJECT TEXTURE", Error);
        if (!ObjectPointer)
            return false;
        for (Mesh& MeshValue : ObjectPointer->Meshes)
        {
            if (!MeshValue.Geometry().UvStages.contains(Stage))
                continue;
            for (SDL_FPoint& Uv : MeshValue.EditGeometry().UvStages.at(Stage))
            {
                Uv.x += U;
                Uv.y += V;
            }
        }
        ObjectPointer->TransformDirty = true;
        return true;
    }

    bool ObjectManager::RecalculateBounds(int ObjectId, const char* Command, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, Command, Error);
        if (!ObjectPointer)
            return false;
        RecalculateObjectBounds(*ObjectPointer);
        return true;
    }

    bool ObjectManager::ScaleObjectTexture(int ObjectId, float U, float V, std::string* Error)
    {
        if (!ScaleObjectTexture(ObjectId, 0, U, V, Error))
            return false;
        return true;
    }

    bool ObjectManager::ScaleObjectTexture(int ObjectId, int Stage, float U, float V, std::string* Error)
    {
        if (Stage < 0)
        {
            if (Error)
                *Error = "SCALE OBJECT TEXTURE stage must be non-negative";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SCALE OBJECT TEXTURE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        for (Mesh& MeshValue : Object.Meshes)
        {
            if (Stage == 0)
            {
                for (SDL_FPoint& Uv : MeshValue.EditGeometry().Uvs)
                {
                    Uv.x *= U;
                    Uv.y *= V;
                }
            }
            else if (MeshValue.Geometry().UvStages.contains(Stage))
            {
                for (SDL_FPoint& Uv : MeshValue.EditGeometry().UvStages.at(Stage))
                {
                    Uv.x *= U;
                    Uv.y *= V;
                }
            }
        }
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetAlphaMappingOn(int ObjectId, float Percentage, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET ALPHA MAPPING ON", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.AlphaMapping = true;
        Object.AlphaMappingAlpha = std::clamp(Percentage, 0.0f, 100.0f) / 100.0f;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ColorLimb(int ObjectId, int LimbId, int RgbPacked, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "COLOR LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "COLOR LIMB", Error))
            return false;
        const SDL_FColor C = UnpackColor(RgbPacked);
        ForEachLimbMesh(Object, LimbId, [&](Mesh& M) { M.LimbTint = SDL_FColor{C.r, C.g, C.b, 1.0f}; });
        Object.TransformDirty = true;
        return true;
    }

    void ObjectManager::ColorObject(int ObjectId, int RgbPacked)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        const SDL_FColor C = UnpackColor(RgbPacked);
        It->second.TintR = C.r;
        It->second.TintG = C.g;
        It->second.TintB = C.b;
        It->second.TransformDirty = true;
    }

    void ObjectManager::FadeObject(int ObjectId, int Percent)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        // DBC FADE OBJECT scales vertex normals: 0=ambient only, 100=normal, 200=2x bright.
        // Negative values invert normals.  No upper clamp — values >100 overbrighten.
        const float Scale = static_cast<float>(Percent) / 100.0f;
        It->second.FadeNormalScale = Scale;
        // Also modulate alpha for fade < 100.  In DBC's D3D7 (gamma-space),
        // ambient-only lighting at fade 0 produced very dark pixels that were
        // near-invisible in additive mode.  Our linear-space pipeline is brighter,
        // so we reduce alpha to compensate.  fade ≥ 100 → alpha = 1.0.
        It->second.Alpha = std::min(1.0f, Scale);
        It->second.TransformDirty = true;
    }

    void ObjectManager::GhostObjectOn(int ObjectId, int Mode)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.Ghosted = true;
        It->second.GhostMode = Mode;
        It->second.TransformDirty = true;
    }

    void ObjectManager::GhostObjectOff(int ObjectId)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.Ghosted = false;
        It->second.TransformDirty = true;
    }

    // ── Visibility / screen ──────────────────────────────────────────

    void ObjectManager::ShowObject(int ObjectId)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.Visible = true;
        It->second.TransformDirty = true;
    }

    void ObjectManager::HideObject(int ObjectId)
    {
        auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        It->second.Visible = false;
        It->second.TransformDirty = true;
    }

    int ObjectManager::ObjectVisible(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0;
        return It->second.Visible ? 1 : 0;
    }

    bool ObjectManager::ObjectScreenX(int& Output, int ObjectId, std::string* Error)
    {
        const Object3D* ObjectPointer = RequireObject(ObjectId, "OBJECT SCREEN X", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        const Vec3 Origin = Kernel_.Renderer().WorldObjectOrigin(Object);
        SDL_FPoint P{};
        float Depth = 0.0f;
        const bool Ok = Object.LockToScreen
                            ? Kernel_.Renderer().ProjectPointLocked(Object.PosX, Object.PosY, Object.PosZ, P, Depth)
                            : Kernel_.Renderer().ProjectPoint(Origin.X, Origin.Y, Origin.Z, P, Depth);
        {
            Output = Ok ? static_cast<int>(std::lround(P.x)) : 0;
            return true;
        }
    }

    bool ObjectManager::ObjectScreenY(int& Output, int ObjectId, std::string* Error)
    {
        const Object3D* ObjectPointer = RequireObject(ObjectId, "OBJECT SCREEN Y", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        const Vec3 Origin = Kernel_.Renderer().WorldObjectOrigin(Object);
        SDL_FPoint P{};
        float Depth = 0.0f;
        const bool Ok = Object.LockToScreen
                            ? Kernel_.Renderer().ProjectPointLocked(Object.PosX, Object.PosY, Object.PosZ, P, Depth)
                            : Kernel_.Renderer().ProjectPoint(Origin.X, Origin.Y, Origin.Z, P, Depth);
        {
            Output = Ok ? static_cast<int>(std::lround(P.y)) : 0;
            return true;
        }
    }

    bool ObjectManager::ObjectInScreen(int& Output, int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "OBJECT IN SCREEN", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        const Vec3 Origin = Kernel_.Renderer().WorldObjectOrigin(Object);
        const Vec3 View = Object.LockToScreen ? Vec3{Object.PosX, Object.PosY, Object.PosZ}
                                              : Kernel_.Renderer().WorldToViewPoint(Origin.X, Origin.Y, Origin.Z);
        const float LocalRadius = RenderBoundingRadius(Object);
        const float Radius = std::max(0.0f, LocalRadius) *
                             std::max({std::abs(Object.ScaleX), std::abs(Object.ScaleY), std::abs(Object.ScaleZ)});
        if (View.Z + Radius < Kernel_.Camera().ClampedNearPlane() ||
            View.Z - Radius > Kernel_.Camera().ClampedFarPlane())
        {
            Output = 0;
            return true;
        }
        const float FovRadians = std::clamp(Kernel_.Camera().Fov(), 10.0f, 170.0f) * static_cast<float>(M_PI / 180.0);
        const float TanY = std::tan(FovRadians * 0.5f);
        const float TanX = TanY * std::max(0.0001f, Kernel_.Camera().Aspect());
        const float HorizontalRadius = Radius * std::sqrt(1.0f + TanX * TanX);
        const float VerticalRadius = Radius * std::sqrt(1.0f + TanY * TanY);
        {
            Output = std::abs(View.X) <= View.Z * TanX + HorizontalRadius &&
                             std::abs(View.Y) <= View.Z * TanY + VerticalRadius
                         ? 1
                         : 0;
            return true;
        }
    }

    bool ObjectManager::LockObjectOn(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "LOCK OBJECT ON", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->LockToScreen = true;
        CheckedObject1->TransformDirty = true;
        return true;
    }

    bool ObjectManager::LockObjectOff(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "LOCK OBJECT OFF", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->LockToScreen = false;
        CheckedObject1->TransformDirty = true;
        return true;
    }

    bool ObjectManager::DisableObjectZdepth(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "DISABLE OBJECT ZDEPTH", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->ZdepthDisabled = true;
        CheckedObject1->TransformDirty = true;
        return true;
    }

    bool ObjectManager::EnableObjectZdepth(int ObjectId, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "ENABLE OBJECT ZDEPTH", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->ZdepthDisabled = false;
        CheckedObject1->TransformDirty = true;
        return true;
    }

    bool ObjectManager::DisableObjectZread(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "DISABLE OBJECT ZREAD", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.DepthTest = false;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::EnableObjectZread(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "ENABLE OBJECT ZREAD", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.DepthTest = true;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::DisableObjectZwrite(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "DISABLE OBJECT ZWRITE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.DepthWrite = false;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::EnableObjectZwrite(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "ENABLE OBJECT ZWRITE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.DepthWrite = true;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::HideObjectBounds(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "HIDE OBJECT BOUNDS", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.DrawBounds = false;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ShowObjectBounds(int ObjectId, int BoxOnly, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SHOW OBJECT BOUNDS", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.DrawBounds = true;
        Object.BoundsBoxOnly = BoxOnly != 0;
        Object.TransformDirty = true;
        return true;
    }

    // ── Limb operations ──────────────────────────────────────────────

    bool ObjectManager::LinkLimb(int ObjectId, int ParentLimbId, int LimbId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "LINK LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, ParentLimbId, "LINK LIMB", Error))
            return false;
        if (!RequireLimb(Object, ObjectId, LimbId, "LINK LIMB", Error))
            return false;
        if (LimbId == 0 || LimbId == ParentLimbId)
        {
            if (Error)
                *Error = "LINK LIMB requires a non-root child distinct from its parent";
            return false;
        }
        const std::size_t LimbCount = std::max(Object.LimbNodes.size(), Object.Meshes.size());
        const std::size_t PreviousLimbCount = Object.LimbTransforms.size();
        Object.LimbTransforms.resize(LimbCount);
        if (Object.LimbNodes.empty())
        {
            for (std::size_t Index = PreviousLimbCount; Index < LimbCount; ++Index)
            {
                const Mesh& MeshValue = Object.Meshes[Index];
                LimbTransform& Transform = Object.LimbTransforms[Index];
                Transform.RotX = MeshValue.LimbRotX;
                Transform.RotY = MeshValue.LimbRotY;
                Transform.RotZ = MeshValue.LimbRotZ;
                Transform.OffX = MeshValue.LimbOffX;
                Transform.OffY = MeshValue.LimbOffY;
                Transform.OffZ = MeshValue.LimbOffZ;
                Transform.ScaleX = MeshValue.LimbScaleX;
                Transform.ScaleY = MeshValue.LimbScaleY;
                Transform.ScaleZ = MeshValue.LimbScaleZ;
                Transform.Hidden = !MeshValue.Visible;
            }
        }
        for (int Ancestor = ParentLimbId, Depth = 0; Ancestor >= 0 && Depth <= static_cast<int>(LimbCount); ++Depth)
        {
            if (Ancestor == LimbId)
            {
                if (Error)
                    *Error = "LINK LIMB cannot create a hierarchy cycle";
                return false;
            }
            if (Ancestor >= static_cast<int>(Object.LimbTransforms.size()))
                break;
            Ancestor = Object.LimbTransforms[static_cast<std::size_t>(Ancestor)].ParentLimb;
        }
        Object.LimbTransforms[static_cast<std::size_t>(LimbId)].ParentLimb = ParentLimbId;
        Object.CachedAnimTicks = -1.0;
        Object.TransformDirty = true;
        RecalculateObjectBounds(Object);
        return true;
    }

    bool ObjectManager::TextureLimb(int ObjectId, int LimbId, int ImageId, std::string* Error)
    {
        if (!TextureLimb(ObjectId, LimbId, 0, ImageId, Error))
            return false;
        return true;
    }

    bool ObjectManager::TextureLimb(int ObjectId, int LimbId, int Stage, int ImageId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "TEXTURE LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "TEXTURE LIMB", Error))
            return false;
        if (Stage < 0)
        {
            if (Error)
                *Error = "TEXTURE LIMB stage must not be negative";
            return false;
        }
        if (ImageId != 0 && Kernel_.ImageStoreRef().ImageExist(ImageId) == 0)
        {
            if (Error)
                *Error = "TEXTURE LIMB requires existing image " + std::to_string(ImageId);
            return false;
        }
        ForEachLimbMesh(Object, LimbId,
                        [&](Mesh& MeshValue)
                        {
                            if (Stage == 0)
                                MeshValue.TextureImageId = ImageId;
                            if (ImageId == 0)
                                MeshValue.TextureStageImageIds.erase(Stage);
                            else
                                MeshValue.TextureStageImageIds[Stage] = ImageId;
                            MeshValue.TextureStageCubeModes.erase(Stage);
                        });
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::RotateLimb(int ObjectId, int LimbId, float XDegrees, float YDegrees, float ZDegrees,
                                   int BoundsFlag, std::string* Error)
    {
        if (BoundsFlag != 0 && BoundsFlag != 1)
        {
            if (Error)
                *Error = "ROTATE LIMB bounds flag must be zero or one";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "ROTATE LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "ROTATE LIMB", Error))
            return false;
        Mesh* Limb = Kernel_.Renderer().FindLimbMesh(Object, LimbId);
        if (Limb)
        {
            Limb->LimbRotX = XDegrees;
            Limb->LimbRotY = YDegrees;
            Limb->LimbRotZ = ZDegrees;
        }
        const size_t Li = static_cast<size_t>(LimbId);
        if (Li < Object.LimbTransforms.size())
        {
            Object.LimbTransforms[Li].RotX = XDegrees;
            Object.LimbTransforms[Li].RotY = YDegrees;
            Object.LimbTransforms[Li].RotZ = ZDegrees;
            Object.CachedAnimTicks = -1.0;
        }
        Object.TransformDirty = true;
        if (BoundsFlag == 1)
            RecalculateObjectBounds(Object);
        return true;
    }

    bool ObjectManager::OffsetLimb(int ObjectId, int LimbId, float X, float Y, float Z, int BoundsFlag,
                                   std::string* Error)
    {
        if (BoundsFlag != 0 && BoundsFlag != 1)
        {
            if (Error)
                *Error = "OFFSET LIMB bounds flag must be zero or one";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "OFFSET LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "OFFSET LIMB", Error))
            return false;
        Mesh* Limb = Kernel_.Renderer().FindLimbMesh(Object, LimbId);
        if (Limb)
        {
            Limb->LimbOffX = X;
            Limb->LimbOffY = Y;
            Limb->LimbOffZ = Z;
        }
        const size_t Li = static_cast<size_t>(LimbId);
        if (Li < Object.LimbTransforms.size())
        {
            Object.LimbTransforms[Li].OffX = X;
            Object.LimbTransforms[Li].OffY = Y;
            Object.LimbTransforms[Li].OffZ = Z;
            Object.CachedAnimTicks = -1.0;
        }
        Object.TransformDirty = true;
        if (BoundsFlag == 1)
            RecalculateObjectBounds(Object);
        return true;
    }

    bool ObjectManager::ScaleLimb(int ObjectId, int LimbId, float XPercent, float YPercent, float ZPercent,
                                  int BoundsFlag, std::string* Error)
    {
        if (BoundsFlag != 0 && BoundsFlag != 1)
        {
            if (Error)
                *Error = "SCALE LIMB bounds flag must be zero or one";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SCALE LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SCALE LIMB", Error))
            return false;
        const float Sx = XPercent / 100.0f;
        const float Sy = YPercent / 100.0f;
        const float Sz = ZPercent / 100.0f;
        Mesh* Limb = Kernel_.Renderer().FindLimbMesh(Object, LimbId);
        if (Limb)
        {
            Limb->LimbScaleX = Sx;
            Limb->LimbScaleY = Sy;
            Limb->LimbScaleZ = Sz;
        }
        const size_t Li = static_cast<size_t>(LimbId);
        if (Li < Object.LimbTransforms.size())
        {
            Object.LimbTransforms[Li].ScaleX = Sx;
            Object.LimbTransforms[Li].ScaleY = Sy;
            Object.LimbTransforms[Li].ScaleZ = Sz;
            Object.CachedAnimTicks = -1.0;
        }
        Object.TransformDirty = true;
        if (BoundsFlag == 1)
            RecalculateObjectBounds(Object);
        return true;
    }

    bool ObjectManager::SetLimbNormals(int ObjectId, int LimbId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET LIMB NORMALS", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SET LIMB NORMALS", Error))
            return false;
        ForEachLimbMesh(Object, LimbId, [](Mesh& MeshValue) { RendererSystem::RegenerateMeshNormals(MeshValue); });
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetLimbSmoothing(int ObjectId, int LimbId, float Percentage, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET LIMB SMOOTHING", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SET LIMB SMOOTHING", Error))
            return false;
        ForEachLimbMesh(Object, LimbId, [Percentage](Mesh& MeshValue) { SmoothMeshNormals(MeshValue, Percentage); });
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ScrollLimbTexture(int ObjectId, int LimbId, float U, float V, std::string* Error)
    {
        if (!ScrollLimbTexture(ObjectId, LimbId, 0, U, V, Error))
            return false;
        return true;
    }

    bool ObjectManager::ScrollLimbTexture(int ObjectId, int LimbId, int Stage, float U, float V, std::string* Error)
    {
        if (Stage < 0)
        {
            if (Error)
                *Error = "SCROLL LIMB TEXTURE stage must be non-negative";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SCROLL LIMB TEXTURE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SCROLL LIMB TEXTURE", Error))
            return false;
        ForEachLimbMesh(Object, LimbId,
                        [&](Mesh& MeshValue)
                        {
                            if (Stage == 0)
                            {
                                for (SDL_FPoint& Uv : MeshValue.EditGeometry().Uvs)
                                {
                                    Uv.x += U;
                                    Uv.y += V;
                                }
                            }
                            else if (MeshValue.Geometry().UvStages.contains(Stage))
                            {
                                for (SDL_FPoint& Uv : MeshValue.EditGeometry().UvStages.at(Stage))
                                {
                                    Uv.x += U;
                                    Uv.y += V;
                                }
                            }
                        });
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ScaleLimbTexture(int ObjectId, int LimbId, float U, float V, std::string* Error)
    {
        if (!ScaleLimbTexture(ObjectId, LimbId, 0, U, V, Error))
            return false;
        return true;
    }

    bool ObjectManager::ScaleLimbTexture(int ObjectId, int LimbId, int Stage, float U, float V, std::string* Error)
    {
        if (Stage < 0)
        {
            if (Error)
                *Error = "SCALE LIMB TEXTURE stage must be non-negative";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SCALE LIMB TEXTURE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SCALE LIMB TEXTURE", Error))
            return false;
        ForEachLimbMesh(Object, LimbId,
                        [&](Mesh& MeshValue)
                        {
                            if (Stage == 0)
                            {
                                for (SDL_FPoint& Uv : MeshValue.EditGeometry().Uvs)
                                {
                                    Uv.x *= U;
                                    Uv.y *= V;
                                }
                            }
                            else if (MeshValue.Geometry().UvStages.contains(Stage))
                            {
                                for (SDL_FPoint& Uv : MeshValue.EditGeometry().UvStages.at(Stage))
                                {
                                    Uv.x *= U;
                                    Uv.y *= V;
                                }
                            }
                        });
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::HideLimb(int ObjectId, int LimbId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "HIDE LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "HIDE LIMB", Error))
            return false;
        ForEachLimbMesh(Object, LimbId, [](Mesh& MeshValue) { MeshValue.Visible = false; });
        if (Kernel_.RuntimeCompatibility().RecursiveLimbVisibility() &&
            LimbId < static_cast<int>(Object.LimbTransforms.size()))
            Object.LimbTransforms[static_cast<std::size_t>(LimbId)].Hidden = true;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectExcluded(int ObjectId, bool Excluded, const char* Command, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, Command, Error);
        if (!Object)
            return false;
        Object->Excluded = Excluded;
        Object->TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetLimbExcluded(int ObjectId, int LimbId, bool Excluded, const char* Command,
                                        std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, Command, Error);
        if (!Object || !RequireLimb(*Object, ObjectId, LimbId, Command, Error))
            return false;
        ForEachLimbMesh(*Object, LimbId, [&](Mesh& MeshValue) { MeshValue.Excluded = Excluded; });
        Object->TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectCameraMask(int ObjectId, int Mask, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, "SET OBJECT MASK", Error);
        if (!Object)
            return false;
        Object->CameraMask = static_cast<uint32_t>(Mask);
        Object->TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectFov(int ObjectId, float Degrees, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, "SET OBJECT FOV", Error);
        if (!Object)
            return false;
        if (!(Degrees == 0.0f || (Degrees >= 1.0f && Degrees <= 179.0f)))
        {
            if (Error)
                *Error = "SET OBJECT FOV takes 0 for the camera's field of view, or 1 to 179 degrees";
            return false;
        }
        Object->ViewFov = Degrees;
        Object->TransformDirty = true;
        return true;
    }

    bool ObjectManager::EnableObjectDepthBias(int ObjectId, float Slope, float Constant, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, "ENABLE OBJECT ZBIAS", Error);
        if (!Object)
            return false;
        if (!std::isfinite(Slope) || !std::isfinite(Constant))
        {
            if (Error)
                *Error = "ENABLE OBJECT ZBIAS requires finite values";
            return false;
        }
        Object->DepthBiased = true;
        Object->DepthBiasSlope = Slope;
        Object->DepthBiasConstant = Constant;
        Object->TransformDirty = true;
        return true;
    }

    bool ObjectManager::DisableObjectDepthBias(int ObjectId, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, "DISABLE OBJECT ZBIAS", Error);
        if (!Object)
            return false;
        Object->DepthBiased = false;
        Object->TransformDirty = true;
        return true;
    }

    bool ObjectManager::CalculateObjectBounds(int ObjectId, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, "CALCULATE OBJECT BOUNDS", Error);
        if (!Object)
            return false;
        RecalculateObjectBounds(*Object);
        return true;
    }

    bool ObjectManager::LimbName(std::string& Output, int ObjectId, int LimbId, std::string* Error) const
    {
        const Object3D* Object = GetObject(ObjectId);
        if (!Object)
        {
            if (Error)
                *Error = "LIMB NAME$ requires existing object " + std::to_string(ObjectId);
            return false;
        }
        if (!RequireLimb(*Object, ObjectId, LimbId, "LIMB NAME$", Error))
            return false;
        // A limb made by a command rather than loaded from a model has no node, so no name.
        const aiNode* Node = LimbId < static_cast<int>(Object->LimbNodes.size())
                                 ? Object->LimbNodes[static_cast<std::size_t>(LimbId)]
                                 : nullptr;
        Output = Node ? Node->mName.C_Str() : "";
        return true;
    }

    bool ObjectManager::ShowLimb(int ObjectId, int LimbId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SHOW LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "SHOW LIMB", Error))
            return false;
        ForEachLimbMesh(Object, LimbId, [](Mesh& MeshValue) { MeshValue.Visible = true; });
        if (Kernel_.RuntimeCompatibility().RecursiveLimbVisibility() &&
            LimbId < static_cast<int>(Object.LimbTransforms.size()))
            Object.LimbTransforms[static_cast<std::size_t>(LimbId)].Hidden = false;
        Object.TransformDirty = true;
        return true;
    }

    float ObjectManager::LimbAngleX(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        if (LimbId >= 0 && LimbId < static_cast<int>(It->second.LimbTransforms.size()))
            return It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].RotX;
        const Mesh* Limb = Kernel_.Renderer().FindLimbMesh(It->second, LimbId);
        return Limb ? Limb->LimbRotX : 0.0f;
    }

    float ObjectManager::LimbAngleY(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        if (LimbId >= 0 && LimbId < static_cast<int>(It->second.LimbTransforms.size()))
            return It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].RotY;
        const Mesh* Limb = Kernel_.Renderer().FindLimbMesh(It->second, LimbId);
        return Limb ? Limb->LimbRotY : 0.0f;
    }

    float ObjectManager::LimbAngleZ(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        if (LimbId >= 0 && LimbId < static_cast<int>(It->second.LimbTransforms.size()))
            return It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].RotZ;
        const Mesh* Limb = Kernel_.Renderer().FindLimbMesh(It->second, LimbId);
        return Limb ? Limb->LimbRotZ : 0.0f;
    }

    float ObjectManager::LimbPositionX(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        return Kernel_.Renderer().WorldLimbOrigin(It->second, LimbId).X;
    }

    float ObjectManager::LimbPositionY(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        return Kernel_.Renderer().WorldLimbOrigin(It->second, LimbId).Y;
    }

    float ObjectManager::LimbPositionZ(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return 0.0f;
        return Kernel_.Renderer().WorldLimbOrigin(It->second, LimbId).Z;
    }

    float ObjectManager::LimbOffsetX(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0.0f;
        if (LimbId < static_cast<int>(It->second.LimbTransforms.size()))
            return It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].OffX;
        if (LimbId >= static_cast<int>(It->second.Meshes.size()))
            return 0.0f;
        return It->second.Meshes[static_cast<std::size_t>(LimbId)].LimbOffX;
    }

    float ObjectManager::LimbOffsetY(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0.0f;
        if (LimbId < static_cast<int>(It->second.LimbTransforms.size()))
            return It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].OffY;
        if (LimbId >= static_cast<int>(It->second.Meshes.size()))
            return 0.0f;
        return It->second.Meshes[static_cast<std::size_t>(LimbId)].LimbOffY;
    }

    float ObjectManager::LimbOffsetZ(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0.0f;
        if (LimbId < static_cast<int>(It->second.LimbTransforms.size()))
            return It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].OffZ;
        if (LimbId >= static_cast<int>(It->second.Meshes.size()))
            return 0.0f;
        return It->second.Meshes[static_cast<std::size_t>(LimbId)].LimbOffZ;
    }

    float ObjectManager::LimbDirectionX(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0.0f;
        return It->second.RotX + LimbAngleX(ObjectId, LimbId);
    }

    float ObjectManager::LimbDirectionY(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0.0f;
        return It->second.RotY + LimbAngleY(ObjectId, LimbId);
    }

    float ObjectManager::LimbDirectionZ(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0.0f;
        return It->second.RotZ + LimbAngleZ(ObjectId, LimbId);
    }

    int ObjectManager::LimbExist(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbId < 0)
            return 0;
        if (static_cast<std::size_t>(LimbId) < It->second.LimbNodes.size())
        {
            return It->second.LimbNodes[static_cast<std::size_t>(LimbId)] != nullptr ? 1 : 0;
        }
        return static_cast<std::size_t>(LimbId) < It->second.Meshes.size() &&
                       !It->second.Meshes[static_cast<std::size_t>(LimbId)].Geometry().Vertices.empty()
                   ? 1
                   : 0;
    }

    int ObjectManager::LimbVisible(int ObjectId, int LimbId)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || LimbExist(ObjectId, LimbId) == 0)
            return 0;
        if (LimbId < static_cast<int>(It->second.LimbTransforms.size()) &&
            It->second.LimbTransforms[static_cast<std::size_t>(LimbId)].Hidden)
            return 0;
        const Mesh* Limb = Kernel_.Renderer().FindLimbMesh(It->second, LimbId);
        return Limb && Limb->Visible ? 1 : 0;
    }

    // ── Mutable vertex data ─────────────────────────────────────────

    bool ObjectManager::LockVertexdataForMesh(int MeshId, std::string* Error)
    {
        const auto It = Meshes_.find(MeshId);
        if (It == Meshes_.end() || It->second.empty())
        {
            if (Error)
                *Error = "LOCK VERTEXDATA FOR MESH requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        VertexLockKind_ = VertexLockKind::Mesh;
        VertexLockId_ = MeshId;
        VertexLockLimb_ = 0;
        return true;
    }

    bool ObjectManager::LockVertexdataForLimb(int ObjectId, int LimbId, int LockMode, std::string* Error)
    {
        if (LockMode < 0 || LockMode > 2)
        {
            if (Error)
                *Error = "LOCK VERTEXDATA FOR LIMB mode must be between zero and two";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "LOCK VERTEXDATA FOR LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (!RequireLimb(Object, ObjectId, LimbId, "LOCK VERTEXDATA FOR LIMB", Error))
            return false;
        if (!Kernel_.Renderer().FindLimbMesh(Object, LimbId))
        {
            if (Error)
                *Error = "LOCK VERTEXDATA FOR LIMB selected a limb without geometry";
            return false;
        }
        VertexLockKind_ = VertexLockKind::Limb;
        VertexLockId_ = ObjectId;
        VertexLockLimb_ = LimbId;
        return true;
    }

    void ObjectManager::UnlockVertexdata()
    {
        if (VertexLockKind_ == VertexLockKind::Limb)
        {
            const auto It = Objects_.find(VertexLockId_);
            if (It != Objects_.end())
                RecalculateObjectBounds(It->second);
        }
        VertexLockKind_ = VertexLockKind::None;
        VertexLockId_ = 0;
        VertexLockLimb_ = 0;
    }

    bool ObjectManager::AddMeshToVertexdata(int MeshId, std::string* Error)
    {
        Mesh* TargetPointer = RequireLockedVertexMesh("ADD MESH TO VERTEXDATA", Error);
        if (!TargetPointer)
            return false;
        Mesh& Target = *TargetPointer;
        const auto It = Meshes_.find(MeshId);
        if (It == Meshes_.end() || It->second.empty())
        {
            if (Error)
                *Error = "ADD MESH TO VERTEXDATA requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        const Mesh Source = It->second.front();
        MeshGeometry& Geometry = Target.EditGeometry();
        const uint32_t VertexBase = static_cast<uint32_t>(Geometry.Vertices.size());
        Geometry.Vertices.insert(Geometry.Vertices.end(), Source.Geometry().Vertices.begin(),
                                 Source.Geometry().Vertices.end());
        Geometry.Normals.resize(VertexBase, Vec3{});
        Geometry.Normals.insert(Geometry.Normals.end(), Source.Geometry().Normals.begin(),
                                Source.Geometry().Normals.end());
        Geometry.Normals.resize(Geometry.Vertices.size(), Vec3{});
        Geometry.Uvs.resize(VertexBase, SDL_FPoint{});
        Geometry.Uvs.insert(Geometry.Uvs.end(), Source.Geometry().Uvs.begin(), Source.Geometry().Uvs.end());
        Geometry.Uvs.resize(Geometry.Vertices.size(), SDL_FPoint{});
        Geometry.DiffuseColors.resize(VertexBase, 0xFFFFFFFFu);
        Geometry.DiffuseColors.insert(Geometry.DiffuseColors.end(), Source.Geometry().DiffuseColors.begin(),
                                      Source.Geometry().DiffuseColors.end());
        Geometry.DiffuseColors.resize(Geometry.Vertices.size(), 0xFFFFFFFFu);
        for (const auto& [Stage, Values] : Source.Geometry().UvStages)
        {
            auto& TargetValues = Geometry.UvStages[Stage];
            TargetValues.resize(VertexBase, SDL_FPoint{});
            TargetValues.insert(TargetValues.end(), Values.begin(), Values.end());
            TargetValues.resize(Geometry.Vertices.size(), SDL_FPoint{});
        }
        for (uint32_t Index : Source.Geometry().Indices)
            Geometry.Indices.push_back(VertexBase + Index);
        return true;
    }

    bool ObjectManager::DeleteMeshFromVertexdata(int VertexStart, int VertexEnd, int IndexStart, int IndexEnd,
                                                 std::string* Error)
    {
        Mesh* ValuePointer = RequireLockedVertexMesh("DELETE MESH FROM VERTEXDATA", Error);
        if (!ValuePointer)
            return false;
        Mesh& Value = *ValuePointer;
        if (VertexStart < 0 || VertexEnd < VertexStart ||
            VertexEnd >= static_cast<int>(Value.Geometry().Vertices.size()) || IndexStart < 0 ||
            IndexEnd < IndexStart || IndexEnd >= static_cast<int>(Value.Geometry().Indices.size()))
        {
            if (Error)
                *Error = "DELETE MESH FROM VERTEXDATA range is outside the locked mesh";
            return false;
        }
        const uint32_t RemovedVertices = static_cast<uint32_t>(VertexEnd - VertexStart + 1);
        for (std::size_t Position = 0; Position < Value.Geometry().Indices.size(); ++Position)
        {
            if (Position >= static_cast<std::size_t>(IndexStart) && Position <= static_cast<std::size_t>(IndexEnd))
                continue;
            const uint32_t Index = Value.Geometry().Indices[Position];
            if (Index >= static_cast<uint32_t>(VertexStart) && Index <= static_cast<uint32_t>(VertexEnd))
            {
                if (Error)
                    *Error = "DELETE MESH FROM VERTEXDATA leaves an index referring to deleted vertices";
                return false;
            }
        }
        const auto CompleteChannel = [VertexEnd](const auto& Values)
        { return Values.empty() || static_cast<std::size_t>(VertexEnd) < Values.size(); };
        if (!CompleteChannel(Value.Geometry().Vertices) || !CompleteChannel(Value.Geometry().Normals) ||
            !CompleteChannel(Value.Geometry().Uvs) || !CompleteChannel(Value.Geometry().DiffuseColors) ||
            !std::all_of(Value.Geometry().UvStages.begin(), Value.Geometry().UvStages.end(),
                         [&](const auto& Stage) { return CompleteChannel(Stage.second); }))
        {
            if (Error)
                *Error = "DELETE MESH FROM VERTEXDATA found an incomplete vertex channel";
            return false;
        }
        MeshGeometry& Geometry = Value.EditGeometry();
        Geometry.Indices.erase(Geometry.Indices.begin() + IndexStart, Geometry.Indices.begin() + IndexEnd + 1);
        for (uint32_t& Index : Geometry.Indices)
        {
            if (Index > static_cast<uint32_t>(VertexEnd))
                Index -= RemovedVertices;
        }
        const auto EraseRange = [VertexStart, VertexEnd](auto& Values)
        {
            if (Values.empty())
                return;

            Values.erase(Values.begin() + VertexStart, Values.begin() + VertexEnd + 1);
        };
        EraseRange(Geometry.Vertices);
        EraseRange(Geometry.Normals);
        EraseRange(Geometry.Uvs);
        EraseRange(Geometry.DiffuseColors);
        for (auto& [Stage, Values] : Geometry.UvStages)
            EraseRange(Values);
        return true;
    }

    bool ObjectManager::ConvertMeshToVertexdata(int MeshId, std::string* Error)
    {
        const auto It = Meshes_.find(MeshId);
        if (It == Meshes_.end() || It->second.empty())
        {
            if (Error)
                *Error = "CONVERT MESH TO VERTEXDATA requires existing mesh " + std::to_string(MeshId);
            return false;
        }
        Mesh& Value = It->second.front();
        if (Value.Geometry().Indices.empty())
            return true;
        Mesh Expanded = Value;
        MeshGeometry& Geometry = Expanded.EditGeometry();
        Geometry.Vertices.clear();
        Geometry.Normals.clear();
        Geometry.Uvs.clear();
        Geometry.DiffuseColors.clear();
        Geometry.UvStages.clear();
        Geometry.Indices.clear();
        // Value still holds the unexpanded geometry: Expanded detached from it above.
        const MeshGeometry& Source = Value.Geometry();
        for (uint32_t SourceIndex : Source.Indices)
        {
            if (SourceIndex >= Source.Vertices.size())
            {
                if (Error)
                    *Error = "CONVERT MESH TO VERTEXDATA found an invalid index";
                return false;
            }
            Geometry.Vertices.push_back(Source.Vertices[SourceIndex]);
            Geometry.Normals.push_back(SourceIndex < Source.Normals.size() ? Source.Normals[SourceIndex] : Vec3{});
            Geometry.Uvs.push_back(SourceIndex < Source.Uvs.size() ? Source.Uvs[SourceIndex] : SDL_FPoint{});
            Geometry.DiffuseColors.push_back(
                SourceIndex < Source.DiffuseColors.size() ? Source.DiffuseColors[SourceIndex] : 0xFFFFFFFFu);
            for (const auto& [Stage, Values] : Source.UvStages)
                Geometry.UvStages[Stage].push_back(SourceIndex < Values.size() ? Values[SourceIndex] : SDL_FPoint{});
            Geometry.Indices.push_back(static_cast<uint32_t>(Geometry.Indices.size()));
        }
        Value = std::move(Expanded);
        return true;
    }

    bool ObjectManager::VertexdataVertexCount(int& Output, std::string* Error) const
    {
        auto* CheckedLockedVertexMesh1 = RequireLockedVertexMesh("GET VERTEXDATA VERTEX COUNT", Error);
        if (!CheckedLockedVertexMesh1)
            return false;
        {
            Output = static_cast<int>(CheckedLockedVertexMesh1->Geometry().Vertices.size());
            return true;
        }
    }

    bool ObjectManager::VertexdataIndexCount(int& Output, std::string* Error) const
    {
        auto* CheckedLockedVertexMesh1 = RequireLockedVertexMesh("GET VERTEXDATA INDEX COUNT", Error);
        if (!CheckedLockedVertexMesh1)
            return false;
        {
            Output = static_cast<int>(CheckedLockedVertexMesh1->Geometry().Indices.size());
            return true;
        }
    }

    bool ObjectManager::SetVertexdataPosition(int Index, float X, float Y, float Z, std::string* Error)
    {
        Mesh* ValuePointer = RequireLockedVertexMesh("SET VERTEXDATA POSITION", Error);
        if (!ValuePointer)
            return false;
        Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "SET VERTEXDATA POSITION vertex is outside the locked mesh";
            return false;
        }
        Value.EditGeometry().Vertices[static_cast<std::size_t>(Index)] = {X, Y, Z};
        return true;
    }

    bool ObjectManager::SetVertexdataNormals(int Index, float X, float Y, float Z, std::string* Error)
    {
        Mesh* ValuePointer = RequireLockedVertexMesh("SET VERTEXDATA NORMALS", Error);
        if (!ValuePointer)
            return false;
        Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "SET VERTEXDATA NORMALS vertex is outside the locked mesh";
            return false;
        }
        MeshGeometry& Geometry = Value.EditGeometry();
        Geometry.Normals.resize(Geometry.Vertices.size(), Vec3{});
        Geometry.Normals[static_cast<std::size_t>(Index)] = {X, Y, Z};
        return true;
    }

    bool ObjectManager::SetVertexdataDiffuse(int Index, uint32_t Color, std::string* Error)
    {
        Mesh* ValuePointer = RequireLockedVertexMesh("SET VERTEXDATA DIFFUSE", Error);
        if (!ValuePointer)
            return false;
        Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "SET VERTEXDATA DIFFUSE vertex is outside the locked mesh";
            return false;
        }
        MeshGeometry& Geometry = Value.EditGeometry();
        Geometry.DiffuseColors.resize(Geometry.Vertices.size(), 0xFFFFFFFFu);
        Geometry.DiffuseColors[static_cast<std::size_t>(Index)] = Color;
        return true;
    }

    bool ObjectManager::SetVertexdataUv(int Index, int Stage, float U, float V, std::string* Error)
    {
        Mesh* ValuePointer = RequireLockedVertexMesh("SET VERTEXDATA UV", Error);
        if (!ValuePointer)
            return false;
        Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()) || Stage < 0 || Stage > 7)
        {
            if (Error)
                *Error = "SET VERTEXDATA UV vertex or stage is outside the locked mesh";
            return false;
        }
        MeshGeometry& Geometry = Value.EditGeometry();
        auto& Values = Stage == 0 ? Geometry.Uvs : Geometry.UvStages[Stage];
        Values.resize(Geometry.Vertices.size(), SDL_FPoint{});
        Values[static_cast<std::size_t>(Index)] = {U, V};
        return true;
    }

    bool ObjectManager::SetIndexdata(int Index, int Value, std::string* Error)
    {
        Mesh* TargetPointer = RequireLockedVertexMesh("SET INDEXDATA", Error);
        if (!TargetPointer)
            return false;
        Mesh& Target = *TargetPointer;
        if (Index < 0 || Index >= static_cast<int>(Target.Geometry().Indices.size()) || Value < 0 ||
            Value >= static_cast<int>(Target.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "SET INDEXDATA index or vertex is outside the locked mesh";
            return false;
        }
        Target.EditGeometry().Indices[static_cast<std::size_t>(Index)] = static_cast<uint32_t>(Value);
        return true;
    }

    bool ObjectManager::VertexdataPositionX(float& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA POSITION X", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA POSITION X vertex is outside the locked mesh";
            return false;
        }
        {
            Output = Value.Geometry().Vertices[static_cast<std::size_t>(Index)].X;
            return true;
        }
    }

    bool ObjectManager::VertexdataPositionY(float& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA POSITION Y", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA POSITION Y vertex is outside the locked mesh";
            return false;
        }
        {
            Output = Value.Geometry().Vertices[static_cast<std::size_t>(Index)].Y;
            return true;
        }
    }

    bool ObjectManager::VertexdataPositionZ(float& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA POSITION Z", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA POSITION Z vertex is outside the locked mesh";
            return false;
        }
        {
            Output = Value.Geometry().Vertices[static_cast<std::size_t>(Index)].Z;
            return true;
        }
    }

    bool ObjectManager::VertexdataNormalsX(float& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA NORMALS X", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Normals.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA NORMALS X vertex has no normal";
            return false;
        }
        {
            Output = Value.Geometry().Normals[static_cast<std::size_t>(Index)].X;
            return true;
        }
    }

    bool ObjectManager::VertexdataNormalsY(float& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA NORMALS Y", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Normals.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA NORMALS Y vertex has no normal";
            return false;
        }
        {
            Output = Value.Geometry().Normals[static_cast<std::size_t>(Index)].Y;
            return true;
        }
    }

    bool ObjectManager::VertexdataNormalsZ(float& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA NORMALS Z", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Normals.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA NORMALS Z vertex has no normal";
            return false;
        }
        {
            Output = Value.Geometry().Normals[static_cast<std::size_t>(Index)].Z;
            return true;
        }
    }

    bool ObjectManager::VertexdataDiffuse(int& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA DIFFUSE", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Vertices.size()))
        {
            if (Error)
                *Error = "GET VERTEXDATA DIFFUSE vertex is outside the locked mesh";
            return false;
        }
        {
            Output = static_cast<int>(Index < static_cast<int>(Value.Geometry().DiffuseColors.size())
                                          ? Value.Geometry().DiffuseColors[static_cast<std::size_t>(Index)]
                                          : 0xFFFFFFFFu);
            return true;
        }
    }

    bool ObjectManager::VertexdataU(float& Output, int Index, int Stage, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA U", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        const auto It = Value.Geometry().UvStages.find(Stage);
        const auto& Values = Stage == 0 ? Value.Geometry().Uvs
                                        : (It != Value.Geometry().UvStages.end() ? It->second : Value.Geometry().Uvs);
        if (Stage < 0 || Stage > 7 || Index < 0 || Index >= static_cast<int>(Values.size()) ||
            (Stage != 0 && It == Value.Geometry().UvStages.end()))
        {
            if (Error)
                *Error = "GET VERTEXDATA U vertex or stage is outside the locked mesh";
            return false;
        }
        {
            Output = Values[static_cast<std::size_t>(Index)].x;
            return true;
        }
    }

    bool ObjectManager::VertexdataV(float& Output, int Index, int Stage, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET VERTEXDATA V", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        const auto It = Value.Geometry().UvStages.find(Stage);
        const auto& Values = Stage == 0 ? Value.Geometry().Uvs
                                        : (It != Value.Geometry().UvStages.end() ? It->second : Value.Geometry().Uvs);
        if (Stage < 0 || Stage > 7 || Index < 0 || Index >= static_cast<int>(Values.size()) ||
            (Stage != 0 && It == Value.Geometry().UvStages.end()))
        {
            if (Error)
                *Error = "GET VERTEXDATA V vertex or stage is outside the locked mesh";
            return false;
        }
        {
            Output = Values[static_cast<std::size_t>(Index)].y;
            return true;
        }
    }

    bool ObjectManager::Indexdata(int& Output, int Index, std::string* Error) const
    {
        const Mesh* ValuePointer = RequireLockedVertexMesh("GET INDEXDATA", Error);
        if (!ValuePointer)
            return false;
        const Mesh& Value = *ValuePointer;
        if (Index < 0 || Index >= static_cast<int>(Value.Geometry().Indices.size()))
        {
            if (Error)
                *Error = "GET INDEXDATA index is outside the locked mesh";
            return false;
        }
        {
            Output = static_cast<int>(Value.Geometry().Indices[static_cast<std::size_t>(Index)]);
            return true;
        }
    }

    // ── Glue ─────────────────────────────────────────────────────────

    bool ObjectManager::GlueObjectToLimb(int ObjectId, int TargetObjectId, int LimbId, int Mode, std::string* Error)
    {
        if (Mode < 0 || Mode > 2)
        {
            if (Error)
                *Error = "GLUE OBJECT TO LIMB mode must be between zero and two";
            return false;
        }
        if (ObjectId == TargetObjectId)
        {
            if (Error)
                *Error = "GLUE OBJECT TO LIMB cannot attach an object to itself";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "GLUE OBJECT TO LIMB", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        const Object3D* TargetPointer = RequireObject(TargetObjectId, "GLUE OBJECT TO LIMB", Error);
        if (!TargetPointer)
            return false;
        const Object3D& Target = *TargetPointer;
        if (!RequireLimb(Target, TargetObjectId, LimbId, "GLUE OBJECT TO LIMB", Error))
            return false;
        for (const Object3D* Ancestor = &Target; Ancestor && Ancestor->GluedTargetObject >= 0;)
        {
            if (Ancestor->GluedTargetObject == ObjectId)
            {
                if (Error)
                    *Error = "GLUE OBJECT TO LIMB cannot create an object attachment cycle";
                return false;
            }
            Ancestor = GetObject(Ancestor->GluedTargetObject);
        }
        Object.GluedTargetObject = TargetObjectId;
        Object.GluedTargetLimb = LimbId;
        Object.GlueMode = Mode;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::UnglueObject(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "UNGLUE OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.GluedTargetObject = -1;
        Object.GluedTargetLimb = 0;
        Object.GlueMode = 0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::FixObjectPivot(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "FIX OBJECT PIVOT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        const Mat3 Rot = Object.UseZyxRotation ? Mat3FromEulerZyx(Object.RotX, Object.RotY, Object.RotZ)
                                               : Mat3FromEulerXyz(Object.RotX, Object.RotY, Object.RotZ);
        if (!Object.ApplyPivot)
        {
            Object.ApplyPivot = true;
            Object.PivotMatrix = Rot;
        }
        else
        {
            Object.PivotMatrix = Mat3Mul(Rot, Object.PivotMatrix);
        }
        Object.RotX = 0.0f;
        Object.RotY = 0.0f;
        Object.RotZ = 0.0f;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectRotationXyz(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT ROTATION XYZ", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.UseZyxRotation = false;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectRotationZyx(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT ROTATION ZYX", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.UseZyxRotation = true;
        Object.TransformDirty = true;
        return true;
    }

    // ── Animation ────────────────────────────────────────────────────

    bool ObjectManager::SetObjectSpeed(int ObjectId, float Speed, std::string* Error)
    {
        auto* CheckedObject1 = RequireObject(ObjectId, "SET OBJECT SPEED", Error);
        if (!CheckedObject1)
            return false;
        CheckedObject1->Speed = Speed;
        return true;
    }

    float ObjectManager::ObjectSpeed(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0.0f : It->second.Speed;
    }

    bool ObjectManager::SetObjectFrame(int ObjectId, int Frame, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT FRAME", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        // The new pose must reach the screen even when nothing else moves.
        Object.TransformDirty = true;
        Object.Frame = std::max(0, Frame);
        if (!Object.ObjectKeyframes.empty())
        {
            Object.AnimTimeSeconds = static_cast<double>(Object.Frame) / 25.0;
            SampleObjectKeyframe(Object, Object.Frame);
            return true;
        }
        if (!Object.AnimationClips.empty())
        {
            SelectAnimationClipForFrame(Object, Object.Frame);
            return true;
        }
        if (Object.ImportedAnimationEnabled && Object.AssimpScene && Object.AnimIndex >= 0 &&
            static_cast<unsigned int>(Object.AnimIndex) < Object.AssimpScene->mNumAnimations)
        {
            const aiAnimation* Anim = Object.AssimpScene->mAnimations[Object.AnimIndex];
            if (Anim)
            {
                const double TicksPerSec = (Anim->mTicksPerSecond != 0.0) ? Anim->mTicksPerSecond : 25.0;
                Object.AnimTimeSeconds = static_cast<double>(Object.Frame) / 25.0;
                Object.ActiveAnimDurationTicks = std::max(0.0, Anim->mDuration);
                Object.ActiveAnimTicksPerSec = TicksPerSec;
            }
        }
        return true;
    }

    bool ObjectManager::SetObjectFrame(int ObjectId, float Frame, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT FRAME", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        // The new pose must reach the screen even when nothing else moves.
        Object.TransformDirty = true;
        Object.Frame = std::max(0, static_cast<int>(Frame));
        if (!Object.ObjectKeyframes.empty())
        {
            Object.AnimTimeSeconds = std::max(0.0, static_cast<double>(Frame)) / 25.0;
            SampleObjectKeyframe(Object, std::max(0.0, static_cast<double>(Frame)));
            return true;
        }
        if (!Object.AnimationClips.empty())
        {
            SelectAnimationClipForFrame(Object, Object.Frame);
            // Apply fractional part within the clip.
            const auto& Clip = Object.AnimationClips[static_cast<size_t>(Object.ActiveAnimationClip)];
            const double Frac = static_cast<double>(Frame) - static_cast<double>(Object.Frame);
            Object.AnimTimeSeconds = (static_cast<double>(Object.Frame - Clip.StartFrame) + Frac) / 25.0;
            return true;
        }
        if (Object.ImportedAnimationEnabled && Object.AssimpScene && Object.AnimIndex >= 0 &&
            static_cast<unsigned int>(Object.AnimIndex) < Object.AssimpScene->mNumAnimations)
        {
            const aiAnimation* Anim = Object.AssimpScene->mAnimations[Object.AnimIndex];
            if (Anim)
            {
                const double TicksPerSec = (Anim->mTicksPerSecond != 0.0) ? Anim->mTicksPerSecond : 25.0;
                Object.AnimTimeSeconds = static_cast<double>(Frame) / 25.0;
                Object.ActiveAnimDurationTicks = std::max(0.0, Anim->mDuration);
                Object.ActiveAnimTicksPerSec = TicksPerSec;
            }
        }
        return true;
    }

    int ObjectManager::ObjectFrame(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0 : It->second.Frame;
    }

    bool ObjectManager::PlayObject(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "PLAY OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.Playing = true;
        Object.Looping = false;
        Object.HasPlayRange = false;
        if (Object.AnimTimeSeconds < 0.0)
            Object.AnimTimeSeconds = 0.0;
        return true;
    }

    bool ObjectManager::PlayObject(int ObjectId, int StartFrame, int EndFrame, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "PLAY OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.PlayStartFrame = std::max(0, StartFrame);
        Object.PlayEndFrame = std::max(Object.PlayStartFrame, EndFrame);
        Object.HasPlayRange = true;
        if (!SetObjectFrame(ObjectId, Object.PlayStartFrame, Error))
            return false;
        Object.Playing = true;
        Object.Looping = false;
        return true;
    }

    bool ObjectManager::LoopObject(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "LOOP OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.Playing = true;
        Object.Looping = true;
        Object.HasPlayRange = false;
        return true;
    }

    bool ObjectManager::LoopObject(int ObjectId, int StartFrame, int EndFrame, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "LOOP OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.PlayStartFrame = std::max(0, StartFrame);
        Object.PlayEndFrame = std::max(Object.PlayStartFrame, EndFrame);
        Object.HasPlayRange = true;
        if (!SetObjectFrame(ObjectId, Object.PlayStartFrame, Error))
            return false;
        Object.Playing = true;
        Object.Looping = true;
        return true;
    }

    bool ObjectManager::StopObject(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "STOP OBJECT", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.Playing = false;
        Object.Looping = false;
        Object.HasPlayRange = false;
        return true;
    }

    int ObjectManager::ObjectPlaying(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0 : (It->second.Playing ? 1 : 0);
    }

    int ObjectManager::ObjectLooping(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0 : (It->second.Looping ? 1 : 0);
    }

    bool ObjectManager::SetObjectInterpolation(int ObjectId, int Value, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT INTERPOLATION", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (Value < 0 || Value > 100)
        {
            if (Error)
                *Error = "SET OBJECT INTERPOLATION value must be between zero and one hundred";
            return false;
        }
        Object.Interpolation = Value;
        return true;
    }

    int ObjectManager::ObjectInterpolation(int ObjectId)
    {
        const auto It = Objects_.find(ObjectId);
        return (It == Objects_.end()) ? 0 : It->second.Interpolation;
    }

    bool ObjectManager::SetObjectKeyframe(int ObjectId, int Frame, std::string* Error)
    {
        if (Frame < 0)
        {
            if (Error)
                *Error = "SET OBJECT KEYFRAME frame cannot be negative";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT KEYFRAME", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (Object.LimbNodes.empty() && Object.LimbTransforms.size() < Object.Meshes.size())
        {
            const std::size_t Previous = Object.LimbTransforms.size();
            Object.LimbTransforms.resize(Object.Meshes.size());
            for (std::size_t Index = Previous; Index < Object.Meshes.size(); ++Index)
            {
                const Mesh& MeshValue = Object.Meshes[Index];
                LimbTransform& Transform = Object.LimbTransforms[Index];
                Transform.RotX = MeshValue.LimbRotX;
                Transform.RotY = MeshValue.LimbRotY;
                Transform.RotZ = MeshValue.LimbRotZ;
                Transform.OffX = MeshValue.LimbOffX;
                Transform.OffY = MeshValue.LimbOffY;
                Transform.OffZ = MeshValue.LimbOffZ;
                Transform.ScaleX = MeshValue.LimbScaleX;
                Transform.ScaleY = MeshValue.LimbScaleY;
                Transform.ScaleZ = MeshValue.LimbScaleZ;
                Transform.Hidden = !MeshValue.Visible;
            }
        }
        const std::size_t VisibilityCount = Object.LimbNodes.empty() ? Object.Meshes.size() : Object.LimbNodes.size();
        Object.LimbTransforms.resize(std::max(Object.LimbTransforms.size(), VisibilityCount));
        for (std::size_t LimbIndex = 0; LimbIndex < VisibilityCount; ++LimbIndex)
        {
            bool HasMesh = false;
            bool AnyVisible = false;
            ForEachLimbMesh(Object, static_cast<int>(LimbIndex),
                            [&](const Mesh& MeshValue)
                            {
                                HasMesh = true;
                                AnyVisible = AnyVisible || MeshValue.Visible;
                            });
            if (HasMesh)
                Object.LimbTransforms[LimbIndex].Hidden = !AnyVisible;
        }
        if (Object.LimbTransforms.size() > PortableLimbLimit)
        {
            if (Error)
                *Error = "SET OBJECT KEYFRAME object has too many limbs";
            return false;
        }
        if (!Object.ObjectKeyframes.contains(Frame) && Object.ObjectKeyframes.size() >= PortableKeyframeLimit)
        {
            if (Error)
                *Error = "SET OBJECT KEYFRAME object has too many keyframes";
            return false;
        }
        if (!ValidateLimbHierarchy(Object.LimbTransforms, "SET OBJECT KEYFRAME", Error))
            return false;
        Object.ObjectKeyframes[Frame] = ObjectKeyframe{Object.LimbTransforms};
        Object.ImportedAnimationEnabled = false;
        Object.Frame = Frame;
        Object.AnimTimeSeconds = static_cast<double>(Frame) / 25.0;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ClearObjectKeyframe(int ObjectId, int Frame, std::string* Error)
    {
        if (Frame < 0)
        {
            if (Error)
                *Error = "CLEAR OBJECT KEYFRAME frame cannot be negative";
            return false;
        }
        Object3D* ObjectPointer = RequireObject(ObjectId, "CLEAR OBJECT KEYFRAME", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.ObjectKeyframes.erase(Frame);
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::ClearAllObjectKeyframes(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "CLEAR ALL OBJECT KEYFRAMES", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.ObjectKeyframes.clear();
        Object.ImportedAnimationEnabled = false;
        Object.AnimationClips.clear();
        Object.AnimChannelByNode.clear();
        Object.ActiveAnimationClip = 0;
        Object.ActiveAnimDurationTicks = 0.0;
        Object.AnimTimeSeconds = 0.0;
        Object.Frame = 0;
        Object.Playing = false;
        Object.Looping = false;
        Object.HasPlayRange = false;
        Object.CachedAnimTicks = std::numeric_limits<double>::quiet_NaN();
        Object.CachedNodeGlobals.clear();
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SaveObjectAnimation(const std::string& Path, int ObjectId, std::string* Error) const
    {
        static constexpr std::array<char, 8> Magic{'O', 'D', 'B', 'A', 'N', 'I', '0', '1'};
        const Object3D* ObjectPointer = RequireObject(ObjectId, "SAVE OBJECT ANIMATION", Error);
        if (!ObjectPointer)
            return false;
        const Object3D& Object = *ObjectPointer;
        if (Object.LimbTransforms.size() > PortableLimbLimit || Object.ObjectKeyframes.size() > PortableKeyframeLimit)
        {
            if (Error)
                *Error = "SAVE OBJECT ANIMATION exceeds keyframe or limb limits";
            return false;
        }
        if (!Object.ObjectKeyframes.empty())
        {
            std::size_t ValidatedSize = 0;
            if (!AnimationFileSize(ValidatedSize, Object.LimbTransforms.size(), Object.ObjectKeyframes.size(),
                                   "SAVE OBJECT ANIMATION", Error))
                return false;
        }
        std::map<int, ObjectKeyframe> Keyframes = Object.ObjectKeyframes;
        if (Keyframes.empty() && !SampleImportedKeyframes(Keyframes, Object, Error))
            return false;
        if (Keyframes.empty())
            Keyframes.emplace(Object.Frame, ObjectKeyframe{Object.LimbTransforms});
        const uint32_t LimbCount = static_cast<uint32_t>(Object.LimbTransforms.size());
        std::size_t ExpectedSize;
        if (!AnimationFileSize(ExpectedSize, LimbCount, Keyframes.size(), "SAVE OBJECT ANIMATION", Error))
            return false;
        PortableWriter Writer;
        Writer.Bytes(Magic.data(), Magic.size());
        Writer.U32(1);
        Writer.U32(LimbCount);
        Writer.U32(static_cast<uint32_t>(Keyframes.size()));
        for (const auto& [Frame, Keyframe] : Keyframes)
        {
            if (Frame < 0 || Keyframe.Limbs.size() != LimbCount)
            {
                if (Error)
                    *Error = "SAVE OBJECT ANIMATION found an inconsistent keyframe";
                return false;
            }
            if (!ValidateLimbHierarchy(Keyframe.Limbs, "SAVE OBJECT ANIMATION", Error))
                return false;
            Writer.I32(Frame);
            for (const LimbTransform& Transform : Keyframe.Limbs)
                if (!WriteLimbTransform(Writer, Transform, Error))
                    return false;
        }
        if (Writer.Data().size() != ExpectedSize)
        {
            if (Error)
                *Error = "SAVE OBJECT ANIMATION produced an inconsistent portable file";
            return false;
        }
        if (!Writer.Status(Error))
            return false;
        std::string Resolved;
        if (!ImageStore::ResolveAssetPath(Resolved, Path, Error))
            return false;
        if (!WritePortableFile(Resolved, Writer.Data(), "SAVE OBJECT ANIMATION", Error))
            return false;
        return true;
    }

    bool ObjectManager::AppendObjectAnimation(const std::string& ModelPath, const std::string& AnimationPath,
                                              int ObjectId, int StartFrame, std::string* Error)
    {
        static constexpr std::array<char, 8> Magic{'O', 'D', 'B', 'A', 'N', 'I', '0', '1'};
        if (StartFrame < 0)
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION start frame cannot be negative";
            return false;
        }
        std::string Model;
        if (!ImageStore::ResolveAssetPath(Model, ModelPath, Error))
            return false;
        std::error_code OperationError;
        if (!std::filesystem::is_regular_file(Model, OperationError) || OperationError)
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION cannot open model '" + Model + "'";
            return false;
        }
        std::string ModelSignature;
        if (!LoadModelStructureSignature(ModelSignature, Model, Kernel_.RuntimeCompatibility().AddSyntheticObjectRoot(),
                                         Error))
            return false;
        if (!Objects_.contains(ObjectId))
            if (!LoadObject(Model, ObjectId, 0, 0, Error))
                return false;
        Object3D* ObjectPointer = RequireObject(ObjectId, "APPEND OBJECT ANIMATION", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        const std::string ObjectSignature = Object.AssimpScene || Object.ModelStructureSignature.empty()
                                                ? ObjectStructureSignature(Object)
                                                : Object.ModelStructureSignature;
        if (ModelSignature.empty() || ObjectSignature != ModelSignature)
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION model structure does not match object " + std::to_string(ObjectId);
            return false;
        }

        std::string Animation;
        if (!ImageStore::ResolveAssetPath(Animation, AnimationPath, Error))
            return false;
        std::vector<uint8_t> Data;
        if (!ReadPortableFile(Data, Animation, "APPEND OBJECT ANIMATION", Error))
            return false;
        PortableReader Reader(Data, "APPEND OBJECT ANIMATION");
        Reader.Expect(Magic);
        if (!Reader.Status(Error))
            return false;
        if (Reader.U32() != 1)
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION version is unsupported";
            return false;
        }
        const uint32_t LimbCount = Reader.U32();
        const uint32_t KeyframeCount = Reader.U32();
        if (LimbCount > PortableLimbLimit || KeyframeCount == 0 || KeyframeCount > PortableKeyframeLimit)
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION has invalid keyframe or limb counts";
            return false;
        }
        std::size_t ExpectedSize = 0;
        if (!AnimationFileSize(ExpectedSize, LimbCount, KeyframeCount, "APPEND OBJECT ANIMATION", Error))
            return false;
        if (ExpectedSize != Data.size())
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION has an inconsistent file size";
            return false;
        }
        if (LimbCount != Object.LimbTransforms.size())
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION limb count does not match object " + std::to_string(ObjectId);
            return false;
        }

        std::map<int, ObjectKeyframe> Imported;
        for (uint32_t KeyframeIndex = 0; KeyframeIndex < KeyframeCount; ++KeyframeIndex)
        {
            const int Frame = Reader.I32();
            if (Frame < 0 || Imported.contains(Frame))
            {
                if (Error)
                    *Error = "APPEND OBJECT ANIMATION found an invalid or duplicate frame";
                return false;
            }
            ObjectKeyframe Keyframe;
            Keyframe.Limbs.reserve(LimbCount);
            for (uint32_t LimbIndex = 0; LimbIndex < LimbCount; ++LimbIndex)
            {
                LimbTransform Transform = ReadLimbTransform(Reader);
                Keyframe.Limbs.push_back(Transform);
            }
            if (!ValidateLimbHierarchy(Keyframe.Limbs, "APPEND OBJECT ANIMATION", Error))
                return false;
            Imported.emplace(Frame, std::move(Keyframe));
        }
        if (!Reader.Status(Error))
            return false;
        if (!Reader.AtEnd())
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION found trailing data";
            return false;
        }
        const int SourceStart = Imported.begin()->first;
        if (!Object.ObjectKeyframes.empty() && StartFrame < Object.ObjectKeyframes.rbegin()->first)
        {
            if (Error)
                *Error = "APPEND OBJECT ANIMATION can only append at the end of existing keyframes";
            return false;
        }
        for (auto& [Frame, Keyframe] : Imported)
        {
            const int64_t Shifted = static_cast<int64_t>(StartFrame) + Frame - SourceStart;
            if (Shifted < 0 || Shifted > std::numeric_limits<int>::max())
            {
                if (Error)
                    *Error = "APPEND OBJECT ANIMATION frame range is out of bounds";
                return false;
            }
            Object.ObjectKeyframes[static_cast<int>(Shifted)] = std::move(Keyframe);
        }
        Object.AnimationClips.clear();
        Object.ImportedAnimationEnabled = false;
        Object.AnimChannelByNode.clear();
        Object.ActiveAnimDurationTicks = 0.0;
        Object.CachedAnimTicks = std::numeric_limits<double>::quiet_NaN();
        if (!SetObjectFrame(ObjectId, StartFrame, Error))
            return false;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::UpdateObjectKeyframeAnimation(Object3D& Object, double ElapsedSeconds)
    {
        if (Object.ObjectKeyframes.empty())
            return false;
        const double First = Object.ObjectKeyframes.begin()->first;
        const double Last = Object.ObjectKeyframes.rbegin()->first;
        double RangeStart = First;
        double RangeEnd = Last;
        if (Object.HasPlayRange)
        {
            RangeStart = std::clamp(static_cast<double>(Object.PlayStartFrame), First, Last);
            RangeEnd = std::clamp(static_cast<double>(Object.PlayEndFrame), RangeStart, Last);
        }
        double Frame = Object.AnimTimeSeconds * 25.0;
        if (Object.Playing)
        {
            const double SpeedScale = Object.Speed == 0.0f ? 1.0 : static_cast<double>(Object.Speed) / 20.0;
            Frame += ElapsedSeconds * 25.0 * SpeedScale;
        }
        const double Span = RangeEnd - RangeStart;
        if (Object.Looping && Span > 0.0)
        {
            Frame = RangeStart + std::fmod(Frame - RangeStart, Span);
            if (Frame < RangeStart)
                Frame += Span;
        }
        else
        {
            if (Frame >= RangeEnd)
            {
                Frame = RangeEnd;
                if (Object.Playing)
                    Object.Playing = false;
            }
            Frame = std::max(Frame, RangeStart);
        }
        Object.AnimTimeSeconds = Frame / 25.0;
        Object.Frame = static_cast<int>(std::floor(Frame));
        SampleObjectKeyframe(Object, Frame);
        return true;
    }

    // ── Size queries ─────────────────────────────────────────────────

    float ObjectManager::ObjectSizeX(int ObjectId, int ActualSize)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || It->second.Meshes.empty())
            return 0.0f;
        const AABB Bounds = ComputeObjectAabb(Kernel_, It->second);
        const float Scale = ActualSize == 1 ? std::abs(It->second.ScaleX) : 1.0f;
        return std::abs(Bounds.MaxX - Bounds.MinX) * Scale;
    }

    float ObjectManager::ObjectSizeY(int ObjectId, int ActualSize)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || It->second.Meshes.empty())
            return 0.0f;
        const AABB Bounds = ComputeObjectAabb(Kernel_, It->second);
        const float Scale = ActualSize == 1 ? std::abs(It->second.ScaleY) : 1.0f;
        return std::abs(Bounds.MaxY - Bounds.MinY) * Scale;
    }

    float ObjectManager::ObjectSizeZ(int ObjectId, int ActualSize)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || It->second.Meshes.empty())
            return 0.0f;
        const AABB Bounds = ComputeObjectAabb(Kernel_, It->second);
        const float Scale = ActualSize == 1 ? std::abs(It->second.ScaleZ) : 1.0f;
        return std::abs(Bounds.MaxZ - Bounds.MinZ) * Scale;
    }

    float ObjectManager::ObjectSize(int ObjectId, int ActualSize)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end() || It->second.Meshes.empty())
            return 0.0f;
        float Radius = It->second.BoundingRadius;
        if (Radius < 0.0f)
        {
            Radius = 0.0f;
            for (const Mesh& MeshValue : It->second.Meshes)
            {
                std::vector<Vec3> Vertices;
                Kernel_.Renderer().ObjectMeshVertices(It->second, MeshValue, Vertices);
                for (const Vec3& Local : Vertices)
                {
                    Radius = std::max(Radius, std::sqrt(Local.X * Local.X + Local.Y * Local.Y + Local.Z * Local.Z));
                }
            }
        }
        if (ActualSize == 1)
        {
            const float AverageScale =
                (std::abs(It->second.ScaleX) + std::abs(It->second.ScaleY) + std::abs(It->second.ScaleZ)) / 3.0f;
            Radius *= AverageScale;
        }
        return Radius;
    }

    // ── DBPro extensions ────────────────────────────────────────────────

    void ObjectManager::SetObjectRadius(int ObjectId, float Radius)
    {
        auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return;
        // The radius bounds the object for view culling too.
        Obj->BoundingRadius = Radius;
        Obj->TransformDirty = true;
    }

    float ObjectManager::IntersectObject(int ObjectId, float X1, float Y1, float Z1, float X2, float Y2, float Z2)
    {
        const auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return 0.0f;
        return RayObjectDistance(Kernel_, *Obj, {X1, Y1, Z1}, {X2, Y2, Z2});
    }

    float ObjectManager::LimbScaleX(int ObjectId, int LimbId)
    {
        const auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return 100.0f;
        if (LimbId >= 0 && LimbId < static_cast<int>(Obj->Meshes.size()))
            return Obj->Meshes[LimbId].LimbScaleX * 100.0f;
        return 100.0f;
    }

    float ObjectManager::LimbScaleY(int ObjectId, int LimbId)
    {
        const auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return 100.0f;
        if (LimbId >= 0 && LimbId < static_cast<int>(Obj->Meshes.size()))
            return Obj->Meshes[LimbId].LimbScaleY * 100.0f;
        return 100.0f;
    }

    float ObjectManager::LimbScaleZ(int ObjectId, int LimbId)
    {
        const auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return 100.0f;
        if (LimbId >= 0 && LimbId < static_cast<int>(Obj->Meshes.size()))
            return Obj->Meshes[LimbId].LimbScaleZ * 100.0f;
        return 100.0f;
    }

    int ObjectManager::CheckLimbLink(int ObjectId, int LimbId)
    {
        const auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return -1;
        if (LimbId >= 0 && LimbId < static_cast<int>(Obj->LimbTransforms.size()))
            return Obj->LimbTransforms[LimbId].ParentLimb;
        return -1;
    }

    void ObjectManager::DeleteAllObjects()
    {
        std::vector<int> ObjectIds;
        ObjectIds.reserve(Objects_.size());
        for (const auto& [Id, Object] : Objects_)
        {
            (void)Object;
            ObjectIds.push_back(Id);
        }
        for (const int Id : ObjectIds)
            DeleteObject(Id);
    }

    Object3D* ObjectManager::RequireObject(int ObjectId, const char* Command, std::string* Error)
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing object " + std::to_string(ObjectId);
            return nullptr;
        }
        return &It->second;
    }

    const Object3D* ObjectManager::RequireObject(int ObjectId, const char* Command, std::string* Error) const
    {
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing object " + std::to_string(ObjectId);
            return nullptr;
        }
        return &It->second;
    }

    ParticleEmitter* ObjectManager::RequireParticles(int ParticleId, const char* Command, std::string* Error)
    {
        const auto It = Particles_.find(ParticleId);
        if (It == Particles_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing particles " + std::to_string(ParticleId);
            return nullptr;
        }
        return &It->second;
    }

    const ParticleEmitter* ObjectManager::RequireParticles(int ParticleId, const char* Command,
                                                           std::string* Error) const
    {
        const auto It = Particles_.find(ParticleId);
        if (It == Particles_.end())
        {
            if (Error)
                *Error = std::string(Command) + " requires existing particles " + std::to_string(ParticleId);
            return nullptr;
        }
        return &It->second;
    }

    bool ObjectManager::RequireLimb(const Object3D& Object, int ObjectId, int LimbId, const char* Command,
                                    std::string* Error) const
    {
        const std::size_t Count = std::max(Object.LimbNodes.size(), Object.Meshes.size());
        const bool RemovedImportedLimb = !Object.LimbNodes.empty() && LimbId >= 0 &&
                                         static_cast<std::size_t>(LimbId) < Object.LimbNodes.size() &&
                                         Object.LimbNodes[static_cast<std::size_t>(LimbId)] == nullptr;
        if (LimbId < 0 || static_cast<std::size_t>(LimbId) >= Count || RemovedImportedLimb)
        {
            if (Error)
                *Error = std::string(Command) + " requires existing limb " + std::to_string(LimbId) + " on object " +
                         std::to_string(ObjectId);
            return false;
        }
        return true;
    }

    void ObjectManager::RecalculateObjectBounds(Object3D& Object)
    {
        const AABB Bounds = ComputeObjectAabb(Kernel_, Object);
        const float X = std::max(std::abs(Bounds.MinX), std::abs(Bounds.MaxX));
        const float Y = std::max(std::abs(Bounds.MinY), std::abs(Bounds.MaxY));
        const float Z = std::max(std::abs(Bounds.MinZ), std::abs(Bounds.MaxZ));
        Object.BoundingRadius = std::sqrt(X * X + Y * Y + Z * Z);
        Object.RenderBoundingRadius = Object.BoundingRadius;
        Object.TransformDirty = true;
    }

    float ObjectManager::RenderBoundingRadius(Object3D& Object)
    {
        if (Object.RenderBoundingRadius < 0.0f)
        {
            const AABB Bounds = ComputeObjectAabb(Kernel_, Object);
            const float X = std::max(std::abs(Bounds.MinX), std::abs(Bounds.MaxX));
            const float Y = std::max(std::abs(Bounds.MinY), std::abs(Bounds.MaxY));
            const float Z = std::max(std::abs(Bounds.MinZ), std::abs(Bounds.MaxZ));
            Object.RenderBoundingRadius = std::sqrt(X * X + Y * Y + Z * Z);
        }
        return Object.RenderBoundingRadius;
    }

    Mesh* ObjectManager::RequireLockedVertexMesh(const char* Command, std::string* Error)
    {
        if (VertexLockKind_ == VertexLockKind::Mesh)
        {
            const auto It = Meshes_.find(VertexLockId_);
            if (It != Meshes_.end() && !It->second.empty())
                return &It->second.front();
        }
        else if (VertexLockKind_ == VertexLockKind::Limb)
        {
            const auto It = Objects_.find(VertexLockId_);
            if (It != Objects_.end())
            {
                if (Mesh* Value = Kernel_.Renderer().FindLimbMesh(It->second, VertexLockLimb_))
                    return Value;
            }
        }
        {
            if (Error)
                *Error = std::string(Command) + " requires locked vertex data";
            return nullptr;
        }
    }

    const Mesh* ObjectManager::RequireLockedVertexMesh(const char* Command, std::string* Error) const
    {
        if (VertexLockKind_ == VertexLockKind::Mesh)
        {
            const auto It = Meshes_.find(VertexLockId_);
            if (It != Meshes_.end() && !It->second.empty())
                return &It->second.front();
        }
        else if (VertexLockKind_ == VertexLockKind::Limb)
        {
            const auto It = Objects_.find(VertexLockId_);
            if (It != Objects_.end())
            {
                if (const Mesh* Value = Kernel_.Renderer().FindLimbMesh(It->second, VertexLockLimb_))
                    return Value;
            }
        }
        {
            if (Error)
                *Error = std::string(Command) + " requires locked vertex data";
            return nullptr;
        }
    }

    void ObjectManager::NotifyObjectCreated(int ObjectId)
    {
        Kernel_.Gpu().DeleteObjectEffect(ObjectId);
        const auto It = Objects_.find(ObjectId);
        if (It == Objects_.end())
            return;
        const Object3D& Object = It->second;
        const float Size = std::max({ObjectSizeX(ObjectId), ObjectSizeY(ObjectId), ObjectSizeZ(ObjectId), 1.0f});
        Kernel_.Camera().AutoPosition(Object.PosX, Object.PosY, Object.PosZ, Size);
    }

    bool ObjectManager::MakeParticles(int ParticleId, int ImageId, int Frequency, float Radius, std::string* Error)
    {
        if (ParticleId < 1 || ParticleId > 65535)
        {
            if (Error)
                *Error = "MAKE PARTICLES number must be between 1 and 65535";
            return false;
        }
        if (Particles_.contains(ParticleId))
        {
            if (Error)
                *Error = "MAKE PARTICLES number already exists: " + std::to_string(ParticleId);
            return false;
        }
        if (ImageId < 0 || ImageId > 65535)
        {
            if (Error)
                *Error = "MAKE PARTICLES image number must be between 0 and 65535";
            return false;
        }
        if (Frequency <= 0)
        {
            if (Error)
                *Error = "MAKE PARTICLES frequency must be positive";
            return false;
        }
        if (Radius <= 0.0f)
        {
            if (Error)
                *Error = "MAKE PARTICLES radius must be positive";
            return false;
        }
        ParticleEmitter Emitter;
        Emitter.ImageId = ImageId;
        Emitter.FlushSize = Frequency;
        Emitter.Radius = Radius;
        Emitter.RandomState = 0x9E3779B9u ^ static_cast<uint32_t>(ParticleId);
        Particles_.emplace(ParticleId, std::move(Emitter));
        Kernel_.Renderer().InvalidateTriCache();
        Kernel_.Camera().AutoPosition(0.0f, 0.0f, 0.0f, Radius);
        return true;
    }

    bool ObjectManager::MakeVolumeParticles(ParticleKind Kind, int ParticleId, int ImageId, int Frequency, float X,
                                            float Y, float Z, float Width, float Height, float Depth,
                                            const char* Command, std::string* Error)
    {
        if (!(Width > 0.0f && Height > 0.0f && Depth > 0.0f) || !std::isfinite(Width) || !std::isfinite(Height) ||
            !std::isfinite(Depth))
        {
            if (Error)
                *Error = std::string(Command) + " requires a positive width, height and depth";
            return false;
        }
        const float Radius = std::max({Width, Height, Depth});
        if (!MakeParticles(ParticleId, ImageId, Frequency, Radius, Error))
            return false;
        ParticleEmitter& Emitter = Particles_.at(ParticleId);
        Emitter.Kind = Kind;
        Emitter.Volume = {Width / Radius, Height / Radius, Depth / Radius};
        Emitter.PosX = X;
        Emitter.PosY = Y;
        Emitter.PosZ = Z;
        // Fire and snow cross the box in the half second they live, and fall under no gravity of their own.
        Emitter.EmissionVelocity = Emitter.Volume.Y * 2.0f;
        Emitter.Gravity = 0.0f;
        Emitter.FloorEnabled = 0;
        Kernel_.Camera().AutoPosition(X, Y, Z, Radius);
        return true;
    }

    bool ObjectManager::DeleteParticles(int ParticleId, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "DELETE PARTICLES", Error);
        if (!CheckedParticles1)
            return false;
        Particles_.erase(ParticleId);
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::PositionParticles(int ParticleId, float X, float Y, float Z, std::string* Error)
    {
        ParticleEmitter* EmitterPointer = RequireParticles(ParticleId, "POSITION PARTICLES", Error);
        if (!EmitterPointer)
            return false;
        ParticleEmitter& Emitter = *EmitterPointer;
        Emitter.PosX = X;
        Emitter.PosY = Y;
        Emitter.PosZ = Z;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::RotateParticles(int ParticleId, float X, float Y, float Z, std::string* Error)
    {
        ParticleEmitter* EmitterPointer = RequireParticles(ParticleId, "ROTATE PARTICLES", Error);
        if (!EmitterPointer)
            return false;
        ParticleEmitter& Emitter = *EmitterPointer;
        Emitter.RotX = X;
        Emitter.RotY = Y;
        Emitter.RotZ = Z;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::PositionParticleEmissions(int ParticleId, float X, float Y, float Z, std::string* Error)
    {
        ParticleEmitter* EmitterPointer = RequireParticles(ParticleId, "POSITION PARTICLE EMISSIONS", Error);
        if (!EmitterPointer)
            return false;
        ParticleEmitter& Emitter = *EmitterPointer;
        Emitter.EmissionPosition = {X / Emitter.Radius, Y / Emitter.Radius, Z / Emitter.Radius};
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::ColorParticles(int ParticleId, int Red, int Green, int Blue, std::string* Error)
    {
        ParticleEmitter* EmitterPointer = RequireParticles(ParticleId, "COLOR PARTICLES", Error);
        if (!EmitterPointer)
            return false;
        ParticleEmitter& Emitter = *EmitterPointer;
        Emitter.EmissionColor = {static_cast<float>(std::clamp(Red, 0, 255)) / 255.0f,
                                 static_cast<float>(std::clamp(Green, 0, 255)) / 255.0f,
                                 static_cast<float>(std::clamp(Blue, 0, 255)) / 255.0f, 1.0f};
        return true;
    }

    bool ObjectManager::SetParticleSpeed(int ParticleId, float SecondsPerFrame, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE SPEED", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->SecondsPerFrame = SecondsPerFrame;
        return true;
    }

    bool ObjectManager::SetParticleEmissions(int ParticleId, int Emissions, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE EMISSIONS", Error);
        if (!CheckedParticles1)
            return false;
        if (Emissions < 0)
        {
            if (Error)
                *Error = "SET PARTICLE EMISSIONS value must not be negative";
            return false;
        }
        CheckedParticles1->EmissionsPerFrame = Emissions;
        return true;
    }

    bool ObjectManager::SetParticleVelocity(int ParticleId, float Velocity, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE VELOCITY", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->EmissionVelocity = Velocity;
        return true;
    }

    bool ObjectManager::SetParticleGravity(int ParticleId, float Gravity, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE GRAVITY", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->Gravity = Gravity;
        return true;
    }

    bool ObjectManager::SetParticleLife(int ParticleId, int LifePercent, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE LIFE", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->LifePercent = LifePercent;
        return true;
    }

    bool ObjectManager::SetParticleFloor(int ParticleId, int Enabled, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE FLOOR", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->FloorEnabled = Enabled;
        return true;
    }

    bool ObjectManager::SetParticleChaos(int ParticleId, float Chaos, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET PARTICLE CHAOS", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->Chaos = Chaos / 1000.0f;
        return true;
    }

    bool ObjectManager::GhostParticlesOn(int ParticleId, int Mode, float Percentage, std::string* Error)
    {
        if (Mode < 0 || Mode > 5)
        {
            if (Error)
                *Error = "GHOST PARTICLES ON mode must be between 0 and 5";
            return false;
        }
        if (Percentage != -1.0f && (Percentage < 0.0f || Percentage > 100.0f))
        {
            if (Error)
                *Error = "GHOST PARTICLES ON percentage must be between 0 and 100";
            return false;
        }
        ParticleEmitter* EmitterPointer = RequireParticles(ParticleId, "GHOST PARTICLES ON", Error);
        if (!EmitterPointer)
            return false;
        ParticleEmitter& Emitter = *EmitterPointer;
        Emitter.Ghost = true;
        Emitter.GhostMode = Mode;
        Emitter.GhostPercentage = Percentage;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::GhostParticlesOff(int ParticleId, std::string* Error)
    {
        ParticleEmitter* EmitterPointer = RequireParticles(ParticleId, "GHOST PARTICLES OFF", Error);
        if (!EmitterPointer)
            return false;
        ParticleEmitter& Emitter = *EmitterPointer;
        Emitter.Ghost = false;
        Emitter.GhostMode = -1;
        Emitter.GhostPercentage = -1.0f;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::HideParticles(int ParticleId, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "HIDE PARTICLES", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->Visible = false;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    bool ObjectManager::ShowParticles(int ParticleId, std::string* Error)
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SHOW PARTICLES", Error);
        if (!CheckedParticles1)
            return false;
        CheckedParticles1->Visible = true;
        Kernel_.Renderer().InvalidateTriCache();
        return true;
    }

    int ObjectManager::ParticlesExist(int ParticleId) const
    {
        return Particles_.contains(ParticleId) ? 1 : 0;
    }

    bool ObjectManager::ParticlesPositionX(float& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "PARTICLES POSITION X", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->PosX;
            return true;
        }
    }

    bool ObjectManager::ParticlesPositionY(float& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "PARTICLES POSITION Y", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->PosY;
            return true;
        }
    }

    bool ObjectManager::ParticlesPositionZ(float& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "PARTICLES POSITION Z", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->PosZ;
            return true;
        }
    }

    bool ObjectManager::ParticlesRotationX(float& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET VECTOR3 TO PARTICLES ROTATION", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->RotX;
            return true;
        }
    }

    bool ObjectManager::ParticlesRotationY(float& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET VECTOR3 TO PARTICLES ROTATION", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->RotY;
            return true;
        }
    }

    bool ObjectManager::ParticlesRotationZ(float& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "SET VECTOR3 TO PARTICLES ROTATION", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->RotZ;
            return true;
        }
    }

    bool ObjectManager::ParticleCount(std::size_t& Output, int ParticleId, std::string* Error) const
    {
        auto* CheckedParticles1 = RequireParticles(ParticleId, "PARTICLE COUNT", Error);
        if (!CheckedParticles1)
            return false;
        {
            Output = CheckedParticles1->Particles.size();
            return true;
        }
    }

    bool ObjectManager::SetObjectTransparency(int ObjectId, int Mode, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT TRANSPARENCY", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.TransparencyMode = Mode;
        Object.TransparentBlack = Mode != 0 && Mode != 7;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::AutomaticObjectCollision(int ObjectId, float Radius, int Response, std::string* Error)
    {
        Object3D* Object = RequireObject(ObjectId, "AUTOMATIC OBJECT COLLISION", Error);
        if (!Object)
            return false;
        if (Radius <= 0.0f)
        {
            AutomaticObjects_.erase(ObjectId);
            return true;
        }
        Object->CollisionEnabled = true;
        AutomaticObjects_[ObjectId] = {Vec3{Object->PosX, Object->PosY, Object->PosZ}, Radius, Response};
        return true;
    }

    bool ObjectManager::AutomaticCameraCollision(int CameraId, float Radius, int Response, std::string* Error)
    {
        CameraSystem& Camera = Kernel_.Camera();
        if (Camera.CameraExist(CameraId) == 0)
        {
            if (Error)
                *Error = "AUTOMATIC CAMERA COLLISION requires existing camera " + std::to_string(CameraId);
            return false;
        }
        if (Radius <= 0.0f)
        {
            AutomaticCameras_.erase(CameraId);
            return true;
        }
        Vec3 Position{};
        if (!Camera.PositionX(Position.X, CameraId, Error) || !Camera.PositionY(Position.Y, CameraId, Error) ||
            !Camera.PositionZ(Position.Z, CameraId, Error))
            return false;
        AutomaticCameras_[CameraId] = {Position, Radius, Response};
        return true;
    }

    void ObjectManager::CalculateAutomaticCollision()
    {
        if (AutomaticObjects_.empty() && AutomaticCameras_.empty())
            return;
        std::vector<SlideTriangle> Triangles;
        // The triangles of the objects near the sweep, in world space; Self is the collider's own object, if any.
        const auto GatherTriangles = [&](int Self, const Vec3& From, const Vec3& To, float Radius)
        {
            Triangles.clear();
            const Vec3 Middle{(From.X + To.X) * 0.5f, (From.Y + To.Y) * 0.5f, (From.Z + To.Z) * 0.5f};
            const Vec3 Half{(To.X - From.X) * 0.5f, (To.Y - From.Y) * 0.5f, (To.Z - From.Z) * 0.5f};
            const float Reach = std::sqrt(Vec3Dot(Half, Half)) + Radius;
            for (auto& [Id, Object] : Objects_)
            {
                if (Id == Self || !(GlobalCollisionEnabled_ || Object.CollisionEnabled) || Object.LockToScreen)
                    continue;
                const Vec3 Origin = Kernel_.Renderer().WorldObjectOrigin(Object);
                const Vec3 Away{Origin.X - Middle.X, Origin.Y - Middle.Y, Origin.Z - Middle.Z};
                const float Near =
                    Reach + RenderBoundingRadius(Object) *
                                std::max({std::abs(Object.ScaleX), std::abs(Object.ScaleY), std::abs(Object.ScaleZ)});
                if (Vec3Dot(Away, Away) > Near * Near)
                    continue;
                for (const Mesh& MeshValue : Object.Meshes)
                {
                    const MeshGeometry& Geometry = MeshValue.Geometry();
                    for (std::size_t Index = 0; Index + 2 < Geometry.Indices.size(); Index += 3)
                    {
                        const uint32_t I0 = Geometry.Indices[Index];
                        const uint32_t I1 = Geometry.Indices[Index + 1];
                        const uint32_t I2 = Geometry.Indices[Index + 2];
                        if (I0 >= Geometry.Vertices.size() || I1 >= Geometry.Vertices.size() ||
                            I2 >= Geometry.Vertices.size())
                            continue;
                        Triangles.push_back(MakeSlideTriangle(
                            Kernel_.Renderer().WorldMeshPoint(Object, MeshValue, Geometry.Vertices[I0]),
                            Kernel_.Renderer().WorldMeshPoint(Object, MeshValue, Geometry.Vertices[I1]),
                            Kernel_.Renderer().WorldMeshPoint(Object, MeshValue, Geometry.Vertices[I2])));
                    }
                }
            }
        };
        const auto Resolve = [&](AutomaticCollider& Collider, int Self, const Vec3& Position)
        {
            const Vec3 Radii{Collider.Radius, Collider.Radius, Collider.Radius};
            GatherTriangles(Self, Collider.LastPosition, Position, Collider.Radius);
            const EllipsoidSlideResult Slide = SlideEllipsoid(Triangles, Collider.LastPosition, Position, Radii);
            return !Slide.Clipped ? Position : Collider.Response == 0 ? Collider.LastPosition : Slide.Position;
        };
        for (auto It = AutomaticObjects_.begin(); It != AutomaticObjects_.end();)
        {
            const auto Object = Objects_.find(It->first);
            if (Object == Objects_.end())
            {
                It = AutomaticObjects_.erase(It);
                continue;
            }
            const Vec3 Position{Object->second.PosX, Object->second.PosY, Object->second.PosZ};
            const Vec3 Free = Resolve(It->second, It->first, Position);
            if (Free.X != Position.X || Free.Y != Position.Y || Free.Z != Position.Z)
                PositionObject(It->first, Free.X, Free.Y, Free.Z);
            It->second.LastPosition = Free;
            ++It;
        }
        CameraSystem& Camera = Kernel_.Camera();
        for (auto It = AutomaticCameras_.begin(); It != AutomaticCameras_.end();)
        {
            Vec3 Position{};
            if (Camera.CameraExist(It->first) == 0 || !Camera.PositionX(Position.X, It->first) ||
                !Camera.PositionY(Position.Y, It->first) || !Camera.PositionZ(Position.Z, It->first))
            {
                It = AutomaticCameras_.erase(It);
                continue;
            }
            const Vec3 Free = Resolve(It->second, 0, Position);
            if ((Free.X != Position.X || Free.Y != Position.Y || Free.Z != Position.Z) &&
                !Camera.PositionCamera(It->first, Free.X, Free.Y, Free.Z))
                DBC_LOG_WARN("Automatic collision could not place camera {}", It->first);
            It->second.LastPosition = Free;
            ++It;
        }
    }

    float ObjectManager::ObjectCollisionRadius(int ObjectId)
    {
        const auto* Obj = GetObject(ObjectId);
        if (!Obj)
            return 0.0f;
        return Obj->BoundingRadius > 0.0f ? Obj->BoundingRadius : 0.0f;
    }

    float ObjectManager::ObjectCollisionCenterX(int ObjectId)
    {
        const auto* Obj = GetObject(ObjectId);
        return Obj ? Obj->PosX : 0.0f;
    }

    float ObjectManager::ObjectCollisionCenterY(int ObjectId)
    {
        const auto* Obj = GetObject(ObjectId);
        return Obj ? Obj->PosY : 0.0f;
    }

    float ObjectManager::ObjectCollisionCenterZ(int ObjectId)
    {
        const auto* Obj = GetObject(ObjectId);
        return Obj ? Obj->PosZ : 0.0f;
    }

    bool ObjectManager::SetObjectDiffuse(int ObjectId, uint32_t Color, int VertexDiffuseMode, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT DIFFUSE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        if (VertexDiffuseMode == 1)
        {
            Object.DiffuseColor = 0xFFFFFFFFu;
            const uint32_t VertexColor = Color | 0xFF000000u;
            for (Mesh& Value : Object.Meshes)
            {
                MeshGeometry& Geometry = Value.EditGeometry();
                Geometry.DiffuseColors.assign(Geometry.Vertices.size(), VertexColor);
            }
        }
        else
        {
            Object.DiffuseColor = Color;
        }
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectNormals(int ObjectId, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT NORMALS", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        for (Mesh& Value : Object.Meshes)
            RendererSystem::RegenerateMeshNormals(Value);
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectSmoothing(int ObjectId, float Percentage, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT SMOOTHING", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        for (Mesh& Value : Object.Meshes)
            SmoothMeshNormals(Value, Percentage);
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectAmbience(int ObjectId, uint32_t Color, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT AMBIENCE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.AmbientColor = Color;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectSpecular(int ObjectId, uint32_t Color, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT SPECULAR", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.SpecularColor = Color;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectSpecularPower(int ObjectId, float Power, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT SPECULAR POWER", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.SpecularPower = Power;
        Object.TransformDirty = true;
        return true;
    }

    bool ObjectManager::SetObjectEmissive(int ObjectId, uint32_t Color, std::string* Error)
    {
        Object3D* ObjectPointer = RequireObject(ObjectId, "SET OBJECT EMISSIVE", Error);
        if (!ObjectPointer)
            return false;
        Object3D& Object = *ObjectPointer;
        Object.EmissiveColor = Color;
        Object.TransformDirty = true;
        return true;
    }

} // namespace dbc
