#pragma once

#include <filesystem>

namespace dbc::studio
{
    namespace core::paths
    {
        // Directory holding the running executable.
        const std::filesystem::path& ExecutableDir();

        // Where bundled data lives: Contents/Resources inside a macOS bundle,
        // next to the executable everywhere else.
        const std::filesystem::path& ResourceDir();

        // Executable on Windows/Linux, application bundle on Apple platforms.
        std::filesystem::path ProductPath();

        // Per-user writable directory for settings and layout.
        const std::filesystem::path& PrefDir();

        // Locates darkbasic_reference.md in the installed product.
        std::filesystem::path FindReferenceDoc();
    } // namespace core::paths

} // namespace dbc::studio
