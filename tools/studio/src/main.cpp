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
#include "core/paths.hpp"
#include "interpreter/command_line.hpp"
#include "toolchain/artifact_export_service.hpp"
#include "toolchain/artifact_runtime.hpp"

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
            toolchain::ArtifactExportRequest Request;
            Request.ProjectRoot = Project.ProjectRoot;
            Request.EntrySource = Project.EntrySource;
            Request.Profile = Project.Profile;
            Request.OrderedSources = Project.OrderedSources;
            Request.ProjectFile = Project.DescriptorPath;
            Request.AssetRoot = Project.AssetRoot;
            Request.OutputDirectory = std::filesystem::absolute(Command.ExportOutput).lexically_normal();
            Request.HostProduct = core::paths::ProductPath();
            Request.HostResources = core::paths::ResourceDir();
            Request.Settings = core::ProjectSettings::Load(Request.ProjectRoot);
            if (!Request.Settings.LoadError.empty())
                return 1;
            if (Request.Settings.AppName.empty())
                Request.Settings.AppName = Project.Metadata.Name;
            if (Request.Settings.VersionName == "1.0.0" && !Project.Metadata.VersionName.empty())
                Request.Settings.VersionName = Project.Metadata.VersionName;
            if (Request.Settings.IconSource.empty())
                Request.Settings.IconSource = Project.Metadata.Icon;
            Request.Sign = Command.ExportSign;
            if (Command.ExportTarget == "desktop")
                Request.Target = toolchain::ArtifactTarget::Desktop;
            else if (Command.ExportTarget == "ios")
                Request.Target = toolchain::ArtifactTarget::Ios;
            else if (Command.ExportTarget == "ios-simulator")
                Request.Target = toolchain::ArtifactTarget::IosSimulator;
            else if (Command.ExportTarget == "android-apk")
                Request.Target = toolchain::ArtifactTarget::AndroidApk;
            else
                Request.Target = toolchain::ArtifactTarget::AndroidBundle;

            const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
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
    const toolchain::EmbeddedArtifactResult Embedded = toolchain::FindEmbeddedArtifact(core::paths::ResourceDir());
    if (!Embedded.Error.empty())
    {
        Write(stderr, Embedded.Error + "\n");
        return static_cast<int>(dbc::interpreter::CommandLineExitCode::Target);
    }

    std::vector<std::string_view> Arguments;
    std::vector<std::string> EmbeddedArguments;
    if (Embedded.Artifact)
    {
        EmbeddedArguments = {
            "run",          Embedded.Artifact->EntryPath.string(),
            "--asset-root", Embedded.Artifact->AssetRoot.string(),
            "--dialect",    Embedded.Artifact->Profile == dbc::language::LanguageProfile::Dbc ? "dbc" : "dbpro"};
        Arguments.reserve(EmbeddedArguments.size());
        for (const std::string& Argument : EmbeddedArguments)
            Arguments.emplace_back(Argument);
    }
    else
    {
        Arguments.reserve(Argc > 1 ? static_cast<std::size_t>(Argc - 1) : 0);
        for (int Index = 1; Index < Argc; ++Index)
            Arguments.emplace_back(Argv[Index]);
    }

    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [](std::string_view Text) { Write(stdout, Text); };
    Environment.StandardError = [](std::string_view Text) { Write(stderr, Text); };
    Environment.LaunchStudio = LaunchStudio;
    Environment.ExportArtifact = ExportArtifact;
    if (Embedded.Artifact)
    {
        Environment.ConfinedSourceRoot = Embedded.Artifact->PayloadRoot;
        Environment.OrderedProjectSources = Embedded.Artifact->OrderedSources;
    }
    return dbc::interpreter::DispatchCommandLine(std::span<const std::string_view>(Arguments), Environment);
}
