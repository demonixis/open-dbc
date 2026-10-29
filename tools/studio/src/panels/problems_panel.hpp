#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "toolchain/execution_adapter.hpp"

namespace dbc::studio
{
    namespace panels
    {
        // Diagnostics of the last run or check, one row each; choosing a row opens its file at its line.
        class ProblemsPanel
        {
          public:
            using SelectHandler = std::function<void(const std::filesystem::path& Path, std::uint32_t Line)>;

            void SetOnSelect(SelectHandler Handler);
            void Set(std::vector<toolchain::RunDiagnostic> Problems);
            void Draw();

          private:
            std::vector<toolchain::RunDiagnostic> Problems_;
            SelectHandler OnSelect_;
        };
    } // namespace panels

} // namespace dbc::studio
