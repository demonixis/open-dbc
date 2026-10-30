#include "panels/scene_editor.hpp"
#include "path_utf8.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <imgui_internal.h>

#include "core/i18n.hpp"
#include "scene/scene_geometry.hpp"
#include "scene/scene_integration.hpp"

namespace dbc::studio
{
    namespace panels
    {
        namespace
        {
            constexpr std::array<scene::NodeKind, 7> KPrimitives = {
                scene::NodeKind::Cube, scene::NodeKind::Box,   scene::NodeKind::Sphere,  scene::NodeKind::Cylinder,
                scene::NodeKind::Cone, scene::NodeKind::Plain, scene::NodeKind::Triangle};

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
            Program_.reset();
            Document_ = {};
            Saved_ = {};
            ReadErrors_.clear();
            UnlinkedLevel_.clear();
            Selection_.clear();
        }

        void SceneEditor::Activate()
        {
            if (!Project_)
                return;
            MoveOlderLevels();
            if (HasLevel_)
                return;
            const std::vector<std::string> Names = LevelNames();
            if (Names.empty())
                NewLevel("Level1");
            else
                OpenLevel(Names.front());
        }

        bool SceneEditor::Dirty() const
        {
            return HasLevel_ && Document_ != Saved_;
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
                const std::string Name = dbc::PathToUtf8(Entry.path().filename());
                if (Entry.is_directory(Error) && std::filesystem::is_regular_file(LevelPath(Name), Error))
                    Names.push_back(Name);
            }
            std::sort(Names.begin(), Names.end());
            return Names;
        }

        std::filesystem::path SceneEditor::LevelPath(const std::string& Name) const
        {
            return Project_->Root / scene::LevelRelativePath(Name);
        }

        std::filesystem::path SceneEditor::WorldPath(const std::string& Name) const
        {
            return Project_->Root / scene::WorldRelativePath(Name);
        }

        void SceneEditor::OpenLevel(const std::string& Name)
        {
            const std::optional<std::string> Text = Host_.ReadSource(LevelPath(Name));
            if (!Text)
            {
                Host_.Log("Cannot read level " + dbc::PathToUtf8(LevelPath(Name)), true);
                return;
            }
            Program_.reset();
            scene::LevelReadResult Read = scene::ReadLevel(*Text, Name, Project_->Profile);
            ReadErrors_.clear();
            const auto Report = [&](const std::vector<scene::ReadError>& Errors, const std::filesystem::path& Path)
            {
                for (const scene::ReadError& Error : Errors)
                {
                    const std::string Where = ":" + std::to_string(Error.Line) + ": " + Error.Message;
                    Host_.Log(dbc::PathToUtf8(Path) + Where, true);
                    ReadErrors_.push_back(dbc::PathToUtf8(Path.filename()) + Where);
                }
            };
            Report(Read.Errors, LevelPath(Name));
            // A level without world settings runs with the engine's defaults, until a save writes them.
            std::error_code Missing;
            if (std::filesystem::exists(WorldPath(Name), Missing))
            {
                if (const std::optional<std::string> World = Host_.ReadSource(WorldPath(Name)))
                    Report(scene::ReadWorld(*World, Read.Document), WorldPath(Name));
                else
                    Report({{0, "Cannot read the file"}}, WorldPath(Name));
            }
            if (!ReadErrors_.empty())
                Host_.Log("The level stays read-only until these lines are fixed in the code editor", true);
            Document_ = std::move(Read.Document);
            Saved_ = Document_;
            History_.Reset(Document_);
            HasLevel_ = true;
            Selection_.clear();
        }

        bool SceneEditor::NewLevel(const std::string& Name)
        {
            const std::string Clean = scene::SanitizeLevelName(Name);
            std::error_code Error;
            if (std::filesystem::exists(LevelPath(Clean), Error))
            {
                OpenLevel(Clean);
                return false;
            }
            Program_.reset();
            Document_ = {};
            Document_.Name = Clean;
            Document_.Profile = Project_->Profile;
            Saved_ = Document_;
            ReadErrors_.clear();
            History_.Reset(Document_);
            HasLevel_ = true;
            Selection_.clear();
            return true;
        }

