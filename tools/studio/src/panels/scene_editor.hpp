#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <imgui.h>
// After imgui.h, which it needs.
#include <ImGuizmo.h>

#include "core/gpu_texture.hpp"
#include "core/media_catalogue.hpp"
#include "scene/scene_capture.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_geometry.hpp"
#include "scene/scene_patch.hpp"
#include "scene/scene_renderer.hpp"

namespace dbc::studio
{
    namespace panels
    {
        // What the Scene view needs from the open project. It only exists when the
        // project has a main .dba source to include the level from.
        struct SceneProject
        {
            std::filesystem::path Root;
            std::filesystem::path AssetRoot;
            std::filesystem::path EntrySource;
            dbc::language::LanguageProfile Profile = dbc::language::LanguageProfile::Dbc;
            // A .dbpro project, whose descriptor sets the display before SetupGame() runs.
            bool HasDescriptor = false;

            bool operator==(const SceneProject&) const = default;
        };

        struct SceneHost
        {
            // A source's current text: the open tab's buffer, else the file. nullopt when unreadable.
            std::function<std::optional<std::string>(const std::filesystem::path&)> ReadSource;
            // `Generated` files are written to disk and any tab follows them; the main
            // source goes to its open tab when there is one, so the user saves it.
            std::function<bool(const std::filesystem::path&, const std::string&, bool Generated)> WriteSource;
            std::function<void(const std::string&, bool Error)> Log;
            // Runs a program to read its scene; OpenCapture follows if it gets that far.
            std::function<void(const std::filesystem::path& Source, int SetupFrames)> CaptureProgram;
            std::function<void(const std::filesystem::path& Path, std::uint32_t Line)> ShowSource;
            // Opens a source in a tab without leaving the Scene view, so a patch of it can be undone.
            std::function<bool(const std::filesystem::path& Path)> OpenSource;
            std::function<bool()> CaptureRunning;
        };

        // The Scene mode: level files, selection, undo and the docked scene panels.
        class SceneEditor
        {
          public:
            static constexpr const char* KPanelHierarchy = "Hierarchy";
            static constexpr const char* KPanelViewport = "Viewport";
            static constexpr const char* KPanelInspector = "Inspector";
            static constexpr const char* KPanelSettings = "Level Settings";
            static constexpr const char* KPanelAssets = "Assets";
            // Drag and drop payload: an asset path relative to the asset root, NUL-terminated.
            static constexpr const char* KAssetPayload = "OPENDBC_ASSET";

            void Initialize(SDL_GPUDevice* Gpu, SceneHost Host);
            void Shutdown();

            // Saves pending changes before a different project replaces this one.
            void SetProject(std::optional<SceneProject> Project);

            const std::optional<SceneProject>& Project() const
            {
                return Project_;
            }

            // Called when the Scene mode is shown: opens a level if none is.
            void Activate();
            bool Dirty() const;
            // Ctrl+S: a program's scene is read again after its edits, to show what they did.
            bool SaveAndRefresh();
            void RefreshProgram();
            // Writes the level and game settings, then links them into the main source.
            bool Save();
            // Shows what a program builds instead of a level. `Source` is the file asked for, `OnlyFile`
            // what the capture is limited to when that file is run through the program including it.
            void OpenCapture(const std::filesystem::path& Source, const scene::SceneCapture& Capture, int SetupFrames,
                             const std::string& OnlyFile);
            // The program shown instead of a level; nullptr when a level is open.
            const std::filesystem::path* ProgramSource() const;

            void DrawMenus();
            void DrawPanels();
            void BuildDefaultLayout(ImGuiID DockspaceId, const char* OutputPanel);
            void HandleShortcuts();
            // Records the viewport's offscreen pass, before the ImGui pass samples it.
            void RecordGpu(SDL_GPUCommandBuffer* Commands);

          private:
            struct Selection
            {
                scene::IdSpace Space = scene::IdSpace::Object;
                int Id = 0;
                bool operator==(const Selection&) const = default;

                std::uint64_t Key() const
                {
                    return (static_cast<std::uint64_t>(Space) << 32) | static_cast<std::uint32_t>(Id);
                }
            };

            // In selection order, with a lookup: a painted group selects thousands of nodes.
            class SelectionList
            {
              public:
                void clear()
                {
                    Order_.clear();
                    Keys_.clear();
                }

                void push_back(const Selection& Entry)
                {
                    if (Keys_.insert(Entry.Key()).second)
                        Order_.push_back(Entry);
                }

                void erase(const Selection& Entry)
                {
                    if (Keys_.erase(Entry.Key()) != 0)
                        std::erase(Order_, Entry);
                }

                bool contains(const Selection& Entry) const
                {
                    return Keys_.count(Entry.Key()) != 0;
                }

                bool empty() const
                {
                    return Order_.empty();
                }

                std::size_t size() const
                {
                    return Order_.size();
                }

                const Selection& back() const
                {
                    return Order_.back();
                }

                std::vector<Selection>::const_iterator begin() const
                {
                    return Order_.begin();
                }

                std::vector<Selection>::const_iterator end() const
                {
                    return Order_.end();
                }

