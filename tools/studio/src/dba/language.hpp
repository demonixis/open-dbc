#pragma once

#include <string>
#include <vector>

#include <TextEditor.h>

#include "language/language_profile.hpp"

namespace dbc::studio
{
    namespace dba
    {
        // TextEditor language definition for DBA. Replaces the regex rules the Qt
        // DbaHighlighter used: keyword sets plus small hand-written tokenizers.
        const TextEditor::Language* Language(dbc::language::LanguageProfile Profile);

    } // namespace dba

} // namespace dbc::studio
