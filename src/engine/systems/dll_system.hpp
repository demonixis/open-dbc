#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>

struct SDL_SharedObject;

namespace dbc
{
    class EngineKernel;

    // DarkBASIC Classic DLL commands over native OpenDBC plugins (sdk/opendbc_plugin.h). Windows DLLs of the
    // period cannot load in a 64-bit process, so LOAD DLL accepts only libraries built for this ABI. See
    // docs/plugins.md.
    class DllSystem
    {
      public:
        enum class ValueType : std::int32_t
        {
            None = 0,
            Integer = 1,
            Real = 2,
            String = 3,
        };

        struct Value
        {
            ValueType Type = ValueType::None;
            std::int32_t Integer = 0;
            double Real = 0.0;
            std::string Text{};
        };

        explicit DllSystem(EngineKernel& Kernel);
        ~DllSystem();

        DllSystem(const DllSystem&) = delete;
        DllSystem& operator=(const DllSystem&) = delete;

        // A name ending in .dll also finds the platform's library with the same stem, with or without "lib".
        bool LoadDll(const std::filesystem::path& Path, int Number, std::string* Error = nullptr);
        bool DeleteDll(int Number, std::string* Error = nullptr);
        bool DllExist(int& Output, int Number, std::string* Error = nullptr) const;
        bool DllCallExist(int& Output, int Number, const std::string& Function, std::string* Error = nullptr) const;
        bool CallDll(Value& Result, int Number, const std::string& Function, std::span<const Value> Arguments,
                     std::string* Error = nullptr);

      private:
        static bool ValidateNumber(int Number, std::string* Error);

        EngineKernel& Kernel_;
        std::unordered_map<int, SDL_SharedObject*> Libraries_;
    };

} // namespace dbc
