#pragma once

#include "language/frontend.hpp"
#include "language/language_profile.hpp"
#include "vm/value.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace dbc
{
    class EngineKernel;
}

namespace dbc::interpreter
{
    // The scene commands of a run with their evaluated arguments and their statement, so the Studio can rebuild
    // and edit the scene of any program. --capture-scene writes them with the engine's final state.
    class SceneRecorder
    {
      public:
        void Record(language::SymbolId Symbol, std::span<const vm::Value> Arguments, const language::SourceSpan& Span,
                    int Frame, const std::filesystem::path& AssetRoot);
        bool Write(const std::filesystem::path& Path, language::LanguageProfile Profile, std::string_view Status,
                   std::size_t Frames, const EngineKernel* Kernel, std::string& Error) const;

      private:
        nlohmann::ordered_json Calls_ = nlohmann::ordered_json::array();
        std::vector<std::filesystem::path> Files_{};
        std::vector<std::filesystem::path> Roots_{};
        bool Truncated_ = false;
    };
} // namespace dbc::interpreter
