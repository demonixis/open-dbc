#include <doctest/doctest.h>

#include "language/frontend.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_integration.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>

using dbc::language::LanguageProfile;

using namespace dbc::studio;

namespace
{
    std::string ReadText(const std::filesystem::path& Path)
    {
        std::ifstream Stream(Path, std::ios::binary);
        REQUIRE(Stream);
        return std::string(std::istreambuf_iterator<char>(Stream), {});
    }

    std::string BindErrors(const std::string& Text, LanguageProfile Profile)
    {
        auto Source = std::make_shared<dbc::language::SourceDocument>();
        Source->LogicalPath = "level.dba";
        Source->Text = Text;
        Source->Profile = Profile;
        const dbc::language::Program Program = dbc::language::Compile(Source);
        std::string Errors;
        for (const auto& Diagnostic : Program.Diagnostics)
            if (Diagnostic.Severity == dbc::language::DiagnosticSeverity::Error)
                Errors += dbc::language::FormatDiagnostic(*Source, Diagnostic) + "\n";
        return Errors;
    }

    scene::SceneDocument FullLevel(LanguageProfile Profile)
    {
        scene::SceneDocument Document;
        Document.Name = "First Level";
        Document.Profile = Profile;

        scene::SceneNode& Cube = Document.Add(scene::NodeKind::Cube);
        Cube.Name = "Crate";
        Cube.Size = 12.5f;
        Cube.Position = {1.0f, -2.5f, 30.0f};
        Cube.Rotation = {0.0f, 45.0f, 0.0f};
        Cube.Scale = {150.0f, 100.0f, 100.0f};
        Cube.Mat.Texture = "media/crate.png";
        Cube.Mat.Color = 0xFF8000;
        Cube.Mat.Flags.Wireframe = true;

        scene::SceneNode& Sphere = Document.Add(scene::NodeKind::Sphere);
        Sphere.Size = 20.0f;
        Sphere.Mat.Texture = "media/crate.png";
        Sphere.Mat.Ghost = true;

        Document.Add(scene::NodeKind::Box).Extent = {10.0f, 20.0f, 30.0f};
        Document.Add(scene::NodeKind::Cylinder).Size = 8.0f;
        Document.Add(scene::NodeKind::Cone).Size = 6.0f;
        Document.Add(scene::NodeKind::Plain).Extent = {50.0f, 25.0f, 0.0f};
        Document.Add(scene::NodeKind::Triangle);
        scene::SceneNode& Ship = Document.Add(scene::NodeKind::Model);
        Ship.Asset = "models/ship.x";
        Ship.Frames = glm::ivec2(2, 10);
        Ship.Loop = false;
        Ship.Speed = 40;
        scene::SceneNode& Walker = Document.Add(scene::NodeKind::Model);
        Walker.Asset = "models/walk.x";
        Walker.Frames = glm::ivec2(0, 20);

        scene::SceneNode& Ground = Document.Add(scene::NodeKind::Matrix);
        Ground.Width = 1000.0f;
        Ground.Depth = 800.0f;
        Ground.XSegments = 20;
        Ground.ZSegments = 16;
        Ground.Mat.Texture = "media/grass.png";
        Ground.TilesAcross = 2;
        Ground.TilesDown = 2;
        Ground.Position = {-500.0f, 0.0f, -400.0f};
        Ground.Heights.assign(scene::MatrixVertexCount(Ground), 5.0f);
        Ground.Heights[0] = 0.0f;
        Ground.Heights[22] = 12.5f;
        Ground.Tile = 3;

        scene::SceneNode& Lamp = Document.Add(scene::NodeKind::Light);
        Lamp.Light = scene::LightKind::Spot;
        Lamp.Position = {0.0f, 100.0f, 0.0f};
        Lamp.Rotation = {90.0f, 0.0f, 0.0f};
        Lamp.Range = 500.0f;
        Lamp.LightColor = 0xFFFFC0;

        scene::SceneNode& Sun = Document.Add(scene::NodeKind::Light);
        Sun.Light = scene::LightKind::Directional;
        Sun.Rotation = {30.0f, -60.0f, 0.0f};

        scene::SceneNode& Hum = Document.Add(scene::NodeKind::Sound);
        Hum.Asset = "sound/hum.wav";
        Hum.Position = {10.0f, 20.0f, 30.0f};
        Hum.Volume = 80;
        scene::SceneNode& Bell = Document.Add(scene::NodeKind::Sound);
        Bell.Asset = "sound/bell.wav";
        Bell.Loop = false;
        scene::SceneNode& Quiet = Document.Add(scene::NodeKind::Sound);
        Quiet.Asset = "sound/quiet.wav";
        Quiet.PlayOnLoad = false;

        scene::Environment& Env = Document.Env;
        Env.AmbientPercent = 40;
        Env.AmbientColor = 0x202040;
        Env.Backdrop = 0x6080FF;
        Env.Fog = true;
        Env.FogColor = 0x808080;
        Env.FogDistance = 2500.0f;
        Env.DefaultLightRotation = glm::vec3(45.0f, 30.0f, 0.0f);
        Env.DefaultLightColor = 0xFFFFFF;
        Env.UseCamera = true;
        Env.CameraPosition = {0.0f, 80.0f, -200.0f};
        Env.CameraRotation = {15.0f, 0.0f, 0.0f};
        Env.CameraFov = 60.0f;
        Env.CameraRange = glm::vec2(1.0f, 10000.0f);

        if (Profile == LanguageProfile::DbPro)
        {
            // Add reallocates the nodes, so the references taken above no longer hold.
            scene::SceneNode& Crate = Document.Nodes[0];
            Crate.Mat.Specular = 0xFFFFFF;
            Crate.Mat.SpecularPower = 32.0f;
            Crate.Mat.Diffuse = 0xC0C0C0;
            Crate.Mat.CastShadow = true;
            Document.Nodes[1].Detail = glm::ivec2(24, 32);
            Env.ShadowMode = -1;
            Env.ShadowPosition = {100.0f, 300.0f, 0.0f};
            scene::SceneNode& Hills = Document.Add(scene::NodeKind::Terrain);
            Hills.Asset = "media/height.png";
            Hills.Mat.Texture = "media/grass.png";
            Hills.Position = {0.0f, -10.0f, 0.0f};
        }
        else
        {
            scene::SceneNode& Rock = Document.Add(scene::NodeKind::Box);
            Rock.Extent = {20.0f, 10.0f, 20.0f};
            Rock.Static = true;
        }
        return Document;
    }
} // namespace

