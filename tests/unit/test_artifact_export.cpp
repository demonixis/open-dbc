#include <doctest/doctest.h>

#include "interpreter/atomic_file.hpp"
#include "interpreter/command_line.hpp"
#include "interpreter/embedded_artifact.hpp"
#include "interpreter/program_image.hpp"
#include "toolchain/artifact_export_service.hpp"
#include <nlohmann/json.hpp>

#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace dbc::studio;

namespace
{
    namespace fs = std::filesystem;

    struct TemporaryDirectory
    {
        explicit TemporaryDirectory(std::string_view Name)
        {
            Root =
                fs::temp_directory_path() /
                (std::string(Name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(Root);
        }

        ~TemporaryDirectory()
        {
            std::error_code Ignored;
            fs::remove_all(Root, Ignored);
        }

        fs::path Root;
    };

    void Write(const fs::path& Path, std::string_view Text)
    {
        fs::create_directories(Path.parent_path());
        std::ofstream Output(Path, std::ios::binary);
        Output << Text;
    }

    void WriteAndroidLibrary(const fs::path& Resources)
    {
        std::string Header(64, '\0');
        Header[0] = static_cast<char>(0x7f);
        Header[1] = 'E';
        Header[2] = 'L';
        Header[3] = 'F';
        Header[4] = 2;
        Header[5] = 1;
        Header[6] = 1;
        Header[18] = static_cast<char>(0xb7);
        const fs::path Binary = Resources / "export-hosts/android/template/app/src/main/jniLibs/arm64-v8a/libmain.so";
        const fs::path Runtime = Resources / "opendbc-runtime.json";
        const fs::path Host = Resources / "export-hosts/android/template/app/src/main/assets/opendbc-host.json";
        const fs::path SourceShaders = fs::path(OPENDBC_TEST_SOURCE_DIR) / "shaders";
        const fs::path ProductShaders = Resources / "shaders";
        const fs::path HostShaders = Host.parent_path() / "shaders";
        fs::create_directories(ProductShaders);
        fs::create_directories(HostShaders);
        for (const std::string_view Name :
             {"dbclassic_tri.vert.spv", "dbclassic_tri.frag.spv", "dbclassic_tri.vert.msl", "dbclassic_tri.frag.msl",
              "vr_transfer.vert.spv", "vr_transfer.frag.spv", "vr_transfer.vert.msl", "vr_transfer.frag.msl"})
        {
            fs::copy_file(SourceShaders / Name, ProductShaders / Name, fs::copy_options::overwrite_existing);
            fs::copy_file(SourceShaders / Name, HostShaders / Name, fs::copy_options::overwrite_existing);
        }
        Write(Binary, Header);
        Write(Runtime, "{\"fingerprint\":\"test\"}\n");
        const std::string RuntimeHash = *toolchain::detail::ArtifactSha256File(Runtime);
        const std::string BinaryHash = *toolchain::detail::ArtifactSha256File(Binary);
        Write(Host, "{\"format\":1,\"runtime_manifest_sha256\":\"" + RuntimeHash + "\",\"binary_sha256\":\"" +
                        BinaryHash + "\",\"needed\":[\"libc.so\"]}\n");
    }

    constexpr std::array<std::string_view, 8> CanonicalShaders{
        "dbclassic_tri.vert.spv", "dbclassic_tri.frag.spv", "dbclassic_tri.vert.msl", "dbclassic_tri.frag.msl",
        "vr_transfer.vert.spv",   "vr_transfer.frag.spv",   "vr_transfer.vert.msl",   "vr_transfer.frag.msl"};

    void CopyCanonicalShaders(const fs::path& Destination)
    {
        fs::create_directories(Destination);
        for (const std::string_view Name : CanonicalShaders)
            fs::copy_file(fs::path(OPENDBC_TEST_SOURCE_DIR) / "shaders" / Name, Destination / Name,
                          fs::copy_options::overwrite_existing);
    }

    // The Studio resources an export reads: runtime manifest, notices, licenses and canonical shaders.
    fs::path FakeHostResources(const fs::path& Root)
    {
#if defined(__APPLE__)
        const fs::path Resources = Root / "OpenDBC.app/Contents/Resources";
#else
        const fs::path Resources = Root;
#if defined(_WIN32)
        Write(Resources / "openxr_loader.dll", "loader");
#else
        Write(Resources / "libopenxr_loader.so.1", "loader");
#endif
#endif
        Write(Resources / "opendbc-runtime.json", "{\"fingerprint\":\"test\"}\n");
        Write(Resources / "NOTICE.md", "notice");
        Write(Resources / "licenses/OpenDBC.txt", "license");
        Write(Resources / "licenses/MuseScore-General-SoundFont-LICENSE.md", "soundfont license");
        CopyCanonicalShaders(Resources / "shaders");
        return Resources;
    }

    // Writes the host manifest the build produces for opendbc-runner, for the runner's current bytes.
    void BindFakeRunner(const fs::path& Root)
    {
#if defined(__APPLE__)
        const fs::path Runner = Root / "OpenDBC.app/Contents/Helpers/opendbc-runner.app";
        const fs::path Binary = Runner / "Contents/MacOS/opendbc-runner";
        const fs::path Manifest = Runner / "Contents/Resources/opendbc-host.json";
#else
        const fs::path Binary = Root / "opendbc-runner";
        const fs::path Manifest = Root / "opendbc-runner-host.json";
#endif
        const fs::path RuntimeManifest = FakeHostResources(Root) / "opendbc-runtime.json";
        Write(Manifest, "{\"format\":1,\"runtime_manifest_sha256\":\"" +
                            *toolchain::detail::ArtifactSha256File(RuntimeManifest) + "\",\"binary_sha256\":\"" +
                            *toolchain::detail::ArtifactSha256File(Binary) + "\",\"needed\":[]}\n");
    }

    // Returns the opendbc-runner product a Studio ships, bound to the fake Studio resources.
    fs::path FakeHost(const fs::path& Root)
    {
        FakeHostResources(Root);
#if defined(__APPLE__)
        const fs::path Host = Root / "OpenDBC.app/Contents/Helpers/opendbc-runner.app";
        Write(Host / "Contents/MacOS/opendbc-runner", "runner");
        CopyCanonicalShaders(Host / "Contents/Resources/shaders");
        Write(Host / "Contents/Info.plist", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                                            "<plist version=\"1.0\"><dict>"
                                            "<key>CFBundleExecutable</key><string>opendbc-runner</string>"
                                            "<key>CFBundleIdentifier</key><string>net.demonixis.opendbc.runner</string>"
                                            "<key>CFBundleDisplayName</key><string>OpenDBC Runner</string>"
                                            "<key>CFBundleName</key><string>OpenDBC Runner</string>"
                                            "<key>CFBundleShortVersionString</key><string>0.3</string>"
                                            "<key>CFBundleVersion</key><string>1</string>"
                                            "</dict></plist>\n");
#else
        const fs::path Host = Root / "opendbc-runner";
        Write(Host, "runner");
#endif
        BindFakeRunner(Root);
        return Host;
    }

    void UseFakeHost(toolchain::ArtifactExportRequest& Request, const fs::path& Root)
    {
        Request.HostProduct = FakeHost(Root);
        Request.HostResources = FakeHostResources(Root);
    }

    fs::path ExportResourceRoot(const fs::path& Output)
    {
#if defined(__APPLE__)
        return Output / "Contents/Resources";
#else
        return Output;
#endif
    }

#if defined(__APPLE__)
    int RunTestProcess(const std::vector<std::string>& Arguments)
    {
        std::vector<const char*> Values;
        Values.reserve(Arguments.size() + 1);
        for (const std::string& Argument : Arguments)
            Values.push_back(Argument.c_str());
        Values.push_back(nullptr);
        SDL_Process* Process = SDL_CreateProcess(Values.data(), false);
        if (Process == nullptr)
            return -1;
        int ExitCode = -1;
        const bool Waited = SDL_WaitProcess(Process, true, &ExitCode);
        SDL_DestroyProcess(Process);
        return Waited ? ExitCode : -1;
    }
#endif
} // namespace

TEST_CASE("desktop artifact export ships the runner, the program image and assets without sources")
{
    TemporaryDirectory Directory("opendbc-artifact-export");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "#include \"include/shared.dba\"\nprint value\n");
    Write(Project / "include/shared.dba", "value=42\n");
    Write(Project / "media/pixel.png", "png");
    Write(Project / "media/level.pak", "pak");
    Write(Project / "media/effect.fx", "effect");
    Write(Project / "media/world.bsp", "world");
    Write(Project / "media/music.mid", "midi");
    Write(Project / "media/music.mp3", "mp3");
    Write(Project / "native.cpp", "int main() {}\n");
    Write(Project / "open-dbc.log", "runtime log\n");
    Write(Project / "open-dbc.1.log", "rotated runtime log\n");
    Write(Project / "open-dbc-crash-20260826-130000.log", "runtime crash report\n");
    Write(Project / "open-dbc-dump-scene.txt", "runtime scene dump\n");
    Write(Project / "open-dbc-dump-renderer.txt", "runtime renderer dump\n");
    Write(Project / "selftest-final.png", "qualification capture\n");
    Write(Project / ".opendbc-project.json", "{\"keystorePassword\":\"secret\"}\n");
    Write(Project / "build/generated.dat", "do not ship\n");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Settings.AppName = "Sample Product";
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    const fs::path Resources = ExportResourceRoot(Result.OutputPath);
    CHECK(fs::is_regular_file(Resources / "opendbc-artifact.json"));
    CHECK(fs::is_regular_file(Resources / "artifact/program.obc"));
    CHECK_FALSE(fs::exists(Resources / "artifact/main.dba"));
    CHECK_FALSE(fs::exists(Resources / "artifact/include/shared.dba"));
#if defined(__APPLE__)
    CHECK(fs::is_regular_file(Result.OutputPath / "Contents/MacOS/opendbc-runner"));
#else
    CHECK(fs::is_regular_file(Result.OutputPath / "opendbc-runner"));
#endif
    CHECK(fs::is_regular_file(Resources / "artifact/media/pixel.png"));
    CHECK(fs::is_regular_file(Resources / "artifact/media/level.pak"));
    CHECK(fs::is_regular_file(Resources / "artifact/media/effect.fx"));
    CHECK(fs::is_regular_file(Resources / "artifact/media/world.bsp"));
    CHECK(fs::is_regular_file(Resources / "artifact/media/music.mid"));
    CHECK(fs::is_regular_file(Resources / "artifact/media/music.mp3"));
    CHECK_FALSE(fs::exists(Resources / "artifact/native.cpp"));
    CHECK_FALSE(fs::exists(Resources / "artifact/open-dbc.log"));
    CHECK_FALSE(fs::exists(Resources / "artifact/open-dbc.1.log"));
    CHECK_FALSE(fs::exists(Resources / "artifact/open-dbc-crash-20260826-130000.log"));
    CHECK_FALSE(fs::exists(Resources / "artifact/open-dbc-dump-scene.txt"));
    CHECK_FALSE(fs::exists(Resources / "artifact/open-dbc-dump-renderer.txt"));
    CHECK_FALSE(fs::exists(Resources / "artifact/selftest-final.png"));
    CHECK_FALSE(fs::exists(Resources / "artifact/.opendbc-project.json"));
    CHECK_FALSE(fs::exists(Resources / "artifact/build/generated.dat"));
    CHECK_FALSE(fs::exists(Resources / "export-hosts"));
    CHECK(fs::is_regular_file(Resources / "NOTICE.md"));
    CHECK(fs::is_regular_file(Resources / "licenses/MuseScore-General-SoundFont-LICENSE.md"));

