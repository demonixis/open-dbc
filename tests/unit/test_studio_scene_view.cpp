// Drives the real Scene view with simulated input: ImGui, ImGuizmo and the renderer run, without a window.

#include <doctest/doctest.h>

#include "panels/scene_editor.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_geometry.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace dbc::studio;

namespace
{
    constexpr float KCubeSize = 20.0f;
    // Cmd on macOS, where ImGui turns the Ctrl key's clicks into right clicks.
    constexpr ImGuiKey KCommandKey = ImGuiMod_Super;

    scene::SceneDocument LevelOfCubes(const std::vector<glm::vec3>& Positions, float Size = KCubeSize)
    {
        scene::SceneDocument Level;
        Level.Name = "Level";
        for (const glm::vec3& Position : Positions)
        {
            scene::SceneNode& Node = Level.Add(scene::NodeKind::Cube);
            Node.Size = Size;
            Node.Position = Position;
        }
        return Level;
    }

    bool IsMultipleOf(float Value, float Step)
    {
        return std::abs(Value - std::round(Value / Step) * Step) < 0.001f;
    }

    class SceneViewDriver
    {
      public:
        SceneViewDriver(const std::string& Name, const scene::SceneDocument& Level)
            : Root_(std::filesystem::path(TEST_OUTPUT_DIR) / "scene_view" / Name)
        {
            REQUIRE(SDL_Init(SDL_INIT_VIDEO));
            Gpu_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
            REQUIRE(Gpu_ != nullptr);

            std::filesystem::remove_all(Root_);
            std::filesystem::create_directories(Root_ / scene::KLevelFolder);
            WriteText(Root_ / "main.dba", "SYNC\n");
            WriteText(Root_ / scene::LevelRelativePath(Level.Name), scene::WriteLevel(Level));

            ImGui::CreateContext();
            ImGuiIO& Io = ImGui::GetIO();
            Io.IniFilename = nullptr;
            Io.LogFilename = nullptr;
            Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            Io.DisplaySize = ImVec2(1600.0f, 900.0f);
            Io.DeltaTime = 1.0f / 60.0f;
            unsigned char* Pixels = nullptr;
            int Width = 0;
            int Height = 0;
            Io.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

            panels::SceneHost Host;
            Host.ReadSource = [](const std::filesystem::path& Path) -> std::optional<std::string>
            {
                std::ifstream File(Path, std::ios::binary);
                if (!File)
                    return std::nullopt;
                std::ostringstream Text;
                Text << File.rdbuf();
                return Text.str();
            };
            Host.WriteSource = [](const std::filesystem::path& Path, const std::string& Text, bool)
            {
                WriteText(Path, Text);
                return true;
            };
            Host.Log = [](const std::string& Message, bool Error)
            {
                if (Error)
                    FAIL_CHECK(Message);
            };
            Editor_.Initialize(Gpu_, std::move(Host));

            panels::SceneProject Project;
            Project.Root = Root_;
            Project.AssetRoot = Root_;
            Project.EntrySource = Root_ / "main.dba";
            Project.Profile = Level.Profile;
            Editor_.SetProject(Project);
            Editor_.Activate();
            // Docking and the renderer settle over the first frames.
            Frame(5);
        }

        ~SceneViewDriver()
        {
            Editor_.Shutdown();
            ImGui::DestroyContext();
            SDL_DestroyGPUDevice(Gpu_);
            SDL_Quit();
        }

        void Frame(int Count = 2)
        {
            for (int Index = 0; Index < Count; ++Index)
            {
                ImGui::NewFrame();
                const ImGuiID Dock = ImGui::GetID("scene-view-test");
                if (!LayoutBuilt_)
                {
                    LayoutBuilt_ = true;
                    Editor_.BuildDefaultLayout(Dock, "Output");
                }
                ImGui::DockSpaceOverViewport(Dock, ImGui::GetMainViewport());
                Editor_.DrawPanels();
                ImGui::Render();
            }
        }

        // Where the editor camera, which the tests never move, shows a point.
        glm::vec2 ScreenOf(const glm::vec3& Point) const
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName(panels::SceneEditor::KPanelViewport);
            const float Spacing = ImGui::GetStyle().ItemSpacing.y;
            const glm::vec2 Min(Window->DC.CursorStartPos.x,
                                Window->DC.CursorStartPos.y + Spacing + ImGui::GetFrameHeight() + Spacing);
            const glm::vec2 Size = glm::vec2(Window->WorkRect.Max.x, Window->WorkRect.Max.y) - Min;

