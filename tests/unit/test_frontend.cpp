#include <doctest/doctest.h>

#include "language/frontend.hpp"
#include "vm/compiler.hpp"
#include "vm/verifier.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::shared_ptr<const dbc::language::SourceDocument> Source(std::string Text,
                                                                dbc::language::LanguageProfile Profile,
                                                                std::string Path = "test.dba")
    {
        return std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{std::filesystem::path(std::move(Path)), std::move(Text), Profile});
    }

    bool HasCode(const dbc::language::Program& Program, std::string_view Code)
    {
        return std::any_of(Program.Diagnostics.begin(), Program.Diagnostics.end(),
                           [&](const auto& Value) { return Value.Code == Code; });
    }

    std::string ReadAll(const std::filesystem::path& Path)
    {
        std::ifstream Input(Path, std::ios::binary);
        return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
    }

    std::string Repeat(std::string_view Text, std::size_t Count)
    {
        std::string Result;
        for (std::size_t Index = 0; Index < Count; ++Index)
            Result += Text;
        return Result;
    }

    // Each shape nests Depth levels deep under the shared statement and expression budget.
    std::vector<std::string> NestedPrograms(std::size_t Depth)
    {
        std::string Loops;
        for (std::size_t Index = 0; Index < Depth; ++Index)
            Loops += "for i" + std::to_string(Index) + " = 1 to 1\n";
        Loops += "x = 1\n";
        for (std::size_t Index = Depth; Index > 0; --Index)
            Loops += "next i" + std::to_string(Index - 1) + "\n";
        std::string ElseIfs = "if x = 0\nx = 1\n";
        // The IF body is one level deep and each ELSEIF branch nests one more.
        for (std::size_t Index = 1; Index < Depth; ++Index)
            ElseIfs += "elseif x = " + std::to_string(Index) + "\nx = 2\n";
        ElseIfs += "endif\n";
        return {
            "x = " + Repeat("(", Depth) + "1" + Repeat(")", Depth) + "\n",
            "x = 1" + Repeat("+1", Depth) + "\n",
            "x = " + Repeat("- ", Depth) + "1\n",
            "x = " + Repeat("abs(", Depth) + "1" + Repeat(")", Depth) + "\n",
            Loops,
            ElseIfs,
            Repeat("if x = 0 then ", Depth) + "x = 1\n",
            Repeat("if x = 0\n", Depth / 2) + "x = " + Repeat("(", Depth - Depth / 2) + "1" +
                Repeat(")", Depth - Depth / 2) + "\n" + Repeat("endif\n", Depth / 2),
        };
    }

    struct NestedBuild
    {
        std::vector<std::string> Codes{};
        bool Lowered = false;
    };

    NestedBuild BuildNested(const std::string& Text)
    {
        NestedBuild Result;
        const dbc::language::Program Program =
            dbc::language::Compile(Source(Text + "y = 2\n", dbc::language::LanguageProfile::DbPro));
        for (const auto& Diagnostic : Program.Diagnostics)
            Result.Codes.push_back(Diagnostic.Code);
        if (!Program.HasErrors())
        {
            const dbc::vm::CompileResult Lowered = dbc::vm::Lower(Program);
            Result.Lowered = Lowered.Success() && dbc::vm::Verify(*Lowered.Bytecode).Success();
        }
        return Result;
    }

    // macOS gives secondary threads 512 KB of stack, the least of the supported hosts, and the Studio
    // builds exports on such a thread. AddressSanitizer frames are larger, so it builds in place.
    std::vector<NestedBuild> BuildOnSecondaryThread(const std::vector<std::string>& Programs)
    {
        std::vector<NestedBuild> Results;
        const auto Build = [&]
        {
            for (const std::string& Text : Programs)
                Results.push_back(BuildNested(Text));
        };
#if defined(__SANITIZE_ADDRESS__)
        Build();
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
        Build();
#else
        std::thread(Build).join();
#endif
#else
        std::thread(Build).join();
#endif
        return Results;
    }
} // namespace

TEST_CASE("Lexer spans are stable across CRLF and comments")
{
    const auto Document = Source("x = 1\r\nrem ignored\r\nprint \"ok\"\n", dbc::language::LanguageProfile::Dbc);
    const dbc::language::Program Program = dbc::language::LexAndParse(Document);
    REQUIRE_FALSE(Program.Tokens.empty());
    CHECK(Program.Tokens.front().Span.Begin.Line == 1);
    CHECK(Program.Tokens.front().Span.Begin.Column == 1);
    const auto Print = std::find_if(Program.Tokens.begin(), Program.Tokens.end(), [&](const auto& Value)
                                    { return dbc::language::Lexeme(*Document, Value.Span) == "print"; });
    REQUIRE(Print != Program.Tokens.end());
    CHECK(Print->Span.Begin.Line == 3);
    CHECK(Print->Span.Begin.Column == 1);
    CHECK(Program.Statements.size() == 2);
}

TEST_CASE("Historical profiles reject Professional lexical forms in Classic")
{
    const dbc::language::Program Classic =
        dbc::language::Compile(Source("x = %1011\ny = 1 << 2\n", dbc::language::LanguageProfile::Dbc));
    CHECK(HasCode(Classic, "DBC1002"));
    CHECK(HasCode(Classic, "DBC1007"));

    const dbc::language::Program Professional =
        dbc::language::Compile(Source("x = %1011\ny = 1 << 2\n", dbc::language::LanguageProfile::DbPro, "test.dba"));
    CHECK_FALSE(HasCode(Professional, "DBC1002"));
    CHECK_FALSE(HasCode(Professional, "DBC1007"));
}

