#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

#include "engine/systems/camera_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/graphics_device_system.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/renderer_system.hpp"
#include "interpreter/engine_builtin_registry.hpp"
#include "toolchain/shader_compiler.hpp"

using namespace dbc::studio;

namespace
{
    template <typename T, typename Reader> T ReadEngineNumber(Reader&& Read)
    {
        T Output{};
        std::string Error;
        REQUIRE_MESSAGE(Read(Error, Output), Error);
        return Output;
    }

    class TemporaryDirectory
    {
      public:
        TemporaryDirectory()
        {
            const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            Path = std::filesystem::temp_directory_path() / ("opendbc-shader-test-" + std::to_string(Suffix));
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code Error;
            std::filesystem::remove_all(Path, Error);
        }

        std::filesystem::path Path;
    };

    std::uint32_t ReadSpirvMagic(const std::filesystem::path& Path)
    {
        std::uint32_t Magic = 0;
        std::ifstream Stream(Path, std::ios::binary);
        Stream.read(reinterpret_cast<char*>(&Magic), sizeof(Magic));
        return Magic;
    }
} // namespace

TEST_CASE("Studio compiles a relocatable HLSL effect package")
{
    std::string EffectError;
    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "tint.fx";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << R"(
struct VertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(3)]] float2 Uv : TEXCOORD0;
};

struct PixelInput
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

cbuffer VertexConstants : register(b0, space1)
{
    float4x4 ViewProjection;
};

Texture2D<float4> ColorTexture : register(t0, space2);
SamplerState ColorSampler : register(s0, space2);

PixelInput VSMain(VertexInput Input)
{
    PixelInput Output;
    Output.Position = mul(ViewProjection, float4(Input.Position, 1.0));
    Output.Uv = Input.Uv;
    return Output;
}