              private:
                std::vector<Selection> Order_;
                std::unordered_set<std::uint64_t> Keys_;
            };

            std::vector<std::string> LevelNames() const;
            std::filesystem::path LevelPath(const std::string& Name) const;
            std::filesystem::path GamePath() const;
            void OpenLevel(const std::string& Name);
            void NewLevel(const std::string& Name);
            void LoadGameSettings();
            bool LinkMain();

            // The active node: the last one selected, whose values the Inspector shows.
            scene::SceneNode* Selected();
            std::vector<scene::SceneNode*> SelectedNodes();
            bool IsSelected(const scene::SceneNode& Node) const;
            // Replaces the selection; nullptr clears it.
            void Select(const scene::SceneNode* Node);
            // Ctrl/Cmd-click: adds the node, or removes it when it is already selected.
            void ToggleSelect(const scene::SceneNode& Node);
            // Drops nodes that no longer exist, after undo or redo.
            void PruneSelection();
            void Commit();
            void Undo();
            void Redo();
            void Create(scene::NodeKind Kind, scene::LightKind Light = scene::LightKind::Point);
            // A model or 3D sound for an asset file, at `At` or ahead of the editor camera.
            void CreateFromAsset(scene::NodeKind Kind, const std::string& Relative, std::optional<glm::vec3> At);
            // The Create menu's items, shared by the menu bar and the Hierarchy's context menu.
            void DrawCreateItems();
            void DuplicateSelected();
            void DeleteSelected();
            bool Editable() const;
            // What a program's scene edits: only transforms, written back to its statements.
            bool TransformEditable() const;
            bool PartEditable(const scene::SceneNode& Node, scene::TransformPart Part) const;
            bool SaveProgramEdits();
            // Puts back the axes no single statement decides, whatever a drag or a typed value did to them.
            void EnforceCaptureLocks();

            // Viewport (scene_viewport.cpp)
            enum class Tool
            {
                View,
                Move,
                Rotate,
                Scale,
                Paint,
            };

            enum class PaintMode
            {
                One,     // a click places one object
                Several, // a click scatters a number of them over the radius
                Brush,   // a drag fills the radius up to a density
            };

            // A Unity-style editor camera: it looks along Yaw/Pitch from Position and
            // orbits a pivot Distance ahead.
            struct ViewCamera
            {
                glm::vec3 Position{0.0f, 80.0f, -180.0f};
                float Yaw = 0.0f;
                float Pitch = 20.0f;
                float Distance = 190.0f;
            };

            void DrawViewport();
            void DrawViewportBar();
            void UpdateCamera(bool Hovered);
            scene::ViewParameters ComputeView(float Aspect) const;
            std::vector<scene::LineVertex> BuildLines() const;
            // `Toggle` adds or removes the node under the mouse instead of selecting it alone.
            void PickAt(const ImVec2& Mouse, bool Toggle = false);
            void DrawGizmo();
            void DrawGroupGizmo(ImGuizmo::OPERATION Operation, ImGuizmo::MODE Mode);
            void DrawIcons(ImDrawList* Draw);
            void FrameSelection();
            // The Snap box, inverted while Ctrl or Cmd is held.
            bool SnapActive() const;
            void AlignSelectionToGrid();
            // While V is held: drags the selection by its vertex nearest the mouse onto the
            // nearest vertex of another node. Returns whether it replaces the gizmo and picking.
            bool UpdateVertexSnap(bool Hovered, ImDrawList* Draw);
            void AcceptAssetDrop();
            // Paint tool (scene_paint.cpp)
            void EnterPaintTool();
            void DrawPaintBar();
            void SetPaintObject(const scene::SceneNode& Node);
            void SetPaintModel(const std::string& Relative);
            // Returns whether the tool is on, replacing the gizmo and picking.
            bool UpdatePaint(bool Hovered, ImDrawList* Draw);
            std::optional<float> PaintSurfaceDistance(const scene::Ray& Ray) const;
            void PaintStamp(const glm::vec3& Center, bool OnMesh);
            void PaintErase(const glm::vec3& Center);
            bool ProjectToScreen(const glm::vec3& Point, ImVec2& Screen) const;
            scene::Ray RayAt(const ImVec2& Mouse) const;

            void DrawSceneMenu();
            void DrawHierarchy();
            // Inspector and settings (scene_inspector.cpp)
            void CommitIfIdle();
            void DrawInspector();
            void DrawShape(scene::SceneNode& Node);
            void DrawMaterial(scene::SceneNode& Node);
            void DrawAnimation(scene::SceneNode& Node);
            // The model file's animation, read once per file; nullptr when unreadable.
            const scene::ModelAnimation* ModelAnimationOf(const std::string& Asset);
            void DrawCameraSettings();
            void DrawSettings();