TEST_CASE("Scene levels round-trip through the generated BASIC in both profiles")
{
    for (const LanguageProfile Profile : {LanguageProfile::Dbc, LanguageProfile::DbPro})
    {
        CAPTURE(dbc::language::ProfileName(Profile));
        const scene::SceneDocument Document = FullLevel(Profile);
        const std::string Text = scene::WriteLevel(Document);
        CHECK(BindErrors(Text, Profile).empty());

        const scene::LevelReadResult Read = scene::ReadLevel(Text, Document.Name, Profile);
        for (const scene::ReadError& Error : Read.Errors)
            FAIL_CHECK(Error.Line << ": " << Error.Message);
        CHECK(Read.Document == Document);
        CHECK(scene::WriteLevel(Read.Document) == Text);
    }
}

namespace
{
    // A model, then painted copies of it: `Copies` of them, each 10 units further along X.
    scene::SceneDocument PaintedLevel(LanguageProfile Profile, int Copies)
    {
        scene::SceneDocument Document;
        Document.Name = "Painted";
        Document.Profile = Profile;
        scene::SceneNode& Tree = Document.Add(scene::NodeKind::Model);
        Tree.Name = "tree";
        Tree.Asset = "models/tree.x";
        Tree.Mat.Texture = "media/bark.png";
        Tree.Mat.Color = 0x80FF80;
        Tree.Position = {5.0f, 1.0f, 0.0f};
        Tree.Scale = {200.0f, 200.0f, 200.0f};
        for (int Index = 1; Index <= Copies; ++Index)
        {
            scene::SceneNode Copy = Document.Nodes.front();
            Copy.Id = Document.NextId(scene::IdSpace::Object);
            Copy.Name = scene::PaintedName("tree", Copy.Id);
            Copy.Painted = true;
            Copy.Position.x += 10.0f * static_cast<float>(Index);
            Copy.Rotation.y = 30.0f * static_cast<float>(Index);
            Document.Nodes.push_back(std::move(Copy));
        }
        return Document;
    }

    std::size_t CountOf(const std::string& Text, const std::string& Part)
    {
        std::size_t Count = 0;
        for (std::size_t At = Text.find(Part); At != std::string::npos; At = Text.find(Part, At + Part.size()))
            ++Count;
        return Count;
    }
} // namespace