    std::ifstream ManifestInput(Resources / "opendbc-artifact.json");
    const std::string Manifest{std::istreambuf_iterator<char>(ManifestInput), std::istreambuf_iterator<char>()};
    CHECK(Manifest.find("media/music.mid") != std::string::npos);
    CHECK(Manifest.find("media/music.mp3") != std::string::npos);

    const fs::path DataRoot = Directory.Root / "data";
    const dbc::interpreter::EmbeddedArtifactResult Embedded = dbc::interpreter::FindEmbeddedArtifact(Resources, DataRoot);
    INFO(Embedded.Error);
    REQUIRE(Embedded.Artifact.has_value());
    CHECK(Embedded.Artifact->ProgramPath.parent_path() == Embedded.Artifact->AssetRoot);
    CHECK(Embedded.Artifact->AssetRoot.string().starts_with(DataRoot.string()));

    // The runner path: the copied image runs without any BASIC source.
    dbc::interpreter::CommandLine Command;
    Command.Action = dbc::interpreter::CommandLineAction::Run;
    Command.Path = Embedded.Artifact->ProgramPath;
    Command.Headless = true;
    Command.Execution.AssetRoot = Embedded.Artifact->AssetRoot;
    std::string Output;
    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [&](std::string_view Text) { Output += Text; };
    Environment.StandardError = [&](std::string_view Text) { Output += Text; };
    CHECK(dbc::interpreter::RunProgramImage(Command, Environment) == 0);
    CHECK(Output.find("42") != std::string::npos);

