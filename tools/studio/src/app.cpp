#include "app.hpp"
#include "engine/systems/log.hpp"

#include <algorithm>
#include <array>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include "core/paths.hpp"
#include "theme.hpp"
#include "toolchain/active_run_planner.hpp"

namespace dbc::studio
{
    namespace
    {
        constexpr const char* KWindowTitle = "OpenDBC";
        constexpr const char* KDockSpaceName = "StudioDockSpace";

        // Panel window titles, shared between the menu, the dock builder and the
        // panel bodies so the default layout keeps matching.
        constexpr const char* KPanelProject = "Project";
        constexpr const char* KPanelEditor = "Editor";
        constexpr const char* KPanelOutput = "Output";
        constexpr const char* KPanelPreview = "Preview";
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

        constexpr const char* KDbaFilter = "dba;dbpro";
#if !defined(DBC_STUDIO_SANDBOXED)
        constexpr const char* KHlslFilter = "fx;hlsl";
#endif
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
        Reference_.Load(core::paths::FindReferenceDoc());
        Commands_.Build(Reference_);
        Editor_.Initialize(&Commands_);

        Launcher_.Initialize(&Commands_, &Workspace_, &Dialog_);
        Launcher_.SetHandlers([this](const std::filesystem::path& Path) { RunLauncherPath(Path); },
                              [this](const std::filesystem::path& Path) { OpenFile(Path); });

        Project_.SetOnActivate([this](const std::filesystem::path& Path) { OpenFile(Path); });

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
            [this](int ExitCode, const std::map<int, std::string>& Errors)
        {
            ExecutionBusy_ = false;
            Editor_.SetErrorMarkers(ExecutionTarget_, Errors);
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

        // Command line wins over the remembered folder.
        std::error_code Ec;
        if (!OpenPath.empty())
        {
            const std::filesystem::path Path(OpenPath);
            if (std::filesystem::is_directory(Path, Ec))
            {
                OpenFolder(Path);
            }
            else
            {
                OpenFile(Path);
            }
        }
        else if (!Settings_.ProjectFolder.empty() && std::filesystem::is_directory(Settings_.ProjectFolder, Ec))
        {
            OpenFolder(Settings_.ProjectFolder);
            if (!Settings_.LastFile.empty() && std::filesystem::is_regular_file(Settings_.LastFile, Ec))
            {
                OpenFile(Settings_.LastFile);
            }
        }

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

        Gpu_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL |
                                       SDL_GPU_SHADERFORMAT_METALLIB,
                                   false, nullptr);
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

            IniPath_ = (core::paths::PrefDir() / "studio-layout.ini").string();
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

            if (SDL_GetWindowFlags(Window_) & SDL_WINDOW_MINIMIZED)
            {
                if (!Execution_.IsVrPacing())
                    SDL_Delay(10);
                continue;
            }

