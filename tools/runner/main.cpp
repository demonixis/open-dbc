// opendbc-runner runs one compiled program image (.obc). It holds the VM, the engine and the asset
// loaders, but neither the BASIC frontend, the Studio nor the shader compiler.

#include "interpreter/command_line.hpp"
#include "interpreter/embedded_artifact.hpp"
#include "interpreter/program_image.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view HelpText = "Usage: opendbc-runner <program.obc> [run options]\n"
                                          "An exported game runs its embedded program and ignores arguments.\n"
                                          "Run options are those of 'opendbc run' (see opendbc --help).\n";

    void Write(std::FILE* Stream, std::string_view Text)
    {
        std::fwrite(Text.data(), sizeof(char), Text.size(), Stream);
        std::fflush(Stream);
    }
} // namespace

int main(int Argc, char* Argv[])
{
    const char* Base = SDL_GetBasePath();
    const dbc::interpreter::EmbeddedArtifactResult Embedded =
        dbc::interpreter::FindEmbeddedArtifact(Base ? std::filesystem::path(Base) : std::filesystem::path{});
    if (!Embedded.Error.empty())
    {
        Write(stderr, Embedded.Error + "\n");
        return static_cast<int>(dbc::interpreter::CommandLineExitCode::Target);
    }

    std::vector<std::string_view> Arguments{"run"};
    std::string Program;
    std::string AssetRoot;
    if (Embedded.Artifact)
    {
        Program = Embedded.Artifact->ProgramPath.string();
        AssetRoot = Embedded.Artifact->AssetRoot.string();
        Arguments.insert(Arguments.end(), {Program, "--asset-root", AssetRoot});
    }
    else
    {
        for (int Index = 1; Index < Argc; ++Index)
            Arguments.emplace_back(Argv[Index]);
    }

    const dbc::interpreter::CommandLineParseResult Parsed = dbc::interpreter::ParseCommandLine(Arguments);
    if (Parsed.HelpRequested)
    {
        Write(stdout, HelpText);
        return static_cast<int>(dbc::interpreter::CommandLineExitCode::Success);
    }
    if (Parsed.VersionRequested)
    {
        Write(stdout, dbc::interpreter::CommandLineVersion());
        return static_cast<int>(dbc::interpreter::CommandLineExitCode::Success);
    }
    if (!Parsed)
    {
        Write(stderr, Parsed.Error + "\n");
        Write(stderr, HelpText);
        return static_cast<int>(dbc::interpreter::CommandLineExitCode::Usage);
    }

    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [](std::string_view Text) { Write(stdout, Text); };
    Environment.StandardError = [](std::string_view Text) { Write(stderr, Text); };
    return dbc::interpreter::RunProgramImage(*Parsed.Command, Environment);
}
