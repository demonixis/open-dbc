#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "renderer_internal.hpp"

#include <numeric>

namespace dbc
{
    // Whether two GPU mesh draws can be instances of one draw call.
    [[nodiscard]] bool SameGpuMeshDraw(const GpuMeshDraw& A, const GpuMeshDraw& B)
    {
        return A.Geometry == B.Geometry && A.Texture == B.Texture && A.Cull == B.Cull && A.StyleHash == B.StyleHash &&
               A.Style == B.Style;
    }

    // Brings together the draws that one instanced call can make. Their order is otherwise free: GPU meshes are
    // opaque and depth tested.
    void GroupGpuMeshDraws(std::vector<GpuMeshDraw>& Draws)
    {
        // Compact keys are sorted, then each draw moves once, along the cycles of the permutation.
        using Key = std::tuple<uint64_t, uint32_t, bool, uintptr_t, std::size_t>;
        std::vector<Key> Keys(Draws.size());
        for (std::size_t Index = 0; Index < Draws.size(); ++Index)
        {
            const GpuMeshDraw& Draw = Draws[Index];
            Keys[Index] = {Draw.Geometry->Revision, Draw.StyleHash, Draw.Cull,
                           reinterpret_cast<uintptr_t>(Draw.Texture), Index};
        }
        std::sort(Keys.begin(), Keys.end());
        std::vector<std::size_t> Source(Draws.size());
        for (std::size_t Position = 0; Position < Keys.size(); ++Position)
            Source[Position] = std::get<4>(Keys[Position]);
        for (std::size_t Start = 0; Start < Source.size(); ++Start)
        {
            if (Source[Start] == Start)
                continue;
            GpuMeshDraw Moved = std::move(Draws[Start]);
            std::size_t Position = Start;
            while (Source[Position] != Start)
            {
                const std::size_t From = Source[Position];
                Draws[Position] = std::move(Draws[From]);
                Source[Position] = Position;
                Position = From;
            }
            Draws[Position] = std::move(Moved);
            Source[Position] = Position;
        }
    }

    RendererSystem::GpuMeshBuffers* RendererSystem::GpuMeshEntry(const std::shared_ptr<const MeshGeometry>& Source)
    {
        const MeshGeometry& Geometry = *Source;
        const auto [Found, Inserted] = GpuMeshes_.try_emplace(Geometry.Revision);
        GpuMeshBuffers& Entry = Found->second;
        Entry.LastUsedFrame = StatFrames_;
        if (!Inserted)
            return Entry.Usable ? &Entry : nullptr;
        Entry.Source = Source;

        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        const std::size_t VertexCount = Geometry.Vertices.size();
        // The CPU path ignores a trailing partial triangle too.
        const std::size_t IndexCount = Geometry.Indices.size() / 3 * 3;
        const auto Indices = std::span<const uint32_t>(Geometry.Indices).first(IndexCount);
        // A mesh without vertex normals gets one vertex per triangle corner, with the triangle's normal.
        const bool FaceNormals = Geometry.Normals.size() != VertexCount;
        const std::size_t UploadedVertices = FaceNormals ? IndexCount : VertexCount;
        // The CPU path lights a zero normal along world +Z, which the shader cannot know.
        const auto ZeroNormal = [](const Vec3& Normal) { return Vec3Dot(Normal, Normal) <= 1e-12f; };
        if (!Device || IndexCount == 0 || (!Geometry.Uvs.empty() && Geometry.Uvs.size() != VertexCount) ||
            UploadedVertices > std::numeric_limits<Uint32>::max() / sizeof(GpuMeshVertex) ||
            IndexCount > std::numeric_limits<Uint32>::max() / sizeof(uint32_t) ||
            std::any_of(Indices.begin(), Indices.end(), [&](uint32_t Index) { return Index >= VertexCount; }) ||
            (!FaceNormals && std::any_of(Geometry.Normals.begin(), Geometry.Normals.end(), ZeroNormal)))
            return nullptr;

        Entry.PendingVertices.resize(UploadedVertices);
        for (std::size_t Corner = 0; Corner < UploadedVertices; ++Corner)
        {
            GpuMeshVertex& Out = Entry.PendingVertices[Corner];
            const std::size_t Index = FaceNormals ? Indices[Corner] : Corner;
            const Vec3& Position = Geometry.Vertices[Index];
            Vec3 Normal{};
            if (!FaceNormals)
                Normal = Geometry.Normals[Index];
            else
            {
                const std::size_t First = Corner - Corner % 3;
                const Vec3& A = Geometry.Vertices[Indices[First]];
                const Vec3 Cross = Vec3Cross(Vec3Sub(Geometry.Vertices[Indices[First + 1]], A),
                                             Vec3Sub(Geometry.Vertices[Indices[First + 2]], A));
                // Only the direction counts, however small the triangle: the object's scale may bring it back.
                const float Largest = std::max({std::abs(Cross.X), std::abs(Cross.Y), std::abs(Cross.Z)});
                Normal = Largest > 0.0f ? Vec3Normalize(Vec3{Cross.X / Largest, Cross.Y / Largest, Cross.Z / Largest})
                                        : Vec3Normalize(Cross);
            }
            Out.Position[0] = Position.X;
            Out.Position[1] = Position.Y;
            Out.Position[2] = Position.Z;
            Out.Normal[0] = Normal.X;
            Out.Normal[1] = Normal.Y;
            Out.Normal[2] = Normal.Z;
            if (!Geometry.Uvs.empty())
            {
                Out.Uv[0] = Geometry.Uvs[Index].x;
                Out.Uv[1] = Geometry.Uvs[Index].y;
            }
            const uint32_t Packed = Index < Geometry.DiffuseColors.size() ? Geometry.DiffuseColors[Index] : 0xFFFFFFFFu;
            Out.Color[0] = static_cast<float>((Packed >> 16u) & 0xFFu) / 255.0f;
            Out.Color[1] = static_cast<float>((Packed >> 8u) & 0xFFu) / 255.0f;
            Out.Color[2] = static_cast<float>(Packed & 0xFFu) / 255.0f;
            // Opaque objects take their alpha from the object, never from the vertex colour.
            Out.Color[3] = 1.0f;
        }
        if (FaceNormals)
        {
            Entry.PendingIndices.resize(IndexCount);
            std::iota(Entry.PendingIndices.begin(), Entry.PendingIndices.end(), 0u);
        }
        else
            Entry.PendingIndices.assign(Indices.begin(), Indices.end());
        Entry.IndexCount = static_cast<Uint32>(IndexCount);

        SDL_GPUBufferCreateInfo Info{};
        Info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        Info.size = static_cast<Uint32>(UploadedVertices * sizeof(GpuMeshVertex));
        Entry.Vertices = SDL_CreateGPUBuffer(Device, &Info);
        Info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        Info.size = static_cast<Uint32>(IndexCount * sizeof(uint32_t));
        Entry.Indices = Entry.Vertices ? SDL_CreateGPUBuffer(Device, &Info) : nullptr;
        if (!Entry.Indices)
        {
            ReportRenderError(std::string("mesh buffer creation failed: ") + SDL_GetError());
            if (Entry.Vertices)
                SDL_ReleaseGPUBuffer(Device, Entry.Vertices);
            Entry.Vertices = nullptr;
            Entry.PendingVertices = {};
            Entry.PendingIndices = {};
            return nullptr;
        }
        Entry.Usable = true;
        return &Entry;
    }