float4 PSMain(PixelInput Input) : SV_Target0
{
    return ColorTexture.Sample(ColorSampler, Input.Uv);
}
)";
    Stream.close();
    REQUIRE(Stream.good());

    toolchain::ShaderCompiler Compiler;
    const toolchain::ShaderCompileResult Result = Compiler.CompileHlslEffect(Source);
    INFO(Result.Error);
    REQUIRE(Result.Success);
    CHECK(Result.ManifestPath == Directory.Path / "tint.effect.json");
    CHECK(Result.VertexPath == Directory.Path / "tint.vert.spv");
    CHECK(Result.FragmentPath == Directory.Path / "tint.frag.spv");
    CHECK(ReadSpirvMagic(Result.VertexPath) == 0x07230203U);
    CHECK(ReadSpirvMagic(Result.FragmentPath) == 0x07230203U);

    std::ifstream ManifestStream(Result.ManifestPath, std::ios::binary);
    const nlohmann::json Manifest = nlohmann::json::parse(ManifestStream);
    INFO(Manifest.dump(2));
    CHECK(Manifest.at("format") == "opendbc-effect-v1");
    CHECK(Manifest.at("source") == "tint.fx");
    CHECK(Manifest.at("source_language") == "hlsl");
    CHECK(Manifest.at("stages").at("vertex").at("entry_point") == "VSMain");
    CHECK(Manifest.at("stages").at("fragment").at("entry_point") == "PSMain");
    CHECK(Manifest.at("stages").at("vertex").at("resources").at("uniform_buffers") == 1);
    CHECK(Manifest.at("stages").at("fragment").at("resources").at("samplers") == 1);
    CHECK(Manifest.at("stages").at("vertex").at("msl_file") == "tint.vert.msl");
    CHECK(Manifest.at("stages").at("fragment").at("msl_file") == "tint.frag.msl");
    CHECK(Manifest.at("stages").at("vertex").at("msl_entry_point") == "VSMain");
    CHECK(Manifest.at("stages").at("fragment").at("msl_entry_point") == "PSMain");
    CHECK(std::filesystem::file_size(Directory.Path / "tint.vert.msl") > 0);
    CHECK(std::filesystem::file_size(Directory.Path / "tint.frag.msl") > 0);

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    REQUIRE(Kernel.Gpu().LoadEffect(Source.string(), 7, 0, 1, &EffectError));
    CHECK(Kernel.Gpu().EffectExist(7) == 1);
    CHECK(Kernel.Gpu().EffectUsesObjectTexture(7));
    CHECK(Kernel.Gpu().EffectDoesNotGenerateExtraData(7));
    CHECK(Kernel.Gpu().EffectFragmentSamplerCount(7) == 1);
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    REQUIRE(Kernel.Objects().SetObjectEffect(1, 7));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().ObjectEffect(Output, 1, &Error); }) == 7);
    REQUIRE(Kernel.Objects().SetLimbEffect(1, 0, 7));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().LimbEffect(Output, 1, 0, &Error); }) == 7);
    Kernel.Objects().MakeObjectCube(2, 2.0f);
    REQUIRE(Kernel.Gpu().SetEffectOn(2, Source.string(), 1, &EffectError));
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().ObjectEffect(Output, 2, &Error); }) == -2);
    CHECK(Kernel.Gpu().HasEffect(-2));
    CHECK_FALSE(Kernel.Gpu().EffectUsesObjectTexture(-2));

    Kernel.Camera().SetAutoCamOff();
    (
        [&]
        {
            std::string CameraError;
            const bool Success = Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &CameraError);
            CHECK_MESSAGE(Success, CameraError);
            return Success;
        }());
    (
        [&]
        {
            std::string CameraError;
            const bool Success = Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &CameraError);
            CHECK_MESSAGE(Success, CameraError);
            return Success;
        }());
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    Kernel.Objects().PositionObject(2, 0.0f, 0.0f, 5.0f);
    Kernel.Objects().HideObject(2);
    REQUIRE(Kernel.Core().EnsureInitialized());
    REQUIRE(Kernel.Gpu().EnsureStencilPipelines());
    CHECK(Kernel.Gpu().ShadowDepthPipeline() != nullptr);
    CHECK(Kernel.Gpu().ShadowVolumePipeline() != nullptr);
    CHECK(Kernel.Gpu().ShadowOverlayPipeline() != nullptr);
    CHECK(Kernel.Gpu().EffectPipeline(7, false, false, false, false, true, true, true) != nullptr);
    SDL_Surface* BlueTexture = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(BlueTexture != nullptr);
    REQUIRE(SDL_FillSurfaceRect(BlueTexture, nullptr, SDL_MapSurfaceRGBA(BlueTexture, 0, 0, 255, 255)));
    Kernel.ImageStoreRef().StoreImageFromSurface(1, BlueTexture);
    SDL_DestroySurface(BlueTexture);
    Kernel.Objects().TextureObject(1, 1);
    Kernel.Objects().TextureObject(2, 1);

    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    Uint8 Red = 0;
    Uint8 Green = 0;
    Uint8 Blue = 0;
    Uint8 Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red <= 16);
    CHECK(Green <= 16);
    CHECK(Blue >= 240);

    Kernel.Objects().HideObject(1);
    Kernel.Objects().ShowObject(2);
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red >= 240);
    CHECK(Green >= 240);
    CHECK(Blue >= 240);

    Kernel.Objects().DeleteObject(2);
    CHECK_FALSE(Kernel.Gpu().HasEffect(-2));
    REQUIRE(Kernel.Gpu().DeleteEffect(7, &EffectError));
    CHECK(Kernel.Gpu().EffectExist(7) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().ObjectEffect(Output, 1, &Error); }) == 0);
    CHECK(ReadEngineNumber<int>([&](std::string& Error, int& Output)
                                { return Kernel.Objects().LimbEffect(Output, 1, 0, &Error); }) == -1);

    REQUIRE(Kernel.Gpu().LoadEffect(Source.string(), 7, 0, 0, &EffectError));
    for (const nlohmann::json& Invalid :
         {nlohmann::json(1.5), nlohmann::json(UINT64_MAX), nlohmann::json(true), nlohmann::json("1")})
    {
        auto Malformed = Manifest;
        Malformed["stages"]["vertex"]["resources"]["uniform_buffers"] = Invalid;
        std::ofstream ManifestFile(Result.ManifestPath, std::ios::binary | std::ios::trunc);
        ManifestFile << Malformed.dump();
        ManifestFile.close();
        CHECK_FALSE(Kernel.Gpu().LoadEffect(Source.string(), 7, 0, 0, &EffectError));
        CHECK(EffectError.find("invalid integer field uniform_buffers") != std::string::npos);
        CHECK(Kernel.Gpu().EffectExist(7) == 1);
        REQUIRE(Kernel.Gpu().EffectErrors().size() == 1);
        CHECK(Kernel.Gpu().EffectErrors().front() == EffectError);
    }
    {
        auto WithoutMsl = Manifest;
        WithoutMsl["stages"]["fragment"].erase("msl_file");
        std::ofstream ManifestFile(Result.ManifestPath, std::ios::binary | std::ios::trunc);
        ManifestFile << WithoutMsl.dump();
        ManifestFile.close();
        CHECK_FALSE(Kernel.Gpu().LoadEffect(Source.string(), 9, 0, 0, &EffectError));
        CHECK(Kernel.Gpu().EffectExist(9) == 0);
    }
    {
        auto EscapingMsl = Manifest;
        EscapingMsl["stages"]["vertex"]["msl_file"] = "../tint.vert.msl";
        std::ofstream ManifestFile(Result.ManifestPath, std::ios::binary | std::ios::trunc);
        ManifestFile << EscapingMsl.dump();
        ManifestFile.close();
        CHECK_FALSE(Kernel.Gpu().LoadEffect(Source.string(), 9, 0, 0, &EffectError));
        CHECK(EffectError.find("sibling filename") != std::string::npos);
    }
    TemporaryDirectory Outside;
    const auto ExternalStage = Outside.Path / "external.vert.spv";
    REQUIRE(std::filesystem::copy_file(Result.VertexPath, ExternalStage));
    std::filesystem::create_symlink(ExternalStage, Directory.Path / "escape.vert.spv");
    auto Escaping = Manifest;
    Escaping["stages"]["vertex"]["file"] = "escape.vert.spv";
    std::ofstream ManifestFile(Result.ManifestPath, std::ios::binary | std::ios::trunc);
    ManifestFile << Escaping.dump();
    ManifestFile.close();
    CHECK_FALSE(Kernel.Gpu().LoadEffect(Source.string(), 8, 0, 0, &EffectError));
    CHECK(EffectError.find("must remain inside") != std::string::npos);
    CHECK(Kernel.Gpu().EffectExist(8) == 0);

    SDL_Surface Oversized{};
    Oversized.format = SDL_PIXELFORMAT_RGBA32;
    Oversized.w = std::numeric_limits<int>::max();
    Oversized.h = std::numeric_limits<int>::max();
    CHECK(Kernel.Gpu().CreateGpuTextureFromSurface(&Oversized) == nullptr);
    CHECK(std::string(SDL_GetError()).find("GPU transfer limit") != std::string::npos);
    const std::array<SDL_Surface*, 6> Faces{&Oversized, &Oversized, &Oversized, &Oversized, &Oversized, &Oversized};
    CHECK(Kernel.Gpu().CreateGpuCubeTextureFromSurfaces(Faces) == nullptr);
    CHECK(std::string(SDL_GetError()).find("GPU transfer limit") != std::string::npos);
}

