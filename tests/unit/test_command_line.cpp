#include <doctest/doctest.h>

#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "interpreter/command_line.hpp"
#include "language/language_catalogue.hpp"

#include <SDL3/SDL_surface.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    struct TemporarySource
    {
        explicit TemporarySource(std::string_view Text)
        {
            const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            Path = std::filesystem::temp_directory_path() / ("opendbc-command-line-" + std::to_string(Suffix) + ".dba");
            std::ofstream Output(Path, std::ios::binary);
            Output << Text;
        }

        ~TemporarySource()
        {
            std::error_code Ignored;
            std::filesystem::remove(Path, Ignored);
        }

        std::filesystem::path Path{};
    };

    struct TemporaryProject
    {
        TemporaryProject()
        {
            const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            Root = std::filesystem::temp_directory_path() / ("opendbc-command-project-" + std::to_string(Suffix));
            std::filesystem::create_directories(Root);
        }

        ~TemporaryProject()
        {
            std::error_code Ignored;
            std::filesystem::remove_all(Root, Ignored);
        }

        void Write(std::string_view Name, std::string_view Text) const
        {
            std::ofstream Output(Root / Name, std::ios::binary);
            Output << Text;
        }

        std::filesystem::path Root{};
    };
} // namespace

TEST_CASE("command line selects Studio without initializing a runtime")
{
    const dbc::interpreter::CommandLineParseResult Empty = dbc::interpreter::ParseCommandLine({});
    REQUIRE(Empty);
    CHECK(Empty.Command->Action == dbc::interpreter::CommandLineAction::Studio);
    CHECK_FALSE(Empty.Command->Path.has_value());

    const std::array<std::string_view, 1> Bare{"project/example.dba"};
    const dbc::interpreter::CommandLineParseResult Path = dbc::interpreter::ParseCommandLine(Bare);
    REQUIRE(Path);
    CHECK(Path.Command->Action == dbc::interpreter::CommandLineAction::Studio);
    CHECK(Path.Command->Path == std::filesystem::path("project/example.dba"));
}

TEST_CASE("command line parses run options and rejects invalid combinations")
{
    const std::array<std::string_view, 16> Run{"run",
                                               "program.dbpro",
                                               "--asset-root",
                                               "assets",
                                               "--log-directory",
                                               "logs",
                                               "--max-frames",
                                               "5",
                                               "--benchmark",
                                               "--debug-overlay",
                                               "--command-line",
                                               "--race ai",
                                               "--builtin-coverage-output",
                                               "coverage.json",
                                               "--benchmark-output",
                                               "benchmark.json"};
    const dbc::interpreter::CommandLineParseResult Parsed = dbc::interpreter::ParseCommandLine(Run);
    REQUIRE(Parsed);
    CHECK(Parsed.Command->Action == dbc::interpreter::CommandLineAction::Run);
    CHECK(Parsed.Command->Execution.AssetRoot == "assets");
    CHECK(Parsed.Command->LogDirectory == "logs");
    CHECK(Parsed.Command->Execution.MaxFrames == 5);
    CHECK(Parsed.Command->Execution.Benchmark);
    CHECK(Parsed.Command->DebugOverlay);
    REQUIRE(Parsed.Command->CommandLineOverride.has_value());
    CHECK(*Parsed.Command->CommandLineOverride == "--race ai");
    CHECK(Parsed.Command->Execution.BuiltinCoverageOutputPath == "coverage.json");
    CHECK(Parsed.Command->Execution.BenchmarkOutputPath == "benchmark.json");

    const std::array<std::string_view, 4> ZeroBudget{"run", "program.dba", "--instruction-budget", "0"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(ZeroBudget));

    const std::array<std::string_view, 3> JsonCheck{"check", "program.dba", "--json"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(JsonCheck));

    const std::array<std::string_view, 1> Missing{"run"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(Missing));

    const std::array<std::string_view, 4> BenchmarkOutputWithoutBenchmark{"run", "program.dba", "--benchmark-output",
                                                                          "benchmark.json"};
    const dbc::interpreter::CommandLineParseResult MissingBenchmark =
        dbc::interpreter::ParseCommandLine(BenchmarkOutputWithoutBenchmark);
    REQUIRE_FALSE(MissingBenchmark);
    CHECK(MissingBenchmark.Error == "--benchmark-output requires --benchmark");

    const std::array<std::string_view, 7> ConflictingOutputs{"run",           "program.dba",
                                                             "--benchmark",   "--benchmark-output",
                                                             "evidence.json", "--builtin-coverage-output",
                                                             "evidence.json"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(ConflictingOutputs));

    const std::string AbsoluteEvidence = std::filesystem::absolute("evidence.json").string();
    const std::array<std::string_view, 7> AliasedOutputs{"run",           "program.dba",
                                                         "--benchmark",   "--benchmark-output",
                                                         "evidence.json", "--builtin-coverage-output",
                                                         AbsoluteEvidence};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(AliasedOutputs));

    const std::array<std::string_view, 6> LogCoverageCollision{
        "run", "program.dba", "--log-output", "evidence.json", "--builtin-coverage-output", "evidence.json"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(LogCoverageCollision));

    const std::array<std::string_view, 7> LogBenchmarkAlias{
        "run", "program.dba", "--benchmark", "--log-output", "evidence.json", "--benchmark-output", AbsoluteEvidence};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(LogBenchmarkAlias));

    const std::array<std::string_view, 4> CommandLineCheck{"check", "program.dba", "--command-line", "value"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(CommandLineCheck));

    const std::array<std::string_view, 4> EmptyCoverage{"run", "program.dba", "--builtin-coverage-output", ""};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(EmptyCoverage));

    const std::array<std::string_view, 5> EmptyBenchmark{"run", "program.dba", "--benchmark", "--benchmark-output", ""};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(EmptyBenchmark));
}

