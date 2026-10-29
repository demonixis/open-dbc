#include "settings.hpp"
#include "engine/systems/log.hpp"
#include "interpreter/atomic_file.hpp"
#include "path_utf8.hpp"
#include <type_traits>

#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"

namespace dbc::studio
{
    namespace
    {
        std::filesystem::path SettingsFile()
        {
            return core::paths::PrefDir() / "studio-settings.json";
        }

        template <typename T> T Get(const nlohmann::json& J, const char* Key, T Fallback, std::string& Error)
        {
            const auto It = J.find(Key);
            if (It == J.end())
            {
                return Fallback;
            }
            const bool Valid = std::is_same_v<T, bool> ? It->is_boolean() : It->is_string();
            if (!Valid)
            {
                if (Error.empty())
                    Error = std::string("Ignoring invalid Studio setting: ") + Key;
                return Fallback;
            }
            return It->get<T>();
        }
    } // namespace

    namespace core
    {
        namespace
        {
            std::string RemappingKey(const std::filesystem::path& Descriptor, std::string_view RawPath)
            {
                std::error_code Error;
                return dbc::GenericPathToUtf8(std::filesystem::absolute(Descriptor, Error).lexically_normal()) + "\n" +
                       std::string(RawPath);
            }
        } // namespace

        Settings Settings::Load()
        {
            Settings S;

            std::ifstream In(SettingsFile());
            if (!In)
            {
                return S;
            }

            std::string Error;
            const std::string Text{std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>()};
            Settings Result = Parse(Text, Error);
            if (!Error.empty())
                DBC_LOG_WARN("{}", Error);
            return Result;
        }

        Settings Settings::Parse(std::string_view Text, std::string& Error)
        {
            Error.clear();
            Settings S;
            const auto J = nlohmann::json::parse(Text, nullptr, false);
            if (J.is_discarded() || !J.is_object())
            {
                Error = "Studio settings must be a JSON object; using defaults";
                return S;
            }

            S.ProjectFolder = Get<std::string>(J, "folder", S.ProjectFolder, Error);
            S.LastFile = Get<std::string>(J, "lastFile", S.LastFile, Error);
            S.LogLevel = Get<std::string>(J, "logLevel", S.LogLevel, Error);
            S.DebugOverlay = Get<bool>(J, "debugOverlay", S.DebugOverlay, Error);
            if (const auto Values = J.find("projectPathRemappings"); Values != J.end() && Values->is_object())
            {
                for (const auto& [Key, Value] : Values->items())
                {
                    if (Value.is_string())
                        S.ProjectPathRemappings.emplace(Key, Value.get<std::string>());
                    else if (Error.empty())
                        Error = "Ignoring invalid Studio project path remapping";
                }
            }
            if (const auto Values = J.find("projectPathRemappings");
                Values != J.end() && !Values->is_object() && Error.empty())
                Error = "Ignoring invalid Studio project path remappings";
            S.JavaHome = Get<std::string>(J, "javaHome", S.JavaHome, Error);
            S.AndroidSdk = Get<std::string>(J, "androidSdk", S.AndroidSdk, Error);
            S.MacSigningIdentity = Get<std::string>(J, "macSigningIdentity", S.MacSigningIdentity, Error);
            S.NotaryProfile = Get<std::string>(J, "notaryProfile", S.NotaryProfile, Error);
            S.WindowsCertificate = Get<std::string>(J, "windowsCertificate", S.WindowsCertificate, Error);
            S.WindowsTimestampUrl = Get<std::string>(J, "windowsTimestampUrl", S.WindowsTimestampUrl, Error);
            if (const auto Values = J.find("exportFolders"); Values != J.end())
            {
                if (!Values->is_object())
                {
                    if (Error.empty())
                        Error = "Ignoring invalid Studio export folders";
                }
                else
                {
                    for (const auto& [Key, Value] : Values->items())
                    {
                        if (Value.is_string())
                            S.ExportFolders.emplace(Key, Value.get<std::string>());
                    }
                }
            }
            return S;
        }

        std::optional<std::filesystem::path> Settings::ProjectPathRemapping(const std::filesystem::path& Descriptor,
                                                                            std::string_view RawPath) const
        {
            const auto Found = ProjectPathRemappings.find(RemappingKey(Descriptor, RawPath));
            if (Found == ProjectPathRemappings.end())
                return std::nullopt;
            return dbc::PathFromUtf8(Found->second);
        }

        void Settings::SetProjectPathRemapping(const std::filesystem::path& Descriptor, std::string_view RawPath,
                                               const std::filesystem::path& LocalPath)
        {
            ProjectPathRemappings[RemappingKey(Descriptor, RawPath)] = dbc::PathToUtf8(LocalPath);
        }

        void Settings::ClearProjectPathRemappings()
        {
            ProjectPathRemappings.clear();
        }

        bool Settings::Save() const
        {
            nlohmann::json J;
            J["folder"] = ProjectFolder;
            J["lastFile"] = LastFile;
            J["logLevel"] = LogLevel;
            J["debugOverlay"] = DebugOverlay;
            J["projectPathRemappings"] = ProjectPathRemappings;
            J["javaHome"] = JavaHome;
            J["androidSdk"] = AndroidSdk;
            J["macSigningIdentity"] = MacSigningIdentity;
            J["notaryProfile"] = NotaryProfile;
            J["windowsCertificate"] = WindowsCertificate;
            J["windowsTimestampUrl"] = WindowsTimestampUrl;
            J["exportFolders"] = ExportFolders;

            std::string Error;
            try
            {
                if (dbc::interpreter::AtomicWriteText(SettingsFile(), J.dump(2) + '\n', &Error))
                    return true;
            }
            catch (const nlohmann::json::exception& Exception)
            {
                Error = Exception.what();
            }
            DBC_LOG_ERROR("Cannot save Studio settings: {}", Error);
            return false;
        }
    } // namespace core

} // namespace dbc::studio