TEST_CASE("Studio records the MSL entry point SPIRV-Cross renames")
{
    std::string EffectError;
    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "renamed.fx";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << R"(
// opendbc:technique plain main PSMain

float4 VSMain(float3 Position : POSITION) : SV_Position { return float4(Position, 1.0); }
float4 main(float3 Position : POSITION) : SV_Position { return float4(Position * 0.5, 1.0); }
float4 PSMain() : SV_Target0 { return float4(1.0, 1.0, 1.0, 1.0); }
)";
    Stream.close();
    REQUIRE(Stream.good());

    const toolchain::ShaderCompileResult Result = toolchain::ShaderCompiler().CompileHlslEffect(Source);
    INFO(Result.Error);
    REQUIRE(Result.Success);
    std::ifstream ManifestStream(Result.ManifestPath, std::ios::binary);
    const nlohmann::json Manifest = nlohmann::json::parse(ManifestStream);
    const nlohmann::json& Vertex = Manifest.at("techniques").at(0).at("stages").at("vertex");
    CHECK(Vertex.at("entry_point") == "main");
    CHECK(Vertex.at("msl_entry_point") == "main0");
    CHECK(Vertex.at("msl_file") == "renamed.plain.vert.msl");
    CHECK(std::filesystem::exists(Directory.Path / "renamed.plain.vert.msl"));

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    CHECK_MESSAGE(Kernel.Gpu().LoadEffect(Source.string(), 1, 0, 0, &EffectError), EffectError);
}

