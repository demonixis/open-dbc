#include "project_settings_window.hpp"
#include "core/i18n.hpp"
#include "engine/systems/log.hpp"
#include "interpreter/atomic_file.hpp"
#include "path_utf8.hpp"
#include "theme.hpp"

#include <algorithm>
#include <array>
#include <fstream>

#include <imgui.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace dbc::studio
{
    namespace
    {
        // ImGui edits fixed buffers, so each string field round-trips through one.
        bool TextField(const char* Label, std::string& Value, size_t Capacity = 512)
        {
            std::string Buffer = Value;
            Buffer.resize(Capacity, '\0');

            if (ImGui::InputText(Label, Buffer.data(), Capacity))
            {
                Value = Buffer.c_str();
                return true;
            }
            return false;
        }

        void CheckboxList(const char* Label, const std::vector<const char*>& Options,
                          std::vector<std::string>& Selected)
        {
            ImGui::TextUnformatted(Label);
            ImGui::Indent();
            for (const char* Option : Options)
            {
                const auto It = std::find(Selected.begin(), Selected.end(), Option);
                bool Checked = It != Selected.end();
                if (ImGui::Checkbox(Option, &Checked))
                {
                    if (Checked)
                    {
                        Selected.emplace_back(Option);
                    }
                    else if (It != Selected.end())
                    {
                        Selected.erase(It);
                    }
                }
            }
            ImGui::Unindent();
        }
    } // namespace

    namespace panels
    {
        void ProjectSettingsWindow::Open(const std::filesystem::path& ProjectRoot,
                                         const core::ProjectSettings& Settings,
                                         const std::optional<std::filesystem::path>& DbproProject)
        {
            Root_ = ProjectRoot;
            Draft_ = Settings;
            DbproProject_.reset();
            RawProject_.clear();
            if (DbproProject)
            {
                std::ifstream Input(*DbproProject, std::ios::binary);
                if (Input)
                {
                    std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
                    DbproProject_ = dbc::language::ParseDbProProject(*DbproProject, std::move(Text));
                    LoadRawProject();
                }
            }
            Open_ = true;
        }

        void ProjectSettingsWindow::LoadRawProject()
        {
            if (!DbproProject_)
                return;
            const std::size_t Capacity = std::max<std::size_t>(65536, DbproProject_->Text.size() + 4096);
            RawProject_.assign(Capacity, '\0');
            std::copy(DbproProject_->Text.begin(), DbproProject_->Text.end(), RawProject_.begin());
        }

        void ProjectSettingsWindow::SetProjectValue(std::string_view Key, std::string_view Value)
        {
            if (!DbproProject_)
                return;
            dbc::language::SetDbProProjectValue(*DbproProject_, Key, Value);
            LoadRawProject();
        }

        bool ProjectSettingsWindow::SaveProject() const
        {
            if (!DbproProject_)
                return true;
            std::string Error;
            if (!dbc::interpreter::AtomicWriteText(DbproProject_->LogicalPath, DbproProject_->Text, &Error))
            {
                DBC_LOG_ERROR("Cannot save DBPro project: {}", Error);
                return false;
            }
            return true;
        }

        bool ProjectSettingsWindow::Draw(core::ProjectSettings& Out)
        {
            if (!Open_)
            {
                return false;
            }

            bool Saved = false;

            ImGui::SetNextWindowSize(ImVec2(720.0f, 560.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin(TrId("Project Settings"), &Open_))
            {
                ImGui::End();
                return false;
            }

            ImGui::TextDisabled("%s", dbc::PathToUtf8(core::ProjectSettings::FilePath(Root_)).c_str());
            ImGui::Separator();

            if (ImGui::BeginTabBar("##settings"))
            {
                if (ImGui::BeginTabItem(TrId("General")))
                {
                    TextField(TrId("App name"), Draft_.AppName);
                    TextField(TrId("Bundle / package ID"), Draft_.BundleId);
                    TextField(TrId("Version name"), Draft_.VersionName, 64);
                    ImGui::InputInt(TrId("Version code"), &Draft_.VersionCode);

                    ImGui::SeparatorText(Tr("Icons"));
                    TextField(TrId("Icon source (1024x1024 PNG)"), Draft_.IconSource);

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("iOS"))
                {
                    TextField(TrId("Development team ID"), Draft_.IosTeamId, 64);
                    ImGui::TextWrapped("%s", Tr("Device signing reads OPENDBC_IOS_SIGNING_IDENTITY and "
                                                "OPENDBC_IOS_PROVISIONING_PROFILE from the export environment."));
                    TextField(TrId("Deployment target"), Draft_.IosDeploymentTarget, 32);

                    ImGui::SeparatorText(Tr("Orientations"));
                    CheckboxList(Tr("Supported"), {"LandscapeLeft", "LandscapeRight", "Portrait", "PortraitUpsideDown"},
                                 Draft_.IosOrientations);

                    ImGui::SeparatorText(Tr("Permission descriptions"));
                    ImGui::TextDisabled("%s", Tr("Leave empty to omit the key from Info.plist."));
                    TextField(TrId("Camera"), Draft_.IosCameraDesc);
                    TextField(TrId("Microphone"), Draft_.IosMicrophoneDesc);
                    TextField(TrId("Location"), Draft_.IosLocationDesc);
                    TextField(TrId("Photo library"), Draft_.IosPhotoDesc);

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Android"))
                {
                    ImGui::InputInt(TrId("Min SDK"), &Draft_.AndroidMinSdk);
                    ImGui::InputInt(TrId("Target SDK"), &Draft_.AndroidTargetSdk);

                    // Orientations and permissions are Android manifest values, shown verbatim: never translate them.
                    static const std::array<const char*, 5> KOrientations = {
                        "sensorLandscape", "landscape", "sensorPortrait", "portrait", "fullSensor",
                    };
                    if (ImGui::BeginCombo(TrId("Screen orientation"), Draft_.AndroidOrientation.c_str()))
                    {
                        for (const char* Orientation : KOrientations)
                        {
                            if (ImGui::Selectable(Orientation, Draft_.AndroidOrientation == Orientation))
                            {
                                Draft_.AndroidOrientation = Orientation;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SeparatorText(Tr("Permissions"));
                    CheckboxList(Tr("Requested"),
                                 {"INTERNET", "CAMERA", "RECORD_AUDIO", "ACCESS_FINE_LOCATION", "READ_EXTERNAL_STORAGE",
                                  "WRITE_EXTERNAL_STORAGE", "VIBRATE"},
                                 Draft_.AndroidPermissions);

                    ImGui::SeparatorText(Tr("Release signing"));
                    TextField(TrId("Keystore path"), Draft_.AndroidKeystorePath);
                    TextField(TrId("Key alias"), Draft_.AndroidKeyAlias, 128);
                    ImGui::TextWrapped("%s", Tr("Passwords are never stored in the project. Set "
                                                "OPENDBC_ANDROID_KEYSTORE_PASSWORD and OPENDBC_ANDROID_KEY_PASSWORD "
                                                "in the export environment."));

                    ImGui::EndTabItem();
                }

                if (DbproProject_ && ImGui::BeginTabItem(TrId("DBPro Sources")))
                {
                    for (const dbc::language::DbProProjectEntry& Entry : DbproProject_->Entries)
                    {
                        if (Entry.CanonicalKey != "main" && !Entry.CanonicalKey.starts_with("include") &&
                            !Entry.CanonicalKey.starts_with("line"))
                            continue;
                        std::string Value = Entry.Value;
                        if (TextField(Entry.Key.c_str(), Value))
                        {
                            SetProjectValue(Entry.Key, Value);
                            break;
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (DbproProject_ && ImGui::BeginTabItem(TrId("DBPro Display")))
                {
                    // Keys of the .dbpro file, shown verbatim like those of the other DBPro tabs: never translate them.
                    for (const char* Key : {"app title", "graphics mode", "window resolution", "fullscreen resolution",
                                            "commandlinearguments"})
                    {
                        std::string Value = DbproProject_->Value(Key).value_or(std::string{});
                        if (TextField(Key, Value))
                            SetProjectValue(Key, Value);
                    }
                    ImGui::EndTabItem();
                }

                if (DbproProject_ && ImGui::BeginTabItem(TrId("DBPro Media")))
                {
                    for (const dbc::language::DbProProjectEntry& Entry : DbproProject_->Entries)
                    {
                        if (Entry.CanonicalKey != "media root path" && !Entry.CanonicalKey.starts_with("media") &&
                            !Entry.CanonicalKey.starts_with("icon") && !Entry.CanonicalKey.starts_with("cursor") &&
                            !Entry.CanonicalKey.starts_with("pointer"))
                            continue;
                        std::string Value = Entry.Value;
                        if (TextField(Entry.Key.c_str(), Value))
                        {
                            SetProjectValue(Entry.Key, Value);
                            break;
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (DbproProject_ && ImGui::BeginTabItem(TrId("DBPro Version")))
                {
                    for (const dbc::language::DbProProjectEntry& Entry : DbproProject_->Entries)
                    {
                        if (!Entry.CanonicalKey.starts_with("ver") && Entry.CanonicalKey != "project name")
                            continue;
                        std::string Value = Entry.Value;
                        if (TextField(Entry.Key.c_str(), Value))
                        {
                            SetProjectValue(Entry.Key, Value);
                            break;
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (DbproProject_ && ImGui::BeginTabItem(TrId("DBPro Advanced")))
                {
                    ImGui::TextWrapped("%s",
                                       Tr("Unknown keys and legacy options are preserved. Active unsupported options "
                                          "are rejected by Run or Export with a targeted diagnostic."));
                    for (const dbc::language::DbProProjectEntry& Entry : DbproProject_->Entries)
                    {
                        std::string Value = Entry.Value;
                        ImGui::PushID(static_cast<int>(Entry.Line));
                        if (TextField(Entry.Key.c_str(), Value))
                        {
                            SetProjectValue(Entry.Key, Value);
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }

                if (DbproProject_ && ImGui::BeginTabItem(TrId("DBPro Source")))
                {
                    Theme::PushCodeFont();
                    const bool RawEdited = ImGui::InputTextMultiline("##dbpro-raw", RawProject_.data(),
                                                                     RawProject_.size(), ImVec2(-1.0f, -80.0f));
                    ImGui::PopFont();
                    if (RawEdited)
                    {
                        DbproProject_ = dbc::language::ParseDbProProject(DbproProject_->LogicalPath,
                                                                         std::string(RawProject_.data()));
                    }
                    for (const dbc::language::ProjectDiagnostic& Diagnostic : DbproProject_->Diagnostics)
                    {
                        const ImU32 Color = Diagnostic.Severity == dbc::language::ProjectDiagnosticSeverity::Error
                                                ? Theme::Error
                                                : Theme::Warning;
                        ImGui::TextColored(
                            ImColor(Color), "%s",
                            TrFmt("Line {0}: {1}", {std::to_string(Diagnostic.Line), Diagnostic.Message}).c_str());
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Separator();

            ImGui::BeginDisabled(Root_.empty());
            if (ImGui::Button(TrId("Save")))
            {
                if (Draft_.Save(Root_) && SaveProject())
                {
                    Out = Draft_;
                    Saved = true;
                    Open_ = false;
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TrId("Cancel")))
            {
                Open_ = false;
            }

            if (Root_.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Tr("Open a folder first."));
            }

            ImGui::End();
            return Saved;
        }
    } // namespace panels

} // namespace dbc::studio
