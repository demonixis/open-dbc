#include "shader_compiler.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <nlohmann/json.hpp>
#include <spirv_cross.hpp>

namespace dbc::studio
{
    namespace
    {
        struct SdlBuffer
        {
            void* Value = nullptr;

            ~SdlBuffer()
            {
                SDL_free(Value);
            }

            SdlBuffer(const SdlBuffer&) = delete;
            SdlBuffer& operator=(const SdlBuffer&) = delete;
            SdlBuffer() = default;
        };

        struct SdlMetadata
        {
            SDL_ShaderCross_GraphicsShaderMetadata* Value = nullptr;

            ~SdlMetadata()
            {
                SDL_free(Value);
            }

            SdlMetadata(const SdlMetadata&) = delete;
            SdlMetadata& operator=(const SdlMetadata&) = delete;
            SdlMetadata() = default;
        };

        struct ShaderCrossScope
        {
            bool Initialized = false;

            ~ShaderCrossScope()
            {
                if (Initialized)
                    SDL_ShaderCross_Quit();
            }
        };

        struct CompiledStage
        {
            std::vector<std::uint8_t> Bytes;
            std::string Msl;
            nlohmann::json Metadata;
        };

        struct TechniqueDefinition
        {
            std::string Name;
            std::string VertexEntryPoint;
            std::string FragmentEntryPoint;
        };

        struct CompiledTechnique
        {
            TechniqueDefinition Definition;
            CompiledStage Vertex;
            CompiledStage Fragment;
        };

        std::string IoTypeName(SDL_ShaderCross_IOVarType Type)
        {
            switch (Type)
            {
                case SDL_SHADERCROSS_IOVAR_TYPE_INT8:
                    return "int8";
                case SDL_SHADERCROSS_IOVAR_TYPE_UINT8:
                    return "uint8";
                case SDL_SHADERCROSS_IOVAR_TYPE_INT16:
                    return "int16";
                case SDL_SHADERCROSS_IOVAR_TYPE_UINT16:
                    return "uint16";
                case SDL_SHADERCROSS_IOVAR_TYPE_INT32:
                    return "int32";
                case SDL_SHADERCROSS_IOVAR_TYPE_UINT32:
                    return "uint32";
                case SDL_SHADERCROSS_IOVAR_TYPE_INT64:
                    return "int64";
                case SDL_SHADERCROSS_IOVAR_TYPE_UINT64:
                    return "uint64";
                case SDL_SHADERCROSS_IOVAR_TYPE_FLOAT16:
                    return "float16";
                case SDL_SHADERCROSS_IOVAR_TYPE_FLOAT32:
                    return "float32";
                case SDL_SHADERCROSS_IOVAR_TYPE_FLOAT64:
                    return "float64";
                case SDL_SHADERCROSS_IOVAR_TYPE_UNKNOWN:
                    return "unknown";
            }
            return "unknown";
        }

        nlohmann::json IoVariables(const SDL_ShaderCross_IOVarMetadata* Variables, Uint32 Count)
        {
            nlohmann::json Result = nlohmann::json::array();
            for (Uint32 Index = 0; Index < Count; ++Index)
            {
                const SDL_ShaderCross_IOVarMetadata& Variable = Variables[Index];
                Result.push_back({{"name", Variable.name ? Variable.name : ""},
                                  {"location", Variable.location},
                                  {"type", IoTypeName(Variable.vector_type)},
                                  {"components", Variable.vector_size}});
            }
            return Result;
        }

        std::string ParameterKind(const spirv_cross::SPIRType& Type)
        {
            if (!Type.array.empty())
                return "opaque";
            if (Type.columns == 4 && Type.vecsize == 4 && Type.basetype == spirv_cross::SPIRType::Float)
                return "matrix";
            if (Type.columns != 1)
                return "opaque";
            if (Type.vecsize == 4 && Type.basetype == spirv_cross::SPIRType::Float)
                return "vector";
            if (Type.vecsize != 1)
                return "opaque";
            switch (Type.basetype)
            {
                case spirv_cross::SPIRType::Boolean:
                    return "boolean";
                case spirv_cross::SPIRType::Int:
                case spirv_cross::SPIRType::UInt:
                    return "integer";
                case spirv_cross::SPIRType::Float:
                    return "float";
                default:
                    return "opaque";
            }
        }