TEST_CASE("Compiled HLSL effects replace the portable object pipeline at runtime")
{
    std::string EffectError;
    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "solid-red.fx";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << R"(
struct VertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
};

struct PixelInput
{
    float4 Position : SV_Position;
};

cbuffer VertexConstants : register(b0, space1)
{
    float4x4 ViewProjection;
};

PixelInput VSMain(VertexInput Input)
{
    PixelInput Output;
    Output.Position = mul(ViewProjection, float4(Input.Position, 1.0));
    return Output;
}

float4 PSMain(PixelInput Input) : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)";
    Stream.close();
    REQUIRE(Stream.good());

    const toolchain::ShaderCompileResult Compiled = toolchain::ShaderCompiler().CompileHlslEffect(Source);
    INFO(Compiled.Error);
    REQUIRE(Compiled.Success);

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    REQUIRE(Kernel.Gpu().LoadEffect(Source.string(), 1, 0, 0, &EffectError));
    Kernel.Camera().SetAutoCamOff();
    (
        [&]
        {
            std::string CameraError;
            const bool Success = Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &CameraError);
            CHECK_MESSAGE(Success, CameraError);
            return Success;
        }());
    (
        [&]
        {
            std::string CameraError;
            const bool Success = Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &CameraError);
            CHECK_MESSAGE(Success, CameraError);
            return Success;
        }());
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectEffect(1, 1));
    REQUIRE(Kernel.Core().EnsureInitialized());

    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    Uint8 Red = 0;
    Uint8 Green = 0;
    Uint8 Blue = 0;
    Uint8 Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red >= 240);
    CHECK(Green <= 16);
    CHECK(Blue <= 16);
    CHECK(Alpha >= 240);
}

TEST_CASE("DBPro effect constants techniques transpose and checklists drive compiled HLSL")
{
    std::string EffectError;
    using dbc::interpreter::detail::BuiltinContext;
    using dbc::interpreter::detail::BuiltinValue;
    using dbc::interpreter::detail::InvokeCommand;

    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "controlled.fx";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << R"(
// opendbc:technique blue VSBlue PSBlue

struct VertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
};

struct PixelInput
{
    float4 Position : SV_Position;
};

cbuffer VertexConstants : register(b0, space1)
{
    float4x4 ViewProjection;
    float4x4 UserMatrix;
    float4 Offset;
};

cbuffer FragmentConstants : register(b0, space3)
{
    float4 Tint;
    bool Enabled;
    int Count;
    float Strength;
};

PixelInput Transform(VertexInput Input)
{
    PixelInput Output;
    float4 Position = mul(UserMatrix, float4(Input.Position + Offset.xyz, 1.0));
    Output.Position = mul(ViewProjection, Position);
    return Output;
}

PixelInput VSMain(VertexInput Input)
{
    return Transform(Input);
}

float4 PSMain(PixelInput Input) : SV_Target0
{
    return Enabled && Count > 0 ? Tint * Strength : float4(0.0, 0.0, 0.0, 1.0);
}

PixelInput VSBlue(VertexInput Input)
{
    return Transform(Input);
}

