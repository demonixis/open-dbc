#include "graphics_device_system.hpp"
#include "../engine_types.hpp"
#include "core_system.hpp"
#include "engine_kernel.hpp"
#include "gpu_command_buffer.hpp"
#include "image_store.hpp"
#include "log.hpp"
#include "object_manager.hpp"
#include "path_utf8.hpp"
#include "terrain_mesh.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

#include <nlohmann/json.hpp>

namespace dbc
{
    namespace
    {
        bool ContainsLegacyShaderAssembly(const std::filesystem::path& Path)
        {
            std::error_code Error;
            const std::uintmax_t Size = std::filesystem::file_size(Path, Error);
            if (Error || Size > 1024 * 1024)
                return false;
            std::ifstream Source(Path, std::ios::binary);
            if (!Source)
                return false;
            std::string Text(std::istreambuf_iterator<char>(Source), {});
            std::transform(Text.begin(), Text.end(), Text.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            return Text.find("vs.1.") != std::string::npos || Text.find("ps.1.") != std::string::npos ||
                   Text.find("vertexshader = asm") != std::string::npos ||
                   Text.find("pixelshader = asm") != std::string::npos;
        }
    } // namespace

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    GraphicsDeviceSystem::GraphicsDeviceSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    GraphicsDeviceSystem::~GraphicsDeviceSystem()
    {
        // GPU resources are released by DestroyGpuBackend() before kernel destruction.
        // We just null out pointers here for safety.
        Renderer_ = nullptr;
        GpuDevice_ = nullptr;
        GpuPipeline_ = nullptr;
        GpuPipelineAlphaWrite_ = nullptr;
        GpuPipelineTransparent_ = nullptr;
        GpuPipelineAdditive_ = nullptr;
        GpuPipelineWireframe_ = nullptr;
        GpuPipelineAlphaWriteWireframe_ = nullptr;
        GpuPipelineTransparentWireframe_ = nullptr;
        GpuPipelineAdditiveWireframe_ = nullptr;
        GpuSurfaceOpaqueNoDepthPipelines_.fill(nullptr);
        GpuTerrainPipelines_.fill(nullptr);
        GpuMeshPipelines_.fill(nullptr);
        GpuSurfaceGhostPipelines_.fill(nullptr);
        GpuSampler_ = nullptr;
        GpuSamplerNearest_ = nullptr;
        GpuSurfaceSamplers_.fill(nullptr);
        GpuVertexBuffer_ = nullptr;
        GpuTransferBuffer_ = nullptr;
        GpuDepthTexture_[0] = nullptr;
        GpuDepthTexture_[1] = nullptr;
        GpuCaptureTexture_ = nullptr;
        GpuFinalColorTexture_ = nullptr;
        GpuCaptureSdlTexture_ = nullptr;
        GpuFinalColorSdlTexture_ = nullptr;
        WhiteTexture_ = nullptr;
        WhiteGpuTexture_ = nullptr;
        WhiteCubeGpuTexture_ = nullptr;
    }

    // -----------------------------------------------------------------------
    // Public methods
    // -----------------------------------------------------------------------

    [[nodiscard]] SDL_GPUTexture* GraphicsDeviceSystem::GetGpuTextureFromSdlTexture(SDL_Texture* Texture)
    {
        if (!Texture)
        {
            return nullptr;
        }
        const SDL_PropertiesID Props = SDL_GetTextureProperties(Texture);
        if (Props == 0)
        {
            return nullptr;
        }
        return static_cast<SDL_GPUTexture*>(
            SDL_GetPointerProperty(Props, SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    }

    namespace
    {
        // A full mip chain for the runtime's default texture sampling.
        [[nodiscard]] Uint32 SurfaceMipLevels(int Width, int Height)
        {
            Uint32 Levels = 1;
            for (int Extent = std::max(Width, Height); Extent > 1; Extent >>= 1)
                ++Levels;
            return Levels;
        }

        // RGBA32 surfaces upload as they are; others through a converted copy the guard releases.
        struct RgbaSurface
        {
            SDL_Surface* Source = nullptr;
            SDL_Surface* Value = nullptr;

            explicit RgbaSurface(SDL_Surface* Surface)
                : Source(Surface),
                  Value(Surface->format == SDL_PIXELFORMAT_RGBA32 ? Surface
                                                                  : SDL_ConvertSurface(Surface, SDL_PIXELFORMAT_RGBA32))
            {
            }

            ~RgbaSurface()
            {
                if (Value && Value != Source)
                    SDL_DestroySurface(Value);
            }

            RgbaSurface(const RgbaSurface&) = delete;
            RgbaSurface& operator=(const RgbaSurface&) = delete;
        };
    } // namespace

    [[nodiscard]] bool GraphicsDeviceSystem::UploadRgbaAreas(SDL_GPUTexture* Texture, SDL_Surface* Rgba,
                                                             std::span<const SDL_Rect> Areas)
    {
        std::uint64_t ByteCount = 0;
        for (const SDL_Rect& Area : Areas)
            ByteCount += static_cast<std::uint64_t>(Area.w) * static_cast<std::uint64_t>(Area.h) * 4;
        if (ByteCount == 0 || ByteCount > std::numeric_limits<Uint32>::max())
            return ByteCount == 0;

        SDL_GPUTransferBufferCreateInfo TransferInfo{};
        TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        TransferInfo.size = static_cast<Uint32>(ByteCount);
        TransferInfo.props = 0;
        SDL_GPUTransferBuffer* Transfer = SDL_CreateGPUTransferBuffer(GpuDevice_, &TransferInfo);
        if (!Transfer)
            return false;

        Uint8* Dst = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(GpuDevice_, Transfer, false));
        if (!Dst)
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            return false;
        }
        for (const SDL_Rect& Area : Areas)
        {
            const Uint8* Src = static_cast<const Uint8*>(Rgba->pixels) +
                               static_cast<std::size_t>(Area.y) * Rgba->pitch + static_cast<std::size_t>(Area.x) * 4;
            const std::size_t RowBytes = static_cast<std::size_t>(Area.w) * 4;
            if (Area.x == 0 && RowBytes == static_cast<std::size_t>(Rgba->pitch))
            {
                SDL_memcpy(Dst, Src, RowBytes * static_cast<std::size_t>(Area.h));
            }
            else
            {
                for (int Y = 0; Y < Area.h; ++Y)
                    SDL_memcpy(Dst + Y * RowBytes, Src + static_cast<std::size_t>(Y) * Rgba->pitch, RowBytes);
            }
            Dst += RowBytes * static_cast<std::size_t>(Area.h);
        }
        SDL_UnmapGPUTransferBuffer(GpuDevice_, Transfer);

        SDL_GPUCommandBuffer* Cmd = SDL_AcquireGPUCommandBuffer(GpuDevice_);
        if (!Cmd)
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            return false;
        }
        GpuCommandBuffer Pending(Cmd);
        SDL_GPUCopyPass* CopyPass = SDL_BeginGPUCopyPass(Cmd);
        if (!CopyPass)
        {
            Pending.Abandon();
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            return false;
        }
        Uint32 Offset = 0;
        for (const SDL_Rect& Area : Areas)
        {
            SDL_GPUTextureTransferInfo SrcInfo{};
            SrcInfo.transfer_buffer = Transfer;
            SrcInfo.offset = Offset;
            SrcInfo.pixels_per_row = static_cast<Uint32>(Area.w);
            SrcInfo.rows_per_layer = static_cast<Uint32>(Area.h);
            SDL_GPUTextureRegion DstRegion{};
            DstRegion.texture = Texture;
            DstRegion.x = static_cast<Uint32>(Area.x);
            DstRegion.y = static_cast<Uint32>(Area.y);
            DstRegion.w = static_cast<Uint32>(Area.w);
            DstRegion.h = static_cast<Uint32>(Area.h);
            DstRegion.d = 1;
            // Overwritten in place: commands run in submission order, so frames already submitted still read the
            // old contents, and an update of part of the texture keeps the rest.
            SDL_UploadToGPUTexture(CopyPass, &SrcInfo, &DstRegion, false);
            Offset += static_cast<Uint32>(Area.w) * static_cast<Uint32>(Area.h) * 4;
        }
        SDL_EndGPUCopyPass(CopyPass);
        if (SurfaceMipLevels(Rgba->w, Rgba->h) > 1)
            SDL_GenerateMipmapsForGPUTexture(Cmd, Texture);
        const bool Submitted = Pending.Submit();
        SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
        return Submitted;
    }

