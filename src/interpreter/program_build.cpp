#include "execution_service.hpp"

#include "basic_path.hpp"
#include "execution_diagnostics.hpp"
#include "vm/compiler.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <unordered_map>
#include <utility>

#include <SDL3/SDL.h>

// Everything that needs the BASIC frontend. opendbc-runner prepares program images through
// ExecutionService::PrepareBytecode and never references this file.
namespace dbc::interpreter
{
    namespace
    {
        using detail::ConvertDiagnostics;
        using detail::HasErrors;
        using detail::InputDiagnostic;

        std::filesystem::path NormalizePath(const std::filesystem::path& Value)
        {
            return NormalizeBasicPath(Value);
        }

        std::string PathKey(const std::filesystem::path& Value)
        {
            return NormalizePath(Value).generic_string();
        }

        bool ValidateLogicalProfile(const std::filesystem::path& Path, language::LanguageProfile Profile,
                                    std::string& Error)
        {
            if (Path.empty())
            {
                Error = "Source logical path cannot be empty";
                return false;
            }
            if (Path.extension().empty())
                return true;
            if (!language::IsLanguageSource(Path))
            {
                Error = "BASIC source must use the .dba extension: " + Path.string();
                return false;
            }
            (void)Profile;
            return true;
        }

        std::size_t LineCount(std::string_view Text)
        {
            if (Text.empty())
                return 0;
            return static_cast<std::size_t>(std::count(Text.begin(), Text.end(), '\n')) +
                   static_cast<std::size_t>(Text.back() != '\n');
        }

        using BuiltinUseKey = std::pair<language::SymbolId, language::OverloadId>;

        void CollectExpressionBuiltins(const language::Expression& Expression, std::set<std::string>& Names,
                                       std::set<BuiltinUseKey>& Uses)
        {
            if (Expression.Builtin)
                Names.emplace(language::Symbol(*Expression.Builtin).CanonicalName);
            if (Expression.Builtin && Expression.Overload)
                Uses.emplace(*Expression.Builtin, *Expression.Overload);
            for (const std::unique_ptr<language::Expression>& Child : Expression.Children)
                CollectExpressionBuiltins(*Child, Names, Uses);
        }

        void CollectStatementBuiltins(const language::Statement& Statement, std::set<std::string>& Names,
                                      std::set<BuiltinUseKey>& Uses)
        {
            if (Statement.Builtin)
                Names.emplace(language::Symbol(*Statement.Builtin).CanonicalName);
            if (Statement.Builtin && Statement.Overload)
                Uses.emplace(*Statement.Builtin, *Statement.Overload);
            for (const std::unique_ptr<language::Expression>& Expression : Statement.Expressions)
                CollectExpressionBuiltins(*Expression, Names, Uses);
            for (const language::Statement& Child : Statement.Body)
                CollectStatementBuiltins(Child, Names, Uses);
            for (const language::Statement& Child : Statement.ElseBody)
                CollectStatementBuiltins(Child, Names, Uses);
            for (const language::CaseClause& Clause : Statement.Cases)
            {
                for (const std::unique_ptr<language::Expression>& Value : Clause.Values)
                    CollectExpressionBuiltins(*Value, Names, Uses);
                for (const language::Statement& Child : Clause.Body)
                    CollectStatementBuiltins(Child, Names, Uses);
            }
        }

        struct FrontendResult
        {
            language::Program Program{};
            SourceInspection Inspection{};
            std::vector<ExecutionDiagnostic> Diagnostics{};
        };