float4 PSBlue(PixelInput Input) : SV_Target0
{
    return float4(0.0, 0.0, 1.0, 1.0);
}
)";
    Stream.close();
    REQUIRE(Stream.good());

    const toolchain::ShaderCompileResult Compiled = toolchain::ShaderCompiler().CompileHlslEffect(Source);
    INFO(Compiled.Error);
    REQUIRE(Compiled.Success);
    std::ifstream ControlledManifestStream(Compiled.ManifestPath, std::ios::binary);
    const nlohmann::json ControlledManifest = nlohmann::json::parse(ControlledManifestStream);
    INFO(ControlledManifest.dump(2));
    CHECK(ReadSpirvMagic(Directory.Path / "controlled.blue.vert.spv") == 0x07230203U);
    CHECK(ReadSpirvMagic(Directory.Path / "controlled.blue.frag.spv") == 0x07230203U);

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("test", 64, 48));
    REQUIRE(Kernel.Gpu().LoadEffect(Source.string(), 1, 0, 0, &EffectError));
    CHECK(Kernel.Gpu().EffectTechniqueName(1) == "default");

    BuiltinContext Context;
    Context.Kernel = &Kernel;
    Context.Profile = dbc::language::LanguageProfile::DbPro;
    Context.Vectors4[1] = {1.0f, 0.0f, 0.0f, 1.0f};
    Context.Vectors4[2] = {0.0f, 0.0f, 0.0f, 0.0f};
    Context.Matrices4[1] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::string Error;
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_SetEffectConstantBoolean,
        {BuiltinValue::FromNumber(1), BuiltinValue::FromString("Enabled"), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetEffectConstantInteger,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromString("Count"), BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_SetEffectConstantFloat,
        {BuiltinValue::FromNumber(1), BuiltinValue::FromString("Strength"), BuiltinValue::FromNumber(1)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetEffectConstantVector,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromString("Tint"), BuiltinValue::FromNumber(1)},
                          Error));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_SetEffectConstantVector,
        {BuiltinValue::FromNumber(1), BuiltinValue::FromString("Offset"), BuiltinValue::FromNumber(2)}, Error));
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetEffectTranspose,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromNumber(1)}, Error));
    CHECK(Kernel.Gpu().EffectTransposeEnabled(1));
    REQUIRE(InvokeCommand(
        Context, dbc::language::SymbolId::Command_SetEffectConstantMatrix,
        {BuiltinValue::FromNumber(1), BuiltinValue::FromString("UserMatrix"), BuiltinValue::FromNumber(1)}, Error));

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForEffectValues,
                          {BuiltinValue::FromNumber(1)}, Error));
    const auto Parameter = std::find_if(Context.Checklist.begin(), Context.Checklist.end(),
                                        [](const auto& Item) { return Item.Name == "Tint"; });
    REQUIRE(Parameter != Context.Checklist.end());
    CHECK(Parameter->A == 4);
    CHECK(Parameter->B == 0);
    const auto Automatic = std::find_if(Context.Checklist.begin(), Context.Checklist.end(),
                                        [](const auto& Item) { return Item.Name == "ViewProjection"; });
    REQUIRE(Automatic != Context.Checklist.end());
    CHECK(Automatic->A == 5);
    CHECK(Automatic->B == 1);

    Kernel.Camera().SetAutoCamOff();
    (
        [&]
        {
            std::string CameraError;
            const bool Success = Kernel.Camera().PositionCamera(0.0f, 0.0f, 0.0f, &CameraError);
            CHECK_MESSAGE(Success, CameraError);
            return Success;
        }());
    (
        [&]
        {
            std::string CameraError;
            const bool Success = Kernel.Camera().RotateCamera(0.0f, 0.0f, 0.0f, &CameraError);
            CHECK_MESSAGE(Success, CameraError);
            return Success;
        }());
    Kernel.Objects().MakeObjectCube(1, 2.0f);
    Kernel.Objects().PositionObject(1, 0.0f, 0.0f, 5.0f);
    REQUIRE(Kernel.Objects().SetObjectEffect(1, 1));
    REQUIRE(Kernel.Core().EnsureInitialized());

    SDL_Surface* Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    Uint8 Red = 0;
    Uint8 Green = 0;
    Uint8 Blue = 0;
    Uint8 Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red >= 240);
    CHECK(Green <= 16);
    CHECK(Blue <= 16);

    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_SetEffectTechnique,
                          {BuiltinValue::FromNumber(1), BuiltinValue::FromString("blue")}, Error));
    CHECK(Kernel.Gpu().EffectTechniqueName(1) == "blue");
    Capture = Kernel.Renderer().CaptureScreen();
    REQUIRE(Capture != nullptr);
    REQUIRE(SDL_ReadSurfacePixel(Capture, Capture->w / 2, Capture->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Capture);
    CHECK(Red <= 16);
    CHECK(Green <= 16);
    CHECK(Blue >= 240);

    CHECK_FALSE(Kernel.Gpu().LoadEffect((Directory.Path / "missing.fx").string(), 2, 0, 0, &EffectError));
    CHECK_FALSE(EffectError.empty());
    Error.clear();
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_LoadEffect,
                          {BuiltinValue::FromString((Directory.Path / "missing.fx").string()),
                           BuiltinValue::FromNumber(2), BuiltinValue::FromNumber(0)},
                          Error));
    CHECK(Error.empty());
    CHECK(Kernel.Gpu().EffectExist(2) == 0);
    REQUIRE(InvokeCommand(Context, dbc::language::SymbolId::Command_PerformChecklistForEffectErrors, {}, Error));
    REQUIRE(Context.Checklist.size() == 1);
    CHECK(Context.Checklist.front().Name.find("missing.effect.json") != std::string::npos);
}

