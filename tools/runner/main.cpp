// opendbc-runner runs one compiled program image (.obc). It holds the VM, the engine and the asset
// loaders, but neither the BASIC frontend, the Studio nor the shader compiler.

#include "interpreter/command_line.hpp"
#include "interpreter/program_image.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view HelpText = "Usage: opendbc-runner [program.obc] [run options]\n"
                                          "Without a program, runs program.obc from the runner's resources.\n"
                                          "Run options are those of 'opendbc run' (see opendbc --help).\n";

    void Write(std::FILE* Stream, std::string_view Text)
    {
        std::fwrite(Text.data(), sizeof(char), Text.size(), Stream);
        std::fflush(Stream);
    }
} // namespace

int main(int Argc, char* Argv[])
{
    std::vector<std::string_view> Arguments{"run"};
    std::string Packaged;
    if (Argc < 2 || Argv[1][0] == '-')
    {
        const char* Base = SDL_GetBasePath();
        Packaged = std::string(Base ? Base : "") + "program" + std::string(dbc::interpreter::ProgramImageExtension);
        Arguments.emplace_back(Packaged);
    }
    for (int Index = 1; Index < Argc; ++Index)
        Arguments.emplace_back(Argv[Index]);

    const dbc::interpreter::CommandLineParseResult Parsed = dbc::interpreter::ParseCommandLine(Arguments);
    if (Parsed.HelpRequested)
    {
        Write(stdout, HelpText);
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