        nlohmann::json UniformBlocks(const std::vector<std::uint8_t>& Bytes,
                                     const std::set<std::string>& BooleanParameters)
        {
            std::vector<std::uint32_t> Words(Bytes.size() / sizeof(std::uint32_t));
            std::memcpy(Words.data(), Bytes.data(), Bytes.size());
            spirv_cross::Compiler Compiler(std::move(Words));
            const spirv_cross::ShaderResources Resources = Compiler.get_shader_resources();

            nlohmann::json Blocks = nlohmann::json::array();
            for (const spirv_cross::Resource& Resource : Resources.uniform_buffers)
            {
                const spirv_cross::SPIRType& BlockType = Compiler.get_type(Resource.base_type_id);
                nlohmann::json Parameters = nlohmann::json::array();
                for (std::uint32_t Index = 0; Index < BlockType.member_types.size(); ++Index)
                {
                    const spirv_cross::SPIRType& MemberType = Compiler.get_type(BlockType.member_types[Index]);
                    const std::string Name = Compiler.get_member_name(BlockType.self, Index);
                    std::string Kind = ParameterKind(MemberType);
                    if (Kind == "integer" && BooleanParameters.contains(Name))
                        Kind = "boolean";
                    Parameters.push_back({{"name", Name},
                                          {"kind", Kind},
                                          {"offset", Compiler.type_struct_member_offset(BlockType, Index)},
                                          {"size", Compiler.get_declared_struct_member_size(BlockType, Index)},
                                          {"row_major", Compiler.has_member_decoration(BlockType.self, Index,
                                                                                       spv::DecorationRowMajor)}});
                }
                Blocks.push_back({{"name", Resource.name},
                                  {"set", Compiler.get_decoration(Resource.id, spv::DecorationDescriptorSet)},
                                  {"binding", Compiler.get_decoration(Resource.id, spv::DecorationBinding)},
                                  {"size", Compiler.get_declared_struct_size(BlockType)},
                                  {"parameters", std::move(Parameters)}});
            }
            return Blocks;
        }

        bool IsIdentifier(const std::string& Value)
        {
            if (Value.empty() || (!std::isalpha(static_cast<unsigned char>(Value.front())) && Value.front() != '_'))
                return false;
            return std::all_of(Value.begin() + 1, Value.end(), [](unsigned char Character)
                               { return std::isalnum(Character) != 0 || Character == '_'; });
        }

        bool ParseTechniques(const std::string& SourceText, std::vector<TechniqueDefinition>& Techniques,
                             std::string& Error)
        {
            constexpr std::string_view Prefix = "// opendbc:technique ";
            std::istringstream Lines(SourceText);
            std::string Line;
            std::size_t LineNumber = 0;
            while (std::getline(Lines, Line))
            {
                ++LineNumber;
                const std::size_t First = Line.find_first_not_of(" \t\r");
                if (First == std::string::npos || !std::string_view(Line).substr(First).starts_with(Prefix))
                    continue;

                std::istringstream Definition(Line.substr(First + Prefix.size()));
                TechniqueDefinition Technique;
                std::string Extra;
                if (!(Definition >> Technique.Name >> Technique.VertexEntryPoint >> Technique.FragmentEntryPoint) ||
                    (Definition >> Extra) || !IsIdentifier(Technique.Name) ||
                    !IsIdentifier(Technique.VertexEntryPoint) || !IsIdentifier(Technique.FragmentEntryPoint) ||
                    Technique.Name == "default")
                {
                    Error = "line " + std::to_string(LineNumber) +
                            ": technique syntax is // opendbc:technique Name VertexEntry FragmentEntry";
                    return false;
                }
                if (std::any_of(Techniques.begin(), Techniques.end(),
                                [&](const TechniqueDefinition& Existing) { return Existing.Name == Technique.Name; }))
                {
                    Error = "line " + std::to_string(LineNumber) + ": duplicate effect technique " + Technique.Name;
                    return false;
                }
                if (Techniques.size() >= 16)
                {
                    Error = "an effect may declare at most 16 techniques";
                    return false;
                }
                Techniques.push_back(std::move(Technique));
            }
            return true;
        }