TEST_CASE("Studio rejects an HLSL effect without both fixed entry points")
{
    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "incomplete.hlsl";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << "float4 VSMain(float4 Position : TEXCOORD0) : SV_Position { return Position; }\n";
    Stream.close();

    const toolchain::ShaderCompileResult Result = toolchain::ShaderCompiler().CompileHlslEffect(Source);
    CHECK_FALSE(Result.Success);
    CHECK(Result.Error.find("PSMain") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(Directory.Path / "incomplete.vert.spv"));
    CHECK_FALSE(std::filesystem::exists(Directory.Path / "incomplete.frag.spv"));
    CHECK_FALSE(std::filesystem::exists(Directory.Path / "incomplete.effect.json"));
}

TEST_CASE("Studio rejects legacy Effect Framework techniques with a conversion diagnostic")
{
    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "legacy-framework.fx";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << R"(
float4 VS(float4 Position : POSITION) : POSITION { return Position; }
float4 PS() : COLOR { return 1.0; }
technique T
{
    pass P
    {
        VertexShader = compile vs_2_0 VS();
        PixelShader = compile ps_2_0 PS();
    }
}
)";
    Stream.close();

    const toolchain::ShaderCompileResult Result = toolchain::ShaderCompiler().CompileHlslEffect(Source);
    CHECK_FALSE(Result.Success);
    CHECK(Result.Error.find("legacy DirectX Effect Framework") != std::string::npos);
    CHECK(Result.Error.find("VSMain/PSMain") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(Directory.Path / "legacy-framework.effect.json"));
}

TEST_CASE("Legacy DirectX shader assembly fails with a dedicated effect diagnostic")
{
    std::string EffectError;
    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Path / "legacy.fx";
    std::ofstream Stream(Source, std::ios::binary);
    Stream << "technique T { pass P { VertexShader = asm { vs.1.1 mov oPos, v0 }; } }\n";
    Stream.close();

    const toolchain::ShaderCompileResult CompileResult = toolchain::ShaderCompiler().CompileHlslEffect(Source);
    CHECK_FALSE(CompileResult.Success);
    CHECK(CompileResult.Error.find("legacy DirectX shader assembly") != std::string::npos);

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("legacy-effect", 16, 16));
    CHECK_FALSE(Kernel.Gpu().LoadEffect(Source.string(), 1, 0, 0, &EffectError));
    CHECK(EffectError.find("does not support legacy DirectX shader assembly") != std::string::npos);
}