        FrontendResult CompileFrontend(ExecutionRequest& Request)
        {
            FrontendResult Result;
            Result.Inspection.RootLogicalPath = NormalizePath(Request.RootLogicalPath);
            Result.Inspection.Profile = Request.Profile;
            Result.Inspection.InputKind = Request.InputKind;
            Result.Inspection.DialectSource = Request.DialectSource;
            Result.Inspection.ProjectFile = Request.ProjectFile;
            Result.Inspection.OrderedSources =
                Request.OrderedProjectSources.empty()
                    ? std::vector<std::filesystem::path>{Result.Inspection.RootLogicalPath}
                    : Request.OrderedProjectSources;

            std::string Error;
            if (!ValidateLogicalProfile(Result.Inspection.RootLogicalPath, Request.Profile, Error))
            {
                Result.Diagnostics.push_back(
                    InputDiagnostic("DBC1000", std::move(Error), Result.Inspection.RootLogicalPath));
                return Result;
            }

            std::unordered_map<std::string, SourceUnit> Sources;
            Sources.reserve(Request.Sources.size());
            for (SourceUnit& Unit : Request.Sources)
            {
                Unit.LogicalPath = NormalizePath(Unit.LogicalPath);
                if (!ValidateLogicalProfile(Unit.LogicalPath, Request.Profile, Error))
                {
                    Result.Diagnostics.push_back(InputDiagnostic("DBC1001", std::move(Error), Unit.LogicalPath));
                    return Result;
                }
                const std::string Key = PathKey(Unit.LogicalPath);
                if (!Sources.emplace(Key, std::move(Unit)).second)
                {
                    Result.Diagnostics.push_back(
                        InputDiagnostic("DBC1002", "Duplicate source logical path '" + Key + "'", Key));
                    return Result;
                }
            }

            const std::string RootKey = PathKey(Result.Inspection.RootLogicalPath);
            const auto Root = Sources.find(RootKey);
            if (Root == Sources.end())
            {
                Result.Diagnostics.push_back(InputDiagnostic("DBC1003",
                                                             "Root source is not present in the in-memory source units",
                                                             Result.Inspection.RootLogicalPath));
                return Result;
            }

            std::set<std::string> Used{RootKey};
            std::string RootText = Root->second.Text;
            for (std::size_t Index = 1; Index < Request.OrderedProjectSources.size(); ++Index)
            {
                const std::filesystem::path UnitPath = NormalizePath(Request.OrderedProjectSources[Index]);
                if (UnitPath.generic_string().find('"') != std::string::npos)
                {
                    Result.Diagnostics.push_back(
                        InputDiagnostic("DBC1006", "Project source path cannot contain a quote", UnitPath));
                    return Result;
                }
                std::filesystem::path IncludePath =
                    UnitPath.lexically_relative(Result.Inspection.RootLogicalPath.parent_path());
                if (IncludePath.empty())
                    IncludePath = UnitPath;
                if (!RootText.empty() && RootText.back() != '\n')
                    RootText.push_back('\n');
                RootText += "#include \"" + IncludePath.generic_string() + "\"\n";
            }
            const auto RootDocument = std::make_shared<const language::SourceDocument>(
                language::SourceDocument{Root->second.LogicalPath, std::move(RootText), Request.Profile});
            const language::IncludeLoader IncludeLoader =
                [&Request, &Sources, &Used](const language::SourceDocument& Including,
                                            const std::filesystem::path& Include,
                                            std::string& LoadError) -> std::shared_ptr<const language::SourceDocument>
            {
                const std::filesystem::path NormalizedInclude = NormalizeBasicPath(Include);
                const bool IncludeIsAbsolute = NormalizedInclude.is_absolute();
                const std::filesystem::path LogicalPath = NormalizePath(
                    IncludeIsAbsolute ? NormalizedInclude : Including.LogicalPath.parent_path() / NormalizedInclude);
                if (Request.ConfinedSourceRoot)
                {
                    if (IncludeIsAbsolute)
                    {
                        LoadError = "#INCLUDE paths in an embedded artifact must be relative";
                        return {};
                    }
                    std::filesystem::path ConfinedPath;
                    if (!ResolveConfinedBasicPath(*Request.ConfinedSourceRoot, LogicalPath, ConfinedPath, &LoadError))
                    {
                        LoadError = "#INCLUDE in an embedded artifact must remain inside its payload: " + LoadError;
                        return {};
                    }
                }
                if (!ValidateLogicalProfile(LogicalPath, Request.Profile, LoadError))
                    return {};
                const std::string Key = PathKey(LogicalPath);
                auto Unit = Sources.find(Key);
                if (Unit == Sources.end() && Request.LoadMissingSource)
                {
                    std::optional<SourceUnit> Loaded =
                        Request.LoadMissingSource(LogicalPath, Request.Profile, LoadError);
                    if (!Loaded)
                    {
                        if (LoadError.empty())
                            LoadError = "Unable to load #INCLUDE '" + LogicalPath.string() + "'";
                        return {};
                    }
                    Loaded->LogicalPath = LogicalPath;
                    Unit = Sources.emplace(Key, std::move(*Loaded)).first;
                }
                if (Unit == Sources.end())
                {
                    LoadError = "#INCLUDE source is not present in memory: " + LogicalPath.string();
                    return {};
                }
                Used.insert(Key);
                return std::make_shared<const language::SourceDocument>(
                    language::SourceDocument{LogicalPath, Unit->second.Text, Request.Profile});
            };

            Result.Program = language::Compile(RootDocument, {.RejectUnsupported = true}, IncludeLoader);
            Result.Diagnostics = ConvertDiagnostics(Result.Program.Diagnostics, DiagnosticStage::Frontend);

            Result.Inspection.SourceCount = Used.size();
            for (const std::string& Key : Used)
            {
                const SourceUnit& Unit = Sources.at(Key);
                Result.Inspection.ByteCount += Unit.Text.size();
                Result.Inspection.LineCount += LineCount(Unit.Text);
            }
            if (!Result.Program.HasErrors())
            {
                std::set<std::string> Names;
                std::set<BuiltinUseKey> Uses;
                for (const language::Statement& Statement : Result.Program.Statements)
                    CollectStatementBuiltins(Statement, Names, Uses);
                for (const language::FunctionDeclaration& Function : Result.Program.Functions)
                {
                    for (const language::Statement& Statement : Function.Body)
                        CollectStatementBuiltins(Statement, Names, Uses);
                    if (Function.FinalReturn)
                        CollectExpressionBuiltins(*Function.FinalReturn, Names, Uses);
                }
                for (const auto& Constant : Result.Program.Constants)
                    if (Constant.Value)
                        CollectExpressionBuiltins(*Constant.Value, Names, Uses);
                Result.Inspection.Builtins.assign(Names.begin(), Names.end());
                for (const auto& [Symbol, Overload] : Uses)
                    Result.Inspection.BuiltinOverloads.push_back({Symbol, Overload});
            }
            return Result;
        }