TEST_CASE("Pro writes painted objects as instances of the first that looks the same")
{
    const scene::SceneDocument Document = PaintedLevel(LanguageProfile::DbPro, 3);
    const std::string Text = scene::WriteLevel(Document);
    CHECK(BindErrors(Text, LanguageProfile::DbPro).empty());
    CHECK(CountOf(Text, "LOAD OBJECT") == 1);
    CHECK(CountOf(Text, "INSTANCE OBJECT") == 3);
    CHECK(Text.find("INSTANCE OBJECT 1001, 1000") != std::string::npos);
    CHECK(Text.find("REM @object 1001 \"tree_1001\" painted") != std::string::npos);
    // The copy starts as the original: only what differs is written.
    CHECK(Text.find("POSITION OBJECT 1001, 15, 1, 0") != std::string::npos);
    CHECK(Text.find("ROTATE OBJECT 1001, 0, 30, 0") != std::string::npos);
    CHECK(Text.find("SCALE OBJECT 1001") == std::string::npos);
    CHECK(Text.find("TEXTURE OBJECT 1001") == std::string::npos);
    CHECK(CountOf(Text, "LOAD IMAGE") == 1);

    const scene::LevelReadResult Read = scene::ReadLevel(Text, Document.Name, LanguageProfile::DbPro);
    for (const scene::ReadError& Error : Read.Errors)
        FAIL_CHECK(Error.Line << ": " << Error.Message);
    CHECK(Read.Document == Document);
    CHECK(scene::WriteLevel(Read.Document) == Text);
}

TEST_CASE("Classic has no INSTANCE OBJECT, so painted objects are written in full")
{
    const scene::SceneDocument Document = PaintedLevel(LanguageProfile::Dbc, 2);
    const std::string Text = scene::WriteLevel(Document);
    CHECK(BindErrors(Text, LanguageProfile::Dbc).empty());
    CHECK(CountOf(Text, "INSTANCE OBJECT") == 0);
    CHECK(CountOf(Text, "LOAD OBJECT") == 3);
    CHECK(CountOf(Text, "TEXTURE OBJECT") == 3);
    const scene::LevelReadResult Read = scene::ReadLevel(Text, Document.Name, LanguageProfile::Dbc);
    for (const scene::ReadError& Error : Read.Errors)
        FAIL_CHECK(Error.Line << ": " << Error.Message);
    CHECK(Read.Document == Document);

    // A Pro level's instances stop a Classic project from editing it, as any Pro command does.
    const std::string Pro = scene::WriteLevel(PaintedLevel(LanguageProfile::DbPro, 1));
    CHECK_FALSE(scene::ReadLevel(Pro, Document.Name, LanguageProfile::Dbc).Ok());
}

TEST_CASE("A painted object is written in full once nothing earlier looks the same")
{
    scene::SceneDocument Document = PaintedLevel(LanguageProfile::DbPro, 3);

    // Without the original, the first copy takes its place.
    Document.Nodes.erase(Document.Nodes.begin());
    std::string Text = scene::WriteLevel(Document);
    CHECK(CountOf(Text, "LOAD OBJECT") == 1);
    CHECK(Text.find("LOAD OBJECT \"models/tree.x\", 1001") != std::string::npos);
    CHECK(Text.find("INSTANCE OBJECT 1002, 1001") != std::string::npos);
    scene::LevelReadResult Read = scene::ReadLevel(Text, Document.Name, LanguageProfile::DbPro);
    CHECK(Read.Ok());
    CHECK(Read.Document == Document);

    // Retextured, a copy no longer looks the same, and it needs its own image.
    Document.Nodes[2].Mat.Texture = "media/birch.png";
    Text = scene::WriteLevel(Document);
    CHECK(CountOf(Text, "LOAD OBJECT") == 2);
    CHECK(CountOf(Text, "INSTANCE OBJECT") == 1);
    CHECK(CountOf(Text, "LOAD IMAGE") == 2);
    Read = scene::ReadLevel(Text, Document.Name, LanguageProfile::DbPro);
    CHECK(Read.Ok());
    CHECK(Read.Document == Document);
}