    const toolchain::ArtifactExportResult Duplicate = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Duplicate);
    CHECK(Duplicate.Error.find("already exists") != std::string::npos);
}

TEST_CASE("DBPro artifact images carry the project startup instead of its descriptor")
{
    TemporaryDirectory Directory("opendbc-artifact-dbpro");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"professional\"\n");
    Write(Project / "game.dbpro", "version=DBP1.00\nmain=main.dba\n");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.OrderedSources = {Request.EntrySource};
    Request.ProjectFile = Project / "game.dbpro";
    Request.AssetRoot = Project;
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Settings.AppName = "DBPro Product";
    Request.Startup.Title = "Professional Title";
    Request.Startup.Width = 1024;
    Request.Startup.Height = 768;
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    const fs::path Resources = ExportResourceRoot(Result.OutputPath);
    CHECK_FALSE(fs::exists(Resources / "artifact/game.dbpro"));
    std::ifstream Input(Resources / "opendbc-artifact.json");
    const nlohmann::json Manifest = nlohmann::json::parse(Input);
    CHECK(Manifest.at("format") == dbc::interpreter::EmbeddedArtifactFormat);
    CHECK(Manifest.at("program") == "program.obc");
    std::ifstream ImageInput(Resources / "artifact/program.obc", std::ios::binary);
    const std::vector<std::uint8_t> Bytes{std::istreambuf_iterator<char>(ImageInput), std::istreambuf_iterator<char>()};
    const dbc::interpreter::ProgramImageReadResult Image = dbc::interpreter::ReadProgramImage(Bytes);
    INFO(Image.Error);
    REQUIRE(Image.Image);
    CHECK(Image.Image->Bytecode->Profile == dbc::language::LanguageProfile::DbPro);
    CHECK(Image.Image->Startup.Title == "Professional Title");
    CHECK(Image.Image->Startup.Width == 1024);
    CHECK(Image.Image->Startup.Height == 768);
}

