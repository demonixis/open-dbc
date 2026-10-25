#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace dbc::studio
{
    namespace toolchain
    {
        class IconGenerator
        {
          public:
            using LogHandler = std::function<void(const std::string& Message)>;

            void SetLogHandler(LogHandler Handler)
            {
                Log_ = std::move(Handler);
            }

            bool Generate(const std::filesystem::path& ProjectRoot, const std::string& IconSource);

          private:
            void WriteLog(const std::string& Message) const;

            LogHandler Log_;
        };
    } // namespace toolchain

} // namespace dbc::studio
