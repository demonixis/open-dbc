#include "scene/scene_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/trigonometric.hpp>

#include "scene_shaders.inc"

namespace dbc::studio
{
    namespace scene
    {
        namespace
        {
            glm::vec3 ColorOf(Rgb Value)
            {
                return glm::vec3(static_cast<float>((Value >> 16) & 0xFF), static_cast<float>((Value >> 8) & 0xFF),
                                 static_cast<float>(Value & 0xFF)) /
                       255.0f;
            }

            std::string NodeId(const SceneNode& Node)
            {
                return std::to_string(static_cast<int>(IdSpaceOf(Node.Kind))) + ":" + std::to_string(Node.Id);
            }

            constexpr float KDefaultAmbient = 0.25f;
            constexpr float KDefaultFogDistance = 3000.0f;
            constexpr float KDefaultLightRange = 1000.0f;
        } // namespace

        SDL_GPUShader* SceneRenderer::CreateShader(const unsigned char* Spirv, std::size_t SpirvSize,
                                                   const unsigned char* Msl, std::size_t MslSize,
                                                   SDL_GPUShaderStage Stage, Uint32 Samplers)
        {
            const bool UseSpirv = (SDL_GetGPUShaderFormats(Gpu_) & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
            SDL_GPUShaderCreateInfo Info = {};
            Info.code = UseSpirv ? Spirv : Msl;
            Info.code_size = UseSpirv ? SpirvSize : MslSize;
            Info.format = UseSpirv ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_MSL;
            Info.entrypoint = Stage == SDL_GPU_SHADERSTAGE_VERTEX ? "VSMain" : "PSMain";
            Info.stage = Stage;
            Info.num_samplers = Samplers;
            Info.num_uniform_buffers = 1;
            return SDL_CreateGPUShader(Gpu_, &Info);
        }

        bool SceneRenderer::CreatePipelines(std::string& Error)
        {
            SDL_GPUShader* MeshVertexShader = CreateShader(K_scene_mesh_vert_spv, sizeof(K_scene_mesh_vert_spv),
                                                     K_scene_mesh_vert_msl, sizeof(K_scene_mesh_vert_msl),
                                                     SDL_GPU_SHADERSTAGE_VERTEX, 0);
            SDL_GPUShader* MeshFragment = CreateShader(K_scene_mesh_frag_spv, sizeof(K_scene_mesh_frag_spv),
                                                       K_scene_mesh_frag_msl, sizeof(K_scene_mesh_frag_msl),
                                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
            SDL_GPUShader* LineVertexShader = CreateShader(K_scene_line_vert_spv, sizeof(K_scene_line_vert_spv),
                                                           K_scene_line_vert_msl, sizeof(K_scene_line_vert_msl),
                                                           SDL_GPU_SHADERSTAGE_VERTEX, 0);
            SDL_GPUShader* LineFragment = CreateShader(K_scene_line_frag_spv, sizeof(K_scene_line_frag_spv),
                                                       K_scene_line_frag_msl, sizeof(K_scene_line_frag_msl),
                                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 0);
            const auto Release = [&]()
            {
                for (SDL_GPUShader* Shader : {MeshVertexShader, MeshFragment, LineVertexShader, LineFragment})
                    if (Shader != nullptr)
                        SDL_ReleaseGPUShader(Gpu_, Shader);
            };
            if (!MeshVertexShader || !MeshFragment || !LineVertexShader || !LineFragment)
            {
                Error = std::string("Scene shaders: ") + SDL_GetError();
                Release();
                return false;
            }

            SDL_GPUColorTargetDescription Color = {};
            Color.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

            SDL_GPUVertexBufferDescription MeshBuffer = {};
            MeshBuffer.slot = 0;
            MeshBuffer.pitch = sizeof(MeshVertex);
            MeshBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const SDL_GPUVertexAttribute MeshAttributes[] = {
                {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(MeshVertex, Position))},
                {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(MeshVertex, Normal))},
                {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(MeshVertex, Uv))}};