TEST_CASE("artifact export rejects files that may contain secrets")
{
    TemporaryDirectory Directory("opendbc-artifact-secret");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    Write(Project / "passwords.txt", "not for publication\n");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("may contain secrets") != std::string::npos);
}

TEST_CASE("artifact export excludes only the explicitly configured Android keystore")
{
    TemporaryDirectory Directory("opendbc-artifact-keystore");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    Write(Project / "release.jks", "private key material\n");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Settings.AndroidKeystorePath = "release.jks";
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    CHECK_FALSE(fs::exists(ExportResourceRoot(Result.OutputPath) / "artifact/release.jks"));
}

TEST_CASE("artifact export rejects project symlinks")
{
    TemporaryDirectory Directory("opendbc-artifact-symlink");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    Write(Directory.Root / "outside.txt", "outside\n");
    std::error_code LinkError;
    fs::create_symlink(Directory.Root / "outside.txt", Project / "linked.txt", LinkError);
    if (LinkError)
        return;

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("symlinks") != std::string::npos);
}

TEST_CASE("artifact export confines relative and absolute includes to the project")
{
    TemporaryDirectory Directory("opendbc-artifact-include-confinement");
    const fs::path Project = Directory.Root / "project";
    const fs::path Outside = Directory.Root / "outside.dba";
    Write(Outside, "print \"outside\"\n");
    fs::create_directories(Directory.Root / "output");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Sign = false;

    SUBCASE("parent traversal")
    {
        Write(Request.EntrySource, "#include \"../outside.dba\"\n");
        const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
        CHECK_FALSE(Result);
        CHECK(Result.Error.find("remain inside its project") != std::string::npos);
    }
    SUBCASE("absolute path")
    {
        Write(Request.EntrySource, "#include \"" + Outside.generic_string() + "\"\n");
        const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
        CHECK_FALSE(Result);
        CHECK(Result.Error.find("remain inside its project") != std::string::npos);
    }
}

TEST_CASE("artifact export compiles includes from excluded directories without shipping them")
{
    TemporaryDirectory Directory("opendbc-artifact-excluded-include");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "#include \"dist/generated.dba\"\nprint value\n");
    Write(Project / "dist/generated.dba", "value=42\n");
    fs::create_directories(Directory.Root / "output");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Sign = false;

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    const fs::path Resources = ExportResourceRoot(Result.OutputPath);
    CHECK_FALSE(fs::exists(Resources / "artifact/dist"));
    CHECK(fs::is_regular_file(Resources / "artifact/program.obc"));
}

TEST_CASE("artifact export excludes an output subtree inside the project")
{
    TemporaryDirectory Directory("opendbc-artifact-output-subtree");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    Write(Project / "release/old-build.pak", "old artifact\n");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Project / "release";
    UseFakeHost(Request, Directory.Root);
    Request.Sign = false;

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    const fs::path Resources = ExportResourceRoot(Result.OutputPath);
    CHECK_FALSE(fs::exists(Resources / "artifact/release/old-build.pak"));
}

#if defined(__APPLE__)
TEST_CASE("macOS unsigned desktop artifact export refreshes its ad hoc signature")
{
    TemporaryDirectory Directory("opendbc-artifact-macos-unsigned");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    const fs::path Host = FakeHost(Directory.Root);
    const fs::path Executable = Host / "Contents/MacOS/opendbc-runner";
    fs::copy_file("/usr/bin/true", Executable, fs::copy_options::overwrite_existing);
    fs::permissions(Executable, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add);
    REQUIRE(RunTestProcess({"/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", Host.string()}) == 0);
    REQUIRE(fs::is_directory(Host / "Contents/_CodeSignature"));
    BindFakeRunner(Directory.Root);

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    Request.HostProduct = Host;
    Request.HostResources = FakeHostResources(Directory.Root);
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    CHECK(fs::is_directory(Result.OutputPath / "Contents/_CodeSignature"));
    CHECK(RunTestProcess({"/usr/bin/codesign", "--verify", "--deep", "--strict", Result.OutputPath.string()}) == 0);
}

TEST_CASE("macOS desktop artifact export applies the configured project icon")
{
    TemporaryDirectory Directory("opendbc-artifact-macos-icon");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");

    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Settings.IconSource = fs::path(OPENDBC_TEST_SOURCE_DIR) / "resources/editor.png";
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    INFO(Result.Error);
    REQUIRE(Result);
    CHECK(fs::is_regular_file(Result.OutputPath / "Contents/Resources/artifact.icns"));
}
#endif

