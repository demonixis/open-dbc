#include "snippet_library.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

// Defines KBuiltinSnippetFiles from tools/studio/snippets.
#include "studio_snippets.inc"

namespace dbc::studio
{
    namespace core
    {
        std::vector<Snippet> BuiltinSnippets()
        {
            std::vector<Snippet> Result;
            for (const auto& [FileName, Text] : KBuiltinSnippetFiles)
                Result.push_back(ParseSnippet(FileName, Text));
            std::stable_sort(Result.begin(), Result.end(),
                             [](const Snippet& Left, const Snippet& Right) { return Left.Name < Right.Name; });
            return Result;
        }
    } // namespace core

} // namespace dbc::studio