TEST_CASE("A painted copy keeps its own animation, which INSTANCE OBJECT does not start")
{
    scene::SceneDocument Document = PaintedLevel(LanguageProfile::DbPro, 1);
    Document.Nodes[0].Frames = glm::ivec2(1, 20);
    Document.Nodes[0].Speed = 40;
    Document.Nodes[1].Frames = glm::ivec2(5, 10);
    Document.Nodes[1].Loop = false;
    const std::string Text = scene::WriteLevel(Document);
    CHECK(CountOf(Text, "INSTANCE OBJECT") == 1);
    CHECK(Text.find("PLAY OBJECT 1001, 5, 10") != std::string::npos);
    const scene::LevelReadResult Read = scene::ReadLevel(Text, Document.Name, LanguageProfile::DbPro);
    for (const scene::ReadError& Error : Read.Errors)
        FAIL_CHECK(Error.Line << ": " << Error.Message);
    CHECK(Read.Document == Document);
}

TEST_CASE("A level of painted objects reads every instance back")
{
    const scene::SceneDocument Document = PaintedLevel(LanguageProfile::DbPro, 3000);
    const std::string Text = scene::WriteLevel(Document);
    const scene::LevelReadResult Read = scene::ReadLevel(Text, Document.Name, LanguageProfile::DbPro);
    CHECK(Read.Ok());
    REQUIRE(Read.Document.Nodes.size() == 3001);
    CHECK(Read.Document == Document);
}

TEST_CASE("Default node names do not depend on the interface")
{
    CHECK(scene::DefaultNodeName(scene::NodeKind::Cube, 12) == "Cube 12");
    CHECK(scene::DefaultNodeName(scene::NodeKind::Sound, 3) == "3D Sound 3");

    scene::SceneDocument Document;
    const scene::SceneNode& Light = Document.Add(scene::NodeKind::Light);
    CHECK(Light.Name == scene::DefaultNodeName(scene::NodeKind::Light, Light.Id));

    const scene::LevelReadResult Read = scene::ReadLevel(scene::WriteLevel(Document), Document.Name, Document.Profile);
    REQUIRE(Read.Ok());
    REQUIRE(Read.Document.Nodes.size() == 1);
    CHECK(Read.Document.Nodes.front().Name == Light.Name);
}

TEST_CASE("Free numbers skip those in use, and painted names keep their stem")
{
    scene::SceneDocument Document;
    Document.Add(scene::NodeKind::Cube);
    Document.Add(scene::NodeKind::Cube).Id = 1002;
    Document.Add(scene::NodeKind::Sphere).Id = 1004;
    CHECK(Document.NextIds(scene::IdSpace::Object, 3) == std::vector<int>{1001, 1003, 1005});
    CHECK(Document.NextIds(scene::IdSpace::Light, 2) == std::vector<int>{1, 2});
    CHECK(Document.NextIds(scene::IdSpace::Object, 0).empty());

    CHECK(scene::PaintedName("tree", 12) == "tree_12");
    CHECK(scene::PaintedName("tree_3", 12) == "tree_12");
    CHECK(scene::PaintedName("tree_big", 12) == "tree_big_12");
    CHECK(scene::PaintedStem("tree_12") == "tree");
    CHECK(scene::PaintedStem("tree") == "tree");
    // CopyName shares the suffix rule.
    Document.Nodes[0].Name = "tree_7";
    CHECK(Document.CopyName("tree") == "tree_8");
}

TEST_CASE("Two objects look the same whatever each sets for itself")
{
    const scene::SceneDocument Document = PaintedLevel(LanguageProfile::DbPro, 1);
    scene::SceneNode Copy = Document.Nodes[1];
    Copy.Frames = glm::ivec2(1, 2);
    Copy.Speed = 3;
    Copy.Loop = false;
    CHECK(scene::SameLook(Document.Nodes[0], Copy));
    Copy.Mat.Color = 0x123456;
    CHECK_FALSE(scene::SameLook(Document.Nodes[0], Copy));
    Copy = Document.Nodes[1];
    Copy.Asset = "models/rock.x";
    CHECK_FALSE(scene::SameLook(Document.Nodes[0], Copy));
}

TEST_CASE("Scene history forgets its oldest states when they hold too many nodes")
{
    // 1000 nodes a state, against a budget of 500 000: the first states go, undo still works.
    scene::SceneDocument Document = PaintedLevel(LanguageProfile::Dbc, 999);
    scene::SceneHistory History;
    History.Reset(Document);
    constexpr int KStates = 600;
    for (int Index = 0; Index < KStates; ++Index)
    {
        Document.Nodes[0].Position.x = static_cast<float>(Index);
        History.Commit(Document);
    }
    int Steps = 0;
    while (History.CanUndo())
    {
        History.Undo();
        ++Steps;
    }
    CHECK(Steps == 499);
    CHECK(History.CanRedo());

    // A single huge state is never dropped.
    scene::SceneHistory Huge;
    scene::SceneDocument Big = PaintedLevel(LanguageProfile::Dbc, 2000);
    Huge.Reset(Big);
    CHECK_FALSE(Huge.CanUndo());
}

