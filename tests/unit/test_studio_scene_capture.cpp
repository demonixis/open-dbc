#include <doctest/doctest.h>

#include "interpreter/command_line.hpp"
#include "language/language_catalogue.hpp"
#include "path_utf8.hpp"
#include "scene/scene_capture.hpp"
#include "scene/scene_patch.hpp"

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

    std::string ReadFile(const fs::path& Path)
    {
        std::ifstream In(Path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
    }

    // What saving an edited document does to the one source file behind it.
    PatchPlan ApplyEdits(const CaptureReadResult& Read, const SceneDocument& Edited, const fs::path& Source)
    {
        PatchPlan Plan = PlanPatches(Read.Document, Edited, Read.Origins);
        const std::string File = dbc::GenericPathToUtf8(Source.lexically_normal());
        REQUIRE(Plan.Files.size() == 1);
        REQUIRE(Plan.Files.count(File) == 1);
        const PatchResult Patch = PatchSource(ReadFile(Source), File, Edited.Profile, Plan.Files.at(File));
        REQUIRE(Patch.Error.empty());
        std::ofstream Out(Source, std::ios::binary);
        Out << Patch.Text;
        return Plan;
    }

    SourceEdit Addition(std::uint32_t Line, SymbolId Symbol, SymbolId Added, std::vector<float> Values)
    {
        SourceEdit Edit;
        Edit.Line = Line;
        Edit.Column = 1;
        Edit.Symbol = Symbol;
        Edit.Added = Added;
        Edit.Values = std::move(Values);
        return Edit;
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

TEST_CASE("an edit rewrites only its argument, or adds a statement after the creation")
{
    Captured Program("patch_round_trip");
    const fs::path Source = Program.Write("main.dba", "sync on\n"
                                                      "make object cube 1, 10 ` the crate\n"
                                                      "position object 1, 5, -2, z#\n"
                                                      "if 1 = 1\n"
                                                      "    make object sphere 2, 8\n"
                                                      "endif\n"
                                                      "if 1 = 1 then make object cone 3, 4\n"
                                                      "make object cube 4, 1 : color object 4, 0\n"
                                                      "xrotate object 1, 30\n"
                                                      "make matrix 1, 100, 100, 4, 4\n"
                                                      "make light 1\n"
                                                      "do\n"
                                                      "  sync\n"
                                                      "loop\n");
    const CaptureReadResult Read = Program.Run(Source);
    SceneDocument Edited = Read.Document;
    Edited.Find(IdSpace::Object, 1)->Position.x = 7.5f;
    Edited.Find(IdSpace::Object, 1)->Rotation.y = 45.0f;
    Edited.Find(IdSpace::Object, 2)->Position = glm::vec3(1.0f, 2.0f, 3.0f);
    Edited.Find(IdSpace::Object, 3)->Position.y = 9.0f;
    Edited.Find(IdSpace::Object, 4)->Scale.x = 250.0f;
    Edited.Find(IdSpace::Matrix, 1)->Position.z = -4.0f;
    Edited.Find(IdSpace::Light, 1)->Position.x = 6.0f;
    const PatchPlan Plan = ApplyEdits(Read, Edited, Source);

    CHECK(ReadFile(Source) == "sync on\n"
                              "make object cube 1, 10 ` the crate\n"
                              "yrotate object 1, 45\n"
                              "position object 1, 7.5, -2, z#\n"
                              "if 1 = 1\n"
                              "    make object sphere 2, 8\n"
                              "    position object 2, 1, 2, 3\n"
                              "endif\n"
                              "if 1 = 1 then make object cone 3, 4 : position object 3, 0, 9, 0\n"
                              "make object cube 4, 1 : scale object 4, 250, 100, 100 : color object 4, 0\n"
                              "xrotate object 1, 30\n"
                              "make matrix 1, 100, 100, 4, 4\n"
                              "position matrix 1, 0, 0, -4\n"
                              "make light 1\n"
                              "position light 1, 6, 0, 0\n"
                              "do\n"
                              "  sync\n"
                              "loop\n");

    const CaptureReadResult Again = Program.Run(Source);
    CHECK(Program.Capture.Status == "completed");
    CHECK(UnappliedEdits(Plan.Axes, Again).empty());
    CHECK(Again.Document.Find(IdSpace::Object, 1)->Position.x == doctest::Approx(7.5f));
    CHECK(Again.Document.Find(IdSpace::Object, 1)->Position.y == doctest::Approx(-2.0f));
    CHECK(Again.Document.Find(IdSpace::Object, 1)->Rotation.x == doctest::Approx(30.0f));
    CHECK(Again.Document.Find(IdSpace::Object, 1)->Rotation.y == doctest::Approx(45.0f));
    CHECK(Again.Document.Find(IdSpace::Object, 2)->Position.z == doctest::Approx(3.0f));
    CHECK(Again.Document.Find(IdSpace::Object, 3)->Position.y == doctest::Approx(9.0f));
    CHECK(Again.Document.Find(IdSpace::Object, 4)->Scale.x == doctest::Approx(250.0f));
    CHECK(Again.Document.Find(IdSpace::Matrix, 1)->Position.z == doctest::Approx(-4.0f));
    CHECK(Again.Document.Find(IdSpace::Light, 1)->Position.x == doctest::Approx(6.0f));
    // What was added is a statement like any other: the next edit rewrites it.
    CHECK(Axis(Again, 2, TransformPart::Position, 0).Access == AxisAccess::Replace);
    CHECK(Axis(Again, 1, TransformPart::Rotation, 1).Access == AxisAccess::Replace);
}

TEST_CASE("locked axes never reach the source")
{
    Captured Program("patch_locked");
    const fs::path Source = Program.Write("main.dba", "sync on\n"
                                                      "for i = 1 to 2\n"
                                                      "  make object cube i, 5\n"
                                                      "  position object i, i, 0, 0\n"
                                                      "next i\n"
                                                      "make object cube 3, 5\n"
                                                      "do\n"
                                                      "  yrotate object 3, 10\n"
                                                      "  sync\n"
                                                      "loop\n");
    const CaptureReadResult Read = Program.Run(Source, 2);
    SceneDocument Edited = Read.Document;
    Edited.Find(IdSpace::Object, 1)->Position.x = 50.0f;
    Edited.Find(IdSpace::Object, 3)->Rotation.y = 50.0f;
    const PatchPlan Plan = PlanPatches(Read.Document, Edited, Read.Origins);
    CHECK(Plan.Files.empty());
    CHECK(Plan.Axes.empty());
}

TEST_CASE("an expression an edit replaces is reported")
{
    Captured Program("patch_expression");
    const fs::path Source = Program.Write("main.dba", "a# = 2\n"
                                                      "make object cube 1, 5\n"
                                                      "position object 1, a# * 2, -3, 0\n");
    const CaptureReadResult Read = Program.Run(Source);
    REQUIRE(Axis(Read, 1, TransformPart::Position, 0).Access == AxisAccess::Replace);
    const std::string File = dbc::GenericPathToUtf8(Source.lexically_normal());

    SceneDocument Edited = Read.Document;
    Edited.Find(IdSpace::Object, 1)->Position.x = 9.0f;
    Edited.Find(IdSpace::Object, 1)->Position.y = 8.0f;
    const PatchPlan Plan = PlanPatches(Read.Document, Edited, Read.Origins);
    const PatchResult Patch = PatchSource(ReadFile(Source), File, Edited.Profile, Plan.Files.at(File));
    REQUIRE(Patch.Error.empty());
    CHECK(Patch.Text.find("position object 1, 9, 8, 0\n") != std::string::npos);
    // A plain number needs no warning, even a negative one.
    REQUIRE(Patch.Replaced.size() == 1);
    CHECK(Patch.Replaced.front() == "3: a# * 2");
}

TEST_CASE("a one-line IF keeps the added statement, and line endings carry over")
{
    Captured Program("patch_flow");
    const SourceEdit Edit =
        Addition(2, SymbolId::Command_MakeObjectCone, SymbolId::Command_PositionObject, {1.0f, 2.0f, 3.0f});

    // Outside the IF, it would position an object that does not exist, and the run would fail.
    const std::string Text = "a = 1\nif a = 2 then make object cone 3, 4\n";
    const PatchResult Inline = PatchSource(Text, "main.dba", dbc::language::LanguageProfile::Dbc, {&Edit, 1});
    REQUIRE(Inline.Error.empty());
    const fs::path Source = Program.Write("main.dba", Inline.Text);
    Program.Run(Source);
    CHECK(Program.Capture.Status == "completed");

    const std::string Windows = "rem start\r\nmake object cone 3, 4 rem the cone\r\nsync\r\n";
    const PatchResult Lines = PatchSource(Windows, "main.dba", dbc::language::LanguageProfile::Dbc, {&Edit, 1});
    REQUIRE(Lines.Error.empty());
    CHECK(Lines.Text == "rem start\r\nmake object cone 3, 4 rem the cone\r\n"
                        "position object 3, 1, 2, 3\r\nsync\r\n");

    // The added statement is spelled like the one it follows.
    const std::string Upper = "SYNC\nMAKE OBJECT CONE 3, 4\n";
    const PatchResult Spelled = PatchSource(Upper, "main.dba", dbc::language::LanguageProfile::Dbc, {&Edit, 1});
    REQUIRE(Spelled.Error.empty());
    CHECK(Spelled.Text == "SYNC\nMAKE OBJECT CONE 3, 4\nPOSITION OBJECT 3, 1, 2, 3\n");

    const std::string Last = "sync\nmake object cone 3, 4";
    const PatchResult End = PatchSource(Last, "main.dba", dbc::language::LanguageProfile::Dbc, {&Edit, 1});
    REQUIRE(End.Error.empty());
    CHECK(End.Text == "sync\nmake object cone 3, 4\nposition object 3, 1, 2, 3");
}

TEST_CASE("a source that no longer matches the capture is refused")
{
    constexpr auto Profile = dbc::language::LanguageProfile::Dbc;
    SourceEdit Edit;
    Edit.Line = 2;
    Edit.Column = 1;
    Edit.Symbol = SymbolId::Command_PositionObject;
    Edit.Argument = 1;
    Edit.Values = {4.0f};
    const std::string Moved = "make object cube 1, 5\nsync\nposition object 1, 1, 2, 3\n";
    CHECK_FALSE(PatchSource(Moved, "main.dba", Profile, {&Edit, 1}).Error.empty());

    Edit.Argument = 7;
    const std::string Short = "make object cube 1, 5\nposition object 1, 1, 2, 3\n";
    CHECK_FALSE(PatchSource(Short, "main.dba", Profile, {&Edit, 1}).Error.empty());
}

TEST_CASE("an added statement repeats the number only if it reads the same twice")
{
    constexpr auto Profile = dbc::language::LanguageProfile::Dbc;
    const SourceEdit Edit =
        Addition(2, SymbolId::Command_MakeObjectCube, SymbolId::Command_PositionObject, {1.0f, 0.0f, 0.0f});

    const std::string Function = "sync\nmake object cube next_id(), 5\nfunction next_id()\n"
                                 "endfunction 1\n";
    CHECK_FALSE(PatchSource(Function, "main.dba", Profile, {&Edit, 1}).Error.empty());
    const std::string Builtin = "sync\nmake object cube rnd(5) + 1, 5\n";
    CHECK_FALSE(PatchSource(Builtin, "main.dba", Profile, {&Edit, 1}).Error.empty());

    const std::string Array = "dim ids(3)\nmake object cube ids(1) + 1, 5\n";
    const PatchResult Patch = PatchSource(Array, "main.dba", Profile, {&Edit, 1});
    REQUIRE(Patch.Error.empty());
    CHECK(Patch.Text == "dim ids(3)\nmake object cube ids(1) + 1, 5\nposition object ids(1) + 1, 1, 0, 0\n");
}

TEST_CASE("an edit the program overrides is reported by the next capture")
{
    Captured Program("patch_overridden");
    const fs::path Source = Program.Write("main.dba", "make object cube 1, 5\n"
                                                      "position object 1, 1, 0, 0\n"
                                                      "if object position x(1) > 3 then position object 1, 0, 0, 0\n");
    const CaptureReadResult Read = Program.Run(Source);
    REQUIRE(Axis(Read, 1, TransformPart::Position, 0).Access == AxisAccess::Replace);
    SceneDocument Edited = Read.Document;
    Edited.Find(IdSpace::Object, 1)->Position.x = 5.0f;
    const PatchPlan Plan = ApplyEdits(Read, Edited, Source);

    const CaptureReadResult Again = Program.Run(Source);
    const std::vector<std::string> Messages = UnappliedEdits(Plan.Axes, Again);
    REQUIRE(Messages.size() == 1);
    CHECK(Messages.front().find("X position 5") != std::string::npos);
    CHECK(Messages.front().find("line 3 of main.dba") != std::string::npos);
}
