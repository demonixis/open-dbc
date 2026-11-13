// Drives the real Scene view with simulated input: ImGui, ImGuizmo and the renderer run, without a window.

#include <doctest/doctest.h>

#include "core/i18n.hpp"
#include "core/locales.hpp"
#include "panels/scene_editor.hpp"
#include "path_utf8.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_geometry.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
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

    std::size_t CountOf(const std::string& Text, const std::string& Part)
    {
        std::size_t Count = 0;
        for (std::size_t At = Text.find(Part); At != std::string::npos; At = Text.find(Part, At + Part.size()))
            ++Count;
        return Count;
    }

    bool IsMultipleOf(float Value, float Step)
    {
        return std::abs(Value - std::round(Value / Step) * Step) < 0.001f;
    }

    class SceneViewDriver
    {
      public:
        SceneViewDriver(const std::string& Name, const scene::SceneDocument& Level,
                        const std::function<void(const std::filesystem::path&)>& PrepareAssets = {})
            : Root_(std::filesystem::path(TEST_OUTPUT_DIR) / "scene_view" / Name)
        {
            REQUIRE(SDL_Init(SDL_INIT_VIDEO));
            Gpu_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
            REQUIRE(Gpu_ != nullptr);

            std::filesystem::remove_all(Root_);
            WriteText(Root_ / "main.dba", "SYNC\n");
            WriteText(Root_ / scene::LevelRelativePath(Level.Name), scene::WriteLevel(Level));
            WriteText(Root_ / scene::WorldRelativePath(Level.Name), scene::WriteWorld(Level));
            if (PrepareAssets)
                PrepareAssets(Root_);

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
            Host.Log = [this](const std::string& Message, bool Error)
            {
                if (Error && !ExpectErrors_)
                    FAIL_CHECK(Message);
                Log_.push_back(Message);
            };
            Editor_.Initialize(Gpu_, std::move(Host));

            panels::SceneProject Project;
            Project.Root = Root_;
            Project.AssetRoot = Root_;
            Project.EntrySource = Root_ / "main.dba";
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
                if (CaptureText_)
                    ImGui::LogToBuffer();
                Editor_.DrawPanels();
                if (CaptureText_)
                {
                    DrawnText_ = ImGui::GetCurrentContext()->LogBuffer.c_str();
                    ImGui::LogFinish();
                }
                Editor_.HandleShortcuts();
                ImGui::Render();
            }
        }

        // Where the editor camera, which the tests never move, shows a point.
        glm::vec2 ScreenOf(const glm::vec3& Point) const
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName(TrId(panels::SceneEditor::KPanelViewport));
            const float Spacing = ImGui::GetStyle().ItemSpacing.y;
            const float Row = ImGui::GetFrameHeight() + Spacing;
            const glm::vec2 Min(Window->DC.CursorStartPos.x,
                                Window->DC.CursorStartPos.y + Spacing + Row * static_cast<float>(BarRows_));
            const glm::vec2 Size = glm::vec2(Window->WorkRect.Max.x, Window->WorkRect.Max.y) - Min;

            const glm::vec3 Eye(0.0f, 80.0f, -180.0f);
            const glm::vec3 Ahead = scene::LightDirection(glm::vec3(20.0f, 0.0f, 0.0f));
            const glm::mat4 ViewProjection = glm::perspective(glm::radians(60.0f), Size.x / Size.y, 0.5f, 20000.0f) *
                                             glm::lookAt(Eye, Eye + Ahead, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec4 Clip = ViewProjection * glm::vec4(Point, 1.0f);
            const glm::vec2 Ndc = glm::vec2(Clip) / Clip.w;
            return Min + glm::vec2((Ndc.x + 1.0f) * 0.5f, (1.0f - Ndc.y) * 0.5f) * Size;
        }

        // Whether the camera, at its starting angle, looks straight at a point from close by.
        bool Frames(const glm::vec3& Point) const
        {
            const glm::vec3 ToPoint = Point - Editor_.View().Eye;
            const glm::vec3 Ahead = scene::LightDirection(glm::vec3(20.0f, 0.0f, 0.0f));
            return glm::length(ToPoint) < 60.0f && glm::dot(glm::normalize(ToPoint), Ahead) > 0.999f;
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
            // The paint tool's settings take a second row above the viewport.
            if (Value == ImGuiKey_B)
                BarRows_ = 2;
            else if (Value == ImGuiKey_Q || Value == ImGuiKey_W || Value == ImGuiKey_E || Value == ImGuiKey_R)
                BarRows_ = 1;
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

        glm::vec3 Eye() const
        {
            return Editor_.View().Eye;
        }

        // The view's third row: the engine's left-handed axes look along it.
        glm::vec3 Ahead() const
        {
            const glm::mat4& View = Editor_.View().View;
            return glm::vec3(View[0][2], View[1][2], View[2][2]);
        }

        // Moves the pointer as a captured mouse does; returns whether the editor took the motion.
        bool Steer(const glm::vec2& Motion)
        {
            SDL_Event Event = {};
            Event.type = SDL_EVENT_MOUSE_MOTION;
            Event.motion.xrel = Motion.x;
            Event.motion.yrel = Motion.y;
            const bool Taken = Editor_.HandleEvent(Event);
            Frame();
            return Taken;
        }

        std::string SavedText()
        {
            REQUIRE(Editor_.Save());
            std::ifstream File(Root_ / scene::LevelRelativePath("Level"), std::ios::binary);
            std::ostringstream Text;
            Text << File.rdbuf();
            return Text.str();
        }

        // The level as a save writes it.
        std::vector<scene::SceneNode> SavedNodes()
        {
            const scene::LevelReadResult Read = scene::ReadLevel(SavedText(), "Level");
            for (const scene::ReadError& Error : Read.Errors)
                FAIL_CHECK(Error.Line << ": " << Error.Message);
            return Read.Document.Nodes;
        }

        std::vector<glm::vec3> SavedPositions()
        {
            std::vector<glm::vec3> Positions;
            for (const scene::SceneNode& Node : SavedNodes())
                Positions.push_back(Node.Position);
            return Positions;
        }

        // Clicks a row of the Hierarchy, counted from the first under the level's name.
        void ClickHierarchyRow(int Row)
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName(TrId(panels::SceneEditor::KPanelHierarchy));
            const float Line = ImGui::GetTextLineHeightWithSpacing();
            MoveMouse(glm::vec2(Window->WorkRect.Min.x + 70.0f, Window->DC.CursorStartPos.y +
                                                                    Line * static_cast<float>(Row + 1) +
                                                                    ImGui::GetTextLineHeight() * 0.5f));
            Button(true);
            Button(false);
        }

        std::string DrawnText()
        {
            CaptureText_ = true;
            Frame(1);
            CaptureText_ = false;
            return DrawnText_;
        }

        // What is drawn with the limb list open. The popup is closed again: it would take the next click.
        std::string DrawnTextWithLimbs()
        {
            Editor_.ShowLimbs();
            std::string Text = DrawnText();
            if (ImGui::GetCurrentContext()->OpenPopupStack.Size > 0)
                ImGui::ClosePopupToLevel(0, false);
            return Text;
        }

        glm::vec2 FirstAssetPosition() const
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName(TrId(panels::SceneEditor::KPanelAssets));
            REQUIRE(Window != nullptr);
            REQUIRE(Window->DC.ChildWindows.Size == 1);
            const ImGuiWindow* Grid = Window->DC.ChildWindows[0];
            return glm::vec2(Grid->DC.CursorStartPos.x + 30.0f, Grid->DC.CursorStartPos.y + 30.0f);
        }

        void ClickFirstAsset()
        {
            // Each click is a fresh selection, not the double-click that inserts a model.
            Frame(30);
            MoveMouse(FirstAssetPosition());
            Button(true);
            Button(false);
        }

        void RefreshAssets()
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName(TrId(panels::SceneEditor::KPanelAssets));
            REQUIRE(Window != nullptr);
            MoveMouse(glm::vec2(Window->DC.CursorStartPos.x + 20.0f,
                                Window->DC.CursorStartPos.y + ImGui::GetFrameHeight() * 0.5f));
            Button(true);
            Button(false);
        }

        void ClickCamera()
        {
            const ImGuiWindow* Window = ImGui::FindWindowByName(TrId(panels::SceneEditor::KPanelHierarchy));
            REQUIRE(Window != nullptr);
            MoveMouse(glm::vec2(Window->WorkRect.Min.x + 70.0f,
                                Window->DC.CursorMaxPos.y - ImGui::GetTextLineHeight() * 0.5f));
            Button(true);
            Button(false);
        }

        panels::SceneEditor& Editor()
        {
            return Editor_;
        }

        const std::filesystem::path& Root() const
        {
            return Root_;
        }

        void ExpectErrors(bool Value)
        {
            ExpectErrors_ = Value;
        }

        // Goes through the New Level popup.
        void CreateLevel(const std::string& Name)
        {
            Editor_.RequestNewLevel();
            Frame();
            ImGui::GetIO().AddInputCharactersUTF8(Name.c_str());
            Frame();
            Tap(ImGuiKey_Enter);
            Frame();
        }

        void WriteFile(const std::filesystem::path& Relative, const std::string& Text)
        {
            WriteText(Root_ / Relative, Text);
        }

        std::string ReadFile(const std::filesystem::path& Relative) const
        {
            std::ifstream File(Root_ / Relative, std::ios::binary);
            std::ostringstream Text;
            Text << File.rdbuf();
            return Text.str();
        }

        // How many Output lines hold `Part`.
        std::size_t Logged(const std::string& Part) const
        {
            return static_cast<std::size_t>(std::count_if(Log_.begin(), Log_.end(), [&](const std::string& Line)
                                                          { return Line.find(Part) != std::string::npos; }));
        }

        void Undo()
        {
            Key(KCommandKey, true);
            Tap(ImGuiKey_Z);
            Key(KCommandKey, false);
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
        bool ExpectErrors_ = false;
        bool CaptureText_ = false;
        std::string DrawnText_;
        std::vector<std::string> Log_;
        int BarRows_ = 1;
    };

    void WriteInspectorModel(const std::filesystem::path& Root, const std::string& SecondLimb = "Turret")
    {
        std::ofstream(Root / "pair.mtl") << "newmtl Paint\nKd 1 1 1\nmap_Kd missing.png\n";
        std::ofstream(Root / "pair.obj") << "mtllib pair.mtl\n"
                                            "v -20 0 0\nv 0 0 0\nv 0 20 0\nv 20 0 0\nv 20 20 0\n"
                                            "o Hull\nusemtl Paint\nf 1 2 3\no "
                                         << SecondLimb << "\nusemtl Paint\nf 2 4 5\n";
    }
} // namespace