TEST_CASE("The BASIC goldens run the generator's exact output")
{
    const std::filesystem::path Root = std::filesystem::path(TEST_SOURCE_DIR) / "tests/dba/scene";
    const std::pair<const char*, LanguageProfile> Cases[] = {{"classic", LanguageProfile::Dbc},
                                                             {"pro", LanguageProfile::DbPro}};
    for (const auto& [Folder, Profile] : Cases)
    {
        CAPTURE(Folder);
        const std::string Level = ReadText(Root / Folder / "levels/Golden.dba");
        const scene::LevelReadResult Read = scene::ReadLevel(Level, "Golden", Profile);
        for (const scene::ReadError& Error : Read.Errors)
            FAIL_CHECK(Error.Line << ": " << Error.Message);
        CHECK(scene::WriteLevel(Read.Document) == Level);

        const std::string Game = ReadText(Root / Folder / scene::KGameFileName);
        const scene::GameReadResult GameRead = scene::ReadGame(Game, Profile);
        CHECK(GameRead.Ok());
        CHECK(scene::WriteGame(GameRead.Settings, Profile) == Game);
    }
}

TEST_CASE("Scene levels share one image per texture and use the reserved numbers")
{
    scene::SceneDocument Document = FullLevel(LanguageProfile::Dbc);
    Document.Base(scene::IdSpace::Image) = 500;
    const std::string Text = scene::WriteLevel(Document);
    CHECK(Text.find("FUNCTION LoadLevel_First_Level()") != std::string::npos);
    CHECK(Text.find("LOAD IMAGE \"media/crate.png\", 500") != std::string::npos);
    CHECK(Text.find("LOAD IMAGE \"media/grass.png\", 501") != std::string::npos);
    CHECK(Text.find("LOAD IMAGE \"media/crate.png\", 502") == std::string::npos);
    CHECK(Text.find("MAKE OBJECT CUBE 1000, 12.5") != std::string::npos);
    CHECK(Text.find("COLOR OBJECT 1000, RGB(255, 128, 0)") != std::string::npos);
    // Wireframe is SET OBJECT's "solid" flag at 0; Classic writes 1 for opaque black.
    CHECK(Text.find("SET OBJECT 1000, 0, 1, 1, 1, 1, 1, 1") != std::string::npos);
    CHECK(Text.find("AUTOCAM OFF") < Text.find("MAKE OBJECT CUBE"));
    CHECK(Text.find("POSITION CAMERA") > Text.find("MAKE MATRIX"));
    // Matrix heights: the most common one fills, the others follow, then the update.
    CHECK(Text.find("FILL MATRIX 1, 5, 3\n") != std::string::npos);
    CHECK(Text.find("SET MATRIX HEIGHT 1, 0, 0, 0\n") != std::string::npos);
    CHECK(Text.find("SET MATRIX HEIGHT 1, 1, 1, 12.5\n") != std::string::npos);
    CHECK(Text.find("UPDATE MATRIX 1\n") > Text.find("SET MATRIX HEIGHT 1, 1, 1"));
    CHECK(Text.find("SET OBJECT SPECULAR") == std::string::npos);
}

TEST_CASE("Scene levels refuse statements they did not write")
{
    const std::string Edited = "FUNCTION LoadLevel_Test()\n"
                               "   MAKE OBJECT CUBE 1000, 10\n"
                               "   x = 5\n"
                               "   POSITION OBJECT 1000, x, 0, 0\n"
                               "   POSITION OBJECT 1001, 0, 0, 0\n"
                               "ENDFUNCTION\n";
    const scene::LevelReadResult Read = scene::ReadLevel(Edited, "Test", LanguageProfile::Dbc);
    REQUIRE(Read.Errors.size() == 3);
    CHECK(Read.Errors[0].Line == 3);
    CHECK(Read.Errors[1].Line == 4);
    CHECK(Read.Errors[2].Line == 5);

    const std::string Animated = "FUNCTION LoadLevel_Test()\n"
                                 "   LOAD OBJECT \"walk.x\", 1000\n"
                                 "   LOOP OBJECT 1000\n"
                                 "ENDFUNCTION\n";
    const scene::LevelReadResult Whole = scene::ReadLevel(Animated, "Test", LanguageProfile::Dbc);
    REQUIRE(Whole.Errors.size() == 1);
    CHECK(Whole.Errors[0].Line == 3);

    CHECK_FALSE(scene::ReadLevel("PRINT 1\n", "Test", LanguageProfile::Dbc).Ok());
    CHECK_FALSE(scene::ReadLevel("FUNCTION LoadLevel_Other()\nENDFUNCTION\n", "Test", LanguageProfile::Dbc).Ok());
}