TEST_CASE("embedded artifact manifest rejects path traversal")
{
    TemporaryDirectory Directory("opendbc-artifact-manifest");
    Write(Directory.Root / "opendbc-artifact.json",
          "{\"format\":6,\"program\":\"../program.obc\",\"storage\":\"artifact-test\","
          "\"files\":[{\"path\":\"../program.obc\",\"hash\":\"0000000000000000\"}]}\n");
    const dbc::interpreter::EmbeddedArtifactResult Result =
        dbc::interpreter::FindEmbeddedArtifact(Directory.Root, Directory.Root / "data");
    CHECK_FALSE(Result.Artifact.has_value());
    CHECK(Result.Error.find("relative .obc path") != std::string::npos);
}

TEST_CASE("embedded artifact manifest requires an exact payload inventory")
{
    TemporaryDirectory Directory("opendbc-artifact-inventory");
    Write(Directory.Root / "opendbc-artifact.json",
          "{\"format\":6,\"program\":\"program.obc\",\"storage\":\"artifact-test\","
          "\"files\":[{\"path\":\"media/pixel.png\",\"hash\":\"0000000000000000\"}]}\n");
    const dbc::interpreter::EmbeddedArtifactResult Result =
        dbc::interpreter::FindEmbeddedArtifact(Directory.Root, Directory.Root / "data");
    CHECK_FALSE(Result.Artifact.has_value());
    CHECK(Result.Error.find("does not contain its program image") != std::string::npos);
}

TEST_CASE("artifact payload synchronization preserves data changes and removes obsolete assets")
{
    TemporaryDirectory Directory("opendbc-artifact-sync");
    const fs::path Source = Directory.Root / "source";
    const fs::path Destination = Directory.Root / "destination";
    Write(Source / "save.dat", "initial\n");
    Write(Source / "obsolete.dat", "obsolete\n");
    std::vector<dbc::interpreter::detail::ArtifactFile> Initial = {
        {"save.dat", dbc::interpreter::detail::ArtifactContentHash("initial\n")},
        {"obsolete.dat", dbc::interpreter::detail::ArtifactContentHash("obsolete\n")},
    };
    std::string Error;
    REQUIRE(dbc::interpreter::detail::SynchronizeArtifactPayload(Source, Destination, Initial, Error));
    Write(Destination / "save.dat", "player progress\n");
    Write(Source / "save.dat", "new packaged default\n");

    const std::vector<dbc::interpreter::detail::ArtifactFile> Updated = {
        {"save.dat", dbc::interpreter::detail::ArtifactContentHash("new packaged default\n")},
    };
    REQUIRE(dbc::interpreter::detail::SynchronizeArtifactPayload(Source, Destination, Updated, Error));

    std::ifstream Save(Destination / "save.dat", std::ios::binary);
    const std::string Saved((std::istreambuf_iterator<char>(Save)), std::istreambuf_iterator<char>());
    CHECK(Saved == "player progress\n");
    CHECK_FALSE(fs::exists(Destination / "obsolete.dat"));
}

TEST_CASE("artifact payload synchronization rejects symlinked parent directories")
{
    TemporaryDirectory Directory("opendbc-artifact-sync-symlink");
    const fs::path Source = Directory.Root / "source";
    const fs::path Destination = Directory.Root / "destination";
    const fs::path Outside = Directory.Root / "outside";
    Write(Source / "media/value.dat", "packaged\n");
    fs::create_directories(Destination);
    fs::create_directories(Outside);
    std::error_code LinkError;
    fs::create_directory_symlink(Outside, Destination / "media", LinkError);
    if (LinkError)
        return;

    const std::vector<dbc::interpreter::detail::ArtifactFile> Files = {
        {"media/value.dat", dbc::interpreter::detail::ArtifactContentHash("packaged\n")},
    };
    std::string Error;
    CHECK_FALSE(dbc::interpreter::detail::SynchronizeArtifactPayload(Source, Destination, Files, Error));
    CHECK(Error.find("unsafe directory") != std::string::npos);
    CHECK_FALSE(fs::exists(Outside / "value.dat"));
}

TEST_CASE("artifact payload synchronization never follows the legacy predictable temporary name")
{
    TemporaryDirectory Directory("opendbc-artifact-sync-temp-symlink");
    const fs::path Source = Directory.Root / "source";
    const fs::path Destination = Directory.Root / "destination";
    const fs::path Outside = Directory.Root / "outside.txt";
    Write(Source / "save.dat", "packaged\n");
    Write(Outside, "must survive\n");
    fs::create_directories(Destination);
    std::error_code LinkError;
    fs::create_symlink(Outside, Destination / "save.dat.opendbc.tmp", LinkError);
    if (LinkError)
        return;

    const std::vector<dbc::interpreter::detail::ArtifactFile> Files = {
        {"save.dat", dbc::interpreter::detail::ArtifactContentHash("packaged\n")},
    };
    std::string Error;
    REQUIRE(dbc::interpreter::detail::SynchronizeArtifactPayload(Source, Destination, Files, Error));

    std::ifstream OutsideInput(Outside, std::ios::binary);
    const std::string OutsideText((std::istreambuf_iterator<char>(OutsideInput)), std::istreambuf_iterator<char>());
    CHECK(OutsideText == "must survive\n");
    CHECK(fs::is_symlink(Destination / "save.dat.opendbc.tmp"));
}

