#pragma once

#include "frontend.hpp"

#include <span>

namespace dbc::language::detail
{
    std::vector<Token> Lex(const SourceDocument& Source, std::vector<Diagnostic>& Diagnostics);
    void Parse(Program& Value);
    void BindProgram(Program& Value, const FrontendOptions& Options);

    std::string Canonical(std::string_view Text);
    std::string TokenText(const SourceDocument& Source, const Token& Value);
    bool IsWord(const SourceDocument& Source, const Token& Value, std::string_view Expected);
    SourceSpan Merge(SourceSpan First, SourceSpan Last);
    void AddError(std::vector<Diagnostic>& Diagnostics, std::string Code, std::string Message, SourceSpan Span);
} // namespace dbc::language::detail
