#include "build_window.hpp"
#include "core/i18n.hpp"
#include "theme.hpp"

#include <array>

#include <imgui.h>

namespace dbc::studio
{
    namespace
    {
        // ImGui edits fixed buffers, so each string field round-trips through one.
        bool TextField(const char* Label, std::string& Value, ImGuiInputTextFlags Flags = 0)
        {
            constexpr std::size_t Capacity = 1024;
            std::string Buffer = Value;
            Buffer.resize(Capacity, '\0');
            if (!ImGui::InputText(Label, Buffer.data(), Capacity, Flags))
                return false;
            Value = Buffer.c_str();
            return true;
        }

        const char* DesktopTargetName()
        {
#if defined(__APPLE__)
            return "macOS app";
#elif defined(_WIN32)
            return "Windows executable";
#else
            return "Linux folder";
#endif
        }

#if defined(__APPLE__)
        // Developer ID Application is the identity for apps distributed outside the Mac App Store.
        bool IdentityCombo(const std::vector<toolchain::SigningIdentity>& Identities, std::string& Value)
        {
            bool Changed = false;
            if (ImGui::BeginCombo(TrId("Developer ID identity"), Value.empty() ? Tr("(none)") : Value.c_str()))
            {
                for (const toolchain::SigningIdentity& Identity : Identities)
                {
                    if (!Identity.Name.starts_with("Developer ID Application:"))
                        continue;
                    if (ImGui::Selectable(Identity.Name.c_str(), Identity.Name == Value))
                    {
                        Value = Identity.Name;
                        Changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return Changed;
        }
#endif

        struct TargetChoice
        {
            const char* Label;
            toolchain::ArtifactTarget Target;
        };
    } // namespace

    namespace panels
    {
        BuildWindow::Action BuildWindow::DrawKeystoreDialog()
        {
            Action Result = Action::None;
            ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal(TrId("Create keystore"), nullptr))
                return Result;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(Tr("Google Play ties the app to this key. Keep a copy of the file and its password: "
                                      "without them, no update can be published."));
            ImGui::PopTextWrapPos();
            ImGui::SetNextItemWidth(-90.0f);
            TextField("##keystore", Keystore_.Path);
            ImGui::SameLine();
            if (ImGui::Button(TrId("Choose...")))
                Result = Action::ChooseKeystore;
            TextField(TrId("Key alias"), Keystore_.Alias);
            TextField(TrId("Your name"), Keystore_.CommonName);
            TextField(TrId("Organization"), Keystore_.Organization);
            TextField(TrId("City"), Keystore_.Locality);
            TextField(TrId("Country code"), Keystore_.Country);
            TextField(TrId("Password"), Keystore_.Password, ImGuiInputTextFlags_Password);
            TextField(TrId("Confirm password"), Keystore_.Confirm, ImGuiInputTextFlags_Password);
            const bool PasswordReady = Keystore_.Password.size() >= 6 && Keystore_.Password == Keystore_.Confirm;
            if (!PasswordReady)
                ImGui::TextDisabled("%s", Tr("The passwords must match and have at least 6 characters."));
            ImGui::BeginDisabled(!PasswordReady || Keystore_.Path.empty() || Keystore_.Alias.empty() ||
                                 Keystore_.CommonName.empty());
            if (ImGui::Button(TrId("Create")))
            {
                Result = Action::CreateKeystore;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(TrId("Cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return Result;
        }

        void BuildWindow::Open(std::string OutputFolder)
        {
            if (OutputFolder_.empty())
                OutputFolder_ = std::move(OutputFolder);
            Open_ = true;
        }

        BuildWindow::Action BuildWindow::Draw(core::Settings& Studio, const core::ProjectSettings& Project,
                                              toolchain::ExportSecrets& Secrets,
                                              const std::vector<toolchain::ExportCheck>& Checks, bool Busy,
                                              std::string_view Status)
        {
            if (!Open_)
                return Action::None;
            Action Result = Action::None;
            ImGui::SetNextWindowSize(ImVec2(620.0f, 560.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin(TrId("Build"), &Open_))
            {
                ImGui::End();
                return Action::None;
            }

            ImGui::BeginDisabled(Busy);
            ImGui::SeparatorText(Tr("Target"));
            std::vector<TargetChoice> Choices = {{DesktopTargetName(), toolchain::ArtifactTarget::Desktop},
                                                 {"Android APK", toolchain::ArtifactTarget::AndroidApk},
                                                 {"Android App Bundle", toolchain::ArtifactTarget::AndroidBundle}};
#if defined(__APPLE__)
            Choices.push_back({"iOS (Xcode project)", toolchain::ArtifactTarget::Ios});
#endif
            for (const TargetChoice& Choice : Choices)
            {
                if (ImGui::RadioButton(TrId(Choice.Label), Target_ == Choice.Target) && Target_ != Choice.Target)
                {
                    Target_ = Choice.Target;
                    Result = Action::Refresh;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();

            ImGui::SeparatorText(Tr("Mode"));
            if (ImGui::RadioButton(TrId("Development (unsigned)"), !Distribution_) && Distribution_)
            {
                Distribution_ = false;
                Result = Action::Refresh;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(TrId("Distribution (signed)"), Distribution_) && !Distribution_)
            {
                Distribution_ = true;
                Result = Action::Refresh;
            }

            ImGui::SeparatorText(Tr("Output"));
            ImGui::SetNextItemWidth(-90.0f);
            TextField("##output", OutputFolder_);
            ImGui::SameLine();
            if (ImGui::Button(TrId("Browse...")))
                Result = Action::BrowseOutput;

            const bool Android =
                Target_ == toolchain::ArtifactTarget::AndroidApk || Target_ == toolchain::ArtifactTarget::AndroidBundle;
            if (Distribution_ && Android)
            {
                ImGui::SeparatorText(Tr("Signing"));
                ImGui::TextUnformatted(
                    TrFmt("Keystore: {0}", {Project.AndroidKeystorePath.empty() ? std::string_view(Tr("(none)"))
                                                                                : Project.AndroidKeystorePath})
                        .c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(TrId("Create keystore...")))
                    ImGui::OpenPopup(TrId("Create keystore"));
                if (const Action Dialog = DrawKeystoreDialog(); Dialog != Action::None)
                    Result = Dialog;
                ImGui::TextDisabled("%s", Tr("Passwords are kept until the Studio closes and are never saved."));
                if (TextField(TrId("Keystore password"), Secrets.AndroidKeystorePassword,
                              ImGuiInputTextFlags_Password) ||
                    TextField(TrId("Key password"), Secrets.AndroidKeyPassword, ImGuiInputTextFlags_Password))
                    Result = Action::Refresh;
                ImGui::Checkbox(TrId("Increase the version code after each signed build"), &IncrementVersionCode_);
            }

            ImGui::SeparatorText(Tr("Checks"));
            for (const toolchain::ExportCheck& Check : Checks)
            {
                const ImU32 Colour = Check.Passed ? Theme::Run : (Check.Required ? Theme::Error : Theme::Warning);
                ImGui::PushStyleColor(ImGuiCol_Text, Colour);
                ImGui::TextUnformatted(Tr(Check.Passed ? "OK" : (Check.Required ? "Missing" : "Warning")));
                ImGui::PopStyleColor();
                ImGui::SameLine(90.0f);
                ImGui::TextUnformatted(Check.Label.c_str());
                if (!Check.Passed && !Check.Detail.empty())
                {
                    ImGui::Indent(90.0f);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("%s", Check.Detail.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Unindent(90.0f);
                }
            }
            if (ImGui::SmallButton(TrId("Check again")))
                Result = Action::Refresh;

            if (ImGui::CollapsingHeader(TrId("External tools")))
            {
                ImGui::TextDisabled("%s", Tr("Saved for this computer; empty paths are detected automatically."));
                bool Changed = false;
                Changed |= TextField(TrId("JDK folder"), Studio.JavaHome);
                Changed |= TextField(TrId("Android SDK folder"), Studio.AndroidSdk);
#if defined(__APPLE__)
                Changed |= IdentityCombo(Identities_, Studio.MacSigningIdentity);
                Changed |= TextField(TrId("Notary profile"), Studio.NotaryProfile);
#elif defined(_WIN32)
                Changed |= TextField(TrId("Certificate SHA-1"), Studio.WindowsCertificate);
                Changed |= TextField(TrId("Timestamp URL"), Studio.WindowsTimestampUrl);
#endif
                if (Changed)
                    Result = Action::Refresh;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            const bool Ready = toolchain::ReadyToExport(Checks) && !OutputFolder_.empty();
            if (Busy)
            {
                if (ImGui::Button(TrId("Cancel")))
                    Result = Action::Cancel;
            }
            else
            {
                ImGui::BeginDisabled(!Ready);
                if (ImGui::Button(TrId("Build")))
                    Result = Action::Build;
                ImGui::EndDisabled();
            }
            if (!Status.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(Status.data(), Status.data() + Status.size());
            }
            ImGui::End();
            return Result;
        }
    } // namespace panels

} // namespace dbc::studio
