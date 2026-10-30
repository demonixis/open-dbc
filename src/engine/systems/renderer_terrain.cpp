#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "image_store.hpp"
#include "matrix_manager.hpp"
#include "renderer_internal.hpp"
#include "terrain_mesh.hpp"

namespace dbc
{
    bool RendererSystem::DrawsTerrainOnGpu(const Matrix& Terrain) const
    {
        // Ghost, transparent and unculled surfaces keep the CPU path with its blending and ordering.
        return Terrain.IsTerrain && !Terrain.Ghost && !Terrain.Transparency && Terrain.Cull &&
               Kernel_.Gpu().TerrainPipeline(false) != nullptr;
    }

    void RendererSystem::PrepareStaticTerrains(PreparedViewGeometry& Geometry)
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
    }

    void RendererSystem::AppendStaticTerrains(const RenderView& View, PreparedViewGeometry& Geometry, bool AfterObjects)
    {
        EngineKernel& K = Kernel_;
        SDL_GPUDevice* Device = K.Gpu().GpuDevice();
        if (!Device)
            return;
        const glm::vec3 Camera = View.CameraPosition;
        std::vector<int> Steps;
        K.Matrices().ForEachTerrain(
            [&](int TerrainId, const Matrix& Terrain)
            {
                if (!DrawsTerrainOnGpu(Terrain) || Terrain.RenderAfterObjects != AfterObjects)
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
} // namespace dbc