            const glm::vec3 Eye(0.0f, 80.0f, -180.0f);
            const glm::vec3 Ahead = scene::LightDirection(glm::vec3(20.0f, 0.0f, 0.0f));
            const glm::mat4 ViewProjection = glm::perspective(glm::radians(60.0f), Size.x / Size.y, 0.5f, 20000.0f) *
                                             glm::lookAt(Eye, Eye + Ahead, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec4 Clip = ViewProjection * glm::vec4(Point, 1.0f);
            const glm::vec2 Ndc = glm::vec2(Clip) / Clip.w;
            return Min + glm::vec2((Ndc.x + 1.0f) * 0.5f, (1.0f - Ndc.y) * 0.5f) * Size;
        }

        void MoveMouse(const glm::vec2& At)
        {
            ImGui::GetIO().AddMousePosEvent(At.x, At.y);
            Frame();
        }

        void Button(bool Down)
        {
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, Down);
            Frame();
        }

        void Key(ImGuiKey Value, bool Down)
        {
            ImGui::GetIO().AddKeyEvent(Value, Down);
            Frame();
        }

        void Tap(ImGuiKey Value)
        {
            Key(Value, true);
            Key(Value, false);
        }

        void Click(const glm::vec3& Point)
        {
            MoveMouse(ScreenOf(Point));
            Button(true);
            Button(false);
        }

        // Finds the gizmo's X arrow to the right of `Center` and drags it sideways.
        void DragGizmoX(const glm::vec3& Center, float Pixels)
        {
            const glm::vec2 Start = ScreenOf(Center);
            glm::vec2 Grip = Start;
            bool Found = false;
            for (float Offset = 10.0f; Offset < 200.0f && !Found; Offset += 5.0f)
            {
                Grip = Start + glm::vec2(Offset, 0.0f);
                MoveMouse(Grip);
                Found = ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X);
            }
            REQUIRE(Found);
            Button(true);
            for (int Step = 1; Step <= 4; ++Step)
                MoveMouse(Grip + glm::vec2(Pixels * static_cast<float>(Step) / 4.0f, 0.0f));
            Button(false);
        }

        // The level as a save writes it.
        std::vector<glm::vec3> SavedPositions()
        {
            REQUIRE(Editor_.Save());
            std::ifstream File(Root_ / scene::LevelRelativePath("Level"), std::ios::binary);
            std::ostringstream Text;
            Text << File.rdbuf();
            const scene::LevelReadResult Read =
                scene::ReadLevel(Text.str(), "Level", dbc::language::LanguageProfile::Dbc);
            for (const scene::ReadError& Error : Read.Errors)
                FAIL_CHECK(Error.Line << ": " << Error.Message);
            std::vector<glm::vec3> Positions;
            for (const scene::SceneNode& Node : Read.Document.Nodes)
                Positions.push_back(Node.Position);
            return Positions;
        }

      private:
        static void WriteText(const std::filesystem::path& Path, const std::string& Text)
        {
            std::filesystem::create_directories(Path.parent_path());
            std::ofstream File(Path, std::ios::binary);
            File << Text;
        }

        std::filesystem::path Root_;
        SDL_GPUDevice* Gpu_ = nullptr;
        panels::SceneEditor Editor_;
        bool LayoutBuilt_ = false;
    };
} // namespace

TEST_CASE("A gizmo move lands on the grid by default, and the command key inverts it")
{
    const glm::vec3 Start(3.0f, 15.0f, 2.0f);
    SceneViewDriver Driver("default_snap", LevelOfCubes({Start}));
    Driver.Click(Start);

    Driver.DragGizmoX(Start, 120.0f);
    const glm::vec3 Moved = Driver.SavedPositions().at(0);
    CHECK(Moved.x > Start.x + 10.0f);
    CHECK(IsMultipleOf(Moved.x, 10.0f));
    // An axis the drag did not touch keeps its value, on the grid or not.
    CHECK(Moved.y == doctest::Approx(Start.y));
    CHECK(Moved.z == doctest::Approx(Start.z));

    Driver.Key(KCommandKey, true);
    Driver.DragGizmoX(Moved, 37.0f);
    Driver.Key(KCommandKey, false);
    const glm::vec3 Free = Driver.SavedPositions().at(0);
    CHECK(Free.x > Moved.x);
    CHECK(!IsMultipleOf(Free.x, 10.0f));
}