            // Assets (scene_assets.cpp)
            void DrawAssets();
            void RefreshAssets();
            // Rescans when the project's asset root changed since the last scan.
            const core::MediaScan& Assets();
            void ReleaseThumbnails();
            bool RenderModelThumbnail(const core::MediaFile& File, core::GpuImage& Out);
            void DrawNewLevelPopup();
            void DrawMatrixHeights(scene::SceneNode& Node);
            void DrawTerrainHeightmap(scene::SceneNode& Node);
            void DrawReadErrors();
            void DrawProgramBanner();
            void DrawNodeOrigin(const scene::SceneNode& Node);
            void DrawAxisLocks(const scene::SceneNode& Node, scene::TransformPart Part);
            const scene::NodeOrigin* OriginOf(const scene::SceneNode& Node) const;

            SDL_GPUDevice* Gpu_ = nullptr;
            SceneHost Host_;
            std::optional<SceneProject> Project_;

            bool HasLevel_ = false;
            scene::SceneDocument Document_;
            scene::SceneDocument Saved_;
            scene::SceneHistory History_;
            // Errors of a level the Scene view could not read: it stays read-only.
            std::vector<scene::ReadError> ReadErrors_;

            // Set when the document was read from a running program rather than a level file.
            struct ProgramView
            {
                std::filesystem::path Source;
                int SetupFrames = 1;
                std::string OnlyFile;
                std::map<scene::NodeKey, scene::NodeOrigin> Origins;
                std::vector<std::string> Notes;
            };

            std::optional<ProgramView> Program_;
            // What the last save promised, checked against the next capture.
            std::vector<scene::AxisEdit> PendingChecks_;

            scene::GameSettings Game_;
            scene::GameSettings SavedGame_;
            std::vector<scene::ReadError> GameErrors_;
            bool GameFileExists_ = false;

            // The back is the active node.
            SelectionList Selection_;
            // The level camera is a setting, not a node, but it is picked and moved like one.
            bool CameraSelected_ = false;

            scene::SceneRenderer Renderer_;
            bool RendererReady_ = false;
            ViewCamera Camera_;
            Tool Tool_ = Tool::Move;
            bool LocalGizmo_ = false;
            bool ShowGrid_ = true;
            // Snapping: world units, degrees and scale percent. The move step is a grid cell.
            bool Snap_ = true;
            float MoveSnap_ = 10.0f;
            float RotateSnap_ = 15.0f;
            float ScaleSnap_ = 10.0f;
            // Scale edits keep the selection's proportions, in the Inspector and the gizmo.
            bool UniformScale_ = false;
            bool LookThroughCamera_ = false;
            bool Navigating_ = false;
            bool GizmoActive_ = false;
            // Position and scale % when the gizmo drag began: snapping and uniform scale start from them.
            glm::vec3 GizmoStartPosition_{0.0f};
            glm::vec3 GizmoStartScale_{100.0f};
            // A multi-selection's gizmo, kept while it is dragged, and the nodes as the drag found them.
            glm::mat4 GroupGizmo_{1.0f};
            glm::mat4 GroupGizmoStart_{1.0f};
            std::vector<scene::SceneNode> GroupStart_;
            // A vertex drag: the vertex grabbed where it began, and how far it has jumped.
            bool VertexDragging_ = false;
            glm::vec3 VertexGrab_{0.0f};
            glm::vec3 VertexMove_{0.0f};

            // What the paint tool places: a copy, which undo or a delete cannot take away.
            std::optional<scene::SceneNode> PaintObject_;
            glm::vec3 PaintMin_{0.0f};
            glm::vec3 PaintMax_{0.0f};
            PaintMode PaintMode_ = PaintMode::One;
            int PaintCount_ = 5;
            float PaintRadius_ = 50.0f;
            float PaintDensity_ = 10.0f; // objects per 100 x 100 units
            bool PaintTurn_ = true;
            bool PaintRandomScale_ = false;
            glm::vec2 PaintScale_{75.0f, 125.0f}; // the range each object's scale is multiplied by, in percent
            bool Painting_ = false;
            std::optional<glm::vec3> PaintLast_;

            scene::ViewParameters View_;
            ImVec2 ViewportMin_{0.0f, 0.0f};
            ImVec2 ViewportSize_{0.0f, 0.0f};

            core::MediaScan AssetScan_;
            std::filesystem::path AssetScanRoot_;
            std::unordered_map<std::string, core::GpuImage> Thumbnails_;
            // Draws model thumbnails, apart from the viewport's renderer and its targets.
            scene::SceneRenderer ThumbnailRenderer_;
            std::optional<bool> ThumbnailRendererReady_;
            std::unordered_map<std::string, std::optional<scene::ModelAnimation>> ModelAnimations_;
            char AssetFilter_[64] = {};
            int AssetFilterKind_ = 0;

            bool NewLevelRequested_ = false;
            char NewLevelName_[64] = {};

            // Height tool settings, kept across selections like a brush's.
            float FillHeight_ = 0.0f;
            int FillTile_ = 1;
            float HeightRange_ = 10.0f;
            int HeightmapSize_ = 128;
            float TerrainFillHeight_ = 0.0f;
            float TerrainRange_ = 40.0f;
            int TerrainHills_ = 4;
            std::string TerrainError_;
            std::mt19937 Random_{std::random_device{}()};
        };
    } // namespace panels

} // namespace dbc::studio
