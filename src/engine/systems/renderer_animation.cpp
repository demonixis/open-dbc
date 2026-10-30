#include "engine_kernel.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    int FindAnimKeyIndex(double TimeTicks, unsigned int KeyCount, const auto* Keys)
    {
        if (KeyCount <= 1)
        {
            return 0;
        }
        for (unsigned int I = 0; I + 1 < KeyCount; ++I)
        {
            if (TimeTicks < Keys[I + 1].mTime)
            {
                return static_cast<int>(I);
            }
        }
        return static_cast<int>(KeyCount - 1);
    }

    aiMatrix4x4 ComposeSrtMatrix(const aiVector3D& Scaling, const aiQuaternion& Rotation, const aiVector3D& Translation)
    {
        aiMatrix4x4 M(Rotation.GetMatrix());
        M.a1 *= Scaling.x;
        M.b1 *= Scaling.x;
        M.c1 *= Scaling.x;
        M.a2 *= Scaling.y;
        M.b2 *= Scaling.y;
        M.c2 *= Scaling.y;
        M.a3 *= Scaling.z;
        M.b3 *= Scaling.z;
        M.c3 *= Scaling.z;
        M.a4 = Translation.x;
        M.b4 = Translation.y;
        M.c4 = Translation.z;
        return M;
    }

    aiMatrix4x4 EvaluateNodeLocalTransform(const Object3D& Object, const aiNode* Node, double TimeTicks,
                                           const std::unordered_map<std::string, const aiNodeAnim*>& Channels,
                                           double DurationTicks)
    {
        if (!Object.AssimpScene || !Node)
        {
            return aiMatrix4x4();
        }

        aiVector3D BaseScale(1.0f, 1.0f, 1.0f);
        aiVector3D BasePos(0.0f, 0.0f, 0.0f);
        aiQuaternion BaseRot;
        Node->mTransformation.Decompose(BaseScale, BaseRot, BasePos);

        aiMatrix4x4 Local = Node->mTransformation;

        const auto ChIt = Channels.find(Node->mName.C_Str());
        if (ChIt != Channels.end() && ChIt->second)
        {
            const aiNodeAnim* Channel = ChIt->second;
            aiVector3D PresentPos = BasePos;
            aiVector3D PresentScale = BaseScale;
            aiQuaternion PresentRot = BaseRot;

            if (Channel->mNumPositionKeys > 0)
            {
                const int KeyIndex = FindAnimKeyIndex(TimeTicks, Channel->mNumPositionKeys, Channel->mPositionKeys);
                const int NextIndex = (KeyIndex + 1) % static_cast<int>(Channel->mNumPositionKeys);
                const aiVectorKey& Key = Channel->mPositionKeys[static_cast<unsigned int>(KeyIndex)];
                const aiVectorKey& Next = Channel->mPositionKeys[static_cast<unsigned int>(NextIndex)];
                double Dt = Next.mTime - Key.mTime;
                if (Dt < 0.0 && DurationTicks > 0.0)
                {
                    Dt += DurationTicks;
                }
                if (Dt > 0.0)
                {
                    const float Factor = static_cast<float>((TimeTicks - Key.mTime) / Dt);
                    PresentPos = Key.mValue + (Next.mValue - Key.mValue) * Factor;
                }
                else
                {
                    PresentPos = Key.mValue;
                }
            }

            if (Channel->mNumRotationKeys > 0)
            {
                const int KeyIndex = FindAnimKeyIndex(TimeTicks, Channel->mNumRotationKeys, Channel->mRotationKeys);
                const int NextIndex = (KeyIndex + 1) % static_cast<int>(Channel->mNumRotationKeys);
                const aiQuatKey& Key = Channel->mRotationKeys[static_cast<unsigned int>(KeyIndex)];
                const aiQuatKey& Next = Channel->mRotationKeys[static_cast<unsigned int>(NextIndex)];
                double Dt = Next.mTime - Key.mTime;
                if (Dt < 0.0 && DurationTicks > 0.0)
                {
                    Dt += DurationTicks;
                }
                if (Dt > 0.0)
                {
                    const float Factor = static_cast<float>((TimeTicks - Key.mTime) / Dt);
                    aiQuaternion::Interpolate(PresentRot, Key.mValue, Next.mValue, Factor);
                }
                else
                {
                    PresentRot = Key.mValue;
                }
            }

            if (Channel->mNumScalingKeys > 0)
            {
                const int KeyIndex = FindAnimKeyIndex(TimeTicks, Channel->mNumScalingKeys, Channel->mScalingKeys);
                PresentScale = Channel->mScalingKeys[static_cast<unsigned int>(KeyIndex)].mValue;
            }

            Local = ComposeSrtMatrix(PresentScale, PresentRot, PresentPos);
        }

        const auto LimbIt = Object.LimbIndexByNode.find(Node);
        if (LimbIt != Object.LimbIndexByNode.end())
        {
            const int LimbIndex = LimbIt->second;
            if (LimbIndex >= 0 && LimbIndex < static_cast<int>(Object.LimbTransforms.size()))
            {
                const LimbTransform& Limb = Object.LimbTransforms[LimbIndex];
                const bool Custom = std::abs(Limb.RotX) > 0.0001f || std::abs(Limb.RotY) > 0.0001f ||
                                    std::abs(Limb.RotZ) > 0.0001f || std::abs(Limb.OffX) > 0.0001f ||
                                    std::abs(Limb.OffY) > 0.0001f || std::abs(Limb.OffZ) > 0.0001f ||
                                    std::abs(Limb.ScaleX - 1.0f) > 0.0001f || std::abs(Limb.ScaleY - 1.0f) > 0.0001f ||
                                    std::abs(Limb.ScaleZ - 1.0f) > 0.0001f;
                if (Custom)
                {
                    const Mat3 R = Mat3FromEulerXyz(Limb.RotX, Limb.RotY, Limb.RotZ);
                    aiMatrix4x4 LimbM;
                    LimbM.a1 = R.M[0][0] * Limb.ScaleX;
                    LimbM.a2 = R.M[0][1] * Limb.ScaleY;
                    LimbM.a3 = R.M[0][2] * Limb.ScaleZ;
                    LimbM.a4 = Limb.OffX;
                    LimbM.b1 = R.M[1][0] * Limb.ScaleX;
                    LimbM.b2 = R.M[1][1] * Limb.ScaleY;
                    LimbM.b3 = R.M[1][2] * Limb.ScaleZ;
                    LimbM.b4 = Limb.OffY;
                    LimbM.c1 = R.M[2][0] * Limb.ScaleX;
                    LimbM.c2 = R.M[2][1] * Limb.ScaleY;
                    LimbM.c3 = R.M[2][2] * Limb.ScaleZ;
                    LimbM.c4 = Limb.OffZ;
                    Local = Local * LimbM;
                }
            }
        }

        return Local;
    }

    aiMatrix4x4 EvaluateNodeLocalTransform(const Object3D& Object, const aiNode* Node, double TimeTicks)
    {
        return EvaluateNodeLocalTransform(Object, Node, TimeTicks, Object.AnimChannelByNode,
                                          Object.ActiveAnimDurationTicks);
    }

    void BuildMeshNodeMapRecursive(const aiNode* Node, std::unordered_map<unsigned int, const aiNode*>& OutMap)
    {
        if (!Node)
        {
            return;
        }
        for (unsigned int I = 0; I < Node->mNumMeshes; ++I)
        {
            OutMap[Node->mMeshes[I]] = Node;
        }
        for (unsigned int I = 0; I < Node->mNumChildren; ++I)
        {
            BuildMeshNodeMapRecursive(Node->mChildren[I], OutMap);
        }
    }

    void BuildLimbMapRecursive(const aiNode* Node, std::vector<const aiNode*>& OutLimbNodes,
                               std::unordered_map<const aiNode*, int>& OutLimbIndexByNode)
    {
        if (!Node)
        {
            return;
        }
        const int LimbIndex = static_cast<int>(OutLimbNodes.size());
        OutLimbNodes.push_back(Node);
        OutLimbIndexByNode[Node] = LimbIndex;
        for (unsigned int I = 0; I < Node->mNumChildren; ++I)
        {
            BuildLimbMapRecursive(Node->mChildren[I], OutLimbNodes, OutLimbIndexByNode);
        }
    }

    void BuildNodeGlobalMapRecursive(const Object3D& Object, const aiNode* Node, const aiMatrix4x4& ParentGlobal,
                                     double TimeTicks, std::unordered_map<const aiNode*, aiMatrix4x4>& OutGlobals)
    {
        if (!Node)
        {
            return;
        }
        const aiMatrix4x4 Local = EvaluateNodeLocalTransform(Object, Node, TimeTicks);
        const aiMatrix4x4 Global = ParentGlobal * Local;
        OutGlobals[Node] = Global;
        for (unsigned int I = 0; I < Node->mNumChildren; ++I)
        {
            BuildNodeGlobalMapRecursive(Object, Node->mChildren[I], Global, TimeTicks, OutGlobals);
        }
    }

    void BuildObjectNodeGlobalMap(const Object3D& Object, double TimeTicks,
                                  std::unordered_map<const aiNode*, aiMatrix4x4>& OutGlobals)
    {
        aiMatrix4x4 ParentGlobal;
        if (Object.CompatibilityRootNode)
        {
            const aiNode* CompatibilityRoot = Object.CompatibilityRootNode.get();
            ParentGlobal = EvaluateNodeLocalTransform(Object, CompatibilityRoot, TimeTicks);
            OutGlobals[CompatibilityRoot] = ParentGlobal;
        }
        if (Object.AssimpScene && Object.AssimpScene->mRootNode)
        {
            BuildNodeGlobalMapRecursive(Object, Object.AssimpScene->mRootNode, ParentGlobal, TimeTicks, OutGlobals);
        }
    }

    void PrepareFrameSimulation(EngineKernel& K, double DtSeconds)
    {
        bool Changed = false;
        K.Objects().ForEachObject(
            [&](int, Object3D& Object)
            {
                if (!Object.Visible)
                    return;
                Changed = Changed || Object.Playing || Object.Looping || Object.TransformDirty;
                const bool AuthoredAnimation = K.Objects().UpdateObjectKeyframeAnimation(Object, DtSeconds);
                if (Object.AssimpScene && Object.AssimpScene->mRootNode)
                {
                    double TimeTicks = 0.0;
                    if (AuthoredAnimation)
                    {
                        Object.AnimChannelByNode.clear();
                    }
                    else if (!Object.AnimationClips.empty())
                    {
                        Object.ActiveAnimationClip = std::clamp(Object.ActiveAnimationClip, 0,
                                                                static_cast<int>(Object.AnimationClips.size()) - 1);
                        const AnimationClip& Clip = Object.AnimationClips[Object.ActiveAnimationClip];
                        Object.AnimChannelByNode = Clip.Channels;
                        const double TicksPerSec = std::max(1e-6, Clip.TicksPerSecond);
                        const double Duration = std::max(0.0, Clip.DurationTicks);
                        Object.ActiveAnimDurationTicks = Duration;
                        Object.ActiveAnimTicksPerSec = TicksPerSec;

                        double RangeStart = 0.0;
                        double RangeEnd = Duration;
                        if (Object.HasPlayRange)
                        {
                            const int ClipStart = Clip.StartFrame;
                            const int ClipFrameCount = std::max(0, Clip.EndFrame - Clip.StartFrame);
                            const int LocalStart = std::clamp(Object.PlayStartFrame - ClipStart, 0, ClipFrameCount);
                            const int LocalEnd =
                                std::clamp(Object.PlayEndFrame - ClipStart, LocalStart, ClipFrameCount);
                            RangeStart = static_cast<double>(LocalStart) * TicksPerSec / 25.0;
                            RangeEnd = static_cast<double>(LocalEnd) * TicksPerSec / 25.0;
                        }
                        const double RangeTicks = std::max(0.0, RangeEnd - RangeStart);

                        if (Object.Playing)
                        {
                            const double SpeedScale =
                                (Object.Speed == 0.0f) ? 1.0 : (static_cast<double>(Object.Speed) / 20.0);
                            Object.AnimTimeSeconds += DtSeconds * SpeedScale;
                            if (RangeTicks > 0.0 && !Object.Looping)
                            {
                                const double StartTime = RangeStart / TicksPerSec;
                                const double EndTime = RangeEnd / TicksPerSec;
                                if (Object.AnimTimeSeconds >= EndTime)
                                {
                                    Object.AnimTimeSeconds = EndTime;
                                    Object.Playing = false;
                                }
                                if (Object.AnimTimeSeconds < StartTime)
                                {
                                    Object.AnimTimeSeconds = StartTime;
                                    Object.Playing = false;
                                }
                            }
                        }
                        if (RangeTicks > 0.0 && Object.Looping)
                        {
                            const double RangeSec = RangeTicks / TicksPerSec;
                            const double StartSec = RangeStart / TicksPerSec;
                            if (RangeSec > 0.0)
                            {
                                while (Object.AnimTimeSeconds >= StartSec + RangeSec)
                                {
                                    Object.AnimTimeSeconds -= RangeSec;
                                }
                                while (Object.AnimTimeSeconds < StartSec)
                                {
                                    Object.AnimTimeSeconds += RangeSec;
                                }
                            }
                        }
                        TimeTicks = Object.AnimTimeSeconds * TicksPerSec;
                        if (RangeTicks > 0.0)
                        {
                            if (Object.Looping)
                            {
                                TimeTicks = std::fmod(TimeTicks - RangeStart, RangeTicks) + RangeStart;
                                if (TimeTicks < RangeStart)
                                {
                                    TimeTicks += RangeTicks;
                                }
                            }
                            else
                            {
                                TimeTicks = std::clamp(TimeTicks, RangeStart, RangeEnd);
                            }
                        }
                        Object.Frame = Clip.StartFrame + static_cast<int>(std::max(0.0, Object.AnimTimeSeconds * 25.0));
                    }
                    else
                    {
                        const aiAnimation* Anim = nullptr;
                        if (Object.ImportedAnimationEnabled && Object.AnimIndex >= 0 &&
                            static_cast<unsigned int>(Object.AnimIndex) < Object.AssimpScene->mNumAnimations)
                        {
                            Anim = Object.AssimpScene->mAnimations[Object.AnimIndex];
                        }
                        if (Anim)
                        {
                            const double TicksPerSec = (Anim->mTicksPerSecond != 0.0) ? Anim->mTicksPerSecond : 25.0;
                            const double Duration = std::max(0.0, Anim->mDuration);
                            Object.ActiveAnimDurationTicks = Duration;
                            Object.ActiveAnimTicksPerSec = TicksPerSec;
                            if (Object.Playing)
                            {
                                const double SpeedScale =
                                    (Object.Speed == 0.0f) ? 1.0 : (static_cast<double>(Object.Speed) / 20.0);
                                Object.AnimTimeSeconds += DtSeconds * SpeedScale;
                                if (Duration > 0.0 && !Object.Looping)
                                {
                                    const double EndTime = Duration / TicksPerSec;
                                    if (Object.AnimTimeSeconds >= EndTime)
                                    {
                                        Object.AnimTimeSeconds = EndTime;
                                        Object.Playing = false;
                                    }
                                    if (Object.AnimTimeSeconds < 0.0)
                                    {
                                        Object.AnimTimeSeconds = 0.0;
                                        Object.Playing = false;
                                    }
                                }
                            }
                            if (Duration > 0.0 && Object.Looping)
                            {
                                const double DurationSec = Duration / TicksPerSec;
                                if (DurationSec > 0.0)
                                {
                                    while (Object.AnimTimeSeconds >= DurationSec)
                                    {
                                        Object.AnimTimeSeconds -= DurationSec;
                                    }
                                    while (Object.AnimTimeSeconds < 0.0)
                                    {
                                        Object.AnimTimeSeconds += DurationSec;
                                    }
                                }
                            }
                            TimeTicks = Object.AnimTimeSeconds * TicksPerSec;
                            if (Duration > 0.0)
                            {
                                if (Object.Looping)
                                {
                                    TimeTicks = std::fmod(TimeTicks, Duration);
                                    if (TimeTicks < 0.0)
                                    {
                                        TimeTicks += Duration;
                                    }
                                }
                                else
                                {
                                    TimeTicks = std::clamp(TimeTicks, 0.0, Duration);
                                }
                            }
                            Object.Frame = static_cast<int>(std::max(0.0, Object.AnimTimeSeconds * 25.0));
                        }
                        else
                        {
                            Object.Frame = 0;
                        }
                    }

                    // Only rebuild the node-global map when the animation frame or clip changes.
                    const bool CacheStale =
                        (TimeTicks != Object.CachedAnimTicks) || (Object.ActiveAnimationClip != Object.CachedAnimClip);
                    if (CacheStale)
                    {
                        Object.CachedNodeGlobals.clear();
                        BuildObjectNodeGlobalMap(Object, TimeTicks, Object.CachedNodeGlobals);
                        Object.CachedAnimTicks = TimeTicks;
                        Object.CachedAnimClip = Object.ActiveAnimationClip;
                    }
                }
            });
        K.Objects().UpdateParticles();
        if (Changed)
            K.Renderer().InvalidateTriCache();
    }
} // namespace dbc
