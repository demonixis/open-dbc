#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Surface;

namespace dbc
{
    class EngineKernel;
    struct GpuFragmentUniforms;
    struct GpuVertexUniforms;

    class GraphicsDeviceSystem
    {
      public:
        enum class EffectParameterKind : std::uint8_t
        {
            Boolean,
            Integer,
            Float,
            Vector,
            Matrix,
            Opaque,
        };

        struct EffectParameterInfo
        {
            std::string Name;
            EffectParameterKind Kind = EffectParameterKind::Opaque;
            bool Automatic = false;
            int LegacyClass = 0;
            int LegacyType = 0;
        };

        explicit GraphicsDeviceSystem(EngineKernel& Kernel);
        ~GraphicsDeviceSystem();

        // --- GPU texture helpers ---

        [[nodiscard]] static SDL_GPUTexture* GetGpuTextureFromSdlTexture(SDL_Texture* Texture);
        [[nodiscard]] SDL_GPUTexture* CreateGpuTextureFromSurface(SDL_Surface* Surface);
        // Re-uploads Areas of Surface, which lie inside it, into a texture CreateGpuTextureFromSurface made from a
        // surface of its size.
        [[nodiscard]] bool UpdateGpuTextureFromSurface(SDL_GPUTexture* Texture, SDL_Surface* Surface,
                                                       std::span<const SDL_Rect> Areas);
        [[nodiscard]] SDL_GPUTexture* CreateGpuCubeTextureFromSurfaces(const std::array<SDL_Surface*, 6>& Surfaces);

        // --- GPU pipeline lifecycle ---

        [[nodiscard]] bool CreateGpuWhiteTexture();
        [[nodiscard]] bool CreateGpuPipeline();
        [[nodiscard]] bool EnsureGpuDepthTarget(Uint32 Width, Uint32 Height, int Index = 0);

        // Offscreen colour targets used for screen readback and the final
        // per-channel Classic gamma transform.
        [[nodiscard]] bool EnsureGpuCaptureTarget(Uint32 Width, Uint32 Height);
        [[nodiscard]] bool ApplyFinalColorScale(float Red, float Green, float Blue);
        [[nodiscard]] bool EnsureGpuVertexCapacity(Uint32 VertexCount);
        void DestroyGpuBackend();

        SDL_GPUTextureFormat GpuColorFormat() const
        {
            return GpuColorFormat_;
        }

        // --- DBPro effects ---

