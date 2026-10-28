#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        // The upload key Google Play ties to an app: losing it, or its password, blocks every later update.
        struct AndroidKeystoreRequest
        {
            std::filesystem::path Path;
            std::string Alias = "release";
            std::string Password;
            std::string CommonName;
            std::string Organization;
            std::string Locality;
            std::string Country;
        };

        // keytool's -dname value, with the RFC 2253 special characters escaped.
        std::string AndroidKeystoreDistinguishedName(const AndroidKeystoreRequest& Request);
        // keytool's arguments. The password is read from OPENDBC_KEYSTORE_PASSWORD, never passed here.
        std::vector<std::string> AndroidKeystoreArguments(const std::filesystem::path& JavaHome,
                                                          const AndroidKeystoreRequest& Request);
        // Creates a PKCS12 keystore holding one RSA 4096 key valid for more than 25 years, as Google Play asks.
        bool CreateAndroidKeystore(const std::filesystem::path& JavaHome, const AndroidKeystoreRequest& Request,
                                   std::string& Error);
    } // namespace toolchain

} // namespace dbc::studio