TEST_CASE("A single model asset click shows limb mapping without inserting or editing a node")
{
    SceneViewDriver Driver("inspect_model_asset", LevelOfCubes({glm::vec3(0.0f)}),
                           [](const std::filesystem::path& Root) { WriteInspectorModel(Root); });
    Driver.ClickFirstAsset();
    // The Inspector holds the statistics and a Limbs button; the list opens in its own popup.
    const std::string Closed = Driver.DrawnText();
    INFO(Closed);
    for (const char* Label : {"Model details", "pair.obj", "Triangles", "Vertices", "Meshes", "Referenced textures",
                              "Limbs (3)..."})
        CHECK(Closed.find(Label) != std::string::npos);
    CHECK(Closed.find("Turret") == std::string::npos);
    CHECK_FALSE(Driver.Editor().Dirty());
    CHECK(Driver.SavedNodes().size() == 1);

    // A second click still follows the established double-click insertion flow.
    Driver.Button(true);
    Driver.Button(false);
    CHECK(Driver.DrawnText().find("Model file") != std::string::npos);
    CHECK(Driver.Editor().Dirty());
    const std::vector<scene::SceneNode> Nodes = Driver.SavedNodes();
    REQUIRE(Nodes.size() == 2);
    CHECK(Nodes.back().Kind == scene::NodeKind::Model);
    CHECK(Nodes.back().Asset == "pair.obj");

    const std::string Text = Driver.DrawnTextWithLimbs();
    INFO(Text);
    for (const char* Label : {"ID", "Hull", "Turret"})
        CHECK(Text.find(Label) != std::string::npos);
}

TEST_CASE("The Inspector offers the texture stages of an object, not of a light")
{
    scene::SceneDocument Level = LevelOfCubes({glm::vec3(0.0f)});
    Level.Add(scene::NodeKind::Light);
    SceneViewDriver Driver("inspect_texture_stages", Level);
    Driver.ClickHierarchyRow(0);
    const std::string Text = Driver.DrawnText();
    INFO(Text);
    for (const char* Label : {"Texture stages", "Shading", "Second texture", "Cube map", "Alpha", "Reflection",
                              "Depth buffer", "Attachment", "Glued to"})
        CHECK(Text.find(Label) != std::string::npos);
    Driver.ClickHierarchyRow(1);
    CHECK(Driver.DrawnText().find("Texture stages") == std::string::npos);
    CHECK_FALSE(Driver.Editor().Dirty());
}

