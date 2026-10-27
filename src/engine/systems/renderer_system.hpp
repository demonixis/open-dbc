#pragma once

#include "../engine_math.hpp"
#include "../engine_types.hpp"
#include "render_view.hpp"
#include "terrain_mesh.hpp"
#include <optional>
#include <span>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_FPoint;
struct SDL_Texture;
struct SDL_GPUTexture;

struct aiNode;

namespace dbc
{
    class EngineKernel;

    struct PreparedViewGeometry
    {
        std::vector<GpuVertex> Vertices;
        std::vector<GpuBatch> Batches;
        std::vector<Triangle> World, Reflected, Overlay, ShadowSources, Shadows;
        SDL_FColor ClearColor{};
        Uint32 VertexOffset = 0;
        // Indices into static terrain vertex buffers, uploaded once per view at TerrainIndexOffset.
        std::vector<uint32_t> TerrainIndices;
        Uint32 TerrainIndexOffset = 0;
        bool HasReflection = false;
        bool HasShadow = false;
    };

    class RendererSystem
    {
      public:
        explicit RendererSystem(EngineKernel& Kernel);
        ~RendererSystem();

        // Main render entry point (called by CoreSystem::Sync)
        // retain_2d keeps transient pasted sprites and Classic screen text alive
        // while execution is blocked. Screen primitives and image pastes live
        // persistently in bitmap 0 until CLS and do not depend on this flag.
        void RenderScene(bool Retain2d = false);
        void PrepareFrameSimulation(double DeltaSeconds);
        [[nodiscard]] RenderFrameState PrepareFrame(double DeltaSeconds, bool Retain2d = false, bool HudLast = false);
        [[nodiscard]] RenderView CameraView(Uint32 PhysicalWidth, Uint32 PhysicalHeight) const;
        bool RenderPreparedView(const RenderView& View, const GpuRenderTarget& Target, SDL_GPUCommandBuffer* Commands,
                                RenderFrameState& Frame);
        bool BuildPreparedView(const RenderView& View, PreparedViewGeometry& Geometry, RenderFrameState& Frame);
        bool UploadPreparedViews(std::span<PreparedViewGeometry> Geometry, SDL_GPUCommandBuffer* Commands);
        bool RecordPreparedView(const RenderView& View, const GpuRenderTarget& Target,
                                const PreparedViewGeometry& Geometry, SDL_GPUCommandBuffer* Commands,
                                RenderFrameState& Frame);
        void FinalizeFrame(RenderFrameState& Frame);
        [[nodiscard]] const RenderView* ActiveView() const
        {
            return ActiveView_;
        }
        void SetActiveView(const RenderView* View)
        {
            if (View && (!CachedView_ || !(*CachedView_ == *View)))
            {
                InvalidateTriCache();
                CachedView_.emplace(*View);
            }
            ActiveView_ = View;
        }

        // Renders a frame into an offscreen target and reads it back, so the
        // script can grab what is on screen (GET IMAGE / SAVE BITMAP from
        // bitmap 0). Caller owns the returned surface. Null on failure.
        [[nodiscard]] SDL_Surface* CaptureScreen();
        // Headless frames are never presented; reads back the last one still held offscreen.
        [[nodiscard]] SDL_Surface* ReadLastHeadlessFrame();

        void SetGamma(int Red, int Green, int Blue);
        [[nodiscard]] std::array<int, 3> GammaLevels() const
        {
            return GammaLevels_;
        }
        [[nodiscard]] bool HasFinalColorTransform() const;
        [[nodiscard]] std::array<float, 3> FinalColorScale() const;
        bool SetMipmapMode(int Mode, std::string* Error = nullptr);
        [[nodiscard]] bool HasMipmapModeOverride() const
        {
            return MipmapModeOverride_;
        }
        [[nodiscard]] int MipmapMode() const
        {
            return MipmapMode_;
        }
        [[nodiscard]] int ResolveMipmapMode(int SurfaceMode) const;
        void ResetRuntimeCompatibilityState();

