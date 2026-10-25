#pragma once

#include <filesystem>
#include <string>

namespace dbc::studio
{
    namespace toolchain
    {
        struct ShaderCompileResult
        {
            bool Success = false;
            std::filesystem::path ManifestPath;
            std::filesystem::path VertexPath;
            std::filesystem::path FragmentPath;
            std::string Error;
        };

        class ShaderCompiler
        {
          public:
            ShaderCompileResult CompileHlslEffect(const std::filesystem::path& Source) const;
        };
    } // namespace toolchain

} // namespace dbc::studio