TEST_CASE("command line requires an explicit artifact target and output")
{
    const std::array<std::string_view, 7> Export{"export",   "program.dbpro", "--target",  "android-aab",
                                                 "--output", "dist",          "--unsigned"};
    const dbc::interpreter::CommandLineParseResult Parsed = dbc::interpreter::ParseCommandLine(Export);
    REQUIRE(Parsed);
    CHECK(Parsed.Command->Action == dbc::interpreter::CommandLineAction::Export);
    CHECK(Parsed.Command->Path == "program.dbpro");
    CHECK(Parsed.Command->ExportTarget == "android-aab");
    CHECK(Parsed.Command->ExportOutput == "dist");
    CHECK_FALSE(Parsed.Command->ExportSign);

    const std::array<std::string_view, 4> MissingOutput{"export", "program.dba", "--target", "desktop"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(MissingOutput));

    const std::array<std::string_view, 4> ForeignOption{"check", "program.dba", "--output", "dist"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(ForeignOption));

    const std::array<std::string_view, 3> UnsignedRun{"run", "program.dba", "--unsigned"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(UnsignedRun));
}

TEST_CASE("command line exports iOS as an Xcode project, without a separate simulator target")
{
    const std::array<std::string_view, 6> Arguments = {"export", "program.dba", "--target", "ios", "--output", "dist"};
    const auto Parsed = dbc::interpreter::ParseCommandLine(Arguments);
    REQUIRE(Parsed.Command.has_value());
    CHECK(Parsed.Command->ExportTarget == "ios");

    const std::array<std::string_view, 6> Simulator = {"export",        "program.dba", "--target",
                                                       "ios-simulator", "--output",    "dist"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(Simulator));
}