        // Object helpers (called by ObjectManager)
        Mesh* FindLimbMesh(Object3D& Object, int LimbId);
        const Mesh* FindLimbMesh(const Object3D& Object, int LimbId);
        Vec3 WorldObjectOrigin(const Object3D& Object);
        Vec3 WorldObjectPoint(const Object3D& Object, const Vec3& Point);
        Vec3 WorldLimbOrigin(const Object3D& Object, int LimbId);
        Vec3 ObjectMeshPoint(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point);
        Vec3 ObjectMeshVertex(const Object3D& Object, const Mesh& MeshValue, std::size_t VertexIndex);
        void ObjectMeshVertices(const Object3D& Object, const Mesh& MeshValue, std::vector<Vec3>& Vertices);
        Vec3 WorldMeshPoint(const Object3D& Object, const Mesh& MeshValue, const Vec3& Point);
        Vec3 WorldMeshVertex(const Object3D& Object, const Mesh& MeshValue, std::size_t VertexIndex);
        bool ProjectPoint(float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth);
        bool ProjectPointLocked(float Wx, float Wy, float Wz, SDL_FPoint& OutPos, float& OutDepth);

        // View-space transforms
        Vec3 WorldToViewPoint(float Wx, float Wy, float Wz);
        Vec3 WorldToViewDirection(float Dx, float Dy, float Dz);
        void SetReflectionView(const Vec3& Point, const Vec3& Normal);
        void ClearReflectionView();
        [[nodiscard]] bool ReflectionViewEnabled() const
        {
            return ReflectionViewEnabled_;
        }
        [[nodiscard]] Vec3 ReflectWorldPoint(const Vec3& Point) const;
        [[nodiscard]] Vec3 ReflectWorldDirection(const Vec3& Direction) const;
        void SetShadowPosition(int Mode, float X, float Y, float Z);
        [[nodiscard]] int ShadowPositionMode() const
        {
            return ShadowPositionMode_;
        }
        [[nodiscard]] Vec3 ShadowPosition() const
        {
            return ShadowPosition_;
        }
        float FogFactor(float Z);
        void AppendClippedViewTriangle(const ViewVertex& V0, const ViewVertex& V1, const ViewVertex& V2,
                                       SDL_Texture* Texture, SDL_GPUTexture* GpuTexture, bool Transparent,
                                       std::vector<Triangle>& Tris, bool Additive = false);
        GpuFragmentUniforms BuildFragmentUniforms();
        SDL_GPUTexture* ImageGpuTexture(Image& ImageValue, bool TransparentBlack);

        // Mesh construction (static — pure functions)
        static void AddTriangle(Mesh& MeshValue, const Vec3& A, const Vec3& B, const Vec3& C,
                                SDL_FPoint Ta = SDL_FPoint{0.0f, 0.0f}, SDL_FPoint Tb = SDL_FPoint{1.0f, 0.0f},
                                SDL_FPoint Tc = SDL_FPoint{1.0f, 1.0f});
        static void RegenerateMeshNormals(Mesh& MeshValue);
        static Mesh MakePlainMesh(float Width, float Height);
        static Mesh MakeBoxMesh(float Width, float Height, float Depth);
        static Mesh MakeSphereMesh(float Radius, int Rings, int Segments);
        static Mesh MakeCylinderMesh(float Radius, float Height, int Segments, bool TopCap, bool BottomCap);
        static Mesh MakeConeMesh(float Radius, float Height, int Segments);

        // Assimp import helpers
        static void BuildMeshNodeMapRecursive(const aiNode* Node,
                                              std::unordered_map<unsigned int, const aiNode*>& OutMap);
        static void BuildLimbMapRecursive(const aiNode* Node, std::vector<const aiNode*>& OutLimbNodes,
                                          std::unordered_map<const aiNode*, int>& OutLimbIndexByNode);
        static aiMatrix4x4 EvaluateImportedNodeLocalTransform(const Object3D& Object, const aiNode* Node,
                                                              const AnimationClip& Clip, double TimeTicks);
        static AssimpImportResult ImportSceneWithFlags(const std::string& Path, unsigned int Flags);

