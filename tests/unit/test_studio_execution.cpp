#include <doctest/doctest.h>

#include "toolchain/execution_adapter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace dbc::studio;

namespace
{
    struct RunCapture
    {
        std::vector<std::string> Lines;
        std::vector<toolchain::RunDiagnostic> Diagnostics;
        int ExitCode = -1;
        bool Finished = false;
    };

    void Attach(toolchain::ExecutionAdapter& Adapter, RunCapture& Capture)
    {
        Adapter.SetRunner(OPENDBC_TEST_RUNNER);
        Adapter.SetHandlers([&Capture](const std::string& Line, bool) { Capture.Lines.push_back(Line); },
                            [&Capture](int ExitCode, const std::vector<toolchain::RunDiagnostic>& Diagnostics)
                            {
                                Capture.ExitCode = ExitCode;
                                Capture.Diagnostics = Diagnostics;
                                Capture.Finished = true;
                            });
    }

    bool WaitFinished(toolchain::ExecutionAdapter& Adapter, RunCapture& Capture)
    {
        const Uint64 Deadline = SDL_GetTicks() + 60'000;
        while (!Capture.Finished && SDL_GetTicks() < Deadline)
        {
            Adapter.Poll();
            SDL_Delay(10);
        }
        return Capture.Finished;
    }

    std::filesystem::path WriteSource(const std::string& Name, const std::string& Text)
    {
        const std::filesystem::path Root = std::filesystem::temp_directory_path() / "opendbc-test-studio-execution";
        std::error_code Error;
        std::filesystem::create_directories(Root, Error);
        const std::filesystem::path Path = Root / Name;
        std::ofstream(Path, std::ios::binary) << Text;
        return Path;
    }
} // namespace

TEST_CASE("Run diagnostics parse the lines opendbc run prints")
{
    const std::filesystem::path Root = std::filesystem::absolute("project");
    const std::optional<toolchain::RunDiagnostic> Relative =
        toolchain::ParseRunDiagnostic("src/main.dba:12:3: DBC5000: Array index is out of bounds", Root);
    REQUIRE(Relative);
    CHECK(Relative->Path == (Root / "src/main.dba").lexically_normal());
    CHECK(Relative->Line == 12);
    CHECK(Relative->Column == 3);
    CHECK(Relative->Code == "DBC5000");
    CHECK(Relative->Message == "Array index is out of bounds");

    // A drive letter is not a line number.
    const std::optional<toolchain::RunDiagnostic> Drive =
        toolchain::ParseRunDiagnostic("C:/games/main.dba:4:1: DBC1101: Unexpected token after expression", Root);
    REQUIRE(Drive);
    CHECK(Drive->Line == 4);
    CHECK(Drive->Column == 1);
    CHECK(Drive->Code == "DBC1101");

    const std::optional<toolchain::RunDiagnostic> Unplaced =
        toolchain::ParseRunDiagnostic("DBC1004: Failed to open log output", Root);
    REQUIRE(Unplaced);
    CHECK(Unplaced->Path.empty());
    CHECK(Unplaced->Line == 0);

    CHECK_FALSE(toolchain::ParseRunDiagnostic("score: 10", Root));
    CHECK_FALSE(toolchain::ParseRunDiagnostic("hello", Root));
}

TEST_CASE("The Studio runs programs in opendbc-runner and reports where they fail")
{
    // The runner inherits this: no window, no sound.
    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "OPENDBC_HEADLESS", "1", true));

    SUBCASE("unsaved buffers run instead of the file on disk")
    {
        const std::filesystem::path Source = WriteSource("buffered.dba", "print \"from disk\"\n");
        toolchain::ExecutionAdapter Adapter;
        RunCapture Capture;
        Attach(Adapter, Capture);
        toolchain::RunDocument Document;
        Document.LogicalPath = Source;
        Document.Sources.push_back({Source, "print \"from the editor\"\n"});
        REQUIRE(Adapter.Run(std::move(Document), {}));
        REQUIRE(WaitFinished(Adapter, Capture));
        CHECK(Capture.ExitCode == 0);
        CHECK(std::find(Capture.Lines.begin(), Capture.Lines.end(), "from the editor") != Capture.Lines.end());
        CHECK(Capture.Diagnostics.empty());
    }

    SUBCASE("a run-time error points at its source line")
    {
        const std::filesystem::path Source = WriteSource("failing.dba", "dim a(2)\na(5) = 1\n");
        toolchain::ExecutionAdapter Adapter;
        RunCapture Capture;
        Attach(Adapter, Capture);
        toolchain::RunDocument Document;
        Document.LogicalPath = Source;
        REQUIRE(Adapter.Run(std::move(Document), {}));
        REQUIRE(WaitFinished(Adapter, Capture));
        CHECK(Capture.ExitCode != 0);
        REQUIRE(Capture.Diagnostics.size() == 1);
        CHECK(Capture.Diagnostics.front().Path == std::filesystem::absolute(Source).lexically_normal());
        CHECK(Capture.Diagnostics.front().Line == 2);
        CHECK(Capture.Diagnostics.front().Code == "DBC5000");
    }

    SUBCASE("a compile error never starts the runner")
    {
        const std::filesystem::path Source = WriteSource("broken.dba", "x = (\n");
        toolchain::ExecutionAdapter Adapter;
        RunCapture Capture;
        Attach(Adapter, Capture);
        toolchain::RunDocument Document;
        Document.LogicalPath = Source;
        CHECK_FALSE(Adapter.Run(std::move(Document), {}));
        CHECK(Capture.Finished);
        CHECK_FALSE(Adapter.IsRunning());
        REQUIRE_FALSE(Capture.Diagnostics.empty());
        CHECK(Capture.Diagnostics.front().Line == 1);
    }

    SUBCASE("Stop ends a program that never finishes")
    {
        const std::filesystem::path Source = WriteSource("endless.dba", "do\nsync\nloop\n");
        toolchain::ExecutionAdapter Adapter;
        RunCapture Capture;
        Attach(Adapter, Capture);
        toolchain::RunDocument Document;
        Document.LogicalPath = Source;
        REQUIRE(Adapter.Run(std::move(Document), {}));
        for (int Frame = 0; Frame < 50; ++Frame)
        {
            Adapter.Poll();
            SDL_Delay(10);
        }
        CHECK(Adapter.IsRunning());
        Adapter.Stop();
        REQUIRE(WaitFinished(Adapter, Capture));
        CHECK(Capture.ExitCode == 130);
    }
}

TEST_CASE("A check compiles the program without running it")
{
    toolchain::ExecutionAdapter Adapter;
    RunCapture Capture;
    Attach(Adapter, Capture);

    toolchain::RunDocument Broken;
    Broken.LogicalPath = WriteSource("check.dba", "print 1\nx = (\n");
    const std::vector<toolchain::RunDiagnostic> Problems = Adapter.Check(std::move(Broken));
    REQUIRE_FALSE(Problems.empty());
    CHECK(Problems.front().Line == 2);
    CHECK(Problems.front().Error);
    CHECK_FALSE(Adapter.IsRunning());
    CHECK_FALSE(Capture.Finished);

    toolchain::RunDocument Valid;
    Valid.LogicalPath = WriteSource("check.dba", "print 1\n");
    CHECK(Adapter.Check(std::move(Valid)).empty());
}