        bool ContainsLegacyShaderAssembly(const std::string& SourceText)
        {
            std::string Lower = SourceText;
            std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            static const std::regex AssemblyBlock(R"(\basm\s*\{)");
            return Lower.find("vs.1.") != std::string::npos || Lower.find("ps.1.") != std::string::npos ||
                   std::regex_search(Lower, AssemblyBlock);
        }

        bool ContainsLegacyEffectFrameworkTechnique(const std::string& SourceText)
        {
            static const std::regex Technique(R"(\btechnique(?:10|11)?\s+[A-Za-z_][A-Za-z0-9_]*\s*\{)",
                                              std::regex::icase);
            static const std::regex StageAssignment(R"(\b(?:VertexShader|PixelShader)\s*=)", std::regex::icase);
            return std::regex_search(SourceText, Technique) && std::regex_search(SourceText, StageAssignment);
        }

        bool DefinesEntryPoint(const std::string& SourceText, std::string_view Name)
        {
            const std::regex Definition("\\b" + std::string(Name) + R"(\s*\()", std::regex::icase);
            return std::regex_search(SourceText, Definition);
        }

        std::set<std::string> HlslBooleanParameters(const std::string& SourceText)
        {
            static const std::regex Declaration(R"(\bbool\s+([A-Za-z_][A-Za-z0-9_]*))");
            std::set<std::string> Result;
            for (std::sregex_iterator It(SourceText.begin(), SourceText.end(), Declaration), End; It != End; ++It)
                Result.insert((*It)[1].str());
            return Result;
        }

        bool CompileStage(const std::string& SourceText, const std::string& IncludeDirectory,
                          const std::set<std::string>& BooleanParameters, const char* EntryPoint,
                          SDL_ShaderCross_ShaderStage Stage, CompiledStage& Result, std::string& Error)
        {
            const SDL_PropertiesID Properties = SDL_CreateProperties();
            if (Properties == 0)
            {
                Error = std::string(EntryPoint) + ": cannot create shader compiler properties: " + SDL_GetError();
                return false;
            }
            SDL_SetBooleanProperty(Properties, SDL_SHADERCROSS_PROP_SHADER_CULL_UNUSED_BINDINGS_BOOLEAN, true);
            SDL_ShaderCross_HLSL_Info Info{};
            Info.source = SourceText.c_str();
            Info.entrypoint = EntryPoint;
            Info.include_dir = IncludeDirectory.c_str();
            Info.shader_stage = Stage;
            Info.props = Properties;

            size_t ByteCount = 0;
            SdlBuffer Buffer;
            Buffer.Value = SDL_ShaderCross_CompileSPIRVFromHLSL(&Info, &ByteCount);
            SDL_DestroyProperties(Properties);
            if (!Buffer.Value)
            {
                Error = std::string(EntryPoint) + ": " + SDL_GetError();
                return false;
            }
            if (ByteCount < sizeof(std::uint32_t) || ByteCount % sizeof(std::uint32_t) != 0)
            {
                Error = std::string(EntryPoint) + ": SDL_shadercross returned invalid SPIR-V";
                return false;
            }

            SdlMetadata Metadata;
            Metadata.Value =
                SDL_ShaderCross_ReflectGraphicsSPIRV(static_cast<const Uint8*>(Buffer.Value), ByteCount, 0);
            if (!Metadata.Value)
            {
                Error = std::string(EntryPoint) + ": SPIR-V reflection failed: " + SDL_GetError();
                return false;
            }

            const auto& Resources = Metadata.Value->resource_info;
            const auto* Bytes = static_cast<const std::uint8_t*>(Buffer.Value);
            Result.Bytes.assign(Bytes, Bytes + ByteCount);

            SDL_ShaderCross_SPIRV_Info SpirvInfo{};
            SpirvInfo.bytecode = Bytes;
            SpirvInfo.bytecode_size = ByteCount;
            SpirvInfo.entrypoint = EntryPoint;
            SpirvInfo.shader_stage = Stage;
            SdlBuffer Msl;
            Msl.Value = SDL_ShaderCross_TranspileMSLFromSPIRV(&SpirvInfo);
            if (!Msl.Value)
            {
                Error = std::string(EntryPoint) + ": MSL transpilation failed: " + SDL_GetError();
                return false;
            }
            Result.Msl = static_cast<const char*>(Msl.Value);
            // SPIRV-Cross renames entry points that MSL reserves, such as main -> main0.
            static const std::regex MslEntryPoint(R"(\b(?:vertex|fragment)\s+\S+\s+(\w+)\s*\()");
            std::smatch MslEntryMatch;
            if (!std::regex_search(Result.Msl, MslEntryMatch, MslEntryPoint))
            {
                Error = std::string(EntryPoint) + ": transpiled MSL has no entry point";
                return false;
            }
            try
            {
                Result.Metadata = {
                    {"entry_point", EntryPoint},
                    {"msl_entry_point", MslEntryMatch[1].str()},
                    {"resources",
                     {{"samplers", Resources.num_samplers},
                      {"storage_textures", Resources.num_storage_textures},
                      {"storage_buffers", Resources.num_storage_buffers},
                      {"uniform_buffers", Resources.num_uniform_buffers}}},
                    {"uniform_blocks", UniformBlocks(Result.Bytes, BooleanParameters)},
                    {"inputs", IoVariables(Metadata.Value->inputs, Metadata.Value->num_inputs)},
                    {"outputs", IoVariables(Metadata.Value->outputs, Metadata.Value->num_outputs)},
                };
            }
            catch (const std::exception& ReflectionError)
            {
                Error = std::string(EntryPoint) + ": uniform reflection failed: " + ReflectionError.what();
                return false;
            }
            return true;
        }

