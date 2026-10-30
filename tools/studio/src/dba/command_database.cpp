#include "command_database.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "reference.hpp"

namespace dbc::studio
{
    namespace
    {
        std::string ToLower(std::string_view Value)
        {
            std::string Result;
            Result.reserve(Value.size());
            for (const char Character : Value)
            {
                Result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Character))));
            }
            return Result;
        }

        std::string Normalize(std::string_view Value)
        {
            std::string Result;
            Result.reserve(Value.size());
            for (const char Character : Value)
            {
                if (std::isalnum(static_cast<unsigned char>(Character)) != 0 || Character == '$' || Character == '#')
                {
                    Result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Character))));
                }
            }
            return Result;
        }
    } // namespace

    namespace dba
    {
        void CommandDatabase::Build(const Reference& ReferenceData)
        {
            Commands_.clear();
            for (const dbc::language::SymbolDescriptor& Descriptor : dbc::language::Symbols())
            {
                if ((Descriptor.Kind != dbc::language::SymbolKind::Command &&
                     Descriptor.Kind != dbc::language::SymbolKind::Function) ||
                    Descriptor.AvailableProfiles == 0)
                {
                    continue;
                }

                Command Entry;
                Entry.Descriptor = &Descriptor;
                Entry.Classic = Descriptor.CanonicalName;
                Entry.Key = Normalize(Entry.Classic);
                std::istringstream Words{Entry.Classic};
                for (std::string Word; Words >> Word;)
                {
                    Entry.Words.push_back(ToLower(Word));
                    Entry.Initials.push_back(Entry.Words.back()[0]);
                }

                if (const Reference::Entry* Documentation = ReferenceData.Find(Entry.Classic))
                {
                    Entry.Description = Documentation->Description;
                }
                Commands_.push_back(std::move(Entry));
            }

            std::sort(Commands_.begin(), Commands_.end(),
                      [](const Command& Left, const Command& Right)
                      {
                          if (Left.Classic != Right.Classic)
                          {
                              return Left.Classic < Right.Classic;
                          }
                          return Left.Descriptor->Kind < Right.Descriptor->Kind;
                      });
        }

        const Command* CommandDatabase::Find(dbc::language::LanguageProfile Profile, std::string_view Name) const
        {
            const std::string Key = Normalize(Name);
            const Command* Function = nullptr;
            for (const Command& Entry : Commands_)
            {
                if (Entry.Key != Key || !dbc::language::AvailableIn(*Entry.Descriptor, Profile))
                {
                    continue;
                }
                if (Entry.Descriptor->Kind == dbc::language::SymbolKind::Command)
                {
                    return &Entry;
                }
                Function = &Entry;
            }
            return Function;
        }
    } // namespace dba

} // namespace dbc::studio
