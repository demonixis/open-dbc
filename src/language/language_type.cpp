// Type comparisons the verifier and the runtime need. They live apart from the frontend so that
// bytecode-only programs such as opendbc-runner do not link the lexer, parser and binder.

#include "frontend_internal.hpp"

#include <cctype>

namespace dbc::language
{
    bool Type::IsNumeric() const
    {
        return Kind == TypeKind::Boolean || Kind == TypeKind::Integer || Kind == TypeKind::Real;
    }

    bool Type::operator==(const Type& Other) const
    {
        return Kind == Other.Kind && detail::Canonical(Name) == detail::Canonical(Other.Name);
    }
} // namespace dbc::language

namespace dbc::language::detail
{
    std::string Canonical(std::string_view Text)
    {
        std::string Result;
        Result.reserve(Text.size());
        bool PendingSpace = false;
        for (const unsigned char Character : Text)
        {
            if (std::isspace(Character) != 0)
            {
                PendingSpace = !Result.empty();
                continue;
            }
            if (PendingSpace)
            {
                Result.push_back(' ');
                PendingSpace = false;
            }
            Result.push_back(static_cast<char>(std::toupper(Character)));
        }
        return Result;
    }
} // namespace dbc::language::detail
