#pragma once

#include "../engine_math.hpp"
#include "../engine_types.hpp"
#include "render_view.hpp"
#include "shadow_mesh.hpp"
#include "terrain_mesh.hpp"
#include "triangle_order.hpp"
#include <optional>
#include <span>

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
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

    // An opaque object mesh drawn from GPU buffers instead of CPU triangles: its instance record, in world space
    // so that every view can draw it, and the state that decides which draws can share one instanced call.
    struct GpuMeshDraw
    {
        std::shared_ptr<const MeshGeometry> Geometry;
        GpuMeshInstance Instance{};
        bool Cull = true;
        SDL_GPUTexture* Texture = nullptr;
        SurfaceStyle Style{};
        uint32_t StyleHash = 0;
    };

    // The scene of one view, shared by the views that draw it and the renderer's cache.
    struct SceneTriangles
    {
        OrderedTriangles World;
        OrderedTriangles Reflected;
        OrderedTriangles Overlay;
        // Grouped by geometry, texture and surface, so each group is one instanced draw.
        std::vector<GpuMeshDraw> MeshDraws;
        // Objects the world pass went through, those of them shown, and those of these outside the view.
        int Objects = 0;
        int VisibleObjects = 0;
        int CulledObjects = 0;
    };

    struct PreparedViewGeometry
    {
        std::vector<GpuVertex> Vertices;
        std::vector<GpuBatch> Batches;
        // Shared with the renderer's cache, so a reused scene is never copied.
        std::shared_ptr<const SceneTriangles> Scene;
        // The scene's GPU mesh instance records, uploaded after Vertices at MeshInstanceOffset bytes, and the
        // indices their draws cover.
        Uint32 MeshInstances = 0;
        Uint32 MeshInstanceOffset = 0;
        uint64_t MeshIndices = 0;
        SDL_FColor ClearColor{};
        Uint32 VertexOffset = 0;
        // Indices into static terrain vertex buffers, uploaded once per view at TerrainIndexOffset.
        std::vector<uint32_t> TerrainIndices;
        Uint32 TerrainIndexOffset = 0;
        bool HasReflection = false;
        bool HasShadow = false;
        // Set by the caller when several cameras share the screen: the 2D layer is drawn with one of them only,
        // and each later camera draws over the earlier ones.
        bool Hud = true;
        bool OverPreviousCamera = false;
    };

    // What one presented frame drew, summed over its views, and the milliseconds each renderer step took.
    struct RendererFrameStats
    {
        int Triangles = 0;
        int Vertices = 0;
        int Batches = 0;
        int Views = 0;
        // Objects the world pass went through, those of them shown, and those of these outside the view.
        int Objects = 0;
        int VisibleObjects = 0;
        int CulledObjects = 0;
        // Shadow casters that changed, whose volumes were extruded again.
        int ShadowCastersExtruded = 0;
        // Gathering the scene's triangles and shadow volumes, ordering them, converting everything to GPU vertices
        // and batches, uploading those, and recording the draw calls.
        double BuildMilliseconds = 0.0;
        double OrderMilliseconds = 0.0;
        double ConvertMilliseconds = 0.0;
        double UploadMilliseconds = 0.0;
        double RecordMilliseconds = 0.0;
    };

    class RendererSystem
    {
      public:
        explicit RendererSystem(EngineKernel& Kernel);
        ~RendererSystem();

        // Main render entry point (called by CoreSystem::Sync)
        // Retain2d keeps transient pasted sprites and Classic screen text alive
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
        // Every vertex of a mesh as WorldMeshVertex gives them, with the bone palette and the object's transform
        // built once for all of them.
        void WorldMeshVertices(const Object3D& Object, const Mesh& MeshValue, std::vector<Vec3>& Vertices);
        // Kept per geometry, which copies of a mesh share.
        [[nodiscard]] const ShadowTopology& ShadowTopologyOf(const Mesh& MeshValue);
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

        // Mesh construction (static — pure functions)
        static void RegenerateMeshNormals(Mesh& MeshValue);
        static Mesh MakePlainMesh(float Width, float Height);
        static Mesh MakeBoxMesh(float Width, float Height, float Depth);
        static Mesh MakeSphereMesh(float Radius, int Rings, int Segments);
        static Mesh MakeCylinderMesh(float Radius, float Height, int Segments, bool TopCap, bool BottomCap);
        static Mesh MakeConeMesh(float Radius, float Height, int Segments);
        static Mesh MakeTriangleMesh(const Vec3& A, const Vec3& B, const Vec3& C);

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
            return CachedScene_->World.Triangles;
        }

        const std::vector<Triangle>& CachedReflectedTris() const
        {
            return CachedScene_->Reflected.Triangles;
        }

        const std::vector<Triangle>& CachedOverlayTris() const
        {
            return CachedScene_->Overlay.Triangles;
        }

        const std::vector<GpuMeshDraw>& CachedMeshDraws() const
        {
            return CachedScene_->MeshDraws;
        }

        void CacheScene(std::shared_ptr<SceneTriangles> Scene)
        {
            assert(Scene);
            CachedScene_ = std::move(Scene);
            HasCachedTris_ = true;
        }

        // Terrains drawn from static GPU buffers rather than per-frame CPU triangles.
        [[nodiscard]] bool DrawsTerrainOnGpu(const Matrix& Terrain) const;

        // Readies the GPU buffers of a geometry for drawing, once per revision; false when it cannot be drawn
        // from them (a zero-length vertex normal, an index outside the vertices).
        [[nodiscard]] bool PrepareGpuMesh(const std::shared_ptr<const MeshGeometry>& Geometry)
        {
            return GpuMeshEntry(Geometry) != nullptr;
        }

        // Whether eligible object meshes are drawn from GPU buffers; off, every mesh takes the CPU path.
        void SetGpuMeshesEnabled(bool Enabled)
        {
            GpuMeshesEnabled_ = Enabled;
            InvalidateTriCache();
        }

        // Triangles of static terrain in the last built view.
        [[nodiscard]] std::size_t LastStaticTerrainTriangles() const
        {
            return LastStaticTerrainTriangles_;
        }

        // STATISTIC(1): triangles sent to the GPU by the last presented frame.
        [[nodiscard]] int LastFramePolygons() const
        {
            return LastFrame_.Triangles;
        }

        [[nodiscard]] const RendererFrameStats& LastFrameStats() const
        {
            return LastFrame_;
        }

        // The mean milliseconds of each step over every frame presented so far; the counts are left at zero.
        [[nodiscard]] RendererFrameStats MeanStepMilliseconds() const;

        // True when the last presented frame contained 3D world geometry.
        [[nodiscard]] bool LastFrameShowedWorld() const
        {
            return LastFrameShowedWorld_;
        }

        // STATISTIC(5): draw calls submitted by the last presented frame.
        [[nodiscard]] int LastFrameDrawCalls() const
        {
            return LastFrame_.Batches;
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

        // A geometry's GPU buffers, keyed by revision. Vertices and indices wait in Pending* until uploaded.
        struct GpuMeshBuffers
        {
            // Released once this geometry is gone or has another revision.
            std::weak_ptr<const MeshGeometry> Source;
            SDL_GPUBuffer* Vertices = nullptr;
            SDL_GPUBuffer* Indices = nullptr;
            Uint32 IndexCount = 0;
            bool Usable = false;
            std::size_t LastUsedFrame = 0;
            std::vector<GpuMeshVertex> PendingVertices;
            std::vector<uint32_t> PendingIndices;
        };

        // The usable buffers of a geometry, created on first use; null when it cannot be drawn from them.
        GpuMeshBuffers* GpuMeshEntry(const std::shared_ptr<const MeshGeometry>& Geometry);
        void AppendGpuMeshes(const SceneTriangles& Scene, PreparedViewGeometry& Geometry);
        bool UploadGpuMeshes(SDL_GPUCommandBuffer* Commands);
        // Frees the buffers of geometries that changed or that no view drew for a few seconds.
        void ReleaseUnusedGpuMeshes();
        void ReleaseGpuMeshes();
        // The upload buffer GPU meshes and shadow volumes share, large enough for Bytes; null after reporting a
        // failure.
        SDL_GPUTransferBuffer* MeshTransferBuffer(std::size_t Bytes);

        // Extrudes again the casters that changed since the last frame, and drops those gone. Must run before the
        // scene build clears the objects' dirty flags.
        void UpdateShadowCasters();
        bool UploadShadowVolumes(SDL_GPUCommandBuffer* Commands);
        void ReleaseShadowVolumes();
        void ReleaseStaleShadowTopologies();

        // An empty scene to rebuild into: the cached one, cleared, when no view still holds it.
        std::shared_ptr<SceneTriangles> SceneForRebuild();
        // Gathers, orders and caches the scene of the active view; OrderTicks receives the time spent ordering.
        std::shared_ptr<const SceneTriangles> RebuildScene(bool HasReflection, const Vec3& ReflectionPoint,
                                                           const Vec3& ReflectionNormal, Uint64& OrderTicks);
        // Clears the view's terrain indices and drops the buffers of terrains deleted or back on the CPU path.
        void PrepareStaticTerrains(PreparedViewGeometry& Geometry);
        // The terrains drawn before the objects, or those drawn after them, as their CPU triangles would be.
        void AppendStaticTerrains(const RenderView& View, PreparedViewGeometry& Geometry, bool AfterObjects);
        bool UploadStaticTerrains(std::span<PreparedViewGeometry> Geometry, SDL_GPUCommandBuffer* Commands);
        void ReleaseStaticTerrains();

        EngineKernel& Kernel_;
        bool RenderFailureReported_ = false;
        PreparedViewGeometry FrameHud_;
        const RenderView* ActiveView_ = nullptr;
        std::optional<RenderView> CachedView_{};
        // Kept after invalidation: a rebuild refills it in place when no view still holds it.
        std::shared_ptr<SceneTriangles> CachedScene_ = std::make_shared<SceneTriangles>();
        Vec3 ReflectionPoint_{};
        Vec3 ReflectionNormal_{0.0f, 1.0f, 0.0f};
        bool ReflectionViewEnabled_ = false;
        int ShadowPositionMode_ = 0;
        Vec3 ShadowPosition_{};
        bool HasCachedTris_ = false;
        bool LastFrameShowedWorld_ = false;
        // The frame being prepared, the last one presented, and the sums of every presented frame's steps.
        RendererFrameStats Current_{};
        RendererFrameStats LastFrame_{};
        RendererFrameStats StepTotals_{};
        std::size_t StatFrames_ = 0;
        bool NormalizeNormals_ = true;
        bool GpuMeshesEnabled_ = true;
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
        std::unordered_map<uint64_t, GpuMeshBuffers> GpuMeshes_;
        // ImageStore::ReleasedGpuTextures when the cached scene was last checked.
        uint64_t CachedReleasedTextures_ = 0;
        SDL_GPUTransferBuffer* GpuMeshTransfer_ = nullptr;
        Uint32 GpuMeshTransferCapacity_ = 0;

        struct ShadowTopologyEntry
        {
            std::weak_ptr<const MeshGeometry> Source;
            ShadowTopology Topology;
        };

        std::unordered_map<uint64_t, ShadowTopologyEntry> ShadowTopologies_;

        // A caster's volume in world space, which the camera does not change, and what it was extruded from. All of
        // them share one pair of GPU buffers, drawn in one call.
        struct ShadowCaster
        {
            std::vector<Vec3> Vertices;
            std::vector<uint32_t> Indices;
            std::vector<uint64_t> Revisions;
            uint64_t LastWalk = 0;
        };

        std::unordered_map<int, ShadowCaster> ShadowCasters_;
        uint64_t ShadowCasterWalk_ = 0;
        Vec3 ShadowCasterLight_{};
        bool ShadowVolumesStale_ = false;
        std::size_t ShadowVolumeVertexCount_ = 0;
        std::size_t ShadowVolumeIndexCount_ = 0;
        SDL_GPUBuffer* ShadowVertexBuffer_ = nullptr;
        SDL_GPUBuffer* ShadowIndexBuffer_ = nullptr;
        Uint32 ShadowVertexCapacity_ = 0;
        Uint32 ShadowIndexCapacity_ = 0;
        std::vector<Vec3> ShadowWorldVertices_;
        std::vector<uint8_t> ShadowLitFaces_;
    };

} // namespace dbc
