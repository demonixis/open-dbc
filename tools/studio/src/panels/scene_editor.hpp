#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <imgui.h>

#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"

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
            void Create(scene::NodeKind Kind);
            void DuplicateSelected();
            void DeleteSelected();
            bool Editable() const;

            void DrawHierarchy();
            void DrawViewport();
            void DrawInspector();
            void DrawSettings();
            void DrawAssets();
            void DrawNewLevelPopup();
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
            bool NewLevelRequested_ = false;
            char NewLevelName_[64] = {};
        };
    } // namespace panels

} // namespace dbc::studio
