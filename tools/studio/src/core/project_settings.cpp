#include <algorithm>
#include "project_settings.hpp"

#include <fstream>
#include <limits>
#include "interpreter/atomic_file.hpp"
#include "engine/systems/log.hpp"

#include <nlohmann/json.hpp>

namespace dbc::studio
{
    namespace
    {
        using nlohmann::json;

        const json& Object(const json& Parent, const char* Key)
        {
            static const json KEmpty = json::object();
            const auto It = Parent.find(Key);
            return (It != Parent.end() && It->is_object()) ? *It : KEmpty;
        }

        std::string Str(const json& J, const char* Key, const std::string& Fallback = std::string())
        {
            const auto It = J.find(Key);
            return (It != J.end() && It->is_string()) ? It->get<std::string>() : Fallback;
        }

        int Int(const json& J, const char* Key, int Fallback)
        {
            const auto It = J.find(Key);
            return (It != J.end() && It->is_number_integer()) ? It->get<int>() : Fallback;
        }

        std::vector<std::string> StrArray(const json& J, const char* Key)
        {
            std::vector<std::string> Out;
            const auto It = J.find(Key);
            if (It == J.end() || !It->is_array())
            {
                return Out;
            }
            for (const json& V : *It)
            {
                if (V.is_string())
                {
                    Out.push_back(V.get<std::string>());
                }
            }
            return Out;
        }
    } // namespace

    namespace core
    {
        std::filesystem::path ProjectSettings::FilePath(const std::filesystem::path& ProjectRoot)
        {
            return ProjectRoot / ".opendbc-project.json";
        }

        ProjectSettings ProjectSettings::Load(const std::filesystem::path& ProjectRoot)
        {
            ProjectSettings S;

            const auto Path = FilePath(ProjectRoot);
            std::error_code FileError;
            const bool Exists = std::filesystem::exists(Path, FileError);
            if (!Exists && !FileError)
                return S;
            std::ifstream In(Path);
            if (!In || FileError)
            {
                S.LoadError = "Cannot open project settings";
            }
            else
            {
                const std::string Text(std::istreambuf_iterator<char>(In), {});
                if (In.bad())
                    S.LoadError = "Cannot read project settings";
                else
                    S = Parse(Text);
            }
            if (!S.LoadError.empty())
                DBC_LOG_ERROR("{}: {}", FilePath(ProjectRoot).string(), S.LoadError);
            return S;
        }

        ProjectSettings ProjectSettings::Parse(const std::string& Text)
        {
            ProjectSettings S;
            const json Root = json::parse(Text, nullptr, false);

            if (Root.is_discarded() || !Root.is_object())
            {
                S.LoadError = "Project settings must be a JSON object";
                return S;
            }
            const auto Check = [&](const char* Pointer, auto Predicate)
            {
                const json::json_pointer Path(Pointer);
                if (Root.contains(Path) && !Predicate(Root.at(Path)) && S.LoadError.empty())
                    S.LoadError = std::string("Invalid project setting type or value: ") + Pointer;
            };
            for (const char* Path : {"/general", "/icons", "/ios", "/ios/permissions", "/android", "/language"})
                Check(Path, [](const json& Value) { return Value.is_object(); });
            for (const char* Path :
                 {"/general/appName", "/general/bundleId", "/general/versionName", "/icons/source", "/ios/teamId",
                  "/ios/deploymentTarget", "/ios/permissions/NSCameraUsageDescription",
                  "/ios/permissions/NSMicrophoneUsageDescription",
                  "/ios/permissions/NSLocationWhenInUseUsageDescription",
                  "/ios/permissions/NSPhotoLibraryUsageDescription", "/android/screenOrientation",
                  "/android/keystorePath", "/android/keyAlias", "/language/dialect", "/language/projectFile",
                  "/language/entry"})
                Check(Path, [](const json& Value) { return Value.is_string(); });
            for (const char* Path : {"/general/versionCode", "/android/minSdk", "/android/targetSdk"})
                Check(Path,
                      [](const json& Value)
                      {
                          if (!Value.is_number_integer())
                              return false;
                          if (Value.is_number_unsigned())
                              return Value.get<std::uint64_t>() <= std::numeric_limits<int>::max();
                          const auto Number = Value.get<std::int64_t>();
                          return Number >= std::numeric_limits<int>::min() && Number <= std::numeric_limits<int>::max();
                      });
            for (const char* Path : {"/ios/orientations", "/android/permissions"})
                Check(Path,
                      [](const json& Value)
                      {
                          return Value.is_array() && std::all_of(Value.begin(), Value.end(),
                                                                 [](const json& Item) { return Item.is_string(); });
                      });
            Check("/language/dialect", [](const json& Value)
                  { return Value.is_string() && dbc::language::ParseProfile(Value.get<std::string>()).has_value(); });
            if (!S.LoadError.empty())
                return S;
            S.PreservedJson = Root.dump();

            const json& Gen = Object(Root, "general");
            S.AppName = Str(Gen, "appName");
            S.BundleId = Str(Gen, "bundleId");
            S.VersionName = Str(Gen, "versionName", "1.0.0");
            S.VersionCode = Int(Gen, "versionCode", 1);

            S.IconSource = Str(Object(Root, "icons"), "source");

            const json& Ios = Object(Root, "ios");
            S.IosTeamId = Str(Ios, "teamId");
            S.IosDeploymentTarget = Str(Ios, "deploymentTarget", "16.0");
            S.IosOrientations = StrArray(Ios, "orientations");
            if (S.IosOrientations.empty())
            {
                S.IosOrientations = {"LandscapeLeft", "LandscapeRight"};
            }
            const json& IosPerms = Object(Ios, "permissions");
            S.IosCameraDesc = Str(IosPerms, "NSCameraUsageDescription");
            S.IosMicrophoneDesc = Str(IosPerms, "NSMicrophoneUsageDescription");
            S.IosLocationDesc = Str(IosPerms, "NSLocationWhenInUseUsageDescription");
            S.IosPhotoDesc = Str(IosPerms, "NSPhotoLibraryUsageDescription");

            const json& Android = Object(Root, "android");
            S.AndroidMinSdk = Int(Android, "minSdk", 29);
            S.AndroidTargetSdk = Int(Android, "targetSdk", 36);
            S.AndroidOrientation = Str(Android, "screenOrientation", "sensorLandscape");
            S.AndroidPermissions = StrArray(Android, "permissions");
            S.AndroidKeystorePath = Str(Android, "keystorePath");
            S.AndroidKeyAlias = Str(Android, "keyAlias", "release");

            const json& Language = Object(Root, "language");
            S.LanguageDialect = dbc::language::ParseProfile(Str(Language, "dialect"));
            S.LanguageProjectFile = Str(Language, "projectFile");
            S.LanguageEntry = Str(Language, "entry");

            return S;
        }