        bool LoadEffect(const std::string& Path, int EffectId, int TextureFlag, int DoNotGenerateExtraData = 0,
                        std::string* Error = nullptr);
        bool DeleteEffect(int EffectId, std::string* Error = nullptr);
        [[nodiscard]] int EffectExist(int EffectId) const;
        bool SetEffectOn(int ObjectId, const std::string& Path, int TextureFlag, std::string* Error = nullptr);
        void DeleteObjectEffect(int ObjectId);
        [[nodiscard]] bool HasEffect(int EffectId) const;
        [[nodiscard]] bool EffectUsesObjectTexture(int EffectId) const;
        [[nodiscard]] bool EffectDoesNotGenerateExtraData(int EffectId) const;
        [[nodiscard]] Uint32 EffectFragmentSamplerCount(int EffectId) const;
        bool SetEffectConstantBoolean(int EffectId, const std::string& Name, bool Value, std::string* Error = nullptr);
        bool SetEffectConstantInteger(int EffectId, const std::string& Name, int Value, std::string* Error = nullptr);
        bool SetEffectConstantFloat(int EffectId, const std::string& Name, float Value, std::string* Error = nullptr);
        bool SetEffectConstantVector(int EffectId, const std::string& Name, const std::array<float, 4>& Value,
                                     std::string* Error = nullptr);
        bool SetEffectConstantMatrix(int EffectId, const std::string& Name, const std::array<float, 16>& Value,
                                     std::string* Error = nullptr);
        bool SetEffectTechnique(int EffectId, const std::string& Name, std::string* Error = nullptr);
        bool SetEffectTranspose(int EffectId, int Flag, std::string* Error = nullptr);
        [[nodiscard]] std::string EffectTechniqueName(int EffectId) const;
        [[nodiscard]] bool EffectTransposeEnabled(int EffectId) const;
        [[nodiscard]] bool EffectParameters(std::vector<EffectParameterInfo>& Output, int EffectId,
                                            std::string* Error = nullptr) const;
        [[nodiscard]] const std::vector<std::string>& EffectErrors() const;
        void PushEffectUniformData(SDL_GPUCommandBuffer* CommandBuffer, int EffectId,
                                   const GpuVertexUniforms& VertexUniforms,
                                   const GpuFragmentUniforms& FragmentUniforms) const;
        [[nodiscard]] SDL_GPUGraphicsPipeline* EffectPipeline(int EffectId, bool Transparent, bool AlphaDepthWrite,
                                                              bool Additive, bool Wireframe, bool DepthTest,
                                                              bool DepthWrite, bool StencilTest = false);
        [[nodiscard]] SDL_GPUGraphicsPipeline* SurfaceGhostPipeline(int Mode, bool Wireframe, bool DepthWrite,
                                                                    bool DepthTest = true,
                                                                    bool StencilTest = false) const;
        [[nodiscard]] SDL_GPUGraphicsPipeline* SurfaceOpaquePipeline(bool Wireframe, bool DepthWrite) const;
        [[nodiscard]] SDL_GPUGraphicsPipeline* SurfacePipeline(bool Transparent, bool AlphaDepthWrite, bool Additive,
                                                               bool Wireframe, bool DepthTest, bool DepthWrite,
                                                               bool StencilTest = false) const;

        // Opaque, back-face culled pipeline for static world-space terrain buffers.
        [[nodiscard]] SDL_GPUGraphicsPipeline* TerrainPipeline(bool Wireframe) const
        {
            return GpuTerrainPipelines_[Wireframe ? 1 : 0];
        }

        // Depth-only variant for the shadow depth pass.
        [[nodiscard]] SDL_GPUGraphicsPipeline* TerrainDepthPipeline() const
        {
            return GpuTerrainPipelines_[2];
        }

        [[nodiscard]] SDL_GPUGraphicsPipeline* ReflectionMaskPipeline() const
        {
            return GpuReflectionMaskPipeline_;
        }

        [[nodiscard]] SDL_GPUGraphicsPipeline* ShadowDepthPipeline() const
        {
            return GpuShadowDepthPipeline_;
        }

        [[nodiscard]] SDL_GPUGraphicsPipeline* ShadowVolumePipeline() const
        {
            return GpuShadowVolumePipeline_;
        }

        [[nodiscard]] SDL_GPUGraphicsPipeline* ShadowOverlayPipeline() const
        {
            return GpuShadowOverlayPipeline_;
        }

        [[nodiscard]] bool SupportsStencil() const
        {
            return GpuStencilSupported_;
        }

        [[nodiscard]] bool EnsureStencilPipelines();
        [[nodiscard]] SDL_GPUSampler* SurfaceSampler(int AddressMode, bool Filter, int MipMode) const;

        // --- Drawing helpers ---

        void SetDrawColorInt(int Packed);

        // --- Mutable accessors (transitional for renderer.cpp, resource_systems.cpp) ---