            SDL_GPUGraphicsPipelineCreateInfo Info = {};
            Info.vertex_shader = MeshVertexShader;
            Info.fragment_shader = MeshFragment;
            Info.vertex_input_state.vertex_buffer_descriptions = &MeshBuffer;
            Info.vertex_input_state.num_vertex_buffers = 1;
            Info.vertex_input_state.vertex_attributes = MeshAttributes;
            Info.vertex_input_state.num_vertex_attributes = 3;
            Info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            // Both sides are drawn: the editor shows a face from behind too.
            Info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            Info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            Info.depth_stencil_state.enable_depth_test = true;
            Info.depth_stencil_state.enable_depth_write = true;
            Info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            Info.target_info.color_target_descriptions = &Color;
            Info.target_info.num_color_targets = 1;
            Info.target_info.depth_stencil_format = DepthFormat_;
            Info.target_info.has_depth_stencil_target = true;
            MeshPipeline_ = SDL_CreateGPUGraphicsPipeline(Gpu_, &Info);

            Info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
            WirePipeline_ = SDL_CreateGPUGraphicsPipeline(Gpu_, &Info);
            Info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

            Color.blend_state.enable_blend = true;
            Color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            Color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            Color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            Color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            Color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            Color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            Info.depth_stencil_state.enable_depth_write = false;
            BlendPipeline_ = SDL_CreateGPUGraphicsPipeline(Gpu_, &Info);

            SDL_GPUVertexBufferDescription LineBuffer = {};
            LineBuffer.slot = 0;
            LineBuffer.pitch = sizeof(LineVertex);
            LineBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            const SDL_GPUVertexAttribute LineAttributes[] = {
                {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(LineVertex, Position))},
                {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, static_cast<Uint32>(offsetof(LineVertex, Color))}};
            Info.vertex_shader = LineVertexShader;
            Info.fragment_shader = LineFragment;
            Info.vertex_input_state.vertex_buffer_descriptions = &LineBuffer;
            Info.vertex_input_state.vertex_attributes = LineAttributes;
            Info.vertex_input_state.num_vertex_attributes = 2;
            Info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
            LinePipeline_ = SDL_CreateGPUGraphicsPipeline(Gpu_, &Info);