TEST_CASE("Scene creation menus follow the profile's catalogue support")
{
    CHECK(scene::KindAvailable(scene::NodeKind::Cube, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Matrix, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Light, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Sound, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Sound, LanguageProfile::DbPro));
    CHECK_FALSE(scene::KindAvailable(scene::NodeKind::Terrain, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Terrain, LanguageProfile::DbPro));

    const std::string Terrain = "FUNCTION LoadLevel_Test()\n"
                                "   MAKE TERRAIN 1, \"height.png\"\n"
                                "ENDFUNCTION\n";
    CHECK_FALSE(scene::ReadLevel(Terrain, "Test", LanguageProfile::Dbc).Ok());
    CHECK(scene::ReadLevel(Terrain, "Test", LanguageProfile::DbPro).Ok());
}

TEST_CASE("Static objects are Classic scenery made from a deleted object")
{
    CHECK(scene::StaticAvailable(LanguageProfile::Dbc));
    CHECK_FALSE(scene::StaticAvailable(LanguageProfile::DbPro));

    scene::SceneDocument Document;
    Document.Name = "Test";
    Document.Profile = LanguageProfile::DbPro;
    Document.Add(scene::NodeKind::Cube).Static = true;
    CHECK(scene::WriteLevel(Document).find("STATIC") == std::string::npos);

    const auto Read = [](const char* Body)
    {
        return scene::ReadLevel(std::string("FUNCTION LoadLevel_Test()\n   MAKE OBJECT CUBE 1000, 10\n") + Body +
                                    "ENDFUNCTION\n",
                                "Test", LanguageProfile::Dbc);
    };
    CHECK(Read("   MAKE STATIC OBJECT 1000\n   DELETE OBJECT 1000\n").Ok());
    CHECK_FALSE(Read("   MAKE STATIC OBJECT 1000\n").Ok());
    CHECK_FALSE(Read("   DELETE OBJECT 1000\n").Ok());
    CHECK_FALSE(Read("   MAKE STATIC OBJECT 1000, 1\n   DELETE OBJECT 1000\n").Ok());
    const std::string Pro = "FUNCTION LoadLevel_Test()\n"
                            "   MAKE OBJECT CUBE 1000, 10\n"
                            "   MAKE STATIC OBJECT 1000\n"
                            "ENDFUNCTION\n";
    CHECK_FALSE(scene::ReadLevel(Pro, "Test", LanguageProfile::DbPro).Ok());
}

TEST_CASE("Scene numbers fill gaps from each space's base")
{
    scene::SceneDocument Document;
    Document.Name = "Gaps";
    CHECK(Document.Add(scene::NodeKind::Cube).Id == 1000);
    CHECK(Document.Add(scene::NodeKind::Sphere).Id == 1001);
    CHECK(Document.Add(scene::NodeKind::Matrix).Id == 1);
    CHECK(Document.Add(scene::NodeKind::Light).Id == 1);
    CHECK(Document.Add(scene::NodeKind::Sound).Id == 1);
    Document.Nodes.erase(Document.Nodes.begin());
    CHECK(Document.Add(scene::NodeKind::Box).Id == 1000);
    Document.Add(scene::NodeKind::Cube).Name = "tree";
    CHECK(Document.CopyName("tree") == "tree_1");
    Document.Add(scene::NodeKind::Cube).Name = "tree_3";
    CHECK(Document.CopyName("tree") == "tree_4");
    CHECK(Document.CopyName("tree_3") == "tree_4");
    CHECK(Document.CopyName("tree_top") == "tree_top_1");
    CHECK(Document.CopyName("tree_") == "tree__1");
    CHECK(scene::SanitizeLevelName("2 caves: deep!") == "Level2_caves_deep");
    CHECK(scene::LevelRelativePath("Main Hall").generic_string() == "levels/Main_Hall.dba");
}

