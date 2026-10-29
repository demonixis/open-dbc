#include <doctest/doctest.h>

#include "toolchain/process_runner.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

using namespace dbc::studio;

#if !defined(_WIN32)
namespace
{
    toolchain::ProcessResult RunShell(std::string Script, std::vector<std::string>* Lines = nullptr,
                                      std::vector<std::pair<std::string, std::string>> Environment = {},
                                      const std::atomic<bool>* Cancel = nullptr, bool Capture = false)
    {
        toolchain::ProcessRequest Request;
        Request.Arguments = {"/bin/sh", "-c", std::move(Script)};
        Request.Environment = std::move(Environment);
        toolchain::ProcessLineSink Sink;
        if (Lines != nullptr)
            Sink = [Lines](std::string_view Line) { Lines->emplace_back(Line); };
        return toolchain::RunProcess(Request, Sink, Cancel, Capture);
    }
} // namespace

TEST_CASE("process runner streams each output line without its line ending")
{
    std::vector<std::string> Lines;
    const toolchain::ProcessResult Result =
        RunShell("printf 'first\\nsecond\\r\\n'; printf 'err\\n' >&2; printf 'last'", &Lines);
    INFO(Result.Error);
    REQUIRE(Result);
    CHECK(Result.ExitCode == 0);
    CHECK(Lines == std::vector<std::string>{"first", "second", "err", "last"});
    CHECK(Result.Output.empty());
}

TEST_CASE("process runner passes secrets through the environment, not the arguments")
{
    const toolchain::ProcessResult Result = RunShell("printf '%s' \"$OPENDBC_TEST_SECRET\"", nullptr,
                                                     {{"OPENDBC_TEST_SECRET", "s3cr3t value"}}, nullptr, true);
    INFO(Result.Error);
    REQUIRE(Result);
    CHECK(Result.Output == "s3cr3t value");
}

TEST_CASE("process runner reports the exit code with the last lines the tool printed")
{
    const toolchain::ProcessResult Result = RunShell("echo first; echo 'what went wrong'; exit 3");
    CHECK_FALSE(Result);
    CHECK(Result.ExitCode == 3);
    CHECK(Result.Error.find("Command failed (3): /bin/sh") != std::string::npos);
    CHECK(Result.Error.find("what went wrong") != std::string::npos);
}

TEST_CASE("process runner stops a cancelled tool")
{
    std::atomic<bool> Cancel{true};
    const Uint64 Start = SDL_GetTicks();
    const toolchain::ProcessResult Result = RunShell("exec sleep 30", nullptr, {}, &Cancel);
    CHECK(Result.Cancelled);
    CHECK_FALSE(Result);
    CHECK(Result.Error.find("Cancelled") != std::string::npos);
    CHECK(SDL_GetTicks() - Start < 10000);
}

TEST_CASE("process runner does not wait for a background process that keeps the output open")
{
    const Uint64 Start = SDL_GetTicks();
    const toolchain::ProcessResult Result = RunShell("sleep 8 & echo started");
    INFO(Result.Error);
    CHECK(Result);
    CHECK(SDL_GetTicks() - Start < 6000);
}
#endif

TEST_CASE("process runner reports a tool that cannot start")
{
    toolchain::ProcessRequest Request;
    Request.Arguments = {"opendbc-tool-that-does-not-exist"};
    const toolchain::ProcessResult Result = toolchain::RunProcess(Request, {}, nullptr, false);
    CHECK_FALSE(Result);
}