        bool WriteTemporary(const std::filesystem::path& Target, const std::uint8_t* Bytes, std::size_t ByteCount,
                            std::filesystem::path& Temporary, std::string& Error)
        {
            Temporary = Target;
            Temporary += ".opendbc-tmp";
            std::error_code RemoveError;
            std::filesystem::remove(Temporary, RemoveError);

            std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
            if (!Stream)
            {
                Error = "Cannot write " + dbc::PathToUtf8(Temporary);
                return false;
            }
            Stream.write(reinterpret_cast<const char*>(Bytes), static_cast<std::streamsize>(ByteCount));
            Stream.close();
            if (!Stream)
            {
                Error = "Cannot write " + dbc::PathToUtf8(Temporary);
                std::filesystem::remove(Temporary, RemoveError);
                return false;
            }
            return true;
        }

        bool PublishTemporary(const std::filesystem::path& Temporary, const std::filesystem::path& Target,
                              std::string& Error)
        {
            std::error_code RenameError;
            std::filesystem::rename(Temporary, Target, RenameError);
            if (!RenameError)
                return true;

            std::error_code RemoveError;
            std::filesystem::remove(Target, RemoveError);
            RenameError.clear();
            std::filesystem::rename(Temporary, Target, RenameError);
            if (!RenameError)
                return true;

            Error = "Cannot publish " + dbc::PathToUtf8(Target) + ": " + RenameError.message();
            return false;
        }

        void RemoveTemporaries(const std::vector<std::filesystem::path>& Temporaries)
        {
            for (const auto& Temporary : Temporaries)
            {
                if (Temporary.empty())
                    continue;
                std::error_code Error;
                std::filesystem::remove(Temporary, Error);
            }
        }
    } // namespace