TEST_CASE("Holding and dragging a model tile keeps the selected model file field available")
{
    scene::SceneDocument Level;
    Level.Name = "Level";
    Level.Add(scene::NodeKind::Model).Asset = "pair.obj";
    SceneViewDriver Driver("inspect_model_drag", Level,
                           [](const std::filesystem::path& Root) { WriteInspectorModel(Root); });
    Driver.ClickHierarchyRow(0);
    REQUIRE(Driver.DrawnText().find("Model file") != std::string::npos);

    Driver.Frame(30);
    const glm::vec2 Tile = Driver.FirstAssetPosition();
    Driver.MoveMouse(Tile);
    Driver.Button(true);
    CHECK(Driver.DrawnText().find("Model file") != std::string::npos);

    Driver.MoveMouse(Tile + glm::vec2(40.0f, 0.0f));
    const ImGuiPayload* Payload = ImGui::GetDragDropPayload();
    REQUIRE(Payload != nullptr);
    CHECK(Payload->IsDataType(panels::SceneEditor::KAssetPayload));
    CHECK(Driver.DrawnText().find("Model file") != std::string::npos);
    // Cancel the drag outside every drop target; it must not turn into an asset selection.
    Driver.MoveMouse(glm::vec2(1.0f, 1.0f));
    Driver.Button(false);
    CHECK(Driver.DrawnText().find("Model file") != std::string::npos);
    CHECK_FALSE(Driver.Editor().Dirty());
    CHECK(Driver.SavedNodes().size() == 1);
}

TEST_CASE("Model details follow scene selection and leave the Inspector for a cube or camera")
{
    scene::SceneDocument Level = LevelOfCubes({glm::vec3(-45.0f, 0.0f, 0.0f)});
    scene::SceneNode& Model = Level.Add(scene::NodeKind::Model);
    Model.Asset = "pair.obj";
    Model.Position = glm::vec3(45.0f, 0.0f, 0.0f);
    Level.Env.UseCamera = true;
    SceneViewDriver Driver("inspect_model_selection", Level,
                           [](const std::filesystem::path& Root) { WriteInspectorModel(Root); });

    Driver.ClickHierarchyRow(1);
    const std::string ModelText = Driver.DrawnTextWithLimbs();
    INFO(ModelText);
    CHECK(ModelText.find("Model details") != std::string::npos);
    CHECK(ModelText.find("Hull") != std::string::npos);
    CHECK(ModelText.find("Turret") != std::string::npos);

    Driver.ClickHierarchyRow(0);
    CHECK(Driver.DrawnText().find("Model details") == std::string::npos);
    Driver.ClickFirstAsset();
    REQUIRE(Driver.DrawnText().find("Model details") != std::string::npos);
    Driver.ClickCamera();
    CHECK(Driver.DrawnText().find("Model details") == std::string::npos);
    Driver.ClickFirstAsset();
    REQUIRE(Driver.DrawnText().find("Model details") != std::string::npos);
    Driver.Click(glm::vec3(-45.0f, 0.0f, 0.0f));
    CHECK(Driver.DrawnText().find("Model details") == std::string::npos);
    CHECK_FALSE(Driver.Editor().Dirty());
}

TEST_CASE("Refreshing assets rereads model details and a missing model cannot keep stale limbs")
{
    scene::SceneDocument Level;
    Level.Name = "Level";
    Level.Add(scene::NodeKind::Model).Asset = "pair.obj";
    SceneViewDriver Driver("inspect_model_refresh", Level,
                           [](const std::filesystem::path& Root) { WriteInspectorModel(Root); });
    Driver.ClickHierarchyRow(0);
    REQUIRE(Driver.DrawnTextWithLimbs().find("Turret") != std::string::npos);

    WriteInspectorModel(Driver.Root(), "Barrel");
    Driver.RefreshAssets();
    const std::string Changed = Driver.DrawnTextWithLimbs();
    INFO(Changed);
    CHECK(Changed.find("Barrel") != std::string::npos);
    CHECK(Changed.find("Turret") == std::string::npos);

    REQUIRE(std::filesystem::remove(Driver.Root() / "pair.obj"));
    Driver.RefreshAssets();
    const std::string Missing = Driver.DrawnTextWithLimbs();
    INFO(Missing);
    CHECK(Missing.find("Model details") != std::string::npos);
    CHECK(Missing.find("The model file cannot be read.") != std::string::npos);
    CHECK(Missing.find("Barrel") == std::string::npos);
    CHECK(Missing.find("Hull") == std::string::npos);
    CHECK_FALSE(Driver.Editor().Dirty());
}

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

TEST_CASE("F frames the selection whichever Scene panel has the focus")
{
    const glm::vec3 Near(0.0f);
    const glm::vec3 Far(600.0f, 10.0f, 300.0f);
    SceneViewDriver Driver("frame_key", LevelOfCubes({Near, Far}));

    const glm::vec3 Start = Driver.Editor().View().Eye;
    Driver.Tap(ImGuiKey_F);
    CHECK(glm::length(Driver.Editor().View().Eye - Start) == doctest::Approx(0.0f));

    Driver.Click(Near);
    Driver.Tap(ImGuiKey_F);
    CHECK(Driver.Frames(Near));

    // A click in the Hierarchy leaves the focus there.
    Driver.ClickHierarchyRow(1);
    CHECK_FALSE(Driver.Frames(Far));
    Driver.Tap(ImGuiKey_F);
    CHECK(Driver.Frames(Far));
}

TEST_CASE("A double-click on a Hierarchy row frames it, where a click only selects")
{
    const glm::vec3 Far(600.0f, 10.0f, 300.0f);
    SceneViewDriver Driver("frame_row", LevelOfCubes({glm::vec3(0.0f), Far}));
    Driver.ClickHierarchyRow(1);
    CHECK_FALSE(Driver.Frames(Far));
    Driver.ClickHierarchyRow(1);
    CHECK(Driver.Frames(Far));
}

