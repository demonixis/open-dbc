#include "renderer_transforms.hpp"
#include "engine_kernel.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"

#include <utility>

namespace dbc
{
    namespace
    {
        // Brings the cached node transforms up to the object's animation time and clip.
        void RefreshNodeGlobals(const Object3D& Object)
        {
            double TimeTicks = Object.AnimTimeSeconds * Object.ActiveAnimTicksPerSec;
            if (Object.ActiveAnimDurationTicks > 0.0)
            {
                if (Object.Looping)
                {
                    TimeTicks = std::fmod(TimeTicks, Object.ActiveAnimDurationTicks);
                    if (TimeTicks < 0.0)
                        TimeTicks += Object.ActiveAnimDurationTicks;
                }
                else
                {
                    TimeTicks = std::clamp(TimeTicks, 0.0, Object.ActiveAnimDurationTicks);
                }
            }
            if (TimeTicks != Object.CachedAnimTicks || Object.ActiveAnimationClip != Object.CachedAnimClip)
            {
                Object.CachedNodeGlobals.clear();
                BuildObjectNodeGlobalMap(Object, TimeTicks, Object.CachedNodeGlobals);
                Object.CachedAnimTicks = TimeTicks;
                Object.CachedAnimClip = Object.ActiveAnimationClip;
            }
        }
    } // namespace

    [[nodiscard]] Vec3 TransformProceduralMeshPoint(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point)
    {
        Vec3 Result = ApplyAiTransformPoint(MeshValue.Geometry().BindTransform, Point);
        ForEachProceduralLimb(Object, MeshValue,
                              [&](const Mesh& Limb) { Result = ApplyLimbPoint(MakeLimbSteps(Limb), Result); });
        return Result;
    }

    [[nodiscard]] Vec3 TransformObjectSpacePoint(EngineKernel& K, const Object3D& Object, const Vec3& Local)
    {
        Vec3 P = ApplyObjectPoint(MakeObjectSteps(Object), Local, Object.GlueMode != 2);
        const Object3D* Current = &Object;
        int GlueDepth = 0;
        while (Current->GluedTargetObject > 0 && GlueDepth < 32)
        {
            ++GlueDepth;
            const Object3D* Target = K.Objects().GetObject(Current->GluedTargetObject);
            if (!Target)
            {
                break;
            }
            if (const Mesh* Limb = dbc::FindLimbMesh(*Target, Current->GluedTargetLimb))
            {
                if (Current->GlueMode == 1)
                {
                    P.X += Limb->LimbOffX;
                    P.Y += Limb->LimbOffY;
                    P.Z += Limb->LimbOffZ;
                }
                else
                {
                    P = ApplyLimbPoint(MakeLimbSteps(*Limb), P);
                }
            }
            P = ApplyObjectPoint(MakeObjectSteps(*Target), P, Target->GlueMode != 2);
            Current = Target;
        }
        return P;
    }

    [[nodiscard]] Vec3 TransformObjectSpaceVector(EngineKernel& K, const Object3D& Object, const Vec3& Local)
    {
        Vec3 P = ApplyObjectVector(MakeObjectSteps(Object), Local);
        const Object3D* Current = &Object;
        int GlueDepth = 0;
        while (Current->GluedTargetObject > 0 && GlueDepth < 32)
        {
            ++GlueDepth;
            const Object3D* Target = K.Objects().GetObject(Current->GluedTargetObject);
            if (!Target)
            {
                break;
            }
            if (const Mesh* Limb = dbc::FindLimbMesh(*Target, Current->GluedTargetLimb))
            {
                if (Current->GlueMode != 1)
                    P = ApplyLimbRotation(MakeLimbSteps(*Limb), P);
            }
            P = ApplyObjectVector(MakeObjectSteps(*Target), P);
            Current = Target;
        }
        return K.Renderer().NormalizeNormals() ? Vec3Normalize(P) : P;
    }

    [[nodiscard]] Vec3 WorldObjectOrigin(EngineKernel& K, const Object3D& Object)
    {
        return dbc::TransformObjectSpacePoint(K, Object, Vec3{0.0f, 0.0f, 0.0f});
    }

    [[nodiscard]] const aiMatrix4x4* CurrentImportedMeshTransform(const Object3D& Object, const Mesh& MeshValue)
    {
        if (!Object.AssimpScene || !Object.AssimpScene->mRootNode || MeshValue.SourceMeshIndex < 0)
            return nullptr;

        RefreshNodeGlobals(Object);
        const auto Node = Object.MeshNodeByIndex.find(static_cast<unsigned int>(MeshValue.SourceMeshIndex));
        if (Node == Object.MeshNodeByIndex.end())
            return nullptr;
        const auto Transform = Object.CachedNodeGlobals.find(Node->second);
        return Transform == Object.CachedNodeGlobals.end() ? nullptr : &Transform->second;
    }

    [[nodiscard]] Vec3 TransformMeshPointInObject(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point,
                                                  const aiMatrix4x4* ImportedTransform, bool ObjectSpace)
    {
        if (ObjectSpace)
            return Point;
        if (ImportedTransform)
            return ApplyAiTransformPoint(*ImportedTransform, Point);
        return TransformProceduralMeshPoint(Object, MeshValue, Point);
    }

