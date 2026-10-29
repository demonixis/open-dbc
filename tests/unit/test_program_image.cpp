#include <doctest/doctest.h>

#include "engine/systems/engine_kernel.hpp"
#include "interpreter/command_line.hpp"
#include "interpreter/program_image.hpp"
#include "vm/bytecode.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace interpreter = dbc::interpreter;

    constexpr std::string_view KProgram = "type point\n"
                                          "  x as integer\n"
                                          "  y as float\n"
                                          "endtype\n"
                                          "dim p(3) as point\n"
                                          "p(1).x = add(2, 3)\n"
                                          "read greeting$, count, ratio#\n"
                                          "print greeting$; p(1).x; count\n"
                                          "end\n"
                                          "data \"hello\", 3, 2.5\n"
                                          "function add(a, b)\n"
                                          "endfunction a + b\n";

    struct TemporaryDirectory
    {
        TemporaryDirectory()
        {
            const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            Root = std::filesystem::temp_directory_path() / ("opendbc-program-image-" + std::to_string(Suffix));
            std::filesystem::create_directories(Root);
        }

        ~TemporaryDirectory()
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Root, Ignored);
        }

        std::filesystem::path Root{};
    };

    interpreter::ProgramBuild Build(const std::filesystem::path& Entry, std::string_view Text)
    {
        interpreter::ExecutionRequest Request;
        Request.RootLogicalPath = Entry;
        Request.Profile = dbc::language::LanguageProfile::DbPro;
        Request.Sources.push_back({Entry, std::string(Text)});
        return interpreter::BuildProgram(std::move(Request));
    }

    std::vector<std::uint8_t> ReadFile(const std::filesystem::path& Path)
    {
        std::ifstream Stream(Path, std::ios::binary);
        return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("program images round-trip deterministically with relative source spans")
{
    TemporaryDirectory Directory;
    const interpreter::ProgramBuild Built = Build(Directory.Root / "game/main.dba", KProgram);
    REQUIRE(Built.Bytecode);

    interpreter::ProjectStartup Startup;
    Startup.Title = "Image";
    Startup.WindowMode = interpreter::ProjectWindowMode::Desktop;
    Startup.Width = 800;
    Startup.Height = 600;
    Startup.Depth = 16;
    Startup.CommandLine = "-level 2";
    const std::vector<std::uint8_t> Image =
        interpreter::WriteProgramImage(*Built.Bytecode, Startup, Directory.Root / "game", "test-runtime");

    const interpreter::ProgramImageReadResult Read = interpreter::ReadProgramImage(Image, "test-runtime");
    INFO(Read.Error);
    REQUIRE(Read.Image);
    const dbc::vm::Module& Loaded = *Read.Image->Bytecode;
    CHECK(Loaded.Profile == dbc::language::LanguageProfile::DbPro);
    CHECK(Loaded.LogicalPath == "main.dba");
    CHECK(Loaded.Types.size() == Built.Bytecode->Types.size());
    CHECK(Loaded.Data.size() == 3);
    CHECK(Loaded.Functions.size() == Built.Bytecode->Functions.size());
    CHECK(Loaded.Strings == Built.Bytecode->Strings);
    CHECK(Read.Image->Startup.Title == "Image");
    CHECK(Read.Image->Startup.WindowMode == interpreter::ProjectWindowMode::Desktop);
    CHECK(Read.Image->Startup.Width == 800);
    CHECK(Read.Image->Startup.Height == 600);
    CHECK(Read.Image->Startup.Depth == 16);
    CHECK(Read.Image->Startup.CommandLine == "-level 2");

    const auto HasLine = [&](std::uint32_t Line)
    {
        return std::any_of(
            Loaded.Functions.begin(), Loaded.Functions.end(),
            [&](const dbc::vm::Function& Function)
            {
                return std::any_of(
                    Function.Code.begin(), Function.Code.end(), [&](const dbc::vm::Instruction& Instruction)
                    { return Instruction.Span.Begin.Line == Line && Instruction.Span.LogicalPath == "main.dba"; });
            });
    };
    CHECK(HasLine(6));
    CHECK(HasLine(12));
    CHECK(interpreter::VerifyBytecode(Loaded).empty());

    // Writing the loaded module again reproduces the image byte for byte.
    CHECK(interpreter::WriteProgramImage(Loaded, Read.Image->Startup, {}, "test-runtime") == Image);
    const std::string Text(Image.begin(), Image.end());
    CHECK(Text.find(Directory.Root.filename().string()) == std::string::npos);
}

TEST_CASE("program images reject foreign runtimes, other formats and corruption")
{
    TemporaryDirectory Directory;
    const interpreter::ProgramBuild Built = Build(Directory.Root / "main.dba", KProgram);
    REQUIRE(Built.Bytecode);
    const std::vector<std::uint8_t> Image =
        interpreter::WriteProgramImage(*Built.Bytecode, {}, Directory.Root, "test-runtime");
    REQUIRE(interpreter::ReadProgramImage(Image, "test-runtime").Image);

    const interpreter::ProgramImageReadResult Foreign = interpreter::ReadProgramImage(Image, "other-runtime");
    CHECK_FALSE(Foreign.Image);
    CHECK(Foreign.Error.find("another OpenDBC runtime") != std::string::npos);

    std::vector<std::uint8_t> Modified = Image;
    Modified[0] = 'X';
    CHECK(interpreter::ReadProgramImage(Modified, "test-runtime").Error == "not an OpenDBC program image");
    Modified = Image;
    Modified[8] = 2;
    CHECK(interpreter::ReadProgramImage(Modified, "test-runtime").Error.find("unsupported program image format") == 0);
    Modified = Image;
    Modified.push_back(0);
    CHECK_FALSE(interpreter::ReadProgramImage(Modified, "test-runtime").Image);

    for (std::size_t Size = 0; Size < Image.size(); ++Size)
    {
        const std::span<const std::uint8_t> Prefix(Image.data(), Size);
        CAPTURE(Size);
        CHECK_FALSE(interpreter::ReadProgramImage(Prefix, "test-runtime").Image);
    }
}

TEST_CASE("Prepare verifies program image bytecode and runs it without sources")
{
    TemporaryDirectory Directory;
    const interpreter::ProgramBuild Built = Build(Directory.Root / "main.dba", KProgram);
    REQUIRE(Built.Bytecode);
    const interpreter::ProgramImageReadResult Read = interpreter::ReadProgramImage(
        interpreter::WriteProgramImage(*Built.Bytecode, {}, Directory.Root, "test-runtime"), "test-runtime");
    REQUIRE(Read.Image);

    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("program-image", 32, 32));
    interpreter::ExecutionService Service(Kernel);

    auto Tampered = std::make_shared<dbc::vm::Module>(*Read.Image->Bytecode);
    Tampered->EntryFunction = Tampered->Functions.size();
    interpreter::ExecutionRequest Rejected;
    Rejected.RootLogicalPath = Directory.Root / "main.obc";
    Rejected.Profile = Tampered->Profile;
    Rejected.Bytecode = Tampered;
    CHECK_FALSE(Service.Prepare(std::move(Rejected)));
    REQUIRE_FALSE(Service.Result().Diagnostics.empty());
    CHECK(Service.Result().Diagnostics.front().Stage == interpreter::DiagnosticStage::Verifier);

    std::string Output;
    interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = Directory.Root / "main.obc";
    Request.Profile = Read.Image->Bytecode->Profile;
    Request.Bytecode = Read.Image->Bytecode;
    Request.Options.Output = [&](std::string_view Text) { Output += Text; };
    REQUIRE(Service.Prepare(std::move(Request)));
    while (!Service.Result().Finished())
        Service.Tick();
    CHECK(Service.Result().Success());
    CHECK(Output.find("hello") != std::string::npos);
    CHECK(Output.find('5') != std::string::npos);
}

