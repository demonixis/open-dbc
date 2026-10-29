#include <doctest/doctest.h>

#include "engine/systems/core_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "interpreter/basic_path.hpp"
#include "interpreter/command_line.hpp"
#include "interpreter/project_resolver.hpp"
#include "interpreter/runtime_startup.hpp"
#include "path_utf8.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
    struct ProjectFixture
    {
        ProjectFixture()
        {
            Root = std::filesystem::temp_directory_path() /
                   ("opendbc-resolver-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(Root);
        }

        ~ProjectFixture()
        {
            std::error_code Error;
            std::filesystem::remove_all(Root, Error);
        }

        void Write(const std::filesystem::path& Relative, std::string_view Text) const
        {
            std::filesystem::create_directories((Root / Relative).parent_path());
            std::ofstream Output(Root / Relative, std::ios::binary);
            Output << Text;
        }

        std::filesystem::path Root;
    };
} // namespace

TEST_CASE("UTF-8 project paths survive CLI parsing source loading includes and file operations")
{
    ProjectFixture Fixture;
    const std::filesystem::path Folder(u8"caf\u00e9-\u65e5\u672c-\U0001f3ae");
    const std::filesystem::path Main(u8"d\u00e9part.dba");
    const std::filesystem::path Include(u8"\u65e5\u672c.dba");
    const std::filesystem::path Data(u8"r\u00e9sultat-\U0001f3ae.txt");
    const auto Root = Fixture.Root / Folder;
    const auto Entry = Root / Main;
    const auto Log = Root / std::filesystem::path(u8"sortie-\u65e5\u672c.txt");
    const std::string DataName = dbc::PathToUtf8(Data);
    Fixture.Write(Folder / Include, "print 42\n");
    std::string Source = "#include \"" + dbc::PathToUtf8(Include) + "\"\n";
    Source += "make file \"" + DataName + "\"\n";
    Source += "print file exist(\"" + DataName + "\")\n";
    Source += "delete file \"" + DataName + "\"\n";
    Source += "open to write 1, \"" + DataName + "\"\n";
    Source += "write string 1, \"unicode-data\"\nclose file 1\nend\n";
    Fixture.Write(Folder / Main, Source);
    const std::string EntryText = dbc::PathToUtf8(Entry);
    const std::string LogText = dbc::PathToUtf8(Log);
    const std::string RootText = dbc::PathToUtf8(Root);
    CHECK(dbc::PathFromUtf8(EntryText) == Entry);
    CHECK(dbc::interpreter::NormalizeBasicPath(Entry) == Entry.lexically_normal());
    const std::vector<std::string_view> Arguments = {"run",          EntryText, "--dialect",    "dbc",  "--headless",
                                                     "--asset-root", RootText,  "--log-output", LogText};
    const auto Parsed = dbc::interpreter::ParseCommandLine(Arguments);
    REQUIRE(Parsed.Command);
    CHECK(Parsed.Command->Path == Entry);
    CHECK(Parsed.Command->Execution.AssetRoot == Root);
    CHECK(Parsed.Command->Execution.LogOutputPath == Log);
    std::string Output;
    dbc::interpreter::CommandLineEnvironment Environment;
    Environment.StandardOutput = [&](std::string_view Text) { Output += Text; };
    Environment.StandardError = [&](std::string_view Text) { Output += Text; };
    CHECK(dbc::interpreter::DispatchCommandLine(*Parsed.Command, Environment) == 0);
    INFO(Output);
    CHECK(std::filesystem::is_regular_file(Root / Data));
    std::ifstream Input(Log, std::ios::binary);
    const std::string Logged{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
    CHECK(Logged == "42\n1\n");
    std::filesystem::path Confined;
    std::string Error;
    CHECK(dbc::interpreter::ResolveConfinedBasicPath(Root, Include, Confined, &Error));
    CHECK_FALSE(dbc::interpreter::ResolveConfinedBasicPath(Root, "../outside.dba", Confined, &Error));
}

TEST_CASE("project resolution follows forced configuration descriptor and detection precedence")
{
    ProjectFixture Fixture;
    Fixture.Write("main.dba", "global value as integer\nprint value\n");

    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root / "main.dba";
    auto Detected = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Detected);
    CHECK(Detected.Project->Profile == dbc::language::LanguageProfile::DbPro);
    CHECK(Detected.Project->ProfileOrigin == dbc::interpreter::DialectOrigin::Detected);

    Fixture.Write(".opendbc-project.json", "{\"language\":{\"dialect\":\"dbc\",\"entry\":\"main.dba\"}}\n");
    auto Configured = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Configured);
    CHECK(Configured.Project->Profile == dbc::language::LanguageProfile::Dbc);
    CHECK(Configured.Project->ProfileOrigin == dbc::interpreter::DialectOrigin::Configuration);

    Request.ForcedProfile = dbc::language::LanguageProfile::DbPro;
    auto Forced = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Forced);
    CHECK(Forced.Project->Profile == dbc::language::LanguageProfile::DbPro);
    CHECK(Forced.Project->ProfileOrigin == dbc::interpreter::DialectOrigin::Forced);
}

