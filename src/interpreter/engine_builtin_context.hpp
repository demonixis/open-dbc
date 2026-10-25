#pragma once

#include "host_services.hpp"
#include "language/language_profile.hpp"

#include <ankerl/unordered_dense.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dbc
{
    class EngineKernel;

    namespace interpreter::detail
    {
        struct ChecklistItem
        {
            std::string Name;
            int A = 0;
            int B = 0;
            int C = 0;
            int D = 0;
            float FloatA = 0.0f;
            float FloatB = 0.0f;
            float FloatC = 0.0f;
            float FloatD = 0.0f;
        };

        struct BuiltinValue
        {
            bool IsString = false;
            double Number = 0.0;
            std::string Text{};

            static BuiltinValue FromNumber(double V)
            {
                BuiltinValue Value;
                Value.Number = V;
                return Value;
            }

            static BuiltinValue FromString(std::string V)
            {
                BuiltinValue Value;
                Value.IsString = true;
                Value.Text = std::move(V);
                return Value;
            }

            double AsNumber() const;
            bool AsInt(int& Output, std::string* Error = nullptr) const;
            bool AsFloat(float& Output, std::string* Error = nullptr) const;
            bool AsBool() const;
            std::string AsString() const;
        };

        struct BuiltinContext
        {
            EngineKernel* Kernel = nullptr;
            std::filesystem::path AssetRoot{};
            bool Benchmark = false;
            int MaxFrames = 0;
            bool ShouldExit = false;
            language::LanguageProfile Profile = language::LanguageProfile::Dbc;

            ankerl::unordered_dense::map<int, std::array<float, 3>> Vectors{};
            ankerl::unordered_dense::map<int, std::array<float, 2>> Vectors2{};
            ankerl::unordered_dense::map<int, std::array<float, 4>> Vectors4{};
            ankerl::unordered_dense::map<int, std::array<float, 16>> Matrices4{};
            std::vector<ChecklistItem> Checklist{};
            std::string TokenSource{};
            std::size_t TokenOffset = 0;
            std::string PendingPrint{};
            std::optional<std::pair<std::string, std::string>> PendingExitPrompt{};
            bool ExitPromptPresented = false;
            std::unordered_set<int> InertMusicIds{};

            std::FILE* LogFile = nullptr;
            std::function<void(std::string_view Text)> Output{};
            HostFileLauncher LaunchFile{};
            ExitPromptPresenter PresentExitPrompt{};
            int SyncCount = 0;
            std::string CommandLine{};
        };

    } // namespace interpreter::detail

} // namespace dbc