TEST_CASE("REMSTART blocks are comments and must be closed")
{
    const auto Open = Source("print 1\nremstart\nprint 2\n", dbc::language::LanguageProfile::Dbc);
    const dbc::language::Program Unclosed = dbc::language::LexAndParse(Open);
    CHECK(HasCode(Unclosed, "DBC1008"));
    CHECK(Unclosed.Statements.size() == 1);

    const dbc::language::Program Closed =
        dbc::language::Compile(Source("print 1\nremstart trailing words\nprint 2\n  REMEND more words\nprint 3\n",
                                      dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(Closed.HasErrors());
    CHECK(Closed.Statements.size() == 2);
}

TEST_CASE("Operator keywords end a name and a keyword before ':' is never a label")
{
    const dbc::language::Program Program = dbc::language::Compile(Source("dim b(3)\n"
                                                                         "c = 1\n"
                                                                         "if c and b(1) then print 1\n"
                                                                         "x = c or mousex()\n"
                                                                         "print : print 2\n"
                                                                         "if c=1:exit:endif\n"
                                                                         "target:\n"
                                                                         "r = 1 : print \"r=\";r\n",
                                                                         dbc::language::LanguageProfile::Dbc),
                                                                  {.RejectUnsupported = false});
    REQUIRE_FALSE(Program.HasErrors());
    const std::size_t Labels = static_cast<std::size_t>(
        std::count_if(Program.Statements.begin(), Program.Statements.end(),
                      [](const auto& Value) { return Value.Kind == dbc::language::StatementKind::Label; }));
    CHECK(Labels == 1);

    const dbc::language::Program Unknown =
        dbc::language::Compile(Source("set image colorkey 255,0,255\n", dbc::language::LanguageProfile::Dbc));
    REQUIRE(Unknown.HasErrors());
    CHECK(Unknown.Diagnostics.front().Message.find("SET IMAGE COLORKEY") != std::string::npos);
}

TEST_CASE("Function return types come from ENDFUNCTION and names inside functions are local")
{
    const dbc::language::Program Program = dbc::language::Compile(Source("a$ = first(\"xy\")\n"
                                                                         "h# = half(3)\n"
                                                                         "x = 1\n"
                                                                         "touch()\n"
                                                                         "end\n"
                                                                         "function first(s$)\n"
                                                                         "t$ = left$(s$, 1)\n"
                                                                         "endfunction t$\n"
                                                                         "function half(v)\n"
                                                                         "endfunction v / 2.0\n"
                                                                         "function touch()\n"
                                                                         "x = 10\n"
                                                                         "endfunction\n",
                                                                         dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(Program.HasErrors());
    REQUIRE(Program.Functions.size() == 3);
    CHECK(Program.Functions[0].ReturnType.Kind == dbc::language::TypeKind::String);
    CHECK(Program.Functions[1].ReturnType.Kind == dbc::language::TypeKind::Real);
    REQUIRE(Program.Functions[2].ImplicitLocals.size() == 1);
    CHECK(Program.Functions[2].ImplicitLocals.front().first == "X");

    const dbc::language::Program Mismatch = dbc::language::Compile(Source("end\n"
                                                                          "function broken(v)\n"
                                                                          "if v then exitfunction \"text\"\n"
                                                                          "endfunction 1\n",
                                                                          dbc::language::LanguageProfile::Dbc));
    CHECK(HasCode(Mismatch, "DBC2041"));
}

TEST_CASE("Function return contracts are profile-specific and cover every return site")
{
    const dbc::language::Program Classic = dbc::language::Compile(Source("value$ = choose(1)\n"
                                                                         "end\n"
                                                                         "function choose(flag)\n"
                                                                         "if flag then exitfunction \"early\"\n"
                                                                         "endfunction \"final\"\n",
                                                                         dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(Classic.HasErrors());
    CHECK(Classic.Functions.front().ReturnType.Kind == dbc::language::TypeKind::String);

    const dbc::language::Program DbProString =
        dbc::language::Compile(Source("value$ = choose$(1)\n"
                                      "end\n"
                                      "function choose$(flag as integer)\n"
                                      "if flag then exitfunction \"early\"\n"
                                      "endfunction \"final\"\n",
                                      dbc::language::LanguageProfile::DbPro, "returns.dba"));
    REQUIRE_FALSE(DbProString.HasErrors());
    CHECK(DbProString.Functions.front().ReturnType.Kind == dbc::language::TypeKind::String);

    const dbc::language::Program MissingSuffix = dbc::language::Compile(Source(
        "end\nfunction text()\nendfunction \"value\"\n", dbc::language::LanguageProfile::DbPro, "missing_suffix.dba"));
    CHECK(HasCode(MissingSuffix, "DBC2041"));

    const dbc::language::Program WrongSuffix = dbc::language::Compile(
        Source("end\nfunction number$()\nendfunction 42\n", dbc::language::LanguageProfile::DbPro, "wrong_suffix.dba"));
    CHECK(HasCode(WrongSuffix, "DBC2041"));

    const dbc::language::Program Mixed = dbc::language::Compile(
        Source("end\nfunction mixed(flag)\nif flag then exitfunction \"value\"\nendfunction 42\n",
               dbc::language::LanguageProfile::Dbc));
    CHECK(HasCode(Mixed, "DBC2041"));

    const dbc::language::Program MixedDbProNumeric = dbc::language::Compile(
        Source("end\nfunction mixed(flag as integer)\nif flag then exitfunction 1\nendfunction 1.5\n",
               dbc::language::LanguageProfile::DbPro, "mixed_numeric.dba"));
    CHECK(HasCode(MixedDbProNumeric, "DBC2041"));
}

TEST_CASE("Reserved words function names and XOR obey only the active profile")
{
    const dbc::language::Program ClassicLabel =
        dbc::language::Compile(Source("local:\nprint 1\n", dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(ClassicLabel.HasErrors());
    REQUIRE(ClassicLabel.Statements.size() == 2);
    CHECK(ClassicLabel.Statements.front().Kind == dbc::language::StatementKind::Label);

    const dbc::language::Program DbProXor =
        dbc::language::Compile(Source("print 7 xor 3\n", dbc::language::LanguageProfile::DbPro, "xor.dba"));
    REQUIRE_FALSE(DbProXor.HasErrors());
    REQUIRE_FALSE(DbProXor.Statements.empty());
    REQUIRE_FALSE(DbProXor.Statements.front().Expressions.empty());
    CHECK(DbProXor.Statements.front().Expressions.front()->Text == "XOR");

    const dbc::language::Program ClassicXor =
        dbc::language::Compile(Source("print 7 xor 3\n", dbc::language::LanguageProfile::Dbc));
    CHECK(ClassicXor.HasErrors());

    const dbc::language::Program ClassicDbProCommand =
        dbc::language::Compile(Source("make matrix4 1\n", dbc::language::LanguageProfile::Dbc));
    REQUIRE(ClassicDbProCommand.HasErrors());
    REQUIRE(ClassicDbProCommand.Statements.size() == 1);
    CHECK(ClassicDbProCommand.Statements.front().Name == "MAKE MATRIX4");
    CHECK(ClassicDbProCommand.Statements.front().Expressions.empty());
}

TEST_CASE("A multiword command beginning with EXIT is never swallowed by the EXIT statement")
{
    const dbc::language::Program Program =
        dbc::language::Compile(Source("exit prompt \"leave?\", \"caption\"\n", dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(Program.HasErrors());
    REQUIRE(Program.Statements.size() == 1);
    CHECK(Program.Statements.front().Kind == dbc::language::StatementKind::Command);
    CHECK(Program.Statements.front().Name == "EXIT PROMPT");
}

TEST_CASE("Classic video commands compile while DBPro remains unsupported")
{
    const std::string Text = "load animation \"clip.avi\", 1\nprint animation exist(1)\n";
    const dbc::language::Program Classic =
        dbc::language::Compile(Source(Text, dbc::language::LanguageProfile::Dbc, "video.dba"));
    CHECK_FALSE(Classic.HasErrors());

    const dbc::language::Program Professional =
        dbc::language::Compile(Source(Text, dbc::language::LanguageProfile::DbPro, "video.dba"));
    CHECK(HasCode(Professional, "DBC2043"));
    CHECK_FALSE(std::any_of(Professional.Diagnostics.begin(), Professional.Diagnostics.end(), [](const auto& Diagnostic)
                            { return Diagnostic.Message.find("Unknown") != std::string::npos; }));
}

TEST_CASE("Arrays and scalars share a spelling and array commands take free markers")
{
    const dbc::language::Program Program = dbc::language::Compile(Source("dim x#(5)\n"
                                                                         "x# = x#(2)\n"
                                                                         "dim map(2,2)\n"
                                                                         "load array \"m.dat\", map(0)\n"
                                                                         "undim map(0)\n"
                                                                         "print late(1)\n"
                                                                         "dim late(3)\n",
                                                                         dbc::language::LanguageProfile::Dbc));
    CHECK_FALSE(Program.HasErrors());
}

TEST_CASE("Parser builds structured control-flow AST")
{
    const dbc::language::Program Program = dbc::language::LexAndParse(Source("for i = 1 to 3\n"
                                                                             "  if i > 1\n"
                                                                             "    print i\n"
                                                                             "  else\n"
                                                                             "    print 0\n"
                                                                             "  endif\n"
                                                                             "next i\n",
                                                                             dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(Program.HasErrors());
    REQUIRE(Program.Statements.size() == 1);
    CHECK(Program.Statements.front().Kind == dbc::language::StatementKind::For);
    REQUIRE(Program.Statements.front().Body.size() == 1);
    const auto& If = Program.Statements.front().Body.front();
    CHECK(If.Kind == dbc::language::StatementKind::If);
    CHECK(If.Body.size() == 1);
    CHECK(If.ElseBody.size() == 1);
}

TEST_CASE("DBPro block ELSEIF retains profile ownership and trailing THEN")
{
    const std::string Text = "if 0 then\n"
                             "  print 0\n"
                             "elseif 1 then\n"
                             "  print 1\n"
                             "else\n"
                             "  print 2\n"
                             "endif\n";
    const dbc::language::Program Professional =
        dbc::language::Compile(Source(Text, dbc::language::LanguageProfile::DbPro, "elseif.dba"));
    REQUIRE_FALSE(Professional.HasErrors());
    REQUIRE(Professional.Statements.size() == 1);
    REQUIRE(Professional.Statements.front().ElseBody.size() == 1);
    const dbc::language::Statement& ElseIf = Professional.Statements.front().ElseBody.front();
    CHECK(ElseIf.Builtin == dbc::language::SymbolId::Statement_Elseif);
    CHECK(ElseIf.Body.size() == 1);
    CHECK(ElseIf.ElseBody.size() == 1);

    const dbc::language::Program Classic =
        dbc::language::Compile(Source(Text, dbc::language::LanguageProfile::Dbc, "elseif.dba"));
    CHECK(HasCode(Classic, "DBC2014"));
}

TEST_CASE("Binder resolves scopes arrays UDTs functions and numeric conversions")
{
    dbc::language::Program Program = dbc::language::Compile(Source("type Point\n"
                                                                   "  x as float\n"
                                                                   "endtype\n"
                                                                   "dim p as Point\n"
                                                                   "p.x = 1.5\n"
                                                                   "dim values(3) as integer\n"
                                                                   "values(1) = 2\n"
                                                                   "function Add#(a as float, b as integer)\n"
                                                                   "  local value# = a + b\n"
                                                                   "  exitfunction value#\n"
                                                                   "endfunction\n"
                                                                   "print Add#(p.x, values(1))\n",
                                                                   dbc::language::LanguageProfile::DbPro, "typed.dba"));
    for (const auto& Diagnostic : Program.Diagnostics)
    {
        INFO(dbc::language::FormatDiagnostic(*Program.Source, Diagnostic));
    }
    CHECK_FALSE(Program.HasErrors());
    CHECK(Program.IsBound);
    REQUIRE(Program.Functions.size() == 1);
    CHECK(Program.Functions.front().ReturnType.Kind == dbc::language::TypeKind::Real);
}

TEST_CASE("Builtin results carry their explicit catalogue return type")
{
    const dbc::language::Program Program = dbc::language::Compile(Source("whole = sin(0.5)\n"
                                                                         "precise# = timer()\n"
                                                                         "text$ = str$(1.5)\n",
                                                                         dbc::language::LanguageProfile::Dbc));
    for (const auto& Diagnostic : Program.Diagnostics)
        INFO(dbc::language::FormatDiagnostic(*Program.Source, Diagnostic));
    REQUIRE_FALSE(Program.HasErrors());
    REQUIRE(Program.Statements.size() == 3);

    const auto& Whole = Program.Statements[0];
    REQUIRE(Whole.Expressions.size() == 2);
    CHECK(Whole.Expressions[0]->ResolvedType.Kind == dbc::language::TypeKind::Integer);
    CHECK(Whole.Expressions[1]->Reference == dbc::language::ReferenceKind::BuiltinFunction);
    CHECK(Whole.Expressions[1]->ResolvedType.Kind == dbc::language::TypeKind::Real);

    const auto& Precise = Program.Statements[1];
    REQUIRE(Precise.Expressions.size() == 2);
    CHECK(Precise.Expressions[1]->Reference == dbc::language::ReferenceKind::BuiltinFunction);
    CHECK(Precise.Expressions[1]->ResolvedType.Kind == dbc::language::TypeKind::Integer);

    const auto& Text = Program.Statements[2];
    REQUIRE(Text.Expressions.size() == 2);
    CHECK(Text.Expressions[1]->Reference == dbc::language::ReferenceKind::BuiltinFunction);
    CHECK(Text.Expressions[1]->ResolvedType.Kind == dbc::language::TypeKind::String);
}

TEST_CASE("DBPro GLOBAL references inside functions reuse a compatible global declaration")
{
    const dbc::language::Program Compatible =
        dbc::language::Compile(Source("global shared = 999\n"
                                      "SetShared()\n"
                                      "function SetShared()\n"
                                      "  global shared as integer\n"
                                      "  shared = 42\n"
                                      "endfunction\n",
                                      dbc::language::LanguageProfile::DbPro, "global-reference.dba"));
    for (const auto& Diagnostic : Compatible.Diagnostics)
        INFO(dbc::language::FormatDiagnostic(*Compatible.Source, Diagnostic));
    CHECK_FALSE(Compatible.HasErrors());

    const dbc::language::Program Conflicting =
        dbc::language::Compile(Source("global shared as integer\n"
                                      "function SetShared()\n"
                                      "  global shared as float\n"
                                      "endfunction\n",
                                      dbc::language::LanguageProfile::DbPro, "global-conflict.dba"));
    CHECK(HasCode(Conflicting, "DBC2019"));
}

TEST_CASE("DBPro GLOBAL and LOCAL declare every name of a comma list")
{
    const dbc::language::Program Listed =
        dbc::language::Compile(Source("global a, b as integer, c# = 1.5\n"
                                      "function Work()\n"
                                      "  local d$, e as float\n"
                                      "endfunction\n",
                                      dbc::language::LanguageProfile::DbPro, "global-list.dba"));
    for (const auto& Diagnostic : Listed.Diagnostics)
        INFO(dbc::language::FormatDiagnostic(*Listed.Source, Diagnostic));
    CHECK_FALSE(Listed.HasErrors());
    REQUIRE(Listed.Statements.size() == 3);
    CHECK(Listed.Statements[0].Name == "A");
    CHECK(Listed.Statements[1].Name == "B");
    CHECK(Listed.Statements[1].HasExplicitType);
    CHECK(Listed.Statements[2].Name == "C#");
    CHECK(Listed.Statements[2].Expressions.size() == 1);

    const dbc::language::Program Empty =
        dbc::language::Compile(Source("global a,\n", dbc::language::LanguageProfile::DbPro, "global-empty-item.dba"));
    CHECK(HasCode(Empty, "DBC1209"));
}

TEST_CASE("Binder diagnoses profile availability and discontinuous arity")
{
    const dbc::language::Program Classic =
        dbc::language::Compile(Source("local x = 1\nposition camera 0,1,2,3\n", dbc::language::LanguageProfile::Dbc));
    CHECK(HasCode(Classic, "DBC2017"));
    CHECK(HasCode(Classic, "DBC2044"));

    const dbc::language::Program Professional = dbc::language::Compile(
        Source("position particles 1,2,3\n", dbc::language::LanguageProfile::DbPro, "arity.dba"));
    CHECK(HasCode(Professional, "DBC2044"));
}

TEST_CASE("Classic compatibility forms bind with corrected spellings and arities")
{
    const dbc::language::Program Program =
        dbc::language::Compile(Source("execute file \"tool\", \"--flag\", \".\"\n"
                                      "execute file \"tool\", \"--flag\", \".\", 1\n"
                                      "ftp get file \"remote.bin\", \"local.bin\"\n"
                                      "ftp get file \"remote.bin\", \"local.bin\", 1024\n"
                                      "save sound \"sample.wav\", 1\n"
                                      "print get static collision x()\n"
                                      "print listener angle x()\n"
                                      "texture$ = limb texture name$(1, 0)\n"
                                      "print appname$()\n"
                                      "print statistic(1)\n"
                                      "print animation looping(1)\n",
                                      dbc::language::LanguageProfile::Dbc, "classic-compat.dba"));
    for (const auto& Diagnostic : Program.Diagnostics)
        INFO(dbc::language::FormatDiagnostic(*Program.Source, Diagnostic));
    CHECK_FALSE(Program.HasErrors());
}

TEST_CASE("Classic accepts a bare zero-argument user function statement")
{
    const dbc::language::Program Program =
        dbc::language::Compile(Source("handle_explosion\n"
                                      "function handle_explosion()\n"
                                      "  print \"handled\"\n"
                                      "endfunction\n",
                                      dbc::language::LanguageProfile::Dbc, "bare-call.dba"));
    for (const auto& Diagnostic : Program.Diagnostics)
        INFO(dbc::language::FormatDiagnostic(*Program.Source, Diagnostic));
    REQUIRE_FALSE(Program.HasErrors());
    REQUIRE(Program.Statements.size() == 1);
    CHECK(Program.Statements.front().Kind == dbc::language::StatementKind::Expression);
    REQUIRE(Program.Statements.front().Expressions.size() == 1);
    CHECK(Program.Statements.front().Expressions.front()->Reference == dbc::language::ReferenceKind::UserFunction);
}

TEST_CASE("Professional does not inherit Classic bare user-function statement syntax")
{
    const dbc::language::Program Program =
        dbc::language::Compile(Source("handle_explosion\n"
                                      "function handle_explosion()\n"
                                      "endfunction\n",
                                      dbc::language::LanguageProfile::DbPro, "dbpro-bare-call.dba"));
    REQUIRE(Program.HasErrors());
    CHECK(HasCode(Program, "DBC2013"));
}

TEST_CASE("Binder selects typed builtin overloads and rejects incompatible arguments")
{
    const dbc::language::Program Valid =
        dbc::language::Compile(Source("text 10, 20, \"hello\"\n"
                                      "value$ = left$(\"abc\", 2)\n"
                                      "read file 1, value\n"
                                      "read fileblock 1, \"payload.bin\"\n"
                                      "write fileblock 1, \"payload.bin\"\n",
                                      dbc::language::LanguageProfile::Dbc, "typed.dba"));
    CHECK_FALSE(Valid.HasErrors());

    const dbc::language::Program Invalid =
        dbc::language::Compile(Source("text 10, 20, 42\n"
                                      "value$ = left$(99, 2)\n"
                                      "read file 1, text$\n"
                                      "read fileblock 1, 99\n"
                                      "ftp get file 1, 2\n",
                                      dbc::language::LanguageProfile::Dbc, "typed.dba"));
    REQUIRE(Invalid.HasErrors());
    CHECK(HasCode(Invalid, "DBC2048"));
}

TEST_CASE("Includes inherit the immutable root profile")
{
    const dbc::language::Program Parsed =
        dbc::language::LexAndParse(Source("#include \"shared.dba\"\n", dbc::language::LanguageProfile::Dbc));
    REQUIRE_FALSE(Parsed.HasErrors());
    REQUIRE(Parsed.Includes.size() == 1);
    CHECK(Parsed.Includes.front().Path == "shared.dba");
    CHECK(Parsed.Includes.front().StatementIndex == 0);
    CHECK(dbc::language::IsLanguageSource("shared.DBA"));
    CHECK(dbc::language::IsLanguageSource("shared.dba"));
}

TEST_CASE("Includes are expanded at their directive and preserve source diagnostics")
{
    const auto Root = Source("before = 1\n#include \"shared.dba\"\nafter = Twice(before)\n",
                             dbc::language::LanguageProfile::Dbc, "project/main.dba");
    const dbc::language::IncludeLoader Loader =
        [](const dbc::language::SourceDocument&, const std::filesystem::path& Path, std::string&)
    {
        if (Path == "shared.dba")
            return Source("function Twice(value)\n  exitfunction value * 2\nendfunction\n",
                          dbc::language::LanguageProfile::Dbc, "project/shared.dba");
        return std::shared_ptr<const dbc::language::SourceDocument>{};
    };
    const dbc::language::Program Program = dbc::language::Compile(Root, {}, Loader);
    for (const dbc::language::Diagnostic& Diagnostic : Program.Diagnostics)
        INFO(dbc::language::FormatDiagnostic(*Program.Source, Diagnostic));
    CHECK_FALSE(Program.HasErrors());
    REQUIRE(Program.Functions.size() == 1);
    CHECK(Program.Functions.front().Span.LogicalPath == "project/shared.dba");
}

TEST_CASE("Includes reject profile changes and cycles")
{
    const dbc::language::IncludeLoader ProfileLoader =
        [](const dbc::language::SourceDocument&, const std::filesystem::path&, std::string&)
    { return Source("print 1\n", dbc::language::LanguageProfile::DbPro, "shared.dba"); };
    const dbc::language::Program CrossProfile = dbc::language::Compile(
        Source("#include \"shared.dba\"\n", dbc::language::LanguageProfile::Dbc, "main.dba"), {}, ProfileLoader);
    CHECK(HasCode(CrossProfile, "DBC1224"));

    const dbc::language::IncludeLoader CycleLoader =
        [](const dbc::language::SourceDocument&, const std::filesystem::path&, std::string&)
    { return Source("#include \"main.dba\"\n", dbc::language::LanguageProfile::Dbc, "main.dba"); };
    const dbc::language::Program Cycle = dbc::language::Compile(
        Source("#include \"main.dba\"\n", dbc::language::LanguageProfile::Dbc, "main.dba"), {}, CycleLoader);
    CHECK(HasCode(Cycle, "DBC1225"));
}

TEST_CASE("AS declarations are not accepted by the Classic profile")
{
    const dbc::language::Program Classic =
        dbc::language::Compile(Source("dim values(3) as integer\n", dbc::language::LanguageProfile::Dbc));
    CHECK(HasCode(Classic, "DBC2046"));
}

TEST_CASE("Diagnostics are deterministic and source positioned")
{
    const auto Document = Source("print missing(1)\n", dbc::language::LanguageProfile::Dbc);
    const dbc::language::Program First = dbc::language::Compile(Document);
    const dbc::language::Program Second = dbc::language::Compile(Document);
    REQUIRE(First.Diagnostics.size() == Second.Diagnostics.size());
    REQUIRE_FALSE(First.Diagnostics.empty());
    for (std::size_t Index = 0; Index < First.Diagnostics.size(); ++Index)
    {
        CHECK(First.Diagnostics[Index].Code == Second.Diagnostics[Index].Code);
        CHECK(First.Diagnostics[Index].Message == Second.Diagnostics[Index].Message);
        CHECK(First.Diagnostics[Index].Span.Begin.Offset == Second.Diagnostics[Index].Span.Begin.Offset);
    }
    CHECK(First.Diagnostics.front().Span.Begin.Line == 1);
}

TEST_CASE("Labels are scoped to their entry block or function")
{
    const dbc::language::Program CrossScope = dbc::language::Compile(Source("function Jump()\n"
                                                                            "  goto outside\n"
                                                                            "endfunction\n"
                                                                            "outside:\n",
                                                                            dbc::language::LanguageProfile::Dbc));
    CHECK(HasCode(CrossScope, "DBC2010"));

    const dbc::language::Program LocalDuplicates = dbc::language::Compile(Source("function First()\n"
                                                                                 "again:\n"
                                                                                 "  exitfunction 1\n"
                                                                                 "endfunction\n"
                                                                                 "function Second()\n"
                                                                                 "again:\n"
                                                                                 "  exitfunction 2\n"
                                                                                 "endfunction\n",
                                                                                 dbc::language::LanguageProfile::Dbc));
    CHECK_FALSE(HasCode(LocalDuplicates, "DBC2006"));
}

TEST_CASE("The new parser accepts every BASIC regression source")
{
    const std::filesystem::path Root = std::filesystem::path(TEST_SOURCE_DIR) / "tests" / "dba";
    std::map<std::string, dbc::language::LanguageProfile> Profiles;
    {
        std::ifstream Input(Root / "test_profiles.tsv");
        for (std::string Line; std::getline(Input, Line);)
        {
            if (Line.empty() || Line.front() == '#')
                continue;
            const std::size_t Separator = Line.find('\t');
            REQUIRE(Separator != std::string::npos);
            Profiles.emplace(Line.substr(0, Separator), Line.substr(Separator + 1) == "dbc"
                                                            ? dbc::language::LanguageProfile::Dbc
                                                            : dbc::language::LanguageProfile::DbPro);
        }
    }
    std::size_t Parsed = 0;
    std::size_t Goldens = 0;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Root))
    {
        if (Entry.is_regular_file() && Entry.path().extension() == ".expected")
        {
            ++Goldens;
        }
        if (!Entry.is_regular_file() || !dbc::language::IsLanguageSource(Entry.path()) ||
            Entry.path().stem().string().rfind("test_", 0) != 0)
        {
            continue;
        }
        const auto Profile = Profiles.find(Entry.path().filename().string());
        REQUIRE(Profile != Profiles.end());
        const auto Document = std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{Entry.path(), ReadAll(Entry.path()), Profile->second});
        const dbc::language::Program Program = dbc::language::LexAndParse(Document);
        std::string Details = Entry.path().string();
        for (const auto& Diagnostic : Program.Diagnostics)
        {
            Details += "\n" + dbc::language::FormatDiagnostic(*Document, Diagnostic);
        }
        INFO(Details);
        CHECK_FALSE(Program.HasErrors());
        ++Parsed;
    }
    // Every golden has a source and every source is parsed: nothing is silently skipped.
    CHECK(Goldens > 0);
    CHECK(Parsed == Goldens);
}

namespace
{
    // Resolves #INCLUDE against the including file's directory, as the execution service does.
    std::shared_ptr<const dbc::language::SourceDocument> LoadIncludeFromDisk(
        const dbc::language::SourceDocument& Including, const std::filesystem::path& Include, std::string& Error)
    {
        const std::filesystem::path Path =
            Include.is_absolute() ? Include : Including.LogicalPath.parent_path() / Include;
        if (!dbc::language::IsLanguageSource(Path))
        {
            Error = "#INCLUDE must reference a .dba source: " + Path.string();
            return {};
        }
        if (!std::filesystem::is_regular_file(Path))
        {
            Error = "#INCLUDE not found: " + Path.string();
            return {};
        }
        return std::make_shared<const dbc::language::SourceDocument>(
            dbc::language::SourceDocument{Path, ReadAll(Path), Including.Profile});
    }

    struct CorpusExpectation
    {
        std::string Profile;
        std::string Stage;
        std::string Codes;

        bool operator==(const CorpusExpectation&) const = default;
    };

    std::map<std::string, CorpusExpectation> ReadCorpusExpectations(const std::filesystem::path& Path)
    {
        std::map<std::string, CorpusExpectation> Result;
        std::ifstream Input(Path);
        for (std::string Line; std::getline(Input, Line);)
        {
            if (Line.empty() || Line.front() == '#')
                continue;
            const std::size_t First = Line.find('|');
            const std::size_t Second = Line.find('|', First + 1);
            const std::size_t Third = Line.find('|', Second + 1);
            REQUIRE(First != std::string::npos);
            REQUIRE(Second != std::string::npos);
            REQUIRE(Third != std::string::npos);
            Result.emplace(Line.substr(0, First),
                           CorpusExpectation{Line.substr(First + 1, Second - First - 1),
                                             Line.substr(Second + 1, Third - Second - 1), Line.substr(Third + 1)});
        }
        return Result;
    }

    int StageRank(std::string_view Code)
    {
        const int Number = std::stoi(std::string(Code.substr(3)));
        if (Number < 1100)
            return 0;
        if (Number < 2000)
            return 1;
        if (Number < 3000)
            return 2;
        if (Number < 4000)
            return 3;
        return 4;
    }

    CorpusExpectation FailureExpectation(std::string Profile, const std::vector<dbc::language::Diagnostic>& Diagnostics)
    {
        int Rank = 5;
        std::vector<std::string> Codes;
        for (const auto& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Severity != dbc::language::DiagnosticSeverity::Error)
                continue;
            const int Candidate = StageRank(Diagnostic.Code);
            if (Candidate < Rank)
            {
                Rank = Candidate;
                Codes.clear();
            }
            if (Candidate == Rank)
                Codes.push_back(Diagnostic.Code);
        }
        std::sort(Codes.begin(), Codes.end());
        Codes.erase(std::unique(Codes.begin(), Codes.end()), Codes.end());
        std::string Joined;
        for (const std::string& Code : Codes)
        {
            if (!Joined.empty())
                Joined += ',';
            Joined += Code;
        }
        static constexpr std::string_view Stages[] = {"lex", "parse", "bind", "lower", "verify"};
        return {std::move(Profile), Rank < 5 ? std::string(Stages[Rank]) : std::string("verify"), std::move(Joined)};
    }
} // namespace

TEST_CASE("Every example has an exact parse bind lower and verify expectation")
{
    const std::filesystem::path Examples = std::filesystem::path(TEST_SOURCE_DIR) / "examples";
    const std::filesystem::path ExpectationsPath =
        std::filesystem::path(TEST_SOURCE_DIR) / "tests" / "unit" / "corpus_expectations.txt";
    const auto Expected = ReadCorpusExpectations(ExpectationsPath);
    REQUIRE_FALSE(Expected.empty());

    std::map<std::string, CorpusExpectation> Actual;
    std::size_t Verified = 0;
    for (const char* Root : {"dbc", "dbpro", "open-dbc"})
    {
        for (const std::filesystem::directory_entry& Entry :
             std::filesystem::recursive_directory_iterator(Examples / Root))
        {
            if (!Entry.is_regular_file() || !dbc::language::IsLanguageSource(Entry.path()))
                continue;
            const std::string Key = std::filesystem::relative(Entry.path(), Examples).generic_string();
            const auto ExpectedEntry = Expected.find(Key);
            REQUIRE(ExpectedEntry != Expected.end());
            const std::string Profile = ExpectedEntry->second.Profile;
            const dbc::language::LanguageProfile LanguageProfile =
                Profile == "dbc" ? dbc::language::LanguageProfile::Dbc : dbc::language::LanguageProfile::DbPro;
            const auto Document = std::make_shared<const dbc::language::SourceDocument>(
                dbc::language::SourceDocument{Entry.path(), ReadAll(Entry.path()), LanguageProfile});
            const dbc::language::Program Program =
                dbc::language::Compile(Document, {.RejectUnsupported = true}, LoadIncludeFromDisk);
            if (Program.HasErrors())
            {
                Actual.emplace(Key, FailureExpectation(Profile, Program.Diagnostics));
                continue;
            }
            const dbc::vm::CompileResult Lowered = dbc::vm::Lower(Program);
            if (!Lowered.Success())
            {
                Actual.emplace(Key, FailureExpectation(Profile, Lowered.Diagnostics));
                continue;
            }
            const dbc::vm::VerificationResult VerifiedProgram = dbc::vm::Verify(*Lowered.Bytecode);
            if (!VerifiedProgram.Success())
            {
                Actual.emplace(Key, FailureExpectation(Profile, VerifiedProgram.Diagnostics));
                continue;
            }
            Actual.emplace(Key, CorpusExpectation{Profile, "verified", "-"});
            ++Verified;
        }
    }

    std::string Report = "verified corpus: " + std::to_string(Verified) + " / " + std::to_string(Actual.size());
    for (const auto& [Path, ExpectedValue] : Expected)
    {
        const auto Found = Actual.find(Path);
        if (Found == Actual.end())
            Report += "\nMISSING " + Path;
        else if (!(Found->second == ExpectedValue))
            Report += "\nCHANGED " + Path + " expected " + ExpectedValue.Stage + "/" + ExpectedValue.Codes +
                      " actual " + Found->second.Stage + "/" + Found->second.Codes;
    }
    for (const auto& [Path, UnusedId] : Actual)
        if (!Expected.contains(Path))
            Report += "\nUNRECORDED " + Path;
    INFO(Report);
    CHECK(Actual == Expected);
}

TEST_CASE("The typed parser accepts every tracked BASIC example")
{
    const std::filesystem::path Examples = std::filesystem::path(TEST_SOURCE_DIR) / "examples";
    const auto Expected =
        ReadCorpusExpectations(std::filesystem::path(TEST_SOURCE_DIR) / "tests" / "unit" / "corpus_expectations.txt");
    const std::vector<std::filesystem::path> Roots = {
        Examples / "dbc",
        Examples / "dbpro",
        Examples / "open-dbc",
    };
    std::size_t Parsed = 0;
    for (const std::filesystem::path& Root : Roots)
    {
        for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(Root))
        {
            if (!Entry.is_regular_file() || !dbc::language::IsLanguageSource(Entry.path()))
            {
                continue;
            }
            const std::string Key = std::filesystem::relative(Entry.path(), Examples).generic_string();
            const auto ExpectedEntry = Expected.find(Key);
            REQUIRE(ExpectedEntry != Expected.end());
            const dbc::language::LanguageProfile Profile = ExpectedEntry->second.Profile == "dbc"
                                                               ? dbc::language::LanguageProfile::Dbc
                                                               : dbc::language::LanguageProfile::DbPro;
            const auto Document = std::make_shared<const dbc::language::SourceDocument>(
                dbc::language::SourceDocument{Entry.path(), ReadAll(Entry.path()), Profile});
            const dbc::language::Program Program = dbc::language::LexAndParse(Document);
            std::string Details = Entry.path().string();
            for (const auto& Diagnostic : Program.Diagnostics)
            {
                Details += "\n" + dbc::language::FormatDiagnostic(*Document, Diagnostic) + " [" +
                           std::string(dbc::language::Lexeme(*Document, Diagnostic.Span)) + "]";
            }
            const bool IsAmbiguousDbProOperator = Entry.path().filename() == "core1.dba";
            const bool UsesDbProPointerDereference = Entry.path().filename() == "basic2d1.dba";
            if (IsAmbiguousDbProOperator)
            {
                INFO(Details);
                REQUIRE(Program.Diagnostics.size() == 1);
                CHECK(Program.Diagnostics.front().Code == "DBC1004");
            }
            else if (UsesDbProPointerDereference)
            {
                // `col = *workptr` is the DBPro pointer dereference, not yet in the grammar.
                INFO(Details);
                CHECK(Program.HasErrors());
                for (const auto& Diagnostic : Program.Diagnostics)
                    CHECK((Diagnostic.Code == "DBC1101" || Diagnostic.Code == "DBC1105"));
            }
            else
            {
                INFO(Details);
                CHECK_FALSE(Program.HasErrors());
            }
            ++Parsed;
        }
    }
    CHECK(Parsed == Expected.size());
}

TEST_CASE("Blocks and expressions nest up to the limit on a 512 KB thread")
{
    const std::vector<std::string> Programs = NestedPrograms(dbc::language::KMaxNestingDepth);
    const std::vector<NestedBuild> Results = BuildOnSecondaryThread(Programs);
    REQUIRE(Results.size() == Programs.size());
    for (std::size_t Index = 0; Index < Results.size(); ++Index)
    {
        INFO("shape " << Index);
        CHECK(Results[Index].Codes.empty());
        CHECK(Results[Index].Lowered);
    }
}

TEST_CASE("Nesting past the limit reports one diagnostic and parses what follows")
{
    const std::vector<std::string> Programs = NestedPrograms(dbc::language::KMaxNestingDepth + 1);
    const std::vector<std::string> Expected = {"DBC1107", "DBC1107", "DBC1107", "DBC1107",
                                               "DBC1226", "DBC1226", "DBC1226", "DBC1107"};
    REQUIRE(Programs.size() == Expected.size());
    const std::vector<NestedBuild> Results = BuildOnSecondaryThread(Programs);
    REQUIRE(Results.size() == Programs.size());
    for (std::size_t Index = 0; Index < Results.size(); ++Index)
    {
        INFO("shape " << Index);
        CHECK(Results[Index].Codes == std::vector<std::string>{Expected[Index]});
    }

    const std::string Members = "x = a" + Repeat(".b", dbc::language::KMaxNestingDepth + 1) + "\n";
    const dbc::language::Program Program =
        dbc::language::LexAndParse(Source(Members, dbc::language::LanguageProfile::DbPro));
    REQUIRE(Program.Diagnostics.size() == 1);
    CHECK(Program.Diagnostics.front().Code == "DBC1107");
}

TEST_CASE("Numeric literals must fit 64-bit integers and finite doubles")
{
    const std::vector<std::string> Rejected = {
        "x = 9223372036854775808\n",       "x = 0x8000000000000000\n", "x = 0x10000000000000000\n",
        "x = %1" + Repeat("0", 63) + "\n", "x# = 1.0e999\n",
    };
    for (const std::string& Text : Rejected)
    {
        INFO(Text);
        const dbc::language::Program Program =
            dbc::language::Compile(Source(Text, dbc::language::LanguageProfile::DbPro));
        REQUIRE(Program.Diagnostics.size() == 1);
        CHECK(Program.Diagnostics.front().Code == "DBC1108");
    }
    const std::vector<std::string> Accepted = {
        "x = 9223372036854775807\n",
        "x = -9223372036854775807\n",
        "x = 0xFFFFFFFF\n",
        "x = %1" + Repeat("1", 62) + "\n",
        "x# = 1.5e308\n",
    };
    for (const std::string& Text : Accepted)
    {
        INFO(Text);
        CHECK_FALSE(dbc::language::Compile(Source(Text, dbc::language::LanguageProfile::DbPro)).HasErrors());
    }
}

TEST_CASE("A leading UTF-8 byte order mark is skipped")
{
    const auto Document = Source("\xEF\xBB\xBFprint 1\n", dbc::language::LanguageProfile::Dbc);
    const dbc::language::Program Program = dbc::language::Compile(Document);
    CHECK_FALSE(Program.HasErrors());
    REQUIRE_FALSE(Program.Tokens.empty());
    CHECK(dbc::language::Lexeme(*Document, Program.Tokens.front().Span) == "print");
}

TEST_CASE("An expression that ends early is reported at its last token")
{
    const dbc::language::Program Program =
        dbc::language::Compile(Source("print 1\nx = 2 +\n", dbc::language::LanguageProfile::Dbc));
    REQUIRE(HasCode(Program, "DBC1103"));
    for (const auto& Diagnostic : Program.Diagnostics)
    {
        if (Diagnostic.Code != "DBC1103")
            continue;
        CHECK(Diagnostic.Span.Begin.Line == 2);
        CHECK(Diagnostic.Span.Begin.Column == 7);
    }
}