        void SceneEditor::OpenCapture(const std::filesystem::path& Source, const scene::SceneCapture& Capture,
                                      int SetupFrames, const std::string& OnlyFile)
        {
            if (!Project_)
                return;
            if (Dirty())
                Save();
            const bool SameProgram = Program_ && Program_->Source == Source;
            scene::CaptureReadResult Read =
                scene::ReadCapture(Capture, {dbc::PathToUtf8(Source.filename()), SetupFrames, OnlyFile});
            if (SameProgram)
                for (const std::string& Message : scene::UnappliedEdits(PendingChecks_, Read))
                    Host_.Log(Message, true);
            PendingChecks_.clear();
            Document_ = std::move(Read.Document);
            Saved_ = Document_;
            ReadErrors_.clear();
            History_.Reset(Document_);
            HasLevel_ = true;
            Program_ = ProgramView{Source, SetupFrames, OnlyFile, std::move(Read.Origins), std::move(Read.Notes)};
            PruneSelection();
            if (SameProgram)
                return;
            Selection_.clear();
            CameraSelected_ = false;
            // Start where the program looks from.
            Camera_.Position = Document_.Env.CameraPosition;
            Camera_.Pitch = Document_.Env.CameraRotation.x;
            Camera_.Yaw = Document_.Env.CameraRotation.y;
        }

        const std::filesystem::path* SceneEditor::ProgramSource() const
        {
            return Program_ ? &Program_->Source : nullptr;
        }

        const scene::NodeOrigin* SceneEditor::OriginOf(const scene::SceneNode& Node) const
        {
            if (!Program_)
                return nullptr;
            const auto Found = Program_->Origins.find({scene::IdSpaceOf(Node.Kind), Node.Id});
            return Found == Program_->Origins.end() ? nullptr : &Found->second;
        }