TEST_CASE("build writes a program image that run executes and reports with relative lines")
{
    const std::array<std::string_view, 3> MissingOutput{"build", "main.dba", "--headless"};
    CHECK_FALSE(interpreter::ParseCommandLine(MissingOutput));
    const std::array<std::string_view, 4> WrongExtension{"build", "main.dba", "--output", "main.bin"};
    CHECK(interpreter::ParseCommandLine(WrongExtension).Error == "Build requires --output <program.obc>");
    const std::array<std::string_view, 6> WithTarget{"build",    "main.dba", "--output",
                                                     "main.obc", "--target", "desktop"};
    CHECK_FALSE(interpreter::ParseCommandLine(WithTarget));

    TemporaryDirectory Directory;
    const std::filesystem::path Source = Directory.Root / "main.dba";
    std::ofstream(Source, std::ios::binary) << "print \"start\"\ndim a(5)\na(10) = 1\n";
    const std::string SourcePath = Source.string();
    const std::string ImagePath = (Directory.Root / "main.obc").string();

    std::string StandardOutput;
    std::string StandardError;
    interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [&](std::string_view Text) { StandardOutput += Text; };
    Environment.StandardError = [&](std::string_view Text) { StandardError += Text; };
    const std::array<std::string_view, 6> BuildImage{"build", SourcePath, "--dialect", "dbc", "--output", ImagePath};
    REQUIRE(interpreter::DispatchCommandLine(BuildImage, Environment) ==
            static_cast<int>(interpreter::CommandLineExitCode::Success));
    REQUIRE(std::filesystem::is_regular_file(ImagePath));
    CHECK(interpreter::ReadProgramImage(ReadFile(ImagePath)).Image);

    const std::array<std::string_view, 2> CheckImage{"check", ImagePath};
    CHECK(interpreter::DispatchCommandLine(CheckImage, Environment) ==
          static_cast<int>(interpreter::CommandLineExitCode::Target));
    const std::array<std::string_view, 4> WrongDialect{"run", ImagePath, "--dialect", "dbpro"};
    CHECK(interpreter::DispatchCommandLine(WrongDialect, Environment) ==
          static_cast<int>(interpreter::CommandLineExitCode::Target));

    std::filesystem::remove(Source);
    StandardOutput.clear();
    StandardError.clear();
    const std::array<std::string_view, 3> RunImage{"run", ImagePath, "--headless"};
    CHECK(interpreter::DispatchCommandLine(RunImage, Environment) ==
          static_cast<int>(interpreter::CommandLineExitCode::Runtime));
    CHECK(StandardOutput.find("start") != std::string::npos);
    CHECK(StandardError.find("main.dba:3:1: DBC5000: Array index is out of bounds") == 0);
}