    [[nodiscard]] SDL_GPUTexture* GraphicsDeviceSystem::CreateGpuTextureFromSurface(SDL_Surface* Surface)
    {
        if (!GpuDevice_ || !Surface)
        {
            return nullptr;
        }

        const RgbaSurface Rgba(Surface);
        if (!Rgba.Value)
            return nullptr;
        const std::uint64_t ByteCount =
            static_cast<std::uint64_t>(Rgba.Value->w) * static_cast<std::uint64_t>(Rgba.Value->h) * 4;
        if (Rgba.Value->w <= 0 || Rgba.Value->h <= 0 || Rgba.Value->w > std::numeric_limits<int>::max() / 4 ||
            ByteCount > std::numeric_limits<Uint32>::max())
        {
            SDL_SetError("Image dimensions exceed the GPU transfer limit");
            return nullptr;
        }
        SDL_GPUTextureCreateInfo TextureInfo{};
        TextureInfo.type = SDL_GPU_TEXTURETYPE_2D;
        TextureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        // SDL generates mipmaps with blits, which require COLOR_TARGET usage.
        const Uint32 Levels = SurfaceMipLevels(Rgba.Value->w, Rgba.Value->h);
        TextureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (Levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
        TextureInfo.width = static_cast<Uint32>(Rgba.Value->w);
        TextureInfo.height = static_cast<Uint32>(Rgba.Value->h);
        TextureInfo.layer_count_or_depth = 1;
        TextureInfo.num_levels = Levels;
        TextureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        TextureInfo.props = 0;
        SDL_GPUTexture* GpuTexture = SDL_CreateGPUTexture(GpuDevice_, &TextureInfo);
        if (!GpuTexture)
            return nullptr;
        const SDL_Rect Whole{0, 0, Rgba.Value->w, Rgba.Value->h};
        if (!UploadRgbaAreas(GpuTexture, Rgba.Value, std::span<const SDL_Rect>(&Whole, 1)))
        {
            SDL_ReleaseGPUTexture(GpuDevice_, GpuTexture);
            return nullptr;
        }
        return GpuTexture;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::UpdateGpuTextureFromSurface(SDL_GPUTexture* Texture, SDL_Surface* Surface,
                                                                         std::span<const SDL_Rect> Areas)
    {
        if (!GpuDevice_ || !Texture || !Surface)
            return false;
        const RgbaSurface Rgba(Surface);
        return Rgba.Value && UploadRgbaAreas(Texture, Rgba.Value, Areas);
    }

    [[nodiscard]] SDL_GPUTexture* GraphicsDeviceSystem::CreateGpuCubeTextureFromSurfaces(
        const std::array<SDL_Surface*, 6>& Surfaces)
    {
        if (!GpuDevice_ || !Surfaces[0] || Surfaces[0]->w <= 0)
            return nullptr;

        const int Side = Surfaces[0]->w;
        const std::uint64_t TotalBytes = static_cast<std::uint64_t>(Side) * Side * 4 * 6;
        if (Side > std::numeric_limits<int>::max() / 4 || TotalBytes > std::numeric_limits<Uint32>::max())
        {
            SDL_SetError("Cube image dimensions exceed the GPU transfer limit");
            return nullptr;
        }
        std::array<SDL_Surface*, 6> Faces{};
        const auto ReleaseFaces = [&]()
        {
            for (SDL_Surface* Face : Faces)
            {
                if (Face)
                    SDL_DestroySurface(Face);
            }
        };
        for (std::size_t Index = 0; Index < Faces.size(); ++Index)
        {
            if (!Surfaces[Index])
            {
                ReleaseFaces();
                return nullptr;
            }
            SDL_Surface* Converted = SDL_ConvertSurface(Surfaces[Index], SDL_PIXELFORMAT_RGBA32);
            if (!Converted)
            {
                ReleaseFaces();
                return nullptr;
            }
            if (Converted->w != Side || Converted->h != Side)
            {
                Faces[Index] = SDL_ScaleSurface(Converted, Side, Side, SDL_SCALEMODE_LINEAR);
                SDL_DestroySurface(Converted);
            }
            else
            {
                Faces[Index] = Converted;
            }
            if (!Faces[Index])
            {
                ReleaseFaces();
                return nullptr;
            }
        }

        SDL_GPUTextureCreateInfo TextureInfo{};
        TextureInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        TextureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        TextureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        TextureInfo.width = static_cast<Uint32>(Side);
        TextureInfo.height = static_cast<Uint32>(Side);
        TextureInfo.layer_count_or_depth = 6;
        TextureInfo.num_levels = 1;
        TextureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* CubeTexture = SDL_CreateGPUTexture(GpuDevice_, &TextureInfo);
        if (!CubeTexture)
        {
            ReleaseFaces();
            return nullptr;
        }

        const Uint32 FaceBytes = static_cast<Uint32>(TotalBytes / 6);
        SDL_GPUTransferBufferCreateInfo TransferInfo{};
        TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        TransferInfo.size = FaceBytes * 6;
        SDL_GPUTransferBuffer* Transfer = SDL_CreateGPUTransferBuffer(GpuDevice_, &TransferInfo);
        if (!Transfer)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, CubeTexture);
            ReleaseFaces();
            return nullptr;
        }

        Uint8* Destination = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(GpuDevice_, Transfer, false));
        if (!Destination)
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            SDL_ReleaseGPUTexture(GpuDevice_, CubeTexture);
            ReleaseFaces();
            return nullptr;
        }
        const int RowBytes = Side * 4;
        for (std::size_t FaceIndex = 0; FaceIndex < Faces.size(); ++FaceIndex)
        {
            const Uint8* Source = static_cast<const Uint8*>(Faces[FaceIndex]->pixels);
            Uint8* FaceDestination = Destination + FaceIndex * FaceBytes;
            for (int Y = 0; Y < Side; ++Y)
                SDL_memcpy(FaceDestination + Y * RowBytes, Source + Y * Faces[FaceIndex]->pitch, RowBytes);
        }
        SDL_UnmapGPUTransferBuffer(GpuDevice_, Transfer);
        ReleaseFaces();

        SDL_GPUCommandBuffer* CommandBuffer = SDL_AcquireGPUCommandBuffer(GpuDevice_);
        if (!CommandBuffer)
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            SDL_ReleaseGPUTexture(GpuDevice_, CubeTexture);
            return nullptr;
        }
        GpuCommandBuffer Pending(CommandBuffer);
        SDL_GPUCopyPass* CopyPass = SDL_BeginGPUCopyPass(CommandBuffer);
        if (!CopyPass)
        {
            Pending.Abandon();
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            SDL_ReleaseGPUTexture(GpuDevice_, CubeTexture);
            return nullptr;
        }
        for (Uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
        {
            SDL_GPUTextureTransferInfo SourceInfo{};
            SourceInfo.transfer_buffer = Transfer;
            SourceInfo.offset = FaceIndex * FaceBytes;
            SourceInfo.pixels_per_row = static_cast<Uint32>(Side);
            SourceInfo.rows_per_layer = static_cast<Uint32>(Side);
            SDL_GPUTextureRegion DestinationRegion{};
            DestinationRegion.texture = CubeTexture;
            DestinationRegion.layer = FaceIndex;
            DestinationRegion.w = static_cast<Uint32>(Side);
            DestinationRegion.h = static_cast<Uint32>(Side);
            DestinationRegion.d = 1;
            SDL_UploadToGPUTexture(CopyPass, &SourceInfo, &DestinationRegion, false);
        }
        SDL_EndGPUCopyPass(CopyPass);
        if (!Pending.Submit())
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
            SDL_ReleaseGPUTexture(GpuDevice_, CubeTexture);
            return nullptr;
        }
        SDL_ReleaseGPUTransferBuffer(GpuDevice_, Transfer);
        return CubeTexture;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::CreateGpuWhiteTexture()
    {
        if (!Renderer_)
        {
            return false;
        }
        SDL_Surface* Surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        if (!Surface)
        {
            return false;
        }
        const Uint32 White = SDL_MapSurfaceRGBA(Surface, 255, 255, 255, 255);
        SDL_FillSurfaceRect(Surface, nullptr, White);
        WhiteTexture_ = SDL_CreateTextureFromSurface(Renderer_, Surface);
        WhiteGpuTexture_ = CreateGpuTextureFromSurface(Surface);
        const std::array<SDL_Surface*, 6> CubeFaces{Surface, Surface, Surface, Surface, Surface, Surface};
        WhiteCubeGpuTexture_ = CreateGpuCubeTextureFromSurfaces(CubeFaces);
        SDL_DestroySurface(Surface);
        if (!WhiteTexture_)
        {
            return false;
        }
        SDL_SetTextureBlendMode(WhiteTexture_, SDL_BLENDMODE_BLEND);
        return WhiteGpuTexture_ != nullptr && WhiteCubeGpuTexture_ != nullptr;
    }

    bool GraphicsDeviceSystem::LoadEffect(const std::string& Path, int EffectId, int TextureFlag,
                                          int DoNotGenerateExtraData, std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        EffectErrors_.clear();
        if (!LoadEffectResource(Path, EffectId, TextureFlag, DoNotGenerateExtraData, false, &Diagnostic))
        {
            EffectErrors_.push_back(Diagnostic);
            return false;
        }
        return true;
    }

    bool GraphicsDeviceSystem::DeleteEffect(int EffectId, std::string* Error)
    {
        if (EffectId <= 0)
        {
            if (Error)
                *Error = "DELETE EFFECT requires a positive effect number";
            return false;
        }
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
            return true;
        ReleaseEffectPipelines(It->second);
        Effects_.erase(It);
        Kernel_.Objects().ClearEffectReferences(EffectId);
        return true;
    }

    int GraphicsDeviceSystem::EffectExist(int EffectId) const
    {
        return EffectId > 0 && Effects_.contains(EffectId) ? 1 : 0;
    }

    bool GraphicsDeviceSystem::SetEffectOn(int ObjectId, const std::string& Path, int TextureFlag, std::string* Error)
    {
        std::string IgnoredError;
        std::string& Diagnostic = Error ? *Error : IgnoredError;
        if (ObjectId <= 0 || !Kernel_.Objects().GetObject(ObjectId))
        {
            Diagnostic = "SET EFFECT ON requires existing object " + std::to_string(ObjectId);
            return false;
        }
        const int EffectId = -ObjectId;
        EffectErrors_.clear();
        if (!LoadEffectResource(Path, EffectId, TextureFlag, 0, true, &Diagnostic))
        {
            EffectErrors_.push_back(Diagnostic);
            return false;
        }
        if (!Kernel_.Objects().SetObjectEffect(ObjectId, EffectId, Error))
            return false;
        return true;
    }

    void GraphicsDeviceSystem::DeleteObjectEffect(int ObjectId)
    {
        if (ObjectId <= 0)
            return;
        const auto It = Effects_.find(-ObjectId);
        if (It == Effects_.end())
            return;
        ReleaseEffectPipelines(It->second);
        Effects_.erase(It);
    }

    bool GraphicsDeviceSystem::HasEffect(int EffectId) const
    {
        return EffectId != 0 && Effects_.contains(EffectId);
    }

    bool GraphicsDeviceSystem::EffectUsesObjectTexture(int EffectId) const
    {
        const auto It = Effects_.find(EffectId);
        return It != Effects_.end() && It->second.TextureFlag == 0;
    }

    bool GraphicsDeviceSystem::EffectDoesNotGenerateExtraData(int EffectId) const
    {
        const auto It = Effects_.find(EffectId);
        return It != Effects_.end() && It->second.DoNotGenerateExtraData != 0;
    }

    Uint32 GraphicsDeviceSystem::EffectFragmentSamplerCount(int EffectId) const
    {
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
            return 0;
        const auto Technique = It->second.Techniques.find(It->second.CurrentTechnique);
        return Technique != It->second.Techniques.end() ? Technique->second.Fragment.SamplerCount : 0;
    }

    bool GraphicsDeviceSystem::SetEffectConstantBoolean(int EffectId, const std::string& Name, bool Value,
                                                        std::string* Error)
    {
        const std::uint32_t Packed = Value ? 1U : 0U;
        if (!SetEffectConstant(EffectId, Name, EffectParameterKind::Boolean, &Packed, sizeof(Packed), Error))
            return false;
        return true;
    }

    bool GraphicsDeviceSystem::SetEffectConstantInteger(int EffectId, const std::string& Name, int Value,
                                                        std::string* Error)
    {
        const std::int32_t Packed = static_cast<std::int32_t>(Value);
        if (!SetEffectConstant(EffectId, Name, EffectParameterKind::Integer, &Packed, sizeof(Packed), Error))
            return false;
        return true;
    }

    bool GraphicsDeviceSystem::SetEffectConstantFloat(int EffectId, const std::string& Name, float Value,
                                                      std::string* Error)
    {
        if (!SetEffectConstant(EffectId, Name, EffectParameterKind::Float, &Value, sizeof(Value), Error))
            return false;
        return true;
    }

    bool GraphicsDeviceSystem::SetEffectConstantVector(int EffectId, const std::string& Name,
                                                       const std::array<float, 4>& Value, std::string* Error)
    {
        if (!SetEffectConstant(EffectId, Name, EffectParameterKind::Vector, Value.data(), sizeof(Value), Error))
            return false;
        return true;
    }

    bool GraphicsDeviceSystem::SetEffectConstantMatrix(int EffectId, const std::string& Name,
                                                       const std::array<float, 16>& Value, std::string* Error)
    {
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
        {
            if (Error)
                *Error = "effect " + std::to_string(EffectId) + " does not exist";
            return false;
        }
        if (!It->second.Transpose)
        {
            if (!SetEffectConstant(EffectId, Name, EffectParameterKind::Matrix, Value.data(), sizeof(Value), Error))
                return false;
            return true;
        }

        std::array<float, 16> Transposed{};
        for (std::size_t Row = 0; Row < 4; ++Row)
            for (std::size_t Column = 0; Column < 4; ++Column)
                Transposed[Row * 4 + Column] = Value[Column * 4 + Row];
        if (!SetEffectConstant(EffectId, Name, EffectParameterKind::Matrix, Transposed.data(), sizeof(Transposed),
                               Error))
            return false;
        return true;
    }

    bool GraphicsDeviceSystem::SetEffectTechnique(int EffectId, const std::string& Name, std::string* Error)
    {
        if (EffectId <= 0)
        {
            if (Error)
                *Error = "SET EFFECT TECHNIQUE requires a positive effect number";
            return false;
        }
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
        {
            if (Error)
                *Error = "effect " + std::to_string(EffectId) + " does not exist";
            return false;
        }
        if (!It->second.Techniques.contains(Name))
        {
            if (Error)
                *Error = "effect " + std::to_string(EffectId) + " has no technique " + Name;
            return false;
        }
        It->second.CurrentTechnique = Name;
        return true;
    }

    bool GraphicsDeviceSystem::SetEffectTranspose(int EffectId, int Flag, std::string* Error)
    {
        if (EffectId <= 0)
        {
            if (Error)
                *Error = "SET EFFECT TRANSPOSE requires a positive effect number";
            return false;
        }
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
        {
            if (Error)
                *Error = "effect " + std::to_string(EffectId) + " does not exist";
            return false;
        }
        It->second.Transpose = Flag != 0;
        return true;
    }

    std::string GraphicsDeviceSystem::EffectTechniqueName(int EffectId) const
    {
        const auto It = Effects_.find(EffectId);
        return It != Effects_.end() ? It->second.CurrentTechnique : std::string{};
    }

    bool GraphicsDeviceSystem::EffectTransposeEnabled(int EffectId) const
    {
        const auto It = Effects_.find(EffectId);
        return It != Effects_.end() && It->second.Transpose;
    }

    bool GraphicsDeviceSystem::EffectParameters(std::vector<EffectParameterInfo>& Output, int EffectId,
                                                std::string* Error) const
    {
        if (EffectId <= 0)
        {
            if (Error)
                *Error = "effect checklist requires a positive effect number";
            return false;
        }
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
        {
            if (Error)
                *Error = "effect " + std::to_string(EffectId) + " does not exist";
            return false;
        }
        const auto TechniqueIt = It->second.Techniques.find(It->second.CurrentTechnique);
        if (TechniqueIt == It->second.Techniques.end())
        {
            Output.clear();
            return true;
        }

        std::vector<EffectParameterInfo> Result;
        const auto Append = [&](const EffectStage& Stage)
        {
            for (const EffectParameter& Parameter : Stage.Parameters)
            {
                if (std::any_of(Result.begin(), Result.end(),
                                [&](const EffectParameterInfo& Existing) { return Existing.Name == Parameter.Name; }))
                    continue;
                int LegacyClass = 0;
                int LegacyType = 0;
                switch (Parameter.Kind)
                {
                    case EffectParameterKind::Boolean:
                        LegacyType = 1;
                        break;
                    case EffectParameterKind::Integer:
                        LegacyType = 2;
                        break;
                    case EffectParameterKind::Float:
                        LegacyType = 3;
                        break;
                    case EffectParameterKind::Vector:
                        LegacyClass = 1;
                        LegacyType = 3;
                        break;
                    case EffectParameterKind::Matrix:
                        LegacyClass = Parameter.RowMajor ? 2 : 3;
                        LegacyType = 3;
                        break;
                    case EffectParameterKind::Opaque:
                        break;
                }
                Result.push_back({Parameter.Name, Parameter.Kind, Parameter.Automatic, LegacyClass, LegacyType});
            }
        };
        Append(TechniqueIt->second.Vertex);
        Append(TechniqueIt->second.Fragment);
        Output = std::move(Result);
        return true;
    }

    const std::vector<std::string>& GraphicsDeviceSystem::EffectErrors() const
    {
        return EffectErrors_;
    }

    bool GraphicsDeviceSystem::SetEffectConstant(int EffectId, const std::string& Name, EffectParameterKind Kind,
                                                 const void* Data, std::size_t Size, std::string* Error)
    {
        if (EffectId <= 0)
        {
            if (Error)
                *Error = "SET EFFECT CONSTANT requires a positive effect number";
            return false;
        }
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
        {
            if (Error)
                *Error = "effect " + std::to_string(EffectId) + " does not exist";
            return false;
        }
        if (Name.empty())
        {
            if (Error)
                *Error = "effect constant name cannot be empty";
            return false;
        }

        std::vector<std::pair<EffectStage*, EffectParameter*>> Targets;
        for (auto& [TechniqueName, Technique] : It->second.Techniques)
        {
            (void)TechniqueName;
            const auto Collect = [&](EffectStage& Stage)
            {
                for (EffectParameter& Parameter : Stage.Parameters)
                {
                    if (Parameter.Name != Name)
                        continue;
                    const bool CompatibleKind =
                        Parameter.Kind == Kind ||
                        (Kind == EffectParameterKind::Boolean && Parameter.Kind == EffectParameterKind::Integer);
                    if (!CompatibleKind || Parameter.Size < Size || Stage.UniformData.size() < Size ||
                        Parameter.Offset > Stage.UniformData.size() - Size)
                    {
                        if (Error)
                            *Error = "effect constant " + Name + " has an incompatible type";
                        return false;
                    }
                    Targets.emplace_back(&Stage, &Parameter);
                }
                return true;
            };
            if (!Collect(Technique.Vertex))
                return false;
            if (!Collect(Technique.Fragment))
                return false;
        }
        if (Targets.empty())
        {
            if (Error)
                *Error = "effect constant " + Name + " does not exist";
            return false;
        }
        for (const auto& [Stage, Parameter] : Targets)
        {
            std::memcpy(Stage->UniformData.data() + Parameter->Offset, Data, Size);
            Parameter->Manual = true;
        }
        return true;
    }

    void GraphicsDeviceSystem::PushEffectUniformData(SDL_GPUCommandBuffer* CommandBuffer, int EffectId,
                                                     const GpuVertexUniforms& VertexUniforms,
                                                     const GpuFragmentUniforms& FragmentUniforms) const
    {
        if (!CommandBuffer)
            return;
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end())
            return;
        const auto TechniqueIt = It->second.Techniques.find(It->second.CurrentTechnique);
        if (TechniqueIt == It->second.Techniques.end())
            return;

        const EffectTechnique& Technique = TechniqueIt->second;
        if (Technique.Vertex.UniformBufferCount != 0)
        {
            if (Technique.Vertex.UniformData.empty())
            {
                SDL_PushGPUVertexUniformData(CommandBuffer, 0, &VertexUniforms, sizeof(VertexUniforms));
            }
            else
            {
                std::vector<Uint8> Data = Technique.Vertex.UniformData;
                for (const EffectParameter& Parameter : Technique.Vertex.Parameters)
                {
                    if (Parameter.Automatic && !Parameter.Manual && Parameter.Size >= sizeof(VertexUniforms) &&
                        Parameter.Offset <= Data.size() - sizeof(VertexUniforms))
                        std::memcpy(Data.data() + Parameter.Offset, &VertexUniforms, sizeof(VertexUniforms));
                }
                SDL_PushGPUVertexUniformData(CommandBuffer, 0, Data.data(), static_cast<Uint32>(Data.size()));
            }
        }
        if (Technique.Fragment.UniformBufferCount != 0)
        {
            if (Technique.Fragment.UniformData.empty())
                SDL_PushGPUFragmentUniformData(CommandBuffer, 0, &FragmentUniforms, sizeof(FragmentUniforms));
            else
                SDL_PushGPUFragmentUniformData(CommandBuffer, 0, Technique.Fragment.UniformData.data(),
                                               static_cast<Uint32>(Technique.Fragment.UniformData.size()));
        }
    }

    bool GraphicsDeviceSystem::LoadEffectResource(const std::string& Path, int EffectId, int TextureFlag,
                                                  int DoNotGenerateExtraData, bool Internal, std::string* Error)
    {
        namespace fs = std::filesystem;
        if ((!Internal && EffectId <= 0) || (Internal && EffectId >= 0))
        {
            if (Error)
                *Error = "LOAD EFFECT requires a positive effect number";
            return false;
        }
        if (TextureFlag != 0 && TextureFlag != 1)
        {
            if (Error)
                *Error = "LOAD EFFECT texture flag must be zero or one";
            return false;
        }
        if (DoNotGenerateExtraData != 0 && DoNotGenerateExtraData != 1)
        {
            if (Error)
                *Error = "LOAD EFFECT extra-data flag must be zero or one";
            return false;
        }

        fs::path Requested = dbc::PathFromUtf8(Path);
        std::string Filename = dbc::PathToUtf8(Requested.filename());
        std::transform(Filename.begin(), Filename.end(), Filename.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        fs::path ManifestPath;
        if (Filename.ends_with(".effect.json"))
        {
            ManifestPath = Requested;
        }
        else if (Requested.has_extension())
        {
            ManifestPath = (std::filesystem::path(Requested).replace_extension(".effect.json"));
        }
        else
        {
            if (Error)
                *Error = "LOAD EFFECT requires an .fx, .hlsl or .effect.json file";
            return false;
        }

        std::ifstream Stream(ManifestPath, std::ios::binary);
        if (!Stream)
        {
            if (ContainsLegacyShaderAssembly(Requested))
            {
                if (Error)
                    *Error =
                        "LOAD EFFECT does not support legacy DirectX shader assembly in " + dbc::PathToUtf8(Requested);
                return false;
            }
            {
                if (Error)
                    *Error = "LOAD EFFECT cannot read compiled manifest " + dbc::PathToUtf8(ManifestPath);
                return false;
            }
        }

        EffectResource Loaded;
        Loaded.ManifestPath = dbc::PathToUtf8(ManifestPath);
        Loaded.TextureFlag = TextureFlag;
        Loaded.DoNotGenerateExtraData = DoNotGenerateExtraData;
        const auto ManifestFailure = [&](std::string Message)
        {
            if (Error)
                *Error = "LOAD EFFECT invalid manifest " + dbc::PathToUtf8(ManifestPath) + ": " + Message;
            return false;
        };
        const auto ReadInteger = [&](const nlohmann::json& Node, const char* Name, int& Value)
        {
            const auto& Field = Node.at(Name);
            if (!Field.is_number_integer() ||
                (Field.is_number_unsigned()
                     ? Field.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                     : Field.get<std::int64_t>() < std::numeric_limits<int>::min() ||
                           Field.get<std::int64_t>() > std::numeric_limits<int>::max()))
                return ManifestFailure(std::string("invalid integer field ") + Name);
            Value = Field.get<int>();
            return true;
        };
        try
        {
            const nlohmann::json Manifest = nlohmann::json::parse(Stream);
            if (Manifest.at("format") != "opendbc-effect-v1")
                return ManifestFailure("unsupported effect manifest format");

            const auto ReadStage =
                [&](const nlohmann::json& Node, const std::string& Name, EffectStage& Stage, bool Vertex)
            {
                const auto ReadSibling = [&](const char* Field, fs::path& File)
                {
                    File = dbc::PathFromUtf8(Node.at(Field).get<std::string>());
                    if (File.empty() || File.is_absolute() || File.has_parent_path() || File == "." || File == ".." ||
                        dbc::PathToUtf8(File).find_first_of("\\:") != std::string::npos)
                        return ManifestFailure(Name + " stage file must be a sibling filename");
                    return true;
                };
                const auto ReadEntryPoint = [&](const char* Field, std::string& EntryPoint)
                {
                    EntryPoint = Node.at(Field).get<std::string>();
                    if (EntryPoint.empty() || EntryPoint.find_first_of(" \t\r\n") != std::string::npos)
                        return ManifestFailure(Name + " stage entry point is invalid");
                    return true;
                };
                fs::path File;
                fs::path MslFile;
                if (!ReadSibling("file", File) || !ReadSibling("msl_file", MslFile) ||
                    !ReadEntryPoint("entry_point", Stage.EntryPoint) ||
                    !ReadEntryPoint("msl_entry_point", Stage.MslEntryPoint))
                    return false;

                const nlohmann::json& Resources = Node.at("resources");
                int Samplers{};
                if (!ReadInteger(Resources, "samplers", Samplers))
                    return false;
                int UniformBuffers{};
                if (!ReadInteger(Resources, "uniform_buffers", UniformBuffers))
                    return false;
                int StorageTextures{};
                if (!ReadInteger(Resources, "storage_textures", StorageTextures))
                    return false;
                int StorageBuffers{};
                if (!ReadInteger(Resources, "storage_buffers", StorageBuffers))
                    return false;
                if (Samplers < 0 || UniformBuffers < 0 || StorageTextures != 0 || StorageBuffers != 0)
                    return ManifestFailure(Name + " stage uses unsupported GPU resources");
                if ((Vertex && Samplers != 0) || (!Vertex && Samplers > 1) || UniformBuffers > 1)
                    return ManifestFailure(Name + " stage exceeds the portable effect ABI");
                Stage.SamplerCount = static_cast<Uint32>(Samplers);
                Stage.UniformBufferCount = static_cast<Uint32>(UniformBuffers);

                if (Node.contains("uniform_blocks"))
                {
                    const nlohmann::json& Blocks = Node.at("uniform_blocks");
                    if (!Blocks.is_array() || static_cast<int>(Blocks.size()) != UniformBuffers)
                        return ManifestFailure(Name + " uniform reflection does not match its resources");
                    if (UniformBuffers == 1)
                    {
                        const nlohmann::json& Block = Blocks.front();
                        int DescriptorSet{};
                        if (!ReadInteger(Block, "set", DescriptorSet))
                            return false;
                        int Binding{};
                        if (!ReadInteger(Block, "binding", Binding))
                            return false;
                        int BufferSize{};
                        if (!ReadInteger(Block, "size", BufferSize))
                            return false;
                        if (DescriptorSet != (Vertex ? 1 : 3) || Binding != 0 || BufferSize <= 0 || BufferSize > 16384)
                            return ManifestFailure(Name + " uniform buffer does not match the portable effect ABI");
                        Stage.UniformData.assign(static_cast<std::size_t>(BufferSize), 0);

                        std::set<std::string> ParameterNames;
                        if (!Block.at("parameters").is_array())
                            return ManifestFailure("effect parameters must be an array");
                        for (const nlohmann::json& ParameterNode : Block.at("parameters"))
                        {
                            EffectParameter Parameter;
                            Parameter.Name = ParameterNode.at("name").get<std::string>();
                            const std::string Kind = ParameterNode.at("kind").get<std::string>();
                            if (Kind == "boolean")
                                Parameter.Kind = EffectParameterKind::Boolean;
                            else if (Kind == "integer")
                                Parameter.Kind = EffectParameterKind::Integer;
                            else if (Kind == "float")
                                Parameter.Kind = EffectParameterKind::Float;
                            else if (Kind == "vector")
                                Parameter.Kind = EffectParameterKind::Vector;
                            else if (Kind == "matrix")
                                Parameter.Kind = EffectParameterKind::Matrix;
                            else if (Kind != "opaque")
                                return ManifestFailure(Name + " uniform parameter kind is invalid");
                            int Offset{};
                            if (!ReadInteger(ParameterNode, "offset", Offset))
                                return false;
                            int Size{};
                            if (!ReadInteger(ParameterNode, "size", Size))
                                return false;
                            if (Parameter.Name.empty() || !ParameterNames.insert(Parameter.Name).second || Offset < 0 ||
                                Size <= 0 || Offset > BufferSize - Size)
                                return ManifestFailure(Name + " uniform parameter layout is invalid");
                            Parameter.Offset = static_cast<Uint32>(Offset);
                            Parameter.Size = static_cast<Uint32>(Size);
                            Parameter.RowMajor = ParameterNode.value("row_major", false);
                            Parameter.Automatic =
                                Vertex && (Parameter.Name == "ViewProj" || Parameter.Name == "ViewProjection") &&
                                Parameter.Kind == EffectParameterKind::Matrix;
                            Stage.Parameters.push_back(std::move(Parameter));
                        }
                    }
                }

                if (Vertex && Node.contains("inputs"))
                {
                    constexpr int Components[] = {3, 3, 4, 2, 4, 4, 4};
                    std::set<int> Locations;
                    if (!Node.at("inputs").is_array())
                        return ManifestFailure("vertex inputs must be an array");
                    for (const nlohmann::json& Input : Node.at("inputs"))
                    {
                        int Location{};
                        if (!ReadInteger(Input, "location", Location))
                            return false;
                        int ComponentCount{};
                        if (!ReadInteger(Input, "components", ComponentCount))
                            return false;
                        const std::string Type = Input.at("type").get<std::string>();
                        if (Location < 0 || Location >= static_cast<int>(std::size(Components)) ||
                            ComponentCount != Components[Location] || Type != "float32" ||
                            !Locations.insert(Location).second)
                            return ManifestFailure("vertex inputs do not match the OpenDBC effect ABI");
                    }
                }

                std::error_code PathError;
                const fs::path StageRoot = fs::weakly_canonical(
                    ManifestPath.has_parent_path() ? ManifestPath.parent_path() : dbc::PathFromUtf8("."), PathError);
                if (PathError)
                    return ManifestFailure("cannot resolve the stage directory: " + PathError.message());
                const fs::path StagePath = fs::weakly_canonical(StageRoot / File, PathError);
                if (PathError || StagePath.parent_path() != StageRoot)
                    return ManifestFailure(Name + " stage file must remain inside its manifest directory");
                const fs::path MslPath = fs::weakly_canonical(StageRoot / MslFile, PathError);
                if (PathError || MslPath.parent_path() != StageRoot)
                    return ManifestFailure(Name + " stage file must remain inside its manifest directory");
                Stage.Msl = LoadFileBytes(dbc::PathToUtf8(MslPath));
                if (Stage.Msl.empty() || std::find(Stage.Msl.begin(), Stage.Msl.end(), Uint8{0}) != Stage.Msl.end())
                    return ManifestFailure(Name + " stage is not valid MSL source");
                Stage.Bytecode = LoadFileBytes(dbc::PathToUtf8(StagePath));
                std::uint32_t Magic = 0;
                if (Stage.Bytecode.size() < sizeof(Magic) || Stage.Bytecode.size() % sizeof(Magic) != 0)
                    return ManifestFailure(Name + " stage is not valid SPIR-V");
                std::memcpy(&Magic, Stage.Bytecode.data(), sizeof(Magic));
                if (Magic != UINT32_C(0x07230203))
                    return ManifestFailure(Name + " stage has an invalid SPIR-V header");
                return true;
            };

            const auto ReadTechnique =
                [&](const nlohmann::json& Stages, const std::string& Name, EffectTechnique& Technique)
            {
                if (!ReadStage(Stages.at("vertex"), Name + " vertex", Technique.Vertex, true))
                    return false;
                if (!ReadStage(Stages.at("fragment"), Name + " fragment", Technique.Fragment, false))
                    return false;
                return true;
            };

            EffectTechnique Default;
            if (!ReadTechnique(Manifest.at("stages"), "default", Default))
                return false;
            Loaded.Techniques.emplace("default", std::move(Default));
            if (Manifest.contains("techniques"))
            {
                const nlohmann::json& Techniques = Manifest.at("techniques");
                if (!Techniques.is_array() || Techniques.size() > 16)
                    return ManifestFailure("effect techniques are invalid");
                for (const nlohmann::json& TechniqueNode : Techniques)
                {
                    const std::string Name = TechniqueNode.at("name").get<std::string>();
                    if (Name.empty() || Name == "default" || Name.find_first_of(" \t\r\n") != std::string::npos ||
                        Loaded.Techniques.contains(Name))
                        return ManifestFailure("effect technique name is invalid");
                    EffectTechnique Technique;
                    if (!ReadTechnique(TechniqueNode.at("stages"), Name, Technique))
                        return false;
                    Loaded.Techniques.emplace(Name, std::move(Technique));
                }
            }

            std::unordered_map<std::string, std::pair<EffectParameterKind, bool>> ParameterTypes;
            for (const auto& [TechniqueName, Technique] : Loaded.Techniques)
            {
                (void)TechniqueName;
                const auto ValidateParameters = [&](const EffectStage& Stage)
                {
                    for (const EffectParameter& Parameter : Stage.Parameters)
                    {
                        const auto [It, Inserted] =
                            ParameterTypes.emplace(Parameter.Name, std::pair{Parameter.Kind, Parameter.RowMajor});
                        if (!Inserted &&
                            (It->second.first != Parameter.Kind || (Parameter.Kind == EffectParameterKind::Matrix &&
                                                                    It->second.second != Parameter.RowMajor)))
                            return ManifestFailure("effect constant " + Parameter.Name +
                                                   " has inconsistent technique layouts");
                    }
                    return true;
                };
                if (!ValidateParameters(Technique.Vertex))
                    return false;
                if (!ValidateParameters(Technique.Fragment))
                    return false;
            }
        }
        catch (const nlohmann::json::exception& JsonError)
        {
            if (Error)
                *Error = "LOAD EFFECT invalid manifest " + dbc::PathToUtf8(ManifestPath) + ": " + JsonError.what();
            return false;
        }

        const auto Existing = Effects_.find(EffectId);
        if (Existing != Effects_.end())
        {
            ReleaseEffectPipelines(Existing->second);
            Effects_.erase(Existing);
        }
        Effects_.emplace(EffectId, std::move(Loaded));
        return true;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::CreateGpuPipeline()
    {
        if (!GpuDevice_ || !Kernel_.Core().Window())
        {
            return false;
        }

        SDL_GPUShader* VertexShader = CreateEngineShader("dbclassic_tri", SDL_GPU_SHADERSTAGE_VERTEX, 0, 3);
        SDL_GPUShader* FragmentShader = CreateEngineShader("dbclassic_tri", SDL_GPU_SHADERSTAGE_FRAGMENT, 9, 2);

        if (!VertexShader || !FragmentShader)
        {
            if (VertexShader)
            {
                SDL_ReleaseGPUShader(GpuDevice_, VertexShader);
            }
            if (FragmentShader)
            {
                SDL_ReleaseGPUShader(GpuDevice_, FragmentShader);
            }
            return false;
        }

        const SDL_GPUTextureUsageFlags DepthUsage =
            static_cast<SDL_GPUTextureUsageFlags>(SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        GpuDepthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        GpuStencilSupported_ = false;
        constexpr SDL_GPUTextureFormat StencilCandidates[]{
            SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        };
        for (SDL_GPUTextureFormat Candidate : StencilCandidates)
        {
            if (SDL_GPUTextureSupportsFormat(GpuDevice_, Candidate, SDL_GPU_TEXTURETYPE_2D, DepthUsage))
            {
                GpuDepthFormat_ = Candidate;
                GpuStencilSupported_ = true;
                break;
            }
        }
        if (GpuDepthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            constexpr SDL_GPUTextureFormat DepthCandidates[]{
                SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                SDL_GPU_TEXTUREFORMAT_D16_UNORM,
            };
            for (SDL_GPUTextureFormat Candidate : DepthCandidates)
            {
                if (SDL_GPUTextureSupportsFormat(GpuDevice_, Candidate, SDL_GPU_TEXTURETYPE_2D, DepthUsage))
                {
                    GpuDepthFormat_ = Candidate;
                    break;
                }
            }
        }
        if (GpuDepthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            DBC_LOG_ERROR("No compatible GPU depth format found for this device.");
            SDL_ReleaseGPUShader(GpuDevice_, VertexShader);
            SDL_ReleaseGPUShader(GpuDevice_, FragmentShader);
            return false;
        }

        SDL_GPUColorTargetDescription ColorTarget{};
        GpuColorFormat_ = SDL_GetGPUSwapchainTextureFormat(GpuDevice_, Kernel_.Core().Window());
        ColorTarget.format = GpuColorFormat_;
        ColorTarget.blend_state.enable_blend = false;
        ColorTarget.blend_state.enable_color_write_mask = true;
        ColorTarget.blend_state.color_write_mask = 0xF;
        ColorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ColorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        ColorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ColorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ColorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ColorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        SDL_GPUVertexBufferDescription VertexBuffer{};
        VertexBuffer.slot = 0;
        VertexBuffer.pitch = sizeof(GpuVertex);
        VertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        VertexBuffer.instance_step_rate = 0;

        SDL_GPUVertexAttribute Attributes[15]{};
        Attributes[0].location = 0;
        Attributes[0].buffer_slot = 0;
        Attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        Attributes[0].offset = offsetof(GpuVertex, X);
        Attributes[1].location = 1;
        Attributes[1].buffer_slot = 0;
        Attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        Attributes[1].offset = offsetof(GpuVertex, Nx);
        Attributes[2].location = 2;
        Attributes[2].buffer_slot = 0;
        Attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        Attributes[2].offset = offsetof(GpuVertex, R);
        Attributes[3].location = 3;
        Attributes[3].buffer_slot = 0;
        Attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        Attributes[3].offset = offsetof(GpuVertex, U);
        Attributes[4].location = 4;
        Attributes[4].buffer_slot = 0;
        Attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        Attributes[4].offset = offsetof(GpuVertex, BoxTrR);
        Attributes[5].location = 5;
        Attributes[5].buffer_slot = 0;
        Attributes[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        Attributes[5].offset = offsetof(GpuVertex, BoxBrR);
        Attributes[6].location = 6;
        Attributes[6].buffer_slot = 0;
        Attributes[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        Attributes[6].offset = offsetof(GpuVertex, BoxBlR);
        for (Uint32 Stage = 0; Stage < 7; ++Stage)
        {
            Attributes[7 + Stage].location = 7 + Stage;
            Attributes[7 + Stage].buffer_slot = 0;
            Attributes[7 + Stage].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            Attributes[7 + Stage].offset =
                static_cast<Uint32>(offsetof(GpuVertex, AuxiliaryUvs) + Stage * sizeof(float) * 2);
        }
        Attributes[14].location = 14;
        Attributes[14].buffer_slot = 0;
        Attributes[14].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        Attributes[14].offset = offsetof(GpuVertex, CubeDirection);

        SDL_GPUGraphicsPipelineCreateInfo PipelineInfo{};
        PipelineInfo.vertex_shader = VertexShader;
        PipelineInfo.fragment_shader = FragmentShader;
        PipelineInfo.vertex_input_state.vertex_buffer_descriptions = &VertexBuffer;
        PipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        PipelineInfo.vertex_input_state.vertex_attributes = Attributes;
        PipelineInfo.vertex_input_state.num_vertex_attributes = 15;
        PipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        PipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        PipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        PipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        PipelineInfo.rasterizer_state.enable_depth_clip = true;
        PipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        PipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        PipelineInfo.depth_stencil_state.enable_depth_test = true;
        PipelineInfo.depth_stencil_state.enable_depth_write = true;
        PipelineInfo.depth_stencil_state.enable_stencil_test = false;
        PipelineInfo.target_info.num_color_targets = 1;
        PipelineInfo.target_info.color_target_descriptions = &ColorTarget;
        PipelineInfo.target_info.depth_stencil_format = GpuDepthFormat_;
        PipelineInfo.target_info.has_depth_stencil_target = true;
        PipelineInfo.props = 0;

        GpuPipeline_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &PipelineInfo);

        if (GpuStencilSupported_ && StencilPipelinesRequested_)
        {
            SDL_GPUColorTargetDescription MaskTarget = ColorTarget;
            MaskTarget.blend_state.color_write_mask = 0;
            SDL_GPUGraphicsPipelineCreateInfo MaskInfo = PipelineInfo;
            MaskInfo.target_info.color_target_descriptions = &MaskTarget;
            MaskInfo.depth_stencil_state.enable_depth_write = false;
            MaskInfo.depth_stencil_state.enable_stencil_test = true;
            MaskInfo.depth_stencil_state.compare_mask = 0xFF;
            MaskInfo.depth_stencil_state.write_mask = 0xFF;
            MaskInfo.depth_stencil_state.front_stencil_state = {SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_REPLACE,
                                                                SDL_GPU_STENCILOP_KEEP, SDL_GPU_COMPAREOP_ALWAYS};
            MaskInfo.depth_stencil_state.back_stencil_state = MaskInfo.depth_stencil_state.front_stencil_state;
            GpuReflectionMaskPipeline_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &MaskInfo);

            SDL_GPUGraphicsPipelineCreateInfo ShadowDepthInfo = PipelineInfo;
            ShadowDepthInfo.target_info.color_target_descriptions = &MaskTarget;
            GpuShadowDepthPipeline_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &ShadowDepthInfo);

            SDL_GPUGraphicsPipelineCreateInfo ShadowVolumeInfo = PipelineInfo;
            ShadowVolumeInfo.target_info.color_target_descriptions = &MaskTarget;
            ShadowVolumeInfo.depth_stencil_state.enable_depth_write = false;
            ShadowVolumeInfo.depth_stencil_state.enable_stencil_test = true;
            ShadowVolumeInfo.depth_stencil_state.compare_mask = 0xFF;
            ShadowVolumeInfo.depth_stencil_state.write_mask = 0xFF;
            ShadowVolumeInfo.depth_stencil_state.front_stencil_state = {SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP,
                                                                        SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,
                                                                        SDL_GPU_COMPAREOP_ALWAYS};
            ShadowVolumeInfo.depth_stencil_state.back_stencil_state = {SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP,
                                                                       SDL_GPU_STENCILOP_DECREMENT_AND_WRAP,
                                                                       SDL_GPU_COMPAREOP_ALWAYS};
            GpuShadowVolumePipeline_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &ShadowVolumeInfo);

            SDL_GPUColorTargetDescription ShadowOverlayTarget = ColorTarget;
            ShadowOverlayTarget.blend_state.enable_blend = true;
            SDL_GPUGraphicsPipelineCreateInfo ShadowOverlayInfo = PipelineInfo;
            ShadowOverlayInfo.target_info.color_target_descriptions = &ShadowOverlayTarget;
            ShadowOverlayInfo.depth_stencil_state.enable_depth_test = false;
            ShadowOverlayInfo.depth_stencil_state.enable_depth_write = false;
            ShadowOverlayInfo.depth_stencil_state.enable_stencil_test = true;
            ShadowOverlayInfo.depth_stencil_state.compare_mask = 0xFF;
            ShadowOverlayInfo.depth_stencil_state.write_mask = 0;
            ShadowOverlayInfo.depth_stencil_state.front_stencil_state = {
                SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP, SDL_GPU_COMPAREOP_NOT_EQUAL};
            ShadowOverlayInfo.depth_stencil_state.back_stencil_state =
                ShadowOverlayInfo.depth_stencil_state.front_stencil_state;
            GpuShadowOverlayPipeline_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &ShadowOverlayInfo);
        }

        SDL_GPUGraphicsPipelineCreateInfo OpaqueNoDepthInfo = PipelineInfo;
        OpaqueNoDepthInfo.depth_stencil_state.enable_depth_write = false;
        GpuSurfaceOpaqueNoDepthPipelines_[0] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &OpaqueNoDepthInfo);
        OpaqueNoDepthInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        GpuSurfaceOpaqueNoDepthPipelines_[1] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &OpaqueNoDepthInfo);

        SDL_GPUColorTargetDescription AlphaColorTarget = ColorTarget;
        AlphaColorTarget.blend_state.enable_blend = true;
        SDL_GPUGraphicsPipelineCreateInfo AlphaWritePipelineInfo = PipelineInfo;
        AlphaWritePipelineInfo.target_info.color_target_descriptions = &AlphaColorTarget;
        GpuPipelineAlphaWrite_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &AlphaWritePipelineInfo);

        SDL_GPUGraphicsPipelineCreateInfo TransparentPipelineInfo = PipelineInfo;
        TransparentPipelineInfo.depth_stencil_state.enable_depth_write = false;
        TransparentPipelineInfo.target_info.color_target_descriptions = &AlphaColorTarget;
        GpuPipelineTransparent_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &TransparentPipelineInfo);

        // Additive blend pipeline for ghost mode 0 (SRC_ALPHA / ONE).
        SDL_GPUColorTargetDescription AdditiveColorTarget = AlphaColorTarget;
        AdditiveColorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        AdditiveColorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        SDL_GPUGraphicsPipelineCreateInfo AdditivePipelineInfo = PipelineInfo;
        AdditivePipelineInfo.depth_stencil_state.enable_depth_write = false;
        AdditivePipelineInfo.target_info.color_target_descriptions = &AdditiveColorTarget;
        GpuPipelineAdditive_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &AdditivePipelineInfo);

        // Static terrain: world-space position, normal and UV per vertex, and the attributes the shader expects
        // but a terrain does not vary (colour, auxiliary UVs) read once per instance from a GpuVertex record.
        SDL_GPUVertexBufferDescription TerrainBuffers[2]{};
        TerrainBuffers[0].slot = 0;
        TerrainBuffers[0].pitch = sizeof(TerrainVertex);
        TerrainBuffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        TerrainBuffers[1].slot = 1;
        TerrainBuffers[1].pitch = sizeof(GpuVertex);
        TerrainBuffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        SDL_GPUVertexAttribute TerrainAttributes[15]{};
        std::copy(std::begin(Attributes), std::end(Attributes), std::begin(TerrainAttributes));
        for (SDL_GPUVertexAttribute& Attribute : TerrainAttributes)
            Attribute.buffer_slot = 1;
        TerrainAttributes[0].buffer_slot = 0;
        TerrainAttributes[0].offset = offsetof(TerrainVertex, Position);
        TerrainAttributes[1].buffer_slot = 0;
        TerrainAttributes[1].offset = offsetof(TerrainVertex, Normal);
        TerrainAttributes[3].buffer_slot = 0;
        TerrainAttributes[3].offset = offsetof(TerrainVertex, Uv);
        SDL_GPUGraphicsPipelineCreateInfo TerrainInfo = PipelineInfo;
        TerrainInfo.vertex_input_state.vertex_buffer_descriptions = TerrainBuffers;
        TerrainInfo.vertex_input_state.num_vertex_buffers = 2;
        TerrainInfo.vertex_input_state.vertex_attributes = TerrainAttributes;
        // The CPU path culls terrain triangles facing away from the camera; the GPU does it here.
        TerrainInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        TerrainInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        GpuTerrainPipelines_[0] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &TerrainInfo);
        SDL_GPUGraphicsPipelineCreateInfo TerrainDepthInfo = TerrainInfo;
        SDL_GPUColorTargetDescription TerrainDepthTarget = ColorTarget;
        TerrainDepthTarget.blend_state.color_write_mask = 0;
        TerrainDepthInfo.target_info.color_target_descriptions = &TerrainDepthTarget;
        GpuTerrainPipelines_[2] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &TerrainDepthInfo);
        TerrainInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        GpuTerrainPipelines_[1] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &TerrainInfo);

        // GPU-resident object meshes: object-space position, normal, texture coordinates and colour per vertex, and
        // the shader's other inputs once per draw from a GpuVertex record, as for terrain.
        SDL_GPUVertexBufferDescription MeshBuffers[2]{TerrainBuffers[0], TerrainBuffers[1]};
        MeshBuffers[0].pitch = sizeof(GpuMeshVertex);
        SDL_GPUVertexAttribute MeshAttributes[15]{};
        std::copy(std::begin(Attributes), std::end(Attributes), std::begin(MeshAttributes));
        for (SDL_GPUVertexAttribute& Attribute : MeshAttributes)
            Attribute.buffer_slot = 1;
        MeshAttributes[0].buffer_slot = 0;
        MeshAttributes[0].offset = offsetof(GpuMeshVertex, Position);
        MeshAttributes[1].buffer_slot = 0;
        MeshAttributes[1].offset = offsetof(GpuMeshVertex, Normal);
        MeshAttributes[2].buffer_slot = 0;
        MeshAttributes[2].offset = offsetof(GpuMeshVertex, Color);
        MeshAttributes[3].buffer_slot = 0;
        MeshAttributes[3].offset = offsetof(GpuMeshVertex, Uv);
        SDL_GPUGraphicsPipelineCreateInfo MeshInfo = PipelineInfo;
        MeshInfo.vertex_input_state.vertex_buffer_descriptions = MeshBuffers;
        MeshInfo.vertex_input_state.num_vertex_buffers = 2;
        MeshInfo.vertex_input_state.vertex_attributes = MeshAttributes;
        // The CPU path culls the triangles facing away from the camera; the GPU does it here. Object triangles
        // wind the other way round from terrain ones.
        MeshInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        for (std::size_t Index = 0; Index < GpuMeshPipelines_.size(); ++Index)
        {
            MeshInfo.rasterizer_state.cull_mode = Index < 2 ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
            MeshInfo.rasterizer_state.fill_mode = Index % 2 == 0 ? SDL_GPU_FILLMODE_FILL : SDL_GPU_FILLMODE_LINE;
            GpuMeshPipelines_[Index] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &MeshInfo);
        }

        // Wireframe surfaces rasterise the same triangles as lines.
        SDL_GPUGraphicsPipelineCreateInfo WireInfo = PipelineInfo;
        WireInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        GpuPipelineWireframe_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &WireInfo);
        SDL_GPUGraphicsPipelineCreateInfo AlphaWriteWireInfo = AlphaWritePipelineInfo;
        AlphaWriteWireInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        GpuPipelineAlphaWriteWireframe_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &AlphaWriteWireInfo);
        SDL_GPUGraphicsPipelineCreateInfo TransparentWireInfo = TransparentPipelineInfo;
        TransparentWireInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        GpuPipelineTransparentWireframe_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &TransparentWireInfo);
        SDL_GPUGraphicsPipelineCreateInfo AdditiveWireInfo = AdditivePipelineInfo;
        AdditiveWireInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        GpuPipelineAdditiveWireframe_ = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &AdditiveWireInfo);

        const SDL_GPUColorTargetDescription* SurfaceTargets[3]{&ColorTarget, &AlphaColorTarget, &AdditiveColorTarget};
        const int StencilVariants = GpuStencilSupported_ && StencilPipelinesRequested_ ? 2 : 1;
        for (int StencilTest = 0; StencilTest < StencilVariants; ++StencilTest)
        {
            for (int DepthTest = 0; DepthTest < 2; ++DepthTest)
            {
                for (int DepthWrite = 0; DepthWrite < 2; ++DepthWrite)
                {
                    for (int Wireframe = 0; Wireframe < 2; ++Wireframe)
                    {
                        for (int Blend = 0; Blend < 3; ++Blend)
                        {
                            SDL_GPUGraphicsPipelineCreateInfo SurfaceInfo = PipelineInfo;
                            SurfaceInfo.depth_stencil_state.enable_depth_test = DepthTest != 0;
                            SurfaceInfo.depth_stencil_state.enable_depth_write = DepthWrite != 0;
                            SurfaceInfo.depth_stencil_state.enable_stencil_test = StencilTest != 0;
                            if (StencilTest != 0)
                            {
                                SurfaceInfo.depth_stencil_state.compare_mask = 0xFF;
                                SurfaceInfo.depth_stencil_state.write_mask = 0;
                                SurfaceInfo.depth_stencil_state.front_stencil_state = {
                                    SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP,
                                    SDL_GPU_COMPAREOP_EQUAL};
                                SurfaceInfo.depth_stencil_state.back_stencil_state =
                                    SurfaceInfo.depth_stencil_state.front_stencil_state;
                            }
                            SurfaceInfo.rasterizer_state.fill_mode =
                                Wireframe != 0 ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
                            SurfaceInfo.target_info.color_target_descriptions = SurfaceTargets[Blend];
                            const size_t Index = static_cast<size_t>(Blend + Wireframe * 3 + DepthWrite * 6 +
                                                                     DepthTest * 12 + StencilTest * 24);
                            GpuSurfaceDepthPipelines_[Index] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &SurfaceInfo);
                        }
                    }
                }
            }
        }

        for (int Mode = 0; Mode < 5; ++Mode)
        {
            SDL_GPUColorTargetDescription GhostTarget = AlphaColorTarget;
            switch (Mode)
            {
                case 0:
                    GhostTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                    GhostTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
                    break;
                case 1:
                    GhostTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
                    GhostTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
                    break;
                case 2:
                    GhostTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
                    GhostTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                    break;
                case 3:
                    GhostTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                    GhostTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                    break;
                case 4:
                    GhostTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
                    GhostTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
                    break;
            }
            GhostTarget.blend_state.src_alpha_blendfactor = GhostTarget.blend_state.src_color_blendfactor;
            GhostTarget.blend_state.dst_alpha_blendfactor = GhostTarget.blend_state.dst_color_blendfactor;
            for (int StencilTest = 0; StencilTest < StencilVariants; ++StencilTest)
            {
                for (int DepthTest = 0; DepthTest < 2; ++DepthTest)
                {
                    for (int DepthWrite = 0; DepthWrite < 2; ++DepthWrite)
                    {
                        SDL_GPUGraphicsPipelineCreateInfo GhostInfo = PipelineInfo;
                        GhostInfo.depth_stencil_state.enable_depth_test = DepthTest != 0;
                        GhostInfo.depth_stencil_state.enable_depth_write = DepthWrite != 0;
                        GhostInfo.depth_stencil_state.enable_stencil_test = StencilTest != 0;
                        if (StencilTest != 0)
                        {
                            GhostInfo.depth_stencil_state.compare_mask = 0xFF;
                            GhostInfo.depth_stencil_state.write_mask = 0;
                            GhostInfo.depth_stencil_state.front_stencil_state = {
                                SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP,
                                SDL_GPU_COMPAREOP_EQUAL};
                            GhostInfo.depth_stencil_state.back_stencil_state =
                                GhostInfo.depth_stencil_state.front_stencil_state;
                        }
                        GhostInfo.target_info.color_target_descriptions = &GhostTarget;
                        const size_t Index =
                            static_cast<size_t>(Mode * 8 + DepthTest * 4 + DepthWrite * 2 + StencilTest * 40);
                        GpuSurfaceGhostPipelines_[Index] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &GhostInfo);
                        GhostInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
                        GpuSurfaceGhostPipelines_[Index + 1] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &GhostInfo);
                    }
                }
            }
        }

        SDL_ReleaseGPUShader(GpuDevice_, VertexShader);
        SDL_ReleaseGPUShader(GpuDevice_, FragmentShader);

        if (!GpuPipeline_ || !GpuPipelineAlphaWrite_ || !GpuPipelineTransparent_ || !GpuPipelineAdditive_ ||
            !GpuPipelineWireframe_ || !GpuPipelineAlphaWriteWireframe_ || !GpuPipelineTransparentWireframe_ ||
            !GpuPipelineAdditiveWireframe_ ||
            (GpuStencilSupported_ && StencilPipelinesRequested_ &&
             (!GpuReflectionMaskPipeline_ || !GpuShadowDepthPipeline_ || !GpuShadowVolumePipeline_ ||
              !GpuShadowOverlayPipeline_)) ||
            std::any_of(GpuSurfaceOpaqueNoDepthPipelines_.begin(), GpuSurfaceOpaqueNoDepthPipelines_.end(),
                        [](SDL_GPUGraphicsPipeline* Pipeline) { return Pipeline == nullptr; }) ||
            std::any_of(GpuTerrainPipelines_.begin(), GpuTerrainPipelines_.end(),
                        [](SDL_GPUGraphicsPipeline* Pipeline) { return Pipeline == nullptr; }) ||
            std::any_of(GpuMeshPipelines_.begin(), GpuMeshPipelines_.end(),
                        [](SDL_GPUGraphicsPipeline* Pipeline) { return Pipeline == nullptr; }) ||
            std::any_of(GpuSurfaceDepthPipelines_.begin(),
                        GpuSurfaceDepthPipelines_.begin() + (StencilVariants == 2 ? 48 : 24),
                        [](SDL_GPUGraphicsPipeline* Pipeline) { return Pipeline == nullptr; }) ||
            std::any_of(GpuSurfaceGhostPipelines_.begin(),
                        GpuSurfaceGhostPipelines_.begin() + (StencilVariants == 2 ? 80 : 40),
                        [](SDL_GPUGraphicsPipeline* Pipeline) { return Pipeline == nullptr; }))
        {
            DBC_LOG_ERROR("SDL_CreateGPUGraphicsPipeline failed: {}", SDL_GetError());
            return false;
        }

        SDL_GPUSamplerCreateInfo SamplerInfo{};
        SamplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        SamplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        SamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        SamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        SamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        SamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        SamplerInfo.compare_op = SDL_GPU_COMPAREOP_NEVER;
        SamplerInfo.min_lod = 0.0f;
        SamplerInfo.max_lod = 16.0f;
        SamplerInfo.props = 0;
        GpuSampler_ = SDL_CreateGPUSampler(GpuDevice_, &SamplerInfo);
        SDL_GPUSamplerCreateInfo NearestInfo = SamplerInfo;
        NearestInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        NearestInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        NearestInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        GpuSamplerNearest_ = SDL_CreateGPUSampler(GpuDevice_, &NearestInfo);
        constexpr std::array<SDL_GPUSamplerAddressMode, 3> AddressModes{
            SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
            SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        for (int Address = 0; Address < 3; ++Address)
        {
            for (int Filter = 0; Filter < 2; ++Filter)
            {
                for (int Mip = 0; Mip < 3; ++Mip)
                {
                    SDL_GPUSamplerCreateInfo SurfaceInfo = SamplerInfo;
                    SurfaceInfo.address_mode_u = AddressModes[Address];
                    SurfaceInfo.address_mode_v = AddressModes[Address];
                    SurfaceInfo.address_mode_w = AddressModes[Address];
                    SurfaceInfo.min_filter = Filter != 0 ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
                    SurfaceInfo.mag_filter = Filter != 0 ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
                    SurfaceInfo.mipmap_mode =
                        Mip == 2 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                    if (Mip == 0)
                        SurfaceInfo.max_lod = 0.0f;
                    const size_t Index = static_cast<size_t>(Address * 6 + Filter * 3 + Mip);
                    GpuSurfaceSamplers_[Index] = SDL_CreateGPUSampler(GpuDevice_, &SurfaceInfo);
                }
            }
        }
        if (!GpuSampler_ || !GpuSamplerNearest_ ||
            std::any_of(GpuSurfaceSamplers_.begin(), GpuSurfaceSamplers_.end(),
                        [](SDL_GPUSampler* Sampler) { return Sampler == nullptr; }))
        {
            DBC_LOG_ERROR("SDL_CreateGPUSampler failed: {}", SDL_GetError());
            return false;
        }
        return true;
    }

    SDL_GPUGraphicsPipeline* GraphicsDeviceSystem::EffectPipeline(int EffectId, bool Transparent, bool AlphaDepthWrite,
                                                                  bool Additive, bool Wireframe, bool DepthTest,
                                                                  bool DepthWrite, bool StencilTest)
    {
        const auto It = Effects_.find(EffectId);
        if (It == Effects_.end() || !GpuDevice_)
            return nullptr;
        EffectResource& Effect = It->second;
        const auto TechniqueIt = Effect.Techniques.find(Effect.CurrentTechnique);
        if (TechniqueIt == Effect.Techniques.end())
            return nullptr;
        EffectTechnique& Technique = TechniqueIt->second;
        const int BlendIndex = Additive ? 3 : (Transparent ? 2 : (AlphaDepthWrite ? 1 : 0));
        const std::size_t PipelineIndex = static_cast<std::size_t>(
            BlendIndex + (Wireframe ? 4 : 0) + (DepthWrite ? 8 : 0) + (DepthTest ? 16 : 0) + (StencilTest ? 32 : 0));
        if (Technique.Pipelines[PipelineIndex])
            return Technique.Pipelines[PipelineIndex];

        SDL_GPUShader* VertexShader = CreateGpuShader(Technique.Vertex, SDL_GPU_SHADERSTAGE_VERTEX);
        SDL_GPUShader* FragmentShader = CreateGpuShader(Technique.Fragment, SDL_GPU_SHADERSTAGE_FRAGMENT);
        if (!VertexShader || !FragmentShader)
        {
            if (VertexShader)
                SDL_ReleaseGPUShader(GpuDevice_, VertexShader);
            if (FragmentShader)
                SDL_ReleaseGPUShader(GpuDevice_, FragmentShader);
            return nullptr;
        }

        const bool Created = CreatePipelineSet(VertexShader, FragmentShader, Technique.Pipelines);
        SDL_ReleaseGPUShader(GpuDevice_, VertexShader);
        SDL_ReleaseGPUShader(GpuDevice_, FragmentShader);
        if (!Created)
        {
            ReleaseEffectPipelines(Effect);
            if (!Effect.PipelineFailureReported)
                DBC_LOG_ERROR("effect pipeline creation failed ({}): {}", Effect.ManifestPath, SDL_GetError());
            Effect.PipelineFailureReported = true;
            return nullptr;
        }
        Effect.PipelineFailureReported = false;
        return Technique.Pipelines[PipelineIndex];
    }

    bool GraphicsDeviceSystem::CreatePipelineSet(SDL_GPUShader* VertexShader, SDL_GPUShader* FragmentShader,
                                                 std::array<SDL_GPUGraphicsPipeline*, 64>& Pipelines)
    {
        if (!GpuDevice_ || GpuColorFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID ||
            GpuDepthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID)
            return false;

        SDL_GPUColorTargetDescription ColorTarget{};
        ColorTarget.format = GpuColorFormat_;
        ColorTarget.blend_state.enable_blend = false;
        ColorTarget.blend_state.enable_color_write_mask = true;
        ColorTarget.blend_state.color_write_mask = 0xF;
        ColorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ColorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        ColorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ColorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ColorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ColorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        SDL_GPUVertexBufferDescription VertexBuffer{};
        VertexBuffer.slot = 0;
        VertexBuffer.pitch = sizeof(GpuVertex);
        VertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute Attributes[7]{};
        Attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, X)};
        Attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, Nx)};
        Attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, R)};
        Attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, U)};
        Attributes[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, BoxTrR)};
        Attributes[5] = {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, BoxBrR)};
        Attributes[6] = {6, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, BoxBlR)};

        SDL_GPUGraphicsPipelineCreateInfo Pipeline{};
        Pipeline.vertex_shader = VertexShader;
        Pipeline.fragment_shader = FragmentShader;
        Pipeline.vertex_input_state.vertex_buffer_descriptions = &VertexBuffer;
        Pipeline.vertex_input_state.num_vertex_buffers = 1;
        Pipeline.vertex_input_state.vertex_attributes = Attributes;
        Pipeline.vertex_input_state.num_vertex_attributes = 7;
        Pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        Pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        Pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        Pipeline.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        Pipeline.rasterizer_state.enable_depth_clip = true;
        Pipeline.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        Pipeline.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        Pipeline.depth_stencil_state.enable_depth_test = true;
        Pipeline.depth_stencil_state.enable_depth_write = true;
        Pipeline.target_info.num_color_targets = 1;
        Pipeline.target_info.color_target_descriptions = &ColorTarget;
        Pipeline.target_info.depth_stencil_format = GpuDepthFormat_;
        Pipeline.target_info.has_depth_stencil_target = true;

        SDL_GPUColorTargetDescription AlphaTarget = ColorTarget;
        AlphaTarget.blend_state.enable_blend = true;
        SDL_GPUColorTargetDescription AdditiveTarget = AlphaTarget;
        AdditiveTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        AdditiveTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        const SDL_GPUColorTargetDescription* Targets[4]{&ColorTarget, &AlphaTarget, &AlphaTarget, &AdditiveTarget};
        const int StencilVariants = GpuStencilSupported_ && StencilPipelinesRequested_ ? 2 : 1;
        for (int StencilTest = 0; StencilTest < StencilVariants; ++StencilTest)
        {
            for (int DepthTest = 0; DepthTest < 2; ++DepthTest)
            {
                for (int DepthWrite = 0; DepthWrite < 2; ++DepthWrite)
                {
                    for (int Wireframe = 0; Wireframe < 2; ++Wireframe)
                    {
                        for (int Blend = 0; Blend < 4; ++Blend)
                        {
                            SDL_GPUGraphicsPipelineCreateInfo Variant = Pipeline;
                            Variant.depth_stencil_state.enable_depth_test = DepthTest != 0;
                            Variant.depth_stencil_state.enable_depth_write = DepthWrite != 0;
                            Variant.depth_stencil_state.enable_stencil_test = StencilTest != 0;
                            if (StencilTest != 0)
                            {
                                Variant.depth_stencil_state.compare_mask = 0xFF;
                                Variant.depth_stencil_state.write_mask = 0;
                                Variant.depth_stencil_state.front_stencil_state = {
                                    SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP,
                                    SDL_GPU_COMPAREOP_EQUAL};
                                Variant.depth_stencil_state.back_stencil_state =
                                    Variant.depth_stencil_state.front_stencil_state;
                            }
                            Variant.rasterizer_state.fill_mode =
                                Wireframe != 0 ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
                            Variant.target_info.color_target_descriptions = Targets[Blend];
                            const std::size_t Index = static_cast<std::size_t>(Blend + Wireframe * 4 + DepthWrite * 8 +
                                                                               DepthTest * 16 + StencilTest * 32);
                            Pipelines[Index] = SDL_CreateGPUGraphicsPipeline(GpuDevice_, &Variant);
                        }
                    }
                }
            }
        }

        return std::all_of(Pipelines.begin(), Pipelines.begin() + (StencilVariants == 2 ? 64 : 32),
                           [](SDL_GPUGraphicsPipeline* Value) { return Value; });
    }

    bool GraphicsDeviceSystem::EnsureStencilPipelines()
    {
        if (!GpuDevice_ || !GpuStencilSupported_)
            return false;
        if (StencilPipelinesRequested_ && GpuReflectionMaskPipeline_ && GpuShadowDepthPipeline_ &&
            GpuShadowVolumePipeline_ && GpuShadowOverlayPipeline_)
            return true;
        if (!SDL_WaitForGPUIdle(GpuDevice_))
            return false;
        StencilPipelinesRequested_ = true;
        ReleaseGpuPipelineResources();
        return CreateGpuPipeline() && GpuStencilSupported_ && GpuReflectionMaskPipeline_ && GpuShadowDepthPipeline_ &&
               GpuShadowVolumePipeline_ && GpuShadowOverlayPipeline_;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::EnsureGpuCaptureTarget(Uint32 Width, Uint32 Height)
    {
        if (!GpuDevice_ || !Renderer_ || Width == 0 || Height == 0)
        {
            return false;
        }
        if (GpuCaptureTexture_ && GpuFinalColorTexture_ && GpuCaptureSdlTexture_ && GpuFinalColorSdlTexture_ &&
            GpuCaptureWidth_ == Width && GpuCaptureHeight_ == Height)
        {
            return true;
        }
        if (GpuCaptureSdlTexture_)
        {
            SDL_DestroyTexture(GpuCaptureSdlTexture_);
            GpuCaptureSdlTexture_ = nullptr;
        }
        if (GpuFinalColorSdlTexture_)
        {
            SDL_DestroyTexture(GpuFinalColorSdlTexture_);
            GpuFinalColorSdlTexture_ = nullptr;
        }
        if (GpuCaptureTexture_)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, GpuCaptureTexture_);
            GpuCaptureTexture_ = nullptr;
        }
        if (GpuFinalColorTexture_)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, GpuFinalColorTexture_);
            GpuFinalColorTexture_ = nullptr;
        }

        GpuCaptureFormat_ = GpuColorFormat_;

        SDL_GPUTextureCreateInfo Info{};
        Info.type = SDL_GPU_TEXTURETYPE_2D;
        Info.format = GpuCaptureFormat_;
        Info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        Info.width = Width;
        Info.height = Height;
        Info.layer_count_or_depth = 1;
        Info.num_levels = 1;
        Info.sample_count = SDL_GPU_SAMPLECOUNT_1;

        GpuCaptureTexture_ = SDL_CreateGPUTexture(GpuDevice_, &Info);
        GpuFinalColorTexture_ = SDL_CreateGPUTexture(GpuDevice_, &Info);
        if (!GpuCaptureTexture_ || !GpuFinalColorTexture_)
        {
            DBC_LOG_ERROR("EnsureGpuCaptureTarget failed ({}x{}): {}", Width, Height, SDL_GetError());
            if (GpuCaptureTexture_)
            {
                SDL_ReleaseGPUTexture(GpuDevice_, GpuCaptureTexture_);
                GpuCaptureTexture_ = nullptr;
            }
            if (GpuFinalColorTexture_)
            {
                SDL_ReleaseGPUTexture(GpuDevice_, GpuFinalColorTexture_);
                GpuFinalColorTexture_ = nullptr;
            }
            return false;
        }

        SDL_PixelFormat PixelFormat = SDL_GetPixelFormatFromGPUTextureFormat(GpuCaptureFormat_);
        if (PixelFormat == SDL_PIXELFORMAT_UNKNOWN)
            PixelFormat = SDL_PIXELFORMAT_RGBA32;
        const auto WrapTexture = [&](SDL_GPUTexture* Texture, SDL_TextureAccess Access)
        {
            const SDL_PropertiesID Properties = SDL_CreateProperties();
            if (Properties == 0)
                return static_cast<SDL_Texture*>(nullptr);
            SDL_SetNumberProperty(Properties, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, PixelFormat);
            SDL_SetNumberProperty(Properties, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, Access);
            SDL_SetNumberProperty(Properties, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, Width);
            SDL_SetNumberProperty(Properties, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, Height);
            SDL_SetNumberProperty(Properties, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
            SDL_SetPointerProperty(Properties, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, Texture);
            SDL_Texture* Result = SDL_CreateTextureWithProperties(Renderer_, Properties);
            SDL_DestroyProperties(Properties);
            return Result;
        };
        GpuCaptureSdlTexture_ = WrapTexture(GpuCaptureTexture_, SDL_TEXTUREACCESS_STATIC);
        GpuFinalColorSdlTexture_ = WrapTexture(GpuFinalColorTexture_, SDL_TEXTUREACCESS_TARGET);
        if (!GpuCaptureSdlTexture_ || !GpuFinalColorSdlTexture_ ||
            !SDL_SetTextureBlendMode(GpuCaptureSdlTexture_, SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(GpuCaptureSdlTexture_, SDL_SCALEMODE_NEAREST))
        {
            DBC_LOG_ERROR("EnsureGpuCaptureTarget texture wrapping failed ({}x{}): {}", Width, Height, SDL_GetError());
            if (GpuCaptureSdlTexture_)
            {
                SDL_DestroyTexture(GpuCaptureSdlTexture_);
                GpuCaptureSdlTexture_ = nullptr;
            }
            if (GpuFinalColorSdlTexture_)
            {
                SDL_DestroyTexture(GpuFinalColorSdlTexture_);
                GpuFinalColorSdlTexture_ = nullptr;
            }
            SDL_ReleaseGPUTexture(GpuDevice_, GpuCaptureTexture_);
            SDL_ReleaseGPUTexture(GpuDevice_, GpuFinalColorTexture_);
            GpuCaptureTexture_ = nullptr;
            GpuFinalColorTexture_ = nullptr;
            return false;
        }
        GpuCaptureWidth_ = Width;
        GpuCaptureHeight_ = Height;
        return true;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::ApplyFinalColorScale(float Red, float Green, float Blue)
    {
        if (!Renderer_ || !GpuCaptureSdlTexture_ || !GpuFinalColorSdlTexture_)
            return false;
        if (!SDL_SetRenderTarget(Renderer_, GpuFinalColorSdlTexture_) ||
            !SDL_SetRenderDrawColorFloat(Renderer_, 0.0f, 0.0f, 0.0f, 1.0f) || !SDL_RenderClear(Renderer_) ||
            !SDL_SetTextureColorModFloat(GpuCaptureSdlTexture_, Red, Green, Blue) ||
            !SDL_RenderTexture(Renderer_, GpuCaptureSdlTexture_, nullptr, nullptr) || !SDL_RenderPresent(Renderer_))
        {
            DBC_LOG_ERROR("Final color transform failed: {}", SDL_GetError());
            return false;
        }
        return true;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::EnsureGpuDepthTarget(Uint32 Width, Uint32 Height, int Index)
    {
        if (Index < 0 || Index > 1)
        {
            return false;
        }
        if (!GpuDevice_ || GpuDepthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID || Width == 0 || Height == 0)
        {
            return false;
        }
        if (GpuDepthTexture_[Index] && GpuDepthWidth_[Index] == Width && GpuDepthHeight_[Index] == Height)
        {
            return true;
        }
        if (GpuDepthTexture_[Index])
        {
            SDL_ReleaseGPUTexture(GpuDevice_, GpuDepthTexture_[Index]);
            GpuDepthTexture_[Index] = nullptr;
        }

        SDL_GPUTextureCreateInfo DepthInfo{};
        DepthInfo.type = SDL_GPU_TEXTURETYPE_2D;
        DepthInfo.format = GpuDepthFormat_;
        DepthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        DepthInfo.width = Width;
        DepthInfo.height = Height;
        DepthInfo.layer_count_or_depth = 1;
        DepthInfo.num_levels = 1;
        DepthInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        DepthInfo.props = 0;
        GpuDepthTexture_[Index] = SDL_CreateGPUTexture(GpuDevice_, &DepthInfo);
        if (!GpuDepthTexture_[Index])
        {
            DBC_LOG_ERROR("SDL_CreateGPUTexture(depth[{}]) failed: {}", Index, SDL_GetError());
            GpuDepthWidth_[Index] = 0;
            GpuDepthHeight_[Index] = 0;
            return false;
        }

        GpuDepthWidth_[Index] = Width;
        GpuDepthHeight_[Index] = Height;
        return true;
    }

    [[nodiscard]] bool GraphicsDeviceSystem::EnsureGpuVertexCapacity(Uint32 VertexCount)
    {
        if (!GpuDevice_)
        {
            return false;
        }
        if (VertexCount == 0)
        {
            return true;
        }
        if (VertexCount <= GpuVertexCapacity_ && GpuVertexBuffer_ && GpuTransferBuffer_)
        {
            return true;
        }

        constexpr Uint32 MaximumVertices = std::numeric_limits<Uint32>::max() / sizeof(GpuVertex);
        if (VertexCount > MaximumVertices)
        {
            DBC_LOG_ERROR("GPU vertex buffer exceeds the SDL size limit");
            return false;
        }
        Uint32 NewCapacity = std::max<Uint32>(65536, GpuVertexCapacity_);
        while (NewCapacity < VertexCount)
            NewCapacity = NewCapacity > MaximumVertices / 2 ? MaximumVertices : NewCapacity * 2;

        if (GpuTransferBuffer_)
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, GpuTransferBuffer_);
            GpuTransferBuffer_ = nullptr;
        }
        if (GpuVertexBuffer_)
        {
            SDL_ReleaseGPUBuffer(GpuDevice_, GpuVertexBuffer_);
            GpuVertexBuffer_ = nullptr;
        }

        SDL_GPUBufferCreateInfo BufferInfo{};
        BufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        BufferInfo.size = NewCapacity * static_cast<Uint32>(sizeof(GpuVertex));
        BufferInfo.props = 0;
        GpuVertexBuffer_ = SDL_CreateGPUBuffer(GpuDevice_, &BufferInfo);
        if (!GpuVertexBuffer_)
        {
            DBC_LOG_ERROR("SDL_CreateGPUBuffer failed: {}", SDL_GetError());
            return false;
        }

        SDL_GPUTransferBufferCreateInfo TransferInfo{};
        TransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        TransferInfo.size = BufferInfo.size;
        TransferInfo.props = 0;
        GpuTransferBuffer_ = SDL_CreateGPUTransferBuffer(GpuDevice_, &TransferInfo);
        if (!GpuTransferBuffer_)
        {
            DBC_LOG_ERROR("SDL_CreateGPUTransferBuffer failed: {}", SDL_GetError());
            SDL_ReleaseGPUBuffer(GpuDevice_, GpuVertexBuffer_);
            GpuVertexBuffer_ = nullptr;
            return false;
        }

        GpuVertexCapacity_ = NewCapacity;
        return true;
    }

    void GraphicsDeviceSystem::DestroyGpuBackend()
    {
        ReleaseGpuPipelineResources();
        if (WhiteGpuTexture_ && GpuDevice_)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, WhiteGpuTexture_);
            WhiteGpuTexture_ = nullptr;
        }
        if (WhiteCubeGpuTexture_ && GpuDevice_)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, WhiteCubeGpuTexture_);
            WhiteCubeGpuTexture_ = nullptr;
        }
        if (WhiteTexture_)
        {
            SDL_DestroyTexture(WhiteTexture_);
            WhiteTexture_ = nullptr;
        }
        if (Renderer_)
        {
            SDL_DestroyRenderer(Renderer_);
            Renderer_ = nullptr;
        }
        if (GpuDevice_ && Kernel_.Core().Window())
        {
            SDL_ReleaseWindowFromGPUDevice(GpuDevice_, Kernel_.Core().Window());
        }
        if (GpuDevice_)
        {
            SDL_DestroyGPUDevice(GpuDevice_);
            GpuDevice_ = nullptr;
        }
    }

    void GraphicsDeviceSystem::SetDrawColorInt(int Packed)
    {
        const Uint8 R = static_cast<Uint8>((Packed >> 16) & 0xFF);
        const Uint8 G = static_cast<Uint8>((Packed >> 8) & 0xFF);
        const Uint8 B = static_cast<Uint8>(Packed & 0xFF);
        SDL_SetRenderDrawColor(Renderer_, R, G, B, 255);
    }

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    [[nodiscard]] std::vector<Uint8> GraphicsDeviceSystem::LoadFileBytes(const std::string& Path)
    {
        size_t Size = 0;
        void* Data = SDL_LoadFile(Path.c_str(), &Size);
        if (!Data || Size == 0)
        {
            if (Data)
            {
                SDL_free(Data);
            }
            return {};
        }
        std::vector<Uint8> Bytes(Size);
        SDL_memcpy(Bytes.data(), Data, Size);
        SDL_free(Data);
        return Bytes;
    }

    [[nodiscard]] SDL_GPUShaderFormat GraphicsDeviceSystem::NativeShaderFormat() const
    {
        if (GpuDevice_ && (SDL_GetGPUShaderFormats(GpuDevice_) & SDL_GPU_SHADERFORMAT_SPIRV) == 0)
            return SDL_GPU_SHADERFORMAT_MSL;
        return SDL_GPU_SHADERFORMAT_SPIRV;
    }

    [[nodiscard]] SDL_GPUShader* GraphicsDeviceSystem::CreateEngineShader(std::string_view Name,
                                                                          SDL_GPUShaderStage Stage, Uint32 NumSamplers,
                                                                          Uint32 NumUniformBuffers)
    {
        if (!GpuDevice_)
            return nullptr;
        const bool Msl = NativeShaderFormat() == SDL_GPU_SHADERFORMAT_MSL;
        const bool Vertex = Stage == SDL_GPU_SHADERSTAGE_VERTEX;
        const std::string Asset =
            "shaders/" + std::string(Name) + (Vertex ? ".vert" : ".frag") + (Msl ? ".msl" : ".spv");
        std::string Path, Error;
        if (!ImageStore::ResolveAssetPath(Path, Asset, &Error))
        {
            DBC_LOG_ERROR("GPU shader path resolution failed: {}", Error);
            return nullptr;
        }
        EffectStage Definition;
        std::vector<Uint8>& Code = Msl ? Definition.Msl : Definition.Bytecode;
        Code = LoadFileBytes(Path);
        if (Code.empty())
        {
            DBC_LOG_ERROR("shader load failed: {}", Path);
            return nullptr;
        }
        Definition.EntryPoint = Vertex ? "VSMain" : "PSMain";
        Definition.MslEntryPoint = Definition.EntryPoint;
        Definition.SamplerCount = NumSamplers;
        Definition.UniformBufferCount = NumUniformBuffers;
        SDL_GPUShader* Shader = CreateGpuShader(Definition, Stage);
        if (!Shader)
            DBC_LOG_ERROR("GPU shader creation failed for {}: {}", Asset, SDL_GetError());
        return Shader;
    }

    [[nodiscard]] SDL_GPUShader* GraphicsDeviceSystem::CreateGpuShader(const EffectStage& EffectStageValue,
                                                                       SDL_GPUShaderStage Stage)
    {
        if (!GpuDevice_)
            return nullptr;
        const bool Msl = NativeShaderFormat() == SDL_GPU_SHADERFORMAT_MSL;
        if (Msl && (SDL_GetGPUShaderFormats(GpuDevice_) & SDL_GPU_SHADERFORMAT_MSL) == 0)
        {
            SDL_SetError("GPU device accepts neither SPIR-V nor MSL shaders");
            return nullptr;
        }
        const std::vector<Uint8>& Code = Msl ? EffectStageValue.Msl : EffectStageValue.Bytecode;
        if (Code.empty())
            return nullptr;

        SDL_GPUShaderCreateInfo Info{};
        Info.code_size = Code.size();
        Info.code = Code.data();
        Info.entrypoint = Msl ? EffectStageValue.MslEntryPoint.c_str() : EffectStageValue.EntryPoint.c_str();
        Info.format = Msl ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;
        Info.stage = Stage;
        Info.num_samplers = EffectStageValue.SamplerCount;
        Info.num_uniform_buffers = EffectStageValue.UniformBufferCount;
        return SDL_CreateGPUShader(GpuDevice_, &Info);
    }

    void GraphicsDeviceSystem::ReleaseEffectPipelines(EffectResource& Effect)
    {
        // SDL defers pipeline destruction until recorded GPU uses have completed.
        for (auto& [Name, Technique] : Effect.Techniques)
        {
            (void)Name;
            if (!GpuDevice_)
            {
                Technique.Pipelines.fill(nullptr);
                continue;
            }
            for (SDL_GPUGraphicsPipeline*& Pipeline : Technique.Pipelines)
            {
                if (Pipeline)
                {
                    SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, Pipeline);
                    Pipeline = nullptr;
                }
            }
        }
    }

    void GraphicsDeviceSystem::ReleaseGpuPipelineResources()
    {
        for (auto& [EffectId, Effect] : Effects_)
        {
            (void)EffectId;
            ReleaseEffectPipelines(Effect);
        }
        if (GpuCaptureSdlTexture_)
        {
            SDL_DestroyTexture(GpuCaptureSdlTexture_);
            GpuCaptureSdlTexture_ = nullptr;
        }
        if (GpuFinalColorSdlTexture_)
        {
            SDL_DestroyTexture(GpuFinalColorSdlTexture_);
            GpuFinalColorSdlTexture_ = nullptr;
        }
        if (GpuCaptureTexture_)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, GpuCaptureTexture_);
            GpuCaptureTexture_ = nullptr;
        }
        if (GpuFinalColorTexture_)
        {
            SDL_ReleaseGPUTexture(GpuDevice_, GpuFinalColorTexture_);
            GpuFinalColorTexture_ = nullptr;
        }
        GpuCaptureFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        GpuCaptureWidth_ = 0;
        GpuCaptureHeight_ = 0;
        for (int I = 0; I < 2; ++I)
        {
            if (GpuDepthTexture_[I])
            {
                SDL_ReleaseGPUTexture(GpuDevice_, GpuDepthTexture_[I]);
                GpuDepthTexture_[I] = nullptr;
            }
            GpuDepthWidth_[I] = 0;
            GpuDepthHeight_[I] = 0;
        }
        GpuColorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        GpuDepthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        GpuStencilSupported_ = false;
        if (GpuTransferBuffer_)
        {
            SDL_ReleaseGPUTransferBuffer(GpuDevice_, GpuTransferBuffer_);
            GpuTransferBuffer_ = nullptr;
        }
        if (GpuVertexBuffer_)
        {
            SDL_ReleaseGPUBuffer(GpuDevice_, GpuVertexBuffer_);
            GpuVertexBuffer_ = nullptr;
        }
        if (GpuSampler_)
        {
            SDL_ReleaseGPUSampler(GpuDevice_, GpuSampler_);
            GpuSampler_ = nullptr;
        }
        if (GpuSamplerNearest_)
        {
            SDL_ReleaseGPUSampler(GpuDevice_, GpuSamplerNearest_);
            GpuSamplerNearest_ = nullptr;
        }
        for (SDL_GPUSampler*& Sampler : GpuSurfaceSamplers_)
        {
            if (Sampler)
            {
                SDL_ReleaseGPUSampler(GpuDevice_, Sampler);
                Sampler = nullptr;
            }
        }
        for (SDL_GPUGraphicsPipeline** Wire : {&GpuPipelineWireframe_, &GpuPipelineAlphaWriteWireframe_,
                                               &GpuPipelineTransparentWireframe_, &GpuPipelineAdditiveWireframe_})
        {
            if (*Wire)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, *Wire);
                *Wire = nullptr;
            }
        }
        if (GpuReflectionMaskPipeline_)
        {
            SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, GpuReflectionMaskPipeline_);
            GpuReflectionMaskPipeline_ = nullptr;
        }
        for (SDL_GPUGraphicsPipeline** Pipeline :
             {&GpuShadowDepthPipeline_, &GpuShadowVolumePipeline_, &GpuShadowOverlayPipeline_})
        {
            if (*Pipeline)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, *Pipeline);
                *Pipeline = nullptr;
            }
        }
        for (SDL_GPUGraphicsPipeline*& Pipeline : GpuSurfaceGhostPipelines_)
        {
            if (Pipeline)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, Pipeline);
                Pipeline = nullptr;
            }
        }
        for (SDL_GPUGraphicsPipeline*& Pipeline : GpuSurfaceDepthPipelines_)
        {
            if (Pipeline)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, Pipeline);
                Pipeline = nullptr;
            }
        }
        for (SDL_GPUGraphicsPipeline*& Pipeline : GpuSurfaceOpaqueNoDepthPipelines_)
        {
            if (Pipeline)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, Pipeline);
                Pipeline = nullptr;
            }
        }
        for (SDL_GPUGraphicsPipeline*& Pipeline : GpuTerrainPipelines_)
        {
            if (Pipeline)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, Pipeline);
                Pipeline = nullptr;
            }
        }
        for (SDL_GPUGraphicsPipeline*& Pipeline : GpuMeshPipelines_)
        {
            if (Pipeline)
            {
                SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, Pipeline);
                Pipeline = nullptr;
            }
        }
        if (GpuPipeline_)
        {
            SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, GpuPipeline_);
            GpuPipeline_ = nullptr;
        }
        if (GpuPipelineAlphaWrite_)
        {
            SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, GpuPipelineAlphaWrite_);
            GpuPipelineAlphaWrite_ = nullptr;
        }
        if (GpuPipelineTransparent_)
        {
            SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, GpuPipelineTransparent_);
            GpuPipelineTransparent_ = nullptr;
        }
        if (GpuPipelineAdditive_)
        {
            SDL_ReleaseGPUGraphicsPipeline(GpuDevice_, GpuPipelineAdditive_);
            GpuPipelineAdditive_ = nullptr;
        }
        GpuVertexCapacity_ = 0;
    }

    // -----------------------------------------------------------------------
    // Mutable accessors
    // -----------------------------------------------------------------------

    SDL_Renderer*& GraphicsDeviceSystem::Renderer()
    {
        return Renderer_;
    }

    SDL_GPUDevice*& GraphicsDeviceSystem::GpuDevice()
    {
        return GpuDevice_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipeline()
    {
        return GpuPipeline_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineAlphaWrite()
    {
        return GpuPipelineAlphaWrite_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineTransparent()
    {
        return GpuPipelineTransparent_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineAdditive()
    {
        return GpuPipelineAdditive_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineWireframe()
    {
        return GpuPipelineWireframe_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineAlphaWriteWireframe()
    {
        return GpuPipelineAlphaWriteWireframe_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineTransparentWireframe()
    {
        return GpuPipelineTransparentWireframe_;
    }

    SDL_GPUGraphicsPipeline*& GraphicsDeviceSystem::GpuPipelineAdditiveWireframe()
    {
        return GpuPipelineAdditiveWireframe_;
    }

    SDL_GPUSampler*& GraphicsDeviceSystem::GpuSampler()
    {
        return GpuSampler_;
    }

    SDL_GPUSampler*& GraphicsDeviceSystem::GpuSamplerNearest()
    {
        return GpuSamplerNearest_;
    }

    SDL_GPUGraphicsPipeline* GraphicsDeviceSystem::SurfaceGhostPipeline(int Mode, bool Wireframe, bool DepthWrite,
                                                                        bool DepthTest, bool StencilTest) const
    {
        if (Mode < 0 || Mode > 4)
            return nullptr;
        const size_t Index = static_cast<size_t>(Mode * 8 + (DepthTest ? 4 : 0) + (DepthWrite ? 2 : 0) +
                                                 (Wireframe ? 1 : 0) + (StencilTest ? 40 : 0));
        return GpuSurfaceGhostPipelines_[Index];
    }

    SDL_GPUGraphicsPipeline* GraphicsDeviceSystem::SurfaceOpaquePipeline(bool Wireframe, bool DepthWrite) const
    {
        if (DepthWrite)
            return Wireframe ? GpuPipelineWireframe_ : GpuPipeline_;
        return GpuSurfaceOpaqueNoDepthPipelines_[Wireframe ? 1u : 0u];
    }

    SDL_GPUGraphicsPipeline* GraphicsDeviceSystem::SurfacePipeline(bool Transparent, bool AlphaDepthWrite,
                                                                   bool Additive, bool Wireframe, bool DepthTest,
                                                                   bool DepthWrite, bool StencilTest) const
    {
        const int Blend = Additive ? 2 : ((Transparent || AlphaDepthWrite) ? 1 : 0);
        const size_t Index = static_cast<size_t>(Blend + (Wireframe ? 3 : 0) + (DepthWrite ? 6 : 0) +
                                                 (DepthTest ? 12 : 0) + (StencilTest ? 24 : 0));
        return GpuSurfaceDepthPipelines_[Index];
    }

    SDL_GPUSampler* GraphicsDeviceSystem::SurfaceSampler(int AddressMode, bool Filter, int MipMode) const
    {
        const int Address = std::clamp(AddressMode, 0, 2);
        const int FilterIndex = Filter ? 1 : 0;
        const int Mip = MipMode < 0 ? 0 : (MipMode == 1 ? 1 : 2);
        return GpuSurfaceSamplers_[static_cast<size_t>(Address * 6 + FilterIndex * 3 + Mip)];
    }

    SDL_GPUBuffer*& GraphicsDeviceSystem::GpuVertexBuffer()
    {
        return GpuVertexBuffer_;
    }

    SDL_GPUTransferBuffer*& GraphicsDeviceSystem::GpuTransferBuffer()
    {
        return GpuTransferBuffer_;
    }

    SDL_GPUTexture*& GraphicsDeviceSystem::GpuDepthTexture(int Index)
    {
        return GpuDepthTexture_[Index];
    }

    SDL_GPUTextureFormat& GraphicsDeviceSystem::GpuDepthFormat()
    {
        return GpuDepthFormat_;
    }

    Uint32& GraphicsDeviceSystem::GpuDepthWidth(int Index)
    {
        return GpuDepthWidth_[Index];
    }

    Uint32& GraphicsDeviceSystem::GpuDepthHeight(int Index)
    {
        return GpuDepthHeight_[Index];
    }

    Uint32& GraphicsDeviceSystem::GpuVertexCapacity()
    {
        return GpuVertexCapacity_;
    }

    SDL_Texture*& GraphicsDeviceSystem::WhiteTexture()
    {
        return WhiteTexture_;
    }

    SDL_GPUTexture*& GraphicsDeviceSystem::WhiteGpuTexture()
    {
        return WhiteGpuTexture_;
    }

    SDL_GPUTexture*& GraphicsDeviceSystem::WhiteCubeGpuTexture()
    {
        return WhiteCubeGpuTexture_;
    }

} // namespace dbc
