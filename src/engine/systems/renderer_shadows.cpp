#include "engine_kernel.hpp"
#include "graphics_device_system.hpp"
#include "light_manager.hpp"
#include "object_manager.hpp"
#include "renderer_internal.hpp"

namespace dbc
{
    namespace
    {
        [[nodiscard]] Vec3 WorldShadowLight(EngineKernel& K)
        {
            const int LightMode = K.Renderer().ShadowPositionMode();
            return LightMode == -1 ? K.Renderer().ShadowPosition()
                                   : Vec3{K.Lights().LightPositionX(LightMode), K.Lights().LightPositionY(LightMode),
                                          K.Lights().LightPositionZ(LightMode)};
        }

        [[nodiscard]] bool SameRevisions(const std::vector<uint64_t>& Revisions, const std::vector<Mesh>& Meshes)
        {
            if (Revisions.size() != Meshes.size())
                return false;
            for (std::size_t Index = 0; Index < Meshes.size(); ++Index)
            {
                if (Revisions[Index] != Meshes[Index].Geometry().Revision)
                    return false;
            }
            return true;
        }
    } // namespace

    const ShadowTopology& RendererSystem::ShadowTopologyOf(const Mesh& MeshValue)
    {
        const std::shared_ptr<const MeshGeometry> Geometry = MeshValue.SharedGeometry();
        const auto [Entry, Inserted] = ShadowTopologies_.try_emplace(Geometry->Revision);
        if (Inserted)
        {
            Entry->second.Source = Geometry;
            Entry->second.Topology = BuildShadowTopology(*Geometry);
        }
        return Entry->second.Topology;
    }

    void RendererSystem::ReleaseStaleShadowTopologies()
    {
        std::erase_if(ShadowTopologies_,
                      [](const auto& Entry)
                      {
                          const std::shared_ptr<const MeshGeometry> Source = Entry.second.Source.lock();
                          return !Source || Source->Revision != Entry.first;
                      });
    }

    void RendererSystem::UpdateShadowCasters()
    {
        EngineKernel& K = Kernel_;
        const Vec3 Light = WorldShadowLight(K);
        const bool LightMoved =
            Light.X != ShadowCasterLight_.X || Light.Y != ShadowCasterLight_.Y || Light.Z != ShadowCasterLight_.Z;
        ShadowCasterLight_ = Light;
        ++ShadowCasterWalk_;
        bool Changed = false;
        K.Objects().ForEachObject(
            [&](int Id, const Object3D& Object)
            {
                if (!Object.Visible || Object.Excluded || !Object.ShadowShading || Object.ShadowRange <= 0.0f)
                    return;
                // A locked object lives on the screen, not in the world: only a shadow mesh of its own casts.
                if (Object.LockToScreen && Object.ShadowMeshes.empty())
                    return;
                const auto [Entry, Inserted] = ShadowCasters_.try_emplace(Id);
                ShadowCaster& Caster = Entry->second;
                Caster.LastWalk = ShadowCasterWalk_;
                const std::vector<Mesh>& Meshes = Object.ShadowMeshes.empty() ? Object.Meshes : Object.ShadowMeshes;
                // A glued object follows a target whose changes do not mark it. Geometry edits need not mark it either.
                if (!Inserted && !LightMoved && !Object.TransformDirty && !Object.Playing && !Object.Looping &&
                    Object.GluedTargetObject <= 0 && SameRevisions(Caster.Revisions, Meshes))
                    return;
                Caster.Vertices.clear();
                Caster.NearCapIndices.clear();
                Caster.Indices.clear();
                Caster.Revisions.clear();
                for (const Mesh& MeshValue : Meshes)
                {
                    Caster.Revisions.push_back(MeshValue.Geometry().Revision);
                    if (!MeshValue.Visible || MeshValue.Excluded)
                        continue;
                    if (!Object.CachedNodeGlobals.empty() && MeshValue.SourceMeshIndex >= 0)
                    {
                        const auto Node =
                            Object.MeshNodeByIndex.find(static_cast<unsigned int>(MeshValue.SourceMeshIndex));
                        if (Node != Object.MeshNodeByIndex.end() && NodeHiddenByLimb(Object, Node->second))
                            continue;
                    }
                    WorldMeshVertices(Object, MeshValue, ShadowWorldVertices_);
                    ExtrudeShadowMesh(ShadowTopologyOf(MeshValue), ShadowWorldVertices_, Light, Object.ShadowRange,
                                      ShadowLitFaces_, Caster.Vertices, Caster.NearCapIndices, Caster.Indices);
                }
                ++Current_.ShadowCastersExtruded;
                Changed = true;
            });
        const auto Gone = [this](const auto& Entry) { return Entry.second.LastWalk != ShadowCasterWalk_; };
        Changed |= std::erase_if(ShadowCasters_, Gone) != 0;
        if (!Changed)
            return;
        ShadowVolumesStale_ = true;
        ShadowVolumeVertexCount_ = 0;
        ShadowNearCapIndexCount_ = 0;
        ShadowVolumeIndexCount_ = 0;
        for (const auto& [Id, Caster] : ShadowCasters_)
        {
            (void)Id;
            ShadowVolumeVertexCount_ += Caster.Vertices.size();
            ShadowNearCapIndexCount_ += Caster.NearCapIndices.size();
            ShadowVolumeIndexCount_ += Caster.NearCapIndices.size() + Caster.Indices.size();
        }
    }