        SDL_Renderer*& Renderer();
        SDL_GPUDevice*& GpuDevice();
        SDL_GPUGraphicsPipeline*& GpuPipeline();
        SDL_GPUGraphicsPipeline*& GpuPipelineAlphaWrite();
        SDL_GPUGraphicsPipeline*& GpuPipelineTransparent();
        SDL_GPUGraphicsPipeline*& GpuPipelineAdditive();
        // Line-fill variants of the three pipelines above, for SET OBJECT / SET MATRIX wireframe surfaces.
        SDL_GPUGraphicsPipeline*& GpuPipelineWireframe();
        SDL_GPUGraphicsPipeline*& GpuPipelineAlphaWriteWireframe();
        SDL_GPUGraphicsPipeline*& GpuPipelineTransparentWireframe();
        SDL_GPUGraphicsPipeline*& GpuPipelineAdditiveWireframe();
        SDL_GPUSampler*& GpuSampler();
        // Point-sampled variant for surfaces whose texture filtering is switched off.
        SDL_GPUSampler*& GpuSamplerNearest();
        SDL_GPUBuffer*& GpuVertexBuffer();
        SDL_GPUTransferBuffer*& GpuTransferBuffer();
        SDL_GPUTexture*& GpuDepthTexture(int Index = 0);

        [[nodiscard]] SDL_GPUTexture* GpuCaptureTexture() const
        {
            return GpuCaptureTexture_;
        }

        [[nodiscard]] SDL_GPUTexture* GpuFinalColorTexture() const
        {
            return GpuFinalColorTexture_;
        }

        [[nodiscard]] Uint32 GpuCaptureWidth() const
        {
            return GpuCaptureWidth_;
        }

        [[nodiscard]] Uint32 GpuCaptureHeight() const
        {
            return GpuCaptureHeight_;
        }

        [[nodiscard]] SDL_GPUTextureFormat GpuCaptureFormat() const
        {
            return GpuCaptureFormat_;
        }

        SDL_GPUTextureFormat& GpuDepthFormat();
        Uint32& GpuDepthWidth(int Index = 0);
        Uint32& GpuDepthHeight(int Index = 0);
        Uint32& GpuVertexCapacity();
        SDL_Texture*& WhiteTexture();
        SDL_GPUTexture*& WhiteGpuTexture();
        SDL_GPUTexture*& WhiteCubeGpuTexture();

      private:
        friend class VrPresenter;

        // Uploads Areas of an RGBA32 surface into the same areas of Texture, then regenerates its mip chain.
        [[nodiscard]] bool UploadRgbaAreas(SDL_GPUTexture* Texture, SDL_Surface* Rgba, std::span<const SDL_Rect> Areas);

        struct EffectParameter
        {
            std::string Name;
            EffectParameterKind Kind = EffectParameterKind::Opaque;
            Uint32 Offset = 0;
            Uint32 Size = 0;
            bool RowMajor = false;
            bool Automatic = false;
            bool Manual = false;
        };

        struct EffectStage
        {
            // Both forms are loaded because LOAD EFFECT may run before the GPU device exists.
            std::vector<Uint8> Bytecode;
            std::string EntryPoint;
            std::vector<Uint8> Msl;
            std::string MslEntryPoint;
            Uint32 SamplerCount = 0;
            Uint32 UniformBufferCount = 0;
            std::vector<Uint8> UniformData;
            std::vector<EffectParameter> Parameters;
        };

        struct EffectTechnique
        {
            EffectStage Vertex;
            EffectStage Fragment;
            std::array<SDL_GPUGraphicsPipeline*, 64> Pipelines{};
        };

        struct EffectResource
        {
            std::string ManifestPath;
            std::unordered_map<std::string, EffectTechnique> Techniques;
            std::string CurrentTechnique = "default";
            int TextureFlag = 0;
            int DoNotGenerateExtraData = 0;
            bool Transpose = false;
            bool PipelineFailureReported = false;
        };

        // --- Private GPU helpers ---