    namespace toolchain
    {
        ShaderCompileResult ShaderCompiler::CompileHlslEffect(const std::filesystem::path& Source) const
        {
            ShaderCompileResult Result;
            std::error_code FileError;
            if (!std::filesystem::is_regular_file(Source, FileError))
            {
                Result.Error = "HLSL source is not a regular file: " + dbc::PathToUtf8(Source);
                return Result;
            }

            std::string Extension = dbc::PathToUtf8(Source.extension());
            for (char& Character : Extension)
                Character = static_cast<char>(SDL_tolower(static_cast<unsigned char>(Character)));
            if (Extension != ".fx" && Extension != ".hlsl")
            {
                Result.Error = "HLSL effects must use the .fx or .hlsl extension";
                return Result;
            }

            std::ifstream Stream(Source, std::ios::binary);
            if (!Stream)
            {
                Result.Error = "Cannot read HLSL source: " + dbc::PathToUtf8(Source);
                return Result;
            }
            std::string SourceText((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
            if (!Stream.good() && !Stream.eof())
            {
                Result.Error = "Cannot read HLSL source: " + dbc::PathToUtf8(Source);
                return Result;
            }
            if (SourceText.empty())
            {
                Result.Error = "HLSL source is empty: " + dbc::PathToUtf8(Source);
                return Result;
            }
            if (SourceText.find('\0') != std::string::npos)
            {
                Result.Error = "HLSL source contains an embedded null byte: " + dbc::PathToUtf8(Source);
                return Result;
            }

            std::vector<TechniqueDefinition> TechniqueDefinitions;
            if (!ParseTechniques(SourceText, TechniqueDefinitions, Result.Error))
            {
                Result.Error = dbc::PathToUtf8(Source.filename()) + ": " + Result.Error;
                return Result;
            }
            if (ContainsLegacyShaderAssembly(SourceText))
            {
                Result.Error = dbc::PathToUtf8(Source.filename()) +
                               ": legacy DirectX shader assembly is not supported; port the effect to HLSL";
                return Result;
            }
            if (ContainsLegacyEffectFrameworkTechnique(SourceText) &&
                (!DefinesEntryPoint(SourceText, "VSMain") || !DefinesEntryPoint(SourceText, "PSMain")))
            {
                Result.Error = dbc::PathToUtf8(Source.filename()) +
                               ": legacy DirectX Effect Framework techniques are not supported; convert the effect to "
                               "VSMain/PSMain and // opendbc:technique declarations";
                return Result;
            }
            ShaderCrossScope ShaderCross;
            ShaderCross.Initialized = SDL_ShaderCross_Init();
            if (!ShaderCross.Initialized)
            {
                Result.Error = "SDL_shadercross initialization failed: " + std::string(SDL_GetError());
                return Result;
            }
            if ((SDL_ShaderCross_GetHLSLShaderFormats() & SDL_GPU_SHADERFORMAT_SPIRV) == 0)
            {
                Result.Error = "This OpenDBC build has no HLSL to SPIR-V compiler";
                return Result;
            }

            const std::string IncludeDirectory =
                Source.parent_path().empty() ? "." : dbc::PathToUtf8(Source.parent_path());
            const std::set<std::string> BooleanParameters = HlslBooleanParameters(SourceText);
            CompiledStage Vertex;
            CompiledStage Fragment;
            if (!CompileStage(SourceText, IncludeDirectory, BooleanParameters, "VSMain",
                              SDL_SHADERCROSS_SHADERSTAGE_VERTEX, Vertex, Result.Error) ||
                !CompileStage(SourceText, IncludeDirectory, BooleanParameters, "PSMain",
                              SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, Fragment, Result.Error))
            {
                Result.Error = dbc::PathToUtf8(Source.filename()) + ": " + Result.Error;
                return Result;
            }

            std::vector<CompiledTechnique> Techniques;
            Techniques.reserve(TechniqueDefinitions.size());
            for (const TechniqueDefinition& Definition : TechniqueDefinitions)
            {
                CompiledTechnique Technique;
                Technique.Definition = Definition;
                if (!CompileStage(SourceText, IncludeDirectory, BooleanParameters, Definition.VertexEntryPoint.c_str(),
                                  SDL_SHADERCROSS_SHADERSTAGE_VERTEX, Technique.Vertex, Result.Error) ||
                    !CompileStage(SourceText, IncludeDirectory, BooleanParameters,
                                  Definition.FragmentEntryPoint.c_str(), SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
                                  Technique.Fragment, Result.Error))
                {
                    Result.Error =
                        dbc::PathToUtf8(Source.filename()) + ": technique " + Definition.Name + ": " + Result.Error;
                    return Result;
                }
                Techniques.push_back(std::move(Technique));
            }

            const std::filesystem::path Stem = Source.parent_path() / Source.stem();
            Result.VertexPath = (std::filesystem::path(Stem) += ".vert.spv");
            Result.FragmentPath = (std::filesystem::path(Stem) += ".frag.spv");
            Result.ManifestPath = (std::filesystem::path(Stem) += ".effect.json");

            const auto NameStage = [](CompiledStage& Stage, const std::filesystem::path& StageStem)
            {
                Stage.Metadata["file"] = dbc::GenericPathToUtf8(StageStem.filename()) + ".spv";
                Stage.Metadata["msl_file"] = dbc::GenericPathToUtf8(StageStem.filename()) + ".msl";
            };
            NameStage(Vertex, (std::filesystem::path(Stem) += ".vert"));
            NameStage(Fragment, (std::filesystem::path(Stem) += ".frag"));
            nlohmann::json TechniqueManifest = nlohmann::json::array();
            for (CompiledTechnique& Technique : Techniques)
            {
                const std::filesystem::path TechniqueStem =
                    dbc::PathFromUtf8(dbc::PathToUtf8(Stem) + "." + Technique.Definition.Name);
                NameStage(Technique.Vertex, (std::filesystem::path(TechniqueStem) += ".vert"));
                NameStage(Technique.Fragment, (std::filesystem::path(TechniqueStem) += ".frag"));
                TechniqueManifest.push_back(
                    {{"name", Technique.Definition.Name},
                     {"stages", {{"vertex", Technique.Vertex.Metadata}, {"fragment", Technique.Fragment.Metadata}}}});
            }
            nlohmann::json Manifest = {
                {"format", "opendbc-effect-v1"},
                {"source", dbc::GenericPathToUtf8(Source.filename())},
                {"source_language", "hlsl"},
                {"stages", {{"vertex", Vertex.Metadata}, {"fragment", Fragment.Metadata}}},
                {"techniques", std::move(TechniqueManifest)},
            };
            const std::string ManifestText = Manifest.dump(2) + '\n';

            std::vector<std::filesystem::path> Targets;
            std::vector<std::pair<const void*, std::size_t>> Payloads;
            const auto AddStage = [&](const CompiledStage& Stage, const std::filesystem::path& StageStem)
            {
                Targets.push_back((std::filesystem::path(StageStem) += ".spv"));
                Payloads.emplace_back(Stage.Bytes.data(), Stage.Bytes.size());
                Targets.push_back((std::filesystem::path(StageStem) += ".msl"));
                Payloads.emplace_back(Stage.Msl.data(), Stage.Msl.size());
            };
            AddStage(Vertex, (std::filesystem::path(Stem) += ".vert"));
            AddStage(Fragment, (std::filesystem::path(Stem) += ".frag"));
            for (const CompiledTechnique& Technique : Techniques)
            {
                const std::filesystem::path TechniqueStem =
                    dbc::PathFromUtf8(dbc::PathToUtf8(Stem) + "." + Technique.Definition.Name);
                AddStage(Technique.Vertex, (std::filesystem::path(TechniqueStem) += ".vert"));
                AddStage(Technique.Fragment, (std::filesystem::path(TechniqueStem) += ".frag"));
            }
            Targets.push_back(Result.ManifestPath);

            std::vector<std::filesystem::path> Temporaries(Targets.size());
            for (std::size_t Index = 0; Index < Payloads.size(); ++Index)
            {
                if (!WriteTemporary(Targets[Index], static_cast<const std::uint8_t*>(Payloads[Index].first),
                                    Payloads[Index].second, Temporaries[Index], Result.Error))
                {
                    RemoveTemporaries(Temporaries);
                    return Result;
                }
            }
            if (!WriteTemporary(Result.ManifestPath, reinterpret_cast<const std::uint8_t*>(ManifestText.data()),
                                ManifestText.size(), Temporaries.back(), Result.Error))
            {
                RemoveTemporaries(Temporaries);
                return Result;
            }

            for (std::size_t Index = 0; Index < Temporaries.size(); ++Index)
            {
                if (!PublishTemporary(Temporaries[Index], Targets[Index], Result.Error))
                {
                    RemoveTemporaries(Temporaries);
                    return Result;
                }
            }

            Result.Success = true;
            return Result;
        }
    } // namespace toolchain

} // namespace dbc::studio