TEST_CASE("folder resolution exposes multiple DBPro descriptors and DBC can detach them")
{
    ProjectFixture Fixture;
    Fixture.Write("main.dba", "print 1\n");
    Fixture.Write("one.dbpro", "version=DBP1.00\nmain=main.dba\n");
    Fixture.Write("two.dbpro", "version=DBP1.00\nmain=main.dba\n");

    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root;
    const auto Multiple = dbc::interpreter::ResolveProject(Request);
    CHECK_FALSE(Multiple);
    CHECK(Multiple.ProjectCandidates.size() == 2);

    Request.ForcedProfile = dbc::language::LanguageProfile::Dbc;
    const auto Detached = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Detached);
    CHECK_FALSE(Detached.Project->DescriptorPath);
    CHECK(Detached.Project->Profile == dbc::language::LanguageProfile::Dbc);
}

TEST_CASE("DBPro startup metadata and active legacy blockers are never ignored")
{
    ProjectFixture Fixture;
    Fixture.Write("main.dba", "print cl$()\n");
    Fixture.Write("game.dbpro", "version=DBP1.00\nmain=main.dba\nproject name=Evidence\napp title=Runtime title\n"
                                "graphics mode=window\nwindow resolution=800x600\ncommandlinearguments=alpha beta\n"
                                "encryption=YES\ncard options window=YES\n");
    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root / "game.dbpro";
    const auto Result = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Result);
    CHECK(Result.Project->Startup.Title == "Runtime title");
    CHECK(Result.Project->Startup.Width == 800);
    CHECK(Result.Project->Startup.Height == 600);
    CHECK(Result.Project->Startup.CommandLine == "alpha beta");
    CHECK_FALSE(Result.Project->RunBlockers.empty());
    CHECK_FALSE(Result.Project->ExportBlockers.empty());
}

TEST_CASE("DBPro project colour depth preserves 16-bit intent and normalizes the 16M alias")
{
    ProjectFixture Fixture;
    Fixture.Write("main.dba", "print screen depth()\n");
    Fixture.Write("depth16.dbpro", "version=DBP1.00\nmain=main.dba\ngraphics mode=fullscreen\n"
                                   "fullscreen resolution=640x480x16\n");
    Fixture.Write("depth16m.dbpro", "version=DBP1.00\nmain=main.dba\ngraphics mode=fullscreen\n"
                                    "fullscreen resolution=640x480x16M\n");

    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root / "depth16.dbpro";
    const auto Depth16 = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Depth16);
    CHECK(Depth16.Project->Startup.Depth == 16);

    Request.Target = Fixture.Root / "depth16m.dbpro";
    const auto Depth16M = dbc::interpreter::ResolveProject(Request);
    REQUIRE(Depth16M);
    CHECK(Depth16M.Project->Startup.Depth == 32);
}