TEST_CASE("Android artifact export rejects invalid identity settings before packaging")
{
    TemporaryDirectory Directory("opendbc-artifact-android-settings");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    const fs::path Resources = Directory.Root / "resources";
    Write(Resources / "export-hosts/android/template/marker", "host\n");
    Write(Resources / "export-hosts/android/template/app/src/main/assets/shaders/dbclassic_tri.vert.spv",
          "vertex shader\n");
    Write(Resources / "export-hosts/android/template/app/src/main/assets/shaders/dbclassic_tri.frag.spv",
          "fragment shader\n");
    WriteAndroidLibrary(Resources);
    fs::create_directories(Directory.Root / "output");

    toolchain::ArtifactExportRequest Request;
    Request.Target = toolchain::ArtifactTarget::AndroidApk;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.OutputDirectory = Directory.Root / "output";
    Request.HostResources = Resources;
    Request.Settings.BundleId = "net..invalid";

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("Java identifier") != std::string::npos);
}

TEST_CASE("Android artifact export requires canonical shaders in its installed host")
{
    TemporaryDirectory Directory("opendbc-artifact-android-shaders");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    const fs::path Resources = Directory.Root / "resources";
    Write(Resources / "export-hosts/android/template/marker", "host\n");
    fs::create_directories(Directory.Root / "output");

    toolchain::ArtifactExportRequest Request;
    Request.Target = toolchain::ArtifactTarget::AndroidApk;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.OutputDirectory = Directory.Root / "output";
    Request.HostResources = Resources;

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("canonical shaders") != std::string::npos);
}

TEST_CASE("Android artifact export rejects a host library for the wrong ABI")
{
    TemporaryDirectory Directory("opendbc-artifact-android-abi");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    const fs::path Resources = Directory.Root / "resources";
    WriteAndroidLibrary(Resources);
    Write(Resources / "export-hosts/android/template/app/src/main/jniLibs/arm64-v8a/libmain.so", "not an ELF");
    fs::create_directories(Directory.Root / "output");

    toolchain::ArtifactExportRequest Request;
    Request.Target = toolchain::ArtifactTarget::AndroidApk;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.OutputDirectory = Directory.Root / "output";
    Request.HostResources = Resources;

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("ELF64 little-endian AArch64") != std::string::npos);
}

TEST_CASE("artifact export rejects a staging directory inside its host")
{
    TemporaryDirectory Directory("opendbc-artifact-recursion");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "print \"hello\"\n");
    const fs::path Host = Directory.Root / "export-hosts/android/template";
    Write(Host / "app/src/main/assets/shaders/dbclassic_tri.vert.spv", "vertex shader\n");
    Write(Host / "app/src/main/assets/shaders/dbclassic_tri.frag.spv", "fragment shader\n");
    WriteAndroidLibrary(Directory.Root);

    toolchain::ArtifactExportRequest Request;
    Request.Target = toolchain::ArtifactTarget::AndroidApk;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.OutputDirectory = Host / "exports";
    Request.HostResources = Directory.Root;
    fs::create_directories(Request.OutputDirectory);

    const toolchain::ArtifactExportResult Result = toolchain::ArtifactExportService().Export(Request);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("inside its source host") != std::string::npos);
}

TEST_CASE("artifact staging refuses a pre-existing symlink")
{
    TemporaryDirectory Directory("opendbc-artifact-stage-symlink");
    const fs::path Outside = Directory.Root / "outside";
    const fs::path Stage = Directory.Root / ".opendbc-stage-fixed";
    fs::create_directories(Outside);
    std::error_code LinkError;
    fs::create_directory_symlink(Outside, Stage, LinkError);
    if (LinkError)
        return;

    std::string Error;
    CHECK_FALSE(toolchain::detail::ClaimArtifactStageDirectory(Stage, Error));
    CHECK(Error == "Artifact staging path already exists");
    CHECK(fs::is_empty(Outside));
}

