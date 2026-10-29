#include <doctest/doctest.h>

#include "core/settings.hpp"
#include "interpreter/embedded_artifact.hpp"
#include "path_utf8.hpp"
#include "toolchain/artifact_export_service.hpp"
#include "toolchain/export_readiness.hpp"
#include "toolchain/toolchain_locator.hpp"

#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <fstream>
#include <memory>

using namespace dbc::studio;

namespace
{
    namespace fs = std::filesystem;

    std::vector<std::uint8_t> VersionBytes(const fs::path& File)
    {
        DWORD Ignored = 0;
        const DWORD Size = GetFileVersionInfoSizeW(File.c_str(), &Ignored);
        REQUIRE(Size > 0);
        std::vector<std::uint8_t> Bytes(Size);
        REQUIRE(GetFileVersionInfoW(File.c_str(), 0, Size, Bytes.data()));
        return Bytes;
    }

    std::wstring VersionString(const std::vector<std::uint8_t>& Bytes, const wchar_t* Name)
    {
        const std::wstring Key = std::wstring(L"\\StringFileInfo\\040904b0\\") + Name;
        void* Value = nullptr;
        UINT Length = 0;
        REQUIRE(VerQueryValueW(Bytes.data(), Key.c_str(), &Value, &Length));
        REQUIRE(Length > 0);
        return std::wstring(static_cast<const wchar_t*>(Value), Length - 1);
    }
} // namespace

TEST_CASE("Windows Studio exposes the configured product version")
{
    const auto File = dbc::PathFromUtf8(OPENDBC_TEST_STUDIO);
    const auto Bytes = VersionBytes(File);
    CHECK(VersionString(Bytes, L"ProductName") == L"OpenDBC");
    CHECK(VersionString(Bytes, L"FileDescription") == L"OpenDBC Studio");
    CHECK(VersionString(Bytes, L"OriginalFilename") == L"opendbc.exe");
    CHECK(VersionString(Bytes, L"ProductVersion") == dbc::PathFromUtf8(OPENDBC_TEST_VERSION).native());
}

namespace
{
    void ExerciseWindowsExport(bool Sign)
    {
        const fs::path Root =
            fs::temp_directory_path() /
            ("opendbc-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        struct Cleanup
        {
            fs::path Path;

            ~Cleanup()
            {
                std::error_code Error;
                fs::remove_all(Path, Error);
            }
        } Owner{Root};

        const fs::path Project = Root / fs::path(u8"projet-\u65e5\u672c-\U0001f3ae");
        fs::create_directories(Project);
        const fs::path Asset(u8"r\u00e9sultat.txt");
        {
            std::ofstream Source(Project / "main.dba", std::ios::binary);
            Source << "make file \"" << dbc::PathToUtf8(Asset) << "\"\nend\n";
            REQUIRE(Source.good());
        }
        toolchain::ArtifactExportRequest Request;
        Request.ProjectRoot = Project;
        Request.EntrySource = Project / "main.dba";
        Request.OutputDirectory = Root / fs::path(u8"export-\u00e9");
        fs::create_directories(Request.OutputDirectory);
        Request.HostProduct = dbc::PathFromUtf8(OPENDBC_TEST_RUNNER);
        Request.HostResources = dbc::PathFromUtf8(OPENDBC_TEST_STUDIO).parent_path();
        Request.Settings.AppName = "Unicode Game";
        Request.Settings.VersionName = "1.2.3";
        Request.Settings.VersionCode = 42;
        Request.Metadata.Company = "Caf\xc3\xa9";
        Request.Sign = Sign;
        if (Sign)
        {
            Request.Toolchain = toolchain::ResolveExportToolchain(core::Settings{}, toolchain::ToolchainSearch::Host());
            REQUIRE_FALSE(Request.Toolchain.WindowsCertificate.empty());
        }
        const auto Result = toolchain::ArtifactExportService().Export(Request);
        REQUIRE_MESSAGE(Result, Result.Error);
        const fs::path Executable = Result.OutputPath / "Unicode Game.exe";
        const auto Bytes = VersionBytes(Executable);
        CHECK(VersionString(Bytes, L"CompanyName") == L"Caf\u00e9");
        CHECK(VersionString(Bytes, L"ProductName") == L"Unicode Game");
        CHECK(VersionString(Bytes, L"FileVersion") == L"1.2.3.42");
        CHECK(VersionString(Bytes, L"OriginalFilename") == L"Unicode Game.exe");
        void* FixedValue = nullptr;
        UINT FixedSize = 0;
        REQUIRE(VerQueryValueW(Bytes.data(), L"\\", &FixedValue, &FixedSize));
        REQUIRE(FixedSize == sizeof(VS_FIXEDFILEINFO));
        const auto* Fixed = static_cast<const VS_FIXEDFILEINFO*>(FixedValue);
        CHECK(Fixed->dwFileVersionMS == ((1u << 16) | 2u));
        CHECK(Fixed->dwFileVersionLS == ((3u << 16) | 42u));

        HMODULE Module = LoadLibraryExW(Executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        REQUIRE(Module);
        const std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> ModuleOwner(Module, FreeLibrary);
        const HRSRC Group = FindResourceW(Module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14));
        CHECK(Group);
        if (Group)
        {
            const auto* Data = static_cast<const WORD*>(LockResource(LoadResource(Module, Group)));
            REQUIRE(Data);
            CHECK(Data[2] == 5);
            for (int Id = 1; Id <= 5; ++Id)
                CHECK(FindResourceW(Module, MAKEINTRESOURCEW(Id), MAKEINTRESOURCEW(3)));
        }
        const auto Embedded = dbc::interpreter::FindEmbeddedArtifact(Result.OutputPath);
        REQUIRE_MESSAGE(Embedded.Artifact, Embedded.Error);
        Cleanup PayloadOwner{Embedded.Artifact->PayloadRoot};
        CHECK_FALSE(fs::exists(Embedded.Artifact->AssetRoot / Asset));
        toolchain::ProcessRequest Process;
        Process.Arguments = {dbc::PathToUtf8(Executable)};
        Process.WorkingDirectory = Result.OutputPath;
        Process.Environment = {{"OPENDBC_HEADLESS", "1"}, {"SDL_ASSERT", "abort"}};
        const auto Run = toolchain::RunProcess(Process, {}, nullptr, true);
        REQUIRE_MESSAGE(Run, Run.Error);
        CHECK(fs::is_regular_file(Embedded.Artifact->AssetRoot / Asset));

        // SignTool uses the same Authenticode policy for the runner and its bundled loader.
        if (const auto Signtool = toolchain::LocateSigntool(toolchain::ToolchainSearch::Host()))
        {
            for (const fs::path& File : {Executable, Result.OutputPath / "openxr_loader.dll"})
            {
                Process.Arguments = {dbc::PathToUtf8(*Signtool), "verify", "/pa", dbc::PathToUtf8(File)};
                const auto Verification = toolchain::RunProcess(Process, {}, nullptr, true);
                if (Sign)
                    CHECK_MESSAGE(Verification, Verification.Error);
                else if (File == Executable)
                {
                    CHECK_FALSE(Verification);
                    CHECK(Verification.ExitCode == 1);
                }
            }
        }
    }

} // namespace

TEST_CASE("Windows exports customize PE resources and run verified bytecode from Unicode folders")
{
    ExerciseWindowsExport(false);
}

TEST_CASE("Windows signed exports pass Authenticode verification" * doctest::skip())
{
    ExerciseWindowsExport(true);
}