    void RendererSystem::AppendGpuMeshes(const SceneTriangles& Scene, PreparedViewGeometry& Geometry)
    {
        const std::vector<GpuMeshDraw>& Draws = Scene.MeshDraws;
        Geometry.MeshInstances = static_cast<Uint32>(Draws.size());
        for (std::size_t First = 0; First < Draws.size();)
        {
            const GpuMeshDraw& Draw = Draws[First];
            std::size_t End = First + 1;
            while (End < Draws.size() && SameGpuMeshDraw(Draws[End], Draw))
                ++End;
            // Recreated here if released while a cached scene went undrawn.
            if (const GpuMeshBuffers* Entry = GpuMeshEntry(Draw.Geometry))
            {
                GpuBatch Batch;
                // Like CPU triangles, an untextured mesh draws with the white texture.
                Batch.Texture = Draw.Texture ? Draw.Texture : WhiteGpuTexture(Kernel_);
                Batch.Style = Draw.Style;
                Batch.MeshDraw = &Draw;
                Batch.FirstInstance = static_cast<Uint32>(First);
                Batch.InstanceCount = static_cast<Uint32>(End - First);
                Batch.StaticVertices = Entry->Vertices;
                Batch.StaticIndices = Entry->Indices;
                Batch.IndexCount = Entry->IndexCount;
                Geometry.Batches.push_back(Batch);
                Geometry.MeshIndices += uint64_t{Batch.IndexCount} * Batch.InstanceCount;
            }
            First = End;
        }
    }