TEST_CASE("Scene history undoes and redoes whole snapshots")
{
    scene::SceneDocument Document;
    scene::SceneHistory History;
    History.Reset(Document);
    CHECK_FALSE(History.CanUndo());
    Document.Add(scene::NodeKind::Cube);
    History.Commit(Document);
    History.Commit(Document);
    Document.Nodes.front().Size = 20.0f;
    History.Commit(Document);
    CHECK(History.Undo().Nodes.front().Size == 10.0f);
    CHECK(History.Undo().Nodes.empty());
    CHECK_FALSE(History.CanUndo());
    CHECK(History.Redo().Nodes.size() == 1);
    Document.Nodes.clear();
    History.Commit(Document);
    CHECK_FALSE(History.CanRedo());
}

TEST_CASE("Game settings round-trip and keep Pro-only forms out of Classic")
{
    scene::GameSettings Settings;
    Settings.DisplayMode = glm::ivec3(1280, 720, 32);
    Settings.Vsync = 1;
    Settings.Windowed = true;
    Settings.WindowSize = glm::ivec2(1280, 720);
    Settings.WindowTitle = "My Game";
    Settings.SyncOn = true;
    Settings.SyncRate = 60;
    Settings.HideMouse = true;

    const std::string Pro = scene::WriteGame(Settings, LanguageProfile::DbPro);
    CHECK(BindErrors(Pro, LanguageProfile::DbPro).empty());
    CHECK(Pro.find("SET DISPLAY MODE 1280, 720, 32, 1") != std::string::npos);
    const scene::GameReadResult ProRead = scene::ReadGame(Pro, LanguageProfile::DbPro);
    CHECK(ProRead.Ok());
    CHECK(ProRead.Settings == Settings);

    const std::string Classic = scene::WriteGame(Settings, LanguageProfile::Dbc);
    CHECK(BindErrors(Classic, LanguageProfile::Dbc).empty());
    CHECK(Classic.find("SET DISPLAY MODE 1280, 720, 32\n") != std::string::npos);
    const scene::GameReadResult ClassicRead = scene::ReadGame(Classic, LanguageProfile::Dbc);
    CHECK(ClassicRead.Ok());
    CHECK_FALSE(ClassicRead.Settings.Vsync.has_value());
}

TEST_CASE("Main sources get their includes on top, then the setup and level calls")
{
    const std::vector<scene::MainLink> Links = {{"opendbc_game.dba", "SetupGame"},
                                                {"levels/First.dba", "LoadLevel_First"}};
    const std::string Main = "REM My game\r\n\r\nREM Display\r\nsync on\r\ndo\r\n   sync\r\nloop\r\n";
    const scene::LinkResult Linked = scene::LinkIntoMain(Main, LanguageProfile::Dbc, Links);
    CHECK(Linked.Changed);
    CHECK(Linked.Text == "REM My game\r\n\r\n#include \"opendbc_game.dba\"\r\n#include \"levels/First.dba\"\r\n\r\n"
                         "SetupGame()\r\nLoadLevel_First()\r\n\r\nREM Display\r\nsync on\r\ndo\r\n   sync\r\nloop\r\n");
    CHECK_FALSE(scene::LinkIntoMain(Linked.Text, LanguageProfile::Dbc, Links).Changed);

    // A second level joins the include block, and its call follows the first level's.
    const std::vector<scene::MainLink> Third = {{"opendbc_game.dba", "SetupGame"},
                                                {"levels/First.dba", "LoadLevel_First"},
                                                {"levels/Cave.dba", "LoadLevel_Cave"}};
    CHECK(scene::LinkIntoMain(Linked.Text, LanguageProfile::Dbc, Third).Text ==
          "REM My game\r\n\r\n#include \"opendbc_game.dba\"\r\n#include \"levels/First.dba\"\r\n"
          "#include \"levels/Cave.dba\"\r\n\r\nSetupGame()\r\nLoadLevel_First()\r\nLoadLevel_Cave()\r\n\r\n"
          "REM Display\r\nsync on\r\ndo\r\n   sync\r\nloop\r\n");

    // A call the user moved is found where it is; a new level's call follows SetupGame().
    const std::string Moved = "#include \"opendbc_game.dba\"\nprint 1\nSetupGame()\nwait key\n";
    const std::vector<scene::MainLink> Second = {{"opendbc_game.dba", "SetupGame"},
                                                 {"levels\\Cave.dba", "LoadLevel_Cave"}};
    const scene::LinkResult Added = scene::LinkIntoMain(Moved, LanguageProfile::DbPro, Second);
    CHECK(Added.Text == "#include \"opendbc_game.dba\"\n#include \"levels\\Cave.dba\"\nprint 1\nSetupGame()\n"
                        "LoadLevel_Cave()\nwait key\n");

    // An empty file gets the includes and the calls only.
    CHECK(scene::LinkIntoMain("", LanguageProfile::Dbc, Links).Text ==
          "#include \"opendbc_game.dba\"\n#include \"levels/First.dba\"\n\nSetupGame()\nLoadLevel_First()\n");
}

