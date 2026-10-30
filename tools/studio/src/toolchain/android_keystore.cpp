#include "android_keystore.hpp"
#include "core/i18n.hpp"
#include "path_utf8.hpp"
#include "process_runner.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <system_error>

namespace dbc::studio
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::string_view PasswordVariable = "OPENDBC_KEYSTORE_PASSWORD";
        // keytool refuses shorter passwords.
        constexpr std::size_t MinimumPasswordLength = 6;

        std::string EscapeDistinguishedValue(std::string_view Value)
        {
            std::string Result;
            for (const char Character : Value)
            {
                if (Character == ',' || Character == '+' || Character == '"' || Character == '\\' || Character == '<' ||
                    Character == '>' || Character == ';' || Character == '=')
                    Result += '\\';
                Result += Character;
            }
            return Result;
        }
    } // namespace

    namespace toolchain
    {
        std::string AndroidKeystoreDistinguishedName(const AndroidKeystoreRequest& Request)
        {
            std::string Name = "CN=" + EscapeDistinguishedValue(Request.CommonName);
            if (!Request.Organization.empty())
                Name += ", O=" + EscapeDistinguishedValue(Request.Organization);
            if (!Request.Locality.empty())
                Name += ", L=" + EscapeDistinguishedValue(Request.Locality);
            if (!Request.Country.empty())
                Name += ", C=" + EscapeDistinguishedValue(Request.Country);
            return Name;
        }

        std::vector<std::string> AndroidKeystoreArguments(const fs::path& JavaHome,
                                                          const AndroidKeystoreRequest& Request)
        {
#if defined(_WIN32)
            const fs::path Keytool = JavaHome / "bin/keytool.exe";
#else
            const fs::path Keytool = JavaHome / "bin/keytool";
#endif
            return {dbc::PathToUtf8(Keytool),
                    "-genkeypair",
                    "-noprompt",
                    "-storetype",
                    "PKCS12",
                    "-keystore",
                    dbc::PathToUtf8(Request.Path),
                    "-alias",
                    Request.Alias,
                    "-keyalg",
                    "RSA",
                    "-keysize",
                    "4096",
                    "-validity",
                    "10000",
                    "-dname",
                    AndroidKeystoreDistinguishedName(Request),
                    "-storepass:env",
                    std::string(PasswordVariable),
                    "-keypass:env",
                    std::string(PasswordVariable)};
        }

        bool CreateAndroidKeystore(const fs::path& JavaHome, const AndroidKeystoreRequest& Request, std::string& Error)
        {
            std::error_code FileError;
            if (JavaHome.empty())
                Error = Tr("Creating a keystore needs a JDK 17 or newer");
            else if (Request.Path.empty() || fs::exists(Request.Path, FileError))
                Error = Tr("Choose a keystore file that does not exist yet");
            else if (!fs::is_directory(Request.Path.parent_path(), FileError))
                Error = Tr("The keystore folder does not exist");
            else if (Request.Alias.empty() || Request.CommonName.empty())
                Error = Tr("A keystore needs a key alias and a name");
            else if (Request.Password.size() < MinimumPasswordLength)
                Error = Tr("The keystore password needs at least 6 characters");
            else if (!Request.Country.empty() &&
                     (Request.Country.size() != 2 ||
                      !std::all_of(Request.Country.begin(), Request.Country.end(),
                                   [](unsigned char Character) { return std::isalpha(Character); })))
                Error = Tr("The country is a two-letter code, such as FR");
            if (!Error.empty())
                return false;
            ProcessRequest Keytool{
                AndroidKeystoreArguments(JavaHome, Request), {{std::string(PasswordVariable), Request.Password}}, {}};
            ProcessResult Result = RunProcess(Keytool, {}, nullptr, false);
            if (!Result)
            {
                Error = std::move(Result.Error);
                return false;
            }
            return true;
        }
    } // namespace toolchain

} // namespace dbc::studio
