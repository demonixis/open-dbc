#include <doctest/doctest.h>

#include "interpreter/command_line.hpp"
#include "language/language_catalogue.hpp"
#include "path_utf8.hpp"
#include "scene/scene_capture.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

using namespace dbc::studio::scene;
using dbc::language::SymbolId;

namespace
{
    namespace fs = std::filesystem;

    // Runs a program as the Studio does and reads back what it built.
    class Captured
    {
      public:
        explicit Captured(std::string_view Name)
        {
            std::error_code Ignored;
            Root_ = fs::temp_directory_path(Ignored) / ("opendbc_capture_" + std::string(Name));
            fs::remove_all(Root_, Ignored);
            fs::create_directories(Root_, Ignored);
        }

        ~Captured()
        {
            std::error_code Ignored;
            fs::remove_all(Root_, Ignored);
        }

        fs::path Write(const std::string& Name, const std::string& Text) const
        {
            std::ofstream Out(Root_ / Name, std::ios::binary);
            Out << Text;
            return Root_ / Name;
        }

        CaptureReadResult Run(const fs::path& Source, int SetupFrames = 1, const std::string& OnlyFile = {})
        {
            const std::string SourcePath = Source.string();
            const std::string CapturePath = (Root_ / "capture.json").string();
            const std::string Frames = std::to_string(SetupFrames + 1);
            const std::array<std::string_view, 11> Arguments{
                "run",          SourcePath, "--dialect",       "dbc",       "--headless",
                "--max-frames", Frames,     "--capture-scene", CapturePath, "--instruction-budget",
                "2000000"};
            dbc::interpreter::CommandLineEnvironment Environment;
            Environment.StandardOutput = [](std::string_view) {};
            dbc::interpreter::DispatchCommandLine(Arguments, Environment);

            std::ifstream In(CapturePath, std::ios::binary);
            const std::string Text((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
            std::string Error;
            Capture = {};
            REQUIRE(ParseSceneCapture(Text, Root_, Root_, Capture, Error));
            return ReadCapture(Capture, {"test", SetupFrames, OnlyFile});
        }

        fs::path Root() const
        {
            return Root_;
        }

        SceneCapture Capture;

      private:
        fs::path Root_;
    };

    const AxisOrigin& Axis(const CaptureReadResult& Read, int Id, TransformPart Part, std::size_t Index)
    {
        return Read.Origins.at({IdSpace::Object, Id}).Axes[static_cast<std::size_t>(Part)][Index];
    }
} // namespace

TEST_CASE("a capture rebuilds the scene of a program no level could express")
{
    Captured Program("rebuild");
    const fs::path Source = Program.Write("main.dba", "sync on\n"
                                                      "for i = 1 to 3\n"
                                                      "  make object cube i, 5 + i\n"
                                                      "  position object i, i * 10, 0, 0\n"
                                                      "next i\n"
                                                      "point object 1, 0, 0, 100\n"
                                                      "delete object 2\n"
                                                      "hide object 3\n"
                                                      "make object sphere 2, 8\n"
                                                      "make matrix 1, 100, 200, 4, 2\n"
                                                      "set matrix height 1, 1, 1, 7\n"
                                                      "position matrix 1, 5, 0, 5\n"
                                                      "make light 1\n"
                                                      "position camera 0, 30, -60\n"
                                                      "do\n"
                                                      "  sync\n"
                                                      "loop\n");
    const CaptureReadResult Read = Program.Run(Source);
    CHECK(Program.Capture.Status == "completed");
    const SceneDocument& Document = Read.Document;

    const SceneNode* First = Document.Find(IdSpace::Object, 1);
    REQUIRE(First != nullptr);
    CHECK(First->Kind == NodeKind::Cube);
    CHECK(First->Size == doctest::Approx(6.0f));
    CHECK(First->Position.x == doctest::Approx(10.0f));
    // POINT OBJECT has no statement in a level: the final state knows its result.
    CHECK(First->Rotation != glm::vec3(0.0f));

    // The number was deleted, then reused.
    const SceneNode* Second = Document.Find(IdSpace::Object, 2);
    REQUIRE(Second != nullptr);
    CHECK(Second->Kind == NodeKind::Sphere);
    CHECK(Second->Position.x == doctest::Approx(0.0f));

    const SceneNode* Third = Document.Find(IdSpace::Object, 3);
    REQUIRE(Third != nullptr);
    CHECK(Third->Hidden);

    const SceneNode* Ground = Document.Find(IdSpace::Matrix, 1);
    REQUIRE(Ground != nullptr);
    CHECK(Ground->Width == doctest::Approx(100.0f));
    CHECK(Ground->XSegments == 4);
    CHECK(Ground->Position.x == doctest::Approx(5.0f));
    REQUIRE(Ground->Heights.size() == MatrixVertexCount(*Ground));
    CHECK(Ground->Heights[1 * 5 + 1] == doctest::Approx(7.0f));
    CHECK(Ground->Tiles.size() == 8);

    CHECK(Document.Find(IdSpace::Light, 1) != nullptr);
    CHECK(Document.Env.UseCamera);
    CHECK(Document.Env.CameraPosition.y == doctest::Approx(30.0f));
}

TEST_CASE("a capture says which statement decides each axis")
{
    Captured Program("origins");
    const fs::path Source = Program.Write("main.dba", "sync on\n"
                                                      "make object cube 1, 10\n"
                                                      "position object 1, 1, 2, 3\n"
                                                      "yrotate object 1, 45\n"
                                                      "for i = 2 to 3\n"
                                                      "  make object cube i, 10\n"
                                                      "  position object i, i, 0, 0\n"
                                                      "next i\n"
                                                      "make object cube 4, 10\n"
                                                      "move object 4, 5\n"
                                                      "make object cube 5, 10\n"
                                                      "do\n"
                                                      "  position object 5, 9, 9, 9\n"
                                                      "  sync\n"
                                                      "loop\n");
    const CaptureReadResult Read = Program.Run(Source);
    const std::string File = dbc::GenericPathToUtf8(Source.lexically_normal());

    const AxisOrigin& Moved = Axis(Read, 1, TransformPart::Position, 1);
    CHECK(Moved.Access == AxisAccess::Replace);
    CHECK(Moved.Writer.File == File);
    CHECK(Moved.Writer.Line == 3);
    CHECK(Moved.Argument == 2);

    // One axis has its own statement; the others have none and can gain one.
    const AxisOrigin& Turned = Axis(Read, 1, TransformPart::Rotation, 1);
    CHECK(Turned.Access == AxisAccess::Replace);
    CHECK(Turned.Writer.Line == 4);
    CHECK(Turned.Argument == 1);
    CHECK(Axis(Read, 1, TransformPart::Rotation, 0).Access == AxisAccess::Insert);
    CHECK(Axis(Read, 1, TransformPart::Scale, 2).Access == AxisAccess::Insert);

    // Each loop statement places several objects, so it belongs to none of them.
    CHECK(Axis(Read, 2, TransformPart::Position, 0).Access == AxisAccess::Locked);
    CHECK(Axis(Read, 2, TransformPart::Rotation, 0).Access == AxisAccess::Locked);

    // MOVE OBJECT is not a statement the editor can rewrite.
    CHECK(Axis(Read, 4, TransformPart::Position, 2).Access == AxisAccess::Locked);
    CHECK(Axis(Read, 4, TransformPart::Position, 0).Access == AxisAccess::Insert);

    // The game loop keeps setting it.
    CHECK(Axis(Read, 5, TransformPart::Position, 0).Access == AxisAccess::Locked);
    CHECK(Read.Origins.at({IdSpace::Object, 5}).Creator.Line == 11);
}

TEST_CASE("a capture keeps only the nodes a chosen file creates")
{
    Captured Program("include");
    const fs::path Included = Program.Write("props.dba", "make object cube 2, 10\n");
    const fs::path Source = Program.Write("main.dba", "make object cube 1, 10\n#include \"props.dba\"\nsync\n");
    const CaptureReadResult Read = Program.Run(Source, 1, dbc::GenericPathToUtf8(Included.lexically_normal()));
    CHECK(Read.Document.Find(IdSpace::Object, 1) == nullptr);
    REQUIRE(Read.Document.Find(IdSpace::Object, 2) != nullptr);
}

TEST_CASE("a capture stands in a box for geometry only the program describes")
{
    Captured Program("custom");
    const fs::path Source = Program.Write("main.dba", "make object cube 1, 20\n"
                                                      "make mesh from object 1, 1\n"
                                                      "make object 2, 1, 0\n"
                                                      "position object 2, 7, 0, 0\n"
                                                      "sync\n");
    const CaptureReadResult Read = Program.Run(Source);
    const SceneNode* Stand = Read.Document.Find(IdSpace::Object, 2);
    REQUIRE(Stand != nullptr);
    CHECK(Stand->Kind == NodeKind::Box);
    CHECK(Stand->Extent.x == doctest::Approx(20.0f));
    CHECK(Stand->Position.x == doctest::Approx(7.0f));
    CHECK_FALSE(Read.Notes.empty());
}

TEST_CASE("an unreadable capture is an error")
{
    SceneCapture Capture;
    std::string Error;
    CHECK_FALSE(ParseSceneCapture("not json", "/root", "/root", Capture, Error));
    CHECK_FALSE(Error.empty());
    CHECK_FALSE(ParseSceneCapture("{\"schema\":\"other\"}", "/root", "/root", Capture, Error));
    // Its shape comes from another process; a wrong one is ignored, never thrown.
    CHECK(ParseSceneCapture("{\"schema\":\"opendbc-scene-capture\",\"version\":1,\"profile\":\"dbc\","
                            "\"calls\":[3,{\"a\":1},[99999]],\"objects\":{\"id\":1},\"lights\":[7]}",
                            "/root", "/root", Capture, Error));
    CHECK(Capture.Commands.empty());
    REQUIRE(Capture.Lights.size() == 1);
    CHECK(Capture.Lights.front().Id == 0);
}