    [[nodiscard]] Vec3 WorldLimbOrigin(EngineKernel& K, const Object3D& Object, int LimbId)
    {
        if (const Mesh* Limb = dbc::FindLimbMesh(Object, LimbId))
        {
            const aiMatrix4x4* MeshTransform = CurrentImportedMeshTransform(Object, *Limb);
            const Vec3 Local = TransformMeshPointInObject(Object, *Limb, Vec3{}, MeshTransform, false);
            return dbc::TransformObjectSpacePoint(K, Object, Local);
        }

        if (Object.AssimpScene && LimbId >= 0 && LimbId < static_cast<int>(Object.LimbNodes.size()))
        {
            const aiNode* LimbNode = Object.LimbNodes[static_cast<size_t>(LimbId)];
            if (LimbNode && Object.AssimpScene->mRootNode)
            {
                RefreshNodeGlobals(Object);
                const auto GlobalIt = Object.CachedNodeGlobals.find(LimbNode);
                if (GlobalIt != Object.CachedNodeGlobals.end())
                {
                    const aiMatrix4x4& NodeM = GlobalIt->second;
                    const Vec3 Local{NodeM.a4, NodeM.b4, NodeM.c4};
                    return dbc::TransformObjectSpacePoint(K, Object, Local);
                }
            }
        }

        return dbc::WorldObjectOrigin(K, Object);
    }

    [[nodiscard]] std::vector<aiMatrix4x4> BuildBonePalette(
        const Object3D& Object, const Mesh& MeshValue,
        const std::unordered_map<const aiNode*, aiMatrix4x4>& NodeGlobals)
    {
        if (!Object.AssimpScene || NodeGlobals.empty())
            return {};
        const MeshGeometry& Geometry = MeshValue.Geometry();
        std::vector<aiMatrix4x4> Palette(Geometry.BoneNodes.size());
        for (std::size_t BoneIndex = 0;
             BoneIndex < Geometry.BoneNodes.size() && BoneIndex < Geometry.BoneOffsets.size(); ++BoneIndex)
        {
            const auto Global = NodeGlobals.find(Geometry.BoneNodes[BoneIndex]);
            if (Global != NodeGlobals.end())
                Palette[BoneIndex] = Object.RootInverseTransform * Global->second * Geometry.BoneOffsets[BoneIndex];
        }
        return Palette;
    }

    [[nodiscard]] bool SkinMeshVertex(const Mesh& MeshValue, std::size_t VertexIndex,
                                      const std::vector<aiMatrix4x4>& BonePalette, Vec3& Point, Vec3& Normal)
    {
        const MeshGeometry& Geometry = MeshValue.Geometry();
        if (VertexIndex >= Geometry.Vertices.size() || VertexIndex >= Geometry.BoneInfluences.size())
            return false;
        const BoneInfluence& Influence = Geometry.BoneInfluences[VertexIndex];
        Vec3 SkinnedPoint{};
        Vec3 SkinnedNormal{};
        float Total = 0.0f;
        for (std::size_t Slot = 0; Slot < Influence.Weights.size(); ++Slot)
        {
            const int BoneIndex = Influence.Indices[Slot];
            const float Weight = Influence.Weights[Slot];
            if (BoneIndex < 0 || Weight <= 0.0f || static_cast<std::size_t>(BoneIndex) >= BonePalette.size())
                continue;
            const aiMatrix4x4& Palette = BonePalette[static_cast<std::size_t>(BoneIndex)];
            const Vec3 WeightedPoint = ApplyAiTransformPoint(Palette, Geometry.Vertices[VertexIndex]);
            SkinnedPoint.X += WeightedPoint.X * Weight;
            SkinnedPoint.Y += WeightedPoint.Y * Weight;
            SkinnedPoint.Z += WeightedPoint.Z * Weight;
            if (Geometry.Normals.size() == Geometry.Vertices.size())
            {
                const Vec3 WeightedNormal = ApplyAiTransformVector(Palette, Geometry.Normals[VertexIndex]);
                SkinnedNormal.X += WeightedNormal.X * Weight;
                SkinnedNormal.Y += WeightedNormal.Y * Weight;
                SkinnedNormal.Z += WeightedNormal.Z * Weight;
            }
            Total += Weight;
        }
        if (Total <= 0.0f)
            return false;
        Point = Vec3{SkinnedPoint.X / Total, SkinnedPoint.Y / Total, SkinnedPoint.Z / Total};
        Normal = Vec3Normalize(SkinnedNormal);
        return true;
    }

    const Mesh* FindLimbMesh(const Object3D& Object, int LimbId)
    {
        if (LimbId < 0)
        {
            return nullptr;
        }
        const size_t Li = static_cast<size_t>(LimbId);

        // If limb_nodes are available, use the node-based mapping.
        // A limb index maps to a scene-graph node, which references
        // Assimp mesh indices via node->mMeshes[].  We find the first
        // Mesh whose source_mesh_index matches.
        if (Li < Object.LimbNodes.size())
        {
            const aiNode* Node = Object.LimbNodes[Li];
            if (Node && Node->mNumMeshes > 0)
            {
                const unsigned int Target = Node->mMeshes[0];
                for (const Mesh& M : Object.Meshes)
                {
                    if (M.SourceMeshIndex == static_cast<int>(Target))
                    {
                        return &M;
                    }
                }
            }
            return nullptr; // node has no meshes (e.g. root)
        }

        // Fallback for procedural objects without scene graph.
        if (Li < Object.Meshes.size())
        {
            return &Object.Meshes[Li];
        }
        return nullptr;
    }

    Mesh* FindLimbMesh(Object3D& Object, int LimbId)
    {
        return const_cast<Mesh*>(FindLimbMesh(std::as_const(Object), LimbId));
    }
} // namespace dbc