        // Scene triangle cache — reused when no objects/camera changed.
        [[nodiscard]] bool HasCachedTris() const
        {
            return HasCachedTris_;
        }
        void InvalidateTriCache()
        {
            HasCachedTris_ = false;
        }
        const std::vector<Triangle>& CachedWorldTris() const
        {
            return CachedWorldTris_;
        }
        const std::vector<Triangle>& CachedReflectedTris() const
        {
            return CachedReflectedTris_;
        }
        const std::vector<Triangle>& CachedOverlayTris() const
        {
            return CachedOverlayTris_;
        }
        void CacheTris(const std::vector<Triangle>& World, const std::vector<Triangle>& Reflected,
                       const std::vector<Triangle>& Overlay)
        {
            CachedWorldTris_ = World;
            CachedReflectedTris_ = Reflected;
            CachedOverlayTris_ = Overlay;
            HasCachedTris_ = true;
        }
        void CacheTris(const std::vector<Triangle>& World, const std::vector<Triangle>& Overlay)
        {
            CacheTris(World, {}, Overlay);
        }

        // Terrains drawn from static GPU buffers rather than per-frame CPU triangles.
        [[nodiscard]] bool DrawsTerrainOnGpu(const Matrix& Terrain) const;
        // Triangles of static terrain in the last built view.
        [[nodiscard]] std::size_t LastStaticTerrainTriangles() const
        {
            return LastStaticTerrainTriangles_;
        }

        // STATISTIC(1): triangles sent to the GPU by the last presented frame.
        [[nodiscard]] int LastFramePolygons() const
        {
            return LastFramePolygons_;
        }
        void RecordFramePolygons(int Count)
        {
            LastFramePolygons_ = Count;
        }
        // True when the last presented frame contained 3D world geometry.
        [[nodiscard]] bool LastFrameShowedWorld() const
        {
            return LastFrameShowedWorld_;
        }
        // STATISTIC(5): draw calls submitted by the last presented frame.
        [[nodiscard]] int LastFrameDrawCalls() const
        {
            return LastFrameDrawCalls_;
        }
        void RecordFrameDrawCalls(int Count)
        {
            LastFrameDrawCalls_ = Count;
        }
        void SetNormalizeNormals(bool Enabled)
        {
            NormalizeNormals_ = Enabled;
            InvalidateTriCache();
        }
        [[nodiscard]] bool NormalizeNormals() const
        {
            return NormalizeNormals_;
        }

        void ReportRenderError(const std::string& Error);

      private:
        struct StaticTerrain
        {
            uint64_t Revision = 0;
            TerrainMesh Mesh;
            SDL_GPUBuffer* Vertices = nullptr;
            bool PendingUpload = false;
        };

        void AppendStaticTerrains(const RenderView& View, PreparedViewGeometry& Geometry);
        bool UploadStaticTerrains(std::span<PreparedViewGeometry> Geometry, SDL_GPUCommandBuffer* Commands);
        void ReleaseStaticTerrains();

        EngineKernel& Kernel_;
        bool RenderFailureReported_ = false;
        PreparedViewGeometry FrameHud_;
        const RenderView* ActiveView_ = nullptr;
        std::optional<RenderView> CachedView_{};
        std::vector<Triangle> CachedWorldTris_;
        std::vector<Triangle> CachedReflectedTris_;
        std::vector<Triangle> CachedOverlayTris_;
        Vec3 ReflectionPoint_{};
        Vec3 ReflectionNormal_{0.0f, 1.0f, 0.0f};
        bool ReflectionViewEnabled_ = false;
        int ShadowPositionMode_ = 0;
        Vec3 ShadowPosition_{};
        bool HasCachedTris_ = false;
        int LastFramePolygons_ = 0;
        bool LastFrameShowedWorld_ = false;
        int LastFrameDrawCalls_ = 0;
        bool NormalizeNormals_ = true;
        std::array<int, 3> GammaLevels_{255, 255, 255};
        bool MipmapModeOverride_ = false;
        int MipmapMode_ = 2;
        std::unordered_map<int, StaticTerrain> StaticTerrains_;
        SDL_GPUBuffer* TerrainIndexBuffer_ = nullptr;
        Uint32 TerrainIndexCapacity_ = 0;
        SDL_GPUBuffer* TerrainInstanceBuffer_ = nullptr;
        bool TerrainInstanceUploaded_ = false;
        SDL_GPUTransferBuffer* TerrainTransfer_ = nullptr;
        Uint32 TerrainTransferCapacity_ = 0;
        std::size_t LastStaticTerrainTriangles_ = 0;
    };

} // namespace dbc
