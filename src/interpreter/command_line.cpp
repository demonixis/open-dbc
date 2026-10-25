#include "command_line.hpp"
#include "atomic_file.hpp"
#include "basic_path.hpp"

#include "engine/systems/core_system.hpp"
#include "engine/systems/debug_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/vr_system.hpp"
#include "runtime_startup.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <limits>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dbc::interpreter
{
    namespace
    {
        constexpr std::string_view HelpText =
            "Usage:\n"
            "  opendbc                              Open the Studio\n"
            "  opendbc studio [file-or-directory]   Open the Studio explicitly\n"
            "  opendbc run <source.dba|project.dbpro> [--dialect <dbc|dbpro>] [options]\n"
            "  opendbc check <source.dba|project.dbpro> [--dialect <dbc|dbpro>]\n"
            "  opendbc inspect <source.dba|project.dbpro> [--json] [--dialect <dbc|dbpro>]\n"
            "  opendbc export <source.dba|project.dbpro> --dialect <dbc|dbpro>\n"
            "                 --target <desktop|ios|ios-simulator|android-apk|android-aab>\n"
            "                 --output <directory> [--unsigned]\n"
            "\n"
            "Run options:\n"
            "  --dialect <dbc|dbpro>\n"
            "  --asset-root <path>\n"
            "  --command-line <text>\n"
            "  --log-output <path>\n"
            "  --builtin-coverage-output <path>\n"
            "  --log-level <trace|debug|info|warn|error>\n"
            "  --max-frames <count>\n"
            "  --instruction-budget <count>\n"
            "  --debug-overlay\n"
            "  --headless (hidden window, offscreen rendering, silent audio)\n"
            "  --capture-output <path.bmp> (requires --headless; saves the last frame)\n"
            "  --benchmark\n"
            "  --benchmark-output <path> (requires --benchmark)\n";

        void WriteStandard(std::FILE* File, std::string_view Text)
        {
            std::fwrite(Text.data(), 1, Text.size(), File);
            std::fflush(File);
        }

        void Write(const std::function<void(std::string_view)>& Sink, std::FILE* Default, std::string_view Text)
        {
            if (Sink)
                Sink(Text);
            else
                WriteStandard(Default, Text);
        }

        bool ParseNonNegative(std::string_view Text, std::size_t& Value)
        {
            if (Text.empty() || Text.front() == '-')
                return false;
            const char* Begin = Text.data();
            const char* End = Text.data() + Text.size();
            const auto [After, Error] = std::from_chars(Begin, End, Value);
            return Error == std::errc{} && After == End;
        }

        int Exit(CommandLineExitCode Value)
        {
            return static_cast<int>(Value);
        }

        std::atomic_bool Interrupted = false;

        void InterruptHandler(int)
        {
            Interrupted.store(true, std::memory_order_relaxed);
        }

        bool IsRunOption(std::string_view Value)
        {
            return Value == "--asset-root" || Value == "--command-line" || Value == "--log-output" ||
                   Value == "--builtin-coverage-output" || Value == "--log-level" || Value == "--max-frames" ||
                   Value == "--instruction-budget" || Value == "--debug-overlay" || Value == "--headless" ||
                   Value == "--benchmark" || Value == "--benchmark-output" || Value == "--capture-output";
        }

        std::string JsonEscape(std::string_view Text)
        {
            std::string Result;
            Result.reserve(Text.size() + 8);
            constexpr char Hex[] = "0123456789abcdef";
            for (const unsigned char Character : Text)
            {
                switch (Character)
                {
                    case '"':
                        Result += "\\\"";
                        break;
                    case '\\':
                        Result += "\\\\";
                        break;
                    case '\b':
                        Result += "\\b";
                        break;
                    case '\f':
                        Result += "\\f";
                        break;
                    case '\n':
                        Result += "\\n";
                        break;
                    case '\r':
                        Result += "\\r";
                        break;
                    case '\t':
                        Result += "\\t";
                        break;
                    default:
                        if (Character < 0x20)
                        {
                            Result += "\\u00";
                            Result += Hex[Character >> 4];
                            Result += Hex[Character & 0x0f];
                        }
                        else
                        {
                            Result += static_cast<char>(Character);
                        }
                        break;
                }
            }
            return Result;
        }

        std::string FormatDiagnostic(const ExecutionDiagnostic& Value)
        {
            std::ostringstream Result;
            if (!Value.LogicalPath.empty())
            {
                Result << Value.LogicalPath.string();
                if (Value.Begin.Line > 0)
                    Result << ':' << Value.Begin.Line << ':' << Value.Begin.Column;
                Result << ": ";
            }
            if (!Value.Code.empty())
                Result << Value.Code << ": ";
            Result << Value.Message << '\n';
            return Result.str();
        }

        std::string_view ProfileId(language::LanguageProfile Profile)
        {
            return Profile == language::LanguageProfile::Dbc ? "dbc" : "dbpro";
        }

        std::filesystem::path EvidencePathIdentity(const std::filesystem::path& Path)
        {
            std::error_code Error;
            std::filesystem::path Result = std::filesystem::absolute(Path, Error);
            if (Error)
                Result = Path;
            Error.clear();
            const std::filesystem::path Canonical = std::filesystem::weakly_canonical(Result, Error);
            return (Error ? Result : Canonical).lexically_normal();
        }

        bool EvidencePathsAlias(const std::filesystem::path& Left, const std::filesystem::path& Right)
        {
            if (Left.empty() || Right.empty())
                return false;
            std::error_code Error;
            if (std::filesystem::exists(Left, Error) && !Error)
            {
                Error.clear();
                if (std::filesystem::exists(Right, Error) && !Error)
                {
                    Error.clear();
                    if (std::filesystem::equivalent(Left, Right, Error) && !Error)
                        return true;
                }
            }
            const std::filesystem::path LeftIdentity = EvidencePathIdentity(Left);
            const std::filesystem::path RightIdentity = EvidencePathIdentity(Right);
#if defined(_WIN32)
            std::wstring LeftText = LeftIdentity.native();
            std::wstring RightText = RightIdentity.native();
            std::transform(LeftText.begin(), LeftText.end(), LeftText.begin(),
                           [](wchar_t Value) { return std::towlower(Value); });
            std::transform(RightText.begin(), RightText.end(), RightText.begin(),
                           [](wchar_t Value) { return std::towlower(Value); });
            return LeftText == RightText;
#else
            return LeftIdentity == RightIdentity;
#endif
        }

        bool ValidateRunOutputPaths(const ExecutionOptions& Options, std::string& Error)
        {
            if (EvidencePathsAlias(Options.BenchmarkOutputPath, Options.BuiltinCoverageOutputPath))
            {
                Error = "--benchmark-output and --builtin-coverage-output must use different paths";
                return false;
            }
            if (EvidencePathsAlias(Options.LogOutputPath, Options.BuiltinCoverageOutputPath))
            {
                Error = "--log-output and --builtin-coverage-output must use different paths";
                return false;
            }
            if (EvidencePathsAlias(Options.LogOutputPath, Options.BenchmarkOutputPath))
            {
                Error = "--log-output and --benchmark-output must use different paths";
                return false;
            }
            return true;
        }

        bool AtomicWriteJson(const std::filesystem::path& Path, const nlohmann::ordered_json& Json, std::string& Error)
        {
            // Serialization is the third-party exception boundary; file failures use explicit status.
            try
            {
                return AtomicWriteText(Path, Json.dump(2) + '\n', &Error);
            }
            catch (const nlohmann::json::exception& Exception)
            {
                Error = "Cannot serialize evidence output '" + Path.string() + "': " + Exception.what();
                return false;
            }
        }

        nlohmann::ordered_json MakeCoverageJson(language::LanguageProfile Profile, const std::filesystem::path& Entry,
                                                std::string_view Status, const ExecutionResult* Result)
        {
            nlohmann::ordered_json Json = nlohmann::ordered_json::object();
            Json["schema"] = "opendbc-builtin-coverage";
            Json["version"] = 1;
            Json["profile"] = ProfileId(Profile);
            Json["entry"] = Entry.generic_string();
            Json["status"] = Status;
            Json["frames"] = Result ? Result->FrameCount : 0;

            std::vector<BuiltinCoverageEntry> Coverage =
                Result ? Result->BuiltinCoverage : std::vector<BuiltinCoverageEntry>{};
            std::sort(Coverage.begin(), Coverage.end(),
                      [](const BuiltinCoverageEntry& Left, const BuiltinCoverageEntry& Right)
                      {
                          if (Left.Symbol != Right.Symbol)
                              return static_cast<std::uint32_t>(Left.Symbol) < static_cast<std::uint32_t>(Right.Symbol);
                          return Left.Overload < Right.Overload;
                      });
            std::uint64_t Attempts = 0;
            std::uint64_t Completed = 0;
            std::uint64_t Failed = 0;
            std::size_t AttemptedOverloads = 0;
            std::size_t CompletedOverloads = 0;
            std::size_t FailedOverloads = 0;
            Json["builtins"] = nlohmann::ordered_json::array();
            for (const BuiltinCoverageEntry& Value : Coverage)
            {
                const language::SymbolDescriptor& Descriptor = language::Symbol(Value.Symbol);
                nlohmann::ordered_json Item = nlohmann::ordered_json::object();
                Item["symbol_id"] = static_cast<std::uint32_t>(Value.Symbol);
                Item["overload_id"] = Value.Overload.Value;
                Item["name"] = Descriptor.CanonicalName;
                Item["kind"] = Descriptor.Kind == language::SymbolKind::Command ? "Command" : "Function";
                const language::SupportStatus Support = language::SupportFor(Descriptor, Profile);
                Item["support"] =
                    Support == language::SupportStatus::CompatibilityStub ? "compatibility_stub" : "implemented";
                if (Support == language::SupportStatus::CompatibilityStub)
                {
                    Item["stub_policy"] =
                        language::StubPolicyFor(Descriptor, Profile) == language::CompatibilityStubPolicy::Permanent
                            ? "permanent"
                            : "temporary";
                }
                else
                {
                    Item["stub_policy"] = nullptr;
                }
                Item["attempts"] = Value.Attempts;
                Item["completed"] = Value.Completed;
                Item["failed"] = Value.Failed;
                Item["covered"] = Value.Completed > 0;
                Json["builtins"].push_back(std::move(Item));
                Attempts += Value.Attempts;
                Completed += Value.Completed;
                Failed += Value.Failed;
                AttemptedOverloads += static_cast<std::size_t>(Value.Attempts > 0);
                CompletedOverloads += static_cast<std::size_t>(Value.Completed > 0);
                FailedOverloads += static_cast<std::size_t>(Value.Failed > 0);
            }
            Json["summary"] = {
                {"attempts", Attempts},
                {"completed", Completed},
                {"failed", Failed},
                {"attempted_overloads", AttemptedOverloads},
                {"completed_overloads", CompletedOverloads},
                {"failed_overloads", FailedOverloads},
            };
            return Json;
        }

        nlohmann::ordered_json MakeBenchmarkJson(language::LanguageProfile Profile, const std::filesystem::path& Entry,
                                                 std::string_view Status, const ExecutionResult* Result,
                                                 const EngineKernel* Kernel)
        {
            const BenchmarkResult Value = Result ? Result->Benchmark : BenchmarkResult{};
            nlohmann::ordered_json Json = nlohmann::ordered_json::object();
            Json["schema"] = "opendbc-benchmark";
            Json["version"] = 1;
            Json["profile"] = ProfileId(Profile);
            Json["entry"] = Entry.generic_string();
            Json["status"] = Status;
            Json["prepare_ms"] = Value.PrepareMilliseconds;
            Json["total_ms"] = Value.TotalMilliseconds;
            Json["frame_count"] = Value.FrameCount;
            Json["frame_ms"] = {
                {"average", Value.AverageFrameMilliseconds}, {"minimum", Value.MinimumFrameMilliseconds},
                {"p50", Value.P50FrameMilliseconds},         {"p95", Value.P95FrameMilliseconds},
                {"p99", Value.P99FrameMilliseconds},         {"maximum", Value.MaximumFrameMilliseconds},
            };
            if (Kernel)
            {
                const FrameStats& Stats = Kernel->Debug().CurrentStats();
                Json["renderer"] = {
                    {"triangles", Kernel->Renderer().LastFramePolygons()},
                    {"draw_calls", Kernel->Renderer().LastFrameDrawCalls()},
                    {"fps", Stats.Fps},
                    {"vertices", Stats.TotalVertices},
                    {"batches", Stats.TotalBatches},
                    {"visible_objects", Stats.VisibleObjects},
                    {"total_objects", Stats.TotalObjects},
                };
            }
            else
            {
                Json["renderer"] = nullptr;
            }
            return Json;
        }

        bool WriteRunEvidence(const CommandLine& Command, language::LanguageProfile Profile,
                              const std::filesystem::path& Entry, std::string_view Status,
                              const ExecutionResult* Result, const EngineKernel* Kernel, std::string& Error)
        {
            if (!Command.Execution.BenchmarkOutputPath.empty() && !Command.Execution.Benchmark)
            {
                Error = "--benchmark-output requires --benchmark";
                return false;
            }
            if (!ValidateRunOutputPaths(Command.Execution, Error))
                return false;
            if (!Command.Execution.BuiltinCoverageOutputPath.empty() &&
                !AtomicWriteJson(Command.Execution.BuiltinCoverageOutputPath,
                                 MakeCoverageJson(Profile, Entry, Status, Result), Error))
            {
                return false;
            }
            // Recheck after the first publication so case-folding or Unicode aliases on the
            // destination volume cannot make the benchmark replace the coverage evidence.
            if (!ValidateRunOutputPaths(Command.Execution, Error))
                return false;
            if (!Command.Execution.BenchmarkOutputPath.empty() &&
                !AtomicWriteJson(Command.Execution.BenchmarkOutputPath,
                                 MakeBenchmarkJson(Profile, Entry, Status, Result, Kernel), Error))
            {
                return false;
            }
            return true;
        }

        void WriteDiagnostics(const std::vector<ExecutionDiagnostic>& Diagnostics,
                              const CommandLineEnvironment& Environment)
        {
            for (const ExecutionDiagnostic& Diagnostic : Diagnostics)
            {
                Write(Environment.StandardError, stderr, FormatDiagnostic(Diagnostic));
            }
        }

        struct DiskRequestResult
        {
            std::optional<ExecutionRequest> Request{};
            std::optional<ResolvedProject> Project{};
            std::string Error{};
        };

        bool StandardInputIsTerminal()
        {
#if defined(_WIN32)
            return _isatty(_fileno(stdin)) != 0;
#else
            return isatty(fileno(stdin)) != 0;
#endif
        }

        std::optional<language::LanguageProfile> PromptDialect(const language::DialectDetection&)
        {
            if (!StandardInputIsTerminal())
                return std::nullopt;
            WriteStandard(stderr, "Dialect is ambiguous. Select DBC [1] or DBPro [2]: ");
            char Answer[32]{};
            if (std::fgets(Answer, sizeof(Answer), stdin) == nullptr)
                return std::nullopt;
            if (Answer[0] == '1' || Answer[0] == 'd' || Answer[0] == 'D')
                return language::LanguageProfile::Dbc;
            if (Answer[0] == '2' || Answer[0] == 'p' || Answer[0] == 'P')
                return language::LanguageProfile::DbPro;
            return std::nullopt;
        }

        std::optional<std::filesystem::path> PromptProject(std::span<const std::filesystem::path> Candidates)
        {
            if (!StandardInputIsTerminal())
                return std::nullopt;
            WriteStandard(stderr, "Multiple DBPro projects found:\n");
            for (std::size_t Index = 0; Index < Candidates.size(); ++Index)
                WriteStandard(stderr,
                              "  " + std::to_string(Index + 1) + ": " + Candidates[Index].filename().string() + "\n");
            WriteStandard(stderr, "Project number: ");
            char Answer[32]{};
            if (std::fgets(Answer, sizeof(Answer), stdin) == nullptr)
                return std::nullopt;
            std::string_view Value = Answer;
            while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.back())))
                Value.remove_suffix(1);
            std::size_t Selection = 0;
            if (!ParseNonNegative(Value, Selection) || Selection == 0 || Selection > Candidates.size())
                return std::nullopt;
            return Candidates[Selection - 1];
        }

        ProjectResolveResult ResolveCommandProject(const CommandLine& Command,
                                                   const CommandLineEnvironment& Environment)
        {
            ProjectResolveRequest Request;
            Request.Target = Command.Path.value_or(std::filesystem::path{});
            Request.ForcedProfile = Command.RequestedProfile;
            Request.SelectDialect = Environment.SelectDialect ? Environment.SelectDialect : PromptDialect;
            Request.SelectProject = Environment.SelectProject ? Environment.SelectProject : PromptProject;
            Request.RemapPath = Environment.RemapProjectPath;
            Request.ReadProjectConfiguration = Environment.OrderedProjectSources.empty();
            if (!Command.Execution.AssetRoot.empty())
            {
                std::error_code Error;
                Request.AssetRootOverride = std::filesystem::absolute(Command.Execution.AssetRoot, Error);
                if (Error)
                {
                    ProjectResolveResult Result;
                    Result.Error = "Cannot resolve asset root: " + Error.message();
                    return Result;
                }
            }
            return ResolveProject(Request);
        }

        DiskRequestResult MakeDiskRequest(const CommandLine& Command, const CommandLineEnvironment& Environment)
        {
            if (!Command.Path)
                return {{}, {}, "Missing source path"};
            ProjectResolveResult Resolved = ResolveCommandProject(Command, Environment);
            if (!Resolved)
                return {{}, {}, std::move(Resolved.Error)};
            ResolvedProject Project = std::move(*Resolved.Project);
            if (!Environment.OrderedProjectSources.empty())
                Project.OrderedSources = Environment.OrderedProjectSources;

            ExecutionRequest Request;
            Request.RootLogicalPath = Project.EntrySource;
            Request.Profile = Project.Profile;
            Request.OrderedProjectSources = Project.OrderedSources;
            Request.InputKind = std::string(ProjectInputKindName(Project.InputKind));
            Request.DialectSource = std::string(DialectOriginName(Project.ProfileOrigin));
            Request.ProjectFile = Project.DescriptorPath;
            for (const std::filesystem::path& Source : Project.OrderedSources)
            {
                std::string Error;
                std::filesystem::path ConfinedSource = Source;
                if (Environment.ConfinedSourceRoot &&
                    !ResolveConfinedBasicPath(*Environment.ConfinedSourceRoot, Source, ConfinedSource, &Error))
                    return {{}, {}, std::move(Error)};
                std::optional<SourceUnit> Unit = LoadSourceUnitFromDisk(Source, Project.Profile, Error);
                if (!Unit)
                    return {{}, {}, std::move(Error)};
                Request.Sources.push_back(std::move(*Unit));
            }
            Request.Options = Command.Execution;
            if (Request.Options.AssetRoot.empty())
                Request.Options.AssetRoot = Project.AssetRoot;
            Request.Options.CommandLine = Command.CommandLineOverride.value_or(Project.Startup.CommandLine);
            Request.LoadMissingSource = LoadSourceUnitFromDisk;
            Request.ConfinedSourceRoot = Environment.ConfinedSourceRoot;
            return {std::move(Request), std::move(Project), {}};
        }

        int DispatchAnalysis(const CommandLine& Command, const CommandLineEnvironment& Environment)
        {
            DiskRequestResult Loaded = MakeDiskRequest(Command, Environment);
            if (!Loaded.Request)
            {
                Write(Environment.StandardError, stderr, Loaded.Error + "\n");
                return Exit(CommandLineExitCode::Target);
            }
            SourceAnalysis Analysis = AnalyzeSources(std::move(*Loaded.Request));
            if (!Analysis.Diagnostics.empty())
                WriteDiagnostics(Analysis.Diagnostics, Environment);
            if (!Analysis.Success())
                return Exit(CommandLineExitCode::Source);
            if (Command.Action == CommandLineAction::Check)
                return Exit(CommandLineExitCode::Success);

            const SourceInspection& Value = Analysis.Inspection;
            std::ostringstream Output;
            if (Command.Json)
            {
                Output << "{\"path\":\"" << JsonEscape(Value.RootLogicalPath.generic_string()) << "\","
                       << "\"inputKind\":\"" << JsonEscape(Value.InputKind) << "\","
                       << "\"dialect\":\"" << (Value.Profile == language::LanguageProfile::Dbc ? "dbc" : "dbpro")
                       << "\","
                       << "\"dialectSource\":\"" << JsonEscape(Value.DialectSource) << "\","
                       << "\"entry\":\"" << JsonEscape(Value.RootLogicalPath.generic_string()) << "\","
                       << "\"projectFile\":";
                if (Value.ProjectFile)
                    Output << '"' << JsonEscape(Value.ProjectFile->generic_string()) << '"';
                else
                    Output << "null";
                Output << ",\"orderedSources\":[";
                for (std::size_t Index = 0; Index < Value.OrderedSources.size(); ++Index)
                {
                    if (Index > 0)
                        Output << ',';
                    Output << '"' << JsonEscape(Value.OrderedSources[Index].generic_string()) << '"';
                }
                Output << "],\"profile\":\"" << JsonEscape(language::ProfileName(Value.Profile)) << "\","
                       << "\"sources\":" << Value.SourceCount << ',' << "\"bytes\":" << Value.ByteCount << ','
                       << "\"lines\":" << Value.LineCount << ",\"builtins\":[";
                for (std::size_t Index = 0; Index < Value.Builtins.size(); ++Index)
                {
                    if (Index > 0)
                        Output << ',';
                    Output << '"' << JsonEscape(Value.Builtins[Index]) << '"';
                }
                Output << "],\"builtin_overloads\":[";
                for (std::size_t Index = 0; Index < Value.BuiltinOverloads.size(); ++Index)
                {
                    if (Index > 0)
                        Output << ',';
                    const SourceInspection::BuiltinUse& Use = Value.BuiltinOverloads[Index];
                    const language::SymbolDescriptor& Descriptor = language::Symbol(Use.Symbol);
                    const language::SupportStatus Support = language::SupportFor(Descriptor, Value.Profile);
                    const language::CompatibilityStubPolicy StubPolicy =
                        language::StubPolicyFor(Descriptor, Value.Profile);
                    Output << "{\"symbol_id\":" << static_cast<std::uint32_t>(Use.Symbol)
                           << ",\"overload_id\":" << Use.Overload.Value << ",\"kind\":\""
                           << (Descriptor.Kind == language::SymbolKind::Command ? "Command" : "Function")
                           << "\",\"name\":\"" << JsonEscape(Descriptor.CanonicalName) << "\",\"support\":\""
                           << (Support == language::SupportStatus::CompatibilityStub ? "Compatibility stub"
                                                                                     : "Implemented")
                           << "\",\"stub_policy\":\""
                           << (StubPolicy == language::CompatibilityStubPolicy::Permanent
                                   ? "Permanent"
                                   : (StubPolicy == language::CompatibilityStubPolicy::Temporary ? "Temporary"
                                                                                                 : "None"))
                           << "\"}";
                }
                Output << "]}\n";
            }
            else
            {
                Output << "Path: " << Value.RootLogicalPath.string() << '\n'
                       << "Input: " << Value.InputKind << '\n'
                       << "Profile: " << language::ProfileName(Value.Profile) << '\n'
                       << "Dialect source: " << Value.DialectSource << '\n'
                       << "Sources: " << Value.SourceCount << '\n'
                       << "Bytes: " << Value.ByteCount << '\n'
                       << "Lines: " << Value.LineCount << '\n'
                       << "Builtins:";
                if (Value.Builtins.empty())
                    Output << " none";
                Output << '\n';
                for (const std::string& Name : Value.Builtins)
                    Output << "  " << Name << '\n';
                std::set<language::SymbolId> StubSymbols;
                for (const SourceInspection::BuiltinUse& Use : Value.BuiltinOverloads)
                {
                    const language::SymbolDescriptor& Descriptor = language::Symbol(Use.Symbol);
                    if (language::SupportFor(Descriptor, Value.Profile) == language::SupportStatus::CompatibilityStub)
                        StubSymbols.insert(Use.Symbol);
                }
                Output << "Compatibility stubs:";
                if (StubSymbols.empty())
                    Output << " none";
                Output << '\n';
                for (const language::SymbolId Symbol : StubSymbols)
                {
                    const language::SymbolDescriptor& Descriptor = language::Symbol(Symbol);
                    Output << "  " << Descriptor.CanonicalName << " ("
                           << (language::StubPolicyFor(Descriptor, Value.Profile) ==
                                       language::CompatibilityStubPolicy::Permanent
                                   ? "permanent"
                                   : "temporary")
                           << ")\n";
                }
            }
            Write(Environment.StandardOutput, stdout, Output.str());
            return Exit(CommandLineExitCode::Success);
        }

        int DispatchRun(const CommandLine& Command, const CommandLineEnvironment& Environment)
        {
            DiskRequestResult Loaded = MakeDiskRequest(Command, Environment);
            if (!Loaded.Request)
            {
                Write(Environment.StandardError, stderr, Loaded.Error + "\n");
                return Exit(CommandLineExitCode::Target);
            }
            const language::LanguageProfile Profile = Loaded.Request->Profile;
            const std::filesystem::path Entry = Loaded.Request->RootLogicalPath;
            const auto Finish = [&](CommandLineExitCode Code, std::string_view Status,
                                    const ExecutionResult* Result = nullptr, const EngineKernel* Kernel = nullptr)
            {
                std::string EvidenceError;
                if (!WriteRunEvidence(Command, Profile, Entry, Status, Result, Kernel, EvidenceError))
                {
                    Write(Environment.StandardError, stderr, EvidenceError + "\n");
                    if (Code == CommandLineExitCode::Success)
                        return Exit(CommandLineExitCode::Runtime);
                }
                return Exit(Code);
            };

            std::filesystem::path LogDirectory = Loaded.Request->RootLogicalPath.parent_path();
#if defined(DBC_MOBILE)
            if (char* PreferencePath = SDL_GetPrefPath("OpenDBC", "OpenDBC"))
            {
                LogDirectory = PreferencePath;
                SDL_free(PreferencePath);
            }
#endif
            if (LogDirectory.empty())
                LogDirectory = ".";
            dbc::log::Init(Command.LogLevel, LogDirectory.string());

            for (const std::string& Blocker : Loaded.Project->RunBlockers)
            {
                Write(Environment.StandardError, stderr, "DBPro project option blocks execution: " + Blocker + "\n");
                return Finish(CommandLineExitCode::Target, "blocked");
            }
            const ProjectStartup Startup = Loaded.Project->Startup;
            if (Command.Headless)
                SDL_SetHint(KHeadlessHint, "1");
            EngineKernel Kernel;
            if (!Kernel.Initialize(Startup.Title, Startup.Width, Startup.Height))
            {
                Write(Environment.StandardError, stderr, "Failed to create the engine kernel\n");
                return Finish(CommandLineExitCode::Runtime, "engine-initialization-failed");
            }
            struct Shutdown
            {
                EngineKernel& Kernel;
                ~Shutdown()
                {
                    Kernel.Shutdown();
                }
            } ShutdownGuard{Kernel};

            if (Environment.StandardOutput)
            {
                Loaded.Request->Options.Output = [&Environment](std::string_view Text)
                {
                    Environment.StandardOutput(Text);
                    Environment.StandardOutput("\n");
                };
            }
            ExecutionService Service(Kernel);
            if (!Service.Prepare(std::move(*Loaded.Request)))
            {
                WriteDiagnostics(Service.Result().Diagnostics, Environment);
                const bool TargetFailure =
                    std::any_of(Service.Result().Diagnostics.begin(), Service.Result().Diagnostics.end(),
                                [](const ExecutionDiagnostic& Value) { return Value.Stage == DiagnosticStage::Input; });
                return Finish(TargetFailure ? CommandLineExitCode::Target : CommandLineExitCode::Source, "failed",
                              &Service.Result(), &Kernel);
            }
            const RuntimeStartupResult Runtime = InitializeProjectRuntime(Kernel, Startup);
            if (!Runtime)
            {
                Write(Environment.StandardError, stderr, Runtime.Diagnostic + "\n");
                Service.RequestStop();
                Service.Tick(1);
                return Finish(CommandLineExitCode::Runtime, "runtime-startup-failed", &Service.Result(), &Kernel);
            }
            Kernel.Core().HideMouse();
            if (Command.Execution.Benchmark)
            {
                Kernel.Core().DisableVSync();
                Service.StartFrameMeasurements();
            }
            if (Command.DebugOverlay)
                Kernel.Debug().SetOverlayEnabled(true);

            Interrupted.store(false, std::memory_order_relaxed);
            const auto PreviousHandler = std::signal(SIGINT, InterruptHandler);
            struct RestoreSignal
            {
                decltype(PreviousHandler) Previous;
                ~RestoreSignal()
                {
                    std::signal(SIGINT, Previous);
                }
            } RestoreSignalGuard{PreviousHandler};
            while (!Service.Result().Finished())
            {
                if (Interrupted.load(std::memory_order_relaxed))
                {
                    Service.RequestStop();
                    Service.Tick(1);
                    return Finish(CommandLineExitCode::Interrupted, "cancelled", &Service.Result(), &Kernel);
                }
                Kernel.Input().PollEvents();
                if (!Kernel.Core().Running())
                {
                    Service.RequestStop();
                    Service.Tick(1);
                    return Finish(CommandLineExitCode::Interrupted, "cancelled", &Service.Result(), &Kernel);
                }

                const ExecutionTick Tick = Service.Tick();
                if (Tick.State != ExecutionState::Suspended)
                {
                    // Automatic refresh (no SYNC ON, or SYNC OFF): present between execution slices.
                    if (Tick.State == ExecutionState::Running && !Kernel.Core().SyncOn())
                    {
                        Kernel.Renderer().RenderScene(true);
                        Service.NotifyPresentedFrame();
                    }
                    std::this_thread::yield();
                    continue;
                }
                switch (Tick.Suspension.Reason)
                {
                    case SuspensionReason::Deadline:
                    {
                        Kernel.Renderer().RenderScene(true);
                        Service.NotifyPresentedFrame();
                        if (!Tick.Suspension.ResumeAt)
                            break;
                        const auto Now = std::chrono::steady_clock::now();
                        if (!Kernel.Vr().Pacing() && Now < *Tick.Suspension.ResumeAt)
                        {
                            const auto Remaining =
                                std::chrono::duration_cast<std::chrono::milliseconds>(*Tick.Suspension.ResumeAt - Now);
                            std::this_thread::sleep_for(
                                std::clamp(Remaining, std::chrono::milliseconds(1), std::chrono::milliseconds(16)));
                        }
                        break;
                    }
                    case SuspensionReason::Key:
                    case SuspensionReason::Mouse:
                    case SuspensionReason::TextInput:
                        Kernel.Renderer().RenderScene(true);
                        Service.NotifyPresentedFrame();
                        if (!Kernel.Vr().Pacing())
                            std::this_thread::sleep_for(std::chrono::milliseconds(16));
                        break;
                    case SuspensionReason::NextTick:
                    case SuspensionReason::Io:
                        std::this_thread::yield();
                        break;
                    case SuspensionReason::None:
                    case SuspensionReason::FrameBoundary:
                        break;
                }
            }

            const ExecutionResult& Result = Service.Result();
            if (!Result.Success())
            {
                WriteDiagnostics(Result.Diagnostics, Environment);
                return Finish(Result.State == ExecutionState::Cancelled ? CommandLineExitCode::Interrupted
                                                                        : CommandLineExitCode::Runtime,
                              Result.State == ExecutionState::Cancelled ? "cancelled" : "failed", &Result, &Kernel);
            }
            if (Command.Execution.Benchmark)
            {
                const BenchmarkResult& Value = Result.Benchmark;
                std::ostringstream Output;
                Output << "Prepare: " << Value.PrepareMilliseconds << " ms\n"
                       << "Frames: " << Value.FrameCount << '\n'
                       << "Frame avg: " << Value.AverageFrameMilliseconds << " ms\n"
                       << "Frame min: " << Value.MinimumFrameMilliseconds << " ms\n"
                       << "Frame max: " << Value.MaximumFrameMilliseconds << " ms\n"
                       << "Total: " << Value.TotalMilliseconds << " ms\n";
                Write(Environment.StandardOutput, stdout, Output.str());
            }
            if (!Command.CaptureOutput.empty())
            {
                SDL_Surface* Frame = Kernel.Renderer().ReadLastHeadlessFrame();
                const bool Saved = Frame && SDL_SaveBMP(Frame, Command.CaptureOutput.string().c_str());
                SDL_DestroySurface(Frame);
                if (!Saved)
                {
                    Write(Environment.StandardError, stderr,
                          "Failed to save the last frame to " + Command.CaptureOutput.string() + "\n");
                    return Finish(CommandLineExitCode::Runtime, "capture-failed", &Result, &Kernel);
                }
            }
            return Finish(CommandLineExitCode::Success, "completed", &Result, &Kernel);
        }
    } // namespace

    CommandLineParseResult::operator bool() const
    {
        return Command.has_value() && Error.empty() && !HelpRequested;
    }

    std::string_view CommandLineHelp()
    {
        return HelpText;
    }

    CommandLineParseResult ParseCommandLine(std::span<const std::string_view> Arguments)
    {
        CommandLineParseResult Result;
        CommandLine Command;
        std::size_t Index = 0;
        if (!Arguments.empty())
        {
            const std::string_view First = Arguments.front();
            if (First == "studio")
            {
                Command.Action = CommandLineAction::Studio;
                ++Index;
            }
            else if (First == "run")
            {
                Command.Action = CommandLineAction::Run;
                ++Index;
            }
            else if (First == "check")
            {
                Command.Action = CommandLineAction::Check;
                ++Index;
            }
            else if (First == "inspect")
            {
                Command.Action = CommandLineAction::Inspect;
                ++Index;
            }
            else if (First == "export")
            {
                Command.Action = CommandLineAction::Export;
                ++Index;
            }
            else if (First == "--help" || First == "-h")
            {
                Result.HelpRequested = true;
                return Result;
            }
            else if (!First.empty() && First.front() == '-')
            {
                Result.Error = "Unknown command or option: " + std::string(First);
                return Result;
            }
            else
            {
                Command.Path = std::filesystem::path(First);
                ++Index;
            }
        }

        while (Index < Arguments.size())
        {
            const std::string_view Argument = Arguments[Index++];
            if (Argument == "--help" || Argument == "-h")
            {
                Result.HelpRequested = true;
                return Result;
            }
            if (Argument == "--json")
            {
                Command.Json = true;
                continue;
            }
            if (Argument == "--debug-overlay")
            {
                Command.DebugOverlay = true;
                continue;
            }
            if (Argument == "--headless")
            {
                Command.Headless = true;
                continue;
            }
            if (Argument == "--benchmark")
            {
                Command.Execution.Benchmark = true;
                continue;
            }
            if (Argument == "--unsigned")
            {
                Command.ExportSign = false;
                continue;
            }
            const bool TakesValue = Argument == "--asset-root" || Argument == "--command-line" ||
                                    Argument == "--log-output" || Argument == "--builtin-coverage-output" ||
                                    Argument == "--benchmark-output" || Argument == "--capture-output" ||
                                    Argument == "--log-level" ||
                                    Argument == "--max-frames" || Argument == "--instruction-budget" ||
                                    Argument == "--dialect" || Argument == "--target" || Argument == "--output";
            if (TakesValue)
            {
                if (Index >= Arguments.size())
                {
                    Result.Error = "Missing value for " + std::string(Argument);
                    return Result;
                }
                const std::string_view Value = Arguments[Index++];
                if ((Argument == "--builtin-coverage-output" || Argument == "--benchmark-output" ||
                     Argument == "--capture-output") &&
                    Value.empty())
                {
                    Result.Error = "Value for " + std::string(Argument) + " must name a file";
                    return Result;
                }
                if (Argument == "--asset-root")
                    Command.Execution.AssetRoot = Value;
                else if (Argument == "--command-line")
                    Command.CommandLineOverride = std::string(Value);
                else if (Argument == "--log-output")
                    Command.Execution.LogOutputPath = Value;
                else if (Argument == "--builtin-coverage-output")
                    Command.Execution.BuiltinCoverageOutputPath = Value;
                else if (Argument == "--benchmark-output")
                    Command.Execution.BenchmarkOutputPath = Value;
                else if (Argument == "--capture-output")
                    Command.CaptureOutput = Value;
                else if (Argument == "--log-level")
                    Command.LogLevel = Value;
                else if (Argument == "--dialect")
                {
                    Command.RequestedProfile = language::ParseProfile(Value);
                    if (!Command.RequestedProfile)
                    {
                        Result.Error = "Unknown dialect: " + std::string(Value);
                        return Result;
                    }
                }
                else if (Argument == "--target")
                    Command.ExportTarget = Value;
                else if (Argument == "--output")
                    Command.ExportOutput = Value;
                else
                {
                    std::size_t Number = 0;
                    if (!ParseNonNegative(Value, Number))
                    {
                        Result.Error =
                            "Invalid non-negative integer for " + std::string(Argument) + ": " + std::string(Value);
                        return Result;
                    }
                    if (Argument == "--max-frames")
                    {
                        if (Number > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                        {
                            Result.Error = "Value for --max-frames is too large";
                            return Result;
                        }
                        Command.Execution.MaxFrames = static_cast<int>(Number);
                    }
                    else
                    {
                        if (Number == 0)
                        {
                            Result.Error = "Value for --instruction-budget must be greater than zero";
                            return Result;
                        }
                        Command.Execution.InstructionBudget = Number;
                    }
                }
                continue;
            }
            if (!Argument.empty() && Argument.front() == '-')
            {
                Result.Error = "Unknown option: " + std::string(Argument);
                return Result;
            }
            if (Command.Path)
            {
                Result.Error = "Unexpected argument: " + std::string(Argument);
                return Result;
            }
            Command.Path = std::filesystem::path(Argument);
        }

        if (Command.Action != CommandLineAction::Inspect && Command.Json)
        {
            Result.Error = "--json is only valid with inspect";
            return Result;
        }
        if (Command.Action != CommandLineAction::Run)
        {
            for (const std::string_view Option :
                 {"--asset-root", "--command-line", "--log-output", "--builtin-coverage-output", "--benchmark-output",
                  "--log-level", "--max-frames", "--instruction-budget", "--debug-overlay", "--headless",
                  "--benchmark", "--capture-output"})
            {
                if (std::find(Arguments.begin(), Arguments.end(), Option) != Arguments.end() && IsRunOption(Option))
                {
                    Result.Error = std::string(Option) + " is only valid with run";
                    return Result;
                }
            }
        }
        if (!Command.Execution.BenchmarkOutputPath.empty() && !Command.Execution.Benchmark)
        {
            Result.Error = "--benchmark-output requires --benchmark";
            return Result;
        }
        if (!Command.CaptureOutput.empty() && !Command.Headless)
        {
            Result.Error = "--capture-output requires --headless";
            return Result;
        }
        if (!ValidateRunOutputPaths(Command.Execution, Result.Error))
            return Result;
        if (Command.Action != CommandLineAction::Export && !Command.ExportSign)
        {
            Result.Error = "--unsigned is only valid with export";
            return Result;
        }
        if ((Command.Action == CommandLineAction::Run || Command.Action == CommandLineAction::Check ||
             Command.Action == CommandLineAction::Inspect || Command.Action == CommandLineAction::Export) &&
            !Command.Path)
        {
            Result.Error = "Missing source path";
            return Result;
        }
        if (Command.Action == CommandLineAction::Export)
        {
            if (Command.ExportTarget != "desktop" && Command.ExportTarget != "ios" &&
                Command.ExportTarget != "ios-simulator" && Command.ExportTarget != "android-apk" &&
                Command.ExportTarget != "android-aab")
            {
                Result.Error = "Export requires --target desktop|ios|ios-simulator|android-apk|android-aab";
                return Result;
            }
            if (Command.ExportOutput.empty())
            {
                Result.Error = "Export requires --output <directory>";
                return Result;
            }
        }
        else if (!Command.ExportTarget.empty() || !Command.ExportOutput.empty())
        {
            Result.Error = "--target and --output are only valid with export";
            return Result;
        }
        Result.Command = std::move(Command);
        return Result;
    }

    int DispatchCommandLine(const CommandLine& Command, const CommandLineEnvironment& Environment)
    {
        switch (Command.Action)
        {
            case CommandLineAction::Studio:
                if (!Environment.LaunchStudio)
                {
                    Write(Environment.StandardError, stderr, "Studio launcher is not available\n");
                    return Exit(CommandLineExitCode::Runtime);
                }
                return Environment.LaunchStudio(Command);
            case CommandLineAction::Run:
                return DispatchRun(Command, Environment);
            case CommandLineAction::Check:
            case CommandLineAction::Inspect:
                return DispatchAnalysis(Command, Environment);
            case CommandLineAction::Export:
                if (!Environment.ExportArtifact)
                {
                    Write(Environment.StandardError, stderr, "Artifact exporter is not available\n");
                    return Exit(CommandLineExitCode::Runtime);
                }
                {
                    ProjectResolveResult Resolved = ResolveCommandProject(Command, Environment);
                    if (!Resolved)
                    {
                        Write(Environment.StandardError, stderr, Resolved.Error + "\n");
                        return Exit(CommandLineExitCode::Target);
                    }
                    for (const std::string& Blocker : Resolved.Project->ExportBlockers)
                    {
                        Write(Environment.StandardError, stderr,
                              "DBPro project option blocks export: " + Blocker + "\n");
                        return Exit(CommandLineExitCode::Target);
                    }
                    return Environment.ExportArtifact(Command, *Resolved.Project);
                }
        }
        return Exit(CommandLineExitCode::Runtime);
    }

    int DispatchCommandLine(std::span<const std::string_view> Arguments, const CommandLineEnvironment& Environment)
    {
        const CommandLineParseResult Parsed = ParseCommandLine(Arguments);
        if (Parsed.HelpRequested)
        {
            Write(Environment.StandardOutput, stdout, CommandLineHelp());
            return Exit(CommandLineExitCode::Success);
        }
        if (!Parsed)
        {
            Write(Environment.StandardError, stderr, Parsed.Error + "\n");
            Write(Environment.StandardError, stderr, CommandLineHelp());
            return Exit(CommandLineExitCode::Usage);
        }
        return DispatchCommandLine(*Parsed.Command, Environment);
    }
} // namespace dbc::interpreter
