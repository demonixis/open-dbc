#pragma once

// What the renderer's source files share with each other, not with the rest of the engine.

#include "renderer_system.hpp"

namespace dbc
{
    struct SpriteData;

    // One channel of a packed colour, from 0 to 1.
    [[nodiscard]] inline float PackedChannel(uint32_t Packed, unsigned Shift)
    {
        return static_cast<float>((Packed >> Shift) & 0xFFu) / 255.0f;
    }

    // Node animation: renderer_animation.cpp.
    aiMatrix4x4 EvaluateNodeLocalTransform(const Object3D& Object, const aiNode* Node, double TimeTicks,
                                           const std::unordered_map<std::string, const aiNodeAnim*>& Channels,
                                           double DurationTicks);
    aiMatrix4x4 EvaluateNodeLocalTransform(const Object3D& Object, const aiNode* Node, double TimeTicks);
    void BuildMeshNodeMapRecursive(const aiNode* Node, std::unordered_map<unsigned int, const aiNode*>& OutMap);
    void BuildLimbMapRecursive(const aiNode* Node, std::vector<const aiNode*>& OutLimbNodes,
                               std::unordered_map<const aiNode*, int>& OutLimbIndexByNode);
    void BuildObjectNodeGlobalMap(const Object3D& Object, double TimeTicks,
                                  std::unordered_map<const aiNode*, aiMatrix4x4>& OutGlobals);
    void PrepareFrameSimulation(EngineKernel& K, double DtSeconds);

    // Model import: renderer_model_import.cpp.
    [[nodiscard]] AssimpImportResult ImportSceneWithFlags(const std::string& Path, unsigned int Flags);

    // Primitive meshes: renderer_primitives.cpp.
    void AddTriangle(Mesh& MeshValue, const Vec3& A, const Vec3& B, const Vec3& C,
                     SDL_FPoint Ta = SDL_FPoint{0.0f, 0.0f}, SDL_FPoint Tb = SDL_FPoint{1.0f, 0.0f},
                     SDL_FPoint Tc = SDL_FPoint{1.0f, 1.0f});
    void RegenerateMeshNormals(Mesh& MeshValue);
    Mesh MakePlainMesh(float Width, float Height);
    Mesh MakeBoxMesh(float Width, float Height, float Depth);
    Mesh MakeSphereMesh(float Radius, int Rings, int Segments);
    Mesh MakeCylinderMesh(float Radius, float Height, int Segments, bool TopCap, bool BottomCap);
    Mesh MakeConeMesh(float Radius, float Height, int Segments);

    // Object and limb transforms: renderer_transforms.cpp.
    [[nodiscard]] Vec3 TransformObjectSpacePoint(EngineKernel& K, const Object3D& Object, const Vec3& Local);
    [[nodiscard]] Vec3 TransformObjectSpaceVector(EngineKernel& K, const Object3D& Object, const Vec3& Local);
    [[nodiscard]] Vec3 WorldObjectOrigin(EngineKernel& K, const Object3D& Object);
    [[nodiscard]] const aiMatrix4x4* CurrentImportedMeshTransform(const Object3D& Object, const Mesh& MeshValue);
    [[nodiscard]] Vec3 TransformMeshPointInObject(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point,
                                                  const aiMatrix4x4* ImportedTransform, bool ObjectSpace);
    [[nodiscard]] Vec3 WorldLimbOrigin(EngineKernel& K, const Object3D& Object, int LimbId);
    [[nodiscard]] std::vector<aiMatrix4x4> BuildBonePalette(
        const Object3D& Object, const Mesh& MeshValue,
        const std::unordered_map<const aiNode*, aiMatrix4x4>& NodeGlobals);
    [[nodiscard]] bool SkinMeshVertex(const Mesh& MeshValue, std::size_t VertexIndex,
                                      const std::vector<aiMatrix4x4>& BonePalette, Vec3& Point, Vec3& Normal);
    const Mesh* FindLimbMesh(const Object3D& Object, int LimbId);
    Mesh* FindLimbMesh(Object3D& Object, int LimbId);

    // Stencil shadow volumes: renderer_shadows.cpp.
    // Those of every caster's whole mesh: one out of frame, behind the camera or turned away from it still
    // shadows what is in frame.
    void AppendCasterVolumes(EngineKernel& K, std::vector<Triangle>& Volumes);
    // Those of objects locked to the screen, from the view-space triangles they draw.
    void BuildShadowVolumes(EngineKernel& K, const std::vector<Triangle>& Sources, std::vector<Triangle>& Volumes);

    // Object meshes: renderer_objects.cpp.
    bool NodeHiddenByLimb(const Object3D& Object, const aiNode* Node);
    [[nodiscard]] bool ObjectsNeedDepthOrder(EngineKernel& K);
    void RenderObjects(EngineKernel& K, std::vector<Triangle>& WorldTris, std::vector<Triangle>& OverlayTris,
                       SceneTriangles* Counts = nullptr, bool GpuMeshes = false);

