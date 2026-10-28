#include "panels/scene_editor.hpp"

#include <algorithm>
#include <array>
#include <imgui_internal.h>

#include "scene/scene_geometry.hpp"
#include "scene/scene_integration.hpp"

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            constexpr std::array<scene::NodeKind, 7> KPrimitives = {
                scene::NodeKind::Cube,     scene::NodeKind::Box,   scene::NodeKind::Sphere,  scene::NodeKind::Cylinder,
                scene::NodeKind::Cone,     scene::NodeKind::Plain, scene::NodeKind::Triangle};

            // Menus list at most this many asset files; the Assets panel shows the rest.
            constexpr std::size_t KMenuAssetLimit = 40;

            bool NeedsAsset(scene::NodeKind Kind)
            {
                return Kind == scene::NodeKind::Model || Kind == scene::NodeKind::Terrain ||
                       Kind == scene::NodeKind::Sound;
            }

            const char* LightKindLabel(scene::LightKind Kind)
            {
                switch (Kind)
                {
                    case scene::LightKind::Point:
                        return "Point Light";
                    case scene::LightKind::Spot:
                        return "Spot Light";
                    case scene::LightKind::Directional:
                        return "Directional Light";
                }
                return "Light";
            }
        } // namespace

        void SceneEditor::Initialize(SDL_GPUDevice* Gpu, SceneHost Host)
        {
            Gpu_ = Gpu;
            Host_ = std::move(Host);
        }

        void SceneEditor::Shutdown()
        {
            ReleaseThumbnails();
            Renderer_.Shutdown();
            RendererReady_ = false;
            Gpu_ = nullptr;
        }

        void SceneEditor::SetProject(std::optional<SceneProject> Project)
        {
            if (Project == Project_)
                return;
            if (Project_ && Dirty())
            {
                // Switching projects would otherwise drop the level silently.
                if (Save())
                    Host_.Log("Saved level " + Document_.Name + " before switching projects", false);
            }
            Project_ = std::move(Project);
            ReleaseThumbnails();
            AssetScan_ = {};
            AssetScanRoot_.clear();
            HasLevel_ = false;
            Document_ = {};
            Saved_ = {};
            ReadErrors_.clear();
            Game_ = {};
            SavedGame_ = {};
            GameErrors_.clear();
            GameFileExists_ = false;
            Selection_.clear();
        }

        void SceneEditor::Activate()
        {
            if (!Project_ || HasLevel_)
                return;
            LoadGameSettings();
            const std::vector<std::string> Names = LevelNames();
            if (Names.empty())
                NewLevel("Level1");
            else
                OpenLevel(Names.front());
        }

        bool SceneEditor::Dirty() const
        {
            return HasLevel_ && (Document_ != Saved_ || Game_ != SavedGame_);
        }

        std::vector<std::string> SceneEditor::LevelNames() const
        {
            std::vector<std::string> Names;
            if (!Project_)
                return Names;
            std::error_code Error;
            const std::filesystem::path Folder = Project_->Root / scene::KLevelFolder;
            for (const auto& Entry : std::filesystem::directory_iterator(Folder, Error))
            {
                const std::filesystem::path& Path = Entry.path();
                if (Entry.is_regular_file(Error) && dbc::language::IsLanguageSource(Path))
                    Names.push_back(Path.stem().string());
            }
            std::sort(Names.begin(), Names.end());
            return Names;
        }

        std::filesystem::path SceneEditor::LevelPath(const std::string& Name) const
        {
            return Project_->Root / scene::LevelRelativePath(Name);
        }

        std::filesystem::path SceneEditor::GamePath() const
        {
            return Project_->Root / scene::KGameFileName;
        }

        void SceneEditor::OpenLevel(const std::string& Name)
        {
            const std::optional<std::string> Text = Host_.ReadSource(LevelPath(Name));
            if (!Text)
            {
                Host_.Log("Cannot read level " + LevelPath(Name).string(), true);
                return;
            }
            scene::LevelReadResult Read = scene::ReadLevel(*Text, Name, Project_->Profile);
            for (const scene::ReadError& Error : Read.Errors)
                Host_.Log(LevelPath(Name).string() + ":" + std::to_string(Error.Line) + ": " + Error.Message, true);
            if (!Read.Ok())
                Host_.Log("The level stays read-only until these lines are fixed in the code editor", true);
            Document_ = std::move(Read.Document);
            ReadErrors_ = std::move(Read.Errors);
            Saved_ = Document_;
            History_.Reset(Document_);
            HasLevel_ = true;
            Selection_.clear();
        }

        void SceneEditor::NewLevel(const std::string& Name)
        {
            const std::string Clean = scene::SanitizeLevelName(Name);
            std::error_code Error;
            if (std::filesystem::exists(LevelPath(Clean), Error))
            {
                OpenLevel(Clean);
                return;
            }
            Document_ = {};
            Document_.Name = Clean;
            Document_.Profile = Project_->Profile;
            Saved_ = Document_;
            ReadErrors_.clear();
            History_.Reset(Document_);
            HasLevel_ = true;
            Selection_.clear();
        }

        void SceneEditor::LoadGameSettings()
        {
            Game_ = {};
            GameErrors_.clear();
            std::error_code Error;
            GameFileExists_ = std::filesystem::exists(GamePath(), Error);
            if (GameFileExists_)
            {
                const std::optional<std::string> Text = Host_.ReadSource(GamePath());
                if (!Text)
                {
                    GameErrors_.push_back({0, "Cannot read " + GamePath().string()});
                }
                else
                {
                    scene::GameReadResult Read = scene::ReadGame(*Text, Project_->Profile);
                    Game_ = Read.Settings;
                    GameErrors_ = std::move(Read.Errors);
                }
                for (const scene::ReadError& Read : GameErrors_)
                    Host_.Log(GamePath().string() + ":" + std::to_string(Read.Line) + ": " + Read.Message, true);
            }
            SavedGame_ = Game_;
        }

        bool SceneEditor::Save()
        {
            if (!Project_ || !HasLevel_)
                return false;
            if (!ReadErrors_.empty())
            {
                Host_.Log("The level is read-only: fix the lines reported when it was opened", true);
                return false;
            }
            for (const scene::SceneNode& Node : Document_.Nodes)
                if (NeedsAsset(Node.Kind) && Node.Asset.empty())
                {
                    Host_.Log(Node.Name + " needs a file before the level can be saved", true);
                    return false;
                }

            if (!Host_.WriteSource(LevelPath(Document_.Name), scene::WriteLevel(Document_), true))
            {
                Host_.Log("Cannot write " + LevelPath(Document_.Name).string(), true);
                return false;
            }
            Saved_ = Document_;

            if (GameErrors_.empty() && (GameFileExists_ || Game_ != scene::GameSettings{}))
            {
                if (!Host_.WriteSource(GamePath(), scene::WriteGame(Game_, Project_->Profile), true))
                {
                    Host_.Log("Cannot write " + GamePath().string(), true);
                    return false;
                }
                GameFileExists_ = true;
                SavedGame_ = Game_;
            }
            return LinkMain();
        }

        bool SceneEditor::LinkMain()
        {
            const std::filesystem::path& Main = Project_->EntrySource;
            const std::optional<std::string> Text = Host_.ReadSource(Main);
            if (!Text)
            {
                Host_.Log("Cannot read the main source " + Main.string(), true);
                return false;
            }
            const auto Relative = [&](const std::filesystem::path& Path)
            { return Path.lexically_relative(Main.parent_path()).generic_string(); };

            std::vector<scene::MainLink> Links;
            if (GameFileExists_)
                Links.push_back({Relative(GamePath()), scene::KGameFunctionName});
            Links.push_back({Relative(LevelPath(Document_.Name)), scene::LevelFunctionName(Document_)});
            const scene::LinkResult Linked = scene::LinkIntoMain(*Text, Project_->Profile, Links);
            if (!Linked.Changed)
                return true;
            if (!Host_.WriteSource(Main, Linked.Text, false))
            {
                Host_.Log("Cannot update the main source " + Main.string(), true);
                return false;
            }
            Host_.Log("Added the level's #include and call to " + Main.filename().string(), false);
            return true;
        }

        bool SceneEditor::Editable() const
        {
            return HasLevel_ && ReadErrors_.empty();
        }

        scene::SceneNode* SceneEditor::Selected()
        {
            return Selection_.empty() ? nullptr : Document_.Find(Selection_.back().Space, Selection_.back().Id);
        }

        std::vector<scene::SceneNode*> SceneEditor::SelectedNodes()
        {
            std::vector<scene::SceneNode*> Nodes;
            for (const Selection& Entry : Selection_)
                if (scene::SceneNode* Node = Document_.Find(Entry.Space, Entry.Id))
                    Nodes.push_back(Node);
            return Nodes;
        }

        bool SceneEditor::IsSelected(const scene::SceneNode& Node) const
        {
            return std::find(Selection_.begin(), Selection_.end(),
                             Selection{scene::IdSpaceOf(Node.Kind), Node.Id}) != Selection_.end();
        }

        void SceneEditor::Select(const scene::SceneNode* Node)
        {
            CameraSelected_ = false;
            Selection_.clear();
            if (Node != nullptr)
                Selection_.push_back(Selection{scene::IdSpaceOf(Node->Kind), Node->Id});
        }

        void SceneEditor::ToggleSelect(const scene::SceneNode& Node)
        {
            CameraSelected_ = false;
            const Selection Entry{scene::IdSpaceOf(Node.Kind), Node.Id};
            const auto Found = std::find(Selection_.begin(), Selection_.end(), Entry);
            if (Found != Selection_.end())
                Selection_.erase(Found);
            else
                Selection_.push_back(Entry);
        }

        void SceneEditor::PruneSelection()
        {
            std::erase_if(Selection_, [&](const Selection& Entry)
                          { return Document_.Find(Entry.Space, Entry.Id) == nullptr; });
        }

        void SceneEditor::Commit()
        {
            History_.Commit(Document_);
        }

        void SceneEditor::Undo()
        {
            if (!Editable() || !History_.CanUndo())
                return;
            Document_ = History_.Undo();
            PruneSelection();
        }

        void SceneEditor::Redo()
        {
            if (!Editable() || !History_.CanRedo())
                return;
            Document_ = History_.Redo();
            PruneSelection();
        }

        void SceneEditor::Create(scene::NodeKind Kind, scene::LightKind Light)
        {
            if (!Editable() || !scene::KindAvailable(Kind, Document_.Profile))
                return;
            scene::SceneNode& Node = Document_.Add(Kind);
            // A matrix grows from its corner: centre it on the origin.
            if (Kind == scene::NodeKind::Matrix)
                Node.Position = glm::vec3(-Node.Width * 0.5f, 0.0f, -Node.Depth * 0.5f);
            if (Kind == scene::NodeKind::Light)
            {
                Node.Light = Light;
                Node.Name = std::string(LightKindLabel(Light)) + " " + std::to_string(Node.Id);
                // A spot points down, a directional light slants like light 0.
                if (Light == scene::LightKind::Spot)
                    Node.Rotation = glm::vec3(90.0f, 0.0f, 0.0f);
                else if (Light == scene::LightKind::Directional)
                    Node.Rotation = glm::vec3(45.0f, 0.0f, 0.0f);
            }
            Select(&Node);
            Commit();
        }

        void SceneEditor::CreateFromAsset(scene::NodeKind Kind, const std::string& Relative,
                                          std::optional<glm::vec3> At)
        {
            if (!Editable() || !scene::KindAvailable(Kind, Document_.Profile))
                return;
            scene::SceneNode& Node = Document_.Add(Kind);
            Node.Asset = Relative;
            Node.Name = std::filesystem::path(Relative).stem().string();
            if (At)
            {
                Node.Position = *At;
            }
            else
            {
                const glm::vec3 Ahead = scene::LightDirection(glm::vec3(Camera_.Pitch, Camera_.Yaw, 0.0f));
                Node.Position = Camera_.Position + Ahead * Camera_.Distance;
            }
            Select(&Node);
            Commit();
        }

        void SceneEditor::DuplicateSelected()
        {
            if (!Editable() || Selection_.empty())
                return;
            // Copied first: adding nodes moves them. The copies become the selection.
            std::vector<scene::SceneNode> Copies;
            for (const scene::SceneNode* Source : SelectedNodes())
                Copies.push_back(*Source);
            Selection_.clear();
            for (scene::SceneNode& Copy : Copies)
            {
                Copy.Id = Document_.NextId(scene::IdSpaceOf(Copy.Kind));
                Copy.Name += " copy";
                Selection_.push_back(Selection{scene::IdSpaceOf(Copy.Kind), Copy.Id});
                Document_.Nodes.push_back(std::move(Copy));
            }
            Commit();
        }

        void SceneEditor::DeleteSelected()
        {
            if (!Editable() || Selection_.empty())
                return;
            std::erase_if(Document_.Nodes, [&](const scene::SceneNode& Node) { return IsSelected(Node); });
            Selection_.clear();
            Commit();
        }

        void SceneEditor::DrawMenus()
        {
            if (ImGui::BeginMenu("Scene"))
                DrawSceneMenu();
            // Like Unity's GameObject menu: everything a level can hold, by type.
            if (ImGui::BeginMenu("Create", Editable()))
            {
                DrawCreateItems();
                ImGui::EndMenu();
            }
        }

        void SceneEditor::DrawSceneMenu()
        {
            if (ImGui::MenuItem("New Level...", nullptr, false, Project_.has_value()))
                NewLevelRequested_ = true;
            const std::vector<std::string> Names = LevelNames();
            if (ImGui::BeginMenu("Open Level", !Names.empty()))
            {
                for (const std::string& Name : Names)
                    if (ImGui::MenuItem(Name.c_str(), nullptr, HasLevel_ && Name == Document_.Name))
                    {
                        if (Dirty())
                            Save();
                        OpenLevel(Name);
                    }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Save Level", "Ctrl+S", false, Editable()))
                Save();
            ImGui::Separator();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, Editable() && History_.CanUndo()))
                Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, Editable() && History_.CanRedo()))
                Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, Editable() && !Selection_.empty()))
                DuplicateSelected();
            if (ImGui::MenuItem("Delete", "Delete", false, Editable() && !Selection_.empty()))
                DeleteSelected();
            ImGui::EndMenu();
        }

        void SceneEditor::DrawCreateItems()
        {
            const auto AssetItems = [&](scene::NodeKind Kind, core::MediaKind Media, const char* Empty)
            {
                std::size_t Listed = 0;
                for (const core::MediaFile& File : Assets().Files)
                {
                    if (File.Kind != Media)
                        continue;
                    if (Listed++ == KMenuAssetLimit)
                    {
                        ImGui::MenuItem("More in the Assets panel", nullptr, false, false);
                        break;
                    }
                    if (ImGui::MenuItem(File.RelativeName.c_str()))
                        CreateFromAsset(Kind, File.RelativeName, std::nullopt);
                }
                if (Listed == 0)
                    ImGui::MenuItem(Empty, nullptr, false, false);
            };

            if (ImGui::BeginMenu("3D Object"))
            {
                for (const scene::NodeKind Kind : KPrimitives)
                    if (ImGui::MenuItem(scene::NodeKindLabel(Kind)))
                        Create(Kind);
                ImGui::Separator();
                if (ImGui::BeginMenu("Model"))
                {
                    AssetItems(scene::NodeKind::Model, core::MediaKind::Model, "No model under the asset folder");
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Landscape"))
            {
                if (ImGui::MenuItem("Matrix"))
                    Create(scene::NodeKind::Matrix);
                const bool Terrain = scene::KindAvailable(scene::NodeKind::Terrain, Document_.Profile);
                if (ImGui::MenuItem("Terrain", nullptr, false, Terrain))
                    Create(scene::NodeKind::Terrain);
                if (!Terrain && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("MAKE TERRAIN needs the Pro profile");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Light"))
            {
                for (const scene::LightKind Kind :
                     {scene::LightKind::Point, scene::LightKind::Spot, scene::LightKind::Directional})
                    if (ImGui::MenuItem(LightKindLabel(Kind)))
                        Create(scene::NodeKind::Light, Kind);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Audio"))
            {
                if (ImGui::BeginMenu(scene::NodeKindLabel(scene::NodeKind::Sound)))
                {
                    AssetItems(scene::NodeKind::Sound, core::MediaKind::Audio, "No sound under the asset folder");
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Camera"))
            {
                if (ImGui::MenuItem("Level Camera", nullptr, Document_.Env.UseCamera))
                {
                    Document_.Env.UseCamera = !Document_.Env.UseCamera;
                    Selection_.clear();
                    CameraSelected_ = Document_.Env.UseCamera;
                    Commit();
                }
                ImGui::EndMenu();
            }
        }

        void SceneEditor::HandleShortcuts()
        {
            const ImGuiIO& Io = ImGui::GetIO();
            if (Io.WantTextInput)
                return;
            const bool Command = Io.KeyCtrl || Io.KeySuper;
            if (Command && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                if (Io.KeyShift)
                    Redo();
                else
                    Undo();
            }
            else if (Command && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                Redo();
            else if (Command && ImGui::IsKeyPressed(ImGuiKey_D, false))
                DuplicateSelected();
            else if (Command && ImGui::IsKeyPressed(ImGuiKey_A, false) && HasLevel_)
            {
                CameraSelected_ = false;
                Selection_.clear();
                for (const scene::SceneNode& Node : Document_.Nodes)
                    Selection_.push_back(Selection{scene::IdSpaceOf(Node.Kind), Node.Id});
            }
            else if (!Command && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                                  ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
                DeleteSelected();
        }

        void SceneEditor::BuildDefaultLayout(ImGuiID DockspaceId, const char* OutputPanel)
        {
            ImGui::DockBuilderRemoveNode(DockspaceId);
            ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(DockspaceId, ImGui::GetMainViewport()->WorkSize);

            ImGuiID Center = DockspaceId;
            const ImGuiID Right = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, 0.24f, nullptr, &Center);
            const ImGuiID Left = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, 0.20f, nullptr, &Center);
            const ImGuiID Bottom = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Down, 0.28f, nullptr, &Center);

            ImGui::DockBuilderDockWindow(KPanelHierarchy, Left);
            ImGui::DockBuilderDockWindow(KPanelViewport, Center);
            ImGui::DockBuilderDockWindow(KPanelSettings, Right);
            ImGui::DockBuilderDockWindow(KPanelInspector, Right);
            ImGui::DockBuilderDockWindow(OutputPanel, Bottom);
            ImGui::DockBuilderDockWindow(KPanelAssets, Bottom);
            ImGui::DockBuilderFinish(DockspaceId);
        }

        void SceneEditor::DrawPanels()
        {
            if (ImGui::Begin(KPanelHierarchy))
                DrawHierarchy();
            ImGui::End();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            const bool ViewportOpen = ImGui::Begin(KPanelViewport, nullptr,
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            if (ViewportOpen)
                DrawViewport();
            ImGui::End();

            if (ImGui::Begin(KPanelInspector))
                DrawInspector();
            ImGui::End();

            if (ImGui::Begin(KPanelSettings))
                DrawSettings();
            ImGui::End();

            if (ImGui::Begin(KPanelAssets))
                DrawAssets();
            ImGui::End();

            DrawNewLevelPopup();
            CommitIfIdle();
        }

        void SceneEditor::DrawReadErrors()
        {
            if (ReadErrors_.empty())
                return;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
            ImGui::TextWrapped("Read-only: %s has lines the Scene view did not write.",
                               scene::LevelRelativePath(Document_.Name).generic_string().c_str());
            for (const scene::ReadError& Error : ReadErrors_)
                ImGui::BulletText("Line %u: %s", Error.Line, Error.Message.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        void SceneEditor::DrawHierarchy()
        {
            if (!HasLevel_)
                return;
            ImGui::TextDisabled("%s%s", Document_.Name.c_str(), Dirty() ? " *" : "");
            DrawReadErrors();
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                ImGui::PushID(static_cast<int>(scene::IdSpaceOf(Node.Kind)));
                ImGui::PushID(Node.Id);
                if (ImGui::Selectable(Node.Name.c_str(), IsSelected(Node)))
                {
                    const ImGuiIO& Io = ImGui::GetIO();
                    const scene::SceneNode* Anchor = Selected();
                    if (Io.KeyCtrl || Io.KeySuper)
                    {
                        ToggleSelect(Node);
                    }
                    else if (Io.KeyShift && Anchor != nullptr)
                    {
                        // The rows from the active node to this one, which becomes active.
                        const std::size_t From = static_cast<std::size_t>(Anchor - Document_.Nodes.data());
                        const std::size_t To = static_cast<std::size_t>(&Node - Document_.Nodes.data());
                        Select(Anchor);
                        for (std::size_t Index = From; Index != To;)
                        {
                            Index = To > From ? Index + 1 : Index - 1;
                            const scene::SceneNode& Row = Document_.Nodes[Index];
                            Selection_.push_back(Selection{scene::IdSpaceOf(Row.Kind), Row.Id});
                        }
                    }
                    else
                    {
                        Select(&Node);
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s %d", scene::NodeKindLabel(Node.Kind), Node.Id);
                ImGui::PopID();
                ImGui::PopID();
            }
            if (Document_.Env.UseCamera)
            {
                ImGui::Separator();
                if (ImGui::Selectable("Level Camera", CameraSelected_))
                {
                    Selection_.clear();
                    CameraSelected_ = true;
                }
            }
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
                Select(nullptr);
            if (Editable() && ImGui::BeginPopupContextWindow("##hierarchy-context",
                                                             ImGuiPopupFlags_MouseButtonRight |
                                                                 ImGuiPopupFlags_NoOpenOverItems))
            {
                DrawCreateItems();
                ImGui::EndPopup();
            }
        }

        void SceneEditor::DrawNewLevelPopup()
        {
            constexpr const char* Popup = "New Level";
            if (NewLevelRequested_)
            {
                ImGui::OpenPopup(Popup);
                NewLevelName_[0] = '\0';
                NewLevelRequested_ = false;
            }
            if (!ImGui::BeginPopupModal(Popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const bool Entered = ImGui::InputText("Name", NewLevelName_, sizeof(NewLevelName_),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Saved as %s",
                                scene::LevelRelativePath(NewLevelName_).generic_string().c_str());
            if (Entered || ImGui::Button("Create"))
            {
                if (Dirty())
                    Save();
                NewLevel(NewLevelName_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void SceneEditor::RecordGpu(SDL_GPUCommandBuffer* Commands)
        {
            if (RendererReady_)
                Renderer_.Record(Commands);
        }
    } // namespace panels

} // namespace dbc::studio