    bool RendererSystem::UploadShadowVolumes(SDL_GPUCommandBuffer* Cmd)
    {
        if (!ShadowVolumesStale_)
            return true;
        if (ShadowVolumeIndexCount_ == 0)
        {
            ShadowVolumesStale_ = false;
            return true;
        }
        SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice();
        if (!Device || !Cmd)
            return false;
        const std::size_t VertexBytes = ShadowVolumeVertexCount_ * sizeof(Vec3);
        const std::size_t IndexBytes = ShadowVolumeIndexCount_ * sizeof(uint32_t);
        if (VertexBytes + IndexBytes > std::numeric_limits<Uint32>::max())
        {
            ReportRenderError("shadow volumes exceed the GPU transfer limit");
            return false;
        }
        const auto EnsureBuffer =
            [&](SDL_GPUBuffer*& Buffer, Uint32& Capacity, SDL_GPUBufferUsageFlags Usage, std::size_t Bytes)
        {
            if (Buffer && Capacity >= Bytes)
                return true;
            if (Buffer)
                SDL_ReleaseGPUBuffer(Device, Buffer);
            SDL_GPUBufferCreateInfo Info{};
            Info.usage = Usage;
            Info.size = static_cast<Uint32>(std::min<std::size_t>(
                std::max<std::size_t>(Bytes, std::size_t{Capacity} * 2), std::numeric_limits<Uint32>::max()));
            Buffer = SDL_CreateGPUBuffer(Device, &Info);
            Capacity = Buffer ? Info.size : 0;
            if (!Buffer)
                ReportRenderError(std::string("shadow volume buffer creation failed: ") + SDL_GetError());
            return Buffer != nullptr;
        };
        if (!EnsureBuffer(ShadowVertexBuffer_, ShadowVertexCapacity_, SDL_GPU_BUFFERUSAGE_VERTEX, VertexBytes) ||
            !EnsureBuffer(ShadowIndexBuffer_, ShadowIndexCapacity_, SDL_GPU_BUFFERUSAGE_INDEX, IndexBytes))
            return false;
        SDL_GPUTransferBuffer* Transfer = MeshTransferBuffer(VertexBytes + IndexBytes);
        if (!Transfer)
            return false;

        auto* Mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, Transfer, true));
        if (!Mapped)
            return false;
        std::byte* VertexOutput = Mapped;
        auto* NearCapOutput = reinterpret_cast<uint32_t*>(Mapped + VertexBytes);
        uint32_t* IndexOutput = NearCapOutput + ShadowNearCapIndexCount_;
        uint32_t BaseVertex = 0;
        for (const auto& [Id, Caster] : ShadowCasters_)
        {
            (void)Id;
            if (Caster.Indices.empty())
                continue;
            SDL_memcpy(VertexOutput, Caster.Vertices.data(), Caster.Vertices.size() * sizeof(Vec3));
            VertexOutput += Caster.Vertices.size() * sizeof(Vec3);
            for (const uint32_t Index : Caster.NearCapIndices)
                *NearCapOutput++ = BaseVertex + Index;
            for (const uint32_t Index : Caster.Indices)
                *IndexOutput++ = BaseVertex + Index;
            BaseVertex += static_cast<uint32_t>(Caster.Vertices.size());
        }
        SDL_UnmapGPUTransferBuffer(Device, Transfer);

        SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Cmd);
        if (!Copy)
            return false;
        const SDL_GPUTransferBufferLocation VertexSource{Transfer, 0};
        const SDL_GPUBufferRegion VertexDestination{ShadowVertexBuffer_, 0, static_cast<Uint32>(VertexBytes)};
        SDL_UploadToGPUBuffer(Copy, &VertexSource, &VertexDestination, true);
        const SDL_GPUTransferBufferLocation IndexSource{Transfer, static_cast<Uint32>(VertexBytes)};
        const SDL_GPUBufferRegion IndexDestination{ShadowIndexBuffer_, 0, static_cast<Uint32>(IndexBytes)};
        SDL_UploadToGPUBuffer(Copy, &IndexSource, &IndexDestination, true);
        SDL_EndGPUCopyPass(Copy);
        ShadowVolumesStale_ = false;
        return true;
    }

    void RendererSystem::ReleaseShadowVolumes()
    {
        if (SDL_GPUDevice* Device = Kernel_.Gpu().GpuDevice())
        {
            if (ShadowVertexBuffer_)
                SDL_ReleaseGPUBuffer(Device, ShadowVertexBuffer_);
            if (ShadowIndexBuffer_)
                SDL_ReleaseGPUBuffer(Device, ShadowIndexBuffer_);
        }
        ShadowVertexBuffer_ = nullptr;
        ShadowIndexBuffer_ = nullptr;
        ShadowVertexCapacity_ = 0;
        ShadowIndexCapacity_ = 0;
    }
} // namespace dbc