    // Matrices, BSP worlds, object bounds and particles: renderer_world.cpp.
    void RenderMatrix(EngineKernel& K, const Matrix& MatrixValue, std::vector<Triangle>& Tris);
    void RenderBspWorld(EngineKernel& K, std::vector<Triangle>& Tris);
    void RenderObjectBounds(EngineKernel& K, const Object3D& Object, std::vector<Triangle>& Tris);
    void RenderParticles(EngineKernel& K, std::vector<Triangle>& WorldTris);

    // GPU vertices and batches: renderer_batches.cpp.
    [[nodiscard]] SDL_GPUTexture* WhiteGpuTexture(EngineKernel& K);
    [[nodiscard]] SDL_GPUTexture* ImageGpuTexture(EngineKernel& K, Image& ImageValue, bool TransparentBlack);
    void FillSurfaceMaterial(GpuVertex& Out, const SurfaceStyle& Style);
    void AppendGpuTriangle(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                           SDL_GPUTexture* Texture, const float Pos[3][3], const float Normal[3][3],
                           const SDL_FColor Color[3], const SDL_FPoint Uv[3], bool Transparent = false,
                           bool Is2d = false, bool Additive = false, SurfaceStyle Style = {},
                           const SDL_FColor* BoxColors = nullptr, bool Sprite = false, bool AlphaDepthWrite = false,
                           int MatrixGhostMode = -1, bool DepthWrite = true, bool DepthTest = true,
                           const std::array<SDL_GPUTexture*, 7>& AuxiliaryTextures = {},
                           SDL_GPUTexture* CubeTexture = nullptr,
                           const std::array<std::array<SDL_FPoint, 3>, 7>* AuxiliaryUvs = nullptr,
                           const float (*CubeDirections)[3] = nullptr);
    void AppendGpuQuad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                       SDL_GPUTexture* Texture, float X, float Y, float W, float H, SDL_FColor Color, float Z = 0.0f,
                       bool Transparent = false, bool Is2d = true, SurfaceStyle Style = {});
    void AppendGpuQuadUv(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                         SDL_GPUTexture* Texture, float X, float Y, float W, float H, SDL_FColor Color,
                         const SDL_FPoint& Uv0, const SDL_FPoint& Uv1, float Z = 0.0f, bool Transparent = false,
                         bool Is2d = true, SurfaceStyle Style = {});
    void AppendGpuSpriteQuad(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches,
                             SDL_GPUTexture* Texture, const SpriteData& Sprite, float Width, float Height,
                             SDL_FColor Color, bool Transparent);
    void AppendGpuLine(EngineKernel& K, std::vector<GpuVertex>& Vertices, std::vector<GpuBatch>& Batches, float X1,
                       float Y1, float X2, float Y2, SDL_FColor Color, float Thickness = 1.0f, float Z = 0.0f,
                       bool Transparent = false, bool Is2d = true);

    // The 2D layer: renderer_hud.cpp.
    // Puts the glyphs of this frame's text in the atlas, before anything is drawn with it.
    void EnsureHudGlyphs(EngineKernel& K);
    // Screen primitives, text, pasted images and sprites.
    void AppendHudLayer(EngineKernel& K, std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches);

    // GPU mesh draws: renderer_gpu_meshes.cpp.
    void GroupGpuMeshDraws(std::vector<GpuMeshDraw>& Draws);

    // View transforms, clipping and fragment uniforms: renderer_view.cpp.
    // The view being built or, outside a frame, the camera's, which Storage then holds.
    [[nodiscard]] const RenderView& ActiveOrCameraView(EngineKernel& K, std::optional<RenderView>& Storage);
    [[nodiscard]] Vec3 ActiveCameraPosition(EngineKernel& K);
    [[nodiscard]] float FogFactor(EngineKernel& K, float Z);

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

    [[nodiscard]] Vec3 WorldToViewPoint(EngineKernel& K, float Wx, float Wy, float Wz);
    [[nodiscard]] Vec3 WorldToViewDirection(EngineKernel& K, float Dx, float Dy, float Dz);
    [[nodiscard]] GpuFragmentUniforms BuildFragmentUniforms(EngineKernel& K);
    [[nodiscard]] bool ProjectPoint(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth);
    [[nodiscard]] bool ProjectPointLocked(EngineKernel& K, float Wx, float Wy, float Wz, SDL_FPoint& OutPos,
                                          float& OutDepth);
    void AppendClippedViewTriangle(EngineKernel& K, const ViewVertex& V0, const ViewVertex& V1, const ViewVertex& V2,
                                   SDL_GPUTexture* GpuTexture, bool Transparent, std::vector<Triangle>& Tris,
                                   bool Additive = false, const SurfaceStyle& Style = {}, int AlphaPhase = -1,
                                   bool VeryEarly = false, float DepthBias = 0.0f, int MatrixGhostMode = -1,
                                   int RenderPriority = 0, bool DepthWrite = true, bool DepthTest = true,
                                   const std::array<SDL_GPUTexture*, 7>& AuxiliaryGpuTextures = {},
                                   SDL_GPUTexture* CubeGpuTexture = nullptr,
                                   std::optional<uint32_t> StyleHash = std::nullopt);
} // namespace dbc