            DrawFrame();
            RenderFrame();
        }
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
        if (Editor_.AnyDirty())
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
        ProjectSettings_ = std::move(LoadedSettings);
        const std::string PreviousFolder = Settings_.ProjectFolder;
        Settings_.ProjectFolder = Folder.string();
        if (!PreviousFolder.empty() && std::filesystem::path(PreviousFolder) != Folder)
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
            ProjectSettings_ = std::move(LoadedSettings);
            Settings_.ProjectFolder = File.parent_path().string();
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
                Output_.Append("[ERROR] Cannot preview " + File.string(), panels::OutputPanel::Severity::Error);
            }
            Status_ = File.filename().string();
            return;
        }

        if (Project_.Root().empty() && dbc::language::IsLanguageSource(File))
            OpenFolder(File.parent_path());

        if (!Editor_.Open(File))
        {
            Output_.Append("[ERROR] Cannot open " + File.string(), panels::OutputPanel::Severity::Error);
            Status_ = "Cannot open " + File.filename().string();
            return;
        }

        Settings_.LastFile = File.string();

        Settings_.Save();

        Status_ = File.filename().string();
    }

    void App::ShowOpenFileDialog()
    {
        const std::string Start = Project_.Root().empty() ? std::string() : Project_.Root().string();
        Dialog_.OpenFile(Window_, Start, "DBA source", KDbaFilter,
                         [this](const std::string& Path)
                         {
                             if (!Path.empty())
                                 OpenFile(Path);
                         });
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
        if (ResolvedProject_->DescriptorPath)
            Launcher_.SetProject(*ResolvedProject_);
        else
            Launcher_.SetFolder(ResolvedProject_->ProjectRoot, ResolvedProject_->Profile);
        Status_ = "Opened " + ResolvedProject_->ProjectRoot.string() + " as " +
                  std::string(dbc::language::ProfileName(ResolvedProject_->Profile));
        for (const std::string& Warning : ResolvedProject_->Warnings)
            Output_.Append(Warning, panels::OutputPanel::Severity::Notice);
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
                ResolvedProject_->DescriptorPath->lexically_relative(Project_.Root()).generic_string();
        if (ResolvedProject_)
            ProjectSettings_.LanguageEntry =
                ResolvedProject_->EntrySource.lexically_relative(Project_.Root()).generic_string();
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
            Status_ = "Saved " + Editor_.CurrentPath().filename().string();
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

        const std::string Start = Project_.Root().empty() ? std::string() : Project_.Root().string();
        Dialog_.SaveFile(Window_, Start, "DBA source", KDbaFilter,
                         [this](const std::string& Path)
                         {
                             if (Path.empty())
                             {
                                 QuitSaveInProgress_ = false;
                                 Status_ = "Quit cancelled";
                                 return;
                             }
                             if (Editor_.SaveCurrentAs(Path))
                             {
                                 Status_ = "Saved " + std::filesystem::path(Path).filename().string();
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
        if (!Editor_.AnyDirty())
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

        if (ShowDemo_)
        {
            ImGui::ShowDemoWindow(&ShowDemo_);
        }

        if (ShowAbout_)
        {
            DrawAbout();
        }

        DrawQuitConfirm();
        DrawProjectResolution();

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
                Settings_.SetProjectPathRemapping(Descriptor, PendingRawPath_, PendingLocalPath_);
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
                if (ImGui::Button(Candidate.filename().string().c_str()))
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
            {
                StopRun();
            }
            else
            {
                RunActiveDocument();
            }
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
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
        {
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
        if (!LayoutBuilt_)
        {
            LayoutBuilt_ = true;
            if (ImGui::DockBuilderGetNode(DockspaceId) == nullptr)
            {
                BuildDefaultLayout(DockspaceId);
            }
        }

        // Reserve a row at the bottom of the host window for the status bar.
        const float StatusHeight = ImGui::GetFrameHeight();
        ImGui::DockSpace(DockspaceId, ImVec2(0.0f, -StatusHeight), ImGuiDockNodeFlags_None);

        DrawStatusBar();

        ImGui::End();
    }

    void App::DrawPanels()
    {
        if (ShowProject_)
        {
            ImGui::Begin(KPanelProject, &ShowProject_);
            Project_.Draw();
            ImGui::End();
        }

        ImGui::Begin(KPanelEditor, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        EditorDockId_ = ImGui::GetWindowDockID();
        Editor_.Draw();
        ImGui::End();

        if (ShowOutput_)
        {
            ImGui::Begin(KPanelOutput, &ShowOutput_);
            Output_.Draw();
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

        Launcher_.Draw(Window_);

        if (SettingsWindow_.Draw(ProjectSettings_))
        {
            Status_ = "Project settings saved";
            const std::filesystem::path Target = ResolvedProject_ && ResolvedProject_->DescriptorPath
                                                     ? *ResolvedProject_->DescriptorPath
                                                     : Project_.Root();
            ResolveWorkspace(Target,
                             DialectLocked_ ? std::optional(dbc::language::LanguageProfile::DbPro) : std::nullopt);
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

        ImGui::DockBuilderDockWindow(KPanelProject, Left);
        ImGui::DockBuilderDockWindow(KPanelEditor, Center);
        ImGui::DockBuilderDockWindow(KPanelPreview, Center);
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
                    const std::string Start = Project_.Root().empty() ? std::string() : Project_.Root().string();
                    Dialog_.OpenFolder(Window_, Start,
                                       [this](const std::string& Path)
                                       {
                                           if (!Path.empty())
                                               OpenFolder(Path);
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
            if (ImGui::MenuItem("Find/Replace", "Ctrl+F", false, Editor != nullptr))
            {
                Editor_.OpenFindReplace();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Trim Trailing Whitespace", nullptr, false, Editor != nullptr))
            {
                Editor_.FormatCurrent();
            }
            ImGui::Separator();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build"))
        {
            if (ImGui::MenuItem("Run", "F5", false, !ExecutionBusy_))
            {
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
            if (ImGui::MenuItem("Dump Scene and Renderer", "F4", false, ExecutionBusy_))
            {
                Execution_.DumpDiagnostics();
            }
#if !defined(DBC_STUDIO_SANDBOXED)
            if (ImGui::MenuItem("Compile HLSL Effect...", nullptr, false, !ExecutionBusy_))
            {
                const std::string Start = Project_.Root().empty() ? std::string() : Project_.Root().string();
                Dialog_.OpenFile(
                    Window_, Start, "HLSL effect", KHlslFilter,
                    [this](const std::string& Path)
                    {
                        if (Path.empty())
                            return;
                        const toolchain::ShaderCompileResult Result = ShaderCompiler_.CompileHlslEffect(Path);
                        if (!Result.Success)
                        {
                            Output_.Append("[ERROR] " + Result.Error, panels::OutputPanel::Severity::Error);
                            Status_ = "HLSL effect compilation failed";
                            return;
                        }
                        Output_.Append("> Compiled " + Result.ManifestPath.filename().string(),
                                       panels::OutputPanel::Severity::Notice);
                        Output_.Append("  " + Result.VertexPath.string());
                        Output_.Append("  " + Result.FragmentPath.string());
                        Status_ = "Compiled " + Result.ManifestPath.filename().string();
                    });
            }
#endif
            ImGui::Separator();
            const bool CanExport = core::platform::CanExportArtifacts() && !ExecutionBusy_ &&
                                   ResolvedProject_.has_value() && ResolvedProject_->ExportBlockers.empty() &&
                                   !ResolvedProject_->EntrySource.empty();
            if (ImGui::BeginMenu("Export", CanExport))
            {
                if (ImGui::MenuItem("Desktop..."))
                    ExportArtifact(toolchain::ArtifactTarget::Desktop);
#if defined(__APPLE__)
                if (ImGui::MenuItem("iOS IPA..."))
                    ExportArtifact(toolchain::ArtifactTarget::Ios);
#endif
                if (ImGui::MenuItem("Android APK..."))
                    ExportArtifact(toolchain::ArtifactTarget::AndroidApk);
                if (ImGui::MenuItem("Android App Bundle..."))
                    ExportArtifact(toolchain::ArtifactTarget::AndroidBundle);
                ImGui::EndMenu();
            }
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
            ImGui::MenuItem(KPanelProject, nullptr, &ShowProject_);
            ImGui::MenuItem(KPanelOutput, nullptr, &ShowOutput_);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
            {
                BuildDefaultLayout(ImGui::GetID(KDockSpaceName));
            }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &ShowDemo_);
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
        ImGui::BeginChild("##Toolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + Style.FramePadding.y * 2.0f),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void App::DrawStatusBar()
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Theme::Panel));
        ImGui::BeginChild("##StatusBar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().FramePadding.y));
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
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(Fps).x);
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

    void App::RunActiveDocument()
    {
        if (ExecutionBusy_)
            return;
        if (!Editor_.HasDocument())
        {
            Output_.Append("Nothing to run: open a .dba document.", panels::OutputPanel::Severity::Error);
            return;
        }

        std::vector<toolchain::MemorySource> Sources;
        const dbc::language::LanguageProfile Profile =
            ResolvedProject_ ? ResolvedProject_->Profile : dbc::language::LanguageProfile::Dbc;
        for (panels::BufferedSource& Source : Editor_.DirtySources(Profile))
            Sources.push_back({std::move(Source.LogicalPath), std::move(Source.Text)});

        toolchain::ActiveRunPlan Plan =
            toolchain::PlanActiveRun(Editor_.CurrentPath(), ResolvedProject_, std::move(Sources));
        if (!Plan)
        {
            Output_.Append(Plan.Error, panels::OutputPanel::Severity::Error);
            return;
        }
        if (!Plan.Blockers.empty())
        {
            for (const std::string& Blocker : Plan.Blockers)
                Output_.Append(Blocker, panels::OutputPanel::Severity::Error);
            return;
        }

        toolchain::RunDocument Document = std::move(*Plan.Document);
        const std::filesystem::path Target = Document.LogicalPath;
        Editor_.SetErrorMarkers(Target, {});

        toolchain::RunOptions RunOptions = RunOptions_;
        RunOptions.AssetRoot = Plan.AssetRoot.string();
        Output_.Append("> " + Execution_.Describe() + "  " + Target.string() + "  --asset-root " + RunOptions.AssetRoot,
                       panels::OutputPanel::Severity::Notice);

        ExecutionTarget_ = Target;
        const bool Started = Execution_.Run(std::move(Document), RunOptions);

        if (Started)
        {
            ExecutionBusy_ = true;
            Status_ = "Running " + Target.filename().string();
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

    void App::ExportArtifact(toolchain::ArtifactTarget Target)
    {
        const std::filesystem::path Entry = ResolvedProject_ ? ResolvedProject_->EntrySource : std::filesystem::path{};
        if (Entry.empty() || Project_.Root().empty() || !ResolvedProject_)
            return;
        if (!Editor_.SaveAll())
        {
            Status_ = "Export failed";
            Output_.Append("Save every modified BASIC source before exporting.", panels::OutputPanel::Severity::Error);
            return;
        }
        Dialog_.OpenFolder(
            Window_, Project_.Root().string(),
            [this, Target, Entry](const std::string& Destination)
            {
                if (Destination.empty())
                    return;
                toolchain::ArtifactExportRequest Request;
                Request.Target = Target;
                Request.ProjectRoot = Project_.Root();
                Request.EntrySource = Entry;
                Request.Profile = ResolvedProject_->Profile;
                Request.OrderedSources = ResolvedProject_->DescriptorPath ? ResolvedProject_->OrderedSources
                                                                          : std::vector<std::filesystem::path>{Entry};
                Request.ProjectFile = ResolvedProject_->DescriptorPath;
                Request.AssetRoot = ResolvedProject_->AssetRoot;
                Request.OutputDirectory = Destination;
                Request.HostProduct = core::paths::ProductPath();
                Request.HostResources = core::paths::ResourceDir();
                Request.Settings = ProjectSettings_;

                Output_.Append("Exporting " + Entry.filename().string() + "...", panels::OutputPanel::Severity::Notice);
                const toolchain::ArtifactExportResult Result = ArtifactExporter_.Export(Request);
                for (const std::string& Message : Result.Messages)
                    Output_.Append(Message, panels::OutputPanel::Severity::Notice);
                if (!Result)
                {
                    Status_ = "Export failed";
                    Output_.Append(Result.Error, panels::OutputPanel::Severity::Error);
                    return;
                }
                Status_ = "Exported " + Result.OutputPath.filename().string();
            });
    }

    void App::Shutdown()
    {
        Execution_.Shutdown();
        if (SettingsLoaded_)
        {
            Settings_.Save();
            SettingsLoaded_ = false;
        }
        Preview_.Shutdown();

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
