#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "core/gpu_texture.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_geometry.hpp"

namespace dbc::studio
{
    namespace scene
    {
        struct ViewParameters
        {
            glm::mat4 View{1.0f};
            glm::mat4 Projection{1.0f};
            glm::vec3 Eye{0.0f};
        };

        // Lines drawn over the scene: grid, bounds, light and camera helpers.
        struct LineVertex
        {
            glm::vec3 Position{0.0f};
            glm::vec4 Color{1.0f};
        };

        // The Scene view's own SDL_GPU renderer. It previews the level with simple
        // per-pixel lighting; the program itself is drawn by the engine.
        class SceneRenderer
        {
          public:
            static constexpr int KMaxLights = 8;

            bool Initialize(SDL_GPUDevice* Gpu, std::string& Error);
            void Shutdown();

            // Resizes the colour and depth targets during the UI pass, before ImGui
            // records the image, and returns the texture to show. nullptr on failure.
            SDL_GPUTexture* Target(int Width, int Height);

            // Builds the frame's draw list; Record() then draws it once.
            void Prepare(const SceneDocument& Document, const ViewParameters& View,
                         const std::filesystem::path& AssetRoot, const std::vector<LineVertex>& Lines);
            void Record(SDL_GPUCommandBuffer* Commands);

            // The node's geometry after Prepare, for picking and bounds; nullptr when
            // the node has none yet.
            const CpuMesh* Mesh(const SceneNode& Node) const;
            // Messages for files that could not be loaded, once per file.
            std::vector<std::string> TakeErrors();

            // Draws the model alone, framed from the front, three-quarters and above,
            // into a new Size x Size sampler texture. Waits for the GPU like
            // core::LoadGpuImage; uses this renderer's targets, so not the Scene view's.
            bool RenderThumbnail(const std::filesystem::path& AssetRoot, const std::string& Model, int Size,
                                 Rgb Backdrop, core::GpuImage& Out, std::string& Error);

          private:
            struct Geometry
            {
                CpuMesh Mesh;
                std::filesystem::path Folder; // where a model's material textures are looked up first
                std::vector<core::GpuImage> Images; // Mesh.Images once uploaded
                SDL_GPUBuffer* Buffer = nullptr;
                bool Uploaded = false;
                bool Used = false;
            };

            struct VertexUniforms
            {
                glm::mat4 ViewProj{1.0f};
                glm::mat4 World{1.0f};
                glm::mat4 NormalWorld{1.0f};
            };

            // Matches FragmentUniforms in scene_mesh.hlsl.
            struct FragmentUniforms
            {
                glm::vec4 BaseColor{1.0f};
                glm::vec4 Emissive{0.0f};
                glm::vec4 Specular{0.0f};
                glm::vec4 Ambient{0.0f};
                glm::vec4 Camera{0.0f};
                glm::vec4 Fog{0.0f};
                glm::vec4 Flags{0.0f};
                std::array<glm::vec4, KMaxLights> LightPosition{};
                std::array<glm::vec4, KMaxLights> LightDirection{};
                std::array<glm::vec4, KMaxLights> LightColor{};
                std::array<glm::vec4, KMaxLights> LightCone{};
            };

            struct DrawItem
            {
                Geometry* Mesh = nullptr;
                VertexUniforms Vertex;
                FragmentUniforms Fragment;
                SDL_GPUTexture* Texture = nullptr;
                Uint32 First = 0;
                Uint32 Count = 0;
                bool Blend = false;
                bool Wireframe = false;
            };

            SDL_GPUShader* CreateShader(const unsigned char* Spirv, std::size_t SpirvSize, const unsigned char* Msl,
                                        std::size_t MslSize, SDL_GPUShaderStage Stage, Uint32 Samplers);
            bool CreatePipelines(std::string& Error);
            std::string GeometryKey(const SceneNode& Node, const SceneDocument& Document,
                                    const std::filesystem::path& AssetRoot) const;
            Geometry& FindGeometry(const SceneNode& Node, const SceneDocument& Document,
                                   const std::filesystem::path& AssetRoot);
            SDL_GPUTexture* FindTexture(const std::filesystem::path& Path);
            SDL_GPUTexture* PartTexture(Geometry& Entry, const MeshPart& Part, const std::filesystem::path& AssetRoot);
            void Release(Geometry& Entry);
            void ReleaseTargets();
            bool Upload(SDL_GPUCopyPass* Pass, SDL_GPUBuffer*& Buffer, Uint32& Capacity, const void* Data,
                        Uint32 Size, std::vector<SDL_GPUTransferBuffer*>& Transfers);

            SDL_GPUDevice* Gpu_ = nullptr;
            SDL_GPUGraphicsPipeline* MeshPipeline_ = nullptr;
            SDL_GPUGraphicsPipeline* BlendPipeline_ = nullptr;
            SDL_GPUGraphicsPipeline* WirePipeline_ = nullptr;
            SDL_GPUGraphicsPipeline* LinePipeline_ = nullptr;
            // SET SHADOW SHADING: volumes count into the stencil, then a dark overlay
            // covers what they marked. Null without a stencil depth format.
            SDL_GPUGraphicsPipeline* VolumePipeline_ = nullptr;
            SDL_GPUGraphicsPipeline* ShadePipeline_ = nullptr;
            bool Stencil_ = false;
            SDL_GPUSampler* Sampler_ = nullptr;
            core::GpuImage White_;
            SDL_GPUTextureFormat DepthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

            SDL_GPUTexture* Color_ = nullptr;
            SDL_GPUTexture* Depth_ = nullptr;
            int Width_ = 0;
            int Height_ = 0;

            std::unordered_map<std::string, Geometry> Geometry_;
            std::unordered_map<std::string, std::string> NodeKeys_;
            std::unordered_map<std::string, core::GpuImage> Textures_;
            std::unordered_map<std::string, bool> Failed_;
            std::vector<std::string> Errors_;

            std::vector<DrawItem> Items_;
            std::vector<LineVertex> Lines_;
            SDL_GPUBuffer* LineBuffer_ = nullptr;
            Uint32 LineCapacity_ = 0;
            // The frame's shadow volumes, then the six overlay corners in clip space.
            std::vector<LineVertex> Shadows_;
            SDL_GPUBuffer* ShadowBuffer_ = nullptr;
            Uint32 ShadowCapacity_ = 0;
            glm::mat4 ViewProj_{1.0f};
            glm::vec4 Clear_{0.0f, 0.0f, 0.0f, 1.0f};
            bool Pending_ = false;
        };
    } // namespace scene

} // namespace dbc::studio
