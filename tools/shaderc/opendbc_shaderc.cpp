// Compiles one engine shader source to the canonical files the engine loads:
// <stem>.vert.spv, <stem>.frag.spv, <stem>.vert.msl and <stem>.frag.msl. The MSL comes
// from the same SDL_shadercross transpiler as runtime effects, so its bindings follow
// SDL_GPU's Metal layout.

#include "path_utf8.hpp"
#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    struct SdlBuffer
    {
        void* Value = nullptr;
        ~SdlBuffer() { SDL_free(Value); }
    };

    bool ReadText(const std::filesystem::path& Path, std::string& Text)
    {
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream)
            return false;
        Text.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
        return !Stream.bad();
    }

    bool WriteBytes(const std::filesystem::path& Path, const void* Bytes, size_t Size)
    {
        std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
        Stream.write(static_cast<const char*>(Bytes), static_cast<std::streamsize>(Size));
        Stream.close();
        if (!Stream)
        {
            std::fprintf(stderr, "opendbc_shaderc: cannot write %s\n", dbc::PathToUtf8(Path).c_str());
            return false;
        }
        return true;
    }

    bool CompileStage(const std::string& Source, const std::string& IncludeDirectory, const std::string& OutputStem,
                      const char* EntryPoint, SDL_ShaderCross_ShaderStage Stage, const char* StageName)
    {
        SDL_ShaderCross_HLSL_Info Hlsl{};
        Hlsl.source = Source.c_str();
        Hlsl.entrypoint = EntryPoint;
        Hlsl.include_dir = IncludeDirectory.c_str();
        Hlsl.shader_stage = Stage;

        size_t SpirvSize = 0;
        SdlBuffer Spirv;
        Spirv.Value = SDL_ShaderCross_CompileSPIRVFromHLSL(&Hlsl, &SpirvSize);
        if (!Spirv.Value)
        {
            std::fprintf(stderr, "opendbc_shaderc: %s: %s\n", EntryPoint, SDL_GetError());
            return false;
        }

        SDL_ShaderCross_SPIRV_Info SpirvInfo{};
        SpirvInfo.bytecode = static_cast<const Uint8*>(Spirv.Value);
        SpirvInfo.bytecode_size = SpirvSize;
        SpirvInfo.entrypoint = EntryPoint;
        SpirvInfo.shader_stage = Stage;
        SdlBuffer Msl;
        Msl.Value = SDL_ShaderCross_TranspileMSLFromSPIRV(&SpirvInfo);
        if (!Msl.Value)
        {
            std::fprintf(stderr, "opendbc_shaderc: %s: MSL transpilation failed: %s\n", EntryPoint, SDL_GetError());
            return false;
        }

        const std::string Stem = OutputStem + "." + StageName;
        return WriteBytes(Stem + ".spv", Spirv.Value, SpirvSize) &&
               WriteBytes(Stem + ".msl", Msl.Value, SDL_strlen(static_cast<const char*>(Msl.Value)));
    }
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount != 3)
    {
        std::fprintf(stderr, "usage: opendbc_shaderc <source.hlsl> <output-stem>\n");
        return 2;
    }
    const std::filesystem::path SourcePath = Arguments[1];
    std::string Source;
    if (!ReadText(SourcePath, Source))
    {
        std::fprintf(stderr, "opendbc_shaderc: cannot read %s\n", dbc::PathToUtf8(SourcePath).c_str());
        return 1;
    }
    if (!SDL_ShaderCross_Init())
    {
        std::fprintf(stderr, "opendbc_shaderc: SDL_shadercross initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    const std::string IncludeDirectory = dbc::PathToUtf8(SourcePath.parent_path());
    const bool Compiled =
        CompileStage(Source, IncludeDirectory, Arguments[2], "VSMain", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "vert") &&
        CompileStage(Source, IncludeDirectory, Arguments[2], "PSMain", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "frag");
    SDL_ShaderCross_Quit();
    return Compiled ? 0 : 1;
}