        bool SceneEditor::Save()
        {
            if (!Project_ || !HasLevel_)
                return false;
            if (Program_)
                return SaveProgramEdits();
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
                Host_.Log("Cannot write " + dbc::PathToUtf8(LevelPath(Document_.Name)), true);
                return false;
            }
            if (!Host_.WriteSource(WorldPath(Document_.Name), scene::WriteWorld(Document_), true))
            {
                Host_.Log("Cannot write " + dbc::PathToUtf8(WorldPath(Document_.Name)), true);
                return false;
            }
            Saved_ = Document_;
            return LinkMain();
        }

        bool SceneEditor::SaveProgramEdits()
        {
            if (Document_ == Saved_)
                return true;
            scene::PatchPlan Plan = scene::PlanPatches(Saved_, Document_, Program_->Origins);

            std::error_code Error;
            const std::filesystem::path Root = std::filesystem::weakly_canonical(Project_->Root, Error);

            struct Patched
            {
                std::filesystem::path Path;
                scene::PatchResult Result;
                std::size_t Count = 0;
            };

            // Every file is patched before any is written, so a refusal leaves the program whole.
            std::vector<Patched> Files;
            for (const auto& [File, Edits] : Plan.Files)
            {
                const std::filesystem::path Path = dbc::PathFromUtf8(File);
                const std::string Name = dbc::PathToUtf8(Path.filename());
                // The capture comes from another process: it never picks a file outside the project.
                const std::filesystem::path Relative =
                    std::filesystem::weakly_canonical(Path, Error).lexically_relative(Root);
                if (Relative.empty() || *Relative.begin() == "..")
                {
                    Host_.Log("Cannot change " + File + ": it is outside the project", true);
                    return false;
                }
                const std::optional<std::string> Text = Host_.ReadSource(Path);
                if (!Text)
                {
                    Host_.Log("Cannot read " + File, true);
                    return false;
                }
                scene::PatchResult Result = scene::PatchSource(*Text, File, Document_.Profile, Edits);
                if (!Result.Error.empty())
                {
                    Host_.Log(Name + ", " + Result.Error, true);
                    return false;
                }
                Files.push_back({Path, std::move(Result), Edits.size()});
            }
            for (const Patched& File : Files)
            {
                const std::string Name = dbc::PathToUtf8(File.Path.filename());
                if (Host_.OpenSource)
                    Host_.OpenSource(File.Path);
                if (!Host_.WriteSource(File.Path, File.Result.Text, false))
                {
                    Host_.Log("Cannot update " + Name, true);
                    return false;
                }
                Host_.Log("Applied " + std::to_string(File.Count) + " edit(s) to " + Name +
                              ": save the file to keep them",
                          false);
                for (const std::string& Replaced : File.Result.Replaced)
                    Host_.Log(Name + ":" + Replaced + " became a number", false);
            }
            Saved_ = Document_;
            PendingChecks_ = std::move(Plan.Axes);
            return true;
        }

        void SceneEditor::RefreshProgram()
        {
            if (!Program_)
                return;
            // Reading the program again is the way out when the source no longer takes the edits.
            if (!Save())
            {
                Host_.Log("Dropped the edits the source refused", false);
                Document_ = Saved_;
            }
            if (Host_.CaptureProgram)
                Host_.CaptureProgram(Program_->Source, Program_->SetupFrames);
        }

        bool SceneEditor::SaveAndRefresh()
        {
            if (!Program_)
                return Save();
            if (!Dirty())
                return true;
            if (!Save())
                return false;
            RefreshProgram();
            return true;
        }

        bool SceneEditor::LinkMain()
        {
            const std::filesystem::path& Main = Project_->EntrySource;
            const std::optional<std::string> Text = Host_.ReadSource(Main);
            if (!Text)
            {
                Host_.Log("Cannot read the main source " + dbc::PathToUtf8(Main), true);
                return false;
            }
            const auto Relative = [&](const std::filesystem::path& Path)
            { return dbc::GenericPathToUtf8(Path.lexically_relative(Main.parent_path())); };

            const scene::LevelImports Imports = scene::FindLevelImports(
                Main, Project_->Root / scene::KLevelFolder, Document_.Name, Project_->Profile, Host_.ReadSource);
            // Once the program has its level, which one it loads and when is its own choice: two
            // levels loaded in a row would also collide on their numbers.
            if (Imports.OtherLevel && !Imports.Level)
            {
                if (UnlinkedLevel_ != Document_.Name)
                    Host_.Log("Level " + Document_.Name + " stays out of " + dbc::PathToUtf8(Main.filename()) +
                                  ", which already imports a level: call " + scene::WorldFunctionName(Document_) +
                                  "() and " + scene::LevelFunctionName(Document_) + "() where the game loads it",
                              false);
                UnlinkedLevel_ = Document_.Name;
                return true;
            }
            const std::vector<scene::MainLink> Links = {
                {Relative(WorldPath(Document_.Name)), scene::WorldFunctionName(Document_), Imports.World},
                {Relative(LevelPath(Document_.Name)), scene::LevelFunctionName(Document_), Imports.Level}};
            const scene::LinkResult Linked = scene::LinkIntoMain(*Text, Project_->Profile, Links);
            if (!Linked.Changed)
                return true;
            if (!Host_.WriteSource(Main, Linked.Text, false))
            {
                Host_.Log("Cannot update the main source " + dbc::PathToUtf8(Main), true);
                return false;
            }
            Host_.Log("Added the level's #include and call to " + dbc::PathToUtf8(Main.filename()), false);
            return true;
        }

        void SceneEditor::MoveOlderLevels()
        {
            std::error_code Error;
            std::vector<std::filesystem::path> Older;
            for (const auto& Entry : std::filesystem::directory_iterator(Project_->Root / scene::KLevelFolder, Error))
                if (Entry.is_regular_file(Error) && dbc::language::IsLanguageSource(Entry.path()))
                    Older.push_back(Entry.path());
            if (Older.empty())
                return;

            const std::filesystem::path& Main = Project_->EntrySource;
            const auto Relative = [&](const std::filesystem::path& Path)
            { return dbc::GenericPathToUtf8(Path.lexically_relative(Main.parent_path())); };
            const std::optional<std::string> Before = Host_.ReadSource(Main);
            std::string Text = Before.value_or(std::string());
            for (const std::filesystem::path& Path : Older)
            {
                const std::filesystem::path Target = LevelPath(dbc::PathToUtf8(Path.stem()));
                std::error_code Move;
                std::filesystem::create_directories(Target.parent_path(), Move);
                if (!Move && !std::filesystem::exists(Target, Move))
                    std::filesystem::rename(Path, Target, Move);
                else if (!Move)
                    Move = std::make_error_code(std::errc::file_exists);
                if (Move)
                {
                    Host_.Log("Cannot move " + dbc::PathToUtf8(Path) + " to its folder: " + Move.message(), true);
                    continue;
                }
                Host_.Log("Moved " + dbc::PathToUtf8(Path.filename()) + " to " + Relative(Target), false);
                if (Before)
                    Text = scene::MoveInclude(Text, Project_->Profile, Relative(Path), Relative(Target));
            }
            if (Before && Text != *Before && !Host_.WriteSource(Main, Text, false))
                Host_.Log("Cannot update the main source " + dbc::PathToUtf8(Main), true);
            if (Host_.FilesMoved)
                Host_.FilesMoved();
            if (std::filesystem::exists(Project_->Root / "opendbc_game.dba", Error))
                Host_.Log("opendbc_game.dba stays as it is and its SetupGame() still runs, but the Scene view no "
                          "longer edits it: each level's world settings hold the display now",
                          false);
        }

        bool SceneEditor::Editable() const
        {
            return HasLevel_ && ReadErrors_.empty() && !Program_;
        }

        bool SceneEditor::TransformEditable() const
        {
            if (!Program_)
                return Editable();
            // The capture on its way would replace what is edited meanwhile.
            return HasLevel_ && !(Host_.CaptureRunning && Host_.CaptureRunning());
        }

        bool SceneEditor::PartEditable(const scene::SceneNode& Node, scene::TransformPart Part) const
        {
            if (!Program_)
                return true;
            const scene::NodeOrigin* Origin = OriginOf(Node);
            if (Origin == nullptr)
                return false;
            const auto& Axes = Origin->Axes[static_cast<std::size_t>(Part)];
            return std::any_of(Axes.begin(), Axes.end(),
                               [](const scene::AxisOrigin& Axis) { return Axis.Access != scene::AxisAccess::Locked; });
        }

        void SceneEditor::EnforceCaptureLocks()
        {
            // A program's scene keeps its nodes, so both documents line up.
            if (!Program_ || Document_.Nodes.size() != Saved_.Nodes.size())
                return;
            for (std::size_t Index = 0; Index < Document_.Nodes.size(); ++Index)
            {
                scene::SceneNode& Node = Document_.Nodes[Index];
                const scene::SceneNode& Saved = Saved_.Nodes[Index];
                if (Node.Position == Saved.Position && Node.Rotation == Saved.Rotation && Node.Scale == Saved.Scale)
                    continue;
                const scene::NodeOrigin* Origin = OriginOf(Node);
                for (std::size_t Part = 0; Part < scene::KTransformPartCount; ++Part)
                {
                    const auto Which = static_cast<scene::TransformPart>(Part);
                    glm::vec3& Value = scene::TransformValue(Node, Which);
                    const glm::vec3& Kept = scene::TransformValue(Saved, Which);
                    for (std::size_t Axis = 0; Axis < 3; ++Axis)
                        if (Origin == nullptr || Origin->Axes[Part][Axis].Access == scene::AxisAccess::Locked)
                            Value[static_cast<int>(Axis)] = Kept[static_cast<int>(Axis)];
                }
            }
        }

        scene::SceneNode* SceneEditor::Selected()
        {
            return Selection_.empty() ? nullptr : Document_.Find(Selection_.back().Space, Selection_.back().Id);
        }

        std::vector<scene::SceneNode*> SceneEditor::SelectedNodes()
        {
            std::vector<scene::SceneNode*> Nodes;
            Nodes.reserve(Selection_.size());
            if (Selection_.size() == 1)
            {
                if (scene::SceneNode* Node = Selected())
                    Nodes.push_back(Node);
                return Nodes;
            }
            std::unordered_map<std::uint64_t, scene::SceneNode*> ByKey;
            ByKey.reserve(Document_.Nodes.size());
            for (scene::SceneNode& Node : Document_.Nodes)
                ByKey[Selection{scene::IdSpaceOf(Node.Kind), Node.Id}.Key()] = &Node;
            for (const Selection& Entry : Selection_)
                if (const auto Found = ByKey.find(Entry.Key()); Found != ByKey.end())
                    Nodes.push_back(Found->second);
            return Nodes;
        }

        bool SceneEditor::IsSelected(const scene::SceneNode& Node) const
        {
            return Selection_.contains(Selection{scene::IdSpaceOf(Node.Kind), Node.Id});
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
            if (Selection_.contains(Entry))
                Selection_.erase(Entry);
            else
                Selection_.push_back(Entry);
        }

        void SceneEditor::PruneSelection()
        {
            const std::vector<scene::SceneNode*> Nodes = SelectedNodes();
            Selection_.clear();
            for (const scene::SceneNode* Node : Nodes)
                Selection_.push_back(Selection{scene::IdSpaceOf(Node->Kind), Node->Id});
        }

        void SceneEditor::Commit()
        {
            EnforceCaptureLocks();
            History_.Commit(Document_);
        }

        void SceneEditor::Undo()
        {
            if (!TransformEditable() || !History_.CanUndo())
                return;
            Document_ = History_.Undo();
            PruneSelection();
        }

        void SceneEditor::Redo()
        {
            if (!TransformEditable() || !History_.CanRedo())
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
            Node.Name = dbc::PathToUtf8(dbc::PathFromUtf8(Relative).stem());
            if (At)
            {
                Node.Position = *At;
            }
            else
            {
                const glm::vec3 Ahead = scene::LightDirection(glm::vec3(Camera_.Pitch, Camera_.Yaw, 0.0f));
                Node.Position = Camera_.Position + Ahead * Camera_.Distance;
            }
            if (SnapActive())
                Node.Position = scene::SnapToGrid(Node.Position, MoveSnap_);
            Select(&Node);
            Commit();
        }

        void SceneEditor::DuplicateSelected()
        {
            if (!Editable() || Selection_.empty())
                return;
            // Copied first: adding nodes moves them. The copies become the selection.
            std::vector<scene::SceneNode> Copies;
            std::array<std::size_t, scene::KIdSpaceCount> Counts{};
            for (const scene::SceneNode* Source : SelectedNodes())
            {
                Copies.push_back(*Source);
                ++Counts[static_cast<std::size_t>(scene::IdSpaceOf(Source->Kind))];
            }
            std::array<std::vector<int>, scene::KIdSpaceCount> Free;
            for (std::size_t Space = 0; Space < Free.size(); ++Space)
                Free[Space] = Document_.NextIds(static_cast<scene::IdSpace>(Space), Counts[Space]);
            Selection_.clear();
            for (std::size_t Index = Copies.size(); Index-- > 0;)
            {
                scene::SceneNode& Copy = Copies[Index];
                std::vector<int>& Ids = Free[static_cast<std::size_t>(scene::IdSpaceOf(Copy.Kind))];
                Copy.Id = Ids.back();
                Ids.pop_back();
            }
            for (scene::SceneNode& Copy : Copies)
            {
                Copy.Name = Copy.Painted ? scene::PaintedName(Copy.Name, Copy.Id) : Document_.CopyName(Copy.Name);
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
            if (Program_)
            {
                if (ImGui::MenuItem("Apply to Source", "Ctrl+S", false, Dirty() && TransformEditable()))
                    SaveAndRefresh();
                if (ImGui::MenuItem("Refresh Program", nullptr, false, Host_.CaptureProgram != nullptr))
                    RefreshProgram();
            }
            else if (ImGui::MenuItem("Save Level", "Ctrl+S", false, Editable()))
            {
                Save();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, TransformEditable() && History_.CanUndo()))
                Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, TransformEditable() && History_.CanRedo()))
                Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, Editable() && !Selection_.empty()))
                DuplicateSelected();
            if (ImGui::MenuItem("Delete", "Delete", false, Editable() && !Selection_.empty()))
                DeleteSelected();
            if (ImGui::MenuItem("Align to Grid", "G", false,
                                TransformEditable() && (!Selection_.empty() || (CameraSelected_ && !Program_))))
                AlignSelectionToGrid();
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
            else if (!Command &&
                     (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
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

            ImGui::DockBuilderDockWindow(TrId(KPanelHierarchy), Left);
            ImGui::DockBuilderDockWindow(TrId(KPanelViewport), Center);
            ImGui::DockBuilderDockWindow(TrId(KPanelSettings), Right);
            ImGui::DockBuilderDockWindow(TrId(KPanelInspector), Right);
            ImGui::DockBuilderDockWindow(TrId(OutputPanel), Bottom);
            ImGui::DockBuilderDockWindow(TrId(KPanelAssets), Bottom);
            ImGui::DockBuilderFinish(DockspaceId);
        }

        void SceneEditor::DrawPanels()
        {
            if (ImGui::Begin(TrId(KPanelHierarchy)))
                DrawHierarchy();
            ImGui::End();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            const bool ViewportOpen = ImGui::Begin(TrId(KPanelViewport), nullptr,
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            if (ViewportOpen)
                DrawViewport();
            ImGui::End();

            if (ImGui::Begin(TrId(KPanelInspector)))
                DrawInspector();
            ImGui::End();

            if (ImGui::Begin(TrId(KPanelSettings)))
                DrawSettings();
            ImGui::End();

            if (ImGui::Begin(TrId(KPanelAssets)))
                DrawAssets();
            ImGui::End();

            DrawNewLevelPopup();
            EnforceCaptureLocks();
            CommitIfIdle();
        }

        void SceneEditor::DrawReadErrors()
        {
            if (ReadErrors_.empty())
                return;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
            ImGui::TextWrapped("Read-only: %s has lines the Scene view did not write.", Document_.Name.c_str());
            for (const std::string& Error : ReadErrors_)
                ImGui::BulletText("%s", Error.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        void SceneEditor::DrawProgramBanner()
        {
            if (!Program_)
                return;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
            ImGui::InputInt("Setup frames", &Program_->SetupFrames, 0);
            Program_->SetupFrames = std::max(Program_->SetupFrames, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How many frames the program runs before the scene counts as built. "
                                  "What later frames set belongs to the game loop.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh"))
                RefreshProgram();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Runs the program again, without a window or sound, to read its scene");
            ImGui::TextDisabled("Move, turn and scale what its statements decide; Ctrl+S writes it to the source.");
            for (const std::string& Note : Program_->Notes)
                ImGui::TextWrapped("%s", Note.c_str());
            ImGui::Separator();
        }

        void SceneEditor::DrawHierarchy()
        {
            if (!HasLevel_)
                return;
            ImGui::TextDisabled("%s%s", Document_.Name.c_str(), Dirty() ? " *" : "");
            DrawReadErrors();
            DrawProgramBanner();
            const auto DrawRow = [&](const scene::SceneNode& Node)
            {
                ImGui::PushID(static_cast<int>(scene::IdSpaceOf(Node.Kind)));
                ImGui::PushID(Node.Id);
                if (Node.Hidden)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                const bool Clicked = ImGui::Selectable(Node.Name.c_str(), IsSelected(Node));
                if (Node.Hidden)
                    ImGui::PopStyleColor();
                const scene::NodeOrigin* Origin = OriginOf(Node);
                if (Origin != nullptr && !Origin->Creator.File.empty() && ImGui::IsItemHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && Host_.ShowSource)
                    Host_.ShowSource(dbc::PathFromUtf8(Origin->Creator.File), Origin->Creator.Line);
                if (Clicked)
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
                {
                    if (Origin != nullptr && !Origin->Creator.File.empty())
                        ImGui::SetTooltip("%s %d%s\n%s:%u", scene::NodeKindLabel(Node.Kind), Node.Id,
                                          Node.Hidden ? " (hidden)" : "",
                                          dbc::PathToUtf8(dbc::PathFromUtf8(Origin->Creator.File).filename()).c_str(),
                                          Origin->Creator.Line);
                    else
                        ImGui::SetTooltip("%s %d", scene::NodeKindLabel(Node.Kind), Node.Id);
                }
                ImGui::PopID();
                ImGui::PopID();
            };

            // Painted objects fold into one row per look, where the first of them sits.
            std::vector<std::vector<const scene::SceneNode*>> Groups;
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                if (!Node.Painted)
                    continue;
                const auto Found = std::find_if(Groups.begin(), Groups.end(), [&](const auto& Group)
                                                { return scene::SameLook(*Group.front(), Node); });
                if (Found != Groups.end())
                    Found->push_back(&Node);
                else
                    Groups.push_back({&Node});
            }
            for (const scene::SceneNode& Node : Document_.Nodes)
            {
                if (!Node.Painted)
                {
                    DrawRow(Node);
                    continue;
                }
                const auto Group = std::find_if(Groups.begin(), Groups.end(),
                                                [&](const auto& Other) { return Other.front() == &Node; });
                if (Group == Groups.end())
                    continue;
                const bool AllSelected = std::all_of(Group->begin(), Group->end(), [&](const scene::SceneNode* Member)
                                                     { return IsSelected(*Member); });
                ImGui::PushID("painted");
                ImGui::PushID(Node.Id);
                const bool Open = ImGui::TreeNodeEx("##group",
                                                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                        (AllSelected ? ImGuiTreeNodeFlags_Selected : 0),
                                                    "%s (%zu)", scene::PaintedStem(Node.Name).c_str(), Group->size());
                // A click selects the whole group, to move, edit or delete it together.
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                {
                    const ImGuiIO& Io = ImGui::GetIO();
                    if (!Io.KeyCtrl && !Io.KeySuper)
                        Select(nullptr);
                    CameraSelected_ = false;
                    for (const scene::SceneNode* Member : *Group)
                        Selection_.push_back(Selection{scene::IdSpaceOf(Member->Kind), Member->Id});
                }
                if (Open)
                {
                    ImGuiListClipper Clipper;
                    Clipper.Begin(static_cast<int>(Group->size()));
                    while (Clipper.Step())
                        for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
                            DrawRow(*(*Group)[static_cast<std::size_t>(Index)]);
                    ImGui::TreePop();
                }
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
            if (Editable() &&
                ImGui::BeginPopupContextWindow("##hierarchy-context",
                                               ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
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
            const bool Entered =
                ImGui::InputText("Name", NewLevelName_, sizeof(NewLevelName_), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Saved as %s", dbc::GenericPathToUtf8(scene::LevelRelativePath(NewLevelName_)).c_str());
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
