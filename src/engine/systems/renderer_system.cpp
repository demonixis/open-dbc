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
#include "renderer_internal.hpp"
#include "renderer_transforms.hpp"
#include "sprite_system.hpp"
#include "text_system.hpp"
#include "video_system.hpp"
#include "vr_system.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <limits>

namespace dbc
{
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

    namespace
    {
        [[nodiscard]] double TicksToMilliseconds(Uint64 Ticks)
        {
            return static_cast<double>(Ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
        }

        // Puts back the view that was active before a view was built or recorded.
        struct ViewScope
        {
            RendererSystem& Renderer;
            const RenderView* Previous;

            ~ViewScope()
            {
                Renderer.SetActiveView(Previous);
            }
        };

        // Adds the time from its construction to its destruction to a step's milliseconds.
        struct StepTimer
        {
            double& Milliseconds;
            const Uint64 Start = SDL_GetPerformanceCounter();

            ~StepTimer()
            {
                Milliseconds += TicksToMilliseconds(SDL_GetPerformanceCounter() - Start);
            }
        };
    } // namespace

    RendererFrameStats RendererSystem::MeanStepMilliseconds() const
    {
        RendererFrameStats Mean{};
        if (StatFrames_ == 0)
            return Mean;
        const auto Frames = static_cast<double>(StatFrames_);
        Mean.BuildMilliseconds = StepTotals_.BuildMilliseconds / Frames;
        Mean.OrderMilliseconds = StepTotals_.OrderMilliseconds / Frames;
        Mean.ConvertMilliseconds = StepTotals_.ConvertMilliseconds / Frames;
        Mean.UploadMilliseconds = StepTotals_.UploadMilliseconds / Frames;
        Mean.RecordMilliseconds = StepTotals_.RecordMilliseconds / Frames;
        return Mean;
    }

    RenderFrameState RendererSystem::PrepareFrame(double DeltaSeconds, bool Retain2d, bool HudLast)
    {
        EngineKernel& K = Kernel_;
        Current_ = {};
        PrepareFrameSimulation(DeltaSeconds);
        // Persistent bitmap text and primitives are already represented by bitmap 0.
        if (K.RuntimeCompatibility().ScreenText() == ScreenTextLifetime::PersistentBitmap)
            K.Text().ClearDebugTextLines();
        K.ImageStoreRef().ClearOverlayBlits();
        K.Draw().ClearPrimitives2D();
        K.Text().ClearOverlayTextLines();
        if (K.Debug().IsOverlayEnabled())
            K.Debug().QueueOverlayText();
        dbc::EnsureHudGlyphs(K);
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

    std::shared_ptr<const SceneTriangles> RendererSystem::RebuildScene(bool HasReflection, const Vec3& ReflectionPoint,
                                                                       const Vec3& ReflectionNormal, Uint64& OrderTicks)
    {
        EngineKernel& K = Kernel_;
        std::shared_ptr<SceneTriangles> Scene = SceneForRebuild();
        auto& Tris = Scene->World.Triangles;
        auto& ReflectedTris = Scene->Reflected.Triangles;
        auto& OverlayTris = Scene->Overlay.Triangles;
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
        // The reflection passes keep every mesh on the CPU path, as do objects whose opaque triangles
        // depend on their place in the depth order.
        const bool GpuMeshes =
            GpuMeshesEnabled_ && !HasReflection && K.Gpu().MeshPipeline(true, false) && !dbc::ObjectsNeedDepthOrder(K);
        dbc::RenderObjects(K, Tris, OverlayTris, Scene.get(), GpuMeshes);
        dbc::RenderParticles(K, Tris);
        Uint64 OrderStart = SDL_GetPerformanceCounter();
        OrderWorldTriangles(Scene->World);
        dbc::GroupGpuMeshDraws(Scene->MeshDraws);
        OrderTicks += SDL_GetPerformanceCounter() - OrderStart;
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
            ReflectedTris.erase(std::remove_if(ReflectedTris.begin(), ReflectedTris.end(),
                                               [](const Triangle& TriangleValue)
                                               { return TriangleValue.Style.ReflectionSurface || TriangleValue.Is2d; }),
                                ReflectedTris.end());
            for (Triangle& TriangleValue : ReflectedTris)
                TriangleValue.Style.ReflectionPass = true;
            OrderStart = SDL_GetPerformanceCounter();
            OrderWorldTriangles(Scene->Reflected);
            OrderTicks += SDL_GetPerformanceCounter() - OrderStart;
        }
        // Overlays: always back-to-front (2D compositing order).
        OrderStart = SDL_GetPerformanceCounter();
        OrderOverlayTriangles(Scene->Overlay);
        OrderTicks += SDL_GetPerformanceCounter() - OrderStart;
        CacheScene(Scene);
        return Scene;
    }