TEST_CASE("The grid keys change the step moves land on")
{
    const glm::vec3 Start(3.0f, 15.0f, 2.0f);
    SceneViewDriver Driver("step_keys", LevelOfCubes({Start}));
    Driver.Click(Start);

    Driver.Tap(ImGuiKey_Equal);
    Driver.DragGizmoX(Start, 160.0f);
    const glm::vec3 Coarse = Driver.SavedPositions().at(0);
    CHECK(Coarse.x > Start.x + 10.0f);
    CHECK(IsMultipleOf(Coarse.x, 25.0f));

    Driver.Tap(ImGuiKey_Minus);
    Driver.Tap(ImGuiKey_Minus);
    Driver.DragGizmoX(Coarse, 33.0f);
    const glm::vec3 Fine = Driver.SavedPositions().at(0);
    CHECK(Fine.x > Coarse.x);
    CHECK(IsMultipleOf(Fine.x, 5.0f));
}

TEST_CASE("Aligning puts every axis of the selection on the grid")
{
    const glm::vec3 Start(13.0f, 17.0f, -4.0f);
    SceneViewDriver Driver("align", LevelOfCubes({Start, glm::vec3(301.0f, 17.0f, -4.0f)}, 40.0f));
    Driver.Click(Start);

    Driver.Tap(ImGuiKey_G);
    const std::vector<glm::vec3> Positions = Driver.SavedPositions();
    CHECK(Positions.at(0).x == doctest::Approx(10.0f));
    CHECK(Positions.at(0).y == doctest::Approx(20.0f));
    CHECK(Positions.at(0).z == doctest::Approx(0.0f));
    // Only the selection moves.
    CHECK(Positions.at(1).x == doctest::Approx(301.0f));
}

TEST_CASE("A group move puts the active node on the grid and keeps the distances")
{
    // Their centre, at -2.5, is not on the grid distance from either node.
    const glm::vec3 First(-40.0f, 15.0f, 0.0f);
    const glm::vec3 Active(35.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("group", LevelOfCubes({First, Active}));
    Driver.Click(First);
    Driver.Key(KCommandKey, true);
    Driver.Click(Active);
    Driver.Key(KCommandKey, false);

    Driver.DragGizmoX((First + Active) * 0.5f, 120.0f);
    const std::vector<glm::vec3> Positions = Driver.SavedPositions();
    CHECK(Positions.at(1).x > Active.x + 10.0f);
    CHECK(IsMultipleOf(Positions.at(1).x, 10.0f));
    CHECK(Positions.at(1).x - Positions.at(0).x == doctest::Approx(Active.x - First.x));
}

TEST_CASE("Holding V drags the selection by a vertex onto another node's vertex")
{
    const glm::vec3 Moving(-40.0f, 15.0f, 0.0f);
    const glm::vec3 Fixed(40.0f, 15.0f, 0.0f);
    const glm::vec3 Corner(KCubeSize * 0.5f, KCubeSize * 0.5f, -KCubeSize * 0.5f);
    const glm::vec3 Across(-KCubeSize * 0.5f, KCubeSize * 0.5f, -KCubeSize * 0.5f);
    SceneViewDriver Driver("vertex", LevelOfCubes({Moving, Fixed}));
    Driver.Click(Moving);

    // Away from any other node's vertex, nothing moves: the drag waits for one in reach.
    Driver.Key(ImGuiKey_V, true);
    Driver.MoveMouse(Driver.ScreenOf(Moving + Corner));
    Driver.Button(true);
    Driver.MoveMouse(Driver.ScreenOf(Moving + Corner) + glm::vec2(0.0f, -150.0f));
    Driver.Button(false);
    CHECK(Driver.SavedPositions().at(0).x == doctest::Approx(Moving.x));

    // The moving cube's top right corner lands on the fixed cube's top left corner.
    Driver.MoveMouse(Driver.ScreenOf(Moving + Corner));
    Driver.Button(true);
    Driver.MoveMouse(Driver.ScreenOf(Fixed + Across) + glm::vec2(8.0f, 5.0f));
    Driver.Button(false);
    Driver.Key(ImGuiKey_V, false);

    const std::vector<glm::vec3> Positions = Driver.SavedPositions();
    CHECK(Positions.at(0).x == doctest::Approx(Fixed.x - KCubeSize));
    CHECK(Positions.at(0).y == doctest::Approx(Moving.y));
    CHECK(Positions.at(0).z == doctest::Approx(Moving.z));
    CHECK(Positions.at(1).x == doctest::Approx(Fixed.x));
}