TEST_CASE("shared project runtime startup reports DBC5000 instead of throwing")
{
    dbc::EngineKernel Kernel;
    REQUIRE(Kernel.Initialize("startup-test", 96, 72));

    dbc::interpreter::ProjectStartup Startup;
    Startup.Width = 96;
    Startup.Height = 72;
    Startup.Depth = 16;
    const dbc::interpreter::RuntimeStartupResult Valid = dbc::interpreter::InitializeProjectRuntime(Kernel, Startup);
    REQUIRE(Valid);
    CHECK(Kernel.Core().ScreenDepth() == 16);

    Startup.Depth = 8;
    const dbc::interpreter::RuntimeStartupResult Invalid = dbc::interpreter::InitializeProjectRuntime(Kernel, Startup);
    CHECK_FALSE(Invalid);
    CHECK(Invalid.Diagnostic.find("DBC5000: Runtime startup failed") == 0);
}

TEST_CASE("opening a DBA inside a unique DBPro project resolves the descriptor")
{
    ProjectFixture Fixture;
    Fixture.Write("src/main.dba", "print 1\n");
    Fixture.Write("game.dbpro", "version=DBP1.00\nmain=src\\main.dba\n");

    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root / "src/main.dba";
    const dbc::interpreter::ProjectResolveResult Result = dbc::interpreter::ResolveProject(Request);

    REQUIRE(Result);
    CHECK(Result.Project->InputKind == dbc::interpreter::ProjectInputKind::DbProProject);
    CHECK(Result.Project->Profile == dbc::language::LanguageProfile::DbPro);
    CHECK(Result.Project->DescriptorPath == Fixture.Root / "game.dbpro");
    CHECK(Result.Project->EntrySource == Fixture.Root / "src/main.dba");
}

TEST_CASE("a standalone DBA resolves relative assets beside its source")
{
    ProjectFixture Fixture;
    Fixture.Write("collection/tutorial/main.dba", "print 1\n");

    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root / "collection/tutorial/main.dba";
    Request.ForcedProfile = dbc::language::LanguageProfile::DbPro;
    Request.ReadProjectConfiguration = false;
    const dbc::interpreter::ProjectResolveResult Result = dbc::interpreter::ResolveProject(Request);

    REQUIRE(Result);
    CHECK(Result.Project->InputKind == dbc::interpreter::ProjectInputKind::Source);
    CHECK(Result.Project->AssetRoot == Fixture.Root / "collection/tutorial");
}

TEST_CASE("configured project paths normalize before enforcing canonical confinement")
{
    ProjectFixture Fixture;
    Fixture.Write("project/main.dba", "print 1\n");
    Fixture.Write("project/sub/placeholder.txt", "");
    Fixture.Write("outside.dba", "print 2\n");
    Fixture.Write("outside.dbpro", "version=DBP1.00\nmain=outside.dba\n");
    const auto Configure = [&](const std::string& Fields)
    { Fixture.Write("project/.opendbc-project.json", "{\"language\":{" + Fields + "}}\n"); };
    dbc::interpreter::ProjectResolveRequest Request;
    Request.Target = Fixture.Root / "project";
    Configure(R"("dialect":"dbc","entry":"sub/../../outside.dba")");
    CHECK_FALSE(dbc::interpreter::ResolveProject(Request));
    Configure(R"("dialect":"dbpro","projectFile":"sub/../../outside.dbpro")");
    CHECK_FALSE(dbc::interpreter::ResolveProject(Request));
    Configure(R"("dialect":"dbc","entry":"sub/../main.dba")");
    REQUIRE(dbc::interpreter::ResolveProject(Request));

    std::error_code Error;
    std::filesystem::create_symlink(Fixture.Root / "outside.dba", Fixture.Root / "project/link.dba", Error);
    if (Error)
    {
        MESSAGE("Symlinks are unavailable: " << Error.message());
        return;
    }
    Configure(R"("dialect":"dbc","entry":"link.dba")");
    CHECK_FALSE(dbc::interpreter::ResolveProject(Request));
    std::filesystem::remove(Fixture.Root / "project/link.dba");
    std::filesystem::create_symlink(Fixture.Root / "project/main.dba", Fixture.Root / "project/link.dba");
    REQUIRE(dbc::interpreter::ResolveProject(Request));
}
