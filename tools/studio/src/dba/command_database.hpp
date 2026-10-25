#pragma once

#include "language/language_catalogue.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace dba
    {
        class Reference;

        struct Command
        {
            const dbc::language::SymbolDescriptor* Descriptor = nullptr;
            std::string Classic;
            std::string Signature;
            std::string Description;
            std::string Key;
            std::string Initials;
            int WordCount = 1;
        };

        class CommandDatabase
        {
          public:
            void Build(const Reference& ReferenceData);

            const std::vector<Command>& All() const
            {
                return Commands_;
            }

            const Command* Find(dbc::language::LanguageProfile Profile, std::string_view Name) const;
            std::vector<std::string> Complete(dbc::language::LanguageProfile Profile, std::string_view Term,
                                              size_t Limit) const;

          private:
            std::vector<Command> Commands_;
        };
    } // namespace dba

} // namespace dbc::studio
