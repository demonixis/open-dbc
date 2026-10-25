#pragma once

#include "language_profile.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dbc::language
{
    enum class DialectDetectionKind
    {
        Dbc,
        DbPro,
        Ambiguous,
        Conflict,
    };

    enum class DialectEvidenceKind
    {
        Grammar,
        Symbol,
        Arity,
    };

    struct DialectEvidence
    {
        LanguageProfile Profile = LanguageProfile::Dbc;
        DialectEvidenceKind Kind = DialectEvidenceKind::Grammar;
        std::filesystem::path LogicalPath{};
        std::uint32_t Line = 1;
        std::string Detail{};
    };

    struct DialectDetection
    {
        DialectDetectionKind Kind = DialectDetectionKind::Ambiguous;
        std::vector<DialectEvidence> Evidence{};

        std::optional<LanguageProfile> Profile() const;
    };

    struct DialectSource
    {
        std::filesystem::path LogicalPath{};
        std::string Text{};
    };

    DialectDetection DetectDialect(const DialectSource& Source);
    DialectDetection DetectDialect(std::span<const DialectSource> Sources);
} // namespace dbc::language
