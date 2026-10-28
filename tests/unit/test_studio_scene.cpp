#include <doctest/doctest.h>

#include "language/frontend.hpp"
#include "scene/scene_codegen.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_integration.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
        Document.Add(scene::NodeKind::Model).Asset = "models/ship.x";

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
            Cube.Mat.Specular = 0xFFFFFF;
            Cube.Mat.SpecularPower = 32.0f;
            Cube.Mat.Diffuse = 0xC0C0C0;
            Cube.Mat.CastShadow = true;
            Sphere.Detail = glm::ivec2(24, 32);
            Env.ShadowMode = -1;
            Env.ShadowPosition = {100.0f, 300.0f, 0.0f};
            scene::SceneNode& Hills = Document.Add(scene::NodeKind::Terrain);
            Hills.Asset = "media/height.png";
            Hills.Mat.Texture = "media/grass.png";
            Hills.Position = {0.0f, -10.0f, 0.0f};
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

    CHECK_FALSE(scene::ReadLevel("PRINT 1\n", "Test", LanguageProfile::Dbc).Ok());
    CHECK_FALSE(scene::ReadLevel("FUNCTION LoadLevel_Other()\nENDFUNCTION\n", "Test", LanguageProfile::Dbc).Ok());
}

TEST_CASE("Scene creation menus follow the profile's catalogue support")
{
    CHECK(scene::KindAvailable(scene::NodeKind::Cube, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Matrix, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Light, LanguageProfile::Dbc));
    CHECK_FALSE(scene::KindAvailable(scene::NodeKind::Terrain, LanguageProfile::Dbc));
    CHECK(scene::KindAvailable(scene::NodeKind::Terrain, LanguageProfile::DbPro));

    const std::string Terrain = "FUNCTION LoadLevel_Test()\n"
                                "   MAKE TERRAIN 1, \"height.png\"\n"
                                "ENDFUNCTION\n";
    CHECK_FALSE(scene::ReadLevel(Terrain, "Test", LanguageProfile::Dbc).Ok());
    CHECK(scene::ReadLevel(Terrain, "Test", LanguageProfile::DbPro).Ok());
}

TEST_CASE("Scene numbers fill gaps from each space's base")
{
    scene::SceneDocument Document;
    Document.Name = "Gaps";
    CHECK(Document.Add(scene::NodeKind::Cube).Id == 1000);
    CHECK(Document.Add(scene::NodeKind::Sphere).Id == 1001);
    CHECK(Document.Add(scene::NodeKind::Matrix).Id == 1);
    CHECK(Document.Add(scene::NodeKind::Light).Id == 1);
    Document.Nodes.erase(Document.Nodes.begin());
    CHECK(Document.Add(scene::NodeKind::Box).Id == 1000);
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
