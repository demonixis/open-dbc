#pragma once

// Shared struct and type definitions for the engine.
// Split from the former monolithic engine_state.hpp.

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#if __has_include(<SDL3_ttf/SDL_ttf.h>)
#include <SDL3_ttf/SDL_ttf.h>
#elif __has_include(<SDL_ttf.h>)
#include <SDL_ttf.h>
#endif

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

namespace dbc
{

    // -----------------------------------------------------------------------
    // Math primitives
    // -----------------------------------------------------------------------

    struct Vec3
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
    };

    struct Mat3
    {
        float M[3][3]{
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
    };

    // -----------------------------------------------------------------------
    // Collision
    // -----------------------------------------------------------------------

    enum class CollisionShape
    {
        Sphere,
        Aabb,
        Polygon,
    };

    struct AABB
    {
        float MinX = 0.0f, MinY = 0.0f, MinZ = 0.0f;
        float MaxX = 0.0f, MaxY = 0.0f, MaxZ = 0.0f;
    };

    struct SpatialHash
    {
        float CellSize = 100.0f;
        std::unordered_map<int64_t, std::vector<int>> Cells;
    };

    // -----------------------------------------------------------------------
    // Image / texture
    // -----------------------------------------------------------------------

    struct Image
    {
        SDL_Texture* Texture = nullptr;
        SDL_Texture* ColorKeyTexture = nullptr;
        SDL_GPUTexture* GpuTexture = nullptr;
        SDL_GPUTexture* GpuColorKeyTexture = nullptr;
        SDL_Surface* Surface = nullptr; // retained for memblock readback
        int Width = 0;
        int Height = 0;
        bool HasAlpha = false;
        bool HasPartialAlpha = false;
        std::string SourcePath{};
        int DivideTextureSize = 0;
    };

    struct OverlayImageBlit
    {
        int ImageId = 0;
        float X = 0.0f;
        float Y = 0.0f;
        bool TransparentBlack = false;
    };

    struct ParticleInstance
    {
        Vec3 Position{};
        Vec3 Velocity{};
        Vec3 InitialPosition{};
        Vec3 InitialVelocity{};
        SDL_FColor Diffuse{1.0f, 1.0f, 1.0f, 1.0f};
        float BirthTime = 0.0f;
        float Fade = 1.0f;
        bool Spark = false;
    };

    struct ParticleEmitter
    {
        int ImageId = 0;
        int FlushSize = 1;
        float Radius = 1.0f;
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;
        float RotX = 0.0f;
        float RotY = 0.0f;
        float RotZ = 0.0f;
        Vec3 EmissionPosition{};
        SDL_FColor EmissionColor{1.0f, 1.0f, 1.0f, 1.0f};
        float SecondsPerFrame = 0.005f;
        int EmissionsPerFrame = 4;
        float EmissionVelocity = 4.0f;
        float Gravity = 5.0f;
        int LifePercent = 100;
        int FloorEnabled = 1;
        float Chaos = 0.0f;
        bool Visible = true;
        bool Ghost = true;
        int GhostMode = -1;
        float GhostPercentage = -1.0f;
        float SimulationTime = 0.0f;
        uint32_t RandomState = 1;
        std::vector<ParticleInstance> Particles;
    };

    // -----------------------------------------------------------------------
    // Matrix (terrain)
    // -----------------------------------------------------------------------

    struct Matrix
    {
        float Width = 0.0f;
        float Depth = 0.0f;
        int SegX = 0;
        int SegZ = 0;
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;

        int ImageId = 0;
        bool OwnsImage = false;
        int TilesX = 1;
        int TilesY = 1;

        std::vector<float> Heights;
        std::vector<int> Tiles;
        // Per-vertex normals (3 floats per vertex: nx, ny, nz). Empty = auto-compute.
        std::vector<float> Normals;
        bool Wireframe = true;
        bool Transparency = false;
        bool Cull = true;
        bool Filter = true;
        bool Light = true;
        bool Fog = true;
        bool Ambient = true;
        bool Ghost = false;
        int GhostMode = 0;
        bool RenderAfterObjects = false;
        int TextureMode = 0; // 0=wrap, 1=mirror, 2=clamp
        int MipMode = 2;     // 0=default linear, 1=point, 2=linear
        float TrimX = 0.0f;
        float TrimY = 0.0f;

        // Terrain extensions (DBPro)
        bool IsTerrain = false;
        float HeightScale = 1.0f;
        int SplitLevel = 4;
        // Changes whenever terrain geometry (heights, size, tiling, position) changes; unique across terrains.
        uint64_t Revision = 0;
    };

    // -----------------------------------------------------------------------
    // Mesh / Limb / Animation / Object3D
    // -----------------------------------------------------------------------

    struct BoneInfluence
    {
        std::array<int, 4> Indices{-1, -1, -1, -1};
        std::array<float, 4> Weights{};
    };

    struct Mesh
    {
        std::vector<Vec3> Vertices;
        std::vector<Vec3> Normals;
        std::vector<SDL_FPoint> Uvs;
        std::unordered_map<int, std::vector<SDL_FPoint>> UvStages;
        std::vector<uint32_t> DiffuseColors;
        std::vector<uint32_t> Indices;
        int TextureImageId = 0;
        std::unordered_map<int, int> TextureStageImageIds;
        std::unordered_map<int, int> TextureStageAddressModes;
        std::unordered_map<int, int> TextureStageMipModes;
        std::unordered_map<int, int> TextureStageBlendModes;
        std::unordered_map<int, int> TextureStageCoordinateModes;
        std::unordered_map<int, bool> TextureStageCubeModes;
        int BuiltInEffect = 0; // 1 = bump, 2 = cartoon, 3 = rainbow
        int EffectId = -1;
        int SourceMeshIndex = -1;
        bool Visible = true;
        bool TextureUsesMaterialColor = false;
        SDL_FColor MaterialColor{1.0f, 1.0f, 1.0f, 1.0f};
        SDL_FColor LimbTint{1.0f, 1.0f, 1.0f, 1.0f}; // COLOR LIMB
        float LimbRotX = 0.0f;
        float LimbRotY = 0.0f;
        float LimbRotZ = 0.0f;
        float LimbOffX = 0.0f;
        float LimbOffY = 0.0f;
        float LimbOffZ = 0.0f;
        float LimbScaleX = 1.0f;
        float LimbScaleY = 1.0f;
        float LimbScaleZ = 1.0f;
        aiMatrix4x4 BindTransform;
        std::vector<BoneInfluence> BoneInfluences;
        std::vector<const aiNode*> BoneNodes;
        std::vector<aiMatrix4x4> BoneOffsets;
    };

    struct LimbTransform
    {
        float RotX = 0.0f;
        float RotY = 0.0f;
        float RotZ = 0.0f;
        float OffX = 0.0f;
        float OffY = 0.0f;
        float OffZ = 0.0f;
        float ScaleX = 1.0f;
        float ScaleY = 1.0f;
        float ScaleZ = 1.0f;
        bool Hidden = false;
        int ParentLimb = -1; // LINK LIMB: -1 = root (default)
    };

    struct ObjectKeyframe
    {
        std::vector<LimbTransform> Limbs;
    };

    struct AnimationClip
    {
        int StartFrame = 0;
        int EndFrame = 0;
        double DurationTicks = 0.0;
        double TicksPerSecond = 25.0;
        std::unordered_map<std::string, const aiNodeAnim*> Channels;
    };

    struct Object3D
    {
        std::vector<Mesh> Meshes;
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;
        float RotX = 0.0f;
        float RotY = 0.0f;
        float RotZ = 0.0f;
        float ScaleX = 1.0f;
        float ScaleY = 1.0f;
        float ScaleZ = 1.0f;
        float TintR = 1.0f;
        float TintG = 1.0f;
        float TintB = 1.0f;
        float Alpha = 1.0f;
        float FadeNormalScale = 1.0f; // FADE OBJECT: multiplies normals (0=ambient only, 1=normal, 2=2x bright)
        float TexScrollU = 0.0f;
        float TexScrollV = 0.0f;
        float Speed = 0.0f;
        int Frame = 0;
        int Interpolation = 0;
        int TextureImageId = 0;
        int EffectId = 0;
        std::shared_ptr<SDL_GPUTexture> CubeGpuTexture{};
        bool Playing = false;
        bool Looping = false;
        bool HasPlayRange = false;
        int PlayStartFrame = 0;
        int PlayEndFrame = 0;
        bool LockToScreen = false;
        bool Ghosted = false;
        int GhostMode = 0; // 0 = additive (SRC_ALPHA/ONE), 1 = alpha blend
        bool CollisionEnabled = true;
        CollisionShape CollisionShapeValue = CollisionShape::Sphere;
        AABB CollisionAabb{};
        bool CollisionAabbExplicit = false;
        bool CollisionBoxRotates = false;
        bool Visible = true;
        bool Wireframe = false;
        bool TransparentBlack = false;
        int TransparencyMode = -1;
        bool Cull = true;
        bool ZdepthDisabled = false;
        bool DepthTest = true;
        bool DepthWrite = true;
        bool DrawBounds = false;
        bool BoundsBoxOnly = false;
        bool AlphaMapping = false;
        float AlphaMappingAlpha = 1.0f;
        bool UseLighting = true;
        bool UseFog = true;
        bool UseAmbient = true;
        bool TextureFilter = true;
        bool ReflectionShading = false;
        bool ShadowShading = false;
        bool ShadowConfigured = false;
        int ShadowMeshId = 0;
        float ShadowRange = 2000.0f;
        bool ShadowUseShader = false;
        std::vector<Mesh> ShadowMeshes;
        float TexScaleU = 1.0f;
        float TexScaleV = 1.0f;
        bool ApplyPivot = false;
        bool UseZyxRotation = false; // SET OBJECT ROTATION ZYX (default = XYZ)
        Mat3 PivotMatrix{};
        int GluedTargetObject = -1;
        int GluedTargetLimb = 0;
        int GlueMode = 0;
        // Imported scenes are never modified after loading, so clones share them with their source.
        std::shared_ptr<const Assimp::Importer> ImporterValue;
        const aiScene* AssimpScene = nullptr;
        std::string ModelStructureSignature;
        std::shared_ptr<const aiNode> CompatibilityRootNode;
        const aiNode* CompatibilitySkippedRootNode = nullptr;
        std::vector<std::shared_ptr<const Assimp::Importer>> AnimationImporters;
        std::unordered_map<unsigned int, const aiNode*> MeshNodeByIndex;
        aiMatrix4x4 RootInverseTransform;
        std::vector<const aiNode*> LimbNodes;
        std::unordered_map<const aiNode*, int> LimbIndexByNode;
        std::vector<LimbTransform> LimbTransforms;
        std::unordered_map<std::string, const aiNodeAnim*> AnimChannelByNode;
        std::vector<AnimationClip> AnimationClips;
        std::map<int, ObjectKeyframe> ObjectKeyframes;
        bool ImportedAnimationEnabled = true;
        int ActiveAnimationClip = 0;
        double ActiveAnimDurationTicks = 0.0;
        double ActiveAnimTicksPerSec = 25.0;
        double AnimTimeSeconds = 0.0;
        int AnimIndex = 0;

        // Material properties (DBPro)
        uint32_t DiffuseColor = 0xFFFFFFFF;
        uint32_t AmbientColor = 0xFFFFFFFF;
        uint32_t SpecularColor = 0x00000000;
        uint32_t EmissiveColor = 0x00000000;
        float SpecularPower = 10.0f;

        // Per-object bounding sphere radius in local-space (from origin, unscaled).
        // Historical collision/object-size state; user commands may override it.
        float BoundingRadius = -1.0f;

        // Geometry-derived local-space radius used only for render visibility.
        // -1.0f = not yet computed from the current meshes.
        float RenderBoundingRadius = -1.0f;

        // Dirty flag — set whenever pos/rot/scale changes.
        // Cleared by the renderer after processing.
        mutable bool TransformDirty = true;

        // Cached node-global transforms for the animation skeleton.
        // Invalidated when time_ticks or active_animation_clip changes.
        mutable double CachedAnimTicks = std::numeric_limits<double>::quiet_NaN();
        mutable int CachedAnimClip = -1;
        mutable std::unordered_map<const aiNode*, aiMatrix4x4> CachedNodeGlobals;
    };

    struct AssimpImportResult
    {
        std::unique_ptr<Assimp::Importer> ImporterValue;
        const aiScene* Scene = nullptr;
        uint64_t TriangleCount = 0;
    };

    // -----------------------------------------------------------------------
    // Light
    // -----------------------------------------------------------------------

    struct Light3D
    {
        int Type = 3;
        bool Visible = true;
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;
        float RotX = 0.0f;
        float RotY = 0.0f;
        float RotZ = 0.0f;
        float DirX = 0.0f;
        float DirY = -1.0f;
        float DirZ = 1.0f;
        float Range = 1000.0f;
        int Color = 0x00FFFFFF;
        float SpotInner = 30.0f;
        float SpotOuter = 60.0f;
    };

    // -----------------------------------------------------------------------
    // Text / font
    // -----------------------------------------------------------------------

    struct TextFontRequest
    {
        std::string FontName = "Arial";
        int Size = 16;
        int Style = TTF_STYLE_NORMAL;
        bool Opaque = false;
    };

    struct TextFontResolved
    {
        std::string SourceName{};
        std::string FamilyName{};
        bool UsingFallback = false;
    };

    struct TextGlyph
    {
        float U0 = 0.0f;
        float V0 = 0.0f;
        float U1 = 0.0f;
        float V1 = 0.0f;
        int Width = 0;
        int Height = 0;
        int Minx = 0;
        int Maxx = 0;
        int Miny = 0;
        int Maxy = 0;
        int Advance = 0;
    };

    struct TextAtlas
    {
        SDL_Surface* Surface = nullptr;
        SDL_GPUTexture* GpuTexture = nullptr;
        int Width = 1024;
        int Height = 1024;
        int NextX = 1;
        int NextY = 1;
        int RowH = 0;
        bool Dirty = false;
        std::unordered_map<std::string, TextGlyph> Glyphs;
    };

    struct DebugTextLine
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::string Value;
        int Color = 0x00FFFFFF;
        TextFontRequest Request{};
    };

    // -----------------------------------------------------------------------
    // 2D primitives
    // -----------------------------------------------------------------------

    enum class Primitive2DType
    {
        Dot,
        Line,
        Box,
        Circle,
        Ellipse,
    };

    struct Primitive2D
    {
        Primitive2DType Type = Primitive2DType::Dot;
        int X1 = 0;
        int Y1 = 0;
        int X2 = 0;
        int Y2 = 0;
        int Color = 0x00FFFFFF;
        // Filled boxes may shade each corner (DBPro BOX): top-left, top-right, bottom-right, bottom-left.
        int CornerColors[4] = {0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF};
    };

    // -----------------------------------------------------------------------
    // GPU render types
    // -----------------------------------------------------------------------

    // Per-surface rendering choices set by SET OBJECT / SET MATRIX: they select the pipeline,
    // the sampler and the fog/ambient uniforms of the batch that carries the surface.
    struct SurfaceStyle
    {
        bool Wireframe = false;
        bool Fog = true;
        bool BlackFog = false;
        bool Ambient = true;
        bool Filter = true;
        std::array<int, 8> TextureBlendModes{4, 0, 0, 0, 0, 0, 0, 0};
        std::array<int, 8> TextureAddressModes{};
        std::array<int, 8> TextureMipModes{2, 2, 2, 2, 2, 2, 2, 2};
        std::array<bool, 8> TextureCubeModes{};
        int BuiltInEffect = 0;
        int EffectId = 0;
        uint32_t DiffuseColor = 0xFFFFFFFF;
        uint32_t AmbientColor = 0xFFFFFFFF;
        uint32_t SpecularColor = 0x00000000;
        uint32_t EmissiveColor = 0x00000000;
        float SpecularPower = 10.0f;
        float AlphaTestThreshold = 0.0f;
        float AlphaOverride = -1.0f;
        bool ReflectionSurface = false;
        bool ReflectionPass = false;
        bool ReflectionOverlay = false;
        bool Backdrop = false;
        bool ShadowCaster = false;
        float ShadowRange = 2000.0f;
        bool ShadowVolume = false;
        bool ShadowOverlay = false;

        bool operator==(const SurfaceStyle& Other) const
        {
            return Wireframe == Other.Wireframe && Fog == Other.Fog && BlackFog == Other.BlackFog &&
                   Ambient == Other.Ambient && Filter == Other.Filter && TextureBlendModes == Other.TextureBlendModes &&
                   TextureAddressModes == Other.TextureAddressModes && TextureMipModes == Other.TextureMipModes &&
                   TextureCubeModes == Other.TextureCubeModes && BuiltInEffect == Other.BuiltInEffect &&
                   EffectId == Other.EffectId && ReflectionSurface == Other.ReflectionSurface &&
                   ReflectionPass == Other.ReflectionPass && ReflectionOverlay == Other.ReflectionOverlay &&
                   Backdrop == Other.Backdrop && ShadowCaster == Other.ShadowCaster &&
                   ShadowRange == Other.ShadowRange && ShadowVolume == Other.ShadowVolume &&
                   ShadowOverlay == Other.ShadowOverlay;
        }
    };

    struct Triangle
    {
        SDL_Texture* Texture = nullptr;
        SDL_GPUTexture* GpuTexture = nullptr;
        float Depth = 0.0f;
        bool Transparent = false;
        bool AlphaDepthWrite = false;
        bool Additive = false;
        bool VeryEarly = false;
        int MatrixGhostMode = -1;
        bool DepthTest = true;
        bool DepthWrite = true;
        int RenderPriority = 0;
        bool Is2d = false;
        SurfaceStyle Style{};
        float Pos[3][3]{};
        float Normal[3][3]{};
        SDL_FColor Color[3]{};
        SDL_FPoint Uv[3]{};
        std::array<SDL_GPUTexture*, 7> AuxiliaryGpuTextures{};
        std::array<std::array<SDL_FPoint, 3>, 7> AuxiliaryUvs{};
        float CubeDirection[3][3]{};
        SDL_GPUTexture* CubeGpuTexture = nullptr;
    };

    struct GpuVertex
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 1.0f;
        float R = 1.0f;
        float G = 1.0f;
        float B = 1.0f;
        float A = 1.0f;
        float U = 0.0f;
        float V = 0.0f;
        float BoxTrR = 0.0f;
        float BoxTrG = 0.0f;
        float BoxTrB = 0.0f;
        float BoxTrA = 0.0f;
        float BoxBrR = 0.0f;
        float BoxBrG = 0.0f;
        float BoxBrB = 0.0f;
        float BoxBrA = 0.0f;
        float BoxBlR = 0.0f;
        float BoxBlG = 0.0f;
        float BoxBlB = 0.0f;
        float BoxBlA = 0.0f;
        float AuxiliaryUvs[7][2]{};
        float CubeDirection[3]{};
    };

    struct GpuBatch
    {
        SDL_GPUTexture* Texture = nullptr;
        Uint32 FirstVertex = 0;
        Uint32 VertexCount = 0;
        bool Transparent = false;
        bool AlphaDepthWrite = false;
        bool Additive = false;
        bool Is2d = false;
        bool Sprite = false;
        int MatrixGhostMode = -1;
        bool DepthTest = true;
        bool DepthWrite = true;
        SurfaceStyle Style{};
        std::array<SDL_GPUTexture*, 7> AuxiliaryTextures{};
        SDL_GPUTexture* CubeTexture = nullptr;
        // Static world-space terrain: drawn from its own vertex buffer with the view's terrain indices.
        SDL_GPUBuffer* StaticVertices = nullptr;
        Uint32 FirstIndex = 0;
        Uint32 IndexCount = 0;
    };

    struct ViewVertex
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 0.0f;
        SDL_FPoint Uv{};
        SDL_FColor Color{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<SDL_FPoint, 7> AuxiliaryUvs{};
        float CubeX = 0.0f;
        float CubeY = 0.0f;
        float CubeZ = 0.0f;
    };

    struct GpuVertexUniforms
    {
        float ViewProj[16]{};
    };

    struct GpuFragmentUniforms
    {
        float DirLightDirection[4]{};
        float DirLightColor[4]{};
        float AmbientColor[4]{};
        float FogColorDist[4]{};
        float Flags[4]{};
        int32_t LightCounts[4]{};
        float PointLights[24][4]{};
        float SpotLights[18][4]{};
    };

    static_assert(sizeof(GpuFragmentUniforms) == 768, "UBO size mismatch");

    struct GpuTextureStageUniforms
    {
        int32_t Stages[8][4]{};
    };

    static_assert(sizeof(GpuTextureStageUniforms) == 128, "texture-stage UBO size mismatch");

} // namespace dbc
