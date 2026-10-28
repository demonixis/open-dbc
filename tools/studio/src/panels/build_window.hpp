#pragma once

#include "core/project_settings.hpp"
#include "core/settings.hpp"
#include "toolchain/artifact_export_service.hpp"
#include "toolchain/export_readiness.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dbc::studio
{
    namespace panels
    {
        // Build > Build...: chooses the target and the mode, shows what the target still needs, holds the
        // session's signing passwords and the machine's external tools, and starts the export.
        class BuildWindow
        {
          public:
            enum class Action
            {
                None,
                // The target, the mode or a tool changed: the checks must be computed again.
                Refresh,
                BrowseOutput,
                ChooseKeystore,
                CreateKeystore,
                Build,
                Cancel,
            };

            // The fields of the Create keystore dialog; the password is cleared once the keystore exists.
            struct KeystoreForm
            {
                std::string Path;
                std::string Alias = "release";
                std::string Password;
                std::string Confirm;
                std::string CommonName;
                std::string Organization;
                std::string Locality;
                std::string Country;
            };

            void Open(std::string OutputFolder);

            bool IsOpen() const
            {
                return Open_;
            }

            Action Draw(core::Settings& Studio, const core::ProjectSettings& Project, toolchain::ExportSecrets& Secrets,
                        const std::vector<toolchain::ExportCheck>& Checks, bool Busy, std::string_view Status);

            toolchain::ArtifactTarget Target() const
            {
                return Target_;
            }

            bool Distribution() const
            {
                return Distribution_;
            }

            const std::string& OutputFolder() const
            {
                return OutputFolder_;
            }

            void SetOutputFolder(std::string Folder)
            {
                OutputFolder_ = std::move(Folder);
            }

            KeystoreForm& Keystore()
            {
                return Keystore_;
            }

            // Google Play refuses an upload that reuses a version code.
            bool IncrementVersionCode() const
            {
                return IncrementVersionCode_;
            }

            void SetSigningIdentities(std::vector<toolchain::SigningIdentity> Identities)
            {
                Identities_ = std::move(Identities);
            }

          private:
            bool Open_ = false;
            toolchain::ArtifactTarget Target_ = toolchain::ArtifactTarget::Desktop;
            bool Distribution_ = false;
            std::string OutputFolder_;
            std::vector<toolchain::SigningIdentity> Identities_;
            KeystoreForm Keystore_;
            bool IncrementVersionCode_ = true;

            Action DrawKeystoreDialog();
        };
    } // namespace panels

} // namespace dbc::studio