TEST_CASE("Multi-selection edits copy only the changed fields each node has")
{
    scene::SceneDocument Document;
    scene::SceneNode& Active = Document.Add(scene::NodeKind::Cube);
    Active.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    scene::SceneNode Cube = Document.Add(scene::NodeKind::Cube);
    Cube.Position = glm::vec3(10.0f, 20.0f, 30.0f);
    Cube.Size = 4.0f;
    Cube.Name = "Other";
    scene::SceneNode Light = Document.Add(scene::NodeKind::Light);
    Light.Position = glm::vec3(-5.0f, 50.0f, 0.0f);
    scene::SceneNode Matrix = Document.Add(scene::NodeKind::Matrix);

    const scene::SceneNode Before = Document.Nodes.front();
    scene::SceneNode After = Before;
    After.Position.y = 7.0f;
    After.Rotation.x = 45.0f;
    After.Scale = glm::vec3(200.0f);
    After.Size = 25.0f;
    After.Mat.Texture = "bmp/stone.bmp";
    After.Mat.Flags.Wireframe = true;
    After.Name = "Renamed";

    scene::ApplySharedEdit(Before, After, Cube);
    // Typing Y sets only Y; unchanged fields keep each node's own value.
    CHECK(Cube.Position == glm::vec3(10.0f, 7.0f, 30.0f));
    CHECK(Cube.Rotation == glm::vec3(45.0f, 0.0f, 0.0f));
    CHECK(Cube.Scale == glm::vec3(200.0f));
    CHECK(Cube.Size == 25.0f);
    CHECK(Cube.Mat.Texture == "bmp/stone.bmp");
    CHECK(Cube.Mat.Flags.Wireframe);
    CHECK(Cube.Name == "Other");

    const scene::SceneNode LightBefore = Light;
    scene::ApplySharedEdit(Before, After, Light);
    CHECK(Light.Position == glm::vec3(-5.0f, 7.0f, 0.0f));
    CHECK(Light.Rotation == glm::vec3(45.0f, 0.0f, 0.0f));
    // A light has no scale, size, texture or object flags.
    CHECK(Light.Scale == LightBefore.Scale);
    CHECK(Light.Size == LightBefore.Size);
    CHECK(Light.Mat == LightBefore.Mat);

    const scene::SceneNode MatrixBefore = Matrix;
    scene::ApplySharedEdit(Before, After, Matrix);
    // Matrices only move, take a texture, and keep their object flags untouched.
    CHECK(Matrix.Rotation == MatrixBefore.Rotation);
    CHECK(Matrix.Scale == MatrixBefore.Scale);
    CHECK(Matrix.Mat.Texture == "bmp/stone.bmp");
    CHECK(Matrix.Mat.Flags == MatrixBefore.Mat.Flags);
}

TEST_CASE("Includes are followed through the files that hold them")
{
    const std::map<std::filesystem::path, std::string> Files = {
        {"/game/main.dba", "print 1\n#include \"lib\\world.dba\"\n"},
        {"/game/lib/world.dba", "#include \"props.dba\"\n#include \"../main.dba\"\n"},
        {"/game/lib/props.dba", "make object cube 1, 10\n"},
        {"/game/other.dba", "print 2\n"},
    };
    const auto Read = [&](const std::filesystem::path& Path) -> std::optional<std::string>
    {
        const auto Found = Files.find(Path);
        return Found == Files.end() ? std::nullopt : std::optional(Found->second);
    };
    const auto Includes = [&](const char* Target)
    { return scene::SourceIncludes("/game/main.dba", Target, LanguageProfile::Dbc, Read); };
    CHECK(Includes("/game/lib/world.dba"));
    CHECK(Includes("/game/lib/props.dba"));
    CHECK_FALSE(Includes("/game/other.dba"));
    CHECK_FALSE(Includes("/game/lib/missing.dba"));
}