TEST_CASE("command line dispatch uses stable help and failure exit codes")
{
    std::string Output;
    std::string Error;
    const dbc::interpreter::CommandLineEnvironment Environment{
        [&](std::string_view Text) { Output += Text; }, [&](std::string_view Text) { Error += Text; }, {}};

    const std::array<std::string_view, 1> Help{"--help"};
    CHECK(dbc::interpreter::DispatchCommandLine(Help, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    CHECK(Output.find("opendbc run") != std::string::npos);
    CHECK(Output.find("--builtin-coverage-output") != std::string::npos);
    CHECK(Output.find("--benchmark-output") != std::string::npos);

    Output.clear();
    const std::array<std::string_view, 1> Version{"--version"};
    CHECK(dbc::interpreter::DispatchCommandLine(Version, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    CHECK(Output == dbc::interpreter::CommandLineVersion());
    CHECK(Output.starts_with("OpenDBC 0."));
    const std::array<std::string_view, 3> RunVersion{"run", "program.obc", "--version"};
    CHECK(dbc::interpreter::ParseCommandLine(RunVersion).VersionRequested);

    const std::array<std::string_view, 1> Invalid{"--unknown"};
    CHECK(dbc::interpreter::DispatchCommandLine(Invalid, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Usage));

    const std::array<std::string_view, 2> Missing{"check", "/definitely/missing/opendbc-source.dba"};
    CHECK(dbc::interpreter::DispatchCommandLine(Missing, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Target));
}

TEST_CASE("execution service reports presented frames and benchmark percentiles")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("benchmark", 1, 1));
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "benchmark.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources.push_back({Request.RootLogicalPath, "end\n"});
    Request.Options.Benchmark = true;

    dbc::interpreter::ExecutionService Service(Kernel);
    REQUIRE(Service.Prepare(std::move(Request)));
    for (int Frame = 0; Frame < 3; ++Frame)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Service.NotifyPresentedFrame();
    }
    CHECK(Service.Tick(32).State == dbc::interpreter::ExecutionState::Completed);
    const dbc::interpreter::ExecutionResult& Result = Service.Result();
    CHECK(Result.FrameCount == 3);
    CHECK(Result.Benchmark.FrameCount == 3);
    CHECK(Result.Benchmark.MinimumFrameMilliseconds <= Result.Benchmark.P50FrameMilliseconds);
    CHECK(Result.Benchmark.P50FrameMilliseconds <= Result.Benchmark.P95FrameMilliseconds);
    CHECK(Result.Benchmark.P95FrameMilliseconds <= Result.Benchmark.P99FrameMilliseconds);
    CHECK(Result.Benchmark.P99FrameMilliseconds <= Result.Benchmark.MaximumFrameMilliseconds);
}

TEST_CASE("execution service centralizes frame bounds")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("sync-bound", 1, 1));
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "sync-bound.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources.push_back({Request.RootLogicalPath, "sync on\ndo\nwait 0\nloop\n"});
    Request.Options.MaxFrames = 3;

    dbc::interpreter::ExecutionService Service(Kernel);
    REQUIRE(Service.Prepare(std::move(Request)));
    for (int TickCount = 0; TickCount < 8 && !Service.Result().Finished(); ++TickCount)
        Service.Tick(100);
    CHECK(Service.Result().State == dbc::interpreter::ExecutionState::Completed);
    CHECK(Service.Result().FrameCount == 3);
}

TEST_CASE("execution service does not advance after an external frame reaches the bound")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("external-frame-bound", 1, 1));
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "external-frame-bound.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources.push_back({Request.RootLogicalPath, "sync : end\n"});
    Request.Options.MaxFrames = 1;

    dbc::interpreter::ExecutionService Service(Kernel);
    REQUIRE(Service.Prepare(std::move(Request)));
    Service.NotifyPresentedFrame();
    CHECK(Service.Tick(100).State == dbc::interpreter::ExecutionState::Completed);
    CHECK(Service.Result().FrameCount == 1);
}

TEST_CASE("execution service consumes cancellation while the host is suspended")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("suspended-cancel", 1, 1));
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "suspended-cancel.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources.push_back({Request.RootLogicalPath, "wait key\n"});
    Request.Options.Benchmark = true;

    dbc::interpreter::ExecutionService Service(Kernel);
    REQUIRE(Service.Prepare(std::move(Request)));
    CHECK(Service.Tick(100).State == dbc::interpreter::ExecutionState::Suspended);
    Kernel.Core().SetSuspended(true);
    Service.RequestStop();
    CHECK(Service.Tick(1).State == dbc::interpreter::ExecutionState::Cancelled);
    CHECK(Service.Result().Finished());
    CHECK(Service.Result().Benchmark.TotalMilliseconds >= Service.Result().Benchmark.PrepareMilliseconds);
}