        bool ProjectSettings::Save(const std::filesystem::path& ProjectRoot) const
        {
            if (!LoadError.empty())
            {
                DBC_LOG_ERROR("Cannot overwrite invalid project settings: {}", LoadError);
                return false;
            }
            json Root = json::parse(PreservedJson, nullptr, false);
            if (Root.is_discarded() || !Root.is_object())
                Root = json::object();

            json Gen = Object(Root, "general");
            Gen["appName"] = AppName;
            Gen["bundleId"] = BundleId;
            Gen["versionName"] = VersionName;
            Gen["versionCode"] = VersionCode;

            json Ios = Object(Root, "ios");
            Ios["teamId"] = IosTeamId;
            Ios["deploymentTarget"] = IosDeploymentTarget;
            Ios["orientations"] = IosOrientations;

            json IosPerms = Object(Ios, "permissions");
            if (!IosCameraDesc.empty())
            {
                IosPerms["NSCameraUsageDescription"] = IosCameraDesc;
            }
            if (!IosMicrophoneDesc.empty())
            {
                IosPerms["NSMicrophoneUsageDescription"] = IosMicrophoneDesc;
            }
            if (!IosLocationDesc.empty())
            {
                IosPerms["NSLocationWhenInUseUsageDescription"] = IosLocationDesc;
            }
            if (!IosPhotoDesc.empty())
            {
                IosPerms["NSPhotoLibraryUsageDescription"] = IosPhotoDesc;
            }
            if (!IosPerms.empty())
            {
                Ios["permissions"] = IosPerms;
            }

            json Android = Object(Root, "android");
            Android.erase("keystorePassword");
            Android.erase("keyPassword");
            Android["minSdk"] = AndroidMinSdk;
            Android["targetSdk"] = AndroidTargetSdk;
            Android["screenOrientation"] = AndroidOrientation;
            Android["permissions"] = AndroidPermissions;
            if (!AndroidKeystorePath.empty())
            {
                Android["keystorePath"] = AndroidKeystorePath;
                Android["keyAlias"] = AndroidKeyAlias;
            }

            Root["general"] = Gen;
            if (!IconSource.empty())
            {
                json Icons;
                Icons["source"] = IconSource;
                Root["icons"] = Icons;
            }
            Root["ios"] = Ios;
            Root["android"] = Android;
            if (LanguageDialect)
            {
                json Language = Object(Root, "language");
                Language["dialect"] = *LanguageDialect == dbc::language::LanguageProfile::Dbc ? "dbc" : "dbpro";
                if (LanguageProjectFile.empty())
                    Language.erase("projectFile");
                else
                    Language["projectFile"] = LanguageProjectFile;
                if (LanguageEntry.empty())
                    Language.erase("entry");
                else
                    Language["entry"] = LanguageEntry;
                Root["language"] = std::move(Language);
            }

            std::string Error;
            try
            {
                if (dbc::interpreter::AtomicWriteText(FilePath(ProjectRoot), Root.dump(4) + "\n", &Error))
                    return true;
            }
            catch (const json::exception& Exception)
            {
                Error = Exception.what();
            }
            DBC_LOG_ERROR("Cannot save project settings: {}", Error);
            return false;
        }
    } // namespace core

} // namespace dbc::studio