    bool RendererSystem::BuildPreparedView(const RenderView& View, PreparedViewGeometry& Geometry,
                                           RenderFrameState& Frame)
    {
        EngineKernel& K = Kernel_;
        if (Frame.Finalized || !View.Valid())
            return false;
        const RenderView* PreviousView = ActiveView();
        SetActiveView(&View);

        const ViewScope Scope{*this, PreviousView};

        // When fog is on and backdrop is off, DarkBASIC Classic uses the fog colour as the clear colour
        // so the horizon blends seamlessly into the sky.
        const int BaseClearColorInt =
            K.Text().ClsRequested() ? K.Text().ClsColor()
                                    : (K.Camera().BackdropEnabled() ? K.Camera().BackdropColor()
                                                                    : (K.Draw().FogOn() ? K.Draw().FogColor() : 0));
        const int ClearColorInt =
            K.Camera().ClearRequested() && !K.Camera().HasCameraView() ? K.Camera().ClearColor() : BaseClearColorInt;
        const SDL_FColor ClearColor = UnpackColor(ClearColorInt);

        const Uint64 BuildStart = SDL_GetPerformanceCounter();
        Uint64 OrderTicks = 0;
        // Releasing the previous frame's scene lets a rebuild reuse the cached one in place.
        Geometry.Scene.reset();
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
        // World-space volumes serve every view of the frame.
        if (!Frame.ShadowCastersUpdated)
        {
            UpdateShadowCasters();
            Frame.ShadowCastersUpdated = true;
        }
        {
            // Check if the 3D scene needs rebuilding. The cached scene may name an image texture released since.
            const uint64_t ReleasedTextures = K.ImageStoreRef().ReleasedGpuTextures();
            bool SceneDirty = K.Camera().IsDirty() || K.Objects().AnyObjectDirty() || !K.Renderer().HasCachedTris() ||
                              ReleasedTextures != CachedReleasedTextures_;
            CachedReleasedTextures_ = ReleasedTextures;
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
                Geometry.Scene = RebuildScene(HasReflection, ReflectionPoint, ReflectionNormal, OrderTicks);
                K.Camera().ClearDirty();
                K.Objects().ClearAllDirtyFlags();
            }
            else
                Geometry.Scene = CachedScene_;
        }
        const SceneTriangles& Scene = *Geometry.Scene;
        const std::vector<Triangle>& Tris = Scene.World.Triangles;
        Current_.Objects += Scene.Objects;
        Current_.VisibleObjects += Scene.VisibleObjects;
        Current_.CulledObjects += Scene.CulledObjects;

        const bool HasShadow = ShadowVolumeIndexCount_ != 0 && GlobalShadows_;
        const Uint64 ConvertStart = SDL_GetPerformanceCounter();
        Current_.BuildMilliseconds += TicksToMilliseconds(ConvertStart - BuildStart - OrderTicks);
        Current_.OrderMilliseconds += TicksToMilliseconds(OrderTicks);

