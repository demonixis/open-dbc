#include "renderer_system.hpp"
#include "bsp_world.hpp"
#include "camera_system.hpp"
#include "core_system.hpp"
#include "debug_system.hpp"
#include "draw_system.hpp"
#include "engine_kernel.hpp"
#include "gpu_command_buffer.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "light_manager.hpp"
#include "log.hpp"
#include "matrix_manager.hpp"
#include "object_manager.hpp"
#include "platform_io.hpp"
#include "sprite_system.hpp"
#include "terrain_mesh.hpp"
#include "text_system.hpp"
#include "video_system.hpp"
#include "vr_system.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string_view>

#if __has_include(<SDL3_image/SDL_image.h>)
#include <SDL3_image/SDL_image.h>
#elif __has_include(<SDL_image.h>)
#include <SDL_image.h>
#endif

namespace dbc
{
    namespace
    {
        [[nodiscard]] Uint32 DecodeUtf8Codepoint(const std::string& Text, size_t& Index)
        {
            if (Index >= Text.size())
            {
                return 0;
            }
            const unsigned char C0 = static_cast<unsigned char>(Text[Index++]);
            if ((C0 & 0x80u) == 0)
            {
                return static_cast<Uint32>(C0);
            }
            if ((C0 & 0xE0u) == 0xC0u && Index < Text.size())
            {
                const unsigned char C1 = static_cast<unsigned char>(Text[Index++]);
                return static_cast<Uint32>(((C0 & 0x1Fu) << 6) | (C1 & 0x3Fu));
            }
            if ((C0 & 0xF0u) == 0xE0u && Index + 1 < Text.size())
            {
                const unsigned char C1 = static_cast<unsigned char>(Text[Index++]);
                const unsigned char C2 = static_cast<unsigned char>(Text[Index++]);
                return static_cast<Uint32>(((C0 & 0x0Fu) << 12) | ((C1 & 0x3Fu) << 6) | (C2 & 0x3Fu));
            }
            if ((C0 & 0xF8u) == 0xF0u && Index + 2 < Text.size())
            {
                const unsigned char C1 = static_cast<unsigned char>(Text[Index++]);
                const unsigned char C2 = static_cast<unsigned char>(Text[Index++]);
                const unsigned char C3 = static_cast<unsigned char>(Text[Index++]);
                return static_cast<Uint32>(((C0 & 0x07u) << 18) | ((C1 & 0x3Fu) << 12) | ((C2 & 0x3Fu) << 6) |
                                           (C3 & 0x3Fu));
            }
            return '?';
        }
    } // anonymous namespace

    // Forward declarations for functions defined later in this file
    Mesh* FindLimbMesh(EngineKernel& K, Object3D& Object, int LimbId);
    const Mesh* FindLimbMesh(EngineKernel& K, const Object3D& Object, int LimbId);
    Vec3 WorldObjectOrigin(EngineKernel& K, const Object3D& Object);
    Vec3 WorldLimbOrigin(EngineKernel& K, const Object3D& Object, int LimbId);
    float FogFactor(EngineKernel& K, float Z);
    Vec3 WorldToViewPoint(EngineKernel& K, float Wx, float Wy, float Wz);
    Vec3 WorldToViewDirection(EngineKernel& K, float Dx, float Dy, float Dz);
    bool ProjectViewPoint(EngineKernel& K, float X, float Y, float Z, SDL_FPoint& OutPos, float& OutDepth);
    bool ProjectPoint(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth);
    bool ProjectPointLocked(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth);
    void AppendClippedViewTriangle(EngineKernel& K, const ViewVertex& V0, const ViewVertex& V1, const ViewVertex& V2,
                                   SDL_Texture* Texture, SDL_GPUTexture* GpuTexture, bool Transparent,
                                   std::vector<Triangle>& Tris, bool Additive = false, SurfaceStyle Style = {},
                                   int AlphaPhase = -1, bool VeryEarly = false, float DepthBias = 0.0f,
                                   int MatrixGhostMode = -1, int RenderPriority = 0, bool DepthWrite = true,
                                   bool DepthTest = true, std::array<SDL_GPUTexture*, 7> AuxiliaryGpuTextures = {},
                                   SDL_GPUTexture* CubeGpuTexture = nullptr);
    GpuFragmentUniforms BuildFragmentUniforms(EngineKernel& K);

    [[nodiscard]] Vec3 ActiveCameraPosition(EngineKernel& K)
    {
        const RenderView* View = K.Renderer().ActiveView();
        const Vec3 Position = View ? Vec3{View->CameraPosition.x, View->CameraPosition.y, View->CameraPosition.z}
                                   : Vec3{K.Camera().PositionX(), K.Camera().PositionY(), K.Camera().PositionZ()};
        return K.Renderer().ReflectionViewEnabled() ? K.Renderer().ReflectWorldPoint(Position) : Position;
    }

