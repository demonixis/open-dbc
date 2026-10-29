#include "command_line.hpp"
#include "command_line_detail.hpp"
#include "atomic_file.hpp"
#include "basic_path.hpp"

#include "engine/systems/core_system.hpp"
#include "engine/systems/debug_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/vr_system.hpp"
#include "program_image.hpp"
#include "runtime_startup.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <limits>
#include <set>
#include <span>
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
    using namespace detail;

    namespace
    {
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
            if (IsProgramImagePath(*Command.Path))
            {
                if (Command.Action != CommandLineAction::Run)
                    return {{}, {}, "Only run accepts a program image: " + Command.Path->string()};
                return MakeImageRequest(Command);
            }
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

        int DispatchBuild(const CommandLine& Command, const CommandLineEnvironment& Environment)
        {
            DiskRequestResult Loaded = MakeDiskRequest(Command, Environment);
            if (!Loaded.Request)
            {
                Write(Environment.StandardError, stderr, Loaded.Error + "\n");
                return Exit(CommandLineExitCode::Target);
            }
            for (const std::string& Blocker : Loaded.Project->RunBlockers)
            {
                Write(Environment.StandardError, stderr, "DBPro project option blocks execution: " + Blocker + "\n");
                return Exit(CommandLineExitCode::Target);
            }
            const ProgramBuild Build = BuildProgram(std::move(*Loaded.Request));
            WriteDiagnostics(Build.Diagnostics, Environment);
            if (!Build.Bytecode)
                return Exit(CommandLineExitCode::Source);
            const std::vector<std::uint8_t> Image =
                WriteProgramImage(*Build.Bytecode, Loaded.Project->Startup, Loaded.Project->ProjectRoot);
            std::string Error;
            if (!AtomicWriteText(Command.ExportOutput,
                                 std::string_view(reinterpret_cast<const char*>(Image.data()), Image.size()), &Error))
            {
                Write(Environment.StandardError, stderr,
                      "Failed to write program image '" + Command.ExportOutput.string() + "': " + Error + "\n");
                return Exit(CommandLineExitCode::Target);
            }
            return Exit(CommandLineExitCode::Success);
        }

        int DispatchRun(const CommandLine& Command, const CommandLineEnvironment& Environment)
        {
            if (Command.Path && IsProgramImagePath(*Command.Path))
                return RunProgramImage(Command, Environment);
            return RunRequest(Command, Environment, MakeDiskRequest(Command, Environment), &ExecutionService::Prepare);
        }
    } // namespace

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
            case CommandLineAction::Build:
                return DispatchBuild(Command, Environment);
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
        if (Parsed.VersionRequested)
        {
            Write(Environment.StandardOutput, stdout, CommandLineVersion());
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