        struct CompiledProgram
        {
            SourceInspection Inspection{};
            std::shared_ptr<vm::Module> Bytecode{};
            std::vector<ExecutionDiagnostic> Diagnostics{};
        };

        // Frontend, lowering and optionally verification; Bytecode is set only when every stage succeeded.
        CompiledProgram CompileProgram(ExecutionRequest& Request, bool Verify)
        {
            FrontendResult Frontend = CompileFrontend(Request);
            CompiledProgram Result{std::move(Frontend.Inspection), {}, std::move(Frontend.Diagnostics)};
            if (HasErrors(Result.Diagnostics))
                return Result;

            vm::CompileResult Compiled = vm::Lower(Frontend.Program);
            if (!Compiled.Success())
            {
                std::vector<ExecutionDiagnostic> Diagnostics =
                    ConvertDiagnostics(Compiled.Diagnostics, DiagnosticStage::Compiler);
                Result.Diagnostics.insert(Result.Diagnostics.end(), std::make_move_iterator(Diagnostics.begin()),
                                          std::make_move_iterator(Diagnostics.end()));
                return Result;
            }
            if (Verify)
            {
                std::vector<ExecutionDiagnostic> Diagnostics = VerifyBytecode(*Compiled.Bytecode);
                Result.Diagnostics.insert(Result.Diagnostics.end(), std::make_move_iterator(Diagnostics.begin()),
                                          std::make_move_iterator(Diagnostics.end()));
                if (HasErrors(Result.Diagnostics))
                    return Result;
            }
            Result.Bytecode = std::move(Compiled.Bytecode);
            return Result;
        }

    } // namespace

    bool ExecutionService::Prepare(ExecutionRequest Request)
    {
        if (Request.Bytecode)
            return PrepareBytecode(std::move(Request));
        const auto Started = std::chrono::steady_clock::now();
        // Start verifies the bytecode, so compilation skips its own verification pass.
        CompiledProgram Compiled = CompileProgram(Request, false);
        Request.Bytecode = std::move(Compiled.Bytecode);
        return Start(std::move(Request), std::move(Compiled.Diagnostics), Started);
    }

    std::optional<SourceUnit> LoadSourceUnitFromDisk(const std::filesystem::path& LogicalPath,
                                                     language::LanguageProfile Profile, std::string& Error)
    {
        const std::filesystem::path NormalizedPath = NormalizeBasicPath(LogicalPath);
        if (!language::IsLanguageSource(NormalizedPath))
        {
            Error = "BASIC source must use the .dba extension: " + NormalizedPath.string();
            return std::nullopt;
        }
        (void)Profile;
        std::size_t Size = 0;
        void* Bytes = SDL_LoadFile(NormalizedPath.string().c_str(), &Size);
        if (Bytes == nullptr)
        {
            Error = "Failed to read source file '" + NormalizedPath.string() + "'";
            return std::nullopt;
        }
        const std::string Text(static_cast<const char*>(Bytes), Size);
        SDL_free(Bytes);
        return SourceUnit{NormalizePath(NormalizedPath), Text};
    }

    SourceAnalysis AnalyzeSources(ExecutionRequest Request)
    {
        CompiledProgram Compiled = CompileProgram(Request, true);
        return {std::move(Compiled.Inspection), std::move(Compiled.Diagnostics)};
    }

    ProgramBuild BuildProgram(ExecutionRequest Request)
    {
        CompiledProgram Compiled = CompileProgram(Request, true);
        ProgramBuild Result{std::move(Compiled.Bytecode), std::move(Compiled.Diagnostics)};
        if (HasErrors(Result.Diagnostics))
            Result.Bytecode.reset();
        return Result;
    }

} // namespace dbc::interpreter
