#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "core/file_dialog.hpp"
#include "core/new_project.hpp"
#include "core/platform.hpp"
#include "core/project_settings.hpp"
#include "core/recovery.hpp"
#include "core/settings.hpp"
#include "core/workspace.hpp"
#include "toolchain/android_keystore.hpp"
#include "toolchain/artifact_export_service.hpp"
#include "toolchain/execution_adapter.hpp"
#include "toolchain/export_job.hpp"
#include "toolchain/export_readiness.hpp"
#include "toolchain/icon_generator.hpp"
#if !defined(DBC_STUDIO_SANDBOXED)
#include "toolchain/shader_compiler.hpp"
#endif
#include "dba/command_database.hpp"
#include "dba/reference.hpp"
#include "interpreter/project_resolver.hpp"
#include "panels/asset_preview.hpp"
#include "panels/build_window.hpp"
#include "panels/code_panel.hpp"
#include "panels/editor_panel.hpp"
#include "panels/launcher_window.hpp"
#include "panels/media_panel.hpp"
#include "panels/output_panel.hpp"
#include "panels/project_panel.hpp"
#include "panels/project_settings_window.hpp"
#include "panels/scene_editor.hpp"
#include "panels/snippet_window.hpp"

// The OpenDBC Studio window and its docked panels.
namespace dbc::studio
{
    class App
    {
      public:
        // `openPath` is the optional file or folder passed on the command line.
        bool Initialize(const std::string& OpenPath,
                        std::optional<dbc::language::LanguageProfile> ForcedProfile = std::nullopt);
        void Run();
        void Shutdown();

      private:
        void PollEvents();
        void DrawFrame();
        void RenderFrame();

        void DrawDockSpace();
        void DrawMenuBar();
        void DrawToolbar();
        void DrawStatusBar();
        void DrawAbout();
        void DrawQuitConfirm();
        void DrawRecovery();
        void UpdateRecovery();
        void DrawProjectResolution();
        void DrawNewProject();
        void DrawPanels();
        void DrawCodePanels();
        void BuildDefaultLayout(ImGuiID DockspaceId);
        void HandleShortcuts();

        void OpenFolder(const std::filesystem::path& Folder);
        void OpenFile(const std::filesystem::path& File);
        void ShowOpenFileDialog();
        // The current tab when snippets can be inserted into it, else nullptr.
        TextEditor* SnippetTarget();
        void InsertSnippet(const core::Snippet& Value);
        void RequestNewProject();
        void CreateNewProject();
        bool ResolveWorkspace(const std::filesystem::path& Target,
                              std::optional<dbc::language::LanguageProfile> Forced = std::nullopt);
        void ApplyResolvedProject(dbc::interpreter::ResolvedProject Project);
        void SetProjectDialect(dbc::language::LanguageProfile Profile);
        void SaveCurrent();
        void SaveCurrentAs();
        void ContinueQuitSave();
        void RequestQuit();

        void RunActiveDocument();
        void RunLauncherPath(const std::filesystem::path& Path);
        void StopRun();
        void OpenBuildWindow();
        void DrawBuildWindow();
        toolchain::ArtifactExportRequest MakeBuildRequest() const;
        void RefreshExportChecks();
        void StartBuild();
        void CreateKeystore();
        void PollExport();

        enum class Mode
        {
            Code,
            Scene,
        };

        // Why the Scene mode cannot open for this project, or empty when it can.
        std::string SceneBlocker() const;
        void UpdateSceneProject();
        void SetMode(Mode Value);
        void RunScene();
        std::optional<std::string> ReadSource(const std::filesystem::path& Path) const;
        bool WriteSource(const std::filesystem::path& Path, const std::string& Text, bool Generated);

        bool CreatePresentation();
        void DestroyPresentation();

        SDL_Window* Window_ = nullptr;
        SDL_GPUDevice* Gpu_ = nullptr;

        // io.IniFilename does not own its string, so we keep it alive here.
        std::string IniPath_;

        float Scale_ = 1.0f;
        bool Running_ = false;
        bool LayoutBuilt_ = false;
        bool SceneLayoutBuilt_ = false;
        Mode Mode_ = Mode::Code;

        // Dock node the editor sits in, so the preview opens beside it even when
        // an older saved layout has no entry for it.
        ImGuiID EditorDockId_ = 0;
        // Same for the Code and Media panels, which open beside the Project panel.
        ImGuiID ProjectDockId_ = 0;

        // Panel visibility
        bool ShowProject_ = true;
        bool ShowOutput_ = true;
        bool ShowCode_ = true;
        bool ShowMedia_ = true;
        bool ShowDemo_ = false;
        bool ShowAbout_ = false;
        bool QuitRequested_ = false;
        bool QuitSaveInProgress_ = false;

        core::Recovery Recovery_;
        std::vector<core::RecoveryCopy> RecoveryCopies_;
        bool RecoveryPopupRequested_ = false;
        std::uint64_t NextRecovery_ = 0;
        std::string RecoveryError_;

        panels::ProjectPanel Project_;
        panels::EditorPanel Editor_;
        panels::OutputPanel Output_;
        panels::AssetPreview Preview_;
        panels::CodePanel Code_;
        panels::MediaPanel Media_;
        panels::LauncherWindow Launcher_;
        panels::SceneEditor Scene_;
        panels::SnippetWindow Snippets_;

        dba::Reference Reference_;
        dba::CommandDatabase Commands_;
        bool SettingsLoaded_ = false;
        bool SdlInitialized_ = false;
        bool WindowClaimed_ = false;
        bool ImguiPlatformInitialized_ = false;
        bool ImguiGpuInitialized_ = false;
        bool RenderFailed_ = false;
        core::Settings Settings_;
        core::ProjectSettings ProjectSettings_;
        core::FileDialog Dialog_;
        core::Workspace Workspace_;

        toolchain::ExecutionAdapter Execution_;
        toolchain::ExportJob ExportJob_;
        std::filesystem::path LastExport_;
        panels::BuildWindow BuildWindow_;
        // Signing passwords for this session only; never saved.
        toolchain::ExportSecrets ExportSecrets_;
        std::vector<toolchain::ExportCheck> ExportChecks_;
        // Set while a signed Android build runs whose success should bump the version code.
        bool IncrementVersionAfterExport_ = false;
        toolchain::RunOptions RunOptions_;
        toolchain::IconGenerator IconGenerator_;
#if !defined(DBC_STUDIO_SANDBOXED)
        toolchain::ShaderCompiler ShaderCompiler_;
#endif
        panels::ProjectSettingsWindow SettingsWindow_;
        bool ExecutionBusy_ = false;
        std::filesystem::path ExecutionTarget_;

        std::optional<dbc::language::LanguageProfile> InitialProfile_;
        std::optional<dbc::interpreter::ResolvedProject> ResolvedProject_;
        dbc::interpreter::ProjectResolveResult PendingResolution_;
        bool ResolutionPopupRequested_ = false;
        bool DialectLocked_ = false;
        std::filesystem::path PendingResolveTarget_;
        std::optional<dbc::language::LanguageProfile> PendingResolveProfile_;
        std::string PendingRawPath_;
        std::string PendingLocalPath_;

        bool NewProjectRequested_ = false;
        std::array<char, 65> NewProjectName_{};
        std::string NewProjectLocation_;
        dbc::language::LanguageProfile NewProjectProfile_ = dbc::language::LanguageProfile::Dbc;
        core::ProjectTemplate NewProjectTemplate_ = core::ProjectTemplate::BasicScene;
        std::string NewProjectError_;

        std::string Status_ = "Ready";
    };

} // namespace dbc::studio