            Release();
            if (!MeshPipeline_ || !WirePipeline_ || !BlendPipeline_ || !LinePipeline_)
            {
                Error = std::string("Scene pipelines: ") + SDL_GetError();
                return false;
            }
            return true;
        }

        bool SceneRenderer::Initialize(SDL_GPUDevice* Gpu, std::string& Error)
        {
            Gpu_ = Gpu;
            if (!SDL_GPUTextureSupportsFormat(Gpu_, DepthFormat_, SDL_GPU_TEXTURETYPE_2D,
                                              SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
                DepthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
            if (!CreatePipelines(Error))
                return false;

            SDL_GPUSamplerCreateInfo SamplerInfo = {};
            SamplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
            SamplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
            SamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            SamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            Sampler_ = SDL_CreateGPUSampler(Gpu_, &SamplerInfo);

            SDL_Surface* White = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_ABGR8888);
            if (White != nullptr)
            {
                SDL_FillSurfaceRect(White, nullptr, 0xFFFFFFFF);
                core::UploadGpuImage(Gpu_, White, White_, Error);
                SDL_DestroySurface(White);
            }
            if (Sampler_ == nullptr || White_.Texture == nullptr)
            {
                Error = std::string("Scene sampler: ") + SDL_GetError();
                return false;
            }
            return true;
        }

        void SceneRenderer::Shutdown()
        {
            if (Gpu_ == nullptr)
                return;
            for (auto& [Key, Entry] : Geometry_)
                if (Entry.Buffer != nullptr)
                    SDL_ReleaseGPUBuffer(Gpu_, Entry.Buffer);
            Geometry_.clear();
            for (auto& [Key, Image] : Textures_)
                core::ReleaseGpuImage(Gpu_, Image);
            Textures_.clear();
            core::ReleaseGpuImage(Gpu_, White_);
            if (LineBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(Gpu_, LineBuffer_);
            LineBuffer_ = nullptr;
            for (SDL_GPUGraphicsPipeline* Pipeline : {MeshPipeline_, BlendPipeline_, WirePipeline_, LinePipeline_})
                if (Pipeline != nullptr)
                    SDL_ReleaseGPUGraphicsPipeline(Gpu_, Pipeline);
            MeshPipeline_ = BlendPipeline_ = WirePipeline_ = LinePipeline_ = nullptr;
            if (Sampler_ != nullptr)
                SDL_ReleaseGPUSampler(Gpu_, Sampler_);
            Sampler_ = nullptr;
            ReleaseTargets();
            Gpu_ = nullptr;
        }

        void SceneRenderer::ReleaseTargets()
        {
            // SDL keeps a released texture alive until the GPU no longer uses it.
            if (Color_ != nullptr)
                SDL_ReleaseGPUTexture(Gpu_, Color_);
            if (Depth_ != nullptr)
                SDL_ReleaseGPUTexture(Gpu_, Depth_);
            Color_ = Depth_ = nullptr;
            Width_ = Height_ = 0;
        }

        SDL_GPUTexture* SceneRenderer::Target(int Width, int Height)
        {
            if (Gpu_ == nullptr || MeshPipeline_ == nullptr || Width <= 0 || Height <= 0)
                return nullptr;
            if (Color_ != nullptr && Width == Width_ && Height == Height_)
                return Color_;
            ReleaseTargets();
            SDL_GPUTextureCreateInfo Info = {};
            Info.type = SDL_GPU_TEXTURETYPE_2D;
            Info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            Info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            Info.width = static_cast<Uint32>(Width);
            Info.height = static_cast<Uint32>(Height);
            Info.layer_count_or_depth = 1;
            Info.num_levels = 1;
            Color_ = SDL_CreateGPUTexture(Gpu_, &Info);
            Info.format = DepthFormat_;
            Info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            Depth_ = SDL_CreateGPUTexture(Gpu_, &Info);
            if (Color_ == nullptr || Depth_ == nullptr)
            {
                ReleaseTargets();
                return nullptr;
            }
            Width_ = Width;
            Height_ = Height;
            return Color_;
        }

        std::string SceneRenderer::GeometryKey(const SceneNode& Node, const SceneDocument& Document,
                                               const std::filesystem::path& AssetRoot) const
        {
            const auto F = [](float Value) { return std::to_string(Value); };
            switch (Node.Kind)
            {
                case NodeKind::Model:
                    return "model:" + (AssetRoot / Node.Asset).lexically_normal().string();
                case NodeKind::Terrain:
                    return "terrain:" + (AssetRoot / Node.Asset).lexically_normal().string();
                case NodeKind::Light:
                    return {};
                case NodeKind::Matrix:
                {
                    // FNV-1a over the heights: the key stays short for large matrices.
                    std::uint64_t Hash = 14695981039346656037ull;
                    for (const float Height : MatrixHeights(Node))
                    {
                        std::uint32_t Bits = 0;
                        std::memcpy(&Bits, &Height, sizeof(Bits));
                        Hash = (Hash ^ Bits) * 1099511628211ull;
                    }
                    return "matrix:" + F(Node.Width) + "," + F(Node.Depth) + "," + std::to_string(Node.XSegments) +
                           "," + std::to_string(Node.ZSegments) + "," + std::to_string(Node.TilesAcross) + "," +
                           std::to_string(Node.TilesDown) + "," + std::to_string(MatrixTile(Node)) + "," +
                           std::to_string(Hash);
                }
                case NodeKind::Triangle:
                {
                    std::string Key = "triangle";
                    for (const glm::vec3& Corner : Node.Corners)
                        Key += ":" + F(Corner.x) + "," + F(Corner.y) + "," + F(Corner.z);
                    return Key;
                }
                default:
                    break;
            }
            std::string Key = std::string(NodeKindLabel(Node.Kind)) + ":" + F(Node.Size) + "," + F(Node.Extent.x) +
                              "," + F(Node.Extent.y) + "," + F(Node.Extent.z);
            if (Node.Detail)
                Key += "," + std::to_string(Node.Detail->x) + "," + std::to_string(Node.Detail->y);
            if (Node.Kind == NodeKind::Plain)
                Key += Document.Profile == dbc::language::LanguageProfile::Dbc ? ",classic" : ",pro";
            return Key;
        }

        SceneRenderer::Geometry& SceneRenderer::FindGeometry(const SceneNode& Node, const SceneDocument& Document,
                                                             const std::filesystem::path& AssetRoot)
        {
            const std::string Key = GeometryKey(Node, Document, AssetRoot);
            NodeKeys_[NodeId(Node)] = Key;
            auto Found = Geometry_.find(Key);
            if (Found != Geometry_.end())
            {
                Found->second.Used = true;
                return Found->second;
            }
            Geometry Entry;
            if (Node.Kind == NodeKind::Model || Node.Kind == NodeKind::Terrain)
            {
                const std::filesystem::path Path = AssetRoot / Node.Asset;
                std::string Error;
                const bool Loaded = !Node.Asset.empty() && (Node.Kind == NodeKind::Model
                                                                ? LoadModelMesh(Path, Entry.Mesh, Error)
                                                                : LoadTerrainMesh(Path, Entry.Mesh, Error));
                if (!Loaded)
                {
                    // A box stands in, so the node can still be seen and picked.
                    if (!Node.Asset.empty() && Failed_.emplace(Key, true).second)
                        Errors_.push_back("Scene preview: cannot load " + Path.string() + ": " + Error);
                    SceneNode Placeholder;
                    Placeholder.Kind = NodeKind::Cube;
                    Entry.Mesh = BuildMesh(Placeholder, Document.Profile);
                }
            }
            else
            {
                Entry.Mesh = BuildMesh(Node, Document.Profile);
            }
            Entry.Used = true;
            return Geometry_.emplace(Key, std::move(Entry)).first->second;
        }

        SDL_GPUTexture* SceneRenderer::FindTexture(const std::filesystem::path& Path)
        {
            const std::string Key = Path.lexically_normal().string();
            if (const auto Found = Textures_.find(Key); Found != Textures_.end())
                return Found->second.Texture;
            if (Failed_.count(Key) != 0)
                return nullptr;
            core::GpuImage Image;
            std::string Error;
            if (!core::LoadGpuImage(Gpu_, Path, Image, Error))
            {
                Failed_.emplace(Key, true);
                Errors_.push_back("Scene preview: " + Path.string() + ": " + Error);
                return nullptr;
            }
            return Textures_.emplace(Key, Image).first->second.Texture;
        }

        const CpuMesh* SceneRenderer::Mesh(const SceneNode& Node) const
        {
            const auto Key = NodeKeys_.find(NodeId(Node));
            if (Key == NodeKeys_.end())
                return nullptr;
            const auto Found = Geometry_.find(Key->second);
            return Found == Geometry_.end() ? nullptr : &Found->second.Mesh;
        }

        std::vector<std::string> SceneRenderer::TakeErrors()
        {
            return std::exchange(Errors_, {});
        }

        void SceneRenderer::Prepare(const SceneDocument& Document, const ViewParameters& View,
                                    const std::filesystem::path& AssetRoot, const std::vector<LineVertex>& Lines)
        {
            Items_.clear();
            Lines_ = Lines;
            if (Gpu_ == nullptr || Color_ == nullptr)
                return;
            for (auto& [Key, Entry] : Geometry_)
                Entry.Used = false;
            NodeKeys_.clear();

            const Environment& Env = Document.Env;
            const bool Pro = Document.Profile == dbc::language::LanguageProfile::DbPro;
            ViewProj_ = View.Projection * View.View;
            Clear_ = glm::vec4(ColorOf(Env.Backdrop.value_or(0x000000)), 1.0f);

            FragmentUniforms Shared;
            Shared.Ambient = glm::vec4(ColorOf(Env.AmbientColor.value_or(0xFFFFFF)) *
                                           (Env.AmbientPercent ? *Env.AmbientPercent / 100.0f : KDefaultAmbient),
                                       1.0f);
            Shared.Camera = glm::vec4(View.Eye, 1.0f);
            if (Env.Fog)
                Shared.Fog = glm::vec4(ColorOf(Env.FogColor.value_or(0x000000)),
                                       Env.FogDistance.value_or(KDefaultFogDistance));
            int Count = 0;
            const auto AddLight = [&](int Type, const glm::vec3& Position, const glm::vec3& Direction, float Range,
                                      Rgb Color, float Inner, float Outer)
            {
                if (Count >= KMaxLights)
                    return;
                Shared.LightPosition[Count] = glm::vec4(Position, static_cast<float>(Type));
                Shared.LightDirection[Count] = glm::vec4(Direction, Range);
                Shared.LightColor[Count] = glm::vec4(ColorOf(Color), 1.0f);
                // Spot angles are the full cones.
                Shared.LightCone[Count] = glm::vec4(std::cos(glm::radians(Inner * 0.5f)),
                                                    std::cos(glm::radians(Outer * 0.5f)), 0.0f, 0.0f);
                ++Count;
            };
            if (!Env.DefaultLightHidden)
                AddLight(0, glm::vec3(0.0f),
                         Env.DefaultLightRotation ? LightDirection(*Env.DefaultLightRotation) : KDefaultLightDirection,
                         0.0f, Env.DefaultLightColor.value_or(0xFFFFFF), 0.0f, 0.0f);
            for (const SceneNode& Node : Document.Nodes)
            {
                if (Node.Kind != NodeKind::Light)
                    continue;
                const int Type = Node.Light == LightKind::Directional ? 0 : Node.Light == LightKind::Point ? 1 : 2;
                AddLight(Type, Node.Position, LightDirection(Node.Rotation), Node.Range.value_or(KDefaultLightRange),
                         Node.LightColor.value_or(0xFFFFFF), Node.SpotInner, Node.SpotOuter);
            }
            Shared.Flags.w = static_cast<float>(Count);

            for (const SceneNode& Node : Document.Nodes)
            {
                if (Node.Kind == NodeKind::Light)
                    continue;
                DrawItem Item;
                Item.Mesh = &FindGeometry(Node, Document, AssetRoot);
                if (Item.Mesh->Mesh.Vertices.empty())
                    continue;
                Item.Vertex.ViewProj = ViewProj_;
                Item.Vertex.World = NodeWorld(Node);
                Item.Vertex.NormalWorld = glm::mat4(glm::inverseTranspose(glm::mat3(Item.Vertex.World)));

                const Material& Mat = Node.Mat;
                Item.Fragment = Shared;
                glm::vec3 Base = ColorOf(Mat.Color.value_or(0xFFFFFF));
                if (Pro && Mat.Diffuse)
                    Base = ColorOf(*Mat.Diffuse);
                Item.Fragment.BaseColor = glm::vec4(Base, Mat.Ghost ? 0.5f : 1.0f);
                if (Pro && Mat.Emissive)
                    Item.Fragment.Emissive = glm::vec4(ColorOf(*Mat.Emissive), 0.0f);
                if (Pro && Mat.Specular)
                    Item.Fragment.Specular = glm::vec4(ColorOf(*Mat.Specular), Mat.SpecularPower.value_or(10.0f));
                if (!Mat.Flags.Fog)
                    Item.Fragment.Fog.w = 0.0f;
                if (!Mat.Flags.Ambient)
                    Item.Fragment.Ambient = glm::vec4(0.0f);
                Item.Fragment.Flags.y = Mat.Flags.Lighting ? 1.0f : 0.0f;
                Item.Texture = Mat.Texture.empty() ? nullptr : FindTexture(AssetRoot / Mat.Texture);
                Item.Fragment.Flags.x = Item.Texture != nullptr ? 1.0f : 0.0f;
                Item.Blend = Mat.Ghost;
                Item.Wireframe = Mat.Flags.Wireframe;
                Items_.push_back(Item);
            }
            // Ghosts last, so what is behind them is already drawn.
            std::stable_partition(Items_.begin(), Items_.end(), [](const DrawItem& Item) { return !Item.Blend; });

            for (auto Entry = Geometry_.begin(); Entry != Geometry_.end();)
            {
                if (Entry->second.Used)
                {
                    ++Entry;
                    continue;
                }
                if (Entry->second.Buffer != nullptr)
                    SDL_ReleaseGPUBuffer(Gpu_, Entry->second.Buffer);
                Entry = Geometry_.erase(Entry);
            }
            Pending_ = true;
        }

        bool SceneRenderer::Upload(SDL_GPUCopyPass* Pass, SDL_GPUBuffer*& Buffer, Uint32& Capacity, const void* Data,
                                   Uint32 Size, std::vector<SDL_GPUTransferBuffer*>& Transfers)
        {
            if (Buffer == nullptr || Capacity < Size)
            {
                if (Buffer != nullptr)
                    SDL_ReleaseGPUBuffer(Gpu_, Buffer);
                SDL_GPUBufferCreateInfo Info = {};
                Info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                Info.size = Size;
                Buffer = SDL_CreateGPUBuffer(Gpu_, &Info);
                Capacity = Buffer != nullptr ? Size : 0;
                if (Buffer == nullptr)
                    return false;
            }
            SDL_GPUTransferBufferCreateInfo TransferInfo = {};
            TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            TransferInfo.size = Size;
            SDL_GPUTransferBuffer* Transfer = SDL_CreateGPUTransferBuffer(Gpu_, &TransferInfo);
            if (Transfer == nullptr)
                return false;
            Transfers.push_back(Transfer);
            void* Mapped = SDL_MapGPUTransferBuffer(Gpu_, Transfer, false);
            if (Mapped == nullptr)
                return false;
            SDL_memcpy(Mapped, Data, Size);
            SDL_UnmapGPUTransferBuffer(Gpu_, Transfer);
            SDL_GPUTransferBufferLocation From = {Transfer, 0};
            SDL_GPUBufferRegion To = {Buffer, 0, Size};
            SDL_UploadToGPUBuffer(Pass, &From, &To, false);
            return true;
        }

        void SceneRenderer::Record(SDL_GPUCommandBuffer* Commands)
        {
            if (!Pending_ || Color_ == nullptr)
                return;
            Pending_ = false;

            std::vector<SDL_GPUTransferBuffer*> Transfers;
            SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Commands);
            if (Copy == nullptr)
                return;
            for (auto& [Key, Entry] : Geometry_)
            {
                if (Entry.Uploaded || Entry.Mesh.Vertices.empty())
                    continue;
                Uint32 Capacity = 0;
                Entry.Uploaded = Upload(Copy, Entry.Buffer, Capacity, Entry.Mesh.Vertices.data(),
                                        static_cast<Uint32>(Entry.Mesh.Vertices.size() * sizeof(MeshVertex)),
                                        Transfers);
            }
            const bool HasLines = !Lines_.empty() &&
                                  Upload(Copy, LineBuffer_, LineCapacity_, Lines_.data(),
                                         static_cast<Uint32>(Lines_.size() * sizeof(LineVertex)), Transfers);
            SDL_EndGPUCopyPass(Copy);
            // Released now, freed once the copy has run.
            for (SDL_GPUTransferBuffer* Transfer : Transfers)
                SDL_ReleaseGPUTransferBuffer(Gpu_, Transfer);

            SDL_GPUColorTargetInfo Color = {};
            Color.texture = Color_;
            Color.clear_color = SDL_FColor{Clear_.r, Clear_.g, Clear_.b, 1.0f};
            Color.load_op = SDL_GPU_LOADOP_CLEAR;
            Color.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPUDepthStencilTargetInfo Depth = {};
            Depth.texture = Depth_;
            Depth.clear_depth = 1.0f;
            Depth.load_op = SDL_GPU_LOADOP_CLEAR;
            Depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
            Depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            Depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_GPURenderPass* Pass = SDL_BeginGPURenderPass(Commands, &Color, 1, &Depth);
            if (Pass == nullptr)
                return;

            SDL_GPUGraphicsPipeline* Bound = nullptr;
            for (const DrawItem& Item : Items_)
            {
                if (!Item.Mesh->Uploaded)
                    continue;
                SDL_GPUGraphicsPipeline* Pipeline =
                    Item.Wireframe ? WirePipeline_ : Item.Blend ? BlendPipeline_ : MeshPipeline_;
                if (Pipeline != Bound)
                {
                    SDL_BindGPUGraphicsPipeline(Pass, Pipeline);
                    Bound = Pipeline;
                }
                SDL_PushGPUVertexUniformData(Commands, 0, &Item.Vertex, sizeof(Item.Vertex));
                SDL_PushGPUFragmentUniformData(Commands, 0, &Item.Fragment, sizeof(Item.Fragment));
                const SDL_GPUTextureSamplerBinding Binding = {Item.Texture ? Item.Texture : White_.Texture, Sampler_};
                SDL_BindGPUFragmentSamplers(Pass, 0, &Binding, 1);
                const SDL_GPUBufferBinding Buffer = {Item.Mesh->Buffer, 0};
                SDL_BindGPUVertexBuffers(Pass, 0, &Buffer, 1);
                SDL_DrawGPUPrimitives(Pass, static_cast<Uint32>(Item.Mesh->Mesh.Vertices.size()), 1, 0, 0);
            }
            if (HasLines)
            {
                SDL_BindGPUGraphicsPipeline(Pass, LinePipeline_);
                SDL_PushGPUVertexUniformData(Commands, 0, &ViewProj_, sizeof(ViewProj_));
                const SDL_GPUBufferBinding Buffer = {LineBuffer_, 0};
                SDL_BindGPUVertexBuffers(Pass, 0, &Buffer, 1);
                SDL_DrawGPUPrimitives(Pass, static_cast<Uint32>(Lines_.size()), 1, 0, 0);
            }
            SDL_EndGPURenderPass(Pass);
        }
    } // namespace scene

} // namespace dbc::studio
