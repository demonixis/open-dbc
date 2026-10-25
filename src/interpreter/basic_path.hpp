#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

namespace dbc::interpreter
{
    inline std::filesystem::path NormalizeBasicPath(const std::filesystem::path& Input)
    {
        const bool NativeAbsolute = Input.is_absolute();
        std::string Text = Input.string();
        std::replace(Text.begin(), Text.end(), '\\', '/');

        std::filesystem::path Result = std::filesystem::path(Text).lexically_normal();
        if (!NativeAbsolute && Result.is_absolute())
            Result = Result.relative_path();
        return Result;
    }

    inline std::filesystem::path ResolveBasicPath(const std::filesystem::path& AssetRoot,
                                                  const std::filesystem::path& Input)
    {
        const bool NativeAbsolute = Input.is_absolute();
        const std::filesystem::path Normalized = NormalizeBasicPath(Input);
        if (NativeAbsolute)
            return Normalized;
        return (NormalizeBasicPath(AssetRoot) / Normalized).lexically_normal();
    }

    inline bool ResolveConfinedBasicPath(const std::filesystem::path& Root, const std::filesystem::path& Input,
                                         std::filesystem::path& Result, std::string* Error = nullptr)
    {
        std::error_code FileError;
        const auto CanonicalRoot = std::filesystem::weakly_canonical(Root, FileError);
        if (FileError)
        {
            if (Error)
                *Error = "Cannot resolve source root: " + FileError.message();
            return false;
        }
        const auto Candidate = std::filesystem::weakly_canonical(ResolveBasicPath(Root, Input), FileError);
        if (FileError)
        {
            if (Error)
                *Error = "Cannot resolve source path: " + FileError.message();
            return false;
        }
        const auto Relative = Candidate.lexically_relative(CanonicalRoot);
        if (Relative.empty() || Relative.is_absolute() || *Relative.begin() == "..")
        {
            if (Error)
                *Error = "Source path must remain inside its root: " + Input.string();
            return false;
        }
        Result = Candidate;
        return true;
    }
} // namespace dbc::interpreter
