#include "build_window.hpp"
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
            if (ImGui::BeginCombo("Developer ID identity", Value.empty() ? "(none)" : Value.c_str()))
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
        void BuildWindow::Open(std::string OutputFolder)
        {
            if (OutputFolder_.empty())
                OutputFolder_ = std::move(OutputFolder);
            Open_ = true;
        }

        BuildWindow::Action BuildWindow::Draw(core::Settings& Studio, toolchain::ExportSecrets& Secrets,
                                              const std::vector<toolchain::ExportCheck>& Checks, bool Busy,
                                              std::string_view Status)
        {
            if (!Open_)
                return Action::None;
            Action Result = Action::None;
            ImGui::SetNextWindowSize(ImVec2(620.0f, 560.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Build", &Open_))
            {
                ImGui::End();
                return Action::None;
            }

            ImGui::BeginDisabled(Busy);
            ImGui::SeparatorText("Target");
            std::vector<TargetChoice> Choices = {{DesktopTargetName(), toolchain::ArtifactTarget::Desktop},
                                                 {"Android APK", toolchain::ArtifactTarget::AndroidApk},
                                                 {"Android App Bundle", toolchain::ArtifactTarget::AndroidBundle}};
#if defined(__APPLE__)
            Choices.push_back({"iOS", toolchain::ArtifactTarget::Ios});
#endif
            for (const TargetChoice& Choice : Choices)
            {
                if (ImGui::RadioButton(Choice.Label, Target_ == Choice.Target) && Target_ != Choice.Target)
                {
                    Target_ = Choice.Target;
                    Result = Action::Refresh;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();

            ImGui::SeparatorText("Mode");
            if (ImGui::RadioButton("Development (unsigned)", !Distribution_) && Distribution_)
            {
                Distribution_ = false;
                Result = Action::Refresh;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Distribution (signed)", Distribution_) && !Distribution_)
            {
                Distribution_ = true;
                Result = Action::Refresh;
            }

            ImGui::SeparatorText("Output");
            ImGui::SetNextItemWidth(-90.0f);
            TextField("##output", OutputFolder_);
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
                Result = Action::BrowseOutput;

            const bool Android = Target_ == toolchain::ArtifactTarget::AndroidApk ||
                                 Target_ == toolchain::ArtifactTarget::AndroidBundle;
            if (Distribution_ && Android)
            {
                ImGui::SeparatorText("Signing (this session only)");
                if (TextField("Keystore password", Secrets.AndroidKeystorePassword, ImGuiInputTextFlags_Password) ||
                    TextField("Key password", Secrets.AndroidKeyPassword, ImGuiInputTextFlags_Password))
                    Result = Action::Refresh;
            }
#if defined(_WIN32)
            if (Distribution_ && Target_ == toolchain::ArtifactTarget::Desktop &&
                Studio.WindowsCertificate.ends_with(".pfx"))
            {
                ImGui::SeparatorText("Signing (this session only)");
                if (TextField("Certificate password", Secrets.WindowsCertificatePassword,
                              ImGuiInputTextFlags_Password))
                    Result = Action::Refresh;
            }
#endif

            ImGui::SeparatorText("Checks");
            for (const toolchain::ExportCheck& Check : Checks)
            {
                const ImU32 Colour = Check.Passed ? Theme::Run : (Check.Required ? Theme::Error : Theme::Warning);
                ImGui::PushStyleColor(ImGuiCol_Text, Colour);
                ImGui::TextUnformatted(Check.Passed ? "OK" : (Check.Required ? "Missing" : "Warning"));
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
            if (ImGui::SmallButton("Check again"))
                Result = Action::Refresh;

            if (ImGui::CollapsingHeader("External tools"))
            {
                ImGui::TextDisabled("Saved for this computer; empty paths are detected automatically.");
                bool Changed = false;
                Changed |= TextField("JDK folder", Studio.JavaHome);
                Changed |= TextField("Android SDK folder", Studio.AndroidSdk);
#if defined(__APPLE__)
                Changed |= IdentityCombo(Identities_, Studio.MacSigningIdentity);
                Changed |= TextField("Notary profile", Studio.NotaryProfile);
#elif defined(_WIN32)
                Changed |= TextField("Certificate SHA-1 or .pfx", Studio.WindowsCertificate);
                Changed |= TextField("Timestamp URL", Studio.WindowsTimestampUrl);
#endif
                if (Changed)
                    Result = Action::Refresh;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            const bool Ready = toolchain::ReadyToExport(Checks) && !OutputFolder_.empty();
            if (Busy)
            {
                if (ImGui::Button("Cancel"))
                    Result = Action::Cancel;
            }
            else
            {
                ImGui::BeginDisabled(!Ready);
                if (ImGui::Button("Build"))
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
