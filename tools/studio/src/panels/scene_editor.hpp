#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <imgui.h>

#include "core/gpu_texture.hpp"
#include "core/media_catalogue.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"
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
            // Writes the level and game settings, then links them into the main source.
            bool Save();

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
            };

            std::vector<std::string> LevelNames() const;
            std::filesystem::path LevelPath(const std::string& Name) const;
            std::filesystem::path GamePath() const;
            void OpenLevel(const std::string& Name);
            void NewLevel(const std::string& Name);
            void LoadGameSettings();
            bool LinkMain();

            scene::SceneNode* Selected();
            void Select(const scene::SceneNode* Node);
            void Commit();
            void Undo();
            void Redo();
            void Create(scene::NodeKind Kind, scene::LightKind Light = scene::LightKind::Point);
            // A model or sound node for an asset file, at `At` or ahead of the editor camera.
            void CreateFromAsset(scene::NodeKind Kind, const std::string& Relative, std::optional<glm::vec3> At);
            // The Create menu's items, shared by the menu bar and the Hierarchy's context menu.
            void DrawCreateItems();
            void DuplicateSelected();
            void DeleteSelected();
            bool Editable() const;

            // Viewport (scene_viewport.cpp)
            enum class Tool
            {
                View,
                Move,
                Rotate,
                Scale,
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
            void PickAt(const ImVec2& Mouse);
            void DrawGizmo();
            void DrawIcons(ImDrawList* Draw);
            void FrameSelection();
            void AcceptAssetDrop();
            bool ProjectToScreen(const glm::vec3& Point, ImVec2& Screen) const;
            scene::Ray RayAt(const ImVec2& Mouse) const;

            void DrawSceneMenu();
            void DrawHierarchy();
            // Inspector and settings (scene_inspector.cpp)
            void CommitIfIdle();
            void DrawInspector();
            void DrawMaterial(scene::SceneNode& Node);
            void DrawCameraSettings();
            void DrawSettings();

            // Assets (scene_assets.cpp)
            void DrawAssets();
            void RefreshAssets();
            // Rescans when the project's asset root changed since the last scan.
            const core::MediaScan& Assets();
            void ReleaseThumbnails();
            void DrawNewLevelPopup();
            void DrawMatrixHeights(scene::SceneNode& Node);
            void DrawTerrainHeightmap(scene::SceneNode& Node);
            void DrawReadErrors();

            SDL_GPUDevice* Gpu_ = nullptr;
            SceneHost Host_;
            std::optional<SceneProject> Project_;

            bool HasLevel_ = false;
            scene::SceneDocument Document_;
            scene::SceneDocument Saved_;
            scene::SceneHistory History_;
            // Errors of a level the Scene view could not read: it stays read-only.
            std::vector<scene::ReadError> ReadErrors_;

            scene::GameSettings Game_;
            scene::GameSettings SavedGame_;
            std::vector<scene::ReadError> GameErrors_;
            bool GameFileExists_ = false;

            std::optional<Selection> Selection_;
            // The level camera is a setting, not a node, but it is picked and moved like one.
            bool CameraSelected_ = false;

            scene::SceneRenderer Renderer_;
            bool RendererReady_ = false;
            ViewCamera Camera_;
            Tool Tool_ = Tool::Move;
            bool LocalGizmo_ = false;
            bool LookThroughCamera_ = false;
            bool Navigating_ = false;
            bool GizmoActive_ = false;
            scene::ViewParameters View_;
            ImVec2 ViewportMin_{0.0f, 0.0f};
            ImVec2 ViewportSize_{0.0f, 0.0f};

            core::MediaScan AssetScan_;
            std::filesystem::path AssetScanRoot_;
            std::unordered_map<std::string, core::GpuImage> Thumbnails_;
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
