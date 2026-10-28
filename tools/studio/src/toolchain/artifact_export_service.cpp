#include "artifact_export_service.hpp"
#include "core/paths.hpp"
#include "export_internal.hpp"

#include <system_error>
#include <utility>

namespace dbc::studio
{
    namespace fs = std::filesystem;

    namespace toolchain
    {
        ArtifactExportResult ArtifactExportService::Export(const ArtifactExportRequest& Request,
                                                           const ArtifactExportProgress& Progress) const
        {
            if (!Request.Settings.LoadError.empty())
                return {{}, {}, Request.Settings.LoadError};
            std::error_code Error;
            if (!fs::is_directory(Request.OutputDirectory, Error))
                return {{}, {}, "Export destination is not a directory: " + Request.OutputDirectory.string()};
            const internal::ExportContext Context{&Progress};
            Context.Note("Compiling " + Request.EntrySource.filename().string());
            std::vector<std::uint8_t> Image;
            std::string ProgramError;
            if (!internal::BuildExportProgram(Request, Image, ProgramError))
                return {{}, {}, std::move(ProgramError)};
            if (Context.Cancelled(ProgramError))
                return {{}, {}, std::move(ProgramError)};
            switch (Request.Target)
            {
                case ArtifactTarget::Desktop:
                    return internal::ExportDesktop(Context, Request, Image);
                case ArtifactTarget::Ios:
                    return internal::ExportIos(Context, Request, Image);
                case ArtifactTarget::IosSimulator:
                    return internal::ExportIosSimulator(Context, Request, Image);
                case ArtifactTarget::AndroidApk:
                    return internal::ExportAndroid(Context, Request, Image, false);
                case ArtifactTarget::AndroidBundle:
                    return internal::ExportAndroid(Context, Request, Image, true);
            }
            return {{}, {}, "Unknown artifact target"};
        }

        ArtifactExportRequest MakeArtifactExportRequest(const dbc::interpreter::ResolvedProject& Project,
                                                        core::ProjectSettings Settings, ArtifactTarget Target,
                                                        const fs::path& OutputDirectory, bool Sign)
        {
            ArtifactExportRequest Request;
            Request.Target = Target;
            Request.ProjectRoot = Project.ProjectRoot;
            Request.EntrySource = Project.EntrySource;
            Request.Profile = Project.Profile;
            Request.OrderedSources = Project.OrderedSources;
            Request.Startup = Project.Startup;
            Request.ProjectFile = Project.DescriptorPath;
            Request.AssetRoot = Project.AssetRoot;
            Request.OutputDirectory = fs::absolute(OutputDirectory).lexically_normal();
            Request.HostProduct = core::paths::RunnerProduct();
            Request.HostResources = core::paths::ResourceDir();
            if (Settings.AppName.empty())
                Settings.AppName = Project.Metadata.Name;
            if (Settings.VersionName == "1.0.0" && !Project.Metadata.VersionName.empty())
                Settings.VersionName = Project.Metadata.VersionName;
            if (Settings.IconSource.empty())
                Settings.IconSource = Project.Metadata.Icon;
            Request.Settings = std::move(Settings);
            Request.Sign = Sign;
            return Request;
        }
    } // namespace toolchain

} // namespace dbc::studio
