#include "app.hpp"
#include "engine/systems/log.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>
#include <iterator>

#include "core/paths.hpp"
#include "interpreter/atomic_file.hpp"
#include "theme.hpp"
#include "toolchain/active_run_planner.hpp"

namespace dbc::studio
{
    namespace
    {
        constexpr const char* KWindowTitle = "OpenDBC";
        constexpr const char* KDockSpaceName = "StudioDockSpace";
        constexpr const char* KSceneDockSpaceName = "SceneDockSpace";

        // Panel window titles, shared between the menu, the dock builder and the
        // panel bodies so the default layout keeps matching.
        constexpr const char* KPanelProject = "Project";
        constexpr const char* KPanelEditor = "Editor";
        constexpr const char* KPanelOutput = "Output";
        constexpr const char* KPanelProblems = "Problems";
        constexpr const char* KPanelPreview = "Preview";
        constexpr const char* KPanelCode = "Code";
        constexpr const char* KPanelMedia = "Media";
        // The Scene mode docks its own Output window: a title shared with the Code
        // mode would belong to one dock space only.
        constexpr const char* KPanelSceneOutput = "Output##Scene";
        constexpr std::array<const char*, 6> KLogLevels = {"trace", "debug", "info", "warn", "error", "off"};

        enum class ToolIcon
        {
            New,
            Open,
            Save,
            Run,
            Stop,
        };