    bool RendererSystem::UploadGpuMeshes(SDL_GPUCommandBuffer* Cmd)
    {
        std::size_t Bytes = 0;
        for (const auto& [Revision, Entry] : GpuMeshes_)
        {
            (void)Revision;
            Bytes +=
                Entry.PendingVertices.size() * sizeof(GpuMeshVertex) + Entry.PendingIndices.size() * sizeof(uint32_t);
        }
        if (Bytes == 0)
            return true;
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        if (!Device || !Cmd)
            return false;
        if (Bytes > std::numeric_limits<Uint32>::max())
        {
            ReportRenderError("mesh geometry exceeds the GPU transfer limit");
            return false;
        }
        if (!GpuMeshTransfer_ || GpuMeshTransferCapacity_ < Bytes)
        {
            if (GpuMeshTransfer_)
                SDL_ReleaseGPUTransferBuffer(Device, GpuMeshTransfer_);
            SDL_GPUTransferBufferCreateInfo Info{};
            Info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            Info.size = static_cast<Uint32>(
                std::min<std::size_t>(std::max<std::size_t>(Bytes, std::size_t{GpuMeshTransferCapacity_} * 2),
                                      std::numeric_limits<Uint32>::max()));
            GpuMeshTransfer_ = SDL_CreateGPUTransferBuffer(Device, &Info);
            GpuMeshTransferCapacity_ = GpuMeshTransfer_ ? Info.size : 0;
            if (!GpuMeshTransfer_)
            {
                ReportRenderError(std::string("mesh transfer buffer creation failed: ") + SDL_GetError());
                return false;
            }
        }

        auto* Mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, GpuMeshTransfer_, true));
        if (!Mapped)
            return false;
        std::size_t Offset = 0;
        for (const auto& [Revision, Entry] : GpuMeshes_)
        {
            (void)Revision;
            const std::size_t VertexBytes = Entry.PendingVertices.size() * sizeof(GpuMeshVertex);
            const std::size_t IndexBytes = Entry.PendingIndices.size() * sizeof(uint32_t);
            if (VertexBytes == 0)
                continue;
            SDL_memcpy(Mapped + Offset, Entry.PendingVertices.data(), VertexBytes);
            SDL_memcpy(Mapped + Offset + VertexBytes, Entry.PendingIndices.data(), IndexBytes);
            Offset += VertexBytes + IndexBytes;
        }
        SDL_UnmapGPUTransferBuffer(Device, GpuMeshTransfer_);

        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Cmd);
        if (!Copy)
            return false;
        Offset = 0;
        for (auto& [Revision, Entry] : GpuMeshes_)
        {
            (void)Revision;
            const auto VertexBytes = static_cast<Uint32>(Entry.PendingVertices.size() * sizeof(GpuMeshVertex));
            const auto IndexBytes = static_cast<Uint32>(Entry.PendingIndices.size() * sizeof(uint32_t));
            if (VertexBytes == 0)
                continue;
            const SDL_GPUTransferBufferLocation VertexSource{GpuMeshTransfer_, static_cast<Uint32>(Offset)};
            const SDL_GPUBufferRegion VertexDestination{Entry.Vertices, 0, VertexBytes};
            SDL_UploadToGPUBuffer(Copy, &VertexSource, &VertexDestination, false);
            const SDL_GPUTransferBufferLocation IndexSource{GpuMeshTransfer_,
                                                            static_cast<Uint32>(Offset + VertexBytes)};
            const SDL_GPUBufferRegion IndexDestination{Entry.Indices, 0, IndexBytes};
            SDL_UploadToGPUBuffer(Copy, &IndexSource, &IndexDestination, false);
            Offset += VertexBytes + IndexBytes;
            Entry.PendingVertices = {};
            Entry.PendingIndices = {};
        }
        SDL_EndGPUCopyPass(Copy);
        return true;
    }

    void RendererSystem::ReleaseUnusedGpuMeshes()
    {
        // About five seconds at 60 frames per second.
        constexpr std::size_t UnusedFrames = 300;
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        for (auto It = GpuMeshes_.begin(); It != GpuMeshes_.end();)
        {
            const std::shared_ptr<const MeshGeometry> Source = It->second.Source.lock();
            if (Source && Source->Revision == It->first && StatFrames_ - It->second.LastUsedFrame <= UnusedFrames)
            {
                ++It;
                continue;
            }
            // SDL keeps a buffer alive until the GPU has finished with it.
            if (Device && It->second.Vertices)
                SDL_ReleaseGPUBuffer(Device, It->second.Vertices);
            if (Device && It->second.Indices)
                SDL_ReleaseGPUBuffer(Device, It->second.Indices);
            It = GpuMeshes_.erase(It);
        }
    }

    void RendererSystem::ReleaseGpuMeshes()
    {
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        if (Device)
        {
            for (auto& [Revision, Entry] : GpuMeshes_)
            {
                (void)Revision;
                if (Entry.Vertices)
                    SDL_ReleaseGPUBuffer(Device, Entry.Vertices);
                if (Entry.Indices)
                    SDL_ReleaseGPUBuffer(Device, Entry.Indices);
            }
            if (GpuMeshTransfer_)
                SDL_ReleaseGPUTransferBuffer(Device, GpuMeshTransfer_);
        }
        GpuMeshes_.clear();
        GpuMeshTransfer_ = nullptr;
        GpuMeshTransferCapacity_ = 0;
    }
} // namespace dbc
