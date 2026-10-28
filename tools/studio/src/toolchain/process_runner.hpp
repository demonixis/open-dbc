#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        struct ProcessRequest
        {
            std::vector<std::string> Arguments;
            // Added to the inherited environment. Secrets travel here, never in Arguments, which other
            // users of the machine can read from the process table.
            std::vector<std::pair<std::string, std::string>> Environment;
            std::filesystem::path WorkingDirectory;
        };

        struct ProcessResult
        {
            int ExitCode = -1;
            bool Cancelled = false;
            // Combined standard output and error, kept only when capture was requested.
            std::string Output;
            std::string Error;

            explicit operator bool() const
            {
                return Error.empty();
            }
        };

        // Receives each line the tool prints on standard output or error, without its line ending.
        using ProcessLineSink = std::function<void(std::string_view Line)>;

        // Runs a tool to completion. A non-zero exit, a launch failure or a cancellation sets Error, which
        // quotes the last lines the tool printed. Cancel is polled while the tool runs.
        ProcessResult RunProcess(const ProcessRequest& Request, const ProcessLineSink& Sink,
                                 const std::atomic<bool>* Cancel, bool CaptureOutput);
    } // namespace toolchain

} // namespace dbc::studio