        [[nodiscard]] static std::vector<Uint8> LoadFileBytes(const std::string& Path);
        // Loads a canonical engine shader (shaders/<Name>.<vert|frag>.<spv|msl>) in the device's native format.
        [[nodiscard]] SDL_GPUShader* CreateEngineShader(std::string_view Name, SDL_GPUShaderStage Stage,
                                                        Uint32 NumSamplers, Uint32 NumUniformBuffers);
        // MSL when the device cannot take SPIR-V (Metal); compiled shaders ship both forms.
        [[nodiscard]] SDL_GPUShaderFormat NativeShaderFormat() const;
        [[nodiscard]] SDL_GPUShader* CreateGpuShader(const EffectStage& EffectStageValue, SDL_GPUShaderStage Stage);
        [[nodiscard]] bool CreatePipelineSet(SDL_GPUShader* VertexShader, SDL_GPUShader* FragmentShader,
                                             std::array<SDL_GPUGraphicsPipeline*, 64>& Pipelines);
        bool LoadEffectResource(const std::string& Path, int EffectId, int TextureFlag, int DoNotGenerateExtraData,
                                bool Internal, std::string* Error = nullptr);
        bool SetEffectConstant(int EffectId, const std::string& Name, EffectParameterKind Kind, const void* Data,
                               std::size_t Size, std::string* Error = nullptr);
        void ReleaseEffectPipelines(EffectResource& Effect);
        void ReleaseGpuPipelineResources();

        EngineKernel& Kernel_;
        SDL_Renderer* Renderer_ = nullptr;
        SDL_GPUDevice* GpuDevice_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipeline_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineAlphaWrite_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineTransparent_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineAdditive_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineWireframe_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineAlphaWriteWireframe_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineTransparentWireframe_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuPipelineAdditiveWireframe_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuReflectionMaskPipeline_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuShadowDepthPipeline_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuShadowVolumePipeline_ = nullptr;
        SDL_GPUGraphicsPipeline* GpuShadowOverlayPipeline_ = nullptr;
        std::array<SDL_GPUGraphicsPipeline*, 2> GpuSurfaceOpaqueNoDepthPipelines_{};
        std::array<SDL_GPUGraphicsPipeline*, 3> GpuTerrainPipelines_{};
        std::array<SDL_GPUGraphicsPipeline*, 48> GpuSurfaceDepthPipelines_{};
        std::array<SDL_GPUGraphicsPipeline*, 80> GpuSurfaceGhostPipelines_{};
        SDL_GPUSampler* GpuSampler_ = nullptr;
        SDL_GPUSampler* GpuSamplerNearest_ = nullptr;
        std::array<SDL_GPUSampler*, 18> GpuSurfaceSamplers_{};
        SDL_GPUBuffer* GpuVertexBuffer_ = nullptr;
        SDL_GPUTransferBuffer* GpuTransferBuffer_ = nullptr;
        SDL_GPUTexture* GpuDepthTexture_[2] = {nullptr, nullptr};
        SDL_GPUTexture* GpuCaptureTexture_ = nullptr;
        SDL_GPUTexture* GpuFinalColorTexture_ = nullptr;
        SDL_Texture* GpuCaptureSdlTexture_ = nullptr;
        SDL_Texture* GpuFinalColorSdlTexture_ = nullptr;
        SDL_GPUTextureFormat GpuCaptureFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        Uint32 GpuCaptureWidth_ = 0;
        Uint32 GpuCaptureHeight_ = 0;
        SDL_GPUTextureFormat GpuColorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUTextureFormat GpuDepthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        bool GpuStencilSupported_ = false;
        bool StencilPipelinesRequested_ = false;
        Uint32 GpuDepthWidth_[2] = {0, 0};
        Uint32 GpuDepthHeight_[2] = {0, 0};
        Uint32 GpuVertexCapacity_ = 0;
        SDL_Texture* WhiteTexture_ = nullptr;
        SDL_GPUTexture* WhiteGpuTexture_ = nullptr;
        SDL_GPUTexture* WhiteCubeGpuTexture_ = nullptr;
        std::unordered_map<int, EffectResource> Effects_;
        std::vector<std::string> EffectErrors_;
    };

} // namespace dbc