TEST_CASE("check and inspect stay headless and report typed frontend results")
{
    std::string Output;
    std::string Error;
    const dbc::interpreter::CommandLineEnvironment Environment{
        [&](std::string_view Text) { Output += Text; }, [&](std::string_view Text) { Error += Text; }, {}};

    TemporarySource Invalid("if then\n");
    const std::string InvalidPath = Invalid.Path.string();
    const std::array<std::string_view, 4> CheckInvalid{"check", InvalidPath, "--dialect", "dbc"};
    CHECK(dbc::interpreter::DispatchCommandLine(CheckInvalid, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Source));
    CHECK_FALSE(Error.empty());

    Output.clear();
    Error.clear();
    TemporarySource Valid("print \"hello\"\n");
    const std::string ValidPath = Valid.Path.string();
    const std::array<std::string_view, 5> Inspect{"inspect", ValidPath, "--json", "--dialect", "dbc"};
    CHECK(dbc::interpreter::DispatchCommandLine(Inspect, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    CHECK(Error.empty());
    CHECK(Output.find("\"profile\":\"DBC\"") != std::string::npos);
    CHECK(Output.find("\"inputKind\":\"source\"") != std::string::npos);
    CHECK(Output.find("\"dialectSource\":\"forced\"") != std::string::npos);
    CHECK(Output.find("\"PRINT\"") != std::string::npos);

    Output.clear();
    Error.clear();
    TemporarySource Stubbed("value = get ftp status()\n");
    const std::string StubbedPath = Stubbed.Path.string();
    const std::array<std::string_view, 5> InspectStub{"inspect", StubbedPath, "--json", "--dialect", "dbc"};
    CHECK(dbc::interpreter::DispatchCommandLine(InspectStub, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    CHECK(Error.empty());
    CHECK(Output.find("\"name\":\"GET FTP STATUS\"") != std::string::npos);
    CHECK(Output.find("\"support\":\"Compatibility stub\"") != std::string::npos);
    CHECK(Output.find("\"stub_policy\":\"Permanent\"") != std::string::npos);
}

TEST_CASE("source analysis prefers dirty in-memory units over the include fallback")
{
    int Loads = 0;
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "/virtual/main.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources = {
        {"/virtual/main.dba", "#include \"values.dba\"\nprint value\n"},
        {"/virtual/values.dba", "value=42\n"},
    };
    Request.LoadMissingSource = [&](const std::filesystem::path&, dbc::language::LanguageProfile, std::string&)
    {
        ++Loads;
        return std::optional<dbc::interpreter::SourceUnit>{};
    };

    const dbc::interpreter::SourceAnalysis Analysis = dbc::interpreter::AnalyzeSources(std::move(Request));
    REQUIRE(Analysis.Success());
    CHECK(Loads == 0);
    CHECK(Analysis.Inspection.SourceCount == 2);
    CHECK(std::find(Analysis.Inspection.Builtins.begin(), Analysis.Inspection.Builtins.end(), "PRINT") !=
          Analysis.Inspection.Builtins.end());
}

TEST_CASE("source analysis normalizes historical include separators before confinement")
{
    dbc::interpreter::ExecutionRequest Request;
    Request.RootLogicalPath = "/virtual/main.dba";
    Request.Profile = dbc::language::LanguageProfile::Dbc;
    Request.Sources = {
        {"/virtual/main.dba", "#include \"nested\\branch/../values.dba\"\nprint value\n"},
        {"/virtual/nested/values.dba", "value=42\n"},
    };

    const dbc::interpreter::SourceAnalysis Analysis = dbc::interpreter::AnalyzeSources(std::move(Request));
    REQUIRE(Analysis.Success());
    CHECK(Analysis.Inspection.SourceCount == 2);

    dbc::interpreter::ExecutionRequest Escaping;
    Escaping.RootLogicalPath = "/payload/main.dba";
    Escaping.Profile = dbc::language::LanguageProfile::Dbc;
    Escaping.Sources = {{"/payload/main.dba", "#include \"..\\outside.dba\"\n"}};
    Escaping.ConfinedSourceRoot = "/payload";
    const dbc::interpreter::SourceAnalysis Rejected = dbc::interpreter::AnalyzeSources(std::move(Escaping));
    REQUIRE_FALSE(Rejected.Success());
    REQUIRE_FALSE(Rejected.Diagnostics.empty());
    CHECK(Rejected.Diagnostics.front().Message.find("remain inside its payload") != std::string::npos);
}

TEST_CASE("a headless run saves its last frame with --capture-output")
{
    const std::array<std::string_view, 5> WithoutHeadless{"run", "program.dba", "--max-frames", "1",
                                                          "--capture-output"};
    CHECK_FALSE(dbc::interpreter::ParseCommandLine(WithoutHeadless));
    const std::array<std::string_view, 4> Windowed{"run", "program.dba", "--capture-output", "frame.bmp"};
    const dbc::interpreter::CommandLineParseResult Rejected = dbc::interpreter::ParseCommandLine(Windowed);
    REQUIRE_FALSE(Rejected);
    CHECK(Rejected.Error == "--capture-output requires --headless");

    TemporarySource Source("sync on\ncls rgb(255, 0, 0)\ndo\nsync\nloop\n");
    const std::filesystem::path Capture = Source.Path.string() + ".bmp";
    const std::string SourcePath = Source.Path.string();
    const std::string CapturePath = Capture.string();
    const std::array<std::string_view, 9> Run{"run",          SourcePath, "--dialect",        "dbc",      "--headless",
                                              "--max-frames", "3",        "--capture-output", CapturePath};
    dbc::interpreter::CommandLineEnvironment Environment;
    REQUIRE(dbc::interpreter::DispatchCommandLine(Run, Environment) ==
            static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    SDL_Surface* Frame = SDL_LoadBMP(CapturePath.c_str());
    REQUIRE(Frame != nullptr);
    Uint8 Red = 0, Green = 0, Blue = 0, Alpha = 0;
    REQUIRE(SDL_ReadSurfacePixel(Frame, Frame->w / 2, Frame->h / 2, &Red, &Green, &Blue, &Alpha));
    SDL_DestroySurface(Frame);
    std::error_code Ignored;
    std::filesystem::remove(Capture, Ignored);
    CHECK(Red > 200);
    CHECK(Green < 50);
    CHECK(Blue < 50);
}

TEST_CASE("a headless run saves its scene commands and 3D state with --capture-scene")
{
    const std::array<std::string_view, 4> Windowed{"run", "program.dba", "--capture-scene", "scene.json"};
    const dbc::interpreter::CommandLineParseResult Rejected = dbc::interpreter::ParseCommandLine(Windowed);
    REQUIRE_FALSE(Rejected);
    CHECK(Rejected.Error == "--capture-scene requires --headless");

    TemporarySource Source("sync on\n"
                           "for i = 1 to 2\n"
                           "  make object cube i, 10\n"
                           "  position object i, i * 5, 0, 0\n"
                           "next i\n"
                           "print \"not a scene command\"\n"
                           "scale object 2, 50, 100, 100\n"
                           "delete object 1\n"
                           "do\n"
                           "  sync\n"
                           "  yrotate object 2, 45\n"
                           "loop\n");
    const std::filesystem::path Capture = Source.Path.string() + ".json";
    const std::string SourcePath = Source.Path.string();
    const std::string CapturePath = Capture.string();
    const std::array<std::string_view, 9> Run{"run",          SourcePath, "--dialect",       "dbc",      "--headless",
                                              "--max-frames", "2",        "--capture-scene", CapturePath};
    dbc::interpreter::CommandLineEnvironment Environment;
    REQUIRE(dbc::interpreter::DispatchCommandLine(Run, Environment) ==
            static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));

    std::ifstream Input(Capture);
    REQUIRE(Input.good());
    const nlohmann::json Json = nlohmann::json::parse(Input, nullptr, false);
    Input.close();
    std::error_code Ignored;
    std::filesystem::remove(Capture, Ignored);
    REQUIRE_FALSE(Json.is_discarded());
    CHECK(Json["schema"] == "opendbc-scene-capture");
    CHECK(Json["status"] == "completed");
    CHECK_FALSE(Json["truncated"].get<bool>());

    // symbol, file, line, column, frame, root, arguments
    const nlohmann::json& Calls = Json["calls"];
    REQUIRE(Calls.size() == 7);
    CHECK(Calls[0][0] == static_cast<std::uint32_t>(dbc::language::SymbolId::Command_MakeObjectCube));
    CHECK(Calls[0][2] == 3);
    CHECK(Calls[0][4] == 0);
    CHECK(Calls[0][6] == nlohmann::json::array({1, 10}));
    CHECK(Calls[3][0] == static_cast<std::uint32_t>(dbc::language::SymbolId::Command_PositionObject));
    CHECK(Calls[3][6][1].get<double>() == doctest::Approx(10.0));
    CHECK(Calls[6][0] == static_cast<std::uint32_t>(dbc::language::SymbolId::Command_YrotateObject));
    CHECK(Calls[6][4] == 1);
    CHECK(Json["files"][Calls[0][1].get<std::size_t>()].get<std::string>().find(
              Source.Path.filename().generic_string()) != std::string::npos);

    REQUIRE(Json["objects"].size() == 1);
    const nlohmann::json& Object = Json["objects"][0];
    CHECK(Object["id"] == 2);
    CHECK(Object["position"][0].get<double>() == doctest::Approx(10.0));
    CHECK(Object["rotation"][1].get<double>() == doctest::Approx(45.0));
    CHECK(Object["scale"][0].get<double>() == doctest::Approx(50.0));
    CHECK(Object["max"][0].get<double>() == doctest::Approx(5.0));
}

TEST_CASE("an ambiguous DBA requires a force in non-interactive CLI mode")
{
    TemporarySource Source("print 1\n");
    const std::string Path = Source.Path.string();
    const std::array<std::string_view, 2> Arguments{"check", Path};
    std::string Error;
    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardError = [&](std::string_view Text) { Error += Text; };
    CHECK(dbc::interpreter::DispatchCommandLine(Arguments, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Target));
    CHECK(Error.find("--dialect") != std::string::npos);

    Environment.SelectDialect = [](const dbc::language::DialectDetection&)
    { return std::optional(dbc::language::LanguageProfile::DbPro); };
    Error.clear();
    CHECK(dbc::interpreter::DispatchCommandLine(Arguments, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
}

TEST_CASE("the same DBA can be forced through either immutable profile")
{
    TemporarySource Source("print 1\n");
    const std::string Path = Source.Path.string();
    for (const std::string_view Dialect : {std::string_view("dbc"), std::string_view("dbpro")})
    {
        const std::array<std::string_view, 4> Arguments{"check", Path, "--dialect", Dialect};
        CHECK(dbc::interpreter::DispatchCommandLine(Arguments) ==
              static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    }
}

TEST_CASE("a DBPro project fixes the profile and compiles ordered source units")
{
    TemporaryProject Project;
    Project.Write("main.dba", "print helper()\n");
    Project.Write("helpers.dba", "function helper()\nendfunction 7\n");
    Project.Write("game.dbpro", "version=DBP1.00\nmain=main.dba\ninclude1=helpers.dba\n");
    const std::string Descriptor = (Project.Root / "game.dbpro").string();

    const std::array<std::string_view, 4> Check{"check", Descriptor, "--dialect", "dbpro"};
    CHECK(dbc::interpreter::DispatchCommandLine(Check) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));

    const std::array<std::string_view, 4> Conflict{"check", Descriptor, "--dialect", "dbc"};
    CHECK(dbc::interpreter::DispatchCommandLine(Conflict) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Target));

    std::string Output;
    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [&](std::string_view Text) { Output += Text; };
    const std::array<std::string_view, 5> Inspect{"inspect", Descriptor, "--json", "--dialect", "dbpro"};
    CHECK(dbc::interpreter::DispatchCommandLine(Inspect, Environment) ==
          static_cast<int>(dbc::interpreter::CommandLineExitCode::Success));
    CHECK(Output.find("\"inputKind\":\"dbpro-project\"") != std::string::npos);
    CHECK(Output.find("helpers.dba") != std::string::npos);
}