        // Toolbar buttons are drawn as vector shapes on a 16-unit grid, so they stay
        // sharp at any DPI without an icon font or texture.
        bool ToolbarButton(const char* Id, ToolIcon Icon, bool Enabled, const char* Tooltip)
        {
            const float Size = ImGui::GetFrameHeight();
            ImGui::BeginDisabled(!Enabled);
            const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Size, Size));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Tooltip);

            const ImVec2 Min = ImGui::GetItemRectMin();
            ImDrawList* Draw = ImGui::GetWindowDrawList();
            if (Enabled && ImGui::IsItemHovered())
                Draw->AddRectFilled(Min, ImGui::GetItemRectMax(),
                                    ImGui::IsItemActive() ? Theme::Selection : Theme::Hover,
                                    ImGui::GetStyle().FrameRounding);

            const float Unit = Size / 16.0f;
            const float Thickness = std::max(1.0f, Unit * 1.2f);
            const auto At = [&](float X, float Y) { return ImVec2(Min.x + X * Unit, Min.y + Y * Unit); };
            const auto Tint = [Enabled](ImU32 Color)
            { return Enabled ? Color : (Color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 0x60); };
            const ImU32 Ink = Tint(Theme::Text);
            switch (Icon)
            {
                case ToolIcon::New:
                {
                    const ImVec2 Page[] = {At(4, 2), At(9.5f, 2), At(12.5f, 5), At(12.5f, 14), At(4, 14)};
                    Draw->AddPolyline(Page, 5, Ink, ImDrawFlags_Closed, Thickness);
                    const ImVec2 Fold[] = {At(9.5f, 2), At(9.5f, 5), At(12.5f, 5)};
                    Draw->AddPolyline(Fold, 3, Ink, ImDrawFlags_None, Thickness);
                    break;
                }
                case ToolIcon::Open:
                {
                    const ImVec2 Folder[] = {At(2, 4), At(6, 4), At(7.5f, 5.5f), At(14, 5.5f), At(14, 13), At(2, 13)};
                    Draw->AddPolyline(Folder, 6, Tint(Theme::String), ImDrawFlags_Closed, Thickness);
                    break;
                }
                case ToolIcon::Save:
                    Draw->AddRect(At(2.5f, 2.5f), At(13.5f, 13.5f), Tint(Theme::Keyword), Unit, ImDrawFlags_None,
                                  Thickness);
                    Draw->AddRectFilled(At(5, 2.5f), At(11, 6.5f), Tint(Theme::Keyword));
                    Draw->AddRect(At(5, 9), At(11, 13.5f), Tint(Theme::Keyword), 0.0f, ImDrawFlags_None, Thickness);
                    break;
                case ToolIcon::Run:
                    Draw->AddTriangleFilled(At(4.5f, 2.5f), At(13.5f, 8), At(4.5f, 13.5f), Tint(Theme::Run));
                    break;
                case ToolIcon::Stop:
                    Draw->AddRectFilled(At(4, 4), At(12, 12), Tint(Theme::Error), Unit);
                    break;
            }
            return Pressed;
        }

        // Height of the toolbar and status bar: one frame-height row plus vertical padding.
        float BarHeight()
        {
            return ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
        }

        void ToolbarSeparator()
        {
            ImGui::SameLine();
            const ImVec2 Cursor = ImGui::GetCursorScreenPos();
            const float Height = ImGui::GetFrameHeight();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(Cursor.x, Cursor.y + Height * 0.2f),
                                                ImVec2(Cursor.x, Cursor.y + Height * 0.8f), Theme::Border);
            ImGui::Dummy(ImVec2(1.0f, Height));
            ImGui::SameLine();
        }

        // One side of the toolbar's Code | Scene switch.
        bool ModeButton(const char* Label, bool Active, bool Enabled, const char* Tooltip)
        {
            ImGui::BeginDisabled(!Enabled);
            ImGui::PushStyleColor(ImGuiCol_Button, Active ? Theme::Selection : 0);
            const bool Pressed = ImGui::Button(Label);
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Tooltip);
            return Pressed && Enabled && !Active;
        }

        constexpr const char* KDbaFilter = "dba;dbpro";
        constexpr const char* KHlslFilter = "fx;hlsl";
        constexpr const char* KOpenFilter = "dba;dbpro;fx;hlsl";

        // Opens the folder holding Path in Finder, Explorer or the desktop's file manager.
        void RevealInFileManager(const std::filesystem::path& Path)
        {
            const std::string Folder = dbc::GenericPathToUtf8(std::filesystem::absolute(Path).parent_path());
            std::string Url = Folder.starts_with('/') ? "file://" : "file:///";
            static constexpr char Hex[] = "0123456789ABCDEF";
            for (const unsigned char Character : Folder)
            {
                if (std::isalnum(Character) || Character == '/' || Character == '-' || Character == '_' ||
                    Character == '.' || Character == '~' || Character == ':')
                {
                    Url += static_cast<char>(Character);
                }
                else
                {
                    Url += '%';
                    Url += Hex[Character >> 4];
                    Url += Hex[Character & 0x0f];
                }
            }
            if (!SDL_OpenURL(Url.c_str()))
                DBC_LOG_WARN("Cannot reveal '{}': {}", Folder, SDL_GetError());
        }
    } // namespace

    bool App::Initialize(const std::string& OpenPath, std::optional<dbc::language::LanguageProfile> ForcedProfile)
    {
        InitialProfile_ = ForcedProfile;
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            DBC_LOG_ERROR("SDL_Init: {}", SDL_GetError());
            return false;
        }

        SdlInitialized_ = true;
        Scale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        if (Scale_ <= 0.0f)
        {
            Scale_ = 1.0f;
        }

        if (!CreatePresentation())
        {
            return false;
        }

        Preview_.Initialize(Gpu_);
        panels::SceneHost SceneHost;
        SceneHost.ReadSource = [this](const std::filesystem::path& Path) { return ReadSource(Path); };
        SceneHost.WriteSource = [this](const std::filesystem::path& Path, const std::string& Text, bool Generated)
        { return WriteSource(Path, Text, Generated); };
        SceneHost.Log = [this](const std::string& Message, bool Error)
        {
            Output_.Append(Message,
                           Error ? panels::OutputPanel::Severity::Error : panels::OutputPanel::Severity::Notice);
        };
        Scene_.Initialize(Gpu_, std::move(SceneHost));
        Reference_.Load(core::paths::FindReferenceDoc());
        Commands_.Build(Reference_);
        Editor_.Initialize(&Commands_);

        Launcher_.Initialize(&Commands_, &Workspace_, &Dialog_);
        Snippets_.SetUserFolder(core::paths::PrefDir() / "snippets");
        Launcher_.SetHandlers([this](const std::filesystem::path& Path) { RunLauncherPath(Path); },
                              [this](const std::filesystem::path& Path) { OpenFile(Path); });

        Project_.SetOnActivate([this](const std::filesystem::path& Path) { OpenFile(Path); });
        Code_.SetOnSelect([this](std::uint32_t Line) { Editor_.GoToLine(Line); });
        Media_.SetOnActivate(
            [this](const std::filesystem::path& Path)
            {
                if (panels::AssetPreview::KindOf(Path) == panels::AssetPreview::Kind::None &&
                    !panels::EditorPanel::CanOpen(Path))
                    Status_ = "No preview for " + dbc::PathToUtf8(Path.filename());
                else
                    OpenFile(Path);
            });

        Settings_ = core::Settings::Load();
        SettingsLoaded_ = true;

        Workspace_.Initialize();

        RunOptions_.LogLevel = Settings_.LogLevel;
        RunOptions_.DebugOverlay = Settings_.DebugOverlay;

        IconGenerator_.SetLogHandler([this](const std::string& Message) { Output_.Append(Message); });

        const toolchain::ExecutionAdapter::LineHandler OnLine = [this](const std::string& Line, bool IsError)
        {
            Output_.Append(Line,
                           IsError ? panels::OutputPanel::Severity::Error : panels::OutputPanel::Severity::Normal);
        };
        const toolchain::ExecutionAdapter::FinishHandler OnFinish =
            [this](int ExitCode, const std::vector<toolchain::RunDiagnostic>& Diagnostics)
        {
            ExecutionBusy_ = false;
            ShowProblems(Diagnostics);
            ExecutionTarget_.clear();
            if (ExitCode == 0)
                Status_ = "Run finished";
            else if (ExitCode == 130)
                Status_ = "Stopped";
            else
                Status_ = "Run failed (exit " + std::to_string(ExitCode) + ")";
            Output_.Append(Status_, ExitCode == 0 || ExitCode == 130 ? panels::OutputPanel::Severity::Notice
                                                                     : panels::OutputPanel::Severity::Error);
        };

        Execution_.SetHandlers(OnLine, OnFinish);
        Problems_.SetOnSelect(
            [this](const std::filesystem::path& Path, std::uint32_t Line)
            {
                OpenFile(Path);
                Editor_.GoToLine(Line);
            });

        // Command line wins over the remembered folder.
        std::error_code Ec;
        if (!OpenPath.empty())
        {
            const std::filesystem::path Path = dbc::PathFromUtf8(OpenPath);
            if (std::filesystem::is_directory(Path, Ec))
            {
                OpenFolder(Path);
            }
            else
            {
                OpenFile(Path);
            }
        }
        else if (!Settings_.ProjectFolder.empty() &&
                 std::filesystem::is_directory(dbc::PathFromUtf8(Settings_.ProjectFolder), Ec))
        {
            OpenFolder(dbc::PathFromUtf8(Settings_.ProjectFolder));
            if (!Settings_.LastFile.empty() &&
                std::filesystem::is_regular_file(dbc::PathFromUtf8(Settings_.LastFile), Ec))
            {
                OpenFile(dbc::PathFromUtf8(Settings_.LastFile));
            }
        }

        Recovery_.SetDirectory(core::paths::PrefDir() / "recovery");
        RecoveryCopies_ = Recovery_.Pending();
        RecoveryPopupRequested_ = !RecoveryCopies_.empty();

        SDL_ShowWindow(Window_);
        Running_ = true;
        return true;
    }

    bool App::CreatePresentation()
    {
        const SDL_WindowFlags Flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        Window_ =
            SDL_CreateWindow(KWindowTitle, static_cast<int>(1440 * Scale_), static_cast<int>(900 * Scale_), Flags);
        if (Window_ == nullptr)
        {
            DBC_LOG_ERROR("SDL_CreateWindow: {}", SDL_GetError());
            return false;
        }
        SDL_SetWindowPosition(Window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        // No DXIL: the Scene view ships SPIR-V and MSL only, so Windows uses Vulkan.
        Gpu_ = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
        if (Gpu_ == nullptr)
        {
            DBC_LOG_ERROR("SDL_CreateGPUDevice: {}", SDL_GetError());
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(Gpu_, Window_))
        {
            DBC_LOG_ERROR("SDL_ClaimWindowForGPUDevice: {}", SDL_GetError());
            return false;
        }
        WindowClaimed_ = true;
        if (!SDL_SetGPUSwapchainParameters(Gpu_, Window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC))
        {
            DBC_LOG_ERROR("SDL_SetGPUSwapchainParameters: {}", SDL_GetError());
            return false;
        }

        // The ImGui context outlives this: only the backends are bound to the
        // window and device, so fonts, style and dock layout survive a suspend.
        if (ImGui::GetCurrentContext() == nullptr)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            ImGuiIO& Io = ImGui::GetIO();
            Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            Io.ConfigDpiScaleFonts = true;
            Io.ConfigDpiScaleViewports = true;

            IniPath_ = dbc::PathToUtf8((core::paths::PrefDir() / "studio-layout.ini"));
            Io.IniFilename = IniPath_.c_str();

            Theme::Apply(Scale_ * core::platform::UiScaleHint());
            ImGui::GetStyle().FontScaleDpi = Scale_ * core::platform::UiScaleHint();
        }

        if (!ImGui_ImplSDL3_InitForSDLGPU(Window_))
        {
            DBC_LOG_ERROR("ImGui SDL platform initialization failed: {}", SDL_GetError());
            return false;
        }
        ImguiPlatformInitialized_ = true;

        ImGui_ImplSDLGPU3_InitInfo Init = {};
        Init.Device = Gpu_;
        Init.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(Gpu_, Window_);
        Init.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        Init.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
        Init.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
        if (!ImGui_ImplSDLGPU3_Init(&Init))
        {
            DBC_LOG_ERROR("ImGui_ImplSDLGPU3_Init failed");
            return false;
        }

        ImguiGpuInitialized_ = true;
        Preview_.Initialize(Gpu_);
        return true;
    }

    void App::DestroyPresentation()
    {
        if (Gpu_ != nullptr)
        {
            SDL_WaitForGPUIdle(Gpu_);
        }

        if (ImguiGpuInitialized_)
        {
            ImGui_ImplSDLGPU3_Shutdown();
            ImguiGpuInitialized_ = false;
        }
        if (ImguiPlatformInitialized_)
        {
            ImGui_ImplSDL3_Shutdown();
            ImguiPlatformInitialized_ = false;
        }

        if (WindowClaimed_)
        {
            SDL_ReleaseWindowFromGPUDevice(Gpu_, Window_);
            WindowClaimed_ = false;
        }
        if (Gpu_ != nullptr)
        {
            SDL_DestroyGPUDevice(Gpu_);
            Gpu_ = nullptr;
        }
        if (Window_ != nullptr)
        {
            SDL_DestroyWindow(Window_);
            Window_ = nullptr;
        }
    }

    void App::Run()
    {
        while (Running_)
        {
            PollEvents();
            Dialog_.Poll();
            Execution_.Poll();
            PollExport();
            for (const std::string& Notice : Editor_.TakeNotices())
                Output_.Append(Notice, panels::OutputPanel::Severity::Notice);
            if (SDL_GetTicks() >= NextRecovery_)
            {
                UpdateRecovery();
                NextRecovery_ = SDL_GetTicks() + core::Recovery::KRecoveryInterval;
            }

            if (SDL_GetWindowFlags(Window_) & SDL_WINDOW_MINIMIZED)
            {
                if (!Execution_.IsVrPacing())
                    SDL_Delay(10);
                continue;
            }

            DrawFrame();
            RenderFrame();
        }
        // Quitting normally means every change was saved or deliberately discarded.
        Recovery_.Clear();
    }

    void App::UpdateRecovery()
    {
        std::string Error;
        if (Recovery_.Update(Editor_.UnsavedTexts(), Error))
        {
            RecoveryError_.clear();
            return;
        }
        // Reported once per distinct failure, not at every refresh.
        if (Error != RecoveryError_)
            Output_.Append("Cannot write a recovery copy: " + Error, panels::OutputPanel::Severity::Error);
        RecoveryError_ = Error;
    }

    void App::DrawRecovery()
    {
        constexpr const char* Popup = "Recover unsaved changes";
        if (RecoveryPopupRequested_)
        {
            ImGui::OpenPopup(Popup);
            RecoveryPopupRequested_ = false;
        }
        if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("OpenDBC did not quit normally. These tabs had unsaved changes:");
        ImGui::Spacing();
        for (const core::RecoveryCopy& Copy : RecoveryCopies_)
            ImGui::BulletText("%s", Copy.Content.Path.empty() ? Copy.Content.Title.c_str()
                                                              : dbc::PathToUtf8(Copy.Content.Path).c_str());
        ImGui::Spacing();
        if (ImGui::Button("Restore"))
        {
            for (const core::RecoveryCopy& Copy : RecoveryCopies_)
            {
                Editor_.OpenRecovered(Copy.Content);
                Recovery_.Remove(Copy.File);
            }
            RecoveryCopies_.clear();
            // The restored tabs get fresh copies before anything else can fail.
            UpdateRecovery();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            for (const core::RecoveryCopy& Copy : RecoveryCopies_)
                Recovery_.Remove(Copy.File);
            RecoveryCopies_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void App::PollEvents()
    {
        Execution_.BeginEventFrame();
        SDL_Event Event;
        while (SDL_PollEvent(&Event))
        {
            ImGui_ImplSDL3_ProcessEvent(&Event);
            Execution_.HandleEvent(Event);

            if (Event.type == SDL_EVENT_QUIT)
            {
                RequestQuit();
            }
            else if (Event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     Event.window.windowID == SDL_GetWindowID(Window_))
            {
                RequestQuit();
            }
        }
    }

    void App::RequestQuit()
    {
        if (QuitSaveInProgress_)
        {
            return;
        }
        if (Editor_.AnyDirty() || Scene_.Dirty())
        {
            QuitRequested_ = true;
            return;
        }
        Running_ = false;
    }

    void App::OpenFolder(const std::filesystem::path& Folder)
    {
        core::ProjectSettings LoadedSettings = core::ProjectSettings::Load(Folder);
        if (!LoadedSettings.LoadError.empty())
        {
            Status_ = LoadedSettings.LoadError;
            Output_.Append(Status_, panels::OutputPanel::Severity::Error);
            return;
        }
        DialectLocked_ = false;
        Project_.SetRoot(Folder);
        Media_.SetRoot(Folder);
        ProjectSettings_ = std::move(LoadedSettings);
        const std::string PreviousFolder = Settings_.ProjectFolder;
        Settings_.ProjectFolder = dbc::PathToUtf8(Folder);
        if (!PreviousFolder.empty() && dbc::PathFromUtf8(PreviousFolder) != Folder)
            Settings_.LastFile.clear();
        Settings_.Save();
        if (!ResolveWorkspace(Folder, InitialProfile_))
            Status_ = PendingResolution_.Error;
        InitialProfile_.reset();
    }

    void App::OpenFile(const std::filesystem::path& File)
    {
        if (dbc::language::IsDbProProject(File))
        {
            core::ProjectSettings LoadedSettings = core::ProjectSettings::Load(File.parent_path());
            if (!LoadedSettings.LoadError.empty())
            {
                Status_ = LoadedSettings.LoadError;
                Output_.Append(Status_, panels::OutputPanel::Severity::Error);
                return;
            }
            Project_.SetRoot(File.parent_path());
            Media_.SetRoot(File.parent_path());
            ProjectSettings_ = std::move(LoadedSettings);
            Settings_.ProjectFolder = dbc::PathToUtf8(File.parent_path());
            Settings_.Save();
            DialectLocked_ = true;
            if (ResolveWorkspace(File, dbc::language::LanguageProfile::DbPro) && ResolvedProject_)
            {
                SettingsWindow_.Open(File.parent_path(), ProjectSettings_, File);
                OpenFile(ResolvedProject_->EntrySource);
            }
            return;
        }
        // Images and sounds open in the preview panel rather than as text.
        if (panels::AssetPreview::KindOf(File) != panels::AssetPreview::Kind::None)
        {
            if (!Preview_.Open(File))
            {
                Output_.Append("[ERROR] Cannot preview " + dbc::PathToUtf8(File), panels::OutputPanel::Severity::Error);
            }
            Status_ = dbc::PathToUtf8(File.filename());
            return;
        }

        if (Project_.Root().empty() && dbc::language::IsLanguageSource(File))
            OpenFolder(File.parent_path());

        if (!Editor_.Open(File))
        {
            Output_.Append("[ERROR] Cannot open " + dbc::PathToUtf8(File), panels::OutputPanel::Severity::Error);
            Status_ = "Cannot open " + dbc::PathToUtf8(File.filename());
            return;
        }

        Settings_.LastFile = dbc::PathToUtf8(File);

        Settings_.Save();

        Status_ = dbc::PathToUtf8(File.filename());
    }

    void App::ShowOpenFileDialog()
    {
        const std::string Start = Project_.Root().empty() ? std::string() : dbc::PathToUtf8(Project_.Root());
        Dialog_.OpenFile(Window_, Start, "DBA source or HLSL effect", KOpenFilter,
                         [this](const std::string& Path)
                         {
                             if (!Path.empty())
                                 OpenFile(dbc::PathFromUtf8(Path));
                         });
    }

    TextEditor* App::SnippetTarget()
    {
        TextEditor* Editor = Editor_.CurrentEditor();
        if (Mode_ != Mode::Code || Editor == nullptr || !Editor_.CurrentIsBasic() || Editor->IsReadOnlyEnabled())
            return nullptr;
        return Editor;
    }

    void App::InsertSnippet(const core::Snippet& Value)
    {
        TextEditor* Editor = SnippetTarget();
        if (Editor == nullptr)
            return;
        const core::SnippetInsertion Plan =
            core::PlanSnippetInsertion(Editor->GetText(), Value, Editor_.CurrentProfile());
        if (!Plan.Error.empty())
        {
            Status_ = Plan.Error;
            Output_.Append(Plan.Error, panels::OutputPanel::Severity::Notice);
            return;
        }
        // Each case is one undoable edit, like a paste.
        switch (Plan.Where)
        {
            case core::SnippetInsertion::Place::Cursor:
                Editor->ReplaceSectionText(Editor->GetMainCursorSelection(), Plan.Text);
                break;
            case core::SnippetInsertion::Place::Line:
            case core::SnippetInsertion::Place::End:
            {
                const std::size_t Last = Editor->GetLineCount() - 1;
                // The index is in glyphs: count the UTF-8 lead bytes of the last line.
                const std::string LastText = Editor->GetLineText(Last);
                const std::size_t Glyphs = static_cast<std::size_t>(
                    std::count_if(LastText.begin(), LastText.end(), [](char Byte) { return (Byte & 0xC0) != 0x80; }));
                const TextEditor::DocPos At = Plan.Where == core::SnippetInsertion::Place::Line
                                                  ? TextEditor::DocPos(Plan.Line, 0)
                                                  : TextEditor::DocPos(Last, Glyphs);
                Editor->ReplaceSectionText(At, At, Plan.Text);
                Editor->ScrollToLine(At.line, TextEditor::Scroll::alignTop);
                Status_ = "Added " + Value.Name + " to the Snippets region at the end of the file";
                break;
            }
        }
        Editor->SetFocus();
    }

    void App::RequestNewProject()
    {
        if (ExecutionBusy_)
            return;
        NewProjectName_.fill('\0');
        NewProjectError_.clear();
        // Beside the open project, else where downloaded projects go. Sandboxed
        // builds can only write there anyway.
        const bool Beside = core::platform::HasFreeformFileSystem() && !Project_.Root().empty();
        NewProjectLocation_ = dbc::PathToUtf8((Beside ? Project_.Root().parent_path() : Workspace_.ImportRoot()));
        NewProjectProfile_ = ResolvedProject_ ? ResolvedProject_->Profile : dbc::language::LanguageProfile::Dbc;
        NewProjectRequested_ = true;
    }

    void App::CreateNewProject()
    {
        const std::filesystem::path Location = dbc::PathFromUtf8(NewProjectLocation_);
        if (!Workspace_.IsWritable(Location))
        {
            NewProjectError_ = "The location is read-only.";
            return;
        }
        const core::NewProjectResult Created =
            core::CreateProject({Location, NewProjectName_.data(), NewProjectProfile_, NewProjectTemplate_});
        if (!Created)
        {
            NewProjectError_ = Created.Error;
            return;
        }
        NewProjectError_.clear();
        ImGui::CloseCurrentPopup();
        Output_.Append("Created project " + dbc::PathToUtf8(Created.Root));
        OpenFolder(Created.Root);
        if (ResolvedProject_)
            OpenFile(Created.MainSource);
    }

    bool App::ResolveWorkspace(const std::filesystem::path& Target,
                               std::optional<dbc::language::LanguageProfile> Forced)
    {
        dbc::interpreter::ProjectResolveRequest Request;
        Request.Target = Target;
        Request.ForcedProfile = Forced;
        Request.RemapPath = [this](const std::filesystem::path& Descriptor, std::string_view RawPath)
        { return Settings_.ProjectPathRemapping(Descriptor, RawPath); };
        PendingResolveTarget_ = Target;
        PendingResolveProfile_ = Forced;
        PendingResolution_ = dbc::interpreter::ResolveProject(Request);
        if (!PendingResolution_)
        {
            ResolutionPopupRequested_ = true;
            ResolvedProject_.reset();
            UpdateSceneProject();
            const std::string Marker = "remapping";
            const std::size_t At = PendingResolution_.Error.find(Marker);
            const std::size_t Colon = PendingResolution_.Error.find(": ", At == std::string::npos ? 0 : At);
            PendingRawPath_ = Colon == std::string::npos ? std::string{} : PendingResolution_.Error.substr(Colon + 2);
            return false;
        }
        ApplyResolvedProject(std::move(*PendingResolution_.Project));
        PendingResolution_ = {};
        return true;
    }

    void App::ApplyResolvedProject(dbc::interpreter::ResolvedProject Project)
    {
        ResolvedProject_ = std::move(Project);
        Editor_.SetProfile(ResolvedProject_->Profile);
        Project_.SetDbProProject(ResolvedProject_->DescriptorPath, ResolvedProject_->OrderedSources,
                                 ResolvedProject_->AssetRoot);
        Media_.SetRoot(ResolvedProject_->AssetRoot.empty() ? ResolvedProject_->ProjectRoot
                                                           : ResolvedProject_->AssetRoot);
        if (ResolvedProject_->DescriptorPath)
            Launcher_.SetProject(*ResolvedProject_);
        else
            Launcher_.SetFolder(ResolvedProject_->ProjectRoot, ResolvedProject_->Profile);
        Status_ = "Opened " + dbc::PathToUtf8(ResolvedProject_->ProjectRoot) + " as " +
                  std::string(dbc::language::ProfileName(ResolvedProject_->Profile));
        for (const std::string& Warning : ResolvedProject_->Warnings)
            Output_.Append(Warning, panels::OutputPanel::Severity::Notice);
        UpdateSceneProject();
    }

    std::string App::SceneBlocker() const
    {
        if (Project_.Root().empty())
            return "Open a project folder to edit its levels.";
        if (!ResolvedProject_)
            return "The project's language and main source are not resolved yet.";
        if (ResolvedProject_->EntrySource.empty() || !dbc::language::IsLanguageSource(ResolvedProject_->EntrySource))
            return "The project needs a main .dba source that includes the level.";
        if (!Workspace_.IsWritable(ResolvedProject_->ProjectRoot))
            return "The project folder is read-only.";
        return {};
    }

    void App::UpdateSceneProject()
    {
        if (!SceneBlocker().empty())
        {
            Scene_.SetProject(std::nullopt);
            Mode_ = Mode::Code;
            return;
        }
        panels::SceneProject Project;
        Project.Root = ResolvedProject_->ProjectRoot;
        Project.AssetRoot =
            ResolvedProject_->AssetRoot.empty() ? ResolvedProject_->ProjectRoot : ResolvedProject_->AssetRoot;
        Project.EntrySource = ResolvedProject_->EntrySource;
        Project.Profile = ResolvedProject_->Profile;
        Project.HasDescriptor = ResolvedProject_->DescriptorPath.has_value();
        Scene_.SetProject(std::move(Project));
    }

    void App::SetMode(Mode Value)
    {
        if (Value == Mode::Scene && !SceneBlocker().empty())
            return;
        Mode_ = Value;
        if (Mode_ == Mode::Scene)
            Scene_.Activate();
    }

    std::optional<std::string> App::ReadSource(const std::filesystem::path& Path) const
    {
        if (std::optional<std::string> Buffer = Editor_.OpenText(Path))
            return Buffer;
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream)
            return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(Stream), {});
    }

    bool App::WriteSource(const std::filesystem::path& Path, const std::string& Text, bool Generated)
    {
        if (!Generated && Editor_.ReplaceText(Path, Text, false))
            return true;
        std::string Error;
        if (!dbc::interpreter::AtomicWriteText(Path, Text, &Error))
        {
            Output_.Append("[ERROR] " + Error, panels::OutputPanel::Severity::Error);
            return false;
        }
        if (Generated)
            Editor_.ReplaceText(Path, Text, true);
        Project_.Refresh();
        return true;
    }

    void App::RunScene()
    {
        if (ExecutionBusy_ || !ResolvedProject_)
            return;
        if (Scene_.Dirty() && !Scene_.Save())
            return;
        OpenFile(ResolvedProject_->EntrySource);
        RunActiveDocument();
    }

    void App::SetProjectDialect(dbc::language::LanguageProfile Profile)
    {
        if (ExecutionBusy_ || DialectLocked_ || Project_.Root().empty())
            return;
        if (!ResolveWorkspace(Project_.Root(), Profile))
            return;
        ProjectSettings_.LanguageDialect = Profile;
        if (Profile == dbc::language::LanguageProfile::Dbc)
            ProjectSettings_.LanguageProjectFile.clear();
        else if (ResolvedProject_ && ResolvedProject_->DescriptorPath)
            ProjectSettings_.LanguageProjectFile =
                dbc::GenericPathToUtf8(ResolvedProject_->DescriptorPath->lexically_relative(Project_.Root()));
        if (ResolvedProject_)
            ProjectSettings_.LanguageEntry =
                dbc::GenericPathToUtf8(ResolvedProject_->EntrySource.lexically_relative(Project_.Root()));
        if (!ProjectSettings_.Save(Project_.Root()))
            Output_.Append("Cannot save the project dialect", panels::OutputPanel::Severity::Error);
    }

    void App::SaveCurrent()
    {
        if (!Editor_.HasDocument())
        {
            return;
        }

        if (Editor_.SaveCurrent())
        {
            Status_ = "Saved " + dbc::PathToUtf8(Editor_.CurrentPath().filename());
            CheckCurrentDocument();
        }
        else
        {
            SaveCurrentAs();
        }
    }

    void App::SaveCurrentAs()
    {
        if (!Editor_.HasDocument())
        {
            return;
        }

        const std::string Start = Project_.Root().empty() ? std::string() : dbc::PathToUtf8(Project_.Root());
        const bool Basic = Editor_.CurrentIsBasic();
        Dialog_.SaveFile(Window_, Start, Basic ? "DBA source" : "HLSL effect", Basic ? KDbaFilter : KHlslFilter,
                         [this](const std::string& Path)
                         {
                             if (Path.empty())
                             {
                                 QuitSaveInProgress_ = false;
                                 Status_ = "Quit cancelled";
                                 return;
                             }
                             if (Editor_.SaveCurrentAs(dbc::PathFromUtf8(Path)))
                             {
                                 Status_ = "Saved " + dbc::PathToUtf8(dbc::PathFromUtf8(Path).filename());
                                 Project_.Refresh();
                                 if (QuitSaveInProgress_)
                                 {
                                     ContinueQuitSave();
                                 }
                             }
                             else
                             {
                                 QuitSaveInProgress_ = false;
                                 Output_.Append("[ERROR] Cannot write " + Path, panels::OutputPanel::Severity::Error);
                             }
                         });
    }

    void App::ContinueQuitSave()
    {
        // The level first: linking it may change the main source, saved just after.
        if (Scene_.Dirty() && !Scene_.Save())
        {
            QuitSaveInProgress_ = false;
            Status_ = "Could not save the level";
            return;
        }
        Editor_.SaveAll();
        if (Editor_.AnyDirtyWithPath())
        {
            QuitSaveInProgress_ = false;
            Status_ = "Could not save every modified document";
            Output_.Append("[ERROR] Could not save every modified document", panels::OutputPanel::Severity::Error);
            return;
        }
        if (Editor_.SelectFirstDirtyWithoutPath())
        {
            SaveCurrentAs();
            return;
        }
        if (!Editor_.AnyDirty() && !Scene_.Dirty())
        {
            Running_ = false;
        }
        QuitSaveInProgress_ = false;
    }

    void App::DrawFrame()
    {
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        DrawDockSpace();
        DrawPanels();
        HandleShortcuts();

        if (ShowAbout_)
        {
            DrawAbout();
        }

        DrawQuitConfirm();
        DrawRecovery();
        DrawProjectResolution();
        DrawNewProject();
        if (std::optional<core::Snippet> Picked = Snippets_.Draw())
            InsertSnippet(*Picked);

        ImGui::Render();
    }

    void App::DrawProjectResolution()
    {
        constexpr const char* Popup = "Select OpenDBC project language";
        if (ResolutionPopupRequested_)
        {
            ImGui::OpenPopup(Popup);
            ResolutionPopupRequested_ = false;
        }
        if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextWrapped("%s", PendingResolution_.Error.c_str());
        if (!PendingRawPath_.empty())
        {
            std::array<char, 2048> Path{};
            SDL_strlcpy(Path.data(), PendingLocalPath_.c_str(), Path.size());
            if (ImGui::InputText("Local replacement", Path.data(), Path.size()))
                PendingLocalPath_ = Path.data();
            if (ImGui::Button("Grant local remapping") && !PendingLocalPath_.empty())
            {
                std::filesystem::path Descriptor = PendingResolveTarget_;
                if (PendingResolution_.PendingDescriptor)
                    Descriptor = *PendingResolution_.PendingDescriptor;
                else if (!dbc::language::IsDbProProject(Descriptor) && !PendingResolution_.ProjectCandidates.empty())
                    Descriptor = PendingResolution_.ProjectCandidates.front();
                Settings_.SetProjectPathRemapping(Descriptor, PendingRawPath_, dbc::PathFromUtf8(PendingLocalPath_));
                Settings_.Save();
                ResolveWorkspace(PendingResolveTarget_, PendingResolveProfile_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
        }
        if (!PendingResolution_.ProjectCandidates.empty())
        {
            for (const std::filesystem::path& Candidate : PendingResolution_.ProjectCandidates)
            {
                if (ImGui::Button(dbc::PathToUtf8(Candidate.filename()).c_str()))
                {
                    ResolveWorkspace(Candidate, dbc::language::LanguageProfile::DbPro);
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Open folder as DBC"))
            {
                SetProjectDialect(dbc::language::LanguageProfile::Dbc);
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            if (ImGui::Button("DBC"))
            {
                SetProjectDialect(dbc::language::LanguageProfile::Dbc);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("DBPro"))
            {
                SetProjectDialect(dbc::language::LanguageProfile::DbPro);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void App::DrawNewProject()
    {
        constexpr const char* Popup = "New Project";
        if (NewProjectRequested_)
        {
            ImGui::OpenPopup(Popup);
            NewProjectRequested_ = false;
        }
        if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool Entered = ImGui::InputText("Name", NewProjectName_.data(), NewProjectName_.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if constexpr (core::platform::HasFreeformFileSystem())
        {
            std::array<char, 2048> Location{};
            SDL_strlcpy(Location.data(), NewProjectLocation_.c_str(), Location.size());
            if (ImGui::InputText("Location", Location.data(), Location.size()))
                NewProjectLocation_ = Location.data();
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                Dialog_.OpenFolder(Window_, NewProjectLocation_,
                                   [this](const std::string& Path)
                                   {
                                       if (!Path.empty())
                                           NewProjectLocation_ = Path;
                                   });
            }
        }
        else
        {
            ImGui::TextDisabled("Location: %s", NewProjectLocation_.c_str());
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Language");
        if (ImGui::RadioButton("DarkBASIC Classic", NewProjectProfile_ == dbc::language::LanguageProfile::Dbc))
            NewProjectProfile_ = dbc::language::LanguageProfile::Dbc;
        ImGui::SameLine();
        if (ImGui::RadioButton("DarkBASIC Professional", NewProjectProfile_ == dbc::language::LanguageProfile::DbPro))
            NewProjectProfile_ = dbc::language::LanguageProfile::DbPro;
        ImGui::TextUnformatted("Template");
        if (ImGui::RadioButton("Basic Scene", NewProjectTemplate_ == core::ProjectTemplate::BasicScene))
            NewProjectTemplate_ = core::ProjectTemplate::BasicScene;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A 1280x720 window, sync at 60 FPS and a spinning cube");
        ImGui::SameLine();
        if (ImGui::RadioButton("Empty", NewProjectTemplate_ == core::ProjectTemplate::Empty))
            NewProjectTemplate_ = core::ProjectTemplate::Empty;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A main loop and nothing else");

        ImGui::Spacing();
        const std::string NameError = core::ValidateProjectName(NewProjectName_.data());
        const bool Ready = NameError.empty() && !NewProjectLocation_.empty();
        if (NewProjectName_[0] != '\0' && !NameError.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error), "%s", NameError.c_str());
        else if (!NewProjectError_.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Error), "%s", NewProjectError_.c_str());
        else if (Ready)
        {
            const std::filesystem::path Root =
                dbc::PathFromUtf8(NewProjectLocation_) / dbc::PathFromUtf8(NewProjectName_.data());
            const char* Descriptor =
                NewProjectProfile_ == dbc::language::LanguageProfile::DbPro ? ".dbpro" : ".opendbc-project.json";
            ImGui::TextDisabled("Creates %s with main.dba and %s", dbc::PathToUtf8(Root).c_str(), Descriptor);
        }

        ImGui::BeginDisabled(!Ready || Dialog_.IsOpen());
        if (ImGui::Button("Create") || (Entered && Ready && !Dialog_.IsOpen()))
            CreateNewProject();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void App::DrawQuitConfirm()
    {
        constexpr const char* Popup = "Quit with unsaved changes";
        if (QuitRequested_)
        {
            ImGui::OpenPopup(Popup);
            QuitRequested_ = false;
        }

        if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("One or more documents have unsaved changes.");
        if (Editor_.AnyDirtyWithoutPath())
        {
            ImGui::TextWrapped("Save All will ask for a destination for each untitled document.");
        }
        ImGui::Spacing();

        if (ImGui::Button("Save All and Quit"))
        {
            QuitSaveInProgress_ = true;
            ImGui::CloseCurrentPopup();
            ContinueQuitSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and Quit"))
        {
            QuitSaveInProgress_ = false;
            Running_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            QuitSaveInProgress_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void App::HandleShortcuts()
    {
        const ImGuiIO& Io = ImGui::GetIO();

        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        {
            if (Io.KeyShift)
                StopRun();
            else if (Mode_ == Mode::Scene)
                RunScene();
            else
                RunActiveDocument();
            return;
        }

        const bool Command = Io.KeyCtrl || Io.KeySuper;
        if (Command && ImGui::IsKeyPressed(ImGuiKey_1, false))
            SetMode(Mode::Code);
        else if (Command && ImGui::IsKeyPressed(ImGuiKey_2, false))
            SetMode(Mode::Scene);

        if (Mode_ == Mode::Scene)
        {
            Scene_.HandleShortcuts();
            if (Command && ImGui::IsKeyPressed(ImGuiKey_S, false) && Scene_.Save())
                Status_ = "Level saved";
            return;
        }

        if (!Io.KeyCtrl && !Io.KeySuper)
        {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            if (Io.KeyShift)
            {
                SaveCurrentAs();
            }
            else
            {
                SaveCurrent();
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_J, false) && SnippetTarget() != nullptr)
        {
            Snippets_.OpenInsert(Editor_.CurrentProfile());
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
        {
            if (Io.KeyShift)
                RequestNewProject();
            else
                Editor_.NewFile();
        }
    }

    void App::DrawDockSpace()
    {
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(Viewport->WorkPos);
        ImGui::SetNextWindowSize(Viewport->WorkSize);
        ImGui::SetNextWindowViewport(Viewport->ID);

        ImGuiWindowFlags HostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##StudioHost", nullptr, HostFlags);
        ImGui::PopStyleVar(3);

        DrawMenuBar();
        DrawToolbar();

        // The default layout is only built on a first run: after that the dock node
        // is restored from the ini file, so it must be tested before DockSpace()
        // creates an empty one.
        const ImGuiID DockspaceId = ImGui::GetID(KDockSpaceName);
        const ImGuiID SceneDockspaceId = ImGui::GetID(KSceneDockSpaceName);
        if (!LayoutBuilt_)
        {
            LayoutBuilt_ = true;
            if (ImGui::DockBuilderGetNode(DockspaceId) == nullptr)
            {
                BuildDefaultLayout(DockspaceId);
            }
        }
        const bool SceneMode = Mode_ == Mode::Scene;
        if (SceneMode && !SceneLayoutBuilt_)
        {
            SceneLayoutBuilt_ = true;
            if (ImGui::DockBuilderGetNode(SceneDockspaceId) == nullptr)
                Scene_.BuildDefaultLayout(SceneDockspaceId, KPanelSceneOutput);
        }

        // Each mode has its own dock space. The hidden one stays alive so its
        // windows keep their places; its windows are simply not submitted.
        // Keeping a dock space alive creates its node, so a Scene layout that was
        // never built is left alone until the mode first opens.
        const ImGuiID HiddenDockspaceId = SceneMode ? DockspaceId : SceneDockspaceId;
        if (ImGui::DockBuilderGetNode(HiddenDockspaceId) != nullptr)
            ImGui::DockSpace(HiddenDockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_KeepAliveOnly);

        // Reserve a row at the bottom of the host window for the status bar, plus
        // the item spacing ImGui inserts between the dock space and it.
        const float StatusHeight = BarHeight() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::DockSpace(SceneMode ? SceneDockspaceId : DockspaceId, ImVec2(0.0f, -StatusHeight),
                         ImGuiDockNodeFlags_None);

        DrawStatusBar();

        ImGui::End();
    }

    void App::DrawPanels()
    {
        if (Mode_ == Mode::Scene)
        {
            Scene_.DrawPanels();
            if (ImGui::Begin(KPanelSceneOutput))
                Output_.Draw();
            ImGui::End();
        }
        else
        {
            DrawCodePanels();
        }

        Launcher_.Draw(Window_);
        DrawBuildWindow();

        if (SettingsWindow_.Draw(ProjectSettings_))
        {
            RefreshExportChecks();
            Status_ = "Project settings saved";
            const std::filesystem::path Target = ResolvedProject_ && ResolvedProject_->DescriptorPath
                                                     ? *ResolvedProject_->DescriptorPath
                                                     : Project_.Root();
            ResolveWorkspace(Target,
                             DialectLocked_ ? std::optional(dbc::language::LanguageProfile::DbPro) : std::nullopt);
        }
    }

    void App::DrawCodePanels()
    {
        if (ShowProject_)
        {
            ImGui::Begin(KPanelProject, &ShowProject_);
            ProjectDockId_ = ImGui::GetWindowDockID();
            Project_.Draw();
            ImGui::End();
        }

        if (ShowCode_)
        {
            if (ProjectDockId_ != 0)
                ImGui::SetNextWindowDockID(ProjectDockId_, ImGuiCond_FirstUseEver);
            // Only a visible panel parses: a hidden tab costs nothing while typing.
            if (ImGui::Begin(KPanelCode, &ShowCode_))
            {
                Code_.Update(Editor_.CurrentIsBasic() ? Editor_.CurrentEditor() : nullptr, Editor_.CurrentPath(),
                             Editor_.CurrentProfile(), ImGui::GetTime());
                Code_.Draw();
            }
            ImGui::End();
        }

        if (ShowMedia_)
        {
            if (ProjectDockId_ != 0)
                ImGui::SetNextWindowDockID(ProjectDockId_, ImGuiCond_FirstUseEver);
            if (ImGui::Begin(KPanelMedia, &ShowMedia_))
                Media_.Draw();
            ImGui::End();
        }

        ImGui::Begin(KPanelEditor, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        EditorDockId_ = ImGui::GetWindowDockID();
        Editor_.Draw();
        ImGui::End();

        if (ShowOutput_)
        {
            ImGui::Begin(KPanelOutput, &ShowOutput_);
            OutputDockId_ = ImGui::GetWindowDockID();
            Output_.Draw();
            ImGui::End();
        }

        if (ShowProblems_)
        {
            if (OutputDockId_ != 0)
                ImGui::SetNextWindowDockID(OutputDockId_, ImGuiCond_FirstUseEver);
            if (ImGui::Begin(KPanelProblems, &ShowProblems_))
                Problems_.Draw();
            ImGui::End();
        }

        if (Preview_.IsOpen())
        {
            bool Open = true;
            if (EditorDockId_ != 0)
            {
                ImGui::SetNextWindowDockID(EditorDockId_, ImGuiCond_FirstUseEver);
            }
            ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin(KPanelPreview, &Open);
            Preview_.Draw();
            ImGui::End();
            if (!Open)
            {
                Preview_.Close();
            }
        }
    }

    void App::BuildDefaultLayout(ImGuiID DockspaceId)
    {
        ImGui::DockBuilderRemoveNode(DockspaceId);
        ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(DockspaceId, ImGui::GetMainViewport()->WorkSize);

        ImGuiID Center = DockspaceId;
        const ImGuiID Left = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, 0.20f, nullptr, &Center);
        const ImGuiID Bottom = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Down, 0.28f, nullptr, &Center);

        ImGui::DockBuilderDockWindow(KPanelCode, Left);
        ImGui::DockBuilderDockWindow(KPanelMedia, Left);
        ImGui::DockBuilderDockWindow(KPanelProject, Left);
        ImGui::DockBuilderDockWindow(KPanelEditor, Center);
        ImGui::DockBuilderDockWindow(KPanelPreview, Center);
        ImGui::DockBuilderDockWindow(KPanelProblems, Bottom);
        ImGui::DockBuilderDockWindow(KPanelOutput, Bottom);

        ImGui::DockBuilderFinish(DockspaceId);
    }

    void App::DrawMenuBar()
    {
        if (!ImGui::BeginMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N"))
            {
                Editor_.NewFile();
            }

            if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N", false, !ExecutionBusy_))
            {
                RequestNewProject();
            }

            // SDL_dialog has no UIKit backend, and mobile has no freeform file
            // system to browse anyway: there the Project panel is the only way in.
            if constexpr (core::platform::HasNativeFileDialog())
            {
                if (ImGui::MenuItem("Open File...", "Ctrl+O"))
                {
                    ShowOpenFileDialog();
                }
                if (ImGui::MenuItem("Open Folder...", "Ctrl+Shift+O"))
                {
                    const std::string Start =
                        Project_.Root().empty() ? std::string() : dbc::PathToUtf8(Project_.Root());
                    Dialog_.OpenFolder(Window_, Start,
                                       [this](const std::string& Path)
                                       {
                                           if (!Path.empty())
                                               OpenFolder(dbc::PathFromUtf8(Path));
                                       });
                }
            }

            if constexpr (!core::platform::HasFreeformFileSystem())
            {
                if (ImGui::BeginMenu("Open Location"))
                {
                    for (const core::Workspace::Location& LocationValue : Workspace_.Locations())
                    {
                        if (ImGui::MenuItem(LocationValue.Name.c_str()))
                        {
                            OpenFolder(LocationValue.Path);
                        }
                        if (!LocationValue.Hint.empty() && ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", LocationValue.Hint.c_str());
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, Editor_.HasDocument()))
            {
                SaveCurrent();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, Editor_.HasDocument()))
            {
                SaveCurrentAs();
            }
            if (ImGui::MenuItem("Save All", nullptr, false, Editor_.AnyDirty()))
            {
                Editor_.SaveAll();
                CheckCurrentDocument();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            {
                RequestQuit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            TextEditor* Editor = Editor_.CurrentEditor();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, Editor != nullptr && Editor->CanUndo()))
            {
                Editor->Undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, Editor != nullptr && Editor->CanRedo()))
            {
                Editor->Redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Find", "Ctrl+R", false, Editor != nullptr))
            {
                Editor_.OpenFind(false);
            }
            if (ImGui::MenuItem("Find and Replace", "Ctrl+Shift+R", false, Editor != nullptr))
            {
                Editor_.OpenFind(true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Trim Trailing Whitespace", nullptr, false, Editor != nullptr))
            {
                Editor_.FormatCurrent();
            }
            ImGui::Separator();
            TextEditor* Target = SnippetTarget();
            if (ImGui::MenuItem("Insert Snippet...", "Ctrl+J", false, Target != nullptr))
            {
                Snippets_.OpenInsert(Editor_.CurrentProfile());
            }
            if (ImGui::MenuItem("Save Selection as Snippet...", nullptr, false,
                                Target != nullptr && Target->AnyCursorHasSelection()))
            {
                Snippets_.OpenSave(Target->GetSectionText(Target->GetMainCursorSelection()), Editor_.CurrentProfile());
            }
            ImGui::EndMenu();
        }

        if (Mode_ == Mode::Scene)
            Scene_.DrawMenus();

        if (ImGui::BeginMenu("Build"))
        {
            if (ImGui::MenuItem("Run", "F5", false, !ExecutionBusy_))
            {
                if (Mode_ == Mode::Scene)
                    RunScene();
                else
                    RunActiveDocument();
            }
            if (ImGui::MenuItem("Stop", "Shift+F5", false, ExecutionBusy_))
            {
                StopRun();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Log Level"))
            {
                for (const char* Level : KLogLevels)
                {
                    if (ImGui::MenuItem(Level, nullptr, RunOptions_.LogLevel == Level))
                    {
                        RunOptions_.LogLevel = Level;
                        Settings_.LogLevel = Level;
                        Settings_.Save();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Debug Overlay on Launch", "F3", &RunOptions_.DebugOverlay))
            {
                Settings_.DebugOverlay = RunOptions_.DebugOverlay;
                Settings_.Save();
            }
#if defined(DBC_STUDIO_SANDBOXED)
            // A desktop program runs in its own process and window, where F4 writes the dumps.
            if (ImGui::MenuItem("Dump Scene and Renderer", "F4", false, Execution_.CanDumpDiagnostics()))
            {
                Execution_.DumpDiagnostics();
            }
#endif
#if !defined(DBC_STUDIO_SANDBOXED)
            if (ImGui::MenuItem("Compile HLSL Effect...", nullptr, false, !ExecutionBusy_))
            {
                const std::string Start = Project_.Root().empty() ? std::string() : dbc::PathToUtf8(Project_.Root());
                Dialog_.OpenFile(Window_, Start, "HLSL effect", KHlslFilter,
                                 [this](const std::string& Path)
                                 {
                                     if (Path.empty())
                                         return;
                                     const toolchain::ShaderCompileResult Result =
                                         ShaderCompiler_.CompileHlslEffect(dbc::PathFromUtf8(Path));
                                     if (!Result.Success)
                                     {
                                         Output_.Append("[ERROR] " + Result.Error,
                                                        panels::OutputPanel::Severity::Error);
                                         Status_ = "HLSL effect compilation failed";
                                         return;
                                     }
                                     Output_.Append("> Compiled " + dbc::PathToUtf8(Result.ManifestPath.filename()),
                                                    panels::OutputPanel::Severity::Notice);
                                     Output_.Append("  " + dbc::PathToUtf8(Result.VertexPath));
                                     Output_.Append("  " + dbc::PathToUtf8(Result.FragmentPath));
                                     Status_ = "Compiled " + dbc::PathToUtf8(Result.ManifestPath.filename());
                                 });
            }
#endif
            ImGui::Separator();
            const bool CanExport = core::platform::CanExportArtifacts() && !ExecutionBusy_ && !ExportJob_.Running() &&
                                   ResolvedProject_.has_value() && ResolvedProject_->ExportBlockers.empty() &&
                                   !ResolvedProject_->EntrySource.empty();
            if (ImGui::MenuItem("Build...", nullptr, false, CanExport))
                OpenBuildWindow();
            if (ExportJob_.Running() && ImGui::MenuItem("Cancel Export"))
                ExportJob_.Cancel();
            if (!LastExport_.empty() && ImGui::MenuItem("Reveal Last Export"))
                RevealInFileManager(LastExport_);
            ImGui::Separator();
            if (ImGui::MenuItem("Project Settings...", "Ctrl+,", false, !Project_.Root().empty()))
            {
                SettingsWindow_.Open(Project_.Root(), ProjectSettings_,
                                     ResolvedProject_ ? ResolvedProject_->DescriptorPath : std::nullopt);
            }
            if (ImGui::MenuItem("Generate Icons", nullptr, false,
                                !Project_.Root().empty() && !ProjectSettings_.IconSource.empty()))
            {
                IconGenerator_.Generate(Project_.Root(), ProjectSettings_.IconSource);
            }
            if (ImGui::MenuItem("Clear Local Path Remappings", nullptr, false,
                                !Settings_.ProjectPathRemappings.empty()))
            {
                Settings_.ClearProjectPathRemappings();
                Settings_.Save();
                Status_ = "Local DBPro path remappings cleared";
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Launcher..."))
            {
                Launcher_.Open();
            }
            ImGui::Separator();
            const std::string Blocker = SceneBlocker();
            // The mode items share labels with panel toggles below, so they need distinct IDs.
            if (ImGui::MenuItem("Code##Mode", "Ctrl+1", Mode_ == Mode::Code))
                SetMode(Mode::Code);
            if (ImGui::MenuItem("Scene##Mode", "Ctrl+2", Mode_ == Mode::Scene, Blocker.empty()))
                SetMode(Mode::Scene);
            if (!Blocker.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Blocker.c_str());
            ImGui::Separator();
            ImGui::MenuItem(KPanelProject, nullptr, &ShowProject_);
            ImGui::MenuItem(KPanelCode, nullptr, &ShowCode_);
            ImGui::MenuItem(KPanelMedia, nullptr, &ShowMedia_);
            ImGui::MenuItem(KPanelOutput, nullptr, &ShowOutput_);
            ImGui::MenuItem(KPanelProblems, nullptr, &ShowProblems_);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
            {
                if (Mode_ == Mode::Scene)
                    Scene_.BuildDefaultLayout(ImGui::GetID(KSceneDockSpaceName), KPanelSceneOutput);
                else
                    BuildDefaultLayout(ImGui::GetID(KDockSpaceName));
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("DBA Reference", nullptr, false, false);
            ImGui::MenuItem("About", nullptr, &ShowAbout_);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

#ifndef OPENDBC_VERSION_STRING
#define OPENDBC_VERSION_STRING "unknown"
#endif

    // The one screen that has to carry the trademark notice: it is what a user of a
    // packaged build can reach without the repository.
    void App::DrawAbout()
    {
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
        if (ImGui::Begin("About OpenDBC", &ShowAbout_, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextUnformatted("OpenDBC " OPENDBC_VERSION_STRING);
            ImGui::Spacing();

            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted("OpenDBC is an independent open-source implementation compatible with the "
                                   "DarkBASIC Classic and DarkBASIC Professional programming languages. OpenDBC is "
                                   "not affiliated with, endorsed by, or sponsored by Dark Basic Software Limited.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Trademarks");
            ImGui::Spacing();
            ImGui::TextUnformatted("DarkBASIC and DarkBASIC Professional are names and trademarks belonging to their "
                                   "respective rights holders. They are referenced solely to describe OpenDBC's "
                                   "language compatibility. OpenDBC is an independent project and is not affiliated "
                                   "with or endorsed by Dark Basic Software Limited.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Licence");
            ImGui::Spacing();
            ImGui::TextUnformatted("OpenDBC is released under the MIT Licence, Copyright (c) 2026 Yannick Comte. "
                                   "Third-party content and its licences are listed in NOTICE.md.");
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            if (ImGui::Button("Close"))
            {
                ShowAbout_ = false;
            }
        }
        ImGui::End();
    }

    void App::DrawToolbar()
    {
        const ImGuiStyle& Style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Theme::Panel));
        ImGui::BeginChild("##Toolbar", ImVec2(0.0f, BarHeight()), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPos(ImVec2(Style.ItemSpacing.x, Style.FramePadding.y));

        if (ToolbarButton("##new", ToolIcon::New, true, "New (Ctrl+N)"))
            Editor_.NewFile();
        if constexpr (core::platform::HasNativeFileDialog())
        {
            ImGui::SameLine();
            if (ToolbarButton("##open", ToolIcon::Open, true, "Open File..."))
                ShowOpenFileDialog();
        }
        ImGui::SameLine();
        if (ToolbarButton("##save", ToolIcon::Save, Editor_.HasDocument(), "Save (Ctrl+S)"))
            SaveCurrent();
        ToolbarSeparator();
        if (ToolbarButton("##run", ToolIcon::Run, !ExecutionBusy_, "Run (F5)"))
            RunActiveDocument();
        ImGui::SameLine();
        if (ToolbarButton("##stop", ToolIcon::Stop, ExecutionBusy_, "Stop (Shift+F5)"))
            StopRun();

        // The mode switch sits in the middle of the toolbar.
        const std::string Blocker = SceneBlocker();
        const float SwitchWidth = ImGui::CalcTextSize("Code").x + ImGui::CalcTextSize("Scene").x +
                                  Style.FramePadding.x * 4.0f + Style.ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - SwitchWidth) * 0.5f));
        if (ModeButton("Code", Mode_ == Mode::Code, true, "Code (Ctrl+1)"))
            SetMode(Mode::Code);
        ImGui::SameLine();
        if (ModeButton("Scene", Mode_ == Mode::Scene, Blocker.empty(),
                       Blocker.empty() ? "Scene (Ctrl+2)" : Blocker.c_str()))
            SetMode(Mode::Scene);

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void App::DrawStatusBar()
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Theme::Panel));
        ImGui::BeginChild("##StatusBar", ImVec2(0.0f, BarHeight()), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().FramePadding.y));
        // Centres the text on the dialect combo that shares its row.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Status_.c_str());

        if (ResolvedProject_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Dialect (%s):",
                                dbc::interpreter::DialectOriginName(ResolvedProject_->ProfileOrigin).data());
            ImGui::SameLine();
            ImGui::BeginDisabled(ExecutionBusy_ || DialectLocked_);
            const char* Selected = ResolvedProject_->Profile == dbc::language::LanguageProfile::Dbc ? "DBC" : "DBPro";
            ImGui::SetNextItemWidth(92.0f);
            if (ImGui::BeginCombo("##project-dialect", Selected))
            {
                if (ImGui::Selectable("DBC", ResolvedProject_->Profile == dbc::language::LanguageProfile::Dbc))
                    SetProjectDialect(dbc::language::LanguageProfile::Dbc);
                if (ImGui::Selectable("DBPro", ResolvedProject_->Profile == dbc::language::LanguageProfile::DbPro))
                    SetProjectDialect(dbc::language::LanguageProfile::DbPro);
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }

        const ImGuiIO& Io = ImGui::GetIO();
        char Fps[32];
        SDL_snprintf(Fps, sizeof(Fps), "%.0f FPS", Io.Framerate);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(Fps).x -
                        ImGui::GetStyle().ItemSpacing.x);
        ImGui::TextDisabled("%s", Fps);

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void App::RenderFrame()
    {
        const auto Fail = [&](const char* Operation)
        {
            if (!RenderFailed_)
                DBC_LOG_ERROR("Studio {} failed: {}", Operation, SDL_GetError());
            RenderFailed_ = true;
        };
        ImDrawData* DrawData = ImGui::GetDrawData();
        if (!DrawData || DrawData->DisplaySize.x <= 0.0f || DrawData->DisplaySize.y <= 0.0f)
            return;
        SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Gpu_);
        if (!Commands)
        {
            Fail("command acquisition");
            return;
        }
        SDL_GPUTexture* Swapchain = nullptr;
        const bool Acquired =
            Execution_.IsVrEnabled()
                ? SDL_AcquireGPUSwapchainTexture(Commands, Window_, &Swapchain, nullptr, nullptr)
                : SDL_WaitAndAcquireGPUSwapchainTexture(Commands, Window_, &Swapchain, nullptr, nullptr);
        if (!Acquired)
        {
            Fail("swapchain acquisition");
            SDL_CancelGPUCommandBuffer(Commands);
            return;
        }
        if (!Swapchain)
        {
            SDL_CancelGPUCommandBuffer(Commands);
            return;
        }
        if (Mode_ == Mode::Scene)
            Scene_.RecordGpu(Commands);
        ImGui_ImplSDLGPU3_PrepareDrawData(DrawData, Commands);
        const ImVec4 Clear = ImGui::ColorConvertU32ToFloat4(Theme::Background);
        SDL_GPUColorTargetInfo Target{};
        Target.texture = Swapchain;
        Target.clear_color = SDL_FColor{Clear.x, Clear.y, Clear.z, Clear.w};
        Target.load_op = SDL_GPU_LOADOP_CLEAR;
        Target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* Pass = SDL_BeginGPURenderPass(Commands, &Target, 1, nullptr);
        if (!Pass)
        {
            Fail("render pass");
            // Once the window image is acquired SDL requires submission, including on failure.
            if (!SDL_SubmitGPUCommandBuffer(Commands))
                Fail("submission");
            return;
        }
        ImGui_ImplSDLGPU3_RenderDrawData(DrawData, Commands, Pass);
        SDL_EndGPURenderPass(Pass);
        if (!SDL_SubmitGPUCommandBuffer(Commands))
        {
            Fail("submission");
            return;
        }
        RenderFailed_ = false;
    }

    void App::RunLauncherPath(const std::filesystem::path& Path)
    {
        if (ExecutionBusy_)
            return;
        OpenFile(Path);
        std::error_code Error;
        if (Editor_.CurrentPath().empty() || !std::filesystem::equivalent(Editor_.CurrentPath(), Path, Error) || Error)
        {
            Output_.Append("The Launcher could not activate the selected .dba document.",
                           panels::OutputPanel::Severity::Error);
            return;
        }
        RunActiveDocument();
    }

    std::optional<toolchain::ActiveRunPlan> App::PlanCurrentRun(bool Report)
    {
        std::vector<toolchain::MemorySource> Sources;
        const dbc::language::LanguageProfile Profile =
            ResolvedProject_ ? ResolvedProject_->Profile : dbc::language::LanguageProfile::Dbc;
        for (panels::BufferedSource& Source : Editor_.DirtySources(Profile))
            Sources.push_back({std::move(Source.LogicalPath), std::move(Source.Text)});

        toolchain::ActiveRunPlan Plan =
            toolchain::PlanActiveRun(Editor_.CurrentPath(), ResolvedProject_, std::move(Sources));
        if (!Plan)
        {
            if (Report)
                Output_.Append(Plan.Error, panels::OutputPanel::Severity::Error);
            return std::nullopt;
        }
        if (!Plan.Blockers.empty())
        {
            for (const std::string& Blocker : Plan.Blockers)
            {
                if (Report)
                    Output_.Append(Blocker, panels::OutputPanel::Severity::Error);
            }
            return std::nullopt;
        }
        return Plan;
    }

    void App::CheckCurrentDocument()
    {
        if (!Editor_.HasDocument() || !Editor_.CurrentIsBasic())
            return;
        // Saving a file the run plan cannot use is not an error worth reporting.
        std::optional<toolchain::ActiveRunPlan> Plan = PlanCurrentRun(false);
        if (Plan)
            ShowProblems(Execution_.Check(std::move(*Plan->Document)));
    }

    void App::ShowProblems(const std::vector<toolchain::RunDiagnostic>& Diagnostics)
    {
        Editor_.ClearErrorMarkers();
        std::map<std::filesystem::path, std::map<int, std::string>> Markers;
        for (const toolchain::RunDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Error && Diagnostic.Line > 0)
                Markers[Diagnostic.Path].emplace(static_cast<int>(Diagnostic.Line), Diagnostic.Message);
        }
        for (const auto& [Path, Errors] : Markers)
            Editor_.SetErrorMarkers(Path, Errors);
        Problems_.Set(Diagnostics);
    }

    void App::RunActiveDocument()
    {
        if (ExecutionBusy_)
            return;
        if (!Editor_.HasDocument())
        {
            Output_.Append("Nothing to run: open a .dba document.", panels::OutputPanel::Severity::Error);
            return;
        }

        std::optional<toolchain::ActiveRunPlan> Plan = PlanCurrentRun(true);
        if (!Plan)
            return;

        toolchain::RunDocument Document = std::move(*Plan->Document);
        const std::filesystem::path Target = Document.LogicalPath;
        ShowProblems({});

        toolchain::RunOptions RunOptions = RunOptions_;
        RunOptions.AssetRoot = dbc::PathToUtf8(Plan->AssetRoot);
        Output_.Append("> " + Execution_.Describe() + "  " + dbc::PathToUtf8(Target) + "  --asset-root " +
                           RunOptions.AssetRoot,
                       panels::OutputPanel::Severity::Notice);

        ExecutionTarget_ = Target;
        // A program that brings the Studio down must not take the unsaved tabs with it.
        UpdateRecovery();
        const bool Started = Execution_.Run(std::move(Document), RunOptions);

        if (Started)
        {
            ExecutionBusy_ = true;
            Status_ = "Running " + dbc::PathToUtf8(Target.filename());
        }
        else if (!ExecutionBusy_)
        {
            ExecutionTarget_.clear();
        }
    }

    void App::StopRun()
    {
        if (!ExecutionBusy_)
        {
            return;
        }
        Execution_.Stop();
        Status_ = "Stopping...";
        Output_.Append("Stopping...", panels::OutputPanel::Severity::Notice);
    }

    void App::OpenBuildWindow()
    {
        if (!ResolvedProject_)
            return;
        const std::string Root = dbc::PathToUtf8(ResolvedProject_->ProjectRoot);
        // The default sits inside the project, where exports skip their own output folder.
        const auto Saved = Settings_.ExportFolders.find(Root);
        const std::filesystem::path Default = ResolvedProject_->ProjectRoot / "exports";
        BuildWindow_.Open(Saved != Settings_.ExportFolders.end() ? Saved->second : dbc::PathToUtf8(Default));
        BuildWindow_.SetSigningIdentities(toolchain::ListSigningIdentities());
        RefreshExportChecks();
    }

    toolchain::ArtifactExportRequest App::MakeBuildRequest() const
    {
        return toolchain::MakeArtifactExportRequest(
            *ResolvedProject_, ProjectSettings_, BuildWindow_.Target(), dbc::PathFromUtf8(BuildWindow_.OutputFolder()),
            BuildWindow_.Distribution(),
            toolchain::ResolveExportToolchain(Settings_, toolchain::ToolchainSearch::Host()), ExportSecrets_);
    }

    void App::RefreshExportChecks()
    {
        ExportChecks_.clear();
        if (ResolvedProject_)
            ExportChecks_ = toolchain::CheckExportReadiness(MakeBuildRequest());
    }

    void App::DrawBuildWindow()
    {
        switch (BuildWindow_.Draw(Settings_, ProjectSettings_, ExportSecrets_, ExportChecks_, ExportJob_.Running(),
                                  Status_))
        {
            case panels::BuildWindow::Action::None:
                break;
            case panels::BuildWindow::Action::Refresh:
                Settings_.Save();
                RefreshExportChecks();
                break;
            case panels::BuildWindow::Action::BrowseOutput:
                Dialog_.OpenFolder(Window_, BuildWindow_.OutputFolder(),
                                   [this](const std::string& Folder)
                                   {
                                       if (!Folder.empty())
                                           BuildWindow_.SetOutputFolder(Folder);
                                   });
                break;
            case panels::BuildWindow::Action::ChooseKeystore:
            {
                const std::filesystem::path Suggested =
                    ResolvedProject_ ? ResolvedProject_->ProjectRoot.parent_path() / "release.keystore"
                                     : dbc::PathFromUtf8("release.keystore");
                Dialog_.SaveFile(Window_, dbc::PathToUtf8(Suggested), "Keystore", "keystore",
                                 [this](const std::string& Path)
                                 {
                                     if (!Path.empty())
                                         BuildWindow_.Keystore().Path = Path;
                                 });
                break;
            }
            case panels::BuildWindow::Action::CreateKeystore:
                CreateKeystore();
                break;
            case panels::BuildWindow::Action::Build:
                StartBuild();
                break;
            case panels::BuildWindow::Action::Cancel:
                ExportJob_.Cancel();
                Status_ = "Cancelling export...";
                break;
        }
    }

    void App::CreateKeystore()
    {
        panels::BuildWindow::KeystoreForm& Form = BuildWindow_.Keystore();
        toolchain::AndroidKeystoreRequest Request;
        Request.Path = dbc::PathFromUtf8(Form.Path);
        Request.Alias = Form.Alias;
        Request.Password = Form.Password;
        Request.CommonName = Form.CommonName;
        Request.Organization = Form.Organization;
        Request.Locality = Form.Locality;
        Request.Country = Form.Country;
        const toolchain::ExportToolchain Toolchain =
            toolchain::ResolveExportToolchain(Settings_, toolchain::ToolchainSearch::Host());
        std::string Error;
        if (!toolchain::CreateAndroidKeystore(Toolchain.JavaHome, Request, Error))
        {
            Status_ = "Keystore not created";
            Output_.Append("Cannot create the keystore: " + Error, panels::OutputPanel::Severity::Error);
            return;
        }
        ProjectSettings_.AndroidKeystorePath = dbc::PathToUtf8(Request.Path);
        ProjectSettings_.AndroidKeyAlias = Request.Alias;
        if (ResolvedProject_ && !ProjectSettings_.Save(ResolvedProject_->ProjectRoot))
            Output_.Append("Cannot save the keystore in the project settings.", panels::OutputPanel::Severity::Error);
        // A PKCS12 keystore protects the key with the keystore password.
        ExportSecrets_.AndroidKeystorePassword = Request.Password;
        ExportSecrets_.AndroidKeyPassword = Request.Password;
        Form.Password.clear();
        Form.Confirm.clear();
        Status_ = "Keystore created";
        Output_.Append("Created " + dbc::PathToUtf8(Request.Path) +
                           ". Back it up with its password: every Google Play "
                           "update must be signed with it.",
                       panels::OutputPanel::Severity::Notice);
        RefreshExportChecks();
    }

    void App::StartBuild()
    {
        if (!ResolvedProject_ || ExecutionBusy_ || !ResolvedProject_->ExportBlockers.empty())
            return;
        if (!Editor_.SaveAll())
        {
            Status_ = "Export failed";
            Output_.Append("Save every modified BASIC source before exporting.", panels::OutputPanel::Severity::Error);
            return;
        }
        RefreshExportChecks();
        if (!toolchain::ReadyToExport(ExportChecks_))
            return;
        const std::filesystem::path Folder = dbc::PathFromUtf8(BuildWindow_.OutputFolder());
        std::error_code FileError;
        std::filesystem::create_directories(Folder, FileError);
        if (FileError)
        {
            Status_ = "Export failed";
            Output_.Append("Cannot create the export folder: " + FileError.message(),
                           panels::OutputPanel::Severity::Error);
            return;
        }
        Settings_.ExportFolders[dbc::PathToUtf8(ResolvedProject_->ProjectRoot)] = dbc::PathToUtf8(Folder);
        Settings_.Save();
        if (!ExportJob_.Start(MakeBuildRequest()))
        {
            Output_.Append("An export is already running.", panels::OutputPanel::Severity::Error);
            return;
        }
        const bool Android = BuildWindow_.Target() == toolchain::ArtifactTarget::AndroidApk ||
                             BuildWindow_.Target() == toolchain::ArtifactTarget::AndroidBundle;
        IncrementVersionAfterExport_ = Android && BuildWindow_.Distribution() && BuildWindow_.IncrementVersionCode();
        Status_ = "Exporting...";
        Output_.Append("Exporting " + dbc::PathToUtf8(ResolvedProject_->EntrySource.filename()) + "...",
                       panels::OutputPanel::Severity::Notice);
    }

    void App::PollExport()
    {
        for (const std::string& Line : ExportJob_.TakeLines())
            Output_.Append(Line);
        std::optional<toolchain::ArtifactExportResult> Result = ExportJob_.TakeResult();
        if (!Result)
            return;
        for (const std::string& Message : Result->Messages)
            Output_.Append(Message, panels::OutputPanel::Severity::Notice);
        if (!*Result)
        {
            IncrementVersionAfterExport_ = false;
            Status_ = "Export failed";
            Output_.Append(Result->Error, panels::OutputPanel::Severity::Error);
            return;
        }
        LastExport_ = Result->OutputPath;
        Status_ = "Exported " + dbc::PathToUtf8(Result->OutputPath.filename());
        if (std::exchange(IncrementVersionAfterExport_, false) && ResolvedProject_)
        {
            ++ProjectSettings_.VersionCode;
            if (ProjectSettings_.Save(ResolvedProject_->ProjectRoot))
                Output_.Append("The next build uses version code " + std::to_string(ProjectSettings_.VersionCode),
                               panels::OutputPanel::Severity::Notice);
        }
    }

    void App::Shutdown()
    {
        ExportJob_.Shutdown();
        Execution_.Shutdown();
        if (SettingsLoaded_)
        {
            Settings_.Save();
            SettingsLoaded_ = false;
        }
        Preview_.Shutdown();
        if (Gpu_ != nullptr)
            SDL_WaitForGPUIdle(Gpu_);
        Scene_.Shutdown();

        DestroyPresentation();

        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::DestroyContext();
        }

        if (SdlInitialized_)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            SdlInitialized_ = false;
        }
    }

} // namespace dbc::studio