TEST_CASE("A double-click on a painted group's row frames the group")
{
    const glm::vec3 Source(-150.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("frame_group", LevelOfCubes({Source}));
    Driver.Click(Source);
    Driver.Tap(ImGuiKey_B);
    Driver.Click(glm::vec3(0.0f));
    const glm::vec3 Copy = Driver.SavedNodes().at(1).Position;

    Driver.ClickHierarchyRow(1);
    CHECK_FALSE(Driver.Frames(Copy));
    Driver.ClickHierarchyRow(1);
    CHECK(Driver.Frames(Copy));
}

TEST_CASE("A fast two-finger scroll zooms in, and the same scroll back out returns")
{
    SceneViewDriver Driver("scroll_zoom", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.MoveMouse(Driver.ScreenOf(glm::vec3(0.0f)));
    const glm::vec3 Start = Driver.Editor().View().Eye;

    ImGui::GetIO().AddMouseWheelEvent(0.0f, 30.0f);
    Driver.Frame();
    CHECK(glm::length(Driver.Editor().View().Eye - Start) > 150.0f);

    ImGui::GetIO().AddMouseWheelEvent(0.0f, -30.0f);
    Driver.Frame();
    CHECK(glm::length(Driver.Editor().View().Eye - Start) < 0.01f);
}

TEST_CASE("The scroll speed setting scales the viewport's zoom")
{
    SceneViewDriver Driver("scroll_speed", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.MoveMouse(Driver.ScreenOf(glm::vec3(0.0f)));
    const glm::vec3 Start = Driver.Editor().View().Eye;

    core::EditorSettings Settings;
    Settings.ScrollSpeed = 2.0f;
    Driver.Editor().SetEditorSettings(Settings);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);
    Driver.Frame();
    // 190 units from the pivot, kept at 0.88 per notch.
    CHECK(glm::length(Driver.Editor().View().Eye - Start) == doctest::Approx(190.0f * (1.0f - 0.88f * 0.88f)));
}

TEST_CASE("Holding a movement key flies where the view looks, and the pointer steers")
{
    SceneViewDriver Driver("fly_keys", LevelOfCubes({glm::vec3(0.0f)}));
    const ImGuiKey Forward = panels::KeyAtPosition(SDL_SCANCODE_W);
    Driver.MoveMouse(Driver.ScreenOf(glm::vec3(0.0f)));
    const glm::vec3 Start = Driver.Eye();
    const glm::vec3 Ahead = Driver.Ahead();
    CHECK_FALSE(Driver.Steer(glm::vec2(50.0f, 0.0f)));
    CHECK(glm::dot(Driver.Ahead(), Ahead) == doctest::Approx(1.0f));

    // Past a tap, since W also picks the Move tool on QWERTY.
    Driver.Key(Forward, true);
    Driver.Frame(20);
    CHECK(Driver.Editor().Flying());
    CHECK(glm::dot(glm::normalize(Driver.Eye() - Start), Ahead) > 0.999f);

    CHECK(Driver.Steer(glm::vec2(200.0f, 0.0f)));
    const glm::vec3 Steered = Driver.Ahead();
    CHECK(glm::dot(Steered, Ahead) < 0.9f);
    const glm::vec3 Before = Driver.Eye();
    Driver.Frame(10);
    CHECK(glm::dot(glm::normalize(Driver.Eye() - Before), Steered) > 0.999f);

    Driver.Key(Forward, false);
    CHECK_FALSE(Driver.Editor().Flying());
    CHECK_FALSE(Driver.Steer(glm::vec2(50.0f, 0.0f)));
    const glm::vec3 Stopped = Driver.Eye();
    Driver.Frame(10);
    CHECK(glm::length(Driver.Eye() - Stopped) == doctest::Approx(0.0f));
}

TEST_CASE("A right drag steers with the pointer too, and flies with the movement keys")
{
    SceneViewDriver Driver("fly_right_drag", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.MoveMouse(Driver.ScreenOf(glm::vec3(0.0f)));
    const glm::vec3 Start = Driver.Eye();
    const glm::vec3 Ahead = Driver.Ahead();

    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    Driver.Frame();
    CHECK(Driver.Editor().Flying());
    CHECK(Driver.Steer(glm::vec2(200.0f, 0.0f)));
    CHECK(glm::dot(Driver.Ahead(), Ahead) < 0.9f);
    CHECK(glm::length(Driver.Eye() - Start) == doctest::Approx(0.0f));

    const ImGuiKey Back = panels::KeyAtPosition(SDL_SCANCODE_S);
    Driver.Key(Back, true);
    CHECK(glm::length(Driver.Eye() - Start) > 0.0f);
    Driver.Key(Back, false);

    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    Driver.Frame();
    CHECK_FALSE(Driver.Editor().Flying());
}

TEST_CASE("The camera speed setting scales a flight")
{
    SceneViewDriver Driver("fly_speed", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.MoveMouse(Driver.ScreenOf(glm::vec3(0.0f)));
    const ImGuiKey Back = panels::KeyAtPosition(SDL_SCANCODE_S);
    const auto Fly = [&]
    {
        const glm::vec3 Before = Driver.Eye();
        Driver.Key(Back, true);
        Driver.Frame(10);
        Driver.Key(Back, false);
        return glm::length(Driver.Eye() - Before);
    };
    const float Normal = Fly();
    CHECK(Normal > 0.0f);

    core::EditorSettings Settings;
    Settings.CameraSpeed = 2.0f;
    Driver.Editor().SetEditorSettings(Settings);
    CHECK(Fly() == doctest::Approx(Normal * 2.0f).epsilon(0.01));
}

TEST_CASE("A movement key that also picks a tool does so when tapped, and flies when held")
{
    const glm::vec3 Center(0.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("fly_tool_key", LevelOfCubes({Center}));
    Driver.Click(Center);

    // W on QWERTY, which picks the Move tool; Q on AZERTY, which picks the View tool.
    const bool Qwerty = panels::KeyAtPosition(SDL_SCANCODE_W) == ImGuiKey_W;
    const ImGuiKey Both = Qwerty ? ImGuiKey_W : ImGuiKey_Q;
    const ImGuiKey Other = Qwerty ? ImGuiKey_Q : ImGuiKey_W;
    Driver.Tap(Other);
    CHECK(Driver.Editor().ViewToolOn() == Qwerty);

    const glm::vec3 Start = Driver.Eye();
    Driver.Tap(Both);
    CHECK(glm::length(Driver.Eye() - Start) == doctest::Approx(0.0f));
    CHECK(Driver.Editor().ViewToolOn() != Qwerty);

    Driver.Tap(Other);
    Driver.Key(Both, true);
    Driver.Frame(20);
    CHECK(Driver.Editor().Flying());
    CHECK(glm::length(Driver.Eye() - Start) > 0.0f);
    Driver.Key(Both, false);
    CHECK_FALSE(Driver.Editor().Flying());
    CHECK(Driver.Editor().ViewToolOn() == Qwerty);
}

TEST_CASE("A movement key does not fly when pressed with a shortcut, or away from the viewport")
{
    SceneViewDriver Driver("fly_armed", LevelOfCubes({glm::vec3(0.0f)}));
    const glm::vec2 Over = Driver.ScreenOf(glm::vec3(0.0f));
    const ImGuiKey Back = panels::KeyAtPosition(SDL_SCANCODE_S);
    Driver.MoveMouse(Over);
    const glm::vec3 Start = Driver.Eye();

    // Cmd+S, with Cmd released first.
    Driver.Key(KCommandKey, true);
    Driver.Key(Back, true);
    Driver.Key(KCommandKey, false);
    Driver.Frame(20);
    CHECK_FALSE(Driver.Editor().Flying());
    Driver.Key(Back, false);

    Driver.MoveMouse(glm::vec2(1.0f, 1.0f));
    Driver.Key(Back, true);
    Driver.MoveMouse(Over);
    Driver.Frame(20);
    CHECK_FALSE(Driver.Editor().Flying());
    Driver.Key(Back, false);

    CHECK(glm::length(Driver.Eye() - Start) == doctest::Approx(0.0f));
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

TEST_CASE("A vertex drag is one undo step, however many vertices it lands on")
{
    const glm::vec3 Moving(-40.0f, 15.0f, 0.0f);
    const glm::vec3 Fixed(40.0f, 15.0f, 0.0f);
    const glm::vec3 Corner(KCubeSize * 0.5f, KCubeSize * 0.5f, -KCubeSize * 0.5f);
    const glm::vec3 NearTop(-KCubeSize * 0.5f, KCubeSize * 0.5f, -KCubeSize * 0.5f);
    const glm::vec3 NearBottom(-KCubeSize * 0.5f, -KCubeSize * 0.5f, -KCubeSize * 0.5f);
    SceneViewDriver Driver("vertex_undo", LevelOfCubes({Moving, Fixed}));
    Driver.Click(Moving);

    Driver.Key(ImGuiKey_V, true);
    Driver.MoveMouse(Driver.ScreenOf(Moving + Corner));
    Driver.Button(true);
    Driver.MoveMouse(Driver.ScreenOf(Fixed + NearTop));
    Driver.MoveMouse(Driver.ScreenOf(Fixed + NearBottom));
    Driver.Button(false);
    Driver.Key(ImGuiKey_V, false);
    CHECK(Driver.SavedPositions().at(0).y == doctest::Approx(Moving.y - KCubeSize));

    Driver.Key(KCommandKey, true);
    Driver.Tap(ImGuiKey_Z);
    Driver.Key(KCommandKey, false);
    const glm::vec3 Undone = Driver.SavedPositions().at(0);
    CHECK(Undone.x == doctest::Approx(Moving.x));
    CHECK(Undone.y == doctest::Approx(Moving.y));
}

TEST_CASE("The paint tool places one object on the grid, resting on the ground")
{
    const glm::vec3 Source(0.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("paint_one", LevelOfCubes({Source}));
    Driver.Click(Source);
    Driver.Tap(ImGuiKey_B);

    Driver.Click(glm::vec3(63.0f, 0.0f, 41.0f));
    std::vector<scene::SceneNode> Nodes = Driver.SavedNodes();
    REQUIRE(Nodes.size() == 2);
    CHECK(Nodes[0].Position.y == doctest::Approx(Source.y));
    CHECK_FALSE(Nodes[0].Painted);
    CHECK(Nodes[1].Painted);
    CHECK(Nodes[1].Position.x == doctest::Approx(60.0f));
    CHECK(Nodes[1].Position.z == doctest::Approx(40.0f));
    // A 20-unit cube's centre, 10 above the ground.
    CHECK(Nodes[1].Position.y == doctest::Approx(10.0f));
    CHECK(Nodes[1].Name == "Cube 1000_1001");
    const std::string Text = Driver.SavedText();
    CHECK(CountOf(Text, "MAKE OBJECT CUBE") == 1);
    CHECK(Text.find("INSTANCE OBJECT 1001, 1000") != std::string::npos);

    // Painted objects are not surfaces: the next one lands on the ground beside the first.
    Driver.Click(glm::vec3(63.0f, 0.0f, 41.0f));
    Nodes = Driver.SavedNodes();
    REQUIRE(Nodes.size() == 3);
    CHECK(Nodes[2].Position.y == doctest::Approx(10.0f));

    // The command key leaves the grid, and each click is one undo step.
    Driver.Key(KCommandKey, true);
    Driver.Click(glm::vec3(-37.0f, 0.0f, 23.0f));
    Driver.Key(KCommandKey, false);
    Nodes = Driver.SavedNodes();
    REQUIRE(Nodes.size() == 4);
    CHECK(Nodes[3].Position.x == doctest::Approx(-37.0f).epsilon(0.02));
    CHECK(!IsMultipleOf(Nodes[3].Position.x, 10.0f));
    Driver.Undo();
    CHECK(Driver.SavedNodes().size() == 3);
    Driver.Undo();
    Driver.Undo();
    CHECK(Driver.SavedNodes().size() == 1);

    // Other tools come back: the gizmo moves the selection again, and no click paints.
    Driver.Tap(ImGuiKey_W);
    Driver.DragGizmoX(Source, 60.0f);
    Nodes = Driver.SavedNodes();
    CHECK(Nodes.size() == 1);
    CHECK(Nodes[0].Position.x > 0.0f);
}

TEST_CASE("Several scatters its count over the radius with one click")
{
    const glm::vec3 Source(-150.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("paint_several", LevelOfCubes({Source}));
    Driver.Click(Source);
    Driver.Tap(ImGuiKey_B);
    Driver.Tap(ImGuiKey_2);

    const glm::vec3 Center(40.0f, 0.0f, 30.0f);
    Driver.Click(Center);
    const std::vector<scene::SceneNode> Nodes = Driver.SavedNodes();
    // The defaults: 5 objects over a radius of 50.
    REQUIRE(Nodes.size() == 6);
    for (std::size_t Index = 1; Index < Nodes.size(); ++Index)
    {
        CHECK(glm::length(glm::vec2(Nodes[Index].Position.x - Center.x, Nodes[Index].Position.z - Center.z)) <= 51.0f);
        CHECK(Nodes[Index].Position.y == doctest::Approx(10.0f));
        CHECK(Nodes[Index].Painted);
    }
    CHECK(CountOf(Driver.SavedText(), "INSTANCE OBJECT") == 5);
}

TEST_CASE("The brush fills up to its density, and Shift erases its copies")
{
    const glm::vec3 Source(-150.0f, 15.0f, 0.0f);
    const glm::vec3 Other(40.0f, 15.0f, 30.0f);
    SceneViewDriver Driver("paint_brush", LevelOfCubes({Source, Other}));
    Driver.Click(Source);
    Driver.Tap(ImGuiKey_B);
    Driver.Tap(ImGuiKey_3);

    // The defaults: 10 per 100 x 100 over a radius of 50 make 8. The other cube looks the same as the
    // source, so it counts towards them. Random points can leave a stamp short, and the next tops it up.
    const glm::vec3 Spot(40.0f, 0.0f, 30.0f);
    std::size_t Once = 0;
    for (int Stamp = 0; Stamp < 8 && Once < 9; ++Stamp)
    {
        Driver.Click(Spot);
        Once = Driver.SavedNodes().size();
    }
    CHECK(Once == 9);

    // Over the same spot, a second click adds nothing.
    Driver.Click(Spot);
    CHECK(Driver.SavedNodes().size() == Once);

    // A drag paints along the way, as one undo step.
    Driver.MoveMouse(Driver.ScreenOf(glm::vec3(-60.0f, 0.0f, 0.0f)));
    Driver.Button(true);
    for (const float X : {-90.0f, -120.0f, -150.0f, -180.0f, -210.0f})
        Driver.MoveMouse(Driver.ScreenOf(glm::vec3(X, 0.0f, 0.0f)));
    Driver.Button(false);
    const std::size_t Dragged = Driver.SavedNodes().size();
    CHECK(Dragged > Once + 8);
    Driver.Undo();
    CHECK(Driver.SavedNodes().size() == Once);

    // Shift erases painted copies within the radius, and never what was not painted.
    Driver.Key(ImGuiMod_Shift, true);
    Driver.Click(Spot);
    Driver.Key(ImGuiMod_Shift, false);
    const std::vector<scene::SceneNode> Left = Driver.SavedNodes();
    CHECK(Left.size() == 2);
    CHECK_FALSE(Left[0].Painted);
    CHECK_FALSE(Left[1].Painted);
    Driver.Undo();
    CHECK(Driver.SavedNodes().size() == Once);
}

TEST_CASE("The paint tool lands on what lies under the mouse, and stops at its edge")
{
    const glm::vec3 Source(-130.0f, 5.0f, 0.0f);
    scene::SceneDocument Level = LevelOfCubes({Source}, 10.0f);
    // A wide table, its top at Y = 60.
    scene::SceneNode& Table = Level.Add(scene::NodeKind::Box);
    Table.Extent = glm::vec3(100.0f, 20.0f, 100.0f);
    Table.Position = glm::vec3(0.0f, 50.0f, 0.0f);
    SceneViewDriver Driver("paint_surface", Level);
    Driver.Click(Source);
    Driver.Tap(ImGuiKey_B);

    Driver.Click(glm::vec3(0.0f, 60.0f, 0.0f));
    std::vector<scene::SceneNode> Nodes = Driver.SavedNodes();
    REQUIRE(Nodes.size() == 3);
    CHECK(Nodes[2].Position.x == doctest::Approx(0.0f));
    // A 10-unit cube's centre, 5 above the table.
    CHECK(Nodes[2].Position.y == doctest::Approx(65.0f));

    // Scattered from the table's corner, some of them fall past its edge and are left out.
    Driver.Tap(ImGuiKey_2);
    Driver.Click(glm::vec3(45.0f, 60.0f, -45.0f));
    Nodes = Driver.SavedNodes();
    CHECK(Nodes.size() <= 8);
    for (std::size_t Index = 3; Index < Nodes.size(); ++Index)
        CHECK(Nodes[Index].Position.y == doctest::Approx(65.0f));
}

TEST_CASE("Painted objects fold into one Hierarchy row, whose click selects them all")
{
    const glm::vec3 Source(-150.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("paint_hierarchy", LevelOfCubes({Source}));
    Driver.Click(Source);
    Driver.Tap(ImGuiKey_B);
    Driver.Click(glm::vec3(0.0f, 0.0f, 0.0f));
    Driver.Click(glm::vec3(30.0f, 0.0f, 0.0f));
    Driver.Click(glm::vec3(60.0f, 0.0f, 0.0f));
    CHECK(Driver.SavedNodes().size() == 4);

    // Row 0 is the source, row 1 the three copies together.
    Driver.ClickHierarchyRow(1);
    Driver.Tap(ImGuiKey_Delete);
    const std::vector<scene::SceneNode> Left = Driver.SavedNodes();
    REQUIRE(Left.size() == 1);
    CHECK_FALSE(Left[0].Painted);
}

TEST_CASE("Saving a level writes its folder and links it into a main source that has none")
{
    SceneViewDriver Driver("level_link", LevelOfCubes({glm::vec3(0.0f)}));
    REQUIRE(Driver.Editor().Save());
    CHECK(std::filesystem::is_regular_file(Driver.Root() / scene::WorldRelativePath("Level")));
    CHECK(Driver.ReadFile("main.dba") == "#include \"levels/Level/world_settings.dba\"\n"
                                         "#include \"levels/Level/Level.dba\"\n\n"
                                         "SetupWorld_Level()\nLoadLevel_Level()\n\nSYNC\n");

    // Saved again, nothing more to add.
    const std::string Linked = Driver.ReadFile("main.dba");
    REQUIRE(Driver.Editor().Save());
    CHECK(Driver.ReadFile("main.dba") == Linked);
}

TEST_CASE("A level already imported by the program leaves the main source alone")
{
    const std::string Main = "#include \"game.dba\"\nSYNC\n";
    SceneViewDriver Driver("level_imported", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.WriteFile("main.dba", Main);
    Driver.WriteFile("game.dba", "#include \"levels/Level/world_settings.dba\"\n#include \"levels/Level/Level.dba\"\n");
    REQUIRE(Driver.Editor().Save());
    CHECK(Driver.ReadFile("main.dba") == Main);
}

TEST_CASE("Another level imported by the program leaves the main source alone, and says so once")
{
    const std::string Main = "#include \"levels/Other/Other.dba\"\nLoadLevel_Other()\nSYNC\n";
    SceneViewDriver Driver("level_other", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.WriteFile("main.dba", Main);
    REQUIRE(Driver.Editor().Save());
    CHECK(Driver.ReadFile("main.dba") == Main);
    CHECK(Driver.Logged("stays out of main.dba") == 1);
    REQUIRE(Driver.Editor().Save());
    CHECK(Driver.Logged("stays out of main.dba") == 1);
}

TEST_CASE("A new level exists as soon as it is named, and only the first one joins the main source")
{
    SceneViewDriver Driver("level_new", LevelOfCubes({glm::vec3(0.0f)}));
    Driver.CreateLevel("Cave");
    CHECK(std::filesystem::is_regular_file(Driver.Root() / scene::LevelRelativePath("Cave")));
    CHECK(std::filesystem::is_regular_file(Driver.Root() / scene::WorldRelativePath("Cave")));
    const std::string Linked = Driver.ReadFile("main.dba");
    CHECK(Linked == "#include \"levels/Cave/world_settings.dba\"\n#include \"levels/Cave/Cave.dba\"\n\n"
                    "SetupWorld_Cave()\nLoadLevel_Cave()\n\nSYNC\n");

    Driver.CreateLevel("Deep");
    CHECK(std::filesystem::is_regular_file(Driver.Root() / scene::LevelRelativePath("Deep")));
    CHECK(Driver.ReadFile("main.dba") == Linked);
    CHECK(Driver.Logged("Level Deep stays out of main.dba") == 1);

    // A name already taken opens that level, and writes nothing.
    const std::string Before = Driver.ReadFile(scene::LevelRelativePath("Cave"));
    Driver.CreateLevel("Cave");
    CHECK(Driver.ReadFile(scene::LevelRelativePath("Cave")) == Before);
    CHECK_FALSE(Driver.Editor().Dirty());
}

TEST_CASE("The Scene view keeps working in French, and writes the same files")
{
    SceneViewDriver Driver("level_french", LevelOfCubes({glm::vec3(0.0f)}));
    core::ActivateInterfaceLanguage("fr");
    Driver.Frame();
    const ImGuiWindow* Viewport = ImGui::FindWindowByName(TrId(panels::SceneEditor::KPanelViewport));
    REQUIRE(Viewport != nullptr);
    CHECK(std::string_view(Viewport->Name).starts_with("Vue 3D###"));

    Driver.CreateLevel("Cave");
    CHECK(Driver.ReadFile(scene::LevelRelativePath("Cave")).find("FUNCTION LoadLevel_Cave()") != std::string::npos);
    CHECK(Driver.ReadFile(scene::WorldRelativePath("Cave")).find("FUNCTION SetupWorld_Cave()") != std::string::npos);
    CHECK(Driver.ReadFile("main.dba").find("LoadLevel_Cave()") != std::string::npos);
    core::ActivateInterfaceLanguage("en");
    Driver.Frame();
}

TEST_CASE("A level from before the folders moves into its own, and its environment follows when saved")
{
    SceneViewDriver Driver("level_flat", LevelOfCubes({glm::vec3(0.0f)}));
    std::filesystem::remove_all(Driver.Root() / scene::KLevelFolder);
    Driver.WriteFile("levels/Level.dba", "FUNCTION LoadLevel_Level()\n   FOG ON\n   MAKE OBJECT CUBE 1000, 20\n"
                                         "ENDFUNCTION\n");
    Driver.WriteFile("opendbc_game.dba", "FUNCTION SetupGame()\n   SYNC ON\nENDFUNCTION\n");
    Driver.WriteFile("main.dba", "#include \"opendbc_game.dba\"\n#include \"levels/Level.dba\"\n\n"
                                 "SetupGame()\nLoadLevel_Level()\n\nSYNC\n");

    const panels::SceneProject Project = *Driver.Editor().Project();
    Driver.Editor().SetProject(std::nullopt);
    Driver.Editor().SetProject(Project);
    Driver.Editor().Activate();
    Driver.Frame();

    CHECK_FALSE(std::filesystem::exists(Driver.Root() / "levels/Level.dba"));
    CHECK(std::filesystem::is_regular_file(Driver.Root() / scene::LevelRelativePath("Level")));
    CHECK(Driver.ReadFile("main.dba") == "#include \"opendbc_game.dba\"\n#include \"levels/Level/Level.dba\"\n\n"
                                         "SetupGame()\nLoadLevel_Level()\n\nSYNC\n");
    CHECK(Driver.Logged("opendbc_game.dba") == 1);
    CHECK_FALSE(Driver.Editor().Dirty());

    REQUIRE(Driver.Editor().Save());
    CHECK(Driver.ReadFile(scene::LevelRelativePath("Level")).find("FOG ON") == std::string::npos);
    CHECK(Driver.ReadFile(scene::WorldRelativePath("Level")).find("FOG ON") != std::string::npos);
    CHECK(Driver.ReadFile("main.dba") == "#include \"opendbc_game.dba\"\n#include \"levels/Level/Level.dba\"\n"
                                         "#include \"levels/Level/world_settings.dba\"\n\n"
                                         "SetupGame()\nSetupWorld_Level()\nLoadLevel_Level()\n\nSYNC\n");
    CHECK(std::filesystem::is_regular_file(Driver.Root() / "opendbc_game.dba"));

    // A target already there is reported, and the older file stays.
    Driver.WriteFile("levels/Level.dba", "FUNCTION LoadLevel_Level()\nENDFUNCTION\n");
    Driver.ExpectErrors(true);
    Driver.Editor().Activate();
    CHECK(Driver.Logged("Cannot move") == 1);
    CHECK(std::filesystem::is_regular_file(Driver.Root() / "levels/Level.dba"));
}

TEST_CASE("A program's scene replaces the level until a level is opened again")
{
    const glm::vec3 Spot(0.0f, 0.0f, 0.0f);
    SceneViewDriver Driver("program_view", LevelOfCubes({Spot}));
    scene::SceneCapture Capture;
    Capture.Status = "completed";
    Capture.Files = {dbc::GenericPathToUtf8(Driver.Root() / "main.dba")};
    Capture.Commands.push_back(
        {dbc::language::SymbolId::Command_MakeObjectCube, {{false, 1.0, {}}, {false, 30.0, {}}}});
    Capture.Sites.push_back({0, 1, 1, 0});
    scene::CapturedObject Object;
    Object.Id = 1;
    Capture.Objects.push_back(Object);
    Capture.HasCamera = true;
    Capture.CameraPosition = glm::vec3(0.0f, 80.0f, -180.0f);
    Capture.CameraRotation = glm::vec3(20.0f, 0.0f, 0.0f);

    const std::filesystem::path Source = Driver.Root() / "main.dba";
    Driver.Editor().OpenCapture(Source, Capture, 1, {});
    Driver.Frame();
    REQUIRE(Driver.Editor().ProgramSource() != nullptr);
    CHECK(*Driver.Editor().ProgramSource() == Source);
    CHECK_FALSE(Driver.Editor().Dirty());

    // Nothing a level would write: the Delete key on the selected object changes nothing.
    Driver.Click(Spot);
    Driver.Tap(ImGuiKey_Delete);
    CHECK_FALSE(Driver.Editor().Dirty());
}

TEST_CASE("The Scene switch shows a level for its own files and the main source, and captures the rest")
{
    SceneViewDriver Driver("show_level_of", LevelOfCubes({glm::vec3(0.0f)}));
    const std::filesystem::path Program = Driver.Root() / "tool.dba";
    Driver.WriteFile("tool.dba", "print 1\n");
    const std::filesystem::path Main = Driver.Root() / "main.dba";
    CHECK_FALSE(Driver.Editor().ShowLevelOf(Program));

    Driver.CreateLevel("Second");
    const std::filesystem::path Level = Driver.Root() / scene::LevelRelativePath("Level");
    const std::filesystem::path Second = Driver.Root() / scene::LevelRelativePath("Second");

    // A level's own file opens it, and the main source keeps the one already open.
    CHECK(Driver.Editor().ShowLevelOf(Level));
    CHECK(Driver.Editor().LevelName() == "Level");
    CHECK(Driver.Editor().ShowLevelOf(Second));
    CHECK(Driver.Editor().LevelName() == "Second");
    CHECK(Driver.Editor().ShowLevelOf(Main));
    CHECK(Driver.Editor().LevelName() == "Second");
    CHECK_FALSE(Driver.Editor().ShowLevelOf(Program));

    // A program's scene gives way to the first level.
    scene::SceneCapture Capture;
    Capture.Status = "completed";
    Driver.Editor().OpenCapture(Program, Capture, 1, {});
    REQUIRE(Driver.Editor().ProgramSource() != nullptr);
    CHECK(Driver.Editor().ShowLevelOf(Main));
    CHECK(Driver.Editor().ProgramSource() == nullptr);
    CHECK(Driver.Editor().LevelName() == "Level");
}

TEST_CASE("The Scene switch captures a main source when the project has no level")
{
    SceneViewDriver Driver("show_level_of_none", LevelOfCubes({}));
    const std::filesystem::path Main = Driver.Root() / "main.dba";
    std::error_code Error;
    std::filesystem::remove_all(Driver.Root() / scene::KLevelFolder, Error);
    CHECK_FALSE(Driver.Editor().ShowLevelOf(Main));
}

TEST_CASE("A gizmo move of a program's node rewrites its statement, and a locked axis stays put")
{
    const glm::vec3 Spot(0.0f, 0.0f, 0.0f);
    const glm::vec3 Far(40.0f, 0.0f, 0.0f);
    SceneViewDriver Driver("program_edit", LevelOfCubes({Spot}));
    const std::filesystem::path Source = Driver.Root() / "program.dba";
    const std::string Text = "make object cube 1, 30\n"
                             "position object 1, 0, 0, 0\n"
                             "make object cube 2, 30\n";
    {
        std::ofstream File(Source, std::ios::binary);
        File << Text;
    }
    scene::SceneCapture Capture;
    Capture.Status = "completed";
    Capture.Files = {dbc::GenericPathToUtf8(Source)};
    using dbc::language::SymbolId;
    Capture.Commands.push_back({SymbolId::Command_MakeObjectCube, {{false, 1.0, {}}, {false, 30.0, {}}}});
    Capture.Sites.push_back({0, 1, 1, 0});
    Capture.Commands.push_back(
        {SymbolId::Command_PositionObject, {{false, 1.0, {}}, {false, 0.0, {}}, {false, 0.0, {}}, {false, 0.0, {}}}});
    Capture.Sites.push_back({0, 2, 1, 0});
    Capture.Commands.push_back({SymbolId::Command_MakeObjectCube, {{false, 2.0, {}}, {false, 30.0, {}}}});
    Capture.Sites.push_back({0, 3, 1, 0});
    scene::CapturedObject First;
    First.Id = 1;
    Capture.Objects.push_back(First);
    // Away from where any statement puts it, so something else moved it along X.
    scene::CapturedObject Second;
    Second.Id = 2;
    Second.Position = Far;
    Capture.Objects.push_back(Second);
    Capture.HasCamera = true;
    Capture.CameraPosition = glm::vec3(0.0f, 80.0f, -180.0f);
    Capture.CameraRotation = glm::vec3(20.0f, 0.0f, 0.0f);
    Driver.Editor().OpenCapture(Source, Capture, 1, {});
    Driver.Frame();

    Driver.Click(Far);
    Driver.DragGizmoX(Far, 120.0f);
    CHECK_FALSE(Driver.Editor().Dirty());

    Driver.Click(Spot);
    Driver.DragGizmoX(Spot, 120.0f);
    CHECK(Driver.Editor().Dirty());
    Driver.Undo();
    CHECK_FALSE(Driver.Editor().Dirty());

    Driver.DragGizmoX(Spot, 120.0f);
    REQUIRE(Driver.Editor().Save());
    CHECK_FALSE(Driver.Editor().Dirty());

    std::ifstream File(Source, std::ios::binary);
    std::ostringstream Saved;
    Saved << File.rdbuf();
    const std::string Patched = Saved.str();
    CHECK(Patched != Text);
    CHECK(Patched.starts_with("make object cube 1, 30\nposition object 1, "));
    CHECK(Patched.ends_with(", 0, 0\nmake object cube 2, 30\n"));

    // The source moved on: a save refuses and keeps the edit, a refresh is the way out.
    const std::string Moved = "sync\n" + Patched;
    {
        std::ofstream Out(Source, std::ios::binary);
        Out << Moved;
    }
    const std::string_view Before = "make object cube 1, 30\nposition object 1, ";
    Driver.DragGizmoX(glm::vec3(std::stof(Patched.substr(Before.size())), 0.0f, 0.0f), 120.0f);
    CHECK(Driver.Editor().Dirty());
    Driver.ExpectErrors(true);
    CHECK_FALSE(Driver.Editor().Save());
    CHECK(Driver.Editor().Dirty());
    Driver.Editor().RefreshProgram();
    Driver.ExpectErrors(false);
    CHECK_FALSE(Driver.Editor().Dirty());
    std::ifstream Kept(Source, std::ios::binary);
    std::ostringstream KeptText;
    KeptText << Kept.rdbuf();
    CHECK(KeptText.str() == Moved);
}

TEST_CASE("On a touch screen a tap picks, the gizmo moves the selection and the view stays put")
{
    const glm::vec3 Start(3.0f, 15.0f, 2.0f);
    SceneViewDriver Driver("touch_gizmo", LevelOfCubes({Start}));
    ImGui::GetIO().AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    const glm::vec3 Eye = Driver.Eye();
    Driver.Click(Start);

    Driver.DragGizmoX(Start, 120.0f);
    CHECK(Driver.SavedPositions().at(0).x > Start.x + 10.0f);
    CHECK(glm::length(Driver.Eye() - Eye) < 0.01f);
}

TEST_CASE("On a touch screen one finger dragged off the gizmo turns the view and moves nothing")
{
    const glm::vec3 Start(0.0f, 15.0f, 0.0f);
    SceneViewDriver Driver("touch_orbit", LevelOfCubes({Start}));
    ImGui::GetIO().AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    const glm::vec3 Ahead = Driver.Ahead();
    const glm::vec2 From = Driver.ScreenOf(glm::vec3(90.0f, 0.0f, 60.0f));

    Driver.MoveMouse(From);
    Driver.Button(true);
    for (int Step = 1; Step <= 4; ++Step)
        Driver.MoveMouse(From + glm::vec2(25.0f * static_cast<float>(Step), 0.0f));
    Driver.Button(false);

    CHECK(glm::dot(Driver.Ahead(), Ahead) < 0.95f);
    CHECK(Driver.SavedPositions().at(0).x == doctest::Approx(Start.x));
}

TEST_CASE("Two fingers on the viewport zoom with their spread and pan with their midpoint")
{
    SceneViewDriver Driver("touch_two_fingers", LevelOfCubes({glm::vec3(0.0f)}));
    const glm::vec2 Centre = Driver.ScreenOf(glm::vec3(0.0f));
    const glm::vec3 Start = Driver.Eye();
    const glm::vec3 Ahead = Driver.Ahead();

    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_DOWN, 1, Centre - glm::vec2(30.0f, 0.0f));
    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_DOWN, 2, Centre + glm::vec2(30.0f, 0.0f));
    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_MOTION, 1, Centre - glm::vec2(60.0f, 0.0f));
    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_MOTION, 2, Centre + glm::vec2(60.0f, 0.0f));
    Driver.Frame();
    // Twice the spread halves the distance to the pivot, along the line of sight.
    const glm::vec3 Zoomed = Driver.Eye();
    CHECK(glm::dot(Zoomed - Start, Ahead) > 10.0f);
    CHECK(glm::dot(Driver.Ahead(), Ahead) > 0.999f);

    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_MOTION, 1, Centre - glm::vec2(20.0f, 0.0f));
    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_MOTION, 2, Centre + glm::vec2(100.0f, 0.0f));
    Driver.Frame();
    const glm::vec3 Panned = Driver.Eye() - Zoomed;
    CHECK(glm::length(Panned) > 1.0f);
    CHECK(std::abs(glm::dot(Panned, Ahead)) < 0.01f * glm::length(Panned) + 0.01f);

    // One finger lifted: the other alone no longer moves the view.
    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_UP, 2, Centre);
    Driver.Editor().HandleFinger(SDL_EVENT_FINGER_MOTION, 1, Centre + glm::vec2(200.0f, 80.0f));
    Driver.Frame();
    CHECK(glm::length(Driver.Eye() - (Zoomed + Panned)) < 0.01f);
}

