#include "dll_system.hpp"
#include "engine_kernel.hpp"
#include "memblock_system.hpp"
#include "path_utf8.hpp"

#include <SDL3/SDL_loadso.h>
#include <opendbc_plugin.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace dbc
{
    namespace
    {
        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

#if defined(_WIN32)
        constexpr const char* KLibraryExtension = ".dll";
#elif defined(__APPLE__)
        constexpr const char* KLibraryExtension = ".dylib";
#else
        constexpr const char* KLibraryExtension = ".so";
#endif

        std::vector<std::filesystem::path> Candidates(const std::filesystem::path& Path)
        {
            std::vector<std::filesystem::path> Result{Path};
            std::string Extension = dbc::PathToUtf8(Path.extension());
            std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                           [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            if (Extension == ".dll" && Extension != KLibraryExtension)
            {
                const std::string Stem = dbc::PathToUtf8(Path.stem());
                Result.push_back(Path.parent_path() / (Stem + KLibraryExtension));
                Result.push_back(Path.parent_path() / ("lib" + Stem + KLibraryExtension));
            }
            return Result;
        }

        // What a plugin function sees through OpenDbcHost.
        struct CallContext
        {
            MemblockSystem& Memblocks;
            std::string Failure;
        };

        void* ResolveAddress(void* Context, std::uint32_t Address, std::uint32_t Size)
        {
            return static_cast<CallContext*>(Context)->Memblocks.ResolveAddress(Address, Size);
        }

        void ReportFailure(void* Context, const char* Message)
        {
            static_cast<CallContext*>(Context)->Failure = Message ? Message : "";
        }
    } // namespace

    DllSystem::DllSystem(EngineKernel& Kernel) : Kernel_(Kernel) {}

    DllSystem::~DllSystem()
    {
        for (const auto& [Number, Library] : Libraries_)
            SDL_UnloadObject(Library);
    }

    bool DllSystem::ValidateNumber(int Number, std::string* Error)
    {
        if (Number < 1 || Number > 256)
            return Fail(Error, "DLL number must be between 1 and 256");
        return true;
    }

    bool DllSystem::LoadDll(const std::filesystem::path& Path, int Number, std::string* Error)
    {
        if (!ValidateNumber(Number, Error))
            return false;
        if (Libraries_.contains(Number))
            return Fail(Error, "LOAD DLL cannot replace existing DLL " + std::to_string(Number));
        std::filesystem::path Found;
        for (const std::filesystem::path& Candidate : Candidates(Path))
        {
            std::error_code StatusError;
            if (std::filesystem::is_regular_file(Candidate, StatusError))
            {
                Found = Candidate;
                break;
            }
        }
        if (Found.empty())
            return Fail(Error, "LOAD DLL cannot find '" + dbc::PathToUtf8(Path) + "'");

        SDL_SharedObject* Library = SDL_LoadObject(dbc::PathToUtf8(Found).c_str());
        if (Library == nullptr)
            return Fail(Error, "LOAD DLL cannot load '" + dbc::PathToUtf8(Found) + "': " + SDL_GetError());
        using AbiVersion = std::uint32_t (*)();
        const auto Version = reinterpret_cast<AbiVersion>(SDL_LoadFunction(Library, "opendbc_plugin_abi_version"));
        if (Version == nullptr || Version() != OPENDBC_PLUGIN_ABI_VERSION)
        {
            SDL_UnloadObject(Library);
            return Fail(Error, "LOAD DLL '" + dbc::PathToUtf8(Found) + "' is not an OpenDBC plugin of ABI version " +
                                   std::to_string(OPENDBC_PLUGIN_ABI_VERSION));
        }
        Libraries_[Number] = Library;
        return true;
    }

    bool DllSystem::DeleteDll(int Number, std::string* Error)
    {
        if (!ValidateNumber(Number, Error))
            return false;
        const auto Found = Libraries_.find(Number);
        if (Found == Libraries_.end())
            return Fail(Error, "DELETE DLL requires existing DLL " + std::to_string(Number));
        SDL_UnloadObject(Found->second);
        Libraries_.erase(Found);
        return true;
    }

    bool DllSystem::DllExist(int& Output, int Number, std::string* Error) const
    {
        if (!ValidateNumber(Number, Error))
            return false;
        Output = Libraries_.contains(Number) ? 1 : 0;
        return true;
    }

    bool DllSystem::DllCallExist(int& Output, int Number, const std::string& Function, std::string* Error) const
    {
        if (!ValidateNumber(Number, Error))
            return false;
        const auto Found = Libraries_.find(Number);
        Output = Found != Libraries_.end() && SDL_LoadFunction(Found->second, Function.c_str()) != nullptr ? 1 : 0;
        return true;
    }

    bool DllSystem::CallDll(Value& Result, int Number, const std::string& Function, std::span<const Value> Arguments,
                            std::string* Error)
    {
        if (!ValidateNumber(Number, Error))
            return false;
        const auto Found = Libraries_.find(Number);
        if (Found == Libraries_.end())
            return Fail(Error, "CALL DLL requires existing DLL " + std::to_string(Number));
        const auto Entry = reinterpret_cast<OpenDbcFunction>(SDL_LoadFunction(Found->second, Function.c_str()));
        if (Entry == nullptr)
            return Fail(Error, "CALL DLL cannot find function '" + Function + "' in DLL " + std::to_string(Number));

        std::vector<OpenDbcValue> Raw(Arguments.size());
        for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
        {
            const Value& Source = Arguments[Index];
            Raw[Index].type = static_cast<std::int32_t>(Source.Type);
            Raw[Index].integer = Source.Integer;
            Raw[Index].real = Source.Real;
            Raw[Index].string = Source.Type == ValueType::String ? Source.Text.c_str() : nullptr;
        }
        CallContext Context{Kernel_.Memblocks(), {}};
        const OpenDbcHost Host{OPENDBC_PLUGIN_ABI_VERSION, &Context, &ResolveAddress, &ReportFailure};
        OpenDbcValue Returned{};
        const std::int32_t Status = Entry(Raw.data(), static_cast<std::int32_t>(Raw.size()), &Returned, &Host);
        if (Status != 0)
            return Fail(Error, "CALL DLL '" + Function + "' failed" +
                                   (Context.Failure.empty() ? " with status " + std::to_string(Status)
                                                            : ": " + Context.Failure));

        Result = Value{};
        switch (Returned.type)
        {
            case OPENDBC_VALUE_NONE:
                return true;
            case OPENDBC_VALUE_INTEGER:
                Result.Type = ValueType::Integer;
                Result.Integer = Returned.integer;
                return true;
            case OPENDBC_VALUE_REAL:
                Result.Type = ValueType::Real;
                Result.Real = Returned.real;
                return true;
            case OPENDBC_VALUE_STRING:
                if (Returned.string == nullptr)
                    break;
                Result.Type = ValueType::String;
                Result.Text = Returned.string;
                return true;
            default:
                break;
        }
        return Fail(Error, "CALL DLL '" + Function + "' returned an invalid value");
    }

} // namespace dbc