// Opaque icons are flattened with sips, so only macOS can produce them.
#if defined(__APPLE__)
TEST_CASE("iOS artifact icon generation removes the alpha channel")
{
    TemporaryDirectory Directory("opendbc-artifact-ios-icon");
    const fs::path Icon = Directory.Root / "AppIcon.png";
    std::string Error;
    REQUIRE(toolchain::detail::WriteArtifactIconPng(fs::path(OPENDBC_TEST_SOURCE_DIR) / "resources/editor.png", Icon,
                                                    180, true, Error));
    std::array<std::uint8_t, 26> Header{};
    std::ifstream Input(Icon, std::ios::binary);
    Input.read(reinterpret_cast<char*>(Header.data()), static_cast<std::streamsize>(Header.size()));
    REQUIRE(Input.gcount() == static_cast<std::streamsize>(Header.size()));
    CHECK((Header[25] == 2 || Header[25] == 3));
}
#endif

TEST_CASE("mobile host identity binds its runtime manifest to its binary")
{
    TemporaryDirectory Directory("opendbc-artifact-host-identity");
    const fs::path Runtime = Directory.Root / "opendbc-runtime.json";
    const fs::path Binary = Directory.Root / "opendbc";
    const fs::path Host = Directory.Root / "opendbc-host.json";
    Write(Runtime, "{\"fingerprint\":\"runtime\"}\n");
    Write(Binary, "linked binary\n");
    const std::string RuntimeHash = *toolchain::detail::ArtifactSha256File(Runtime);
    const std::string BinaryHash = *toolchain::detail::ArtifactSha256File(Binary);
    Write(Host, "{\"format\":1,\"runtime_manifest_sha256\":\"" + RuntimeHash + "\",\"binary_sha256\":\"" + BinaryHash +
                    "\"}\n");

    std::string Error;
    REQUIRE(toolchain::detail::ValidateArtifactHostIdentity(Host, Binary, Runtime, Error));
    Write(Binary, "mutated binary\n");
    CHECK_FALSE(toolchain::detail::ValidateArtifactHostIdentity(Host, Binary, Runtime, Error));
}