    [[nodiscard]] SurfaceStyle BuildSurfaceStyle(const Object3D& Object, const Mesh& MeshValue)
    {
        SurfaceStyle Style;
        Style.Wireframe = Object.Wireframe;
        Style.Fog = Object.UseFog;
        Style.Ambient = Object.UseAmbient;
        Style.Filter = Object.TextureFilter;
        for (const auto& [Stage, BlendMode] : MeshValue.TextureStageBlendModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureBlendModes.size()))
                Style.TextureBlendModes[static_cast<std::size_t>(Stage)] = BlendMode;
        }
        for (const auto& [Stage, AddressMode] : MeshValue.TextureStageAddressModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureAddressModes.size()))
                Style.TextureAddressModes[static_cast<std::size_t>(Stage)] =
                    AddressMode == 2 ? 1 : (AddressMode >= 3 ? 2 : 0);
        }
        for (const auto& [Stage, MipMode] : MeshValue.TextureStageMipModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureMipModes.size()))
                Style.TextureMipModes[static_cast<std::size_t>(Stage)] = MipMode;
        }
        for (const auto& [Stage, CubeMode] : MeshValue.TextureStageCubeModes)
        {
            if (Stage >= 0 && Stage < static_cast<int>(Style.TextureCubeModes.size()))
                Style.TextureCubeModes[static_cast<std::size_t>(Stage)] = CubeMode;
        }
        Style.BuiltInEffect = MeshValue.BuiltInEffect;
        Style.EffectId = MeshValue.EffectId >= 0 ? MeshValue.EffectId : Object.EffectId;
        Style.DiffuseColor = Object.DiffuseColor;
        Style.AmbientColor = Object.AmbientColor;
        Style.SpecularColor = Object.SpecularColor;
        Style.EmissiveColor = Object.EmissiveColor;
        Style.SpecularPower = Object.SpecularPower;
        Style.AlphaTestThreshold =
            (Object.TransparencyMode == 4 || Object.TransparencyMode == 6) ? (207.0f / 255.0f) : 0.0f;
        Style.AlphaOverride = Object.AlphaMapping ? Object.AlphaMappingAlpha : -1.0f;
        Style.ReflectionSurface = Object.ReflectionShading;
        Style.ShadowCaster = Object.ShadowShading && Object.ShadowMeshes.empty();
        Style.ShadowRange = Object.ShadowRange;
        return Style;
    }

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
                    { return Index < MeshValue.Vertices.size() ? &MeshValue.Vertices[Index] : nullptr; };
                    const std::size_t TriangleCount =
                        MeshValue.Indices.empty() ? MeshValue.Vertices.size() / 3 : MeshValue.Indices.size() / 3;
                    for (std::size_t TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
                    {
                        const uint32_t I0 = MeshValue.Indices.empty() ? static_cast<uint32_t>(TriangleIndex * 3)
                                                                      : MeshValue.Indices[TriangleIndex * 3];
                        const uint32_t I1 = MeshValue.Indices.empty() ? static_cast<uint32_t>(TriangleIndex * 3 + 1)
                                                                      : MeshValue.Indices[TriangleIndex * 3 + 1];
                        const uint32_t I2 = MeshValue.Indices.empty() ? static_cast<uint32_t>(TriangleIndex * 3 + 2)
                                                                      : MeshValue.Indices[TriangleIndex * 3 + 2];
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
                        dbc::AppendClippedViewTriangle(K, V0, V1, V2, nullptr, nullptr, false, Sources, false, Style);
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
                dbc::AppendClippedViewTriangle(K, MakeVertex(A), MakeVertex(B), MakeVertex(C), nullptr, nullptr, false,
                                               Volumes, false, VolumeStyle, -1, false, 0.0f, -1, 0, false, true);
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

    SDL_GPUTexture* ImageGpuTexture(EngineKernel& K, Image& ImageValue, bool TransparentBlack);

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

    Vec3 ApplyAiTransformPoint(const aiMatrix4x4& M, const Vec3& P)
    {
        return Vec3{
            M.a1 * P.X + M.a2 * P.Y + M.a3 * P.Z + M.a4,
            M.b1 * P.X + M.b2 * P.Y + M.b3 * P.Z + M.b4,
            M.c1 * P.X + M.c2 * P.Y + M.c3 * P.Z + M.c4,
        };
    }

    Vec3 ApplyAiTransformVector(const aiMatrix4x4& M, const Vec3& V)
    {
        return Vec3{
            M.a1 * V.X + M.a2 * V.Y + M.a3 * V.Z,
            M.b1 * V.X + M.b2 * V.Y + M.b3 * V.Z,
            M.c1 * V.X + M.c2 * V.Y + M.c3 * V.Z,
        };
    }

    [[nodiscard]] uint64_t CountSceneTriangles(const aiScene* Scene)
    {
        if (!Scene)
        {
            return 0;
        }
        uint64_t TriCount = 0;
        for (unsigned int Mi = 0; Mi < Scene->mNumMeshes; ++Mi)
        {
            const aiMesh* Mesh = Scene->mMeshes[Mi];
            if (!Mesh)
            {
                continue;
            }
            for (unsigned int Fi = 0; Fi < Mesh->mNumFaces; ++Fi)
            {
                if (Mesh->mFaces[Fi].mNumIndices == 3)
                {
                    ++TriCount;
                }
            }
        }
        return TriCount;
    }

    [[nodiscard]] bool IsXIdentifierCharacter(char Character)
    {
        const unsigned char Value = static_cast<unsigned char>(Character);
        return std::isalnum(Value) != 0 || Character == '_';
    }

    void SkipXWhitespace(const std::string& Source, std::size_t& Position)
    {
        while (Position < Source.size() && std::isspace(static_cast<unsigned char>(Source[Position])) != 0)
        {
            ++Position;
        }
    }

    [[nodiscard]] bool ConsumeXNumber(const std::string& Source, std::size_t& Position)
    {
        SkipXWhitespace(Source, Position);
        if (Position < Source.size() && (Source[Position] == '+' || Source[Position] == '-'))
        {
            ++Position;
        }

        bool HasDigit = false;
        while (Position < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Position])) != 0)
        {
            HasDigit = true;
            ++Position;
        }
        if (Position < Source.size() && Source[Position] == '.')
        {
            ++Position;
            while (Position < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Position])) != 0)
            {
                HasDigit = true;
                ++Position;
            }
        }
        if (!HasDigit)
        {
            return false;
        }

        if (Position < Source.size() && (Source[Position] == 'e' || Source[Position] == 'E'))
        {
            ++Position;
            if (Position < Source.size() && (Source[Position] == '+' || Source[Position] == '-'))
            {
                ++Position;
            }
            const std::size_t ExponentStart = Position;
            while (Position < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Position])) != 0)
            {
                ++Position;
            }
            if (Position == ExponentStart)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool RepairSingleTerminatedFrameMatrices(std::string& Source)
    {
        if (Source.size() < 16 || Source.compare(0, 4, "xof ") != 0 || Source.compare(8, 4, "txt ") != 0)
        {
            return false;
        }

        constexpr std::string_view MatrixToken = "FrameTransformMatrix";
        bool Repaired = false;
        std::size_t SearchPosition = 0;
        while ((SearchPosition = Source.find(MatrixToken, SearchPosition)) != std::string::npos)
        {
            const std::size_t TokenEnd = SearchPosition + MatrixToken.size();
            if ((SearchPosition > 0 && IsXIdentifierCharacter(Source[SearchPosition - 1])) ||
                (TokenEnd < Source.size() && IsXIdentifierCharacter(Source[TokenEnd])))
            {
                SearchPosition = TokenEnd;
                continue;
            }

            std::size_t Position = TokenEnd;
            SkipXWhitespace(Source, Position);
            if (Position >= Source.size() || Source[Position] != '{')
            {
                SearchPosition = TokenEnd;
                continue;
            }
            ++Position;

            bool IsNumericMatrix = true;
            std::size_t LastSeparator = std::string::npos;
            for (int Component = 0; Component < 16; ++Component)
            {
                if (!ConsumeXNumber(Source, Position))
                {
                    IsNumericMatrix = false;
                    break;
                }
                SkipXWhitespace(Source, Position);
                if (Position >= Source.size() || (Source[Position] != ',' && Source[Position] != ';'))
                {
                    IsNumericMatrix = false;
                    break;
                }
                LastSeparator = Position++;
            }
            if (!IsNumericMatrix || LastSeparator == std::string::npos || Source[LastSeparator] != ';')
            {
                SearchPosition = TokenEnd;
                continue;
            }

            SkipXWhitespace(Source, Position);
            if (Position < Source.size() && Source[Position] == ';')
            {
                SearchPosition = Position + 1;
                continue;
            }
            if (Position >= Source.size() || Source[Position] != '}')
            {
                SearchPosition = TokenEnd;
                continue;
            }

            Source.insert(LastSeparator + 1, 1, ';');
            Repaired = true;
            SearchPosition = Position + 2;
        }
        return Repaired;
    }

    [[nodiscard]] bool ReadAssimpSource(const std::string& Path, std::string& Source)
    {
        SdlAssimpIOSystem IoSystem;
        std::unique_ptr<Assimp::IOStream> Input(IoSystem.Open(Path.c_str(), "rb"));
        if (!Input)
        {
            return false;
        }
        const std::size_t FileSize = Input->FileSize();
        if (FileSize == 0)
        {
            return false;
        }
        Source.resize(FileSize);
        return Input->Read(Source.data(), 1, FileSize) == FileSize;
    }

    [[nodiscard]] AssimpImportResult ImportSceneWithFlags(const std::string& Path, unsigned int Flags)
    {
        AssimpImportResult Result{};
        Result.ImporterValue = std::make_unique<Assimp::Importer>();
        Result.ImporterValue->SetIOHandler(new SdlAssimpIOSystem());
        Result.Scene = Result.ImporterValue->ReadFile(Path, Flags);
        if (!Result.Scene && FileExtensionLower(Path) == ".x")
        {
            const char* Error = Result.ImporterValue->GetErrorString();
            std::string RepairedSource;
            if (Error && std::string_view(Error).find("Semicolon expected.") != std::string_view::npos &&
                ReadAssimpSource(Path, RepairedSource) && RepairSingleTerminatedFrameMatrices(RepairedSource))
            {
                std::unique_ptr<Assimp::Importer> Retry = std::make_unique<Assimp::Importer>();
                Retry->SetIOHandler(new SdlAssimpIOSystem());
                const aiScene* RetriedScene =
                    Retry->ReadFileFromMemory(RepairedSource.data(), RepairedSource.size(), Flags, "x");
                if (RetriedScene)
                {
                    Result.ImporterValue = std::move(Retry);
                    Result.Scene = RetriedScene;
                }
            }
        }
        if (Result.Scene && Result.Scene->HasMeshes())
        {
            Result.TriangleCount = CountSceneTriangles(Result.Scene);
        }
        return Result;
    }

    void AddTriangle(Mesh& MeshValue, const Vec3& A, const Vec3& B, const Vec3& C,
                     SDL_FPoint Ta = SDL_FPoint{0.0f, 0.0f}, SDL_FPoint Tb = SDL_FPoint{1.0f, 0.0f},
                     SDL_FPoint Tc = SDL_FPoint{1.0f, 1.0f})
    {
        const uint32_t Base = static_cast<uint32_t>(MeshValue.Vertices.size());
        MeshValue.Vertices.push_back(A);
        MeshValue.Vertices.push_back(B);
        MeshValue.Vertices.push_back(C);
        MeshValue.Uvs.push_back(Ta);
        MeshValue.Uvs.push_back(Tb);
        MeshValue.Uvs.push_back(Tc);
        MeshValue.Indices.push_back(Base + 0);
        MeshValue.Indices.push_back(Base + 1);
        MeshValue.Indices.push_back(Base + 2);
    }

    void RegenerateMeshNormals(Mesh& MeshValue)
    {
        if (MeshValue.Vertices.empty())
        {
            MeshValue.Normals.clear();
            return;
        }
        MeshValue.Normals.assign(MeshValue.Vertices.size(), Vec3{0.0f, 0.0f, 0.0f});
        for (size_t I = 0; I + 2 < MeshValue.Indices.size(); I += 3)
        {
            const uint32_t I0 = MeshValue.Indices[I];
            const uint32_t I1 = MeshValue.Indices[I + 1];
            const uint32_t I2 = MeshValue.Indices[I + 2];
            if (I0 >= MeshValue.Vertices.size() || I1 >= MeshValue.Vertices.size() || I2 >= MeshValue.Vertices.size())
            {
                continue;
            }
            const Vec3 E1 = Vec3Sub(MeshValue.Vertices[I1], MeshValue.Vertices[I0]);
            const Vec3 E2 = Vec3Sub(MeshValue.Vertices[I2], MeshValue.Vertices[I0]);
            const Vec3 Fn = Vec3Normalize(Vec3Cross(E1, E2));
            MeshValue.Normals[I0].X += Fn.X;
            MeshValue.Normals[I0].Y += Fn.Y;
            MeshValue.Normals[I0].Z += Fn.Z;
            MeshValue.Normals[I1].X += Fn.X;
            MeshValue.Normals[I1].Y += Fn.Y;
            MeshValue.Normals[I1].Z += Fn.Z;
            MeshValue.Normals[I2].X += Fn.X;
            MeshValue.Normals[I2].Y += Fn.Y;
            MeshValue.Normals[I2].Z += Fn.Z;
        }
        for (Vec3& N : MeshValue.Normals)
        {
            N = Vec3Normalize(N);
        }
    }

    Mesh MakePlainMesh(float Width, float Height)
    {
        Mesh MeshValue;
        const float Hw = Width * 0.5f;
        const float Hh = Height * 0.5f;
        const Vec3 A{-Hw, Hh, 0.0f};
        const Vec3 B{Hw, Hh, 0.0f};
        const Vec3 C{Hw, -Hh, 0.0f};
        const Vec3 D{-Hw, -Hh, 0.0f};
        AddTriangle(MeshValue, A, C, B, SDL_FPoint{0.0f, 0.0f}, SDL_FPoint{1.0f, 1.0f}, SDL_FPoint{1.0f, 0.0f});
        AddTriangle(MeshValue, A, D, C, SDL_FPoint{0.0f, 0.0f}, SDL_FPoint{0.0f, 1.0f}, SDL_FPoint{1.0f, 1.0f});
        return MeshValue;
    }

    Mesh MakeBoxMesh(float Width, float Height, float Depth)
    {
        Mesh MeshValue;
        const float Hx = Width * 0.5f;
        const float Hy = Height * 0.5f;
        const float Hz = Depth * 0.5f;

        const Vec3 P000{-Hx, -Hy, -Hz};
        const Vec3 P001{-Hx, -Hy, Hz};
        const Vec3 P010{-Hx, Hy, -Hz};
        const Vec3 P011{-Hx, Hy, Hz};
        const Vec3 P100{Hx, -Hy, -Hz};
        const Vec3 P101{Hx, -Hy, Hz};
        const Vec3 P110{Hx, Hy, -Hz};
        const Vec3 P111{Hx, Hy, Hz};

        AddTriangle(MeshValue, P001, P101, P111);
        AddTriangle(MeshValue, P001, P111, P011);

        AddTriangle(MeshValue, P100, P000, P010);
        AddTriangle(MeshValue, P100, P010, P110);

        AddTriangle(MeshValue, P000, P001, P011);
        AddTriangle(MeshValue, P000, P011, P010);

        AddTriangle(MeshValue, P101, P100, P110);
        AddTriangle(MeshValue, P101, P110, P111);

        AddTriangle(MeshValue, P010, P011, P111);
        AddTriangle(MeshValue, P010, P111, P110);

        AddTriangle(MeshValue, P000, P100, P101);
        AddTriangle(MeshValue, P000, P101, P001);
        return MeshValue;
    }

    Mesh MakeSphereMesh(float Radius, int Rings, int Segments)
    {
        Mesh MeshValue;
        const int R = std::max(3, Rings);
        const int S = std::max(3, Segments);
        MeshValue.Vertices.reserve(static_cast<size_t>(R + 1) * static_cast<size_t>(S + 1));
        MeshValue.Normals.reserve(static_cast<size_t>(R + 1) * static_cast<size_t>(S + 1));
        MeshValue.Uvs.reserve(static_cast<size_t>(R + 1) * static_cast<size_t>(S + 1));

        for (int I = 0; I <= R; ++I)
        {
            const float V = static_cast<float>(I) / static_cast<float>(R);
            const float Phi = static_cast<float>(M_PI) * (V - 0.5f);
            const float Cp = std::cos(Phi);
            const float Sp = std::sin(Phi);
            for (int J = 0; J <= S; ++J)
            {
                const float U = static_cast<float>(J) / static_cast<float>(S);
                const float Th = static_cast<float>(2.0 * M_PI) * U;
                const float Ct = std::cos(Th);
                const float St = std::sin(Th);
                MeshValue.Vertices.push_back(Vec3{Radius * St * Cp, Radius * Sp, Radius * Ct * Cp});
                MeshValue.Normals.push_back(Vec3Normalize(Vec3{St * Cp, Sp, Ct * Cp}));
                MeshValue.Uvs.push_back(SDL_FPoint{U, 1.0f - V});
            }
        }

        const int Stride = S + 1;
        for (int I = 0; I < R; ++I)
        {
            for (int J = 0; J < S; ++J)
            {
                const uint32_t A = static_cast<uint32_t>(I * Stride + J);
                const uint32_t B = static_cast<uint32_t>(A + 1);
                const uint32_t C = static_cast<uint32_t>((I + 1) * Stride + J);
                const uint32_t D = static_cast<uint32_t>(C + 1);
                MeshValue.Indices.push_back(A);
                MeshValue.Indices.push_back(D);
                MeshValue.Indices.push_back(C);
                MeshValue.Indices.push_back(A);
                MeshValue.Indices.push_back(B);
                MeshValue.Indices.push_back(D);
            }
        }
        return MeshValue;
    }

    Mesh MakeCylinderMesh(float Radius, float Height, int Segments, bool TopCap, bool BottomCap)
    {
        Mesh MeshValue;
        const int S = std::max(3, Segments);
        const float Hy = Height * 0.5f;

        for (int I = 0; I < S; ++I)
        {
            const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
            const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
            const Vec3 B0{std::sin(A0) * Radius, -Hy, std::cos(A0) * Radius};
            const Vec3 B1{std::sin(A1) * Radius, -Hy, std::cos(A1) * Radius};
            const Vec3 T0{B0.X, Hy, B0.Z};
            const Vec3 T1{B1.X, Hy, B1.Z};
            AddTriangle(MeshValue, B0, T1, T0);
            AddTriangle(MeshValue, B0, B1, T1);
        }

        if (TopCap)
        {
            const Vec3 C{0.0f, Hy, 0.0f};
            for (int I = 0; I < S; ++I)
            {
                const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
                const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
                const Vec3 P0{std::sin(A0) * Radius, Hy, std::cos(A0) * Radius};
                const Vec3 P1{std::sin(A1) * Radius, Hy, std::cos(A1) * Radius};
                AddTriangle(MeshValue, C, P0, P1);
            }
        }

        if (BottomCap)
        {
            const Vec3 C{0.0f, -Hy, 0.0f};
            for (int I = 0; I < S; ++I)
            {
                const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
                const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
                const Vec3 P0{std::sin(A0) * Radius, -Hy, std::cos(A0) * Radius};
                const Vec3 P1{std::sin(A1) * Radius, -Hy, std::cos(A1) * Radius};
                AddTriangle(MeshValue, C, P1, P0);
            }
        }

        return MeshValue;
    }

    Mesh MakeConeMesh(float Radius, float Height, int Segments)
    {
        Mesh MeshValue;
        const int S = std::max(3, Segments);
        const float Hy = Height * 0.5f;
        const Vec3 Tip{0.0f, Hy, 0.0f};

        for (int I = 0; I < S; ++I)
        {
            const float A0 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I) / static_cast<float>(S);
            const float A1 = static_cast<float>(2.0 * M_PI) * static_cast<float>(I + 1) / static_cast<float>(S);
            const Vec3 P0{std::sin(A0) * Radius, -Hy, std::cos(A0) * Radius};
            const Vec3 P1{std::sin(A1) * Radius, -Hy, std::cos(A1) * Radius};
            AddTriangle(MeshValue, P0, P1, Tip);
        }
        return MeshValue;
    }

    [[nodiscard]] int ProceduralMeshLimbIndex(const Object3D& Object, const Mesh& MeshValue)
    {
        for (std::size_t Index = 0; Index < Object.Meshes.size(); ++Index)
        {
            if (&Object.Meshes[Index] == &MeshValue)
                return static_cast<int>(Index);
        }
        return -1;
    }

    [[nodiscard]] Vec3 ApplyProceduralLimbPoint(const Mesh& Limb, Vec3 Point)
    {
        Point.X *= Limb.LimbScaleX;
        Point.Y *= Limb.LimbScaleY;
        Point.Z *= Limb.LimbScaleZ;
        Point = RotateX(Point, Limb.LimbRotX);
        Point = RotateY(Point, Limb.LimbRotY);
        Point = RotateZ(Point, Limb.LimbRotZ);
        Point.X += Limb.LimbOffX;
        Point.Y += Limb.LimbOffY;
        Point.Z += Limb.LimbOffZ;
        return Point;
    }

    [[nodiscard]] Vec3 ApplyProceduralLimbNormal(const Mesh& Limb, Vec3 Normal)
    {
        Normal.X /= std::max(0.0001f, std::abs(Limb.LimbScaleX));
        Normal.Y /= std::max(0.0001f, std::abs(Limb.LimbScaleY));
        Normal.Z /= std::max(0.0001f, std::abs(Limb.LimbScaleZ));
        Normal = RotateX(Normal, Limb.LimbRotX);
        Normal = RotateY(Normal, Limb.LimbRotY);
        Normal = RotateZ(Normal, Limb.LimbRotZ);
        return Normal;
    }

    [[nodiscard]] Vec3 TransformProceduralMeshPoint(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point)
    {
        Vec3 Result = ApplyAiTransformPoint(MeshValue.BindTransform, Point);
        int LimbIndex = ProceduralMeshLimbIndex(Object, MeshValue);
        if (LimbIndex < 0)
            return ApplyProceduralLimbPoint(MeshValue, Result);

        for (std::size_t Depth = 0; LimbIndex >= 0 && Depth < Object.Meshes.size(); ++Depth)
        {
            const std::size_t Index = static_cast<std::size_t>(LimbIndex);
            if (Index >= Object.Meshes.size())
                break;
            Result = ApplyProceduralLimbPoint(Object.Meshes[Index], Result);
            LimbIndex = Index < Object.LimbTransforms.size() ? Object.LimbTransforms[Index].ParentLimb : -1;
        }
        return Result;
    }

    [[nodiscard]] Vec3 TransformProceduralMeshNormal(const Object3D& Object, const Mesh& MeshValue, const Vec3& Normal)
    {
        Vec3 Result = ApplyAiTransformVector(MeshValue.BindTransform, Normal);
        int LimbIndex = ProceduralMeshLimbIndex(Object, MeshValue);
        if (LimbIndex < 0)
            return ApplyProceduralLimbNormal(MeshValue, Result);

        for (std::size_t Depth = 0; LimbIndex >= 0 && Depth < Object.Meshes.size(); ++Depth)
        {
            const std::size_t Index = static_cast<std::size_t>(LimbIndex);
            if (Index >= Object.Meshes.size())
                break;
            Result = ApplyProceduralLimbNormal(Object.Meshes[Index], Result);
            LimbIndex = Index < Object.LimbTransforms.size() ? Object.LimbTransforms[Index].ParentLimb : -1;
        }
        return Result;
    }

    [[nodiscard]] Vec3 TransformObjectSpacePoint(EngineKernel& K, const Object3D& Object, const Vec3& Local)
    {
        auto ApplyLocalObjectTransform = [](const Object3D& Obj, const Vec3& In, bool IncludePosition)
        {
            Vec3 P{
                In.X * Obj.ScaleX,
                In.Y * Obj.ScaleY,
                In.Z * Obj.ScaleZ,
            };
            if (Obj.ApplyPivot)
            {
                P = Mat3Apply(P, Obj.PivotMatrix);
            }
            if (Obj.UseZyxRotation)
            {
                P = RotateZ(P, Obj.RotZ);
                P = RotateY(P, Obj.RotY);
                P = RotateX(P, Obj.RotX);
            }
            else
            {
                P = RotateX(P, Obj.RotX);
                P = RotateY(P, Obj.RotY);
                P = RotateZ(P, Obj.RotZ);
            }
            if (IncludePosition)
            {
                P.X += Obj.PosX;
                P.Y += Obj.PosY;
                P.Z += Obj.PosZ;
            }
            return P;
        };

        auto ApplyLimbTransform = [](const Mesh& Limb, const Vec3& In)
        {
            Vec3 P = In;
            P.X *= Limb.LimbScaleX;
            P.Y *= Limb.LimbScaleY;
            P.Z *= Limb.LimbScaleZ;
            P = RotateX(P, Limb.LimbRotX);
            P = RotateY(P, Limb.LimbRotY);
            P = RotateZ(P, Limb.LimbRotZ);
            P.X += Limb.LimbOffX;
            P.Y += Limb.LimbOffY;
            P.Z += Limb.LimbOffZ;
            return P;
        };

        Vec3 P = ApplyLocalObjectTransform(Object, Local, Object.GlueMode != 2);
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
            if (const Mesh* Limb = dbc::FindLimbMesh(K, *Target, Current->GluedTargetLimb))
            {
                if (Current->GlueMode == 1)
                {
                    P.X += Limb->LimbOffX;
                    P.Y += Limb->LimbOffY;
                    P.Z += Limb->LimbOffZ;
                }
                else
                {
                    P = ApplyLimbTransform(*Limb, P);
                }
            }
            P = ApplyLocalObjectTransform(*Target, P, Target->GlueMode != 2);
            Current = Target;
        }
        return P;
    }

    [[nodiscard]] Vec3 TransformObjectSpaceVector(EngineKernel& K, const Object3D& Object, const Vec3& Local)
    {
        auto ApplyLocalObjectRotation = [](const Object3D& Obj, const Vec3& In)
        {
            Vec3 P = In;
            P.X /= std::max(0.0001f, std::abs(Obj.ScaleX));
            P.Y /= std::max(0.0001f, std::abs(Obj.ScaleY));
            P.Z /= std::max(0.0001f, std::abs(Obj.ScaleZ));
            if (Obj.ApplyPivot)
            {
                P = Mat3Apply(P, Obj.PivotMatrix);
            }
            if (Obj.UseZyxRotation)
            {
                P = RotateZ(P, Obj.RotZ);
                P = RotateY(P, Obj.RotY);
                P = RotateX(P, Obj.RotX);
            }
            else
            {
                P = RotateX(P, Obj.RotX);
                P = RotateY(P, Obj.RotY);
                P = RotateZ(P, Obj.RotZ);
            }
            return P;
        };

        auto ApplyLimbRotation = [](const Mesh& Limb, const Vec3& In)
        {
            Vec3 P = In;
            P.X *= Limb.LimbScaleX;
            P.Y *= Limb.LimbScaleY;
            P.Z *= Limb.LimbScaleZ;
            P = RotateX(P, Limb.LimbRotX);
            P = RotateY(P, Limb.LimbRotY);
            P = RotateZ(P, Limb.LimbRotZ);
            return P;
        };

        Vec3 P = ApplyLocalObjectRotation(Object, Local);
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
            if (const Mesh* Limb = dbc::FindLimbMesh(K, *Target, Current->GluedTargetLimb))
            {
                if (Current->GlueMode != 1)
                    P = ApplyLimbRotation(*Limb, P);
            }
            P = ApplyLocalObjectRotation(*Target, P);
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

    [[nodiscard]] Vec3 TransformMeshNormalInObject(const Object3D& Object, const Mesh& MeshValue, const Vec3& Normal,
                                                   const aiMatrix4x4* ImportedTransform, bool ObjectSpace)
    {
        if (ObjectSpace)
            return Normal;
        if (ImportedTransform)
            return ApplyAiTransformVector(*ImportedTransform, Normal);
        return TransformProceduralMeshNormal(Object, MeshValue, Normal);
    }

    [[nodiscard]] Vec3 WorldLimbOrigin(EngineKernel& K, const Object3D& Object, int LimbId)
    {
        if (const Mesh* Limb = dbc::FindLimbMesh(K, Object, LimbId))
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
                double TimeTicks = Object.AnimTimeSeconds * Object.ActiveAnimTicksPerSec;
                if (Object.ActiveAnimDurationTicks > 0.0)
                {
                    if (Object.Looping)
                    {
                        TimeTicks = std::fmod(TimeTicks, Object.ActiveAnimDurationTicks);
                        if (TimeTicks < 0.0)
                        {
                            TimeTicks += Object.ActiveAnimDurationTicks;
                        }
                    }
                    else
                    {
                        TimeTicks = std::clamp(TimeTicks, 0.0, Object.ActiveAnimDurationTicks);
                    }
                }
                if ((TimeTicks != Object.CachedAnimTicks) || (Object.ActiveAnimationClip != Object.CachedAnimClip))
                {
                    Object.CachedNodeGlobals.clear();
                    BuildObjectNodeGlobalMap(Object, TimeTicks, Object.CachedNodeGlobals);
                    Object.CachedAnimTicks = TimeTicks;
                    Object.CachedAnimClip = Object.ActiveAnimationClip;
                }
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
        std::vector<aiMatrix4x4> Palette(MeshValue.BoneNodes.size());
        for (std::size_t BoneIndex = 0;
             BoneIndex < MeshValue.BoneNodes.size() && BoneIndex < MeshValue.BoneOffsets.size(); ++BoneIndex)
        {
            const auto Global = NodeGlobals.find(MeshValue.BoneNodes[BoneIndex]);
            if (Global != NodeGlobals.end())
                Palette[BoneIndex] = Object.RootInverseTransform * Global->second * MeshValue.BoneOffsets[BoneIndex];
        }
        return Palette;
    }

    [[nodiscard]] bool SkinMeshVertex(const Mesh& MeshValue, std::size_t VertexIndex,
                                      const std::vector<aiMatrix4x4>& BonePalette, Vec3& Point, Vec3& Normal)
    {
        if (VertexIndex >= MeshValue.Vertices.size() || VertexIndex >= MeshValue.BoneInfluences.size())
            return false;
        const BoneInfluence& Influence = MeshValue.BoneInfluences[VertexIndex];
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
            const Vec3 WeightedPoint = ApplyAiTransformPoint(Palette, MeshValue.Vertices[VertexIndex]);
            SkinnedPoint.X += WeightedPoint.X * Weight;
            SkinnedPoint.Y += WeightedPoint.Y * Weight;
            SkinnedPoint.Z += WeightedPoint.Z * Weight;
            if (MeshValue.Normals.size() == MeshValue.Vertices.size())
            {
                const Vec3 WeightedNormal = ApplyAiTransformVector(Palette, MeshValue.Normals[VertexIndex]);
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

    [[nodiscard]] Vec3 TransformObjectPoint(EngineKernel& K, const Object3D& Object, const Mesh& MeshValue,
                                            const Vec3& Local, const aiMatrix4x4* MeshTransform,
                                            bool ObjectSpace = false)
    {
        const Vec3 P = TransformMeshPointInObject(Object, MeshValue, Local, MeshTransform, ObjectSpace);
        return dbc::TransformObjectSpacePoint(K, Object, P);
    }

    [[nodiscard]] Vec3 TransformObjectNormal(EngineKernel& K, const Object3D& Object, const Mesh& MeshValue,
                                             const Vec3& Local, const aiMatrix4x4* MeshTransform,
                                             bool ObjectSpace = false)
    {
        const Vec3 N = TransformMeshNormalInObject(Object, MeshValue, Local, MeshTransform, ObjectSpace);
        return dbc::TransformObjectSpaceVector(K, Object, N);
    }

    Mesh* FindLimbMesh([[maybe_unused]] EngineKernel& K, Object3D& Object, int LimbId)
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
                for (Mesh& M : Object.Meshes)
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

    const Mesh* FindLimbMesh([[maybe_unused]] EngineKernel& K, const Object3D& Object, int LimbId)
    {
        if (LimbId < 0)
        {
            return nullptr;
        }
        const size_t Li = static_cast<size_t>(LimbId);

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
            return nullptr;
        }

        if (Li < Object.Meshes.size())
        {
            return &Object.Meshes[Li];
        }
        return nullptr;
    }

    void RenderMatrix(EngineKernel& K, const Matrix& MatrixValue, std::vector<Triangle>& Tris)
    {
        if (MatrixValue.SegX <= 0 || MatrixValue.SegZ <= 0)
            return;
        Image* Img = MatrixValue.ImageId != 0 ? K.ImageStoreRef().GetImage(MatrixValue.ImageId) : nullptr;
        SDL_Texture* Texture = Img ? Img->Texture : nullptr;
        SDL_GPUTexture* GpuTexture = Img ? dbc::ImageGpuTexture(K, *Img, false) : nullptr;

        std::optional<RenderView> CameraView;
        const RenderView* View = K.Renderer().ActiveView();
        if (!View)
        {
            CameraView.emplace(K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight()));
            View = &*CameraView;
        }
        const Vec3 Camera = ActiveCameraPosition(K);
        const bool CullBackFaces = MatrixValue.Cull && !K.Renderer().ReflectionViewEnabled();

        const int SegX = MatrixValue.SegX;
        const int SegZ = MatrixValue.SegZ;
        const float StepX = MatrixValue.Width / static_cast<float>(SegX);
        const float StepZ = MatrixValue.Depth / static_cast<float>(SegZ);
        const float SignZ = MatrixValue.IsTerrain ? -1.0f : 1.0f;
        const float HeightScale = MatrixValue.IsTerrain ? MatrixValue.HeightScale : 1.0f;
        const float* Heights = MatrixValue.Heights.data();
        const auto Height = [&](int X, int Z)
        { return Heights[static_cast<size_t>(Z) * static_cast<size_t>(SegX + 1) + static_cast<size_t>(X)]; };

        const int TileCount = std::max(1, MatrixValue.TilesX * MatrixValue.TilesY);
        const float TileW = 1.0f / static_cast<float>(MatrixValue.TilesX);
        const float TileH = 1.0f / static_cast<float>(MatrixValue.TilesY);

        SurfaceStyle Style{};
        Style.Wireframe = MatrixValue.Wireframe;
        Style.Fog = MatrixValue.Fog;
        Style.BlackFog = MatrixValue.Fog && MatrixValue.Ghost;
        Style.Ambient = MatrixValue.Ambient;
        Style.Filter = MatrixValue.Filter;
        Style.TextureAddressModes[0] = MatrixValue.TextureMode;
        Style.TextureMipModes[0] = MatrixValue.MipMode;
        const int AlphaPhase = MatrixValue.Ghost ? 2 : (MatrixValue.Transparency ? 1 : 0);
        const int GhostMode = MatrixValue.Ghost ? MatrixValue.GhostMode : -1;
        const int Priority = MatrixValue.RenderAfterObjects ? 1 : -1;
        const auto FrontFacing = [&](const Vec3& A, const Vec3& B, const Vec3& C)
        {
            if (!CullBackFaces)
                return true;
            const Vec3 Normal = Vec3Cross(Vec3Sub(C, A), Vec3Sub(B, A));
            const Vec3 Center{(A.X + B.X + C.X) / 3.0f, (A.Y + B.Y + C.Y) / 3.0f, (A.Z + B.Z + C.Z) / 3.0f};
            const Vec3 ToCamera{Camera.X - Center.X, Camera.Y - Center.Y, Camera.Z - Center.Z};
            return Vec3Dot(Normal, ToCamera) > 0.0f;
        };

        // First pass: the world bounds of every block decide whether it is visible and how coarse it is.
        const int BlocksX = (SegX + MatrixBlockCells - 1) / MatrixBlockCells;
        const int BlocksZ = (SegZ + MatrixBlockCells - 1) / MatrixBlockCells;
        const bool Lod = MatrixValue.IsTerrain && SegX % MatrixBlockCells == 0 && SegZ % MatrixBlockCells == 0;

        struct Block
        {
            bool Visible = false;
            int Step = 1;
        };

        std::vector<Block> Blocks(static_cast<size_t>(BlocksX) * static_cast<size_t>(BlocksZ));
        for (int Bz = 0; Bz < BlocksZ; ++Bz)
        {
            for (int Bx = 0; Bx < BlocksX; ++Bx)
            {
                const int X0 = Bx * MatrixBlockCells, X1 = std::min(X0 + MatrixBlockCells, SegX);
                const int Z0 = Bz * MatrixBlockCells, Z1 = std::min(Z0 + MatrixBlockCells, SegZ);
                float Low = Height(X0, Z0), High = Low;
                for (int Z = Z0; Z <= Z1; ++Z)
                {
                    for (int X = X0; X <= X1; ++X)
                    {
                        const float H = Height(X, Z);
                        Low = std::min(Low, H);
                        High = std::max(High, H);
                    }
                }
                const float Ya = MatrixValue.PosY + Low * HeightScale, Yb = MatrixValue.PosY + High * HeightScale;
                const Vec3 Minimum{MatrixValue.PosX + static_cast<float>(X0) * StepX, std::min(Ya, Yb),
                                   MatrixValue.PosZ + SignZ * static_cast<float>(SignZ > 0.0f ? Z0 : Z1) * StepZ};
                const Vec3 Maximum{MatrixValue.PosX + static_cast<float>(X1) * StepX, std::max(Ya, Yb),
                                   MatrixValue.PosZ + SignZ * static_cast<float>(SignZ > 0.0f ? Z1 : Z0) * StepZ};
                const Vec3 Center{(Minimum.X + Maximum.X) * 0.5f, (Minimum.Y + Maximum.Y) * 0.5f,
                                  (Minimum.Z + Maximum.Z) * 0.5f};
                const Vec3 Half{Maximum.X - Center.X, Maximum.Y - Center.Y, Maximum.Z - Center.Z};
                const Vec3 ViewCenter = dbc::WorldToViewPoint(K, Center.X, Center.Y, Center.Z);
                Block& Current = Blocks[static_cast<size_t>(Bz) * static_cast<size_t>(BlocksX) + Bx];
                Current.Visible = View->ContainsSphere(glm::vec3(ViewCenter.X, ViewCenter.Y, ViewCenter.Z),
                                                       std::sqrt(Vec3Dot(Half, Half)));
                if (Lod)
                {
                    const Vec3 Nearest{std::clamp(Camera.X, Minimum.X, Maximum.X),
                                       std::clamp(Camera.Y, Minimum.Y, Maximum.Y),
                                       std::clamp(Camera.Z, Minimum.Z, Maximum.Z)};
                    const Vec3 Offset = Vec3Sub(Nearest, Camera);
                    Current.Step = TerrainBlockStep(std::sqrt(Vec3Dot(Offset, Offset)),
                                                    static_cast<float>(MatrixBlockCells) * std::max(StepX, StepZ));
                }
            }
        }
        const auto StepOf = [&](int Bx, int Bz)
        {
            if (Bx < 0 || Bz < 0 || Bx >= BlocksX || Bz >= BlocksZ)
                return 1;
            return Blocks[static_cast<size_t>(Bz) * static_cast<size_t>(BlocksX) + Bx].Step;
        };

        // Second pass: transform each grid vertex of a visible block once, then emit its cells.
        struct GridVertex
        {
            Vec3 World;
            Vec3 View;
            Vec3 Normal;
        };

        std::array<GridVertex, (MatrixBlockCells + 1) * (MatrixBlockCells + 1)> Grid;
        for (int Bz = 0; Bz < BlocksZ; ++Bz)
        {
            for (int Bx = 0; Bx < BlocksX; ++Bx)
            {
                const Block& Current = Blocks[static_cast<size_t>(Bz) * static_cast<size_t>(BlocksX) + Bx];
                if (!Current.Visible)
                    continue;
                const int Step = Current.Step;
                const int X0 = Bx * MatrixBlockCells, X1 = std::min(X0 + MatrixBlockCells, SegX);
                const int Z0 = Bz * MatrixBlockCells, Z1 = std::min(Z0 + MatrixBlockCells, SegZ);
                const int Columns = (X1 - X0) / Step + 1;
                const int Rows = (Z1 - Z0) / Step + 1;
                // An edge shared with a coarser block follows that block's straight edge, so no crack opens.
                const int StepBelow = StepOf(Bx, Bz - 1), StepAbove = StepOf(Bx, Bz + 1);
                const int StepLeft = StepOf(Bx - 1, Bz), StepRight = StepOf(Bx + 1, Bz);
                for (int Row = 0; Row < Rows; ++Row)
                {
                    for (int Column = 0; Column < Columns; ++Column)
                    {
                        const int X = X0 + Column * Step;
                        const int Z = Z0 + Row * Step;
                        float H = Height(X, Z);
                        const int EdgeStepX = Row == 0 ? StepBelow : (Row == Rows - 1 ? StepAbove : 0);
                        const int EdgeStepZ = Column == 0 ? StepLeft : (Column == Columns - 1 ? StepRight : 0);
                        if (EdgeStepX > Step && X % EdgeStepX != 0)
                        {
                            const int Left = X - X % EdgeStepX;
                            const float T = static_cast<float>(X - Left) / static_cast<float>(EdgeStepX);
                            H = Height(Left, Z) + (Height(Left + EdgeStepX, Z) - Height(Left, Z)) * T;
                        }
                        else if (EdgeStepZ > Step && Z % EdgeStepZ != 0)
                        {
                            const int Below = Z - Z % EdgeStepZ;
                            const float T = static_cast<float>(Z - Below) / static_cast<float>(EdgeStepZ);
                            H = Height(X, Below) + (Height(X, Below + EdgeStepZ) - Height(X, Below)) * T;
                        }
                        GridVertex& Vertex = Grid[static_cast<size_t>(Row) * (MatrixBlockCells + 1) + Column];
                        Vertex.World = {MatrixValue.PosX + static_cast<float>(X) * StepX,
                                        MatrixValue.PosY + H * HeightScale,
                                        MatrixValue.PosZ + SignZ * static_cast<float>(Z) * StepZ};
                        Vertex.View = dbc::WorldToViewPoint(K, Vertex.World.X, Vertex.World.Y, Vertex.World.Z);
                        Vertex.Normal = Vec3{};
                        if (MatrixValue.Light)
                        {
                            const Vec3 Normal = MatrixVertexNormal(MatrixValue, X, Z, StepX, StepZ);
                            Vertex.Normal = dbc::WorldToViewDirection(K, Normal.X, Normal.Y, Normal.Z);
                        }
                    }
                }

                for (int Row = 0; Row + 1 < Rows; ++Row)
                {
                    for (int Column = 0; Column + 1 < Columns; ++Column)
                    {
                        const int X = X0 + Column * Step;
                        const int Z = Z0 + Row * Step;
                        const auto At = [&](int C, int R) -> const GridVertex&
                        { return Grid[static_cast<size_t>(R) * (MatrixBlockCells + 1) + C]; };
                        const GridVertex& P00 = At(Column, Row);
                        const GridVertex& P10 = At(Column + 1, Row);
                        const GridVertex& P11 = At(Column + 1, Row + 1);
                        const GridVertex& P01 = At(Column, Row + 1);

                        float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
                        if (MatrixValue.IsTerrain)
                        {
                            U0 = static_cast<float>(X) / static_cast<float>(SegX) * MatrixValue.TilesX;
                            V0 = static_cast<float>(Z) / static_cast<float>(SegZ) * MatrixValue.TilesY;
                            U1 = static_cast<float>(X + Step) / static_cast<float>(SegX) * MatrixValue.TilesX;
                            V1 = static_cast<float>(Z + Step) / static_cast<float>(SegZ) * MatrixValue.TilesY;
                        }
                        else
                        {
                            const int TileId =
                                std::clamp(MatrixManager::MatrixTile(MatrixValue, X, Z) - 1, 0, TileCount - 1);
                            U0 = static_cast<float>(TileId % MatrixValue.TilesX) * TileW + MatrixValue.TrimX;
                            V0 = static_cast<float>(TileId / MatrixValue.TilesX) * TileH + MatrixValue.TrimY;
                            U1 = static_cast<float>(TileId % MatrixValue.TilesX + 1) * TileW - MatrixValue.TrimX;
                            V1 = static_cast<float>(TileId / MatrixValue.TilesX + 1) * TileH - MatrixValue.TrimY;
                        }

                        // White vertex colour: the shader lights the surface from normals and uniforms.
                        const auto Corner = [](const GridVertex& P, float U, float V)
                        {
                            ViewVertex Result{};
                            Result.X = P.View.X;
                            Result.Y = P.View.Y;
                            Result.Z = P.View.Z;
                            Result.Nx = P.Normal.X;
                            Result.Ny = P.Normal.Y;
                            Result.Nz = P.Normal.Z;
                            Result.Uv = SDL_FPoint{U, V};
                            return Result;
                        };
                        const ViewVertex Q0 = Corner(P00, U0, V0);
                        const ViewVertex Q1 = Corner(P10, U1, V0);
                        const ViewVertex Q2 = Corner(P11, U1, V1);
                        const ViewVertex Q3 = Corner(P01, U0, V1);
                        if (MatrixValue.IsTerrain)
                        {
                            if (FrontFacing(P00.World, P11.World, P10.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q2, Q1, Texture, GpuTexture, false, Tris, false,
                                                               Style, AlphaPhase, false, 0.0f, GhostMode, Priority);
                            if (FrontFacing(P00.World, P01.World, P11.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q3, Q2, Texture, GpuTexture, false, Tris, false,
                                                               Style, AlphaPhase, false, 0.0f, GhostMode, Priority);
                        }
                        else
                        {
                            if (FrontFacing(P00.World, P10.World, P11.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q1, Q2, Texture, GpuTexture, false, Tris, false,
                                                               Style, AlphaPhase, false, 0.0f, GhostMode, Priority);
                            if (FrontFacing(P00.World, P11.World, P01.World))
                                dbc::AppendClippedViewTriangle(K, Q0, Q2, Q3, Texture, GpuTexture, false, Tris, false,
                                                               Style, AlphaPhase, false, 0.0f, GhostMode, Priority);
                        }
                    }
                }
            }
        }
    }

    // DBPro draws only the leaves the BSP camera's leaf can see (PVS) that also meet the view frustum.
    void RenderBspWorld(EngineKernel& K, std::vector<Triangle>& Tris)
    {
        const BspWorldSystem& World = K.BspWorld();
        if (!World.HasWorld())
            return;
        const BspMap& Map = World.Map();
        const bool Reflection = K.Renderer().ReflectionViewEnabled();
        const int Leaf = FindBspLeaf(Map, Reflection ? ActiveCameraPosition(K) : World.CullingPosition());
        if (Leaf < 0)
            return;
        std::vector<uint8_t> Visible;
        DecodeBspPvs(Map, Leaf, Visible);

        std::optional<RenderView> CameraView;
        const RenderView* View = K.Renderer().ActiveView();
        if (!View)
        {
            CameraView.emplace(K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight()));
            View = &*CameraView;
        }
        const Vec3 Camera = ActiveCameraPosition(K);

        std::vector<SDL_Texture*> Textures(Map.TextureNames.size(), nullptr);
        std::vector<SDL_GPUTexture*> GpuTextures(Map.TextureNames.size(), nullptr);
        for (std::size_t Index = 0; Index < Map.TextureNames.size(); ++Index)
        {
            if (Image* Img = K.ImageStoreRef().GetImage(World.TextureImage(static_cast<int>(Index))))
            {
                Textures[Index] = Img->Texture;
                GpuTextures[Index] = dbc::ImageGpuTexture(K, *Img, false);
            }
        }

        const SurfaceStyle Style{};
        for (std::size_t LeafIndex = 0; LeafIndex < Map.Leaves.size(); ++LeafIndex)
        {
            if (!Visible[LeafIndex])
                continue;
            const BspLeaf& LeafValue = Map.Leaves[LeafIndex];
            const Vec3 Center{(LeafValue.BoxMin.X + LeafValue.BoxMax.X) * 0.5f,
                              (LeafValue.BoxMin.Y + LeafValue.BoxMax.Y) * 0.5f,
                              (LeafValue.BoxMin.Z + LeafValue.BoxMax.Z) * 0.5f};
            const Vec3 Half = Vec3Sub(LeafValue.BoxMax, Center);
            const Vec3 ViewCenter = dbc::WorldToViewPoint(K, Center.X, Center.Y, Center.Z);
            if (!View->ContainsSphere(glm::vec3(ViewCenter.X, ViewCenter.Y, ViewCenter.Z),
                                      std::sqrt(Vec3Dot(Half, Half))))
                continue;
            for (int32_t PolygonIndex = LeafValue.FirstPolygon; PolygonIndex < LeafValue.EndPolygon; ++PolygonIndex)
            {
                const BspPolygon& Polygon = Map.Polygons[static_cast<std::size_t>(PolygonIndex)];
                if (Polygon.Vertices.empty())
                    continue;
                // Stored normals face open space; DBPro culled the other side except in reflections.
                const Vec3 ToCamera = Vec3Sub(Camera, Polygon.Vertices.front().Position);
                if (!Reflection && Vec3Dot(Polygon.Normal, ToCamera) <= 0.0f)
                    continue;
                const Vec3 Normal = dbc::WorldToViewDirection(K, Polygon.Normal.X, Polygon.Normal.Y, Polygon.Normal.Z);
                std::array<ViewVertex, 3> Corners{};
                for (std::size_t Index = 0; Index + 2 < Polygon.Indices.size(); Index += 3)
                {
                    for (std::size_t Corner = 0; Corner < 3; ++Corner)
                    {
                        const BspVertex& Source = Polygon.Vertices[Polygon.Indices[Index + Corner]];
                        const Vec3 Point =
                            dbc::WorldToViewPoint(K, Source.Position.X, Source.Position.Y, Source.Position.Z);
                        ViewVertex& Target = Corners[Corner];
                        Target.X = Point.X;
                        Target.Y = Point.Y;
                        Target.Z = Point.Z;
                        Target.Nx = Normal.X;
                        Target.Ny = Normal.Y;
                        Target.Nz = Normal.Z;
                        Target.Uv = SDL_FPoint{Source.U, Source.V};
                        Target.Color = SDL_FColor{static_cast<float>((Source.Color >> 16) & 0xFFu) / 255.0f,
                                                  static_cast<float>((Source.Color >> 8) & 0xFFu) / 255.0f,
                                                  static_cast<float>(Source.Color & 0xFFu) / 255.0f,
                                                  static_cast<float>((Source.Color >> 24) & 0xFFu) / 255.0f};
                    }
                    dbc::AppendClippedViewTriangle(K, Corners[0], Corners[1], Corners[2], Textures[Polygon.Texture],
                                                   GpuTextures[Polygon.Texture], false, Tris, false, Style);
                }
            }
        }
    }

    void RenderObjectBounds(EngineKernel& K, const Object3D& Object, std::vector<Triangle>& Tris)
    {
        if (!Object.DrawBounds || Object.Meshes.empty() || Object.LockToScreen)
            return;

        Vec3 Minimum{};
        Vec3 Maximum{};
        bool First = true;
        for (const Mesh& MeshValue : Object.Meshes)
        {
            for (const Vec3& Vertex : MeshValue.Vertices)
            {
                Vec3 Local = ApplyAiTransformPoint(MeshValue.BindTransform, Vertex);
                Local.X *= MeshValue.LimbScaleX;
                Local.Y *= MeshValue.LimbScaleY;
                Local.Z *= MeshValue.LimbScaleZ;
                Local = RotateX(Local, MeshValue.LimbRotX);
                Local = RotateY(Local, MeshValue.LimbRotY);
                Local = RotateZ(Local, MeshValue.LimbRotZ);
                Local.X += MeshValue.LimbOffX;
                Local.Y += MeshValue.LimbOffY;
                Local.Z += MeshValue.LimbOffZ;
                if (First)
                {
                    Minimum = Maximum = Local;
                    First = false;
                }
                else
                {
                    Minimum.X = std::min(Minimum.X, Local.X);
                    Minimum.Y = std::min(Minimum.Y, Local.Y);
                    Minimum.Z = std::min(Minimum.Z, Local.Z);
                    Maximum.X = std::max(Maximum.X, Local.X);
                    Maximum.Y = std::max(Maximum.Y, Local.Y);
                    Maximum.Z = std::max(Maximum.Z, Local.Z);
                }
            }
        }
        if (First)
            return;

        const Vec3 Center{(Minimum.X + Maximum.X) * 0.5f, (Minimum.Y + Maximum.Y) * 0.5f,
                          (Minimum.Z + Maximum.Z) * 0.5f};
        const float Width = std::max(Maximum.X - Minimum.X, 0.0001f);
        const float Height = std::max(Maximum.Y - Minimum.Y, 0.0001f);
        const float Depth = std::max(Maximum.Z - Minimum.Z, 0.0001f);
        const float Radius = std::sqrt(Width * Width + Height * Height + Depth * Depth) * 0.5f;

        SurfaceStyle Style;
        Style.Wireframe = true;
        Style.Fog = false;
        Style.Ambient = false;
        const auto AppendMesh = [&](const Mesh& BoundsMesh, const SDL_FColor& Color)
        {
            for (std::size_t Index = 0; Index + 2 < BoundsMesh.Indices.size(); Index += 3)
            {
                ViewVertex Vertices[3]{};
                bool Valid = true;
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t VertexIndex = BoundsMesh.Indices[Index + static_cast<std::size_t>(Corner)];
                    if (VertexIndex >= BoundsMesh.Vertices.size())
                    {
                        Valid = false;
                        break;
                    }
                    const Vec3& Source = BoundsMesh.Vertices[VertexIndex];
                    const Vec3 World = TransformObjectSpacePoint(
                        K, Object, Vec3{Center.X + Source.X, Center.Y + Source.Y, Center.Z + Source.Z});
                    const Vec3 View = dbc::WorldToViewPoint(K, World.X, World.Y, World.Z);
                    Vertices[Corner].X = View.X;
                    Vertices[Corner].Y = View.Y;
                    Vertices[Corner].Z = View.Z;
                    Vertices[Corner].Color = Color;
                }
                if (Valid)
                {
                    dbc::AppendClippedViewTriangle(K, Vertices[0], Vertices[1], Vertices[2], nullptr, nullptr, false,
                                                   Tris, false, Style, 0, false, -0.0001f, -1, 2, false, true);
                }
            }
        };

        AppendMesh(MakeBoxMesh(Width, Height, Depth), SDL_FColor{1.0f, 0.25f, 0.1f, 1.0f});
        if (!Object.BoundsBoxOnly)
            AppendMesh(MakeSphereMesh(Radius, 8, 12), SDL_FColor{0.2f, 1.0f, 0.35f, 1.0f});
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

    void RenderObjects(EngineKernel& K, std::vector<Triangle>& WorldTris, std::vector<Triangle>& OverlayTris)
    {
        const RenderView View = K.Renderer().ActiveView()
                                    ? *K.Renderer().ActiveView()
                                    : K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight());

        bool HasWaterline = false;
        float WaterlineY = std::numeric_limits<float>::max();
        K.Objects().ForEachObject(
            [&](int, const Object3D& Object)
            {
                if (Object.Visible && Object.TransparencyMode == 5)
                {
                    WaterlineY = std::min(WaterlineY, dbc::WorldObjectOrigin(K, Object).Y);
                    HasWaterline = true;
                }
            });

        K.Objects().ForEachObject(
            [&](int Id, Object3D& Object)
            {
                if (!Object.Visible)
                    return;

                // ── Frustum culling (sphere test) ────────────────────────────────────
                // Skip objects whose bounding sphere lies entirely outside the view frustum.
                // lock_to_screen objects live in screen-space and are never culled.
                if (!Object.LockToScreen &&
                    (!K.Objects().IsStaticRenderObject(Id) || K.Objects().StaticOcclusionEnabled()))
                {
                    const float MaxScale =
                        std::max({std::abs(Object.ScaleX), std::abs(Object.ScaleY), std::abs(Object.ScaleZ)});
                    const float WorldRadius = K.Objects().RenderBoundingRadius(Object) * MaxScale;
                    // For glued objects, use the actual world position (accounting for the glue chain).
                    const Vec3 WorldCenter = (Object.GluedTargetObject > 0)
                                                 ? dbc::WorldObjectOrigin(K, Object)
                                                 : Vec3{Object.PosX, Object.PosY, Object.PosZ};
                    const Vec3 Vc = dbc::WorldToViewPoint(K, WorldCenter.X, WorldCenter.Y, WorldCenter.Z);

                    if (!View.ContainsSphere({Vc.X, Vc.Y, Vc.Z}, WorldRadius, Object.Ghosted && Object.GhostMode == 0))
                        return;
                }
                const auto& NodeGlobals = Object.CachedNodeGlobals;

                int AlphaPhase = -1;
                if (Object.TransparencyMode >= 0)
                {
                    if (Object.TransparencyMode == 0 || Object.TransparencyMode == 7)
                        AlphaPhase = 0;
                    else if (Object.TransparencyMode == 2 || Object.TransparencyMode == 3 ||
                             Object.TransparencyMode == 5 || Object.TransparencyMode == 6)
                        AlphaPhase = 2;
                    else
                        AlphaPhase = 1;
                }
                if (Object.Ghosted)
                    AlphaPhase = 2;
                const bool VeryEarly = Object.TransparencyMode == 7;
                float DepthBias = 0.0f;
                if (HasWaterline && AlphaPhase == 2)
                {
                    bool BelowWaterline = Object.TransparencyMode == 5;
                    if (!BelowWaterline)
                    {
                        float LocalMinY = std::numeric_limits<float>::max();
                        float LocalMaxY = std::numeric_limits<float>::lowest();
                        for (const Mesh& BoundMesh : Object.Meshes)
                        {
                            for (const Vec3& Vertex : BoundMesh.Vertices)
                            {
                                const Vec3 BoundVertex = ApplyAiTransformPoint(BoundMesh.BindTransform, Vertex);
                                LocalMinY = std::min(LocalMinY, BoundVertex.Y);
                                LocalMaxY = std::max(LocalMaxY, BoundVertex.Y);
                            }
                        }
                        const float Height = LocalMaxY >= LocalMinY ? LocalMaxY - LocalMinY : 0.0f;
                        const float BaseY = dbc::WorldObjectOrigin(K, Object).Y - Height * std::abs(Object.ScaleY);
                        BelowWaterline = BaseY < WaterlineY;
                    }
                    if (BelowWaterline)
                    {
                        const glm::vec4 FarPlane = View.ClipPlane(5);
                        DepthBias = FarPlane.z < 0.0f ? -FarPlane.w / FarPlane.z : 1e7f;
                    }
                }

                for (const Mesh& MeshValue : Object.Meshes)
                {
                    if (!MeshValue.Visible)
                    {
                        continue;
                    }
                    const aiMatrix4x4* MeshTransform = nullptr;
                    const aiNode* MeshNode = nullptr;
                    if (!NodeGlobals.empty() && MeshValue.SourceMeshIndex >= 0)
                    {
                        const auto MnIt =
                            Object.MeshNodeByIndex.find(static_cast<unsigned int>(MeshValue.SourceMeshIndex));
                        if (MnIt != Object.MeshNodeByIndex.end())
                        {
                            MeshNode = MnIt->second;
                            const auto MtIt = NodeGlobals.find(MnIt->second);
                            if (MtIt != NodeGlobals.end())
                            {
                                MeshTransform = &MtIt->second;
                            }
                        }
                    }
                    if (MeshNode)
                    {
                        bool HiddenByLimb = false;
                        for (const aiNode* N = MeshNode; N; N = N->mParent)
                        {
                            const auto LiIt = Object.LimbIndexByNode.find(N);
                            if (LiIt == Object.LimbIndexByNode.end())
                            {
                                continue;
                            }
                            const int Li = LiIt->second;
                            if (Li >= 0 && Li < static_cast<int>(Object.LimbTransforms.size()) &&
                                Object.LimbTransforms[static_cast<size_t>(Li)].Hidden)
                            {
                                HiddenByLimb = true;
                                break;
                            }
                        }
                        if (HiddenByLimb)
                        {
                            continue;
                        }
                    }
                    const std::vector<aiMatrix4x4> BonePalette = BuildBonePalette(Object, MeshValue, NodeGlobals);
                    std::vector<Vec3> SkinnedPositions;
                    std::vector<Vec3> SkinnedNormals;
                    std::vector<uint8_t> SkinnedVertices;
                    if (!BonePalette.empty())
                    {
                        SkinnedPositions = MeshValue.Vertices;
                        SkinnedNormals.resize(MeshValue.Vertices.size());
                        SkinnedVertices.resize(MeshValue.Vertices.size());
                        for (std::size_t VertexIndex = 0; VertexIndex < MeshValue.Vertices.size(); ++VertexIndex)
                        {
                            SkinnedVertices[VertexIndex] =
                                SkinMeshVertex(MeshValue, VertexIndex, BonePalette, SkinnedPositions[VertexIndex],
                                               SkinnedNormals[VertexIndex])
                                    ? 1
                                    : 0;
                        }
                    }
                    for (size_t I = 0; I + 2 < MeshValue.Indices.size(); I += 3)
                    {
                        const uint32_t I0 = MeshValue.Indices[I];
                        const uint32_t I1 = MeshValue.Indices[I + 1];
                        const uint32_t I2 = MeshValue.Indices[I + 2];
                        if (I0 >= MeshValue.Vertices.size() || I1 >= MeshValue.Vertices.size() ||
                            I2 >= MeshValue.Vertices.size())
                        {
                            continue;
                        }

                        const bool Skinned0 = !SkinnedVertices.empty() && SkinnedVertices[I0] != 0;
                        const bool Skinned1 = !SkinnedVertices.empty() && SkinnedVertices[I1] != 0;
                        const bool Skinned2 = !SkinnedVertices.empty() && SkinnedVertices[I2] != 0;
                        const Vec3& P0 = Skinned0 ? SkinnedPositions[I0] : MeshValue.Vertices[I0];
                        const Vec3& P1 = Skinned1 ? SkinnedPositions[I1] : MeshValue.Vertices[I1];
                        const Vec3& P2 = Skinned2 ? SkinnedPositions[I2] : MeshValue.Vertices[I2];
                        const Vec3 W0 = dbc::TransformObjectPoint(K, Object, MeshValue, P0, MeshTransform, Skinned0);
                        const Vec3 W1 = dbc::TransformObjectPoint(K, Object, MeshValue, P1, MeshTransform, Skinned1);
                        const Vec3 W2 = dbc::TransformObjectPoint(K, Object, MeshValue, P2, MeshTransform, Skinned2);

                        const Vec3 V0 =
                            Object.LockToScreen ? Vec3{W0.X, W0.Y, W0.Z} : dbc::WorldToViewPoint(K, W0.X, W0.Y, W0.Z);
                        const Vec3 V1 =
                            Object.LockToScreen ? Vec3{W1.X, W1.Y, W1.Z} : dbc::WorldToViewPoint(K, W1.X, W1.Y, W1.Z);
                        const Vec3 V2 =
                            Object.LockToScreen ? Vec3{W2.X, W2.Y, W2.Z} : dbc::WorldToViewPoint(K, W2.X, W2.Y, W2.Z);

                        const Vec3 E1 = Vec3Sub(W1, W0);
                        const Vec3 E2 = Vec3Sub(W2, W0);
                        const Vec3 FaceNormal = Vec3Normalize(Vec3Cross(E1, E2));
                        if (Object.Cull && !K.Renderer().ReflectionViewEnabled())
                        {
                            const float Cx = (W0.X + W1.X + W2.X) / 3.0f;
                            const float Cy = (W0.Y + W1.Y + W2.Y) / 3.0f;
                            const float Cz = (W0.Z + W1.Z + W2.Z) / 3.0f;
                            const Vec3 Camera = ActiveCameraPosition(K);
                            const Vec3 ToCam{Camera.X - Cx, Camera.Y - Cy, Camera.Z - Cz};
                            const float Facing = Vec3Dot(FaceNormal, ToCam);
                            if (Facing <= 0.0f)
                            {
                                continue;
                            }
                        }
                        // Per-vertex normals in world space (or face normal fallback).
                        Vec3 Wn0 = FaceNormal;
                        Vec3 Wn1 = FaceNormal;
                        Vec3 Wn2 = FaceNormal;
                        if (MeshValue.Normals.size() == MeshValue.Vertices.size())
                        {
                            Wn0 = dbc::TransformObjectNormal(K, Object, MeshValue,
                                                             Skinned0 ? SkinnedNormals[I0] : MeshValue.Normals[I0],
                                                             MeshTransform, Skinned0);
                            Wn1 = dbc::TransformObjectNormal(K, Object, MeshValue,
                                                             Skinned1 ? SkinnedNormals[I1] : MeshValue.Normals[I1],
                                                             MeshTransform, Skinned1);
                            Wn2 = dbc::TransformObjectNormal(K, Object, MeshValue,
                                                             Skinned2 ? SkinnedNormals[I2] : MeshValue.Normals[I2],
                                                             MeshTransform, Skinned2);
                        }

                        // Transform normals to view-space for GPU lighting.
                        // For unlit objects, pass zero normals so the shader skips lighting.
                        Vec3 Vn0{}, Vn1{}, Vn2{};
                        if (Object.UseLighting || MeshValue.BuiltInEffect != 0)
                        {
                            Vn0 = dbc::WorldToViewDirection(K, Wn0.X, Wn0.Y, Wn0.Z);
                            Vn1 = dbc::WorldToViewDirection(K, Wn1.X, Wn1.Y, Wn1.Z);
                            Vn2 = dbc::WorldToViewDirection(K, Wn2.X, Wn2.Y, Wn2.Z);
                            // DBC FADE OBJECT: normal magnitude encodes the fade multiplier.
                            // The shader uses length(N) as a diffuse/specular multiplier:
                            //   0.015 → ambient only,  1.0 → normal,  2.0 → 2× bright.
                            // Clamp minimum to 0.015 so lit objects never hit the shader's
                            // unlit path (length < 0.01) which outputs full brightness.
                            const float Fs = std::max(Object.FadeNormalScale, 0.015f);
                            if (Fs != 1.0f)
                            {
                                Vn0.X *= Fs;
                                Vn0.Y *= Fs;
                                Vn0.Z *= Fs;
                                Vn1.X *= Fs;
                                Vn1.Y *= Fs;
                                Vn1.Z *= Fs;
                                Vn2.X *= Fs;
                                Vn2.Y *= Fs;
                                Vn2.Z *= Fs;
                            }
                        }

                        SDL_Texture* Texture = nullptr;
                        SDL_GPUTexture* GpuTexture = nullptr;
                        std::array<SDL_GPUTexture*, 7> AuxiliaryGpuTextures{};
                        SDL_GPUTexture* CubeGpuTexture = Object.CubeGpuTexture.get();
                        bool BlendTransparent = false;
                        int TextureImageId = Object.TextureImageId;
                        if (TextureImageId == 0)
                        {
                            TextureImageId = MeshValue.TextureImageId;
                        }
                        if (const auto StageZero = MeshValue.TextureStageImageIds.find(0);
                            StageZero != MeshValue.TextureStageImageIds.end())
                            TextureImageId = StageZero->second;
                        if (TextureImageId != 0)
                        {
                            Image* TexImg = K.ImageStoreRef().GetImage(TextureImageId);
                            if (TexImg)
                            {
                                Texture = Object.TransparentBlack ? K.ImageStoreRef().EnsureBlackKeyTexture(*TexImg)
                                                                  : TexImg->Texture;
                                GpuTexture = dbc::ImageGpuTexture(K, *TexImg, Object.TransparentBlack);
                                // transparent_black textures have alpha=0 for black pixels.
                                // The fragment shader discards fragments with alpha <= 0.001,
                                // so these objects stay in the OPAQUE pass (depth write ON):
                                //  • black pixels → alpha=0 → discard → no depth/color write
                                //  • non-black pixels → alpha=1 → normal depth write
                                // This correctly blocks additive objects (ghost water, etc.)
                                // behind opaque regions while allowing see-through on keyed areas.
                                if (!Object.TransparentBlack)
                                {
                                    BlendTransparent = TexImg->HasPartialAlpha;
                                }
                            }
                        }
                        for (int Stage = 1; Stage < 8; ++Stage)
                        {
                            if (const auto StageImage = MeshValue.TextureStageImageIds.find(Stage);
                                StageImage != MeshValue.TextureStageImageIds.end())
                            {
                                if (Image* AuxiliaryImage = K.ImageStoreRef().GetImage(StageImage->second))
                                    AuxiliaryGpuTextures[static_cast<std::size_t>(Stage - 1)] =
                                        dbc::ImageGpuTexture(K, *AuxiliaryImage, false);
                            }
                        }
                        // Ghost mode 0 = additive blending, mode 1 = alpha blending.
                        const bool BlendAdditive = Object.Ghosted && Object.GhostMode == 0;
                        if (Object.Ghosted || Object.Alpha < 0.999f || Object.AlphaMapping)
                        {
                            BlendTransparent = true;
                        }

                        // LOAD OBJECT modes 2-5 preserve the imported diffuse material when texturing.
                        const bool HasTexture = (GpuTexture != nullptr || Texture != nullptr);
                        const bool UseMaterial = !HasTexture || MeshValue.TextureUsesMaterialColor;
                        const SDL_FColor Col{std::clamp(Object.TintR * MeshValue.LimbTint.r *
                                                            (UseMaterial ? MeshValue.MaterialColor.r : 1.0f),
                                                        0.0f, 1.0f),
                                             std::clamp(Object.TintG * MeshValue.LimbTint.g *
                                                            (UseMaterial ? MeshValue.MaterialColor.g : 1.0f),
                                                        0.0f, 1.0f),
                                             std::clamp(Object.TintB * MeshValue.LimbTint.b *
                                                            (UseMaterial ? MeshValue.MaterialColor.b : 1.0f),
                                                        0.0f, 1.0f),
                                             std::clamp(Object.Alpha, 0.0f, 1.0f)};
                        // As in DBPro's fixed pipeline, vertex alpha only matters to objects that blend or
                        // alpha-test. An opaque object stays opaque when RGB() leaves the alpha byte at zero.
                        const bool UsesVertexAlpha =
                            Object.Ghosted || Object.Alpha < 0.999f || Object.AlphaMapping || Object.TransparentBlack;
                        const auto VertexColor = [&](uint32_t Index)
                        {
                            const uint32_t Packed =
                                Index < MeshValue.DiffuseColors.size() ? MeshValue.DiffuseColors[Index] : 0xFFFFFFFFu;
                            return SDL_FColor{
                                Col.r * static_cast<float>((Packed >> 16u) & 0xFFu) / 255.0f,
                                Col.g * static_cast<float>((Packed >> 8u) & 0xFFu) / 255.0f,
                                Col.b * static_cast<float>(Packed & 0xFFu) / 255.0f,
                                UsesVertexAlpha ? Col.a * static_cast<float>((Packed >> 24u) & 0xFFu) / 255.0f : Col.a,
                            };
                        };
                        const SDL_FColor Color0 = VertexColor(I0);
                        const SDL_FColor Color1 = VertexColor(I1);
                        const SDL_FColor Color2 = VertexColor(I2);

                        SDL_FPoint T0{Object.TexScrollU, Object.TexScrollV};
                        SDL_FPoint T1{Object.TexScrollU, Object.TexScrollV};
                        SDL_FPoint T2{Object.TexScrollU, Object.TexScrollV};
                        if (!MeshValue.Uvs.empty())
                        {
                            if (I0 < MeshValue.Uvs.size())
                            {
                                T0 = MeshValue.Uvs[I0];
                            }
                            if (I1 < MeshValue.Uvs.size())
                            {
                                T1 = MeshValue.Uvs[I1];
                            }
                            if (I2 < MeshValue.Uvs.size())
                            {
                                T2 = MeshValue.Uvs[I2];
                            }
                            T0.x = T0.x * Object.TexScaleU + Object.TexScrollU;
                            T0.y = T0.y * Object.TexScaleV + Object.TexScrollV;
                            T1.x = T1.x * Object.TexScaleU + Object.TexScrollU;
                            T1.y = T1.y * Object.TexScaleV + Object.TexScrollV;
                            T2.x = T2.x * Object.TexScaleU + Object.TexScrollU;
                            T2.y = T2.y * Object.TexScaleV + Object.TexScrollV;
                        }

                        const auto ReflectionDirection = [&](const Vec3& WorldPoint, const Vec3& WorldNormal)
                        {
                            const Vec3 Camera = ActiveCameraPosition(K);
                            const Vec3 Incident = Vec3Normalize(
                                Vec3{WorldPoint.X - Camera.X, WorldPoint.Y - Camera.Y, WorldPoint.Z - Camera.Z});
                            const Vec3 Normal = Vec3Normalize(WorldNormal);
                            const float Projection = 2.0f * Vec3Dot(Incident, Normal);
                            return Vec3Normalize(Vec3{Incident.X - Projection * Normal.X,
                                                      Incident.Y - Projection * Normal.Y,
                                                      Incident.Z - Projection * Normal.Z});
                        };
                        const Vec3 Reflection0 = ReflectionDirection(W0, Wn0);
                        const Vec3 Reflection1 = ReflectionDirection(W1, Wn1);
                        const Vec3 Reflection2 = ReflectionDirection(W2, Wn2);
                        const auto ResolveStageUv = [&](uint32_t VertexIndex, int Stage, SDL_FPoint BaseUv,
                                                        const Vec3& WorldNormal, const Vec3& Reflection)
                        {
                            int CoordinateMode = 0;
                            if (const auto Mode = MeshValue.TextureStageCoordinateModes.find(Stage);
                                Mode != MeshValue.TextureStageCoordinateModes.end())
                                CoordinateMode = Mode->second;
                            if (CoordinateMode == 1)
                            {
                                const Vec3 ViewNormal = Vec3Normalize(
                                    dbc::WorldToViewDirection(K, WorldNormal.X, WorldNormal.Y, WorldNormal.Z));
                                return SDL_FPoint{ViewNormal.X * 0.5f + 0.5f, ViewNormal.Y * -0.5f + 0.5f};
                            }
                            if (CoordinateMode == 2)
                                return SDL_FPoint{Reflection.X * 0.5f + 0.5f, Reflection.Y * -0.5f + 0.5f};
                            int SourceStage = Stage;
                            if (CoordinateMode == 3)
                                SourceStage = 0;
                            else if (CoordinateMode >= 10 && CoordinateMode <= 17)
                                SourceStage = CoordinateMode - 10;
                            if (SourceStage == 0)
                                return BaseUv;
                            if (const auto StageUvs = MeshValue.UvStages.find(SourceStage);
                                StageUvs != MeshValue.UvStages.end() && VertexIndex < StageUvs->second.size())
                                return StageUvs->second[VertexIndex];
                            return BaseUv;
                        };
                        T0 = ResolveStageUv(I0, 0, T0, Wn0, Reflection0);
                        T1 = ResolveStageUv(I1, 0, T1, Wn1, Reflection1);
                        T2 = ResolveStageUv(I2, 0, T2, Wn2, Reflection2);
                        std::array<SDL_FPoint, 7> AuxiliaryUvs0{};
                        std::array<SDL_FPoint, 7> AuxiliaryUvs1{};
                        std::array<SDL_FPoint, 7> AuxiliaryUvs2{};
                        for (int Stage = 1; Stage < 8; ++Stage)
                        {
                            const std::size_t AuxiliaryIndex = static_cast<std::size_t>(Stage - 1);
                            AuxiliaryUvs0[AuxiliaryIndex] = ResolveStageUv(I0, Stage, T0, Wn0, Reflection0);
                            AuxiliaryUvs1[AuxiliaryIndex] = ResolveStageUv(I1, Stage, T1, Wn1, Reflection1);
                            AuxiliaryUvs2[AuxiliaryIndex] = ResolveStageUv(I2, Stage, T2, Wn2, Reflection2);
                        }

                        if (MeshValue.BuiltInEffect == 2)
                        {
                            const auto EffectUvs = [&](const Vec3& WorldPoint, const Vec3& WorldNormal)
                            {
                                const Vec3 Normal = Vec3Normalize(WorldNormal);
                                Vec3 ToLight{};
                                if (K.Lights().LightVisible(0) != 0)
                                {
                                    if (K.Lights().LightType(0) == 1 || K.Lights().LightType(0) == 2)
                                    {
                                        ToLight = Vec3Normalize(Vec3{K.Lights().LightPositionX(0) - WorldPoint.X,
                                                                     K.Lights().LightPositionY(0) - WorldPoint.Y,
                                                                     K.Lights().LightPositionZ(0) - WorldPoint.Z});
                                    }
                                    else
                                    {
                                        ToLight = Vec3Normalize(Vec3{-K.Lights().LightDirectionX(0),
                                                                     -K.Lights().LightDirectionY(0),
                                                                     -K.Lights().LightDirectionZ(0)});
                                    }
                                }
                                const Vec3 Camera = ActiveCameraPosition(K);
                                const Vec3 ToEye = Vec3Normalize(
                                    Vec3{Camera.X - WorldPoint.X, Camera.Y - WorldPoint.Y, Camera.Z - WorldPoint.Z});
                                return std::array<SDL_FPoint, 2>{
                                    SDL_FPoint{std::clamp(Vec3Dot(Normal, ToLight), 0.0f, 1.0f), 0.5f},
                                    SDL_FPoint{std::clamp(Vec3Dot(Normal, ToEye), 0.0f, 1.0f), 0.5f}};
                            };
                            const auto Effect0 = EffectUvs(W0, Wn0);
                            const auto Effect1 = EffectUvs(W1, Wn1);
                            const auto Effect2 = EffectUvs(W2, Wn2);
                            T0 = Effect0[0];
                            T1 = Effect1[0];
                            T2 = Effect2[0];
                            AuxiliaryUvs0[0] = Effect0[1];
                            AuxiliaryUvs1[0] = Effect1[1];
                            AuxiliaryUvs2[0] = Effect2[1];
                        }
                        else if (MeshValue.BuiltInEffect == 3)
                        {
                            const auto EffectUvs = [&](const Vec3& WorldPoint, const Vec3& WorldNormal)
                            {
                                const Vec3 Normal = Vec3Normalize(WorldNormal);
                                const Vec3 Camera = ActiveCameraPosition(K);
                                const Vec3 Eye = Vec3Normalize(
                                    Vec3{Camera.X - WorldPoint.X, Camera.Y - WorldPoint.Y, Camera.Z - WorldPoint.Z});
                                const Vec3 Half =
                                    Vec3Normalize(Vec3{Normal.X + Eye.X, Normal.Y + Eye.Y, Normal.Z + Eye.Z});
                                return std::array<SDL_FPoint, 2>{
                                    SDL_FPoint{Vec3Dot(Normal, Eye), Vec3Dot(Half, Normal)}, SDL_FPoint{Eye.X, Half.Y}};
                            };
                            const auto Effect0 = EffectUvs(W0, Wn0);
                            const auto Effect1 = EffectUvs(W1, Wn1);
                            const auto Effect2 = EffectUvs(W2, Wn2);
                            T0 = Effect0[0];
                            T1 = Effect1[0];
                            T2 = Effect2[0];
                            AuxiliaryUvs0[0] = Effect0[1];
                            AuxiliaryUvs1[0] = Effect1[1];
                            AuxiliaryUvs2[0] = Effect2[1];
                        }

                        Vec3 ShaderDirection0 = Reflection0;
                        Vec3 ShaderDirection1 = Reflection1;
                        Vec3 ShaderDirection2 = Reflection2;
                        if (MeshValue.BuiltInEffect == 1)
                        {
                            const Vec3 Edge1 = Vec3Sub(V1, V0);
                            const Vec3 Edge2 = Vec3Sub(V2, V0);
                            const float DeltaU1 = T1.x - T0.x;
                            const float DeltaV1 = T1.y - T0.y;
                            const float DeltaU2 = T2.x - T0.x;
                            const float DeltaV2 = T2.y - T0.y;
                            const float Determinant = DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1;
                            Vec3 Tangent{};
                            if (std::abs(Determinant) > 0.000001f)
                            {
                                const float Inverse = 1.0f / Determinant;
                                Tangent = Vec3Normalize(Vec3{(Edge1.X * DeltaV2 - Edge2.X * DeltaV1) * Inverse,
                                                             (Edge1.Y * DeltaV2 - Edge2.Y * DeltaV1) * Inverse,
                                                             (Edge1.Z * DeltaV2 - Edge2.Z * DeltaV1) * Inverse});
                            }
                            if (Vec3Dot(Tangent, Tangent) < 0.000001f)
                            {
                                const Vec3 Normal = Vec3Normalize(Vn0);
                                const Vec3 Axis =
                                    std::abs(Normal.Y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
                                Tangent = Vec3Normalize(Vec3Cross(Axis, Normal));
                            }
                            ShaderDirection0 = Tangent;
                            ShaderDirection1 = Tangent;
                            ShaderDirection2 = Tangent;
                        }

                        if (Object.LockToScreen)
                        {
                            SDL_FPoint Screen0{};
                            SDL_FPoint Screen1{};
                            SDL_FPoint Screen2{};
                            float Pd0 = 0.0f;
                            float Pd1 = 0.0f;
                            float Pd2 = 0.0f;
                            if (!dbc::ProjectPointLocked(K, V0.X, V0.Y, V0.Z, Screen0, Pd0) ||
                                !dbc::ProjectPointLocked(K, V1.X, V1.Y, V1.Z, Screen1, Pd1) ||
                                !dbc::ProjectPointLocked(K, V2.X, V2.Y, V2.Z, Screen2, Pd2))
                            {
                                continue;
                            }

                            Triangle Tri;
                            Tri.Texture = Texture;
                            Tri.GpuTexture = GpuTexture;
                            Tri.Depth = (Pd0 + Pd1 + Pd2) / 3.0f;
                            const bool AutomaticTransparency =
                                BlendTransparent || Color0.a < 0.999f || Color1.a < 0.999f || Color2.a < 0.999f;
                            Tri.Transparent = AlphaPhase == 2 || (AlphaPhase < 0 && AutomaticTransparency);
                            Tri.AlphaDepthWrite = AlphaPhase == 1;
                            Tri.Additive = BlendAdditive;
                            Tri.VeryEarly = VeryEarly;
                            Tri.Is2d = true;
                            Tri.DepthTest = Object.DepthTest;
                            Tri.DepthWrite = Object.DepthWrite && !BlendAdditive;
                            Tri.Style = BuildSurfaceStyle(Object, MeshValue);
                            Tri.Pos[0][0] = Screen0.x;
                            Tri.Pos[0][1] = Screen0.y;
                            Tri.Pos[0][2] = 0.0f;
                            Tri.Pos[1][0] = Screen1.x;
                            Tri.Pos[1][1] = Screen1.y;
                            Tri.Pos[1][2] = 0.0f;
                            Tri.Pos[2][0] = Screen2.x;
                            Tri.Pos[2][1] = Screen2.y;
                            Tri.Pos[2][2] = 0.0f;
                            Tri.Normal[0][0] = 0.0f;
                            Tri.Normal[0][1] = 0.0f;
                            Tri.Normal[0][2] = 0.0f;
                            Tri.Normal[1][0] = 0.0f;
                            Tri.Normal[1][1] = 0.0f;
                            Tri.Normal[1][2] = 0.0f;
                            Tri.Normal[2][0] = 0.0f;
                            Tri.Normal[2][1] = 0.0f;
                            Tri.Normal[2][2] = 0.0f;
                            Tri.Color[0] = Color0;
                            Tri.Color[1] = Color1;
                            Tri.Color[2] = Color2;
                            Tri.Uv[0] = T0;
                            Tri.Uv[1] = T1;
                            Tri.Uv[2] = T2;
                            Tri.AuxiliaryGpuTextures = AuxiliaryGpuTextures;
                            Tri.CubeGpuTexture = CubeGpuTexture;
                            for (std::size_t Stage = 0; Stage < Tri.AuxiliaryUvs.size(); ++Stage)
                            {
                                Tri.AuxiliaryUvs[Stage][0] = AuxiliaryUvs0[Stage];
                                Tri.AuxiliaryUvs[Stage][1] = AuxiliaryUvs1[Stage];
                                Tri.AuxiliaryUvs[Stage][2] = AuxiliaryUvs2[Stage];
                            }
                            Tri.CubeDirection[0][0] = ShaderDirection0.X;
                            Tri.CubeDirection[0][1] = ShaderDirection0.Y;
                            Tri.CubeDirection[0][2] = ShaderDirection0.Z;
                            Tri.CubeDirection[1][0] = ShaderDirection1.X;
                            Tri.CubeDirection[1][1] = ShaderDirection1.Y;
                            Tri.CubeDirection[1][2] = ShaderDirection1.Z;
                            Tri.CubeDirection[2][0] = ShaderDirection2.X;
                            Tri.CubeDirection[2][1] = ShaderDirection2.Y;
                            Tri.CubeDirection[2][2] = ShaderDirection2.Z;
                            OverlayTris.push_back(Tri);
                        }
                        else
                        {
                            ViewVertex Vv0{};
                            Vv0.X = V0.X;
                            Vv0.Y = V0.Y;
                            Vv0.Z = V0.Z;
                            Vv0.Nx = Vn0.X;
                            Vv0.Ny = Vn0.Y;
                            Vv0.Nz = Vn0.Z;
                            Vv0.Uv = T0;
                            Vv0.AuxiliaryUvs = AuxiliaryUvs0;
                            Vv0.CubeX = ShaderDirection0.X;
                            Vv0.CubeY = ShaderDirection0.Y;
                            Vv0.CubeZ = ShaderDirection0.Z;
                            Vv0.Color = Color0;

                            ViewVertex Vv1{};
                            Vv1.X = V1.X;
                            Vv1.Y = V1.Y;
                            Vv1.Z = V1.Z;
                            Vv1.Nx = Vn1.X;
                            Vv1.Ny = Vn1.Y;
                            Vv1.Nz = Vn1.Z;
                            Vv1.Uv = T1;
                            Vv1.AuxiliaryUvs = AuxiliaryUvs1;
                            Vv1.CubeX = ShaderDirection1.X;
                            Vv1.CubeY = ShaderDirection1.Y;
                            Vv1.CubeZ = ShaderDirection1.Z;
                            Vv1.Color = Color1;

                            ViewVertex Vv2{};
                            Vv2.X = V2.X;
                            Vv2.Y = V2.Y;
                            Vv2.Z = V2.Z;
                            Vv2.Nx = Vn2.X;
                            Vv2.Ny = Vn2.Y;
                            Vv2.Nz = Vn2.Z;
                            Vv2.Uv = T2;
                            Vv2.AuxiliaryUvs = AuxiliaryUvs2;
                            Vv2.CubeX = ShaderDirection2.X;
                            Vv2.CubeY = ShaderDirection2.Y;
                            Vv2.CubeZ = ShaderDirection2.Z;
                            Vv2.Color = Color2;

                            dbc::AppendClippedViewTriangle(
                                K, Vv0, Vv1, Vv2, Texture, GpuTexture, BlendTransparent, WorldTris, BlendAdditive,
                                BuildSurfaceStyle(Object, MeshValue), AlphaPhase, VeryEarly, DepthBias, -1, 0,
                                Object.DepthWrite, Object.DepthTest, AuxiliaryGpuTextures, CubeGpuTexture);
                        }
                    }
                }
                dbc::RenderObjectBounds(K, Object, WorldTris);
            });
    }

    void RenderParticles(EngineKernel& K, std::vector<Triangle>& WorldTris)
    {
        K.Objects().ForEachParticles(
            [&](int /*id*/, const ParticleEmitter& Emitter)
            {
                if (!Emitter.Visible)
                    return;

                SDL_Texture* Texture = nullptr;
                SDL_GPUTexture* GpuTexture = nullptr;
                if (Image* ParticleImage = K.ImageStoreRef().GetImage(Emitter.ImageId))
                {
                    Texture = ParticleImage->Texture;
                    GpuTexture = dbc::ImageGpuTexture(K, *ParticleImage, false);
                }

                SurfaceStyle Style;
                Style.Fog = true;
                Style.BlackFog = true;
                Style.Ambient = false;
                Style.Filter = false;
                Style.TextureMipModes[0] = 1;
                Style.AlphaOverride = Emitter.GhostPercentage >= 0.0f ? Emitter.GhostPercentage / 100.0f : -1.0f;
                const int GhostMode = Emitter.Ghost && Emitter.GhostPercentage < 0.0f && Emitter.GhostMode != 5
                                          ? std::clamp(Emitter.GhostMode < 0 ? 0 : Emitter.GhostMode, 0, 4)
                                          : -1;
                const Mat3 Rotation = Mat3FromEulerXyz(Emitter.RotX, Emitter.RotY, Emitter.RotZ);
                const float HalfSize = Emitter.Radius * 0.05f;

                for (const ParticleInstance& Particle : Emitter.Particles)
                {
                    Vec3 LocalPosition{Particle.Position.X * Emitter.Radius, Particle.Position.Y * Emitter.Radius,
                                       Particle.Position.Z * Emitter.Radius};
                    LocalPosition = Mat3Apply(LocalPosition, Rotation);
                    Vec3 WorldPosition{Emitter.PosX + LocalPosition.X, Emitter.PosY + LocalPosition.Y,
                                       Emitter.PosZ + LocalPosition.Z};
                    Vec3 WorldVelocity{Particle.Velocity.X * Emitter.Radius, Particle.Velocity.Y * Emitter.Radius,
                                       Particle.Velocity.Z * Emitter.Radius};
                    WorldVelocity = Mat3Apply(WorldVelocity, Rotation);
                    const float SpeedSquared = Vec3Dot(WorldVelocity, WorldVelocity);
                    const int Steps = SpeedSquared < 1.0f     ? 2
                                      : SpeedSquared < 4.0f   ? 3
                                      : SpeedSquared < 9.0f   ? 4
                                      : SpeedSquared < 12.25f ? 5
                                      : SpeedSquared < 16.0f  ? 6
                                      : SpeedSquared < 20.25f ? 7
                                                              : 8;
                    const Vec3 TrailStep{-WorldVelocity.X * 0.04f / static_cast<float>(Steps),
                                         -WorldVelocity.Y * 0.04f / static_cast<float>(Steps),
                                         -WorldVelocity.Z * 0.04f / static_cast<float>(Steps)};
                    SDL_FColor Color = Particle.Diffuse;

                    for (int StepIndex = 0; StepIndex < Steps; ++StepIndex)
                    {
                        const Vec3 Center = dbc::WorldToViewPoint(K, WorldPosition.X, WorldPosition.Y, WorldPosition.Z);
                        const ViewVertex TopLeft{
                            Center.X - HalfSize, Center.Y + HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {0.0f, 0.0f}, Color};
                        const ViewVertex TopRight{
                            Center.X + HalfSize, Center.Y + HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {1.0f, 0.0f}, Color};
                        const ViewVertex BottomRight{
                            Center.X + HalfSize, Center.Y - HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {1.0f, 1.0f}, Color};
                        const ViewVertex BottomLeft{
                            Center.X - HalfSize, Center.Y - HalfSize, Center.Z, 0.0f, 0.0f, 0.0f, {0.0f, 1.0f}, Color};
                        dbc::AppendClippedViewTriangle(K, TopLeft, BottomRight, TopRight, Texture, GpuTexture,
                                                       Emitter.Ghost, WorldTris, false, Style, -1, false, 0.0f,
                                                       GhostMode, 1, false);
                        dbc::AppendClippedViewTriangle(K, TopLeft, BottomLeft, BottomRight, Texture, GpuTexture,
                                                       Emitter.Ghost, WorldTris, false, Style, -1, false, 0.0f,
                                                       GhostMode, 1, false);
                        WorldPosition.X += TrailStep.X;
                        WorldPosition.Y += TrailStep.Y;
                        WorldPosition.Z += TrailStep.Z;
                    }
                }
            });
    }

    [[nodiscard]] SDL_GPUTexture* WhiteGpuTexture(EngineKernel& K)
    {
        if (K.Gpu().WhiteGpuTexture())
        {
            return K.Gpu().WhiteGpuTexture();
        }
        return GraphicsDeviceSystem::GetGpuTextureFromSdlTexture(K.Gpu().WhiteTexture());
    }

    [[nodiscard]] SDL_GPUTexture* ImageGpuTexture(EngineKernel& K, Image& ImageValue, bool TransparentBlack)
    {
        if (!ImageValue.GpuTexture && !ImageValue.SourcePath.empty())
        {
            SDL_Surface* Surface = LoadSurface(ImageValue.SourcePath);
            if (Surface)
            {
                ImageValue.GpuTexture = K.Gpu().CreateGpuTextureFromSurface(Surface);
                SDL_DestroySurface(Surface);
            }
        }
        if (TransparentBlack)
        {
            if (!ImageValue.GpuColorKeyTexture)
            {
                (void)K.ImageStoreRef().EnsureBlackKeyTexture(ImageValue);
            }
            if (ImageValue.GpuColorKeyTexture)
            {
                return ImageValue.GpuColorKeyTexture;
            }
        }
        if (ImageValue.GpuTexture)
        {
            return ImageValue.GpuTexture;
        }
        return GraphicsDeviceSystem::GetGpuTextureFromSdlTexture(ImageValue.Texture);
    }

    // The material rides in the box-colour attributes: ambient, alpha override, specular and emissive.
    void FillSurfaceMaterial(GpuVertex& Out, const SurfaceStyle& Style)
    {
        const auto Channel = [](uint32_t Packed, unsigned Shift)
        { return static_cast<float>((Packed >> Shift) & 0xFFu) / 255.0f; };
        Out.BoxTrR = Channel(Style.AmbientColor, 16u);
        Out.BoxTrG = Channel(Style.AmbientColor, 8u);
        Out.BoxTrB = Channel(Style.AmbientColor, 0u);
        Out.BoxTrA = Style.AlphaOverride >= 0.0f ? -(Style.AlphaOverride + 1.0f) : 0.0f;
        Out.BoxBrR = Channel(Style.SpecularColor, 16u);
        Out.BoxBrG = Channel(Style.SpecularColor, 8u);
        Out.BoxBrB = Channel(Style.SpecularColor, 0u);
        Out.BoxBrA = Style.SpecularPower;
        Out.BoxBlR = Channel(Style.EmissiveColor, 16u);
        Out.BoxBlG = Channel(Style.EmissiveColor, 8u);
        Out.BoxBlB = Channel(Style.EmissiveColor, 0u);
        Out.BoxBlA = Style.AlphaTestThreshold;
    }

    void AppendGpuTriangle(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                           SDL_GPUTexture* Texture, const float Pos[3][3], const float Normal[3][3],
                           const SDL_FColor Color[3], const SDL_FPoint Uv[3], bool Transparent = false,
                           bool Is2d = false, bool Additive = false, SurfaceStyle Style = {},
                           const SDL_FColor* BoxColors = nullptr, bool Sprite = false, bool AlphaDepthWrite = false,
                           int MatrixGhostMode = -1, bool DepthWrite = true, bool DepthTest = true,
                           const std::array<SDL_GPUTexture*, 7>& AuxiliaryTextures = {},
                           SDL_GPUTexture* CubeTexture = nullptr,
                           const std::array<std::array<SDL_FPoint, 3>, 7>* AuxiliaryUvs = nullptr,
                           const float (*CubeDirections)[3] = nullptr)
    {
        if (Is2d)
            DepthWrite = false;
        SDL_GPUTexture* DrawTexture = Texture ? Texture : WhiteGpuTexture(K);
        if (!DrawTexture)
        {
            return;
        }

        const Uint32 First = static_cast<Uint32>(Vertices.size());
        for (int I = 0; I < 3; ++I)
        {
            const auto Channel = [](uint32_t Packed, unsigned Shift)
            { return static_cast<float>((Packed >> Shift) & 0xFFu) / 255.0f; };
            GpuVertex Out{};
            Out.X = Pos[I][0];
            Out.Y = Pos[I][1];
            Out.Z = Pos[I][2];
            Out.Nx = Normal[I][0];
            Out.Ny = Normal[I][1];
            Out.Nz = Normal[I][2];
            Out.R = Color[I].r * Channel(Style.DiffuseColor, 16u);
            Out.G = Color[I].g * Channel(Style.DiffuseColor, 8u);
            Out.B = Color[I].b * Channel(Style.DiffuseColor, 0u);
            Out.A = Color[I].a;
            Out.U = Uv[I].x;
            Out.V = Uv[I].y;
            FillSurfaceMaterial(Out, Style);
            for (std::size_t Stage = 0; Stage < 7; ++Stage)
            {
                const SDL_FPoint StageUv = AuxiliaryUvs ? (*AuxiliaryUvs)[Stage][static_cast<std::size_t>(I)] : Uv[I];
                Out.AuxiliaryUvs[Stage][0] = StageUv.x;
                Out.AuxiliaryUvs[Stage][1] = StageUv.y;
            }
            if (CubeDirections)
            {
                Out.CubeDirection[0] = CubeDirections[I][0];
                Out.CubeDirection[1] = CubeDirections[I][1];
                Out.CubeDirection[2] = CubeDirections[I][2];
            }
            if (BoxColors)
            {
                Out.Nx = 0.0f;
                Out.Ny = 0.0f;
                Out.Nz = -1.0f;
                Out.R = BoxColors[0].r;
                Out.G = BoxColors[0].g;
                Out.B = BoxColors[0].b;
                Out.A = BoxColors[0].a;
                Out.BoxTrR = BoxColors[1].r;
                Out.BoxTrG = BoxColors[1].g;
                Out.BoxTrB = BoxColors[1].b;
                Out.BoxTrA = BoxColors[1].a;
                Out.BoxBrR = BoxColors[2].r;
                Out.BoxBrG = BoxColors[2].g;
                Out.BoxBrB = BoxColors[2].b;
                Out.BoxBrA = BoxColors[2].a;
                Out.BoxBlR = BoxColors[3].r;
                Out.BoxBlG = BoxColors[3].g;
                Out.BoxBlB = BoxColors[3].b;
                Out.BoxBlA = BoxColors[3].a;
            }
            Vertices.push_back(Out);
        }

        if (!Batches.empty() && !Batches.back().StaticVertices && Batches.back().Texture == DrawTexture &&
            Batches.back().AuxiliaryTextures == AuxiliaryTextures && Batches.back().CubeTexture == CubeTexture &&
            Batches.back().Transparent == Transparent && Batches.back().AlphaDepthWrite == AlphaDepthWrite &&
            Batches.back().Additive == Additive && Batches.back().Is2d == Is2d && Batches.back().Style == Style &&
            Batches.back().Sprite == Sprite && Batches.back().MatrixGhostMode == MatrixGhostMode &&
            Batches.back().DepthTest == DepthTest && Batches.back().DepthWrite == DepthWrite &&
            Batches.back().FirstVertex + Batches.back().VertexCount == First)
        {
            Batches.back().VertexCount += 3;
        }
        else
        {
            Batches.push_back(GpuBatch{DrawTexture, First, 3, Transparent, AlphaDepthWrite, Additive, Is2d, Sprite,
                                       MatrixGhostMode, DepthTest, DepthWrite, Style});
            Batches.back().AuxiliaryTextures = AuxiliaryTextures;
            Batches.back().CubeTexture = CubeTexture;
        }
    }

    void AppendGpuQuad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                       SDL_GPUTexture* Texture, float X, float Y, float W, float H, SDL_FColor Color, float Z = 0.0f,
                       bool Transparent = false, bool Is2d = true, SurfaceStyle Style = {})
    {
        if (W <= 0.0f || H <= 0.0f)
        {
            return;
        }
        const float PosA[3][3]{{X, Y, Z}, {X + W, Y, Z}, {X + W, Y + H, Z}};
        const float ZeroN[3][3]{};
        const SDL_FColor ColA[3]{Color, Color, Color};
        const SDL_FPoint UvA[3]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PosA, ZeroN, ColA, UvA, Transparent, Is2d, false, Style);

        const float PosB[3][3]{{X, Y, Z}, {X + W, Y + H, Z}, {X, Y + H, Z}};
        const SDL_FColor ColB[3]{Color, Color, Color};
        const SDL_FPoint UvB[3]{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PosB, ZeroN, ColB, UvB, Transparent, Is2d, false, Style);
    }

    void AppendGpuQuadUv(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                         SDL_GPUTexture* Texture, float X, float Y, float W, float H, SDL_FColor Color,
                         const SDL_FPoint& Uv0, const SDL_FPoint& Uv1, float Z = 0.0f, bool Transparent = false,
                         bool Is2d = true)
    {
        if (W <= 0.0f || H <= 0.0f)
        {
            return;
        }
        const float PosA[3][3]{{X, Y, Z}, {X + W, Y, Z}, {X + W, Y + H, Z}};
        const float ZeroN[3][3]{};
        const SDL_FColor ColA[3]{Color, Color, Color};
        const SDL_FPoint UvA[3]{{Uv0.x, Uv0.y}, {Uv1.x, Uv0.y}, {Uv1.x, Uv1.y}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PosA, ZeroN, ColA, UvA, Transparent, Is2d);

        const float PosB[3][3]{{X, Y, Z}, {X + W, Y + H, Z}, {X, Y + H, Z}};
        const SDL_FColor ColB[3]{Color, Color, Color};
        const SDL_FPoint UvB[3]{{Uv0.x, Uv0.y}, {Uv1.x, Uv1.y}, {Uv0.x, Uv1.y}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PosB, ZeroN, ColB, UvB, Transparent, Is2d);
    }

    void AppendGpuSpriteQuad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                             SDL_GPUTexture* Texture, const SpriteData& Sprite, float Width, float Height,
                             SDL_FColor Color, bool Transparent)
    {
        if (Width <= 0.0f || Height <= 0.0f)
            return;
        const float Radians = Sprite.Angle * static_cast<float>(M_PI / 180.0);
        const float Cosine = std::cos(Radians);
        const float Sine = std::sin(Radians);
        const auto Position = [&](float LocalX, float LocalY)
        {
            return std::array<float, 3>{Sprite.X + LocalX * Cosine - LocalY * Sine,
                                        Sprite.Y + LocalX * Sine + LocalY * Cosine, 0.0f};
        };
        const float Left = -static_cast<float>(Sprite.OffsetX);
        const float Top = -static_cast<float>(Sprite.OffsetY);
        const auto TopLeft = Position(Left, Top);
        const auto TopRight = Position(Left + Width, Top);
        const auto BottomRight = Position(Left + Width, Top + Height);
        const auto BottomLeft = Position(Left, Top + Height);
        const float PositionsA[3][3]{
            {TopLeft[0], TopLeft[1], 0.0f}, {TopRight[0], TopRight[1], 0.0f}, {BottomRight[0], BottomRight[1], 0.0f}};
        const float PositionsB[3][3]{{TopLeft[0], TopLeft[1], 0.0f},
                                     {BottomRight[0], BottomRight[1], 0.0f},
                                     {BottomLeft[0], BottomLeft[1], 0.0f}};
        const float Normals[3][3]{};
        const SDL_FColor Colors[3]{Color, Color, Color};
        std::array<SpriteData::TextureCoordinate, 4> Coordinates = Sprite.TextureCoordinates;
        if (Sprite.Mirrored)
        {
            std::swap(Coordinates[0], Coordinates[1]);
            std::swap(Coordinates[3], Coordinates[2]);
        }
        if (Sprite.Flipped)
        {
            std::swap(Coordinates[0], Coordinates[3]);
            std::swap(Coordinates[1], Coordinates[2]);
        }
        const SDL_FPoint UvA[3]{{Coordinates[0].U, Coordinates[0].V},
                                {Coordinates[1].U, Coordinates[1].V},
                                {Coordinates[2].U, Coordinates[2].V}};
        const SDL_FPoint UvB[3]{{Coordinates[0].U, Coordinates[0].V},
                                {Coordinates[2].U, Coordinates[2].V},
                                {Coordinates[3].U, Coordinates[3].V}};
        SurfaceStyle Style;
        if (!Sprite.Transparent)
            Style.AlphaOverride = Color.a;
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PositionsA, Normals, Colors, UvA, Transparent, true,
                               Sprite.Additive, Style, nullptr, true);
        dbc::AppendGpuTriangle(K, Vertices, Batches, Texture, PositionsB, Normals, Colors, UvB, Transparent, true,
                               Sprite.Additive, Style, nullptr, true);
    }

    void AppendGpuLine(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches, float X1,
                       float Y1, float X2, float Y2, SDL_FColor Color, float Thickness = 1.0f, float Z = 0.0f,
                       bool Transparent = false, bool Is2d = true)
    {
        const float Dx = X2 - X1;
        const float Dy = Y2 - Y1;
        const float Len = std::sqrt(Dx * Dx + Dy * Dy);
        if (Len < 0.0001f)
        {
            dbc::AppendGpuQuad(K, Vertices, Batches, WhiteGpuTexture(K), X1, Y1, Thickness, Thickness, Color, Z,
                               Transparent, Is2d);
            return;
        }

        const float HalfT = std::max(0.5f, Thickness * 0.5f);
        const float Nx = -Dy / Len * HalfT;
        const float Ny = Dx / Len * HalfT;

        const float Px0 = X1 + Nx, Py0 = Y1 + Ny;
        const float Px1 = X2 + Nx, Py1 = Y2 + Ny;
        const float Px2 = X2 - Nx, Py2 = Y2 - Ny;
        const float Px3 = X1 - Nx, Py3 = Y1 - Ny;

        const float PosA[3][3]{{Px0, Py0, Z}, {Px1, Py1, Z}, {Px2, Py2, Z}};
        const float ZeroN[3][3]{};
        const SDL_FColor ColA[3]{Color, Color, Color};
        const SDL_FPoint UvA[3]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, WhiteGpuTexture(K), PosA, ZeroN, ColA, UvA, Transparent, Is2d);

        const float PosB[3][3]{{Px0, Py0, Z}, {Px2, Py2, Z}, {Px3, Py3, Z}};
        const SDL_FColor ColB[3]{Color, Color, Color};
        const SDL_FPoint UvB[3]{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        dbc::AppendGpuTriangle(K, Vertices, Batches, WhiteGpuTexture(K), PosB, ZeroN, ColB, UvB, Transparent, Is2d);
    }

    // Copies the offscreen capture target back to the CPU and, on the way,
    // blits it to the swapchain so the user still sees the frame.
    // Takes ownership of the command buffer: it submits it.
    SDL_Surface* DownloadCapture(EngineKernel& K, GpuCommandBuffer& Commands, SDL_GPUTexture* Capture,
                                 SDL_GPUTexture* Swapchain, Uint32 Width, Uint32 Height, bool& Submitted,
                                 Uint32 DestinationWidth = 0, Uint32 DestinationHeight = 0)
    {
        Submitted = false;
        SDL_GPUCommandBuffer* Cmd = Commands.Get();
        SDL_GPUDevice* Device = K.Gpu().GpuDevice();
        if (DestinationWidth == 0)
            DestinationWidth = Width;
        if (DestinationHeight == 0)
            DestinationHeight = Height;

        SDL_GPUBlitInfo Blit{};
        Blit.source.texture = Capture;
        Blit.source.w = Width;
        Blit.source.h = Height;
        Blit.destination.texture = Swapchain;
        Blit.destination.w = DestinationWidth;
        Blit.destination.h = DestinationHeight;
        Blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        Blit.filter = SDL_GPU_FILTER_NEAREST;
        // Headless frames have no swapchain: the capture is only read back.
        if (Swapchain)
            SDL_BlitGPUTexture(Cmd, &Blit);

        SDL_GPUTransferBufferCreateInfo TransferInfo{};
        TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        TransferInfo.size = Width * Height * 4;

        SDL_GPUTransferBuffer* Transfer = SDL_CreateGPUTransferBuffer(Device, &TransferInfo);
        if (!Transfer)
        {
            DBC_LOG_ERROR("download_capture: transfer buffer failed: {}", SDL_GetError());
            Submitted = Commands.Submit();
            return nullptr;
        }

        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Cmd);
        if (!Copy)
        {
            DBC_LOG_ERROR("Capture copy pass failed: {}", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(Device, Transfer);
            Submitted = Commands.Submit();
            return nullptr;
        }
        SDL_GPUTextureRegion Region{};
        Region.texture = Capture;
        Region.w = Width;
        Region.h = Height;
        Region.d = 1;
        SDL_GPUTextureTransferInfo Destination{};
        Destination.transfer_buffer = Transfer;
        Destination.offset = 0;
        SDL_DownloadFromGPUTexture(Copy, &Region, &Destination);
        SDL_EndGPUCopyPass(Copy);

        SDL_GPUFence* Fence = Commands.SubmitAndAcquireFence();
        if (!Fence)
        {
            DBC_LOG_ERROR("download_capture: command submission failed: {}", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(Device, Transfer);
            return nullptr;
        }
        Submitted = true;
        if (!SDL_WaitForGPUFences(Device, true, &Fence, 1))
        {
            DBC_LOG_ERROR("download_capture: fence wait failed: {}", SDL_GetError());
            SDL_ReleaseGPUFence(Device, Fence);
            SDL_WaitForGPUIdle(Device);
            SDL_ReleaseGPUTransferBuffer(Device, Transfer);
            return nullptr;
        }
        SDL_ReleaseGPUFence(Device, Fence);

        // The swapchain format decides the byte order we just read back.
        const SDL_GPUTextureFormat Format = K.Gpu().GpuCaptureFormat();
        SDL_PixelFormat PixelFormat = SDL_PIXELFORMAT_RGBA32;
        if (Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || Format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB)
        {
            PixelFormat = SDL_PIXELFORMAT_BGRA32;
        }

        SDL_Surface* Surface = nullptr;
        if (void* Mapped = SDL_MapGPUTransferBuffer(Device, Transfer, false))
        {
            Surface = SDL_CreateSurface(static_cast<int>(Width), static_cast<int>(Height), PixelFormat);
            if (Surface)
            {
                const auto* Src = static_cast<const Uint8*>(Mapped);
                auto* Dst = static_cast<Uint8*>(Surface->pixels);
                for (Uint32 Row = 0; Row < Height; ++Row)
                {
                    SDL_memcpy(Dst + static_cast<size_t>(Row) * Surface->pitch,
                               Src + static_cast<size_t>(Row) * Width * 4, static_cast<size_t>(Width) * 4);
                }
            }
            SDL_UnmapGPUTransferBuffer(Device, Transfer);
        }
        else
        {
            DBC_LOG_ERROR("download_capture: transfer buffer mapping failed: {}", SDL_GetError());
        }

        SDL_ReleaseGPUTransferBuffer(Device, Transfer);
        return Surface;
    }

    void RendererSystem::ReportRenderError(const std::string& Error)
    {
        if (!RenderFailureReported_)
            DBC_LOG_ERROR("Render failed: {} ({})", Error, SDL_GetError());
        RenderFailureReported_ = true;
        Kernel_.Debug().SetLastRenderError(Error);
    }

    RenderFrameState RendererSystem::PrepareFrame(double DeltaSeconds, bool Retain2d, bool HudLast)
    {
        EngineKernel& K = Kernel_;
        PrepareFrameSimulation(DeltaSeconds);
        // Persistent bitmap text and primitives are already represented by bitmap 0.
        if (K.RuntimeCompatibility().ScreenText() == ScreenTextLifetime::PersistentBitmap)
            K.Text().ClearDebugTextLines();
        K.ImageStoreRef().ClearOverlayBlits();
        K.Draw().ClearPrimitives2D();
        K.Text().ClearOverlayTextLines();
        if (K.Debug().IsOverlayEnabled())
            K.Debug().QueueOverlayText();
        K.Text().ForEachDebugTextLine(
            [&](const DebugTextLine& Line)
            {
                if (Line.Value.empty() || !K.Text().EnsureActiveTextFont(Line.Request))
                    return;
                for (size_t I = 0; I < Line.Value.size();)
                {
                    const Uint32 Cp = DecodeUtf8Codepoint(Line.Value, I);
                    if (Cp == '\r' || Cp == '\n')
                        continue;
                    (void)K.Text().EnsureTextGlyph(Line.Request, Cp);
                }
            });
        K.Text().RefreshTextAtlasGpu();

        RenderFrameState Frame;
        Frame.Ticks = SDL_GetTicks();
        Frame.Retain2d = Retain2d;
        Frame.HudLast = HudLast;
        return Frame;
    }

    bool RendererSystem::RenderPreparedView(const RenderView& View, const GpuRenderTarget& Target,
                                            SDL_GPUCommandBuffer* Cmd, RenderFrameState& Frame)
    {
        PreparedViewGeometry Geometry;
        return BuildPreparedView(View, Geometry, Frame) &&
               UploadPreparedViews(std::span<PreparedViewGeometry>(&Geometry, 1), Cmd) &&
               RecordPreparedView(View, Target, Geometry, Cmd, Frame);
    }

    bool RendererSystem::BuildPreparedView(const RenderView& View, PreparedViewGeometry& Geometry,
                                           RenderFrameState& Frame)
    {
        EngineKernel& K = Kernel_;
        if (Frame.Finalized || !View.Valid())
            return false;
        const RenderView* PreviousView = ActiveView();
        SetActiveView(&View);

        struct ViewScope
        {
            RendererSystem& Renderer;
            const RenderView* Previous;

            ~ViewScope()
            {
                Renderer.SetActiveView(Previous);
            }
        } Scope{*this, PreviousView};

        // When fog is on and backdrop is off, DarkBASIC Classic uses the fog colour as the clear colour
        // so the horizon blends seamlessly into the sky.
        const int BaseClearColorInt =
            K.Text().ClsRequested() ? K.Text().ClsColor()
                                    : (K.Camera().BackdropEnabled() ? K.Camera().BackdropColor()
                                                                    : (K.Draw().FogOn() ? K.Draw().FogColor() : 0));
        const int ClearColorInt =
            K.Camera().ClearRequested() && !K.Camera().HasCameraView() ? K.Camera().ClearColor() : BaseClearColorInt;
        const SDL_FColor ClearColor = UnpackColor(ClearColorInt);

        auto& Tris = Geometry.World;
        auto& ReflectedTris = Geometry.Reflected;
        auto& OverlayTris = Geometry.Overlay;
        Tris.clear();
        ReflectedTris.clear();
        OverlayTris.clear();
        Vec3 ReflectionPoint{};
        Vec3 ReflectionNormal{};
        bool HasReflection = false;
        int ReflectionObjectId = std::numeric_limits<int>::max();
        K.Objects().ForEachObject(
            [&](int Id, const Object3D& Object)
            {
                if (!Object.Visible || !Object.ReflectionShading || Id >= ReflectionObjectId)
                    return;
                ReflectionObjectId = Id;
                ReflectionPoint = dbc::WorldObjectOrigin(K, Object);
                ReflectionNormal = dbc::TransformObjectSpaceVector(K, Object, Vec3{0.0f, 0.0f, 1.0f});
                HasReflection = true;
            });
        {
            // Check if the 3D scene needs rebuilding.
            bool SceneDirty = K.Camera().IsDirty() || K.Objects().AnyObjectDirty() || !K.Renderer().HasCachedTris();
            // Animated objects always require rebuild.
            if (!SceneDirty)
            {
                K.Objects().ForEachObject(
                    [&](int /*id*/, const Object3D& Obj)
                    {
                        if (Obj.Visible && (Obj.Playing || Obj.Looping))
                            SceneDirty = true;
                    });
            }

            if (SceneDirty)
            {
                Tris.reserve(8192);
                ReflectedTris.reserve(8192);
                OverlayTris.reserve(2048);
                K.Matrices().ForEachMatrix([&](int /*id*/, const Matrix& MatrixValue)
                                           { dbc::RenderMatrix(K, MatrixValue, Tris); });
                K.Matrices().ForEachTerrain(
                    [&](int /*id*/, const Matrix& Terrain)
                    {
                        if (!K.Renderer().DrawsTerrainOnGpu(Terrain))
                            dbc::RenderMatrix(K, Terrain, Tris);
                    });
                dbc::RenderBspWorld(K, Tris);
                dbc::RenderObjects(K, Tris, OverlayTris);
                dbc::RenderParticles(K, Tris);
                const auto Phase = [](const Triangle& TriangleValue)
                {
                    if (TriangleValue.VeryEarly)
                        return 0;
                    return (TriangleValue.Transparent || TriangleValue.Additive) ? 2 : 1;
                };
                std::stable_sort(Tris.begin(), Tris.end(),
                                 [&](const Triangle& A, const Triangle& B)
                                 {
                                     const int PhaseA = Phase(A);
                                     const int PhaseB = Phase(B);
                                     if (PhaseA != PhaseB)
                                         return PhaseA < PhaseB;
                                     if (A.RenderPriority != B.RenderPriority)
                                         return A.RenderPriority < B.RenderPriority;
                                     return PhaseA == 2 ? A.Depth > B.Depth : A.Depth < B.Depth;
                                 });
                if (HasReflection)
                {
                    std::vector<Triangle> IgnoredOverlay;
                    K.Renderer().SetReflectionView(ReflectionPoint, ReflectionNormal);
                    K.Matrices().ForEachMatrix([&](int /*id*/, const Matrix& MatrixValue)
                                               { dbc::RenderMatrix(K, MatrixValue, ReflectedTris); });
                    K.Matrices().ForEachTerrain([&](int /*id*/, const Matrix& Terrain)
                                                { dbc::RenderMatrix(K, Terrain, ReflectedTris); });
                    dbc::RenderBspWorld(K, ReflectedTris);
                    dbc::RenderObjects(K, ReflectedTris, IgnoredOverlay);
                    dbc::RenderParticles(K, ReflectedTris);
                    K.Renderer().ClearReflectionView();
                    ReflectedTris.erase(
                        std::remove_if(ReflectedTris.begin(), ReflectedTris.end(), [](const Triangle& TriangleValue)
                                       { return TriangleValue.Style.ReflectionSurface || TriangleValue.Is2d; }),
                        ReflectedTris.end());
                    for (Triangle& TriangleValue : ReflectedTris)
                        TriangleValue.Style.ReflectionPass = true;
                    std::stable_sort(ReflectedTris.begin(), ReflectedTris.end(),
                                     [&](const Triangle& A, const Triangle& B)
                                     {
                                         const int PhaseA = Phase(A);
                                         const int PhaseB = Phase(B);
                                         if (PhaseA != PhaseB)
                                             return PhaseA < PhaseB;
                                         if (A.RenderPriority != B.RenderPriority)
                                             return A.RenderPriority < B.RenderPriority;
                                         return PhaseA == 2 ? A.Depth > B.Depth : A.Depth < B.Depth;
                                     });
                }
                // Overlays: always back-to-front (2D compositing order).
                std::sort(OverlayTris.begin(), OverlayTris.end(),
                          [](const Triangle& A, const Triangle& B) { return A.Depth > B.Depth; });
                // Cache for next frame.
                K.Renderer().CacheTris(Tris, ReflectedTris, OverlayTris);
                K.Camera().ClearDirty();
                K.Objects().ClearAllDirtyFlags();
            }
            else
            {
                Tris = K.Renderer().CachedWorldTris();
                ReflectedTris = K.Renderer().CachedReflectedTris();
                OverlayTris = K.Renderer().CachedOverlayTris();
            }
        }

        auto& ShadowSources = Geometry.ShadowSources;
        // Only shadow casters build volumes; copying every world triangle would cost more than the volumes.
        ShadowSources.clear();
        std::copy_if(Tris.begin(), Tris.end(), std::back_inserter(ShadowSources),
                     [](const Triangle& TriangleValue) { return TriangleValue.Style.ShadowCaster; });
        dbc::AppendShadowMeshSources(K, ShadowSources);
        auto& ShadowTris = Geometry.Shadows;
        ShadowTris.clear();
        ShadowTris.reserve(ShadowSources.size() * 4);
        dbc::BuildShadowVolumes(K, ShadowSources, ShadowTris);
        const bool HasShadow = !ShadowTris.empty();

        auto& Vertices = Geometry.Vertices;
        auto& Batches = Geometry.Batches;
        Vertices.clear();
        Batches.clear();
        {
            Vertices.reserve(static_cast<size_t>(
                (Tris.size() + ReflectedTris.size() + ShadowTris.size() + OverlayTris.size()) * 3 + 8192));
            Batches.reserve(2048);

            // A camera that does not clear the whole target fills its viewport first; the scissor bounds the quad.
            SurfaceStyle ViewFillStyle{};
            ViewFillStyle.Fog = false;
            ViewFillStyle.Ambient = false;
            ViewFillStyle.Backdrop = true;
            const auto FillView = [&](int Color)
            {
                dbc::AppendGpuQuad(K, Vertices, Batches, WhiteGpuTexture(K), 0.0f, 0.0f,
                                   static_cast<float>(View.LogicalWidth), static_cast<float>(View.LogicalHeight),
                                   UnpackColor(Color), 1.0f, false, true, ViewFillStyle);
            };
            if (K.Camera().ClearRequested() && (K.Camera().HasCameraView() || Geometry.OverPreviousCamera))
                FillView(K.Camera().ClearColor());
            else if (Geometry.OverPreviousCamera && (K.Camera().BackdropEnabled() || K.Draw().FogOn()))
                FillView(K.Camera().BackdropEnabled() ? K.Camera().BackdropColor() : K.Draw().FogColor());

            if (K.Camera().BackdropEnabled() && K.Draw().BackdropImageId() > 0)
            {
                Image* BgImg = K.ImageStoreRef().GetImage(K.Draw().BackdropImageId());
                if (BgImg && (BgImg->Texture || BgImg->GpuTexture) && BgImg->Width > 0 && BgImg->Height > 0)
                {
                    SDL_GPUTexture* BgTexture = dbc::ImageGpuTexture(K, *BgImg, false);
                    if (BgTexture)
                    {
                        SurfaceStyle BackdropStyle{};
                        BackdropStyle.Fog = false;
                        BackdropStyle.Ambient = false;
                        BackdropStyle.Backdrop = true;
                        const float Tw = static_cast<float>(BgImg->Width);
                        const float Th = static_cast<float>(BgImg->Height);
                        float Ox = std::fmod(K.Draw().BackdropScrollX() * Tw / 100.0f, Tw);
                        float Oy = std::fmod(K.Draw().BackdropScrollY() * Th / 100.0f, Th);
                        if (Ox < 0.0f)
                        {
                            Ox += Tw;
                        }
                        if (Oy < 0.0f)
                        {
                            Oy += Th;
                        }
                        for (float Y = -Oy; Y < static_cast<float>(View.LogicalHeight); Y += Th)
                        {
                            for (float X = -Ox; X < static_cast<float>(View.LogicalWidth); X += Tw)
                            {
                                dbc::AppendGpuQuad(K, Vertices, Batches, BgTexture, X, Y, Tw, Th,
                                                   SDL_FColor{1, 1, 1, 1}, 1.0f, false, true, BackdropStyle);
                            }
                        }
                    }
                }
            }

            // Cache the logical HUD once, then replay it in both XR eyes.
            const auto AppendBitmap = [&](std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
            {
                SDL_GPUTexture* Bmp0Tex = K.ImageStoreRef().GetBitmapGpuTexture(0);
                if (Bmp0Tex)
                {
                    dbc::AppendGpuQuad(K, HudVertices, HudBatches, Bmp0Tex, 0.0f, 0.0f,
                                       static_cast<float>(View.LogicalWidth), static_cast<float>(View.LogicalHeight),
                                       SDL_FColor{1, 1, 1, 1}, 0.0f, true);
                }
            };
            if (!Frame.HudLast && Geometry.Hud)
                AppendBitmap(Vertices, Batches);

            // DRAW TO BACK: 2D overlay goes before 3D world.
            // DRAW TO FRONT (default): 3D world goes first, 2D on top.
            const auto AppendOverlay = [&](std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
            {
                for (const Triangle& Tri : OverlayTris)
                {
                    dbc::AppendGpuTriangle(K, HudVertices, HudBatches, Tri.GpuTexture, Tri.Pos, Tri.Normal, Tri.Color,
                                           Tri.Uv, Tri.Transparent, Tri.Is2d, Tri.Additive, Tri.Style, nullptr, false,
                                           Tri.AlphaDepthWrite, Tri.MatrixGhostMode, Tri.DepthWrite, Tri.DepthTest,
                                           Tri.AuxiliaryGpuTextures, Tri.CubeGpuTexture, &Tri.AuxiliaryUvs,
                                           Tri.CubeDirection);
                }
            };

            if (!(Frame.HudLast || K.Draw().IsDrawToFront()) && Geometry.Hud)
                AppendOverlay(Vertices, Batches);

            for (const Triangle& Tri : Tris)
            {
                dbc::AppendGpuTriangle(K, Vertices, Batches, Tri.GpuTexture, Tri.Pos, Tri.Normal, Tri.Color, Tri.Uv,
                                       Tri.Transparent, Tri.Is2d, Tri.Additive, Tri.Style, nullptr, false,
                                       Tri.AlphaDepthWrite, Tri.MatrixGhostMode, Tri.DepthWrite, Tri.DepthTest,
                                       Tri.AuxiliaryGpuTextures, Tri.CubeGpuTexture, &Tri.AuxiliaryUvs,
                                       Tri.CubeDirection);
            }
            AppendStaticTerrains(View, Geometry);

            for (const Triangle& Tri : ReflectedTris)
            {
                dbc::AppendGpuTriangle(K, Vertices, Batches, Tri.GpuTexture, Tri.Pos, Tri.Normal, Tri.Color, Tri.Uv,
                                       Tri.Transparent, Tri.Is2d, Tri.Additive, Tri.Style, nullptr, false,
                                       Tri.AlphaDepthWrite, Tri.MatrixGhostMode, Tri.DepthWrite, Tri.DepthTest,
                                       Tri.AuxiliaryGpuTextures, Tri.CubeGpuTexture, &Tri.AuxiliaryUvs,
                                       Tri.CubeDirection);
            }

            for (const Triangle& Tri : ShadowTris)
            {
                dbc::AppendGpuTriangle(K, Vertices, Batches, Tri.GpuTexture, Tri.Pos, Tri.Normal, Tri.Color, Tri.Uv,
                                       false, false, false, Tri.Style, nullptr, false, false, -1, false, true);
            }

            if (HasReflection)
            {
                SurfaceStyle OverlayStyle{};
                OverlayStyle.Fog = false;
                OverlayStyle.Ambient = false;
                OverlayStyle.ReflectionOverlay = true;
                dbc::AppendGpuQuad(K, Vertices, Batches, WhiteGpuTexture(K), 0.0f, 0.0f,
                                   static_cast<float>(View.LogicalWidth), static_cast<float>(View.LogicalHeight),
                                   SDL_FColor{0.0f, 0.0f, 0.0f, 64.0f / 255.0f}, 0.0f, true, true, OverlayStyle);
            }

            if (HasShadow)
            {
                SurfaceStyle OverlayStyle{};
                OverlayStyle.Fog = false;
                OverlayStyle.Ambient = false;
                OverlayStyle.ShadowOverlay = true;
                dbc::AppendGpuQuad(K, Vertices, Batches, WhiteGpuTexture(K), 0.0f, 0.0f,
                                   static_cast<float>(View.LogicalWidth), static_cast<float>(View.LogicalHeight),
                                   SDL_FColor{0.0f, 0.0f, 0.0f, 64.0f / 255.0f}, 0.0f, true, true, OverlayStyle);
            }

            const auto AppendHud = [&](std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
            {
                if (Frame.HudLast)
                    AppendBitmap(HudVertices, HudBatches);
                if ((Frame.HudLast || K.Draw().IsDrawToFront()))
                    AppendOverlay(HudVertices, HudBatches);

                K.Draw().ForEachPrimitive2D(
                    [&](const Primitive2D& P)
                    {
                        const SDL_FColor C = UnpackColor(P.Color);
                        if (P.Type == Primitive2DType::Dot)
                        {
                            dbc::AppendGpuQuad(K, HudVertices, HudBatches, WhiteGpuTexture(K), static_cast<float>(P.X1),
                                               static_cast<float>(P.Y1), 1.0f, 1.0f, C);
                        }
                        else if (P.Type == Primitive2DType::Line)
                        {
                            dbc::AppendGpuLine(K, HudVertices, HudBatches, static_cast<float>(P.X1),
                                               static_cast<float>(P.Y1), static_cast<float>(P.X2),
                                               static_cast<float>(P.Y2), C, 1.0f);
                        }
                        else if (P.Type == Primitive2DType::Box)
                        {
                            // Filled, inclusive of its right and bottom edge, shaded between the corner colours.
                            const float L = static_cast<float>(std::min(P.X1, P.X2));
                            const float T = static_cast<float>(std::min(P.Y1, P.Y2));
                            const float R = static_cast<float>(std::max(P.X1, P.X2)) + 1.0f;
                            const float B = static_cast<float>(std::max(P.Y1, P.Y2)) + 1.0f;
                            const SDL_FColor Tl = UnpackColor(P.CornerColors[0]);
                            const SDL_FColor Tr = UnpackColor(P.CornerColors[1]);
                            const SDL_FColor Br = UnpackColor(P.CornerColors[2]);
                            const SDL_FColor Bl = UnpackColor(P.CornerColors[3]);
                            const SDL_FColor Corners[4]{Tl, Tr, Br, Bl};
                            const float ZeroN[3][3]{};
                            const float PosA[3][3]{{L, T, 0.0f}, {R, T, 0.0f}, {R, B, 0.0f}};
                            const SDL_FColor ColA[3]{Tl, Tl, Tl};
                            const SDL_FPoint UvA[3]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
                            dbc::AppendGpuTriangle(K, HudVertices, HudBatches, WhiteGpuTexture(K), PosA, ZeroN, ColA,
                                                   UvA, false, true, false, {}, Corners);
                            const float PosB[3][3]{{L, T, 0.0f}, {R, B, 0.0f}, {L, B, 0.0f}};
                            const SDL_FColor ColB[3]{Tl, Tl, Tl};
                            const SDL_FPoint UvB[3]{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
                            dbc::AppendGpuTriangle(K, HudVertices, HudBatches, WhiteGpuTexture(K), PosB, ZeroN, ColB,
                                                   UvB, false, true, false, {}, Corners);
                        }
                        else if (P.Type == Primitive2DType::Circle || P.Type == Primitive2DType::Ellipse)
                        {
                            const float Cx = static_cast<float>(P.X1);
                            const float Cy = static_cast<float>(P.Y1);
                            const float Rx = std::max(1.0f, std::abs(static_cast<float>(P.X2)));
                            const float Ry = std::max(1.0f, std::abs(static_cast<float>(P.Y2)));
                            constexpr int KSegs = 48;
                            float Px = Cx + Rx;
                            float Py = Cy;
                            for (int I = 1; I <= KSegs; ++I)
                            {
                                const float A = static_cast<float>(I) * static_cast<float>(2.0 * M_PI / KSegs);
                                const float Nx = Cx + std::cos(A) * Rx;
                                const float Ny = Cy + std::sin(A) * Ry;
                                dbc::AppendGpuLine(K, HudVertices, HudBatches, Px, Py, Nx, Ny, C, 1.0f);
                                Px = Nx;
                                Py = Ny;
                            }
                        }
                    });

                K.Text().ForEachDebugTextLine(
                    [&](const DebugTextLine& Line)
                    {
                        if (Line.Value.empty())
                            return;

                        int TextW = 0;
                        int TextH = 0;
                        if (Line.Request.Opaque && K.Text().MeasureText(Line.Request, Line.Value, TextW, TextH))
                        {
                            dbc::AppendGpuQuad(K, HudVertices, HudBatches, WhiteGpuTexture(K), Line.X, Line.Y,
                                               static_cast<float>(TextW), static_cast<float>(TextH),
                                               UnpackColor(K.Draw().BackdropColor()), 0.0f, false, true);
                        }

                        if (!K.Text().EnsureActiveTextFont(Line.Request))
                            return;
                        const int LineSkip = std::max(1, TTF_GetFontLineSkip(K.Text().ActiveTextFont()));
                        const SDL_FColor GlyphColor = UnpackColor(Line.Color);

                        float PenX = Line.X;
                        float PenY = Line.Y;
                        Uint32 PrevCp = 0;
                        bool HasPrev = false;
                        for (size_t I = 0; I < Line.Value.size();)
                        {
                            const Uint32 Cp = DecodeUtf8Codepoint(Line.Value, I);
                            if (Cp == '\r')
                                continue;
                            if (Cp == '\n')
                            {
                                PenX = Line.X;
                                PenY += static_cast<float>(LineSkip);
                                HasPrev = false;
                                PrevCp = 0;
                                continue;
                            }

                            TextGlyph* Glyph = K.Text().FindTextGlyph(Line.Request, Cp);
                            if (!Glyph)
                            {
                                HasPrev = false;
                                PrevCp = 0;
                                continue;
                            }

                            int Kerning = 0;
                            if (HasPrev)
                            {
                                (void)TTF_GetGlyphKerning(K.Text().ActiveTextFont(), PrevCp, Cp, &Kerning);
                            }

                            if (Glyph->Width > 0 && Glyph->Height > 0)
                            {
                                if (K.Text().Atlas().GpuTexture)
                                {
                                    const float DrawX = PenX + static_cast<float>(Glyph->Minx + Kerning);
                                    // SDL_ttf already places every rendered glyph surface on its line origin.
                                    // Applying ascent/maxy here a second time makes characters jump vertically.
                                    const float DrawY = PenY;
                                    dbc::AppendGpuQuadUv(K, HudVertices, HudBatches, K.Text().Atlas().GpuTexture, DrawX,
                                                         DrawY, static_cast<float>(Glyph->Width),
                                                         static_cast<float>(Glyph->Height), GlyphColor,
                                                         SDL_FPoint{Glyph->U0, Glyph->V0},
                                                         SDL_FPoint{Glyph->U1, Glyph->V1}, 0.0f, true, true);
                                }
                            }

                            PenX += static_cast<float>(Glyph->Advance + Kerning);
                            PrevCp = Cp;
                            HasPrev = true;
                        }
                    });

                K.ImageStoreRef().ForEachOverlayBlit(
                    [&](const OverlayImageBlit& Blit)
                    {
                        Image* BlitImg = K.ImageStoreRef().GetImage(Blit.ImageId);
                        if (!BlitImg || (!BlitImg->Texture && !BlitImg->GpuTexture))
                            return;
                        SDL_GPUTexture* Tex = dbc::ImageGpuTexture(K, *BlitImg, Blit.TransparentBlack);
                        if (!Tex)
                            return;
                        dbc::AppendGpuQuad(K, HudVertices, HudBatches, Tex, Blit.X, Blit.Y,
                                           static_cast<float>(BlitImg->Width), static_cast<float>(BlitImg->Height),
                                           SDL_FColor{1, 1, 1, 1}, 0.0f, Blit.TransparentBlack || BlitImg->HasAlpha);
                    });

                const auto AppendSprite = [&](const SpriteData& Spr)
                {
                    Image* SprImg = K.ImageStoreRef().GetImage(Spr.ImageId);
                    if (!SprImg || (!SprImg->Texture && !SprImg->GpuTexture))
                        return;
                    SDL_GPUTexture* Tex = dbc::ImageGpuTexture(K, *SprImg, false);
                    if (!Tex)
                        return;
                    float W = Spr.CustomSize
                                  ? static_cast<float>(Spr.SizeX)
                                  : static_cast<float>(Spr.AnimatedAtlas
                                                           ? Spr.FrameWidth
                                                           : (Spr.ImageWidth > 0 ? Spr.ImageWidth : SprImg->Width)) *
                                        Spr.ScaleX / 100.0f;
                    float H = Spr.CustomSize
                                  ? static_cast<float>(Spr.SizeY)
                                  : static_cast<float>(Spr.AnimatedAtlas
                                                           ? Spr.FrameHeight
                                                           : (Spr.ImageHeight > 0 ? Spr.ImageHeight : SprImg->Height)) *
                                        Spr.ScaleY / 100.0f;
                    SpriteData Positioned = Spr;
                    Positioned.X = Spr.X * K.Sprites().SpriteGlobalScaleX() + K.Sprites().SpriteGlobalOffsetX();
                    const float SpriteScaleX = Spr.CustomSize ? 1.0f : Spr.ScaleX / 100.0f;
                    const float SpriteScaleY = Spr.CustomSize ? 1.0f : Spr.ScaleY / 100.0f;
                    Positioned.OffsetX = static_cast<int>(static_cast<float>(Spr.OffsetX) * SpriteScaleX *
                                                          K.Sprites().SpriteGlobalScaleX());
                    Positioned.OffsetY = static_cast<int>(static_cast<float>(Spr.OffsetY) * SpriteScaleY);
                    W *= K.Sprites().SpriteGlobalScaleX();
                    const SDL_FColor Color{
                        std::clamp(Spr.DiffuseR / 255.0f, 0.0f, 1.0f), std::clamp(Spr.DiffuseG / 255.0f, 0.0f, 1.0f),
                        std::clamp(Spr.DiffuseB / 255.0f, 0.0f, 1.0f), std::clamp(Spr.Alpha / 255.0f, 0.0f, 1.0f)};
                    const bool Transparent = Color.a < 0.999f || (Spr.Transparent && SprImg->HasAlpha);
                    dbc::AppendGpuSpriteQuad(K, HudVertices, HudBatches, Tex, Positioned, W, H, Color, Transparent);
                };
                K.Sprites().ForEachVisibleSprite(AppendSprite);
                K.Sprites().ForEachPastedSprite(AppendSprite);
            };
            if (Geometry.Hud && Frame.HudLast)
            {
                if (!Frame.HudPrepared)
                {
                    FrameHud_.Vertices.clear();
                    FrameHud_.Batches.clear();
                    AppendHud(FrameHud_.Vertices, FrameHud_.Batches);
                    Frame.HudPrepared = true;
                }
                const auto FirstVertex = static_cast<Uint32>(Vertices.size());
                Vertices.insert(Vertices.end(), FrameHud_.Vertices.begin(), FrameHud_.Vertices.end());
                for (auto Batch : FrameHud_.Batches)
                {
                    Batch.FirstVertex += FirstVertex;
                    Batches.push_back(Batch);
                }
            }
            else if (Geometry.Hud)
                AppendHud(Vertices, Batches);
        }

        Geometry.ClearColor = ClearColor;
        Geometry.HasReflection = HasReflection;
        Geometry.HasShadow = HasShadow;
        return true;
    }

    bool RendererSystem::UploadPreparedViews(std::span<PreparedViewGeometry> Geometry, SDL_GPUCommandBuffer* Cmd)
    {
        if (!UploadStaticTerrains(Geometry, Cmd))
            return false;
        auto& Gpu = Kernel_.Gpu();
        std::size_t Count = 0;
        for (auto& View : Geometry)
        {
            if (View.Vertices.size() > std::numeric_limits<Uint32>::max() / sizeof(GpuVertex) - Count)
            {
                ReportRenderError("combined view geometry exceeds the GPU buffer limit");
                return false;
            }
            View.VertexOffset = static_cast<Uint32>(Count * sizeof(GpuVertex));
            Count += View.Vertices.size();
        }
        if (!Count)
            return true;
        if (!Cmd || !Gpu.EnsureGpuVertexCapacity(static_cast<Uint32>(Count)))
            return false;
        auto* Mapped =
            static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Gpu.GpuDevice(), Gpu.GpuTransferBuffer(), true));
        if (!Mapped)
            return false;
        for (const auto& View : Geometry)
            if (!View.Vertices.empty())
                SDL_memcpy(Mapped + View.VertexOffset, View.Vertices.data(), View.Vertices.size() * sizeof(GpuVertex));
        SDL_UnmapGPUTransferBuffer(Gpu.GpuDevice(), Gpu.GpuTransferBuffer());
        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Cmd);
        if (!Copy)
            return false;
        SDL_GPUTransferBufferLocation Source{Gpu.GpuTransferBuffer(), 0};
        SDL_GPUBufferRegion Destination{Gpu.GpuVertexBuffer(), 0, static_cast<Uint32>(Count * sizeof(GpuVertex))};
        SDL_UploadToGPUBuffer(Copy, &Source, &Destination, true);
        SDL_EndGPUCopyPass(Copy);
        return true;
    }

    bool RendererSystem::RecordPreparedView(const RenderView& View, const GpuRenderTarget& Target,
                                            const PreparedViewGeometry& Geometry, SDL_GPUCommandBuffer* Cmd,
                                            RenderFrameState& Frame)
    {
        EngineKernel& K = Kernel_;
        if (Frame.Finalized || !View.Valid() || !K.Gpu().GpuDevice() || !K.Gpu().GpuPipeline() ||
            !K.Gpu().GpuSampler() || !Cmd || !Target.Color || !Target.Depth || Target.Width != View.PhysicalWidth ||
            Target.Height != View.PhysicalHeight || Target.ColorFormat != K.Gpu().GpuColorFormat() ||
            Target.DepthFormat != K.Gpu().GpuDepthFormat())
        {
            ReportRenderError("invalid or incompatible borrowed render target");
            Frame.HasErrors = true;
            return false;
        }
        const RenderView* PreviousView = ActiveView();
        SetActiveView(&View);

        struct ViewScope
        {
            RendererSystem& Renderer;
            const RenderView* Previous;

            ~ViewScope()
            {
                Renderer.SetActiveView(Previous);
            }
        } Scope{*this, PreviousView};

        const auto& Vertices = Geometry.Vertices;
        const auto& Batches = Geometry.Batches;
        const auto ClearColor = Geometry.ClearColor;
        const bool HasReflection = Geometry.HasReflection;
        const bool HasShadow = Geometry.HasShadow;
        SDL_GPUColorTargetInfo ColorTarget{};
        ColorTarget.texture = Target.Color;
        ColorTarget.clear_color = ClearColor;
        ColorTarget.load_op = Geometry.OverPreviousCamera ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        ColorTarget.store_op = SDL_GPU_STOREOP_STORE;
        ColorTarget.mip_level = 0;
        ColorTarget.layer_or_depth_plane = Target.ColorLayer;
        ColorTarget.resolve_texture = nullptr;
        ColorTarget.resolve_mip_level = 0;
        ColorTarget.resolve_layer = 0;
        ColorTarget.cycle = false;
        ColorTarget.cycle_resolve_texture = false;

        const glm::mat4& ProjPersp = View.Projection;
        const glm::mat4& ProjPerspAdditive = View.AdditiveProjection;
        const glm::mat4 ProjOrtho = glm::ortho(0.0f, static_cast<float>(View.LogicalWidth),
                                               static_cast<float>(View.LogicalHeight), 0.0f, 0.0f, 1.0f);

        GpuVertexUniforms PerspUbo{}, PerspUboAdditive{}, OrthoUbo{};
        memcpy(PerspUbo.ViewProj, &ProjPersp[0][0], 64);
        memcpy(PerspUboAdditive.ViewProj, &ProjPerspAdditive[0][0], 64);
        memcpy(OrthoUbo.ViewProj, &ProjOrtho[0][0], 64);

        SDL_PushGPUVertexUniformData(Cmd, 0, &PerspUbo, sizeof(GpuVertexUniforms));
        // Slot 1 is the model-view matrix: identity for view-space vertices, the view for static terrain.
        const glm::mat4 IdentityModelView(1.0f);
        SDL_PushGPUVertexUniformData(Cmd, 1, &IdentityModelView[0][0], sizeof(glm::mat4));

        GpuFragmentUniforms FragUbo = dbc::BuildFragmentUniforms(K);
        GpuFragmentUniforms FragUbo2d = FragUbo;
        FragUbo2d.Flags[1] = 0.0f;
        SDL_PushGPUFragmentUniformData(Cmd, 0, &FragUbo, sizeof(GpuFragmentUniforms));

        SDL_GPUDepthStencilTargetInfo DepthTarget{};
        DepthTarget.texture = Target.Depth;
        DepthTarget.clear_depth = 1.0f;
        DepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        DepthTarget.store_op = HasShadow ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        DepthTarget.stencil_load_op = HasReflection ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_DONT_CARE;
        DepthTarget.stencil_store_op = HasReflection ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        DepthTarget.cycle = false;
        DepthTarget.clear_stencil = 0;
        DepthTarget.mip_level = 0;
        DepthTarget.layer = static_cast<Uint8>(Target.DepthLayer);

        bool FailedEffectBatches = false;
        for (const GpuBatch& Batch : Batches)
        {
            if (Batch.Style.EffectId != 0 &&
                !K.Gpu().EffectPipeline(Batch.Style.EffectId, Batch.Transparent, Batch.AlphaDepthWrite, Batch.Additive,
                                        Batch.Style.Wireframe, Batch.DepthTest, Batch.DepthWrite,
                                        Batch.Style.ReflectionPass))
            {
                FailedEffectBatches = true;
            }
        }

        SDL_GPURenderPass* Pass = SDL_BeginGPURenderPass(Cmd, &ColorTarget, 1, &DepthTarget);
        if (!Pass)
        {
            K.Renderer().ReportRenderError("SDL_BeginGPURenderPass failed");
            Frame.HasErrors = true;
            return false;
        }

        const auto ApplyCameraViewport = [&](SDL_GPURenderPass* RenderPass)
        {
            const SDL_GPUViewport Viewport = View.PhysicalViewport();
            const SDL_Rect Scissor = View.PhysicalScissor();
            SDL_SetGPUViewport(RenderPass, &Viewport);
            SDL_SetGPUScissor(RenderPass, &Scissor);
        };
        ApplyCameraViewport(Pass);
        if (HasReflection)
            SDL_SetGPUStencilReference(Pass, 1);

        // The Metal backend dereferences the texture and the sampler it is handed
        // without checking either, so a batch that has no texture is dropped
        // rather than bound, and a missing sampler cancels the textured passes.
        bool SkippedUntexturedBatches = false;
        SDL_GPUSampler* const Sampler = K.Gpu().GpuSampler();
        // Static terrain can be the only 3D content, with no per-frame vertices at all.
        const bool HasGeometry = !Vertices.empty() || !Geometry.TerrainIndices.empty();
        if (!Sampler && HasGeometry)
        {
            K.Renderer().ReportRenderError("no GPU sampler");
        }
        if (HasGeometry && Sampler)
        {
            SDL_GPUBufferBinding VertexBinding{};
            VertexBinding.buffer = K.Gpu().GpuVertexBuffer();
            VertexBinding.offset = Geometry.VertexOffset;
            const auto BindSharedVertices = [&](SDL_GPURenderPass* RenderPass)
            {
                if (!Vertices.empty() && VertexBinding.buffer)
                    SDL_BindGPUVertexBuffers(RenderPass, 0, &VertexBinding, 1);
            };
            BindSharedVertices(Pass);

            // One batch loop serves the three passes. A batch's SurfaceStyle picks the line-fill
            // pipeline variant, the point sampler, and a uniform copy without fog or ambient.
            const auto DrawBatches = [&](SDL_GPUGraphicsPipeline* FillPipeline, SDL_GPUGraphicsPipeline* WirePipeline,
                                         const GpuVertexUniforms& WorldUbo, auto&& Selects, bool ForcePipeline = false)
            {
                if (!FillPipeline)
                    return;
                if (!WirePipeline)
                    WirePipeline = FillPipeline;
                bool CurrentIs2d = false;
                SurfaceStyle CurrentStyle{};
                SDL_GPUGraphicsPipeline* BoundPipeline = nullptr;
                int BoundUniformEffect = 0;
                const GpuVertexUniforms* CurrentVertexUniforms = &WorldUbo;
                GpuFragmentUniforms CurrentFragmentUniforms = FragUbo;
                SDL_PushGPUVertexUniformData(Cmd, 0, &WorldUbo, sizeof(GpuVertexUniforms));
                SDL_PushGPUFragmentUniformData(Cmd, 0, &FragUbo, sizeof(GpuFragmentUniforms));
                for (const GpuBatch& Batch : Batches)
                {
                    if (!Selects(Batch))
                        continue;
                    const bool MaskBatch = Batch.Style.ReflectionSurface && !Batch.Style.ReflectionPass;
                    const bool ShadowVolume = Batch.Style.ShadowVolume;
                    const bool ShadowOverlay = Batch.Style.ShadowOverlay;
                    const bool StencilTest = Batch.Style.ReflectionPass || Batch.Style.ReflectionOverlay;
                    const int BatchEffect =
                        MaskBatch || ShadowVolume || ShadowOverlay || ForcePipeline ? 0 : Batch.Style.EffectId;
                    bool StandardUniformsChanged = false;
                    if (Batch.Is2d != CurrentIs2d)
                    {
                        CurrentIs2d = Batch.Is2d;
                        CurrentVertexUniforms = CurrentIs2d ? &OrthoUbo : &WorldUbo;
                        SDL_PushGPUVertexUniformData(Cmd, 0, CurrentVertexUniforms, sizeof(GpuVertexUniforms));
                        CurrentStyle = {};
                        CurrentFragmentUniforms = CurrentIs2d ? FragUbo2d : FragUbo;
                        SDL_PushGPUFragmentUniformData(Cmd, 0, &CurrentFragmentUniforms,
                                                       sizeof(CurrentFragmentUniforms));
                        StandardUniformsChanged = true;
                    }
                    const bool UniformsDiffer =
                        Batch.Style.Fog != CurrentStyle.Fog || Batch.Style.BlackFog != CurrentStyle.BlackFog ||
                        Batch.Style.Ambient != CurrentStyle.Ambient || Batch.Style.EffectId != CurrentStyle.EffectId;
                    if (UniformsDiffer)
                    {
                        CurrentStyle = Batch.Style;
                        CurrentFragmentUniforms = CurrentIs2d ? FragUbo2d : FragUbo;
                        if (!Batch.Style.Fog)
                            CurrentFragmentUniforms.Flags[1] = 0.0f;
                        if (Batch.Style.BlackFog)
                        {
                            CurrentFragmentUniforms.FogColorDist[0] = 0.0f;
                            CurrentFragmentUniforms.FogColorDist[1] = 0.0f;
                            CurrentFragmentUniforms.FogColorDist[2] = 0.0f;
                        }
                        if (!Batch.Style.Ambient)
                            CurrentFragmentUniforms.AmbientColor[3] = 0.0f;
                        SDL_PushGPUFragmentUniformData(Cmd, 0, &CurrentFragmentUniforms,
                                                       sizeof(CurrentFragmentUniforms));
                        StandardUniformsChanged = true;
                    }
                    SDL_GPUGraphicsPipeline* Pipeline = Batch.Style.Wireframe ? WirePipeline : FillPipeline;
                    if (ForcePipeline)
                    {
                        Pipeline = FillPipeline;
                    }
                    else if (MaskBatch)
                    {
                        Pipeline = K.Gpu().ReflectionMaskPipeline();
                    }
                    else if (ShadowVolume)
                    {
                        Pipeline = K.Gpu().ShadowVolumePipeline();
                    }
                    else if (ShadowOverlay)
                    {
                        Pipeline = K.Gpu().ShadowOverlayPipeline();
                    }
                    else if (Batch.MatrixGhostMode >= 0)
                    {
                        Pipeline = K.Gpu().SurfaceGhostPipeline(Batch.MatrixGhostMode, Batch.Style.Wireframe,
                                                                Batch.DepthWrite, Batch.DepthTest, StencilTest);
                        if (!Pipeline)
                            continue;
                    }
                    else if (Batch.Style.EffectId != 0)
                    {
                        Pipeline = K.Gpu().EffectPipeline(Batch.Style.EffectId, Batch.Transparent,
                                                          Batch.AlphaDepthWrite, Batch.Additive, Batch.Style.Wireframe,
                                                          Batch.DepthTest, Batch.DepthWrite, StencilTest);
                        if (!Pipeline)
                            continue;
                    }
                    else
                    {
                        Pipeline = K.Gpu().SurfacePipeline(Batch.Transparent, Batch.AlphaDepthWrite, Batch.Additive,
                                                           Batch.Style.Wireframe, Batch.DepthTest, Batch.DepthWrite,
                                                           StencilTest);
                    }
                    if (Batch.StaticVertices)
                        Pipeline = ForcePipeline ? K.Gpu().TerrainDepthPipeline()
                                                 : K.Gpu().TerrainPipeline(Batch.Style.Wireframe);
                    if (!Pipeline)
                        continue;
                    if (Pipeline != BoundPipeline)
                    {
                        BoundPipeline = Pipeline;
                        SDL_BindGPUGraphicsPipeline(Pass, Pipeline);
                    }
                    if (BatchEffect != 0 && (BatchEffect != BoundUniformEffect || StandardUniformsChanged))
                    {
                        K.Gpu().PushEffectUniformData(Cmd, BatchEffect, *CurrentVertexUniforms,
                                                      CurrentFragmentUniforms);
                    }
                    else if (BatchEffect == 0 && BoundUniformEffect != 0)
                    {
                        SDL_PushGPUVertexUniformData(Cmd, 0, CurrentVertexUniforms, sizeof(GpuVertexUniforms));
                        SDL_PushGPUFragmentUniformData(Cmd, 0, &CurrentFragmentUniforms,
                                                       sizeof(CurrentFragmentUniforms));
                    }
                    BoundUniformEffect = BatchEffect;

                    if (BatchEffect == 0)
                    {
                        GpuTextureStageUniforms StageUniforms{};
                        for (std::size_t Stage = 0; Stage < Batch.Style.TextureBlendModes.size(); ++Stage)
                        {
                            StageUniforms.Stages[Stage][0] = Batch.Style.TextureBlendModes[Stage];
                            StageUniforms.Stages[Stage][1] = Batch.Style.TextureCubeModes[Stage] ? 1 : 0;
                        }
                        StageUniforms.Stages[0][2] = Batch.Style.BuiltInEffect;
                        SDL_PushGPUFragmentUniformData(Cmd, 1, &StageUniforms, sizeof(StageUniforms));
                    }

                    const Uint32 SamplerCount =
                        MaskBatch && !ForcePipeline
                            ? 0
                            : (BatchEffect == 0 ? 9 : K.Gpu().EffectFragmentSamplerCount(BatchEffect));
                    if (SamplerCount != 0)
                    {
                        SDL_GPUTexture* Texture = Batch.Texture;
                        if ((ShadowVolume || ShadowOverlay || ForcePipeline) && !Texture)
                            Texture = WhiteGpuTexture(K);
                        if (BatchEffect != 0 && !K.Gpu().EffectUsesObjectTexture(BatchEffect))
                            Texture = WhiteGpuTexture(K);
                        if (!Texture)
                        {
                            SkippedUntexturedBatches = true;
                            continue;
                        }
                        if (BatchEffect == 0)
                        {
                            SDL_GPUTextureSamplerBinding Bindings[9]{};
                            for (std::size_t Stage = 0; Stage < 8; ++Stage)
                            {
                                Bindings[Stage].texture = Stage == 0 ? Texture
                                                                     : (Batch.AuxiliaryTextures[Stage - 1]
                                                                            ? Batch.AuxiliaryTextures[Stage - 1]
                                                                            : WhiteGpuTexture(K));
                                const int AddressMode = Batch.Style.TextureAddressModes[Stage];
                                const int MipMode = K.Renderer().ResolveMipmapMode(Batch.Style.TextureMipModes[Stage]);
                                Bindings[Stage].sampler =
                                    !K.Renderer().HasMipmapModeOverride() && AddressMode == 0 && MipMode == 2
                                        ? (Batch.Style.Filter ? K.Gpu().GpuSampler() : K.Gpu().GpuSamplerNearest())
                                        : K.Gpu().SurfaceSampler(AddressMode, Batch.Style.Filter, MipMode);
                            }
                            Bindings[8].texture = Batch.CubeTexture ? Batch.CubeTexture : K.Gpu().WhiteCubeGpuTexture();
                            if (!K.Renderer().HasMipmapModeOverride())
                            {
                                Bindings[8].sampler =
                                    Batch.Style.Filter ? K.Gpu().GpuSampler() : K.Gpu().GpuSamplerNearest();
                            }
                            else
                            {
                                const int CubeMipMode = K.Renderer().ResolveMipmapMode(Batch.Style.TextureMipModes[0]);
                                Bindings[8].sampler = K.Gpu().SurfaceSampler(0, Batch.Style.Filter, CubeMipMode);
                            }
                            SDL_BindGPUFragmentSamplers(Pass, 0, Bindings, 9);
                        }
                        else
                        {
                            SDL_GPUTextureSamplerBinding Binding{};
                            Binding.texture = Texture;
                            const int AddressMode = Batch.Style.TextureAddressModes[0];
                            const int MipMode = K.Renderer().ResolveMipmapMode(Batch.Style.TextureMipModes[0]);
                            Binding.sampler =
                                !K.Renderer().HasMipmapModeOverride() && AddressMode == 0 && MipMode == 2
                                    ? (Batch.Style.Filter ? K.Gpu().GpuSampler() : K.Gpu().GpuSamplerNearest())
                                    : K.Gpu().SurfaceSampler(AddressMode, Batch.Style.Filter, MipMode);
                            SDL_BindGPUFragmentSamplers(Pass, 0, &Binding, 1);
                        }
                    }
                    if (!Batch.StaticVertices)
                    {
                        SDL_DrawGPUPrimitives(Pass, Batch.VertexCount, 1, Batch.FirstVertex, 0);
                        continue;
                    }
                    if (!TerrainIndexBuffer_ || !TerrainInstanceBuffer_)
                        continue;
                    const SDL_GPUBufferBinding TerrainBindings[2]{{Batch.StaticVertices, 0},
                                                                  {TerrainInstanceBuffer_, 0}};
                    SDL_BindGPUVertexBuffers(Pass, 0, TerrainBindings, 2);
                    const SDL_GPUBufferBinding IndexBinding{TerrainIndexBuffer_, 0};
                    SDL_BindGPUIndexBuffer(Pass, &IndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_PushGPUVertexUniformData(Cmd, 1, &View.WorldToView[0][0], sizeof(glm::mat4));
                    SDL_DrawGPUIndexedPrimitives(Pass, Batch.IndexCount, 1,
                                                 Geometry.TerrainIndexOffset + Batch.FirstIndex, 0, 0);
                    SDL_PushGPUVertexUniformData(Cmd, 1, &IdentityModelView[0][0], sizeof(glm::mat4));
                    BindSharedVertices(Pass);
                }
            };

            const auto DrawGroup = [&](auto&& Selects)
            {
                DrawBatches(K.Gpu().GpuPipeline(), K.Gpu().GpuPipelineWireframe(), PerspUbo, [&](const GpuBatch& Batch)
                            { return Selects(Batch) && !Batch.Transparent && !Batch.Additive; });
                DrawBatches(K.Gpu().GpuPipelineTransparent() ? K.Gpu().GpuPipelineTransparent() : K.Gpu().GpuPipeline(),
                            K.Gpu().GpuPipelineTransparentWireframe(), PerspUbo, [&](const GpuBatch& Batch)
                            { return Selects(Batch) && Batch.Transparent && !Batch.Additive; });
                DrawBatches(K.Gpu().GpuPipelineAdditive(), K.Gpu().GpuPipelineAdditiveWireframe(), PerspUboAdditive,
                            [&](const GpuBatch& Batch) { return Selects(Batch) && Batch.Additive; });
            };

            const auto DrawBackdrop = [](const GpuBatch& Batch) { return Batch.Style.Backdrop; };
            DrawGroup(DrawBackdrop);

            if ((!Frame.HudLast && K.Sprites().SpritesDrawFirst()))
            {
                DrawBatches(K.Gpu().GpuPipelineTransparent() ? K.Gpu().GpuPipelineTransparent() : K.Gpu().GpuPipeline(),
                            K.Gpu().GpuPipelineTransparentWireframe(), PerspUbo,
                            [](const GpuBatch& Batch) { return Batch.Sprite && !Batch.Additive; });
                DrawBatches(K.Gpu().GpuPipelineAdditive(), K.Gpu().GpuPipelineAdditiveWireframe(), PerspUboAdditive,
                            [](const GpuBatch& Batch) { return Batch.Sprite && Batch.Additive; });
            }

            const auto Draw2d = [&](const GpuBatch& Batch)
            {
                return Batch.Is2d && !Batch.Style.ReflectionOverlay && !Batch.Style.ShadowOverlay &&
                       !Batch.Style.Backdrop && (!(!Frame.HudLast && K.Sprites().SpritesDrawFirst()) || !Batch.Sprite);
            };
            const auto Draw3d = [&](const GpuBatch& Batch) { return !Batch.Is2d && !Batch.Style.ShadowVolume; };
            // Screen 2D keeps screen coordinates whatever the camera viewport.
            const auto DrawScreen2d = [&]()
            {
                const SDL_GPUViewport Screen{
                    0.0f, 0.0f, static_cast<float>(Target.Width), static_cast<float>(Target.Height), 0.0f, 1.0f};
                const SDL_Rect ScreenScissor{0, 0, static_cast<int>(Target.Width), static_cast<int>(Target.Height)};
                SDL_SetGPUViewport(Pass, &Screen);
                SDL_SetGPUScissor(Pass, &ScreenScissor);
                DrawGroup(Draw2d);
                ApplyCameraViewport(Pass);
            };
            if (!HasReflection)
            {
                if ((Frame.HudLast || K.Draw().IsDrawToFront()))
                {
                    DrawGroup(Draw3d);
                    if (!HasShadow)
                        DrawScreen2d();
                }
                else
                {
                    DrawScreen2d();
                    DrawGroup(Draw3d);
                }
            }
            else
            {
                const auto DrawNormal3d = [](const GpuBatch& Batch)
                {
                    return !Batch.Is2d && !Batch.Style.ReflectionSurface && !Batch.Style.ReflectionPass &&
                           !Batch.Style.ShadowVolume;
                };
                const auto DrawReflectionMask = [](const GpuBatch& Batch)
                { return !Batch.Is2d && Batch.Style.ReflectionSurface && !Batch.Style.ReflectionPass; };
                const auto DrawReflected = [](const GpuBatch& Batch)
                { return !Batch.Is2d && Batch.Style.ReflectionPass; };
                const auto DrawReflectionOverlay = [](const GpuBatch& Batch) { return Batch.Style.ReflectionOverlay; };

                if (!(Frame.HudLast || K.Draw().IsDrawToFront()))
                    DrawScreen2d();
                DrawGroup(DrawNormal3d);
                DrawGroup(DrawReflectionMask);
                SDL_EndGPURenderPass(Pass);

                ColorTarget.load_op = SDL_GPU_LOADOP_LOAD;
                DepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
                DepthTarget.store_op = HasShadow ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
                DepthTarget.stencil_load_op = SDL_GPU_LOADOP_LOAD;
                DepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                Pass = SDL_BeginGPURenderPass(Cmd, &ColorTarget, 1, &DepthTarget);
                if (!Pass)
                {
                    K.Renderer().ReportRenderError("reflection render pass failed");
                    Frame.HasErrors = true;
                    return false;
                }
                ApplyCameraViewport(Pass);
                SDL_SetGPUStencilReference(Pass, 1);
                BindSharedVertices(Pass);
                DrawGroup(DrawReflected);
                DrawGroup(DrawReflectionOverlay);
                if ((Frame.HudLast || K.Draw().IsDrawToFront()) && !HasShadow)
                    DrawScreen2d();
            }

            if (HasShadow)
            {
                SDL_EndGPURenderPass(Pass);
                ColorTarget.load_op = SDL_GPU_LOADOP_LOAD;
                DepthTarget.load_op = HasReflection ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                DepthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
                DepthTarget.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
                DepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                Pass = SDL_BeginGPURenderPass(Cmd, &ColorTarget, 1, &DepthTarget);
                if (!Pass)
                {
                    K.Renderer().ReportRenderError("shadow render pass failed");
                    Frame.HasErrors = true;
                    return false;
                }
                ApplyCameraViewport(Pass);
                SDL_SetGPUStencilReference(Pass, 0);
                BindSharedVertices(Pass);

                if (HasReflection)
                {
                    DrawBatches(
                        K.Gpu().ShadowDepthPipeline(), K.Gpu().ShadowDepthPipeline(), PerspUbo,
                        [](const GpuBatch& Batch)
                        {
                            return !Batch.Is2d && !Batch.Style.ReflectionPass && !Batch.Style.ShadowVolume &&
                                   Batch.DepthTest && Batch.DepthWrite;
                        },
                        true);
                }

                DrawBatches(K.Gpu().ShadowVolumePipeline(), K.Gpu().ShadowVolumePipeline(), PerspUbo,
                            [](const GpuBatch& Batch) { return Batch.Style.ShadowVolume; });
                DrawBatches(K.Gpu().ShadowOverlayPipeline(), K.Gpu().ShadowOverlayPipeline(), OrthoUbo,
                            [](const GpuBatch& Batch) { return Batch.Style.ShadowOverlay; });
                if ((Frame.HudLast || K.Draw().IsDrawToFront()))
                    DrawScreen2d();
            }
        }
        SDL_EndGPURenderPass(Pass);

        if (SkippedUntexturedBatches)
        {
            K.Renderer().ReportRenderError("batch with no texture");
        }
        if (FailedEffectBatches)
        {
            K.Renderer().ReportRenderError("effect pipeline creation failed");
        }

        Frame.HasErrors |= SkippedUntexturedBatches || FailedEffectBatches;
        Frame.WorldRendered |= !Geometry.World.empty() || !Geometry.TerrainIndices.empty();
        Frame.Vertices += static_cast<int>(Vertices.size() + Geometry.TerrainIndices.size());
        Frame.Batches += static_cast<int>(Batches.size());
        ++Frame.Views;
        return true;
    }

    void RendererSystem::FinalizeFrame(RenderFrameState& Frame)
    {
        if (Frame.Finalized || Frame.Views == 0)
            return;
        Frame.Finalized = true;
        EngineKernel& K = Kernel_;
        std::string PresentationError;
        if (!K.Sprites().CommitPresentedBobs(&PresentationError))
        {
            Frame.HasErrors = true;
            ReportRenderError(PresentationError);
        }
        if (!Frame.HasErrors)
        {
            RenderFailureReported_ = false;
            K.Debug().ClearRenderError();
        }
        K.Camera().ConsumeClearRequests();

        K.Renderer().RecordFramePolygons(Frame.Vertices / 3);
        K.Renderer().RecordFrameDrawCalls(Frame.Batches);
        if (K.Debug().IsOverlayEnabled())
        {
            FrameStats Stats;
            Stats.Fps = K.Core().Fps();
            Stats.DrawCalls = Frame.Batches;
            Stats.TotalVertices = Frame.Vertices;
            Stats.TotalBatches = Frame.Batches;
            int Total = 0;
            int Visible = 0;
            K.Objects().ForEachObject(
                [&](int, const Object3D& Object)
                {
                    ++Total;
                    if (Object.Visible)
                        ++Visible;
                });
            Stats.TotalObjects = Total;
            Stats.VisibleObjects = Visible;
            K.Debug().UpdateFrameStats(Stats);
        }

        // 2D content is accumulated by the script and consumed by the frame that
        // draws it. A frame presented while the script is blocked must keep it,
        // otherwise the next one is empty. CLS already discarded everything
        // queued before it, so content added after CLS follows the normal rule.
        K.Text().ClearOverlayTextLines();
        if (!Frame.Retain2d)
        {
            K.Text().ClearDebugTextLines();
            K.ImageStoreRef().ClearOverlayBlits();
            K.Draw().ClearPrimitives2D();
            K.Sprites().ClearPastedSprites();
            LastFrameShowedWorld_ = Frame.WorldRendered;
            // Once a 3D scene is shown, DarkBASIC repaints the screen at every SYNC, so what the program
            // drew onto it lasts one frame. 2D-only programs keep their screen. BACKDROP OFF does not
            // change this: the 3D pass still clears its target, and keeping the 2D layer would stack
            // every frame's text over the scene.
            if (Frame.WorldRendered)
                K.ImageStoreRef().ClearScreenBitmap();
        }

        ++K.Core().FramesThisSecond();
        if (Frame.Ticks - K.Core().FpsWindowStart() >= 1000)
        {
            K.Core().Fps() = K.Core().FramesThisSecond();
            K.Core().FramesThisSecond() = 0;
            K.Core().FpsWindowStart() = Frame.Ticks;
        }
    }

    void RenderSceneGpu(EngineKernel& K, bool Retain2d, SDL_Surface** OutCapture)
    {
        if (!K.Gpu().GpuDevice() || !K.Gpu().GpuPipeline() || !K.Gpu().GpuSampler())
        {
            K.Renderer().ReportRenderError("GPU device/pipeline/sampler null");
            return;
        }

        const Uint64 NowTicks = SDL_GetTicks();
        if (K.Core().LastRenderTicks() == 0)
        {
            K.Core().LastRenderTicks() = NowTicks;
        }
        const double DtSeconds = static_cast<double>(NowTicks - K.Core().LastRenderTicks()) / 1000.0;
        K.Core().LastRenderTicks() = NowTicks;

        const bool ApplyFinalColorTransform = K.Renderer().HasFinalColorTransform();
        // A headless window is never presented: every frame renders into the offscreen capture target.
        const bool Headless = K.Core().Headless();
        SDL_GPUCommandBuffer* Cmd = SDL_AcquireGPUCommandBuffer(K.Gpu().GpuDevice());
        if (!Cmd)
        {
            K.Renderer().ReportRenderError("SDL_AcquireGPUCommandBuffer failed");
            return;
        }

        GpuCommandBuffer Commands(Cmd);
        SDL_GPUTexture* SwapchainTexture = nullptr;
        Uint32 DrawableW = 0;
        Uint32 DrawableH = 0;
        if (ApplyFinalColorTransform || Headless)
        {
            int PixelWidth = 0;
            int PixelHeight = 0;
            if (!SDL_GetWindowSizeInPixels(K.Core().Window(), &PixelWidth, &PixelHeight) || PixelWidth <= 0 ||
                PixelHeight <= 0)
            {
                DBC_LOG_DEBUG("render_scene_gpu: window has no drawable pixels");
                Commands.Abandon();
                SDL_Delay(30);
                return;
            }
            DrawableW = static_cast<Uint32>(PixelWidth);
            DrawableH = static_cast<Uint32>(PixelHeight);
            if (!K.Gpu().EnsureGpuCaptureTarget(DrawableW, DrawableH))
            {
                K.Renderer().ReportRenderError("final color target setup failed");
                Commands.Abandon();
                return;
            }
        }
        else
        {
            if (!Commands.AcquireSwapchain(K.Core().Window(), &SwapchainTexture, &DrawableW, &DrawableH))
            {
                DBC_LOG_WARN("SDL_WaitAndAcquireGPUSwapchainTexture failed: {}", SDL_GetError());
                K.Renderer().ReportRenderError("SDL_WaitAndAcquireGPUSwapchainTexture failed");
                Commands.Abandon();
                SDL_Delay(30);
                return;
            }
            if (!SwapchainTexture || DrawableW == 0 || DrawableH == 0)
            {
                DBC_LOG_DEBUG("render_scene_gpu: null swapchain texture (window minimized?)");
                Commands.Abandon();
                SDL_Delay(30);
                return;
            }
        }

        // Keep WindowWidth/Height at LOGICAL window size (e.g. 640x480)
        // so DBC programs, ortho projection, and 2D coordinates work correctly.
        // drawable_w/h may differ on HiDPI/Retina (e.g. 1280x960 for 640x480).
        // Only use drawable dimensions for GPU resource allocation (depth target).
        if (!K.Gpu().EnsureGpuDepthTarget(DrawableW, DrawableH))
        {
            K.Renderer().ReportRenderError("EnsureGpuDepthTarget failed");
            Commands.Abandon();
            return;
        }

        RenderFrameState Frame = K.Renderer().PrepareFrame(DtSeconds, Retain2d);
        SDL_GPUTexture* CaptureTexture = nullptr;
        if (ApplyFinalColorTransform || OutCapture != nullptr || Headless)
        {
            if (!K.Gpu().EnsureGpuCaptureTarget(DrawableW, DrawableH))
            {
                K.Renderer().ReportRenderError("capture target setup failed");
                return;
            }
            CaptureTexture = K.Gpu().GpuCaptureTexture();
        }
        const GpuRenderTarget Target{CaptureTexture ? CaptureTexture : SwapchainTexture,
                                     K.Gpu().GpuDepthTexture(),
                                     K.Gpu().GpuColorFormat(),
                                     K.Gpu().GpuDepthFormat(),
                                     DrawableW,
                                     DrawableH};
        const std::vector<int> CameraIds = K.Camera().ScreenCameraIds();
        std::vector<RenderView> Views;
        std::vector<PreparedViewGeometry> Geometry(CameraIds.size());
        Views.reserve(CameraIds.size());
        bool Prepared = true;
        for (std::size_t Index = 0; Prepared && Index < CameraIds.size(); ++Index)
        {
            const int Previous = K.Camera().SelectRenderCamera(CameraIds[Index]);
            // The triangle cache holds one camera's view-space geometry.
            if (CameraIds.size() > 1)
                K.Renderer().InvalidateTriCache();
            Views.push_back(K.Renderer().CameraView(DrawableW, DrawableH));
            // DRAW TO BACK puts the 2D layer under every camera, DRAW TO FRONT over them all.
            Geometry[Index].Hud = K.Draw().IsDrawToFront() ? Index + 1 == CameraIds.size() : Index == 0;
            Geometry[Index].OverPreviousCamera = Index > 0;
            Prepared = K.Renderer().BuildPreparedView(Views.back(), Geometry[Index], Frame);
            K.Camera().SelectRenderCamera(Previous);
        }
        if (!Prepared || !K.Renderer().UploadPreparedViews(Geometry, Cmd))
            return;
        for (std::size_t Index = 0; Index < Views.size(); ++Index)
        {
            const int Previous = K.Camera().SelectRenderCamera(CameraIds[Index]);
            const bool Recorded = K.Renderer().RecordPreparedView(Views[Index], Target, Geometry[Index], Cmd, Frame);
            K.Camera().SelectRenderCamera(Previous);
            if (!Recorded)
                return;
        }
        if (CameraIds.size() > 1)
            K.Renderer().InvalidateTriCache();

        bool Submitted = false;
        if (ApplyFinalColorTransform)
        {
            Submitted = Commands.Submit();
            if (!Submitted)
            {
                K.Renderer().ReportRenderError("offscreen command submission failed");
                return;
            }

            const std::array<float, 3> Scale = K.Renderer().FinalColorScale();
            if (!K.Gpu().ApplyFinalColorScale(Scale[0], Scale[1], Scale[2]))
            {
                K.Renderer().ReportRenderError("final color transform failed");
                return;
            }

            const Uint32 FinalWidth = DrawableW;
            const Uint32 FinalHeight = DrawableH;
            if (Headless && OutCapture == nullptr)
            {
                K.Renderer().FinalizeFrame(Frame);
                return;
            }
            Cmd = SDL_AcquireGPUCommandBuffer(K.Gpu().GpuDevice());
            Commands.Reset(Cmd);
            if (!Cmd)
            {
                K.Renderer().ReportRenderError("final presentation command buffer failed");
                return;
            }
            if (!Headless &&
                (!Commands.AcquireSwapchain(K.Core().Window(), &SwapchainTexture, &DrawableW, &DrawableH) ||
                 !SwapchainTexture))
            {
                DBC_LOG_WARN("Final presentation swapchain acquisition failed: {}", SDL_GetError());
                K.Renderer().ReportRenderError("final presentation swapchain acquisition failed");
                Commands.Abandon();
                return;
            }

            SDL_GPUTexture* FinalTexture = K.Gpu().GpuFinalColorTexture();
            if (OutCapture != nullptr)
            {
                *OutCapture = dbc::DownloadCapture(K, Commands, FinalTexture, SwapchainTexture, FinalWidth, FinalHeight,
                                                   Submitted, DrawableW, DrawableH);
            }
            else
            {
                SDL_GPUBlitInfo Blit{};
                Blit.source.texture = FinalTexture;
                Blit.source.w = FinalWidth;
                Blit.source.h = FinalHeight;
                Blit.destination.texture = SwapchainTexture;
                Blit.destination.w = DrawableW;
                Blit.destination.h = DrawableH;
                Blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                Blit.filter = SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture(Cmd, &Blit);
                Submitted = Commands.Submit();
            }
        }
        else if (CaptureTexture != nullptr && OutCapture != nullptr)
        {
            *OutCapture =
                dbc::DownloadCapture(K, Commands, CaptureTexture, SwapchainTexture, DrawableW, DrawableH, Submitted);
        }
        else
        {
            Submitted = Commands.Submit();
        }
        if (!Submitted)
        {
            K.Renderer().ReportRenderError("command submission failed");
            return;
        }
        K.Renderer().FinalizeFrame(Frame);
    }

    // -----------------------------------------------------------------------
    // Renderer pipeline helpers
    // -----------------------------------------------------------------------

    [[nodiscard]] float FogFactor(EngineKernel& K, float Z)
    {
        if (!K.Draw().FogOn())
        {
            return 0.0f;
        }
        const float Start = K.Draw().FogStartDistance();
        const float Range = K.Draw().FogDistance() - Start;
        if (std::abs(Range) <= std::numeric_limits<float>::epsilon())
            return Z >= Start ? 1.0f : 0.0f;
        return std::clamp((Z - Start) / Range, 0.0f, 1.0f);
    }

    // Camera inverse rotation helpers — apply a single-axis inverse rotation.
    inline void CamInvX(float& Y, float& Z, float AngleDeg)
    {
        const float A = -AngleDeg * static_cast<float>(M_PI / 180.0);
        const float C = std::cos(A);
        const float S = std::sin(A);
        const float Ny = Y * C - Z * S;
        const float Nz = Y * S + Z * C;
        Y = Ny;
        Z = Nz;
    }

    inline void CamInvY(float& X, float& Z, float AngleDeg)
    {
        const float A = AngleDeg * static_cast<float>(M_PI / 180.0);
        const float C = std::cos(A);
        const float S = std::sin(A);
        const float Nx = X * C - Z * S;
        const float Nz = X * S + Z * C;
        X = Nx;
        Z = Nz;
    }

    inline void CamInvZ(float& X, float& Y, float AngleDeg)
    {
        const float A = -AngleDeg * static_cast<float>(M_PI / 180.0);
        const float C = std::cos(A);
        const float S = std::sin(A);
        const float Nx = X * C - Y * S;
        const float Ny = X * S + Y * C;
        X = Nx;
        Y = Ny;
    }

    [[nodiscard]] Vec3 WorldToViewPoint(EngineKernel& K, float Wx, float Wy, float Wz)
    {
        const auto& Cam = K.Camera();
        if (K.Renderer().ReflectionViewEnabled())
        {
            const Vec3 Reflected = K.Renderer().ReflectWorldPoint(Vec3{Wx, Wy, Wz});
            Wx = Reflected.X;
            Wy = Reflected.Y;
            Wz = Reflected.Z;
        }
        if (const RenderView* View = K.Renderer().ActiveView())
        {
            const glm::vec4 Point = View->WorldToView * glm::vec4(Wx, Wy, Wz, 1.0f);
            return {Point.x, Point.y, Point.z};
        }
        float X = Wx - Cam.PositionX();
        float Y = Wy - Cam.PositionY();
        float Z = Wz - Cam.PositionZ();

        if (Cam.IsZyxRotation())
        {
            // ZYX forward → inverse: X^-1, Y^-1, Z^-1
            CamInvX(Y, Z, Cam.AngleX());
            CamInvY(X, Z, Cam.AngleY());
            CamInvZ(X, Y, Cam.AngleZ());
        }
        else
        {
            // Default order: Y, X, Z (matches original DBC FPS convention)
            CamInvY(X, Z, Cam.AngleY());
            CamInvX(Y, Z, Cam.AngleX());
            CamInvZ(X, Y, Cam.AngleZ());
        }

        return Vec3{X, Y, Z};
    }

    [[nodiscard]] Vec3 WorldToViewDirection(EngineKernel& K, float Dx, float Dy, float Dz)
    {
        const auto& Cam = K.Camera();
        if (K.Renderer().ReflectionViewEnabled())
        {
            const Vec3 Reflected = K.Renderer().ReflectWorldDirection(Vec3{Dx, Dy, Dz});
            Dx = Reflected.X;
            Dy = Reflected.Y;
            Dz = Reflected.Z;
        }
        if (const RenderView* View = K.Renderer().ActiveView())
        {
            const glm::vec4 Direction = View->WorldToView * glm::vec4(Dx, Dy, Dz, 0.0f);
            return {Direction.x, Direction.y, Direction.z};
        }
        float X = Dx, Y = Dy, Z = Dz;

        if (Cam.IsZyxRotation())
        {
            CamInvX(Y, Z, Cam.AngleX());
            CamInvY(X, Z, Cam.AngleY());
            CamInvZ(X, Y, Cam.AngleZ());
        }
        else
        {
            CamInvY(X, Z, Cam.AngleY());
            CamInvX(Y, Z, Cam.AngleX());
            CamInvZ(X, Y, Cam.AngleZ());
        }

        return Vec3{X, Y, Z};
    }

    [[nodiscard]] GpuFragmentUniforms BuildFragmentUniforms(EngineKernel& K)
    {
        GpuFragmentUniforms Ubo{};

        Ubo.Flags[0] = 1.0f; // use_lighting (overridden per-vertex via zero normal)
        Ubo.Flags[1] = K.Draw().FogOn() ? 1.0f : 0.0f;
        Ubo.Flags[2] = 1.0f;  // use_ambient
        Ubo.Flags[3] = 32.0f; // retained custom-effect ABI value; stock materials carry their own power

        {
            const int AmbCol = K.Lights().AmbientColor();
            const float AmbLvl = K.Lights().AmbientLight();
            const SDL_FColor Ac = UnpackColor(AmbCol);
            Ubo.AmbientColor[0] = Ac.r;
            Ubo.AmbientColor[1] = Ac.g;
            Ubo.AmbientColor[2] = Ac.b;
            Ubo.AmbientColor[3] = std::clamp(AmbLvl, 0.0f, 1.0f);
        }

        if (K.Draw().FogOn())
        {
            const SDL_FColor Fc = UnpackColor(K.Draw().FogColor());
            Ubo.FogColorDist[0] = Fc.r;
            Ubo.FogColorDist[1] = Fc.g;
            Ubo.FogColorDist[2] = Fc.b;
            Ubo.FogColorDist[3] = K.Draw().FogDistance();
            Ubo.DirLightDirection[3] = K.Draw().FogStartDistance();
        }

        int NumPoint = 0;
        int NumSpot = 0;
        bool HasDir = false;

        K.Lights().ForEachLight(
            [&](int /*id*/, const Light3D& Light)
            {
                if (!Light.Visible)
                    return;

                if (Light.Type == 3 && !HasDir)
                {
                    const Vec3 Vd = dbc::WorldToViewDirection(K, Light.DirX, Light.DirY, Light.DirZ);
                    Ubo.DirLightDirection[0] = Vd.X;
                    Ubo.DirLightDirection[1] = Vd.Y;
                    Ubo.DirLightDirection[2] = Vd.Z;
                    Ubo.DirLightDirection[3] = K.Draw().FogStartDistance();
                    const SDL_FColor Lc = UnpackColor(Light.Color);
                    Ubo.DirLightColor[0] = Lc.r;
                    Ubo.DirLightColor[1] = Lc.g;
                    Ubo.DirLightColor[2] = Lc.b;
                    Ubo.DirLightColor[3] = 1.0f;
                    HasDir = true;
                }
                else if (Light.Type == 1 && NumPoint < 12)
                {
                    const Vec3 Vp = dbc::WorldToViewPoint(K, Light.PosX, Light.PosY, Light.PosZ);
                    const SDL_FColor Lc = UnpackColor(Light.Color);
                    const int Base = NumPoint * 2;
                    Ubo.PointLights[Base][0] = Vp.X;
                    Ubo.PointLights[Base][1] = Vp.Y;
                    Ubo.PointLights[Base][2] = Vp.Z;
                    Ubo.PointLights[Base][3] = Light.Range;
                    Ubo.PointLights[Base + 1][0] = Lc.r;
                    Ubo.PointLights[Base + 1][1] = Lc.g;
                    Ubo.PointLights[Base + 1][2] = Lc.b;
                    Ubo.PointLights[Base + 1][3] = 1.0f;
                    ++NumPoint;
                }
                else if (Light.Type == 2 && NumSpot < 6)
                {
                    const Vec3 Vp = dbc::WorldToViewPoint(K, Light.PosX, Light.PosY, Light.PosZ);
                    const Vec3 Vd = dbc::WorldToViewDirection(K, Light.DirX, Light.DirY, Light.DirZ);
                    const SDL_FColor Lc = UnpackColor(Light.Color);
                    const float CosInner = std::cos(Light.SpotInner * static_cast<float>(M_PI / 180.0));
                    const float CosOuter = std::cos(Light.SpotOuter * static_cast<float>(M_PI / 180.0));
                    const int Base = NumSpot * 3;
                    Ubo.SpotLights[Base][0] = Vp.X;
                    Ubo.SpotLights[Base][1] = Vp.Y;
                    Ubo.SpotLights[Base][2] = Vp.Z;
                    Ubo.SpotLights[Base][3] = Light.Range;
                    Ubo.SpotLights[Base + 1][0] = Vd.X;
                    Ubo.SpotLights[Base + 1][1] = Vd.Y;
                    Ubo.SpotLights[Base + 1][2] = Vd.Z;
                    Ubo.SpotLights[Base + 1][3] = CosInner;
                    Ubo.SpotLights[Base + 2][0] = Lc.r;
                    Ubo.SpotLights[Base + 2][1] = Lc.g;
                    Ubo.SpotLights[Base + 2][2] = Lc.b;
                    Ubo.SpotLights[Base + 2][3] = CosOuter;
                    ++NumSpot;
                }
            });

        Ubo.LightCounts[0] = NumPoint;
        Ubo.LightCounts[1] = NumSpot;
        Ubo.LightCounts[2] = 0;
        Ubo.LightCounts[3] = 0;

        return Ubo;
    }

    [[nodiscard]] bool ProjectViewPoint(EngineKernel& K, float X, float Y, float Z, SDL_FPoint& OutPos, float& OutDepth)
    {
        OutDepth = Z;
        if (const RenderView* View = K.Renderer().ActiveView())
            return View->Project({X, Y, Z}, OutPos);
        return K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight()).Project({X, Y, Z}, OutPos);
    }

    [[nodiscard]] bool ProjectPoint(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth)
    {
        const Vec3 View = dbc::WorldToViewPoint(K, Wx, Wy, Wz);
        return dbc::ProjectViewPoint(K, View.X, View.Y, View.Z, OutPos, OutDepth);
    }

    [[nodiscard]] bool ProjectPointLocked(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos,
                                          float& OutDepth)
    {
        return dbc::ProjectViewPoint(K, Wx, Wy, Wz, OutPos, OutDepth);
    }

    [[nodiscard]] ViewVertex InterpolateViewVertex(const ViewVertex& A, const ViewVertex& B, float T)
    {
        const float ClampedT = std::clamp(T, 0.0f, 1.0f);
        ViewVertex Out{};
        Out.X = A.X + (B.X - A.X) * ClampedT;
        Out.Y = A.Y + (B.Y - A.Y) * ClampedT;
        Out.Z = A.Z + (B.Z - A.Z) * ClampedT;
        Out.Nx = A.Nx + (B.Nx - A.Nx) * ClampedT;
        Out.Ny = A.Ny + (B.Ny - A.Ny) * ClampedT;
        Out.Nz = A.Nz + (B.Nz - A.Nz) * ClampedT;
        Out.Uv.x = A.Uv.x + (B.Uv.x - A.Uv.x) * ClampedT;
        Out.Uv.y = A.Uv.y + (B.Uv.y - A.Uv.y) * ClampedT;
        for (std::size_t Stage = 0; Stage < Out.AuxiliaryUvs.size(); ++Stage)
        {
            Out.AuxiliaryUvs[Stage].x =
                A.AuxiliaryUvs[Stage].x + (B.AuxiliaryUvs[Stage].x - A.AuxiliaryUvs[Stage].x) * ClampedT;
            Out.AuxiliaryUvs[Stage].y =
                A.AuxiliaryUvs[Stage].y + (B.AuxiliaryUvs[Stage].y - A.AuxiliaryUvs[Stage].y) * ClampedT;
        }
        Out.CubeX = A.CubeX + (B.CubeX - A.CubeX) * ClampedT;
        Out.CubeY = A.CubeY + (B.CubeY - A.CubeY) * ClampedT;
        Out.CubeZ = A.CubeZ + (B.CubeZ - A.CubeZ) * ClampedT;
        Out.Color.r = A.Color.r + (B.Color.r - A.Color.r) * ClampedT;
        Out.Color.g = A.Color.g + (B.Color.g - A.Color.g) * ClampedT;
        Out.Color.b = A.Color.b + (B.Color.b - A.Color.b) * ClampedT;
        Out.Color.a = A.Color.a + (B.Color.a - A.Color.a) * ClampedT;
        return Out;
    }

    [[nodiscard]] int ClipPolygonAgainstPlane(const ViewVertex* Input, int Count, ViewVertex* Output,
                                              const glm::vec4& Plane)
    {
        if (Count <= 0)
            return 0;
        const auto Distance = [&](const ViewVertex& Vertex)
        { return glm::dot(Plane, glm::vec4(Vertex.X, Vertex.Y, Vertex.Z, 1.0f)); };
        int OutputCount = 0;
        ViewVertex Previous = Input[Count - 1];
        float PreviousDistance = Distance(Previous);
        for (int Index = 0; Index < Count; ++Index)
        {
            const ViewVertex Current = Input[Index];
            const float CurrentDistance = Distance(Current);
            if ((PreviousDistance >= 0.0f) != (CurrentDistance >= 0.0f))
            {
                const float Ratio = PreviousDistance / (PreviousDistance - CurrentDistance);
                Output[OutputCount++] = InterpolateViewVertex(Previous, Current, Ratio);
            }
            if (CurrentDistance >= 0.0f)
                Output[OutputCount++] = Current;
            Previous = Current;
            PreviousDistance = CurrentDistance;
        }
        return OutputCount;
    }

    void AppendClippedViewTriangle(EngineKernel& K, const ViewVertex& V0, const ViewVertex& V1, const ViewVertex& V2,
                                   SDL_Texture* Texture, SDL_GPUTexture* GpuTexture, bool Transparent,
                                   std::vector<Triangle>& Tris, bool Additive, SurfaceStyle Style, int AlphaPhase,
                                   bool VeryEarly, float DepthBias, int MatrixGhostMode, int RenderPriority,
                                   bool DepthWrite, bool DepthTest, std::array<SDL_GPUTexture*, 7> AuxiliaryGpuTextures,
                                   SDL_GPUTexture* CubeGpuTexture)
    {
        std::optional<RenderView> CameraView;
        const RenderView* View = K.Renderer().ActiveView();
        if (!View)
        {
            CameraView.emplace(K.Renderer().CameraView(K.Core().WindowWidth(), K.Core().WindowHeight()));
            View = &*CameraView;
        }

        ViewVertex ClippedA[8]{};
        ViewVertex ClippedB[8]{};
        ClippedA[0] = V0;
        ClippedA[1] = V1;
        ClippedA[2] = V2;

        int Count = 3;
        Count = ClipPolygonAgainstPlane(ClippedA, Count, ClippedB, View->ClipPlane(4, Additive));
        if (Count < 3)
        {
            return;
        }
        Count = ClipPolygonAgainstPlane(ClippedB, Count, ClippedA, View->ClipPlane(5, Additive));
        if (Count < 3)
        {
            return;
        }

        for (int I = 1; I + 1 < Count; ++I)
        {
            const ViewVertex A = ClippedA[0];
            const ViewVertex B = ClippedA[I];
            const ViewVertex C = ClippedA[I + 1];

            Triangle Tri;
            Tri.Texture = Texture;
            Tri.GpuTexture = GpuTexture;
            Tri.AuxiliaryGpuTextures = AuxiliaryGpuTextures;
            Tri.CubeGpuTexture = CubeGpuTexture;
            Tri.Depth = (A.Z + B.Z + C.Z) / 3.0f + DepthBias;
            const bool AutomaticTransparency =
                Transparent || A.Color.a < 0.999f || B.Color.a < 0.999f || C.Color.a < 0.999f;
            Tri.Transparent = AlphaPhase == 2 || (AlphaPhase < 0 && AutomaticTransparency);
            Tri.AlphaDepthWrite = AlphaPhase == 1;
            Tri.Additive = Additive;
            Tri.VeryEarly = VeryEarly;
            Tri.MatrixGhostMode = MatrixGhostMode;
            Tri.DepthTest = DepthTest;
            Tri.DepthWrite = DepthWrite && !Additive;
            Tri.RenderPriority = RenderPriority;
            Tri.Is2d = false;
            Tri.Style = Style;
            Tri.Pos[0][0] = A.X;
            Tri.Pos[0][1] = A.Y;
            Tri.Pos[0][2] = A.Z;
            Tri.Pos[1][0] = B.X;
            Tri.Pos[1][1] = B.Y;
            Tri.Pos[1][2] = B.Z;
            Tri.Pos[2][0] = C.X;
            Tri.Pos[2][1] = C.Y;
            Tri.Pos[2][2] = C.Z;
            Tri.Normal[0][0] = A.Nx;
            Tri.Normal[0][1] = A.Ny;
            Tri.Normal[0][2] = A.Nz;
            Tri.Normal[1][0] = B.Nx;
            Tri.Normal[1][1] = B.Ny;
            Tri.Normal[1][2] = B.Nz;
            Tri.Normal[2][0] = C.Nx;
            Tri.Normal[2][1] = C.Ny;
            Tri.Normal[2][2] = C.Nz;
            Tri.Color[0] = A.Color;
            Tri.Color[1] = B.Color;
            Tri.Color[2] = C.Color;
            Tri.Uv[0] = A.Uv;
            Tri.Uv[1] = B.Uv;
            Tri.Uv[2] = C.Uv;
            for (std::size_t Stage = 0; Stage < Tri.AuxiliaryUvs.size(); ++Stage)
            {
                Tri.AuxiliaryUvs[Stage][0] = A.AuxiliaryUvs[Stage];
                Tri.AuxiliaryUvs[Stage][1] = B.AuxiliaryUvs[Stage];
                Tri.AuxiliaryUvs[Stage][2] = C.AuxiliaryUvs[Stage];
            }
            Tri.CubeDirection[0][0] = A.CubeX;
            Tri.CubeDirection[0][1] = A.CubeY;
            Tri.CubeDirection[0][2] = A.CubeZ;
            Tri.CubeDirection[1][0] = B.CubeX;
            Tri.CubeDirection[1][1] = B.CubeY;
            Tri.CubeDirection[1][2] = B.CubeZ;
            Tri.CubeDirection[2][0] = C.CubeX;
            Tri.CubeDirection[2][1] = C.CubeY;
            Tri.CubeDirection[2][2] = C.CubeZ;
            Tris.push_back(Tri);
        }
    }

    void RenderScene(EngineKernel& K, bool Retain2d)
    {
        dbc::RenderSceneGpu(K, Retain2d, nullptr);
    }

    // =======================================================================
    // RendererSystem — constructor / destructor
    // =======================================================================

    RendererSystem::RendererSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    RendererSystem::~RendererSystem()
    {
        ReleaseStaticTerrains();
    }

    bool RendererSystem::DrawsTerrainOnGpu(const Matrix& Terrain) const
    {
        // Ghost, transparent and unculled surfaces keep the CPU path with its blending and ordering.
        return Terrain.IsTerrain && !Terrain.Ghost && !Terrain.Transparency && Terrain.Cull &&
               Kernel_.Gpu().TerrainPipeline(false) != nullptr;
    }

    void RendererSystem::AppendStaticTerrains(const RenderView& View, PreparedViewGeometry& Geometry)
    {
        EngineKernel& K = Kernel_;
        Geometry.TerrainIndices.clear();
        LastStaticTerrainTriangles_ = 0;
        SDL_GPUDevice* Device = K.Gpu().GpuDevice();
        if (!Device)
            return;

        // Drop buffers of terrains that were deleted or left the GPU path.
        for (auto It = StaticTerrains_.begin(); It != StaticTerrains_.end();)
        {
            const Matrix* Terrain = K.Matrices().GetTerrain(It->first);
            if (Terrain && DrawsTerrainOnGpu(*Terrain))
            {
                ++It;
                continue;
            }
            if (It->second.Vertices)
                SDL_ReleaseGPUBuffer(Device, It->second.Vertices);
            It = StaticTerrains_.erase(It);
        }

        const glm::vec3 Camera = View.CameraPosition;
        std::vector<int> Steps;
        K.Matrices().ForEachTerrain(
            [&](int TerrainId, const Matrix& Terrain)
            {
                if (!DrawsTerrainOnGpu(Terrain))
                    return;
                StaticTerrain& Entry = StaticTerrains_[TerrainId];
                if (!Entry.Vertices || Entry.Revision != Terrain.Revision)
                {
                    BuildTerrainMesh(Terrain, Entry.Mesh);
                    if (Entry.Vertices)
                        SDL_ReleaseGPUBuffer(Device, Entry.Vertices);
                    Entry.Vertices = nullptr;
                    Entry.Revision = Terrain.Revision;
                    if (Entry.Mesh.Vertices.empty() ||
                        Entry.Mesh.Vertices.size() > std::numeric_limits<Uint32>::max() / sizeof(TerrainVertex))
                        return;
                    SDL_GPUBufferCreateInfo Info{};
                    Info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                    Info.size = static_cast<Uint32>(Entry.Mesh.Vertices.size() * sizeof(TerrainVertex));
                    Entry.Vertices = SDL_CreateGPUBuffer(Device, &Info);
                    if (!Entry.Vertices)
                    {
                        ReportRenderError(std::string("terrain vertex buffer creation failed: ") + SDL_GetError());
                        return;
                    }
                    Entry.PendingUpload = true;
                }
                const TerrainMesh& Mesh = Entry.Mesh;

                // Visibility and grid step per block, then each block's triangles, stitched to coarser neighbours.
                Steps.assign(Mesh.Blocks.size(), 0);
                for (std::size_t Index = 0; Index < Mesh.Blocks.size(); ++Index)
                {
                    const TerrainBlock& Block = Mesh.Blocks[Index];
                    const glm::vec3 Minimum(Block.Minimum.X, Block.Minimum.Y, Block.Minimum.Z);
                    const glm::vec3 Maximum(Block.Maximum.X, Block.Maximum.Y, Block.Maximum.Z);
                    const glm::vec3 Center = (Minimum + Maximum) * 0.5f;
                    const glm::vec3 ViewCenter(View.WorldToView * glm::vec4(Center, 1.0f));
                    const bool Visible = View.ContainsSphere(ViewCenter, glm::length(Maximum - Center));
                    int Step = 1;
                    if (Mesh.Lod)
                        Step = TerrainBlockStep(glm::length(glm::clamp(Camera, Minimum, Maximum) - Camera),
                                                Mesh.BlockSize);
                    Steps[Index] = Visible ? Step : -Step;
                }
                const auto StepOf = [&](int Bx, int Bz)
                {
                    if (Bx < 0 || Bz < 0 || Bx >= Mesh.BlocksX || Bz >= Mesh.BlocksZ)
                        return 1;
                    return std::abs(Steps[static_cast<std::size_t>(Bz) * Mesh.BlocksX + Bx]);
                };
                const auto FirstIndex = static_cast<Uint32>(Geometry.TerrainIndices.size());
                for (int Bz = 0; Bz < Mesh.BlocksZ; ++Bz)
                {
                    for (int Bx = 0; Bx < Mesh.BlocksX; ++Bx)
                    {
                        const int Step = Steps[static_cast<std::size_t>(Bz) * Mesh.BlocksX + Bx];
                        if (Step < 0)
                            continue;
                        AppendTerrainBlockIndices(
                            Mesh, Mesh.Blocks[static_cast<std::size_t>(Bz) * Mesh.BlocksX + Bx], Step,
                            {StepOf(Bx, Bz - 1), StepOf(Bx, Bz + 1), StepOf(Bx - 1, Bz), StepOf(Bx + 1, Bz)},
                            Geometry.TerrainIndices);
                    }
                }
                const auto IndexCount = static_cast<Uint32>(Geometry.TerrainIndices.size()) - FirstIndex;
                if (IndexCount == 0)
                    return;

                Image* Img = Terrain.ImageId != 0 ? K.ImageStoreRef().GetImage(Terrain.ImageId) : nullptr;
                GpuBatch Batch;
                // Like CPU triangles, an untextured terrain draws with the white texture.
                Batch.Texture = Img ? dbc::ImageGpuTexture(K, *Img, false) : nullptr;
                if (!Batch.Texture)
                    Batch.Texture = WhiteGpuTexture(K);
                Batch.FirstVertex = static_cast<Uint32>(Geometry.Vertices.size());
                Batch.Style.Wireframe = Terrain.Wireframe;
                Batch.Style.Fog = Terrain.Fog;
                Batch.Style.Ambient = Terrain.Ambient;
                Batch.Style.Filter = Terrain.Filter;
                Batch.Style.TextureAddressModes[0] = Terrain.TextureMode;
                Batch.Style.TextureMipModes[0] = Terrain.MipMode;
                Batch.StaticVertices = Entry.Vertices;
                Batch.FirstIndex = FirstIndex;
                Batch.IndexCount = IndexCount;
                Geometry.Batches.push_back(Batch);
                LastStaticTerrainTriangles_ += IndexCount / 3;
            });
    }

    bool RendererSystem::UploadStaticTerrains(std::span<PreparedViewGeometry> Geometry, SDL_GPUCommandBuffer* Cmd)
    {
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        std::size_t IndexCount = 0;
        for (PreparedViewGeometry& View : Geometry)
        {
            View.TerrainIndexOffset = static_cast<Uint32>(IndexCount);
            IndexCount += View.TerrainIndices.size();
        }
        std::size_t VertexBytes = 0;
        for (const auto& [TerrainId, Entry] : StaticTerrains_)
        {
            (void)TerrainId;
            if (Entry.PendingUpload)
                VertexBytes += Entry.Mesh.Vertices.size() * sizeof(TerrainVertex);
        }
        const std::size_t InstanceBytes = TerrainInstanceUploaded_ ? 0 : sizeof(GpuVertex);
        const std::size_t IndexBytes = IndexCount * sizeof(uint32_t);
        const std::size_t TotalBytes = VertexBytes + InstanceBytes + IndexBytes;
        if (IndexCount == 0 && VertexBytes == 0)
            return true;
        if (!Device || !Cmd)
            return false;
        if (TotalBytes > std::numeric_limits<Uint32>::max())
        {
            ReportRenderError("terrain geometry exceeds the GPU buffer limit");
            return false;
        }

        const auto Recreate = [&](auto*& Resource, Uint32& Capacity, Uint32 Needed, auto&& Create, auto&& Release)
        {
            if (Resource && Capacity >= Needed)
                return true;
            if (Resource)
                Release(Resource);
            Capacity = std::max<Uint32>(Needed, Capacity * 2);
            Resource = Create(Capacity);
            if (!Resource)
                Capacity = 0;
            return Resource != nullptr;
        };
        if (!TerrainInstanceBuffer_)
        {
            SDL_GPUBufferCreateInfo Info{};
            Info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            Info.size = sizeof(GpuVertex);
            TerrainInstanceBuffer_ = SDL_CreateGPUBuffer(Device, &Info);
            TerrainInstanceUploaded_ = false;
        }
        const bool Ready =
            TerrainInstanceBuffer_ &&
            (IndexBytes == 0 || Recreate(
                                    TerrainIndexBuffer_, TerrainIndexCapacity_, static_cast<Uint32>(IndexBytes),
                                    [&](Uint32 Size)
                                    {
                                        SDL_GPUBufferCreateInfo Info{};
                                        Info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
                                        Info.size = Size;
                                        return SDL_CreateGPUBuffer(Device, &Info);
                                    },
                                    [&](SDL_GPUBuffer* Buffer) { SDL_ReleaseGPUBuffer(Device, Buffer); })) &&
            Recreate(
                TerrainTransfer_, TerrainTransferCapacity_, static_cast<Uint32>(TotalBytes),
                [&](Uint32 Size)
                {
                    SDL_GPUTransferBufferCreateInfo Info{};
                    Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    Info.size = Size;
                    return SDL_CreateGPUTransferBuffer(Device, &Info);
                },
                [&](SDL_GPUTransferBuffer* Buffer) { SDL_ReleaseGPUTransferBuffer(Device, Buffer); });
        if (!Ready)
        {
            ReportRenderError(std::string("terrain buffer creation failed: ") + SDL_GetError());
            return false;
        }

        auto* Mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, TerrainTransfer_, true));
        if (!Mapped)
            return false;
        std::size_t Offset = 0;
        for (const auto& [TerrainId, Entry] : StaticTerrains_)
        {
            (void)TerrainId;
            if (!Entry.PendingUpload)
                continue;
            const std::size_t Bytes = Entry.Mesh.Vertices.size() * sizeof(TerrainVertex);
            SDL_memcpy(Mapped + Offset, Entry.Mesh.Vertices.data(), Bytes);
            Offset += Bytes;
        }
        if (InstanceBytes != 0)
        {
            // What every CPU terrain vertex carries besides position, normal and UV: white colour and the
            // default material (terrains have no material commands).
            GpuVertex Constant{};
            FillSurfaceMaterial(Constant, SurfaceStyle{});
            SDL_memcpy(Mapped + Offset, &Constant, sizeof(Constant));
            Offset += sizeof(Constant);
        }
        for (const PreparedViewGeometry& View : Geometry)
        {
            SDL_memcpy(Mapped + Offset, View.TerrainIndices.data(), View.TerrainIndices.size() * sizeof(uint32_t));
            Offset += View.TerrainIndices.size() * sizeof(uint32_t);
        }
        SDL_UnmapGPUTransferBuffer(Device, TerrainTransfer_);

        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Cmd);
        if (!Copy)
            return false;
        Offset = 0;
        for (auto& [TerrainId, Entry] : StaticTerrains_)
        {
            (void)TerrainId;
            if (!Entry.PendingUpload)
                continue;
            const auto Bytes = static_cast<Uint32>(Entry.Mesh.Vertices.size() * sizeof(TerrainVertex));
            const SDL_GPUTransferBufferLocation Source{TerrainTransfer_, static_cast<Uint32>(Offset)};
            const SDL_GPUBufferRegion Destination{Entry.Vertices, 0, Bytes};
            SDL_UploadToGPUBuffer(Copy, &Source, &Destination, false);
            Offset += Bytes;
            Entry.PendingUpload = false;
            // The GPU now holds the grid; the blocks keep what visibility and LOD need.
            Entry.Mesh.Vertices.clear();
            Entry.Mesh.Vertices.shrink_to_fit();
        }
        if (InstanceBytes != 0)
        {
            const SDL_GPUTransferBufferLocation Source{TerrainTransfer_, static_cast<Uint32>(Offset)};
            const SDL_GPUBufferRegion Destination{TerrainInstanceBuffer_, 0, sizeof(GpuVertex)};
            SDL_UploadToGPUBuffer(Copy, &Source, &Destination, false);
            Offset += sizeof(GpuVertex);
            TerrainInstanceUploaded_ = true;
        }
        if (IndexBytes != 0)
        {
            const SDL_GPUTransferBufferLocation Source{TerrainTransfer_, static_cast<Uint32>(Offset)};
            const SDL_GPUBufferRegion Destination{TerrainIndexBuffer_, 0, static_cast<Uint32>(IndexBytes)};
            SDL_UploadToGPUBuffer(Copy, &Source, &Destination, true);
        }
        SDL_EndGPUCopyPass(Copy);
        return true;
    }

    void RendererSystem::ReleaseStaticTerrains()
    {
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        if (Device)
        {
            for (auto& [TerrainId, Entry] : StaticTerrains_)
            {
                (void)TerrainId;
                if (Entry.Vertices)
                    SDL_ReleaseGPUBuffer(Device, Entry.Vertices);
            }
            if (TerrainIndexBuffer_)
                SDL_ReleaseGPUBuffer(Device, TerrainIndexBuffer_);
            if (TerrainInstanceBuffer_)
                SDL_ReleaseGPUBuffer(Device, TerrainInstanceBuffer_);
            if (TerrainTransfer_)
                SDL_ReleaseGPUTransferBuffer(Device, TerrainTransfer_);
        }
        StaticTerrains_.clear();
        TerrainIndexBuffer_ = nullptr;
        TerrainInstanceBuffer_ = nullptr;
        TerrainTransfer_ = nullptr;
        TerrainIndexCapacity_ = 0;
        TerrainTransferCapacity_ = 0;
        TerrainInstanceUploaded_ = false;
    }

    // =======================================================================
    // RendererSystem — public methods (delegate to file-local implementations)
    // =======================================================================

    RenderView RendererSystem::CameraView(Uint32 PhysicalWidth, Uint32 PhysicalHeight) const
    {
        const auto& Camera = Kernel_.Camera();
        glm::mat4 Transform(1.0f);
        for (int Column = 0; Column < 3; ++Column)
        {
            float X = Transform[Column].x, Y = Transform[Column].y, Z = Transform[Column].z;
            if (Camera.IsZyxRotation())
            {
                CamInvX(Y, Z, Camera.AngleX());
                CamInvY(X, Z, Camera.AngleY());
            }
            else
            {
                CamInvY(X, Z, Camera.AngleY());
                CamInvX(Y, Z, Camera.AngleX());
            }
            CamInvZ(X, Y, Camera.AngleZ());
            Transform[Column] = glm::vec4(X, Y, Z, 0.0f);
        }
        Transform[3] = Transform * glm::vec4(-Camera.PositionX(), -Camera.PositionY(), -Camera.PositionZ(), 1.0f);
        const float Fov = glm::radians(std::clamp(Camera.Fov(), 10.0f, 170.0f));
        const float Near = Camera.ClampedNearPlane(), Far = Camera.ClampedFarPlane();
        const int Width = Kernel_.Core().WindowWidth(), Height = Kernel_.Core().WindowHeight();
        const SDL_FRect Viewport =
            Camera.HasCameraView()
                ? SDL_FRect{static_cast<float>(Camera.ViewLeft()), static_cast<float>(Camera.ViewTop()),
                            static_cast<float>(static_cast<double>(Camera.ViewRight()) - Camera.ViewLeft()),
                            static_cast<float>(static_cast<double>(Camera.ViewBottom()) - Camera.ViewTop())}
                : SDL_FRect{0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height)};
        return {Transform,
                glm::perspectiveLH_ZO(Fov, Camera.Aspect(), Near, Far),
                glm::perspectiveLH_ZO(Fov, Camera.Aspect(), Near, std::max(Far, 1e7f)),
                Viewport,
                Width,
                Height,
                PhysicalWidth,
                PhysicalHeight};
    }

    void RendererSystem::PrepareFrameSimulation(double DeltaSeconds)
    {
        dbc::PrepareFrameSimulation(Kernel_, DeltaSeconds);
    }

    void RendererSystem::RenderScene(bool Retain2d)
    {
        // As in DBPro's SYNC: BSP collisions resolve before drawing and record their next start after.
        Kernel_.BspWorld().ResolveCollisions();
        Kernel_.Video().PaintFrames();
        bool Rendered = false;
        if (Kernel_.Vr().Enabled())
        {
            const auto Now = SDL_GetTicks();
            auto& Previous = Kernel_.Core().LastRenderTicks();
            const double Delta = Previous ? static_cast<double>(Now - Previous) / 1000.0 : 0.0;
            if (Kernel_.Vr().RenderFrame(Delta, Retain2d))
            {
                Previous = Now;
                Rendered = true;
            }
        }
        if (!Rendered)
            dbc::RenderScene(Kernel_, Retain2d);
        Kernel_.BspWorld().CaptureCollisionStarts();
    }

    SDL_Surface* RendererSystem::CaptureScreen()
    {
        if (Kernel_.Vr().Enabled())
            return Kernel_.Vr().CapturePreview();
        SDL_Surface* Surface = nullptr;
        // Keep the 2D content: reading the screen must not erase it.
        dbc::RenderSceneGpu(Kernel_, true, &Surface);
        return Surface;
    }

    SDL_Surface* RendererSystem::ReadLastHeadlessFrame()
    {
        GraphicsDeviceSystem& Gpu = Kernel_.Gpu();
        SDL_GPUTexture* Texture = HasFinalColorTransform() ? Gpu.GpuFinalColorTexture() : Gpu.GpuCaptureTexture();
        if (!Kernel_.Core().Headless() || !Texture || Gpu.GpuCaptureWidth() == 0 || Gpu.GpuCaptureHeight() == 0)
            return nullptr;
        SDL_GPUCommandBuffer* Cmd = SDL_AcquireGPUCommandBuffer(Gpu.GpuDevice());
        if (!Cmd)
            return nullptr;
        GpuCommandBuffer Commands(Cmd);
        bool Submitted = false;
        return dbc::DownloadCapture(Kernel_, Commands, Texture, nullptr, Gpu.GpuCaptureWidth(), Gpu.GpuCaptureHeight(),
                                    Submitted);
    }

    void RendererSystem::SetGamma(int Red, int Green, int Blue)
    {
        GammaLevels_ = {std::clamp(Red, 0, 511), std::clamp(Green, 0, 511), std::clamp(Blue, 0, 511)};
    }

    bool RendererSystem::HasFinalColorTransform() const
    {
        return GammaLevels_ != std::array<int, 3>{255, 255, 255};
    }

    std::array<float, 3> RendererSystem::FinalColorScale() const
    {
        constexpr float DefaultLevel = 255.0f;
        return {static_cast<float>(GammaLevels_[0]) / DefaultLevel, static_cast<float>(GammaLevels_[1]) / DefaultLevel,
                static_cast<float>(GammaLevels_[2]) / DefaultLevel};
    }

    bool RendererSystem::SetMipmapMode(int Mode, std::string* Error)
    {
        if (Mode < 0 || Mode > 2)
        {
            if (Error)
                *Error = "SET MIPMAP MODE requires a value from 0 to 2";
            return false;
        }
        MipmapMode_ = Mode;
        MipmapModeOverride_ = true;
        return true;
    }

    int RendererSystem::ResolveMipmapMode(int SurfaceMode) const
    {
        if (!MipmapModeOverride_)
            return SurfaceMode;
        return MipmapMode_ == 0 ? -1 : MipmapMode_;
    }

    void RendererSystem::ResetRuntimeCompatibilityState()
    {
        GammaLevels_ = {255, 255, 255};
        MipmapModeOverride_ = false;
        MipmapMode_ = 2;
    }

    Mesh* RendererSystem::FindLimbMesh(Object3D& Object, int LimbId)
    {
        return dbc::FindLimbMesh(Kernel_, Object, LimbId);
    }

    const Mesh* RendererSystem::FindLimbMesh(const Object3D& Object, int LimbId)
    {
        return dbc::FindLimbMesh(Kernel_, Object, LimbId);
    }

    Vec3 RendererSystem::WorldObjectOrigin(const Object3D& Object)
    {
        return dbc::WorldObjectOrigin(Kernel_, Object);
    }

    Vec3 RendererSystem::WorldObjectPoint(const Object3D& Object, const Vec3& Point)
    {
        return dbc::TransformObjectSpacePoint(Kernel_, Object, Point);
    }

    Vec3 RendererSystem::WorldLimbOrigin(const Object3D& Object, int LimbId)
    {
        return dbc::WorldLimbOrigin(Kernel_, Object, LimbId);
    }

    Vec3 RendererSystem::ObjectMeshPoint(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point)
    {
        return TransformMeshPointInObject(Object, MeshValue, Point, CurrentImportedMeshTransform(Object, MeshValue),
                                          false);
    }

    Vec3 RendererSystem::ObjectMeshVertex(const Object3D& Object, const Mesh& MeshValue, std::size_t VertexIndex)
    {
        if (VertexIndex >= MeshValue.Vertices.size())
            return {};
        const aiMatrix4x4* CurrentMeshTransform = CurrentImportedMeshTransform(Object, MeshValue);
        Vec3 Point = MeshValue.Vertices[VertexIndex];
        Vec3 Normal{};
        const std::vector<aiMatrix4x4> BonePalette = BuildBonePalette(Object, MeshValue, Object.CachedNodeGlobals);
        const bool Skinned = SkinMeshVertex(MeshValue, VertexIndex, BonePalette, Point, Normal);
        return TransformMeshPointInObject(Object, MeshValue, Point, Skinned ? nullptr : CurrentMeshTransform, Skinned);
    }

    void RendererSystem::ObjectMeshVertices(const Object3D& Object, const Mesh& MeshValue, std::vector<Vec3>& Vertices)
    {
        Vertices.resize(MeshValue.Vertices.size());
        const aiMatrix4x4* CurrentMeshTransform = CurrentImportedMeshTransform(Object, MeshValue);
        const std::vector<aiMatrix4x4> BonePalette = BuildBonePalette(Object, MeshValue, Object.CachedNodeGlobals);
        for (std::size_t VertexIndex = 0; VertexIndex < MeshValue.Vertices.size(); ++VertexIndex)
        {
            Vec3 Point = MeshValue.Vertices[VertexIndex];
            Vec3 Normal{};
            const bool Skinned = SkinMeshVertex(MeshValue, VertexIndex, BonePalette, Point, Normal);
            Vertices[VertexIndex] =
                TransformMeshPointInObject(Object, MeshValue, Point, Skinned ? nullptr : CurrentMeshTransform, Skinned);
        }
    }

    Vec3 RendererSystem::WorldMeshPoint(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point)
    {
        return dbc::TransformObjectSpacePoint(Kernel_, Object, ObjectMeshPoint(Object, MeshValue, Point));
    }

    Vec3 RendererSystem::WorldMeshVertex(const Object3D& Object, const Mesh& MeshValue, std::size_t VertexIndex)
    {
        if (VertexIndex >= MeshValue.Vertices.size())
            return WorldObjectOrigin(Object);
        return dbc::TransformObjectSpacePoint(Kernel_, Object, ObjectMeshVertex(Object, MeshValue, VertexIndex));
    }

    bool RendererSystem::ProjectPoint(float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth)
    {
        return dbc::ProjectPoint(Kernel_, Wx, Wy, Wz, OutPos, OutDepth);
    }

    bool RendererSystem::ProjectPointLocked(float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth)
    {
        return dbc::ProjectPointLocked(Kernel_, Wx, Wy, Wz, OutPos, OutDepth);
    }

    Vec3 RendererSystem::WorldToViewPoint(float Wx, float Wy, float Wz)
    {
        return dbc::WorldToViewPoint(Kernel_, Wx, Wy, Wz);
    }

    Vec3 RendererSystem::WorldToViewDirection(float Dx, float Dy, float Dz)
    {
        return dbc::WorldToViewDirection(Kernel_, Dx, Dy, Dz);
    }

    void RendererSystem::SetReflectionView(const Vec3& Point, const Vec3& Normal)
    {
        ReflectionPoint_ = Point;
        ReflectionNormal_ = Vec3Normalize(Normal);
        ReflectionViewEnabled_ = true;
    }

    void RendererSystem::ClearReflectionView()
    {
        ReflectionViewEnabled_ = false;
    }

    Vec3 RendererSystem::ReflectWorldPoint(const Vec3& Point) const
    {
        const Vec3 Offset = Vec3Sub(Point, ReflectionPoint_);
        const float Distance = Vec3Dot(Offset, ReflectionNormal_);
        return Vec3{Point.X - ReflectionNormal_.X * 2.0f * Distance, Point.Y - ReflectionNormal_.Y * 2.0f * Distance,
                    Point.Z - ReflectionNormal_.Z * 2.0f * Distance};
    }

    Vec3 RendererSystem::ReflectWorldDirection(const Vec3& Direction) const
    {
        const float Projection = 2.0f * Vec3Dot(Direction, ReflectionNormal_);
        return Vec3{Direction.X - ReflectionNormal_.X * Projection, Direction.Y - ReflectionNormal_.Y * Projection,
                    Direction.Z - ReflectionNormal_.Z * Projection};
    }

    void RendererSystem::SetShadowPosition(int Mode, float X, float Y, float Z)
    {
        ShadowPositionMode_ = Mode;
        if (Mode == -1)
            ShadowPosition_ = Vec3{X, Y, Z};
        InvalidateTriCache();
    }

    float RendererSystem::FogFactor(float Z)
    {
        return dbc::FogFactor(Kernel_, Z);
    }

    void RendererSystem::AppendClippedViewTriangle(const ViewVertex& V0, const ViewVertex& V1, const ViewVertex& V2,
                                                   SDL_Texture* Texture, SDL_GPUTexture* GpuTexture, bool Transparent,
                                                   std::vector<Triangle>& Tris, bool Additive)
    {
        dbc::AppendClippedViewTriangle(Kernel_, V0, V1, V2, Texture, GpuTexture, Transparent, Tris, Additive);
    }

    GpuFragmentUniforms RendererSystem::BuildFragmentUniforms()
    {
        return dbc::BuildFragmentUniforms(Kernel_);
    }

    SDL_GPUTexture* RendererSystem::ImageGpuTexture(Image& ImageValue, bool TransparentBlack)
    {
        return dbc::ImageGpuTexture(Kernel_, ImageValue, TransparentBlack);
    }

    // =======================================================================
    // RendererSystem — static methods (pure functions)
    // =======================================================================

    void RendererSystem::AddTriangle(Mesh& MeshValue, const Vec3& A, const Vec3& B, const Vec3& C, SDL_FPoint Ta,
                                     SDL_FPoint Tb, SDL_FPoint Tc)
    {
        dbc::AddTriangle(MeshValue, A, B, C, Ta, Tb, Tc);
    }

    void RendererSystem::RegenerateMeshNormals(Mesh& MeshValue)
    {
        dbc::RegenerateMeshNormals(MeshValue);
    }

    Mesh RendererSystem::MakePlainMesh(float Width, float Height)
    {
        return dbc::MakePlainMesh(Width, Height);
    }

    Mesh RendererSystem::MakeBoxMesh(float Width, float Height, float Depth)
    {
        return dbc::MakeBoxMesh(Width, Height, Depth);
    }

    Mesh RendererSystem::MakeSphereMesh(float Radius, int Rings, int Segments)
    {
        return dbc::MakeSphereMesh(Radius, Rings, Segments);
    }

    Mesh RendererSystem::MakeCylinderMesh(float Radius, float Height, int Segments, bool TopCap, bool BottomCap)
    {
        return dbc::MakeCylinderMesh(Radius, Height, Segments, TopCap, BottomCap);
    }

    Mesh RendererSystem::MakeConeMesh(float Radius, float Height, int Segments)
    {
        return dbc::MakeConeMesh(Radius, Height, Segments);
    }

    void RendererSystem::BuildMeshNodeMapRecursive(const aiNode* Node,
                                                   std::unordered_map<unsigned int, const aiNode*>& OutMap)
    {
        dbc::BuildMeshNodeMapRecursive(Node, OutMap);
    }

    void RendererSystem::BuildLimbMapRecursive(const aiNode* Node, std::vector<const aiNode*>& OutLimbNodes,
                                               std::unordered_map<const aiNode*, int>& OutLimbIndexByNode)
    {
        dbc::BuildLimbMapRecursive(Node, OutLimbNodes, OutLimbIndexByNode);
    }

    aiMatrix4x4 RendererSystem::EvaluateImportedNodeLocalTransform(const Object3D& Object, const aiNode* Node,
                                                                   const AnimationClip& Clip, double TimeTicks)
    {
        return EvaluateNodeLocalTransform(Object, Node, TimeTicks, Clip.Channels, Clip.DurationTicks);
    }

    AssimpImportResult RendererSystem::ImportSceneWithFlags(const std::string& Path, unsigned int Flags)
    {
        return dbc::ImportSceneWithFlags(Path, Flags);
    }

} // namespace dbc
