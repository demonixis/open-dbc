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

        bool StartsWith(const std::string& Value, const std::string& Prefix)
        {
            return Value.size() >= Prefix.size() && Value.compare(0, Prefix.size(), Prefix) == 0;
        }

        int Score(const dba::Command& CommandEntry, const std::string& Term)
        {
            if (StartsWith(CommandEntry.Key, Term))
            {
                return 1000;
            }
            if (Term.size() >= 2 && StartsWith(CommandEntry.Initials, Term))
            {
                return 600;
            }
            std::istringstream Words{CommandEntry.Classic};
            for (std::string Word; Words >> Word;)
            {
                if (StartsWith(ToLower(Word), Term))
                {
                    return 400;
                }
            }
            return CommandEntry.Key.find(Term) != std::string::npos ? 200 : 0;
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
                    ++Entry.WordCount;
                    Entry.Initials.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Word[0]))));
                }
                Entry.WordCount = std::max(1, Entry.WordCount - 1);

                if (const Reference::Entry* Documentation = ReferenceData.Find(Entry.Classic))
                {
                    Entry.Signature = Documentation->Signature;
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

        std::vector<std::string> CommandDatabase::Complete(dbc::language::LanguageProfile Profile,
                                                           std::string_view Term, size_t Limit) const
        {
            std::vector<std::string> Results;
            const std::string Needle = Normalize(Term);
            if (Needle.empty())
            {
                return Results;
            }

            struct Scored
            {
                const Command* Entry;
                int Points;
            };
            std::vector<Scored> Matches;
            for (const Command& Entry : Commands_)
            {
                if (!dbc::language::AvailableIn(*Entry.Descriptor, Profile))
                {
                    continue;
                }
                if (const int Points = Score(Entry, Needle); Points > 0)
                {
                    Matches.push_back({&Entry, Points});
                }
            }

            std::sort(Matches.begin(), Matches.end(),
                      [](const Scored& Left, const Scored& Right)
                      {
                          if (Left.Points != Right.Points)
                          {
                              return Left.Points > Right.Points;
                          }
                          if (Left.Entry->Key.size() != Right.Entry->Key.size())
                          {
                              return Left.Entry->Key.size() < Right.Entry->Key.size();
                          }
                          return Left.Entry->Classic < Right.Entry->Classic;
                      });

            for (const Scored& Match : Matches)
            {
                if (Results.size() >= Limit)
                {
                    break;
                }
                if (Results.empty() || Results.back() != Match.Entry->Classic)
                {
                    Results.push_back(Match.Entry->Classic);
                }
            }
            return Results;
        }
    } // namespace dba

} // namespace dbc::studio
