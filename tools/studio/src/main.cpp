#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app.hpp"
#include "core/settings.hpp"
#include "interpreter/command_line.hpp"
#include "toolchain/artifact_export_service.hpp"
#include "toolchain/export_readiness.hpp"

namespace dbc::studio
{
    namespace
    {
        void Write(FILE* Stream, std::string_view Text)
        {
            std::fwrite(Text.data(), sizeof(char), Text.size(), Stream);
            std::fflush(Stream);
        }

        int LaunchStudio(const dbc::interpreter::CommandLine& Command)
        {
            SDL_SetAppMetadata("OpenDBC", OPENDBC_VERSION_STRING, "net.demonixis.opendbc");

            App Studio;
            if (!Studio.Initialize(Command.Path ? Command.Path->string() : std::string(), Command.RequestedProfile))
            {
                Studio.Shutdown();
                return 1;
            }

            Studio.Run();
            Studio.Shutdown();
            return 0;
        }

        int ExportArtifact(const dbc::interpreter::CommandLine& Command,
                           const dbc::interpreter::ResolvedProject& Project)
        {
            core::ProjectSettings Settings = core::ProjectSettings::Load(Project.ProjectRoot);
            if (!Settings.LoadError.empty())
                return 1;
            toolchain::ArtifactTarget Target = toolchain::ArtifactTarget::AndroidBundle;
            if (Command.ExportTarget == "desktop")
                Target = toolchain::ArtifactTarget::Desktop;
            else if (Command.ExportTarget == "ios")
                Target = toolchain::ArtifactTarget::Ios;
            else if (Command.ExportTarget == "ios-simulator")
                Target = toolchain::ArtifactTarget::IosSimulator;
            else if (Command.ExportTarget == "android-apk")
                Target = toolchain::ArtifactTarget::AndroidApk;
            const toolchain::ArtifactExportRequest Request = toolchain::MakeArtifactExportRequest(
                Project, std::move(Settings), Target, Command.ExportOutput, Command.ExportSign,
                toolchain::ResolveExportToolchain(core::Settings::Load(), toolchain::ToolchainSearch::Host()),
                toolchain::ExportSecrets::FromEnvironment());

            const std::vector<toolchain::ExportCheck> Checks = toolchain::CheckExportReadiness(Request);
            for (const toolchain::ExportCheck& Check : Checks)
            {
                if (!Check.Passed)
                    Write(stderr, std::string(Check.Required ? "Missing: " : "Warning: ") + Check.Label + ". " +
                                      Check.Detail + "\n");
            }
            if (!toolchain::ReadyToExport(Checks))
                return static_cast<int>(dbc::interpreter::CommandLineExitCode::Target);

            toolchain::ArtifactExportProgress Progress;
            Progress.Log = [](std::string_view Line) { Write(stdout, std::string(Line) + "\n"); };
            const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request, Progress);
            for (const std::string& Message : Result.Messages)
                Write(stdout, Message + "\n");
            if (!Result)
            {
                Write(stderr, Result.Error + "\n");
                return static_cast<int>(dbc::interpreter::CommandLineExitCode::Target);
            }
            return static_cast<int>(dbc::interpreter::CommandLineExitCode::Success);
        }
    } // namespace

} // namespace dbc::studio

using namespace dbc::studio;

int main(int Argc, char* Argv[])
{
    std::vector<std::string_view> Arguments;
    Arguments.reserve(Argc > 1 ? static_cast<std::size_t>(Argc - 1) : 0);
    for (int Index = 1; Index < Argc; ++Index)
        Arguments.emplace_back(Argv[Index]);

    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [](std::string_view Text) { Write(stdout, Text); };
    Environment.StandardError = [](std::string_view Text) { Write(stderr, Text); };
    Environment.LaunchStudio = LaunchStudio;
    Environment.ExportArtifact = ExportArtifact;
    return dbc::interpreter::DispatchCommandLine(std::span<const std::string_view>(Arguments), Environment);
}
