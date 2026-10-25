#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace dbc::interpreter
{
    struct HostFileLaunchRequest
    {
        std::filesystem::path FileName{};
        std::string CommandLine{};
        std::filesystem::path WorkingDirectory{};
        bool WaitForTermination = false;
    };

    using HostFileLauncher = std::function<bool(const HostFileLaunchRequest& Request, std::string& Error)>;
    using ExitPromptPresenter = std::function<void(std::string_view Title, std::string_view Description)>;
} // namespace dbc::interpreter