        auto& Vertices = Geometry.Vertices;
        auto& Batches = Geometry.Batches;
        Vertices.clear();
        Batches.clear();
        Geometry.MeshInstances = 0;
        Geometry.MeshIndices = 0;
        {
            const std::size_t TriangleCount =
                Tris.size() + Scene.Reflected.Triangles.size() + Scene.Overlay.Triangles.size();
            Vertices.reserve(TriangleCount * 3 + 8192);
            Batches.reserve(2048);
            const auto AppendTriangle =
                [&](const Triangle& Tri, std::vector<GpuVertex>& ListVertices, std::vector<GpuBatch>& ListBatches)
            {
                dbc::AppendGpuTriangle(K, ListVertices, ListBatches, Tri.GpuTexture, Tri.Pos, Tri.Normal, Tri.Color,
                                       Tri.Uv, Tri.Transparent, Tri.Is2d, Tri.Additive, Tri.Style, nullptr, false,
                                       Tri.AlphaDepthWrite, Tri.MatrixGhostMode, Tri.DepthWrite, Tri.DepthTest,
                                       Tri.AuxiliaryGpuTextures, Tri.CubeGpuTexture, &Tri.AuxiliaryUvs,
                                       Tri.CubeDirection);
            };
            const auto AppendOrdered = [&](const OrderedTriangles& List, std::vector<GpuVertex>& ListVertices,
                                           std::vector<GpuBatch>& ListBatches)
            {
                for (const uint32_t Index : List.Order)
                    AppendTriangle(List.Triangles[Index], ListVertices, ListBatches);
            };

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
            { AppendOrdered(Scene.Overlay, HudVertices, HudBatches); };

            if (!(Frame.HudLast || K.Draw().IsDrawToFront()) && Geometry.Hud)
                AppendOverlay(Vertices, Batches);

            // Static terrain and GPU meshes take the place of their CPU triangles among the opaque ones, at their
            // render priority: terrain at -1, or 1 after the objects, and meshes at 0 with the objects. Each goes
            // after the very early phase and the lower priorities, before the higher ones (particles, bounds) and
            // transparency.
            PrepareStaticTerrains(Geometry);
            constexpr std::array<int, 3> InsertionPriorities{-1, 0, 1};
            std::size_t Inserted = 0;
            const auto InsertNext = [&]
            {
                if (InsertionPriorities[Inserted] == 0)
                    AppendGpuMeshes(Scene, Geometry);
                else
                    AppendStaticTerrains(View, Geometry, InsertionPriorities[Inserted] > 0);
                ++Inserted;
            };
            for (const uint32_t Index : Scene.World.Order)
            {
                const Triangle& Tri = Scene.World.Triangles[Index];
                while (Inserted < InsertionPriorities.size() && !Tri.VeryEarly &&
                       (Tri.Transparent || Tri.Additive || Tri.RenderPriority >= InsertionPriorities[Inserted]))
                    InsertNext();
                AppendTriangle(Tri, Vertices, Batches);
            }
            while (Inserted < InsertionPriorities.size())
                InsertNext();
            AppendOrdered(Scene.Reflected, Vertices, Batches);

            // Reflections and shadows darken what the stencil marked.
            const auto AppendDarkening = [&](bool Shadow)
            {
                SurfaceStyle OverlayStyle{};
                OverlayStyle.Fog = false;
                OverlayStyle.Ambient = false;
                OverlayStyle.ReflectionOverlay = !Shadow;
                OverlayStyle.ShadowOverlay = Shadow;
                dbc::AppendGpuQuad(K, Vertices, Batches, WhiteGpuTexture(K), 0.0f, 0.0f,
                                   static_cast<float>(View.LogicalWidth), static_cast<float>(View.LogicalHeight),
                                   Shadow ? GlobalShadowColor_ : SDL_FColor{0.0f, 0.0f, 0.0f, 64.0f / 255.0f}, 0.0f,
                                   true, true, OverlayStyle);
            };
            if (HasReflection)
                AppendDarkening(false);
            if (HasShadow)
                AppendDarkening(true);

            const auto AppendHud = [&](std::vector<GpuVertex>& HudVertices, std::vector<GpuBatch>& HudBatches)
            {
                if (Frame.HudLast)
                    AppendBitmap(HudVertices, HudBatches);
                if ((Frame.HudLast || K.Draw().IsDrawToFront()))
                    AppendOverlay(HudVertices, HudBatches);

                dbc::AppendHudLayer(K, HudVertices, HudBatches);
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
        Current_.ConvertMilliseconds += TicksToMilliseconds(SDL_GetPerformanceCounter() - ConvertStart);
        return true;
    }

    bool RendererSystem::UploadPreparedViews(std::span<PreparedViewGeometry> Geometry, SDL_GPUCommandBuffer* Cmd)
    {
        const StepTimer Timer{Current_.UploadMilliseconds};
        if (!UploadStaticTerrains(Geometry, Cmd) || !UploadGpuMeshes(Cmd) || !UploadShadowVolumes(Cmd))
            return false;
        auto& Gpu = Kernel_.Gpu();
        // Each view's vertices, then its GPU mesh instance records, in the one frame vertex buffer.
        std::size_t Bytes = 0;
        for (auto& View : Geometry)
        {
            const std::size_t ViewBytes =
                View.Vertices.size() * sizeof(GpuVertex) + std::size_t{View.MeshInstances} * sizeof(GpuMeshInstance);
            if (ViewBytes > std::numeric_limits<Uint32>::max() - Bytes)
            {
                ReportRenderError("combined view geometry exceeds the GPU buffer limit");
                return false;
            }
            View.VertexOffset = static_cast<Uint32>(Bytes);
            View.MeshInstanceOffset = static_cast<Uint32>(Bytes + View.Vertices.size() * sizeof(GpuVertex));
            Bytes += ViewBytes;
        }
        if (!Bytes)
            return true;
        const auto Count = static_cast<Uint32>((Bytes + sizeof(GpuVertex) - 1) / sizeof(GpuVertex));
        if (!Cmd || !Gpu.EnsureGpuVertexCapacity(Count))
            return false;
        auto* Mapped =
            static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Gpu.GpuDevice(), Gpu.GpuTransferBuffer(), true));
        if (!Mapped)
            return false;
        for (const auto& View : Geometry)
        {
            if (!View.Vertices.empty())
                SDL_memcpy(Mapped + View.VertexOffset, View.Vertices.data(), View.Vertices.size() * sizeof(GpuVertex));
            for (Uint32 Index = 0; Index < View.MeshInstances; ++Index)
                SDL_memcpy(Mapped + View.MeshInstanceOffset + Index * sizeof(GpuMeshInstance),
                           &View.Scene->MeshDraws[Index].Instance, sizeof(GpuMeshInstance));
        }
        SDL_UnmapGPUTransferBuffer(Gpu.GpuDevice(), Gpu.GpuTransferBuffer());
        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Cmd);
        if (!Copy)
            return false;
        SDL_GPUTransferBufferLocation Source{Gpu.GpuTransferBuffer(), 0};
        SDL_GPUBufferRegion Destination{Gpu.GpuVertexBuffer(), 0, static_cast<Uint32>(Bytes)};
        SDL_UploadToGPUBuffer(Copy, &Source, &Destination, true);
        SDL_EndGPUCopyPass(Copy);
        return true;
    }

    bool RendererSystem::RecordPreparedView(const RenderView& View, const GpuRenderTarget& Target,
                                            const PreparedViewGeometry& Geometry, SDL_GPUCommandBuffer* Cmd,
                                            RenderFrameState& Frame)
    {
        const StepTimer Timer{Current_.RecordMilliseconds};
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

        const ViewScope Scope{*this, PreviousView};

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
        // Slot 1 is the model-view matrix: identity for view-space vertices, the view for static terrain and GPU
        // meshes.
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
        const bool HasGeometry = !Vertices.empty() || !Geometry.TerrainIndices.empty() || Geometry.MeshInstances != 0;
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
                // A mesh leaves its buffers and transforms bound until a batch of shared vertices needs them back.
                bool MeshStateBound = false;
                bool MaterialUniformsBound = false;
                const auto RestoreSharedState = [&]
                {
                    SDL_PushGPUVertexUniformData(Cmd, 1, &IdentityModelView[0][0], sizeof(glm::mat4));
                    BindSharedVertices(Pass);
                    MeshStateBound = false;
                };
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
                    const bool ShadowOverlay = Batch.Style.ShadowOverlay;
                    const bool StencilTest = Batch.Style.ReflectionPass || Batch.Style.ReflectionOverlay;
                    const int BatchEffect = MaskBatch || ShadowOverlay || ForcePipeline ? 0 : Batch.Style.EffectId;
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
                    if (Batch.MeshDraw)
                    {
                        // Meshes are never drawn with the reflection passes, which alone force a pipeline.
                        if (ForcePipeline)
                            continue;
                        Pipeline = K.Gpu().MeshPipeline(Batch.MeshDraw->Cull, Batch.Style.Wireframe);
                    }
                    else if (Batch.StaticVertices)
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

                        // Read for surface maps only, but the shader's slot must hold something.
                        const bool SurfaceMaps = Batch.Style.BuiltInEffect == 4;
                        if (SurfaceMaps || !MaterialUniformsBound)
                        {
                            GpuMaterialUniforms Material{};
                            if (SurfaceMaps)
                            {
                                // The rotation's inverse is its transpose, mirrored reflection views included.
                                for (int Row = 0; Row < 3; ++Row)
                                    for (int Column = 0; Column < 3; ++Column)
                                        Material.ViewToWorld[Row][Column] = View.WorldToView[Row][Column];
                                Material.Params[0] = Batch.Style.NormalStrength;
                                Material.Params[1] = Batch.Style.ReflectionAmount;
                                Material.Params[2] = Batch.Style.ReflectionFresnel;
                                Material.Params[3] = Batch.CubeTexture ? Batch.Style.CubeLevels : 0.0f;
                                // A deleted image leaves its binding empty.
                                Material.Maps[0] = !Batch.AuxiliaryTextures[3]   ? 0
                                                   : Batch.Style.TwoNormalLayers ? 2
                                                                                 : 1;
                                Material.Maps[1] = Batch.AuxiliaryTextures[4] ? 1 : 0;
                                Material.Maps[2] = Batch.AuxiliaryTextures[5] ? 1 : 0;
                                Material.Maps[3] = Batch.AuxiliaryTextures[6] ? 1 : 0;
                            }
                            SDL_PushGPUFragmentUniformData(Cmd, 2, &Material, sizeof(Material));
                            MaterialUniformsBound = true;
                        }
                    }

                    // The reflection mask writes no colour, but its fragment shader samples like any other.
                    const Uint32 SamplerCount = BatchEffect == 0 ? 9 : K.Gpu().EffectFragmentSamplerCount(BatchEffect);
                    if (SamplerCount != 0)
                    {
                        SDL_GPUTexture* Texture = Batch.Texture;
                        if ((ShadowOverlay || ForcePipeline) && !Texture)
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
                    if (Batch.MeshDraw)
                    {
                        // The instance records are bound at the group's first, so first_instance stays 0.
                        const SDL_GPUBufferBinding MeshBindings[2]{
                            {Batch.StaticVertices, 0},
                            {VertexBinding.buffer,
                             Geometry.MeshInstanceOffset +
                                 Batch.FirstInstance * static_cast<Uint32>(sizeof(GpuMeshInstance))}};
                        SDL_BindGPUVertexBuffers(Pass, 0, MeshBindings, 2);
                        const SDL_GPUBufferBinding IndexBinding{Batch.StaticIndices, 0};
                        SDL_BindGPUIndexBuffer(Pass, &IndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                        if (!MeshStateBound)
                            SDL_PushGPUVertexUniformData(Cmd, 1, &View.WorldToView[0][0], sizeof(glm::mat4));
                        SDL_DrawGPUIndexedPrimitives(Pass, Batch.IndexCount, Batch.InstanceCount, 0, 0, 0);
                        MeshStateBound = true;
                        continue;
                    }
                    if (MeshStateBound)
                        RestoreSharedState();
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
                    RestoreSharedState();
                }
                // The next batch loop starts from the shared vertices and identity transforms.
                if (MeshStateBound)
                    RestoreSharedState();
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
            const auto Draw3d = [&](const GpuBatch& Batch) { return !Batch.Is2d; };
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
                { return !Batch.Is2d && !Batch.Style.ReflectionSurface && !Batch.Style.ReflectionPass; };
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
                        { return !Batch.Is2d && !Batch.Style.ReflectionPass && Batch.DepthTest && Batch.DepthWrite; },
                        true);
                }

                SDL_GPUGraphicsPipeline* NearCapPipeline = K.Gpu().ShadowNearCapPipeline();
                SDL_GPUGraphicsPipeline* VolumePipeline = K.Gpu().ShadowVolumePipeline();
                if (NearCapPipeline && VolumePipeline)
                {
                    const auto NearCapIndices = static_cast<Uint32>(ShadowNearCapIndexCount_);
                    SDL_BindGPUGraphicsPipeline(Pass, NearCapPipeline);
                    SDL_PushGPUVertexUniformData(Cmd, 0, &PerspUbo, sizeof(GpuVertexUniforms));
                    SDL_PushGPUVertexUniformData(Cmd, 1, &View.WorldToView[0][0], sizeof(glm::mat4));
                    const SDL_GPUBufferBinding VolumeVertices{ShadowVertexBuffer_, 0};
                    SDL_BindGPUVertexBuffers(Pass, 0, &VolumeVertices, 1);
                    const SDL_GPUBufferBinding VolumeIndices{ShadowIndexBuffer_, 0};
                    SDL_BindGPUIndexBuffer(Pass, &VolumeIndices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_DrawGPUIndexedPrimitives(Pass, NearCapIndices, 1, 0, 0, 0);
                    SDL_BindGPUGraphicsPipeline(Pass, VolumePipeline);
                    SDL_DrawGPUIndexedPrimitives(Pass, static_cast<Uint32>(ShadowVolumeIndexCount_) - NearCapIndices, 1,
                                                 NearCapIndices, 0, 0);
                    SDL_PushGPUVertexUniformData(Cmd, 1, &IdentityModelView[0][0], sizeof(glm::mat4));
                    BindSharedVertices(Pass);
                }
                // Each shade darkens what that many shadows overlap, counted by the stencil.
                for (int Shade = 1; Shade <= GlobalShadowShades_; ++Shade)
                {
                    SDL_SetGPUStencilReference(Pass, static_cast<Uint8>(Shade));
                    DrawBatches(K.Gpu().ShadowOverlayPipeline(), K.Gpu().ShadowOverlayPipeline(), OrthoUbo,
                                [](const GpuBatch& Batch) { return Batch.Style.ShadowOverlay; });
                }
                SDL_SetGPUStencilReference(Pass, 0);
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
        Frame.WorldRendered |=
            (Geometry.Scene && (!Geometry.Scene->World.Triangles.empty() || !Geometry.Scene->MeshDraws.empty())) ||
            !Geometry.TerrainIndices.empty();
        // Instancing can multiply past the range of the statistics.
        const uint64_t ViewVertices =
            uint64_t{Vertices.size()} + Geometry.TerrainIndices.size() + Geometry.MeshIndices + ShadowVolumeIndexCount_;
        Frame.Vertices = static_cast<int>(
            std::min<uint64_t>(static_cast<uint64_t>(Frame.Vertices) + ViewVertices, std::numeric_limits<int>::max()));
        Frame.Batches += static_cast<int>(Batches.size()) + (ShadowVolumeIndexCount_ != 0 ? 2 : 0);
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

        Current_.Triangles = Frame.Vertices / 3;
        Current_.Vertices = Frame.Vertices;
        Current_.Batches = Frame.Batches;
        Current_.Views = Frame.Views;
        LastFrame_ = Current_;
        StepTotals_.BuildMilliseconds += Current_.BuildMilliseconds;
        StepTotals_.OrderMilliseconds += Current_.OrderMilliseconds;
        StepTotals_.ConvertMilliseconds += Current_.ConvertMilliseconds;
        StepTotals_.UploadMilliseconds += Current_.UploadMilliseconds;
        StepTotals_.RecordMilliseconds += Current_.RecordMilliseconds;
        ++StatFrames_;
        ReleaseUnusedGpuMeshes();
        ReleaseStaleShadowTopologies();
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
        std::vector<int> CameraIds = K.Camera().ImageCameraIds();
        const std::size_t ImageCameras = CameraIds.size();
        for (const int Id : K.Camera().ScreenCameraIds())
            CameraIds.push_back(Id);
        std::vector<RenderView> Views;
        std::vector<PreparedViewGeometry> Geometry(CameraIds.size());
        std::vector<GpuRenderTarget> Targets;
        Views.reserve(CameraIds.size());
        Targets.reserve(CameraIds.size());
        // DRAW TO CAMERA names one camera; a camera SYNC MASK leaves out draws no 2D layer.
        const int HudCamera = K.Draw().DrawToCamera();
        const auto HasHud = [&](std::size_t Index)
        {
            if (HudCamera >= 0)
                return CameraIds[Index] == HudCamera;
            // DRAW TO BACK puts the 2D layer under every screen camera, DRAW TO FRONT over them all.
            return K.Draw().IsDrawToFront() ? Index + 1 == CameraIds.size() : Index == ImageCameras;
        };
        bool Prepared = true;
        for (std::size_t Index = 0; Prepared && Index < CameraIds.size(); ++Index)
        {
            const int Previous = K.Camera().SelectRenderCamera(CameraIds[Index]);
            // The triangle cache holds one camera's view-space geometry.
            if (CameraIds.size() > 1)
                K.Renderer().InvalidateTriCache();
            const bool ToImage = Index < ImageCameras;
            Targets.push_back(ToImage ? K.Renderer().CameraImageTarget(
                                            CameraIds[Index], static_cast<Uint32>(K.Camera().TargetImageWidth()),
                                            static_cast<Uint32>(K.Camera().TargetImageHeight()))
                                      : Target);
            Prepared = Targets.back().Color != nullptr;
            if (Prepared)
            {
                Views.push_back(K.Renderer().CameraView(Targets.back().Width, Targets.back().Height));
                Geometry[Index].Hud = HasHud(Index);
                Geometry[Index].OverPreviousCamera = Index > ImageCameras;
                Prepared = K.Renderer().BuildPreparedView(Views.back(), Geometry[Index], Frame);
            }
            K.Camera().SelectRenderCamera(Previous);
        }
        if (!Prepared || !K.Renderer().UploadPreparedViews(Geometry, Cmd))
            return;
        for (std::size_t Index = 0; Index < Views.size(); ++Index)
        {
            const int Previous = K.Camera().SelectRenderCamera(CameraIds[Index]);
            const int ImageId = Index < ImageCameras ? K.Camera().TargetImage() : 0;
            const bool Recorded =
                K.Renderer().RecordPreparedView(Views[Index], Targets[Index], Geometry[Index], Cmd, Frame) &&
                (ImageId == 0 || K.Renderer().TransferCameraImage(CameraIds[Index], ImageId, Cmd));
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
        ReleaseCameraTargets();
        ReleaseGpuMeshes();
        ReleaseShadowVolumes();
    }

    std::shared_ptr<SceneTriangles> RendererSystem::SceneForRebuild()
    {
        // Another view may still draw from the cached scene this frame.
        if (CachedScene_.use_count() != 1)
            return std::make_shared<SceneTriangles>();
        CachedScene_->World.Clear();
        CachedScene_->Reflected.Clear();
        CachedScene_->Overlay.Clear();
        CachedScene_->MeshDraws.clear();
        CachedScene_->Objects = 0;
        CachedScene_->VisibleObjects = 0;
        CachedScene_->CulledObjects = 0;
        return CachedScene_;
    }

    // =======================================================================
    // RendererSystem — public methods (delegate to the renderer's other files)
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
        // A camera sent to an image takes the image's pixels as its logical coordinates.
        const bool ToImage = Camera.TargetImage() != 0;
        const int Width = ToImage ? Camera.TargetImageWidth() : Kernel_.Core().WindowWidth();
        const int Height = ToImage ? Camera.TargetImageHeight() : Kernel_.Core().WindowHeight();
        const SDL_FRect Viewport =
            Camera.HasCameraView()
                ? SDL_FRect{static_cast<float>(Camera.ViewLeft()), static_cast<float>(Camera.ViewTop()),
                            static_cast<float>(static_cast<double>(Camera.ViewRight()) - Camera.ViewLeft()),
                            static_cast<float>(static_cast<double>(Camera.ViewBottom()) - Camera.ViewTop())}
                : SDL_FRect{0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height)};
        glm::mat4 Projection = glm::perspectiveLH_ZO(Fov, Camera.Aspect(), Near, Far);
        glm::mat4 AdditiveProjection = glm::perspectiveLH_ZO(Fov, Camera.Aspect(), Near, std::max(Far, 1e7f));
        if (Camera.HasClipPlane())
        {
            const std::array<float, 4> Plane = Camera.ClipPlane();
            // A plane's coefficients go through the inverse transpose of the matrix that moves its points.
            const glm::vec4 ViewPlane =
                glm::transpose(glm::inverse(Transform)) * glm::vec4(Plane[0], Plane[1], Plane[2], Plane[3]);
            // The near plane becomes the clipping plane, which has no cost per fragment and needs no pipeline. It
            // only exists when the eye is on the clipped side of the plane.
            if (ViewPlane.w < 0.0f)
                for (glm::mat4* Matrix : {&Projection, &AdditiveProjection})
                {
                    const glm::vec4 Corner =
                        glm::inverse(*Matrix) *
                        glm::vec4(ViewPlane.x < 0.0f ? -1.0f : 1.0f, ViewPlane.y < 0.0f ? -1.0f : 1.0f, 1.0f, 1.0f);
                    const glm::vec4 Row = ViewPlane / glm::dot(ViewPlane, Corner);
                    for (int Column = 0; Column < 4; ++Column)
                        (*Matrix)[Column][2] = Row[Column];
                }
        }
        return {Transform, Projection, AdditiveProjection, Viewport, Width, Height, PhysicalWidth, PhysicalHeight};
    }

    GpuRenderTarget RendererSystem::CameraImageTarget(int CameraId, Uint32 Width, Uint32 Height)
    {
        EngineKernel& K = Kernel_;
        SDL_GPUDevice* Device = K.Gpu().GpuDevice();
        if (!Device || Width == 0 || Height == 0)
            return {};
        CameraTarget& Entry = CameraTargets_[CameraId];
        if (!Entry.Color || Entry.Width != Width || Entry.Height != Height)
        {
            if (Entry.Color)
                SDL_ReleaseGPUTexture(Device, Entry.Color);
            if (Entry.Depth)
                SDL_ReleaseGPUTexture(Device, Entry.Depth);
            Entry = {};
            SDL_GPUTextureCreateInfo Info{};
            Info.type = SDL_GPU_TEXTURETYPE_2D;
            Info.width = Width;
            Info.height = Height;
            Info.layer_count_or_depth = 1;
            Info.num_levels = 1;
            Info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            Info.format = K.Gpu().GpuColorFormat();
            Info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            Entry.Color = SDL_CreateGPUTexture(Device, &Info);
            Info.format = K.Gpu().GpuDepthFormat();
            Info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            Entry.Depth = SDL_CreateGPUTexture(Device, &Info);
            if (!Entry.Color || !Entry.Depth)
            {
                DBC_LOG_ERROR("Camera image target failed: {}", SDL_GetError());
                ReportRenderError("camera image target failed");
                return {};
            }
            Entry.Width = Width;
            Entry.Height = Height;
        }
        return {Entry.Color, Entry.Depth, K.Gpu().GpuColorFormat(), K.Gpu().GpuDepthFormat(), Width, Height};
    }

    bool RendererSystem::TransferCameraImage(int CameraId, int ImageId, SDL_GPUCommandBuffer* Cmd)
    {
        EngineKernel& K = Kernel_;
        const auto Entry = CameraTargets_.find(CameraId);
        Image* Destination = K.ImageStoreRef().GetImage(ImageId);
        // A program may have deleted or replaced the image, which only leaves nothing to write to.
        if (Entry == CameraTargets_.end() || !Destination || !Destination->GpuTexture ||
            Destination->Width != static_cast<int>(Entry->second.Width) ||
            Destination->Height != static_cast<int>(Entry->second.Height))
            return true;
        if (!CameraTransferPipeline_)
        {
            CameraTransferPipeline_ = K.Gpu().CreateTransferPipeline(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
            if (!CameraTransferPipeline_)
            {
                ReportRenderError("camera image transfer pipeline failed");
                return false;
            }
        }
        SDL_GPUColorTargetInfo Color{};
        Color.texture = Destination->GpuTexture;
        Color.load_op = SDL_GPU_LOADOP_DONT_CARE;
        Color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* Pass = SDL_BeginGPURenderPass(Cmd, &Color, 1, nullptr);
        if (!Pass)
        {
            ReportRenderError("camera image transfer pass failed");
            return false;
        }
        SDL_BindGPUGraphicsPipeline(Pass, CameraTransferPipeline_);
        // The camera wrote encoded values; an image's format encodes what it is given, so they are decoded first.
        const std::array<float, 8> Uniforms{1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SDL_PushGPUFragmentUniformData(Cmd, 0, Uniforms.data(), sizeof(Uniforms));
        const SDL_GPUTextureSamplerBinding Binding{Entry->second.Color, K.Gpu().GpuSampler()};
        SDL_BindGPUFragmentSamplers(Pass, 0, &Binding, 1);
        SDL_DrawGPUPrimitives(Pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(Pass);
        SDL_GenerateMipmapsForGPUTexture(Cmd, Destination->GpuTexture);
        return true;
    }

    void RendererSystem::ReleaseCameraTargets()
    {
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        if (Device)
        {
            for (auto& [CameraId, Entry] : CameraTargets_)
            {
                (void)CameraId;
                if (Entry.Color)
                    SDL_ReleaseGPUTexture(Device, Entry.Color);
                if (Entry.Depth)
                    SDL_ReleaseGPUTexture(Device, Entry.Depth);
            }
            if (CameraTransferPipeline_)
                SDL_ReleaseGPUGraphicsPipeline(Device, CameraTransferPipeline_);
        }
        CameraTargets_.clear();
        CameraTransferPipeline_ = nullptr;
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
        return dbc::FindLimbMesh(Object, LimbId);
    }

    const Mesh* RendererSystem::FindLimbMesh(const Object3D& Object, int LimbId)
    {
        return dbc::FindLimbMesh(Object, LimbId);
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
        if (VertexIndex >= MeshValue.Geometry().Vertices.size())
            return {};
        const aiMatrix4x4* CurrentMeshTransform = CurrentImportedMeshTransform(Object, MeshValue);
        Vec3 Point = MeshValue.Geometry().Vertices[VertexIndex];
        Vec3 Normal{};
        const std::vector<aiMatrix4x4> BonePalette = BuildBonePalette(Object, MeshValue, Object.CachedNodeGlobals);
        const bool Skinned = SkinMeshVertex(MeshValue, VertexIndex, BonePalette, Point, Normal);
        return TransformMeshPointInObject(Object, MeshValue, Point, Skinned ? nullptr : CurrentMeshTransform, Skinned);
    }

    void RendererSystem::ObjectMeshVertices(const Object3D& Object, const Mesh& MeshValue, std::vector<Vec3>& Vertices)
    {
        Vertices.resize(MeshValue.Geometry().Vertices.size());
        const aiMatrix4x4* CurrentMeshTransform = CurrentImportedMeshTransform(Object, MeshValue);
        const std::vector<aiMatrix4x4> BonePalette = BuildBonePalette(Object, MeshValue, Object.CachedNodeGlobals);
        for (std::size_t VertexIndex = 0; VertexIndex < MeshValue.Geometry().Vertices.size(); ++VertexIndex)
        {
            Vec3 Point = MeshValue.Geometry().Vertices[VertexIndex];
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
        if (VertexIndex >= MeshValue.Geometry().Vertices.size())
            return WorldObjectOrigin(Object);
        return dbc::TransformObjectSpacePoint(Kernel_, Object, ObjectMeshVertex(Object, MeshValue, VertexIndex));
    }

    void RendererSystem::WorldMeshVertices(const Object3D& Object, const Mesh& MeshValue, std::vector<Vec3>& Vertices)
    {
        ObjectMeshVertices(Object, MeshValue, Vertices);
        if (Object.GluedTargetObject > 0)
        {
            for (Vec3& Vertex : Vertices)
                Vertex = dbc::TransformObjectSpacePoint(Kernel_, Object, Vertex);
            return;
        }
        const ObjectSteps Steps = MakeObjectSteps(Object);
        for (Vec3& Vertex : Vertices)
            Vertex = ApplyObjectPoint(Steps, Vertex, Object.GlueMode != 2);
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

    bool RendererSystem::SetShadowLight(int LightId, float X, float Y, float Z, float Range, std::string* Error)
    {
        if (LightId >= 0 && !Kernel_.Lights().PositionLight(LightId, X, Y, Z, Error))
            return false;
        SetShadowPosition(LightId >= 0 ? LightId : -1, X, Y, Z);
        ShadowLightRange_ = std::max(Range, 0.0f);
        return true;
    }

    void RendererSystem::SetGlobalShadows(bool Enabled)
    {
        GlobalShadows_ = Enabled;
        InvalidateTriCache();
    }

    void RendererSystem::SetGlobalShadowColor(int Red, int Green, int Blue, int Alpha)
    {
        const auto Unit = [](int Value) { return static_cast<float>(std::clamp(Value, 0, 255)) / 255.0f; };
        GlobalShadowColor_ = SDL_FColor{Unit(Red), Unit(Green), Unit(Blue), Unit(Alpha)};
        InvalidateTriCache();
    }

    bool RendererSystem::SetGlobalShadowShades(int Shades, std::string* Error)
    {
        if (Shades < 1 || Shades > 16)
        {
            if (Error)
                *Error = "SET GLOBAL SHADOW SHADES takes 1 to 16 shades";
            return false;
        }
        GlobalShadowShades_ = Shades;
        InvalidateTriCache();
        return true;
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

    // =======================================================================
    // RendererSystem — static methods (pure functions)
    // =======================================================================

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

    Mesh RendererSystem::MakeTriangleMesh(const Vec3& A, const Vec3& B, const Vec3& C)
    {
        return dbc::MakeTriangleMesh(A, B, C);
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