TEST_CASE("artifact SHA-256 matches the standard abc vector")
{
    TemporaryDirectory Directory("opendbc-artifact-sha256");
    const fs::path Value = Directory.Root / "value.txt";
    Write(Value, "abc");
    REQUIRE(toolchain::detail::ArtifactSha256File(Value).has_value());
    CHECK(*toolchain::detail::ArtifactSha256File(Value) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("artifact host shader validation rejects missing or changed shaders")
{
    TemporaryDirectory Directory("opendbc-artifact-shaders");
    const fs::path Product = fs::path(OPENDBC_TEST_SOURCE_DIR) / "shaders";
    const fs::path Host = Directory.Root / "shaders";
    fs::create_directories(Host);
    fs::copy_file(Product / "dbclassic_tri.vert.spv", Host / "dbclassic_tri.vert.spv");
    fs::copy_file(Product / "dbclassic_tri.frag.spv", Host / "dbclassic_tri.frag.spv");
    std::string Error;
    CHECK_FALSE(toolchain::detail::ValidateArtifactShaders(Host, Product, Error));
    fs::copy_file(Product / "vr_transfer.vert.spv", Host / "vr_transfer.vert.spv");
    fs::copy_file(Product / "vr_transfer.frag.spv", Host / "vr_transfer.frag.spv");
    CHECK_FALSE(toolchain::detail::ValidateArtifactShaders(Host, Product, Error));
    for (const std::string_view Name :
         {"dbclassic_tri.vert.msl", "dbclassic_tri.frag.msl", "vr_transfer.vert.msl", "vr_transfer.frag.msl"})
        fs::copy_file(Product / Name, Host / Name);
    REQUIRE(toolchain::detail::ValidateArtifactShaders(Host, Product, Error));
    Write(Host / "dbclassic_tri.vert.msl", "changed");
    CHECK_FALSE(toolchain::detail::ValidateArtifactShaders(Host, Product, Error));
    fs::copy_file(Product / "dbclassic_tri.vert.msl", Host / "dbclassic_tri.vert.msl",
                  fs::copy_options::overwrite_existing);
    Write(Host / "vr_transfer.frag.spv", "changed");
    CHECK_FALSE(toolchain::detail::ValidateArtifactShaders(Host, Product, Error));
    fs::copy_file(Product / "vr_transfer.frag.spv", Host / "vr_transfer.frag.spv", fs::copy_options::overwrite_existing);
    Write(Host / "dbclassic_tri.frag.spv", "changed");
    CHECK_FALSE(toolchain::detail::ValidateArtifactShaders(Host, Product, Error));
}

TEST_CASE("Android host dependency inventory rejects non-system libraries")
{
    TemporaryDirectory Directory("opendbc-artifact-android-needed");
    const fs::path Manifest = Directory.Root / "opendbc-host.json";
    Write(Manifest, "{\"needed\":[\"libc.so\",\"libm.so\"]}\n");
    std::string Error;
    REQUIRE(toolchain::detail::ValidateAndroidSystemDependencies(Manifest, Error));
    Write(Manifest, "{\"needed\":[\"libc.so\",\"libavcodec.so\"]}\n");
    CHECK_FALSE(toolchain::detail::ValidateAndroidSystemDependencies(Manifest, Error));
}

TEST_CASE("DBPro artifacts execute ordered sources with independent media and include roots")
{
    TemporaryDirectory Directory("opendbc-artifact-multisource");
    const fs::path Project = Directory.Root / "project";
    Write(Project / "main.dba", "#include \"include/shared.dba\"\nprint answer() + offset\nend\n");
    Write(Project / "include/shared.dba", "offset=2\n");
    Write(Directory.Root / "external/answer.dba", "function answer()\nendfunction 40\n");
    Write(Directory.Root / "media/pixel.txt", "media");
    Write(Directory.Root / "media/signing.bin", "private signing material");
    toolchain::ArtifactExportRequest Request;
    Request.ProjectRoot = Project;
    Request.EntrySource = Project / "main.dba";
    Request.Profile = dbc::language::LanguageProfile::DbPro;
    Request.OrderedSources = {Request.EntrySource, Directory.Root / "external/answer.dba"};
    Request.AssetRoot = Directory.Root / "media";
    Request.Settings.AndroidKeystorePath = "../media/signing.bin";
    Request.OutputDirectory = Directory.Root / "output";
    UseFakeHost(Request, Directory.Root);
    Request.Sign = false;
    fs::create_directories(Request.OutputDirectory);
    const auto Exported = toolchain::ArtifactExportService().Export(Request);
    INFO(Exported.Error);
    REQUIRE(Exported);
    const auto Resources = ExportResourceRoot(Exported.OutputPath);
    CHECK_FALSE(fs::exists(Resources / "artifact/__external/assets/signing.bin"));
    std::ifstream ManifestInput(Resources / "opendbc-artifact.json");
    auto Manifest = nlohmann::json::parse(ManifestInput);
    CHECK(Manifest.dump().find("signing.bin") == std::string::npos);
    CHECK_FALSE(fs::exists(Resources / "artifact/__external/sources"));
    const auto Embedded = dbc::interpreter::FindEmbeddedArtifact(Resources, Directory.Root / "data");
    INFO(Embedded.Error);
    REQUIRE(Embedded.Artifact);
    CHECK(Embedded.Artifact->PayloadRoot != Embedded.Artifact->AssetRoot);
    CHECK(fs::is_regular_file(Embedded.Artifact->AssetRoot / "pixel.txt"));
    dbc::interpreter::CommandLine Command;
    Command.Action = dbc::interpreter::CommandLineAction::Run;
    Command.Path = Embedded.Artifact->ProgramPath;
    Command.Headless = true;
    Command.Execution.AssetRoot = Embedded.Artifact->AssetRoot;
    dbc::interpreter::CommandLineEnvironment Environment;
    std::string Output;
    Environment.StandardOutput = [&](std::string_view Text) { Output += Text; };
    Environment.StandardError = [&](std::string_view Text) { Output += Text; };
    CHECK(dbc::interpreter::RunProgramImage(Command, Environment) == 0);
    CHECK(Output.find("42") != std::string::npos);

    // A tampered payload no longer matches its inventory.
    Write(Resources / "artifact/program.obc", "tampered");
    CHECK_FALSE(dbc::interpreter::FindEmbeddedArtifact(Resources, Directory.Root / "data2").Artifact);
}

TEST_CASE("artifact manifest format requires a bounded integer")
{
    TemporaryDirectory Directory("opendbc-artifact-format");
    for (const auto& Format : {nlohmann::json("4"), nlohmann::json(nullptr), nlohmann::json::object(),
                               nlohmann::json(4.0), nlohmann::json(UINT64_MAX)})
    {
        Write(Directory.Root / "opendbc-artifact.json", nlohmann::json{{"format", Format}}.dump());
        const auto Result = dbc::interpreter::FindEmbeddedArtifact(Directory.Root, Directory.Root / "data");
        CHECK_FALSE(Result.Artifact);
        CHECK_FALSE(Result.Error.empty());
    }
}

TEST_CASE("atomic descriptor publication does not follow a planted temporary symlink")
{
    TemporaryDirectory Directory("opendbc-atomic-descriptor");
    const auto Destination = Directory.Root / "game.dbpro";
    const auto Sentinel = Directory.Root / "sentinel.txt";
    Write(Sentinel, "unchanged");
    std::error_code LinkError;
    fs::create_symlink(Sentinel, Destination.string() + ".opendbc.tmp", LinkError);
    if (LinkError)
    {
        MESSAGE("Symlinks are unavailable: " << LinkError.message());
        return;
    }
    std::string Error;
    CHECK_FALSE(dbc::interpreter::AtomicWriteText({}, "invalid"));
    REQUIRE(dbc::interpreter::AtomicWriteText(Destination, "version=DBP1.00\n", &Error));
    std::ifstream Input(Sentinel);
    CHECK(std::string(std::istreambuf_iterator<char>(Input), {}) == "unchanged");
    CHECK(fs::is_symlink(Destination.string() + ".opendbc.tmp"));
}
