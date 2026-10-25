#pragma once

#include "language/language_catalogue.hpp"

#include "engine_builtin_context.hpp"

#include <string>
#include <vector>

namespace dbc::interpreter::detail
{
    bool HasBuiltinHandler(language::SymbolId Symbol);
    bool InvokeCommand(BuiltinContext& Context, language::SymbolId Symbol, const std::vector<BuiltinValue>& Arguments,
                       std::string& Error);
    bool InvokeFunction(BuiltinContext& Context, language::SymbolId Symbol, const std::vector<BuiltinValue>& Arguments,
                        BuiltinValue& Result, std::string& Error);

} // namespace dbc::interpreter::detail
