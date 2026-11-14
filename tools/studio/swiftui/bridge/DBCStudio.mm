// Projects, files, the language and runs for the SwiftUI Studio: what app.cpp does for the Dear
// ImGui one, with the interface behind a delegate instead of panels.

#import "DBCStudio.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

// SwiftUI has the application's entry point.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <imgui.h>

#include "core/bundled_resources.hpp"
#include "core/i18n.hpp"
#include "core/locales.hpp"
#include "core/media_catalogue.hpp"
#include "core/new_project.hpp"
#include "core/paths.hpp"
#include "core/stop_button.hpp"
#include "core/text_encoding.hpp"
#include "dba/code_outline.hpp"
#include "dba/completion.hpp"
#include "engine/systems/log.hpp"
#include "interpreter/atomic_file.hpp"
#include "language/language_catalogue.hpp"
#include "language/language_profile.hpp"
#include "path_utf8.hpp"
#include "studio_session.hpp"
#include "theme.hpp"
#include "toolchain/active_run_planner.hpp"

#include "studio_reference.inc"

#ifndef OPENDBC_VERSION_STRING
#define OPENDBC_VERSION_STRING "unknown"
#endif

using namespace dbc::studio;
using namespace dbc::studio::swiftui;
using dbc::language::LanguageProfile;

@implementation DBCDiagnostic
@end
@implementation DBCTextFile
@end
@implementation DBCOpenDocument
@end
@implementation DBCProjectLocation
@end
@implementation DBCFileEntry
@end
@implementation DBCOutlineEntry
@end
@implementation DBCOutline
@end
@implementation DBCMediaFile
@end
@implementation DBCSnippet
@end
@implementation DBCSnippetInsertion
@end
@implementation DBCLibraryTexture
@end
@implementation DBCCommandHelp
@end
@implementation DBCCompletionItem
@end
@implementation DBCCompletion
@end
@implementation DBCSignature
@end
@implementation DBCProjectResolution
@end
@implementation DBCField
@end
@implementation DBCSceneRow
@end

namespace
{
    Session* SessionValue = nullptr;
    __weak DBCStudio* StudioValue = nil;
    // The interface's window, which gets the keyboard back when a program's window closes.
    __weak UIWindow* InterfaceWindow = nil;

    std::string LowerExtension(const std::filesystem::path& Path)
    {
        std::string Extension = dbc::PathToUtf8(Path.extension());
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        return Extension;
    }

    std::string ToLower(std::string_view Text)
    {
        std::string Out(Text);
        std::transform(Out.begin(), Out.end(), Out.begin(),
                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        return Out;
    }

    DBCProfile ToBridge(LanguageProfile Profile)
    {
        return Profile == LanguageProfile::DbPro ? DBCProfileDbPro : DBCProfileDbc;
    }

    LanguageProfile FromBridge(DBCProfile Profile)
    {
        return Profile == DBCProfileDbPro ? LanguageProfile::DbPro : LanguageProfile::Dbc;
    }

    NSArray<NSString*>* ToNsArray(const std::vector<std::string>& Values)
    {
        NSMutableArray<NSString*>* Out = [NSMutableArray arrayWithCapacity:Values.size()];
        for (const std::string& Value : Values)
            [Out addObject:ToNs(Value)];
        return Out;
    }

    NSArray<NSString*>* ToNsArray(const std::vector<std::filesystem::path>& Values)
    {
        NSMutableArray<NSString*>* Out = [NSMutableArray arrayWithCapacity:Values.size()];
        for (const std::filesystem::path& Value : Values)
            [Out addObject:ToNs(Value)];
        return Out;
    }

    std::vector<std::string> FromNsArray(NSArray* Values)
    {
        std::vector<std::string> Out;
        for (id Value in Values)
        {
            if ([Value isKindOfClass:[NSString class]])
                Out.push_back(FromNs(Value));
        }
        return Out;
    }

    // The resolver names its command-line option where the Studio's dialog has buttons.
    std::string ResolutionMessage(const dbc::interpreter::ProjectResolveResult& Resolution)
    {
        if (!Resolution.DialectUndecided)
            return Resolution.Error;
        return Tr(Resolution.Detection.Kind == dbc::language::DialectDetectionKind::Conflict
                      ? "This source has evidence for both DBC and DBPro: choose one."
                      : "This source does not show which dialect it uses: choose DBC or DBPro.");
    }

    std::filesystem::path RunMarker()
    {
        return core::paths::PrefDir() / "run-in-progress";
    }

    bool SamePath(const std::filesystem::path& Left, const std::filesystem::path& Right)
    {
        std::error_code Error;
        return std::filesystem::weakly_canonical(Left, Error) == std::filesystem::weakly_canonical(Right, Error);
    }

    core::TextFormat FormatOf(DBCOpenDocument* Document)
    {
        core::TextFormat Format;
        Format.Windows1252 = Document.windows1252;
        Format.Bom = Document.bom;
        Format.Crlf = Document.crlf;
        return Format;
    }

    NSArray<DBCOpenDocument*>* OpenDocuments()
    {
        id<DBCStudioDelegate> Delegate = TheStudio().delegate;
        return Delegate != nil ? [Delegate studioOpenDocuments] : @[];
    }

    std::filesystem::path CurrentPath()
    {
        NSString* Path = [TheStudio().delegate studioCurrentPath];
        return Path != nil ? PathFromNs(Path) : std::filesystem::path();
    }

    // Unsaved BASIC tabs, as the files would hold them: what a run compiles instead of the disk.
    std::vector<toolchain::MemorySource> DirtySources()
    {
        std::vector<toolchain::MemorySource> Sources;
        for (DBCOpenDocument* Document in OpenDocuments())
        {
            if (!Document.dirty || !Document.basic || Document.path.length == 0)
                continue;
            const std::string Text = FromNs(Document.text);
            std::optional<std::string> Encoded = core::EncodeText(Text, FormatOf(Document));
            Sources.push_back({PathFromNs(Document.path), Encoded ? std::move(*Encoded) : Text});
        }
        return Sources;
    }

    void ApplyAppearance()
    {
        Session& S = TheSession();
        Theme::SetAppearance(Theme::ResolveAppearance(S.Settings.Theme, S.SystemLight));
        if (S.ImGuiReady)
            S.Scene.ApplyTheme();
    }

    void ApplyLanguage()
    {
        std::vector<std::string> SystemLocales;
        for (NSString* Identifier in NSLocale.preferredLanguages)
        {
            NSString* Language = [Identifier componentsSeparatedByString:@"-"].firstObject;
            if (Language != nil)
                SystemLocales.push_back(FromNs(Language));
        }
        core::ActivateInterfaceLanguage(core::ResolveInterfaceLanguage(TheSession().Settings.Language, SystemLocales));
    }

    void ShowProblems(const std::vector<toolchain::RunDiagnostic>& Diagnostics)
    {
        NSMutableArray<DBCDiagnostic*>* Problems = [NSMutableArray arrayWithCapacity:Diagnostics.size()];
        for (const toolchain::RunDiagnostic& Value : Diagnostics)
        {
            DBCDiagnostic* Problem = [DBCDiagnostic new];
            Problem.path = ToNs(Value.Path);
            Problem.line = Value.Line;
            Problem.column = Value.Column;
            Problem.code = ToNs(Value.Code);
            Problem.message = ToNs(Value.Message);
            Problem.isError = Value.Error;
            [Problems addObject:Problem];
        }
        [TheStudio().delegate studioProblemsChanged:Problems];
    }

    void ApplyResolvedProject(dbc::interpreter::ResolvedProject Project)
    {
        Session& S = TheSession();
        S.Resolved = std::move(Project);
        SetStatus(TrFmt("Opened {0} as {1}", {dbc::PathToUtf8(S.Resolved->ProjectRoot),
                                              dbc::language::ProfileName(S.Resolved->Profile)}));
        for (const std::string& Warning : S.Resolved->Warnings)
            AppendOutput(Warning, DBCSeverityNotice);
        UpdateSceneProject();
        [TheStudio().delegate studioProjectChanged];
    }

    bool ResolveWorkspace(const std::filesystem::path& Target, std::optional<LanguageProfile> Forced)
    {
        Session& S = TheSession();
        dbc::interpreter::ProjectResolveRequest Request;
        Request.Target = Target;
        Request.ForcedProfile = Forced;
        Request.RemapPath = [](const std::filesystem::path& Descriptor, std::string_view RawPath)
        { return TheSession().Settings.ProjectPathRemapping(Descriptor, RawPath); };
        S.PendingTarget = Target;
        S.PendingProfile = Forced;
        S.Pending = dbc::interpreter::ResolveProject(Request);
        if (!S.Pending)
        {
            S.Resolved.reset();
            UpdateSceneProject();
            [TheStudio().delegate studioProjectChanged];
            [TheStudio().delegate studioNeedsProjectResolution];
            return false;
        }
        ApplyResolvedProject(std::move(*S.Pending.Project));
        S.Pending = {};
        return true;
    }

    bool OpenFolder(const std::filesystem::path& Folder)
    {
        Session& S = TheSession();
        core::ProjectSettings Loaded = core::ProjectSettings::Load(Folder);
        if (!Loaded.LoadError.empty())
        {
            SetStatus(Loaded.LoadError);
            AppendOutput(Loaded.LoadError, DBCSeverityError);
            return false;
        }
        S.DialectLocked = false;
        const std::filesystem::path Previous = S.Root;
        S.Root = Folder;
        S.ProjectSettings = std::move(Loaded);
        S.Settings.ProjectFolder = dbc::PathToUtf8(Folder);
        if (!Previous.empty() && Previous != Folder)
        {
            S.Settings.LastFile.clear();
            // The other project's tabs would otherwise stay in front, and Run would start them.
            [TheStudio().delegate studioCloseAllDocuments];
        }
        S.Settings.Save();
        const bool Resolved = ResolveWorkspace(Folder, std::nullopt);
        if (!Resolved)
            SetStatus(ResolutionMessage(S.Pending));
        return Resolved;
    }

    void OpenInEditor(const std::filesystem::path& File)
    {
        Session& S = TheSession();
        if (S.Root.empty() && dbc::language::IsLanguageSource(File))
            OpenFolder(File.parent_path());
        if (![TheStudio().delegate studioOpenInEditor:ToNs(File)])
        {
            AppendOutput("[ERROR] " + TrFmt("Cannot open {0}", {dbc::PathToUtf8(File)}), DBCSeverityError);
            SetStatus(TrFmt("Cannot open {0}", {dbc::PathToUtf8(File.filename())}));
            return;
        }
        // The next launch reopens this through OpenFile, which would load a .dbpro as the project.
        if (!dbc::language::IsDbProProject(File))
        {
            S.Settings.LastFile = dbc::PathToUtf8(File);
            S.Settings.Save();
        }
        SetStatus(dbc::PathToUtf8(File.filename()));
    }

    void OpenProjectFolder(const std::filesystem::path& Folder)
    {
        Session& S = TheSession();
        if (OpenFolder(Folder) && S.Resolved && !S.Resolved->EntrySource.empty())
            OpenFile(S.Resolved->EntrySource);
    }

    std::optional<toolchain::ActiveRunPlan> PlanCurrentRun(bool Report)
    {
        Session& S = TheSession();
        toolchain::ActiveRunPlan Plan = toolchain::PlanActiveRun(CurrentPath(), S.Resolved, DirtySources());
        if (!Plan)
        {
            if (Report)
                AppendOutput(Plan.Error, DBCSeverityError);
            return std::nullopt;
        }
        if (!Plan.Blockers.empty())
        {
            if (Report)
            {
                for (const std::string& Blocker : Plan.Blockers)
                    AppendOutput(Blocker, DBCSeverityError);
            }
            return std::nullopt;
        }
        return Plan;
    }

    std::vector<core::UnsavedText> UnsavedTexts()
    {
        std::vector<core::UnsavedText> Texts;
        for (DBCOpenDocument* Document in OpenDocuments())
        {
            if (Document.dirty)
                Texts.push_back({PathFromNs(Document.path), FromNs(Document.title), FromNs(Document.text)});
        }
        return Texts;
    }

    void UpdateRecovery()
    {
        Session& S = TheSession();
        std::string Error;
        if (S.Recovery.Update(UnsavedTexts(), Error))
        {
            S.RecoveryError.clear();
            return;
        }
        // Reported once per distinct failure, not at every refresh.
        if (Error != S.RecoveryError)
            AppendOutput(TrFmt("Cannot write a recovery copy: {0}", {Error}), DBCSeverityError);
        S.RecoveryError = Error;
    }

    UIWindow* FindInterfaceWindow()
    {
        for (UIScene* Scene in UIApplication.sharedApplication.connectedScenes)
        {
            if (![Scene isKindOfClass:[UIWindowScene class]])
                continue;
            for (UIWindow* Window in ((UIWindowScene*)Scene).windows)
            {
                if (Window.isKeyWindow)
                    return Window;
            }
        }
        return nil;
    }

    void CloseRunWindow()
    {
        Session& S = TheSession();
        core::HideStopButton();
        if (S.RunWindow != nullptr)
        {
            SDL_DestroyWindow(S.RunWindow);
            S.RunWindow = nullptr;
        }
        [InterfaceWindow makeKeyAndVisible];
    }

    void StartRun(toolchain::RunDocument Document, const std::filesystem::path& AssetRoot)
    {
        Session& S = TheSession();
        const std::filesystem::path Target = Document.LogicalPath;
        ShowProblems({});

        toolchain::RunOptions Options = S.RunOptions;
        Options.AssetRoot = dbc::PathToUtf8(AssetRoot);
        AppendOutput("> " + S.Execution.Describe() + "  " + dbc::PathToUtf8(Target) + "  --asset-root " +
                         Options.AssetRoot,
                     DBCSeverityNotice);
        // A program that brings the Studio down must not take the unsaved tabs with it.
        UpdateRecovery();

        // The program draws in a window of its own, in front of the interface.
        InterfaceWindow = FindInterfaceWindow();
        S.RunWindow = SDL_CreateWindow("OpenDBC", 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (S.RunWindow == nullptr)
        {
            AppendOutput(std::string("SDL_CreateWindow: ") + SDL_GetError(), DBCSeverityError);
            return;
        }
        S.Execution.SetHostWindow(
            S.RunWindow, [] { return true; },
            []
            {
                CloseRunWindow();
                return true;
            });

        if (S.Execution.Run(std::move(Document), Options))
        {
            S.Busy = true;
            SetStatus(TrFmt("Running {0}", {dbc::PathToUtf8(Target.filename())}));
            std::ofstream(RunMarker()) << dbc::PathToUtf8(Target.filename()) << '\n';
            if (S.Execution.OwnsWindow())
                core::ShowStopButton(S.RunWindow);
            [TheStudio().delegate studioRunStateChanged:YES];
        }
        else if (!S.Busy)
        {
            // Run gave up before the program had the window.
            CloseRunWindow();
        }
    }

    void RunCurrent()
    {
        Session& S = TheSession();
        if (S.Busy)
            return;
        if (CurrentPath().empty())
        {
            AppendOutput(Tr("Nothing to run: open a .dba document."), DBCSeverityError);
            return;
        }
        std::optional<toolchain::ActiveRunPlan> Plan = PlanCurrentRun(true);
        if (!Plan)
            return;
        StartRun(std::move(*Plan->Document), Plan->AssetRoot);
    }

    DBCCommandHelp* ToBridge(const dba::CommandHelp& Help)
    {
        DBCCommandHelp* Out = [DBCCommandHelp new];
        Out.signatures = ToNsArray(Help.Signatures);
        Out.warning = ToNs(Help.Warning);
        Out.text = ToNs(Help.Description);
        return Out;
    }

    // Whether the text above a line leaves it inside a REMSTART block.
    bool InBlockComment(NSString* TextBefore)
    {
        bool InBlock = false;
        std::istringstream Lines(FromNs(TextBefore));
        std::string Line;
        while (std::getline(Lines, Line))
            dba::EndsInCommentOrString(Line, InBlock);
        return InBlock;
    }

    NSInteger Utf16Length(std::string_view Bytes)
    {
        return static_cast<NSInteger>(ToNs(Bytes).length);
    }

    std::uint32_t ToArgb(ImU32 Color)
    {
        const std::uint32_t R = Color & 0xFF;
        const std::uint32_t G = (Color >> 8) & 0xFF;
        const std::uint32_t B = (Color >> 16) & 0xFF;
        const std::uint32_t A = (Color >> 24) & 0xFF;
        return (A << 24) | (R << 16) | (G << 8) | B;
    }
} // namespace

namespace dbc::studio::swiftui
{
    Session& TheSession()
    {
        return *SessionValue;
    }

    DBCStudio* TheStudio()
    {
        return StudioValue;
    }

    void AppendOutput(const std::string& Line, int Severity)
    {
        [TheStudio().delegate studioAppendOutput:ToNs(Line) severity:static_cast<DBCSeverity>(Severity)];
    }

    void SetStatus(const std::string& Status)
    {
        TheSession().Status = Status;
        [TheStudio().delegate studioStatusChanged:ToNs(Status)];
    }

    void NotifySceneChanged()
    {
        [TheStudio().delegate studioSceneChanged];
    }

    std::optional<std::string> ReadSource(const std::filesystem::path& Path)
    {
        for (DBCOpenDocument* Document in OpenDocuments())
        {
            if (Document.path.length != 0 && SamePath(PathFromNs(Document.path), Path))
                return FromNs(Document.text);
        }
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream)
            return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(Stream), {});
    }

    bool WriteSource(const std::filesystem::path& Path, const std::string& Text, bool Generated)
    {
        id<DBCStudioDelegate> Delegate = TheStudio().delegate;
        if (!Generated && [Delegate studioReplaceText:ToNs(Text) atPath:ToNs(Path) saved:NO])
            return true;
        std::string Error;
        if (!dbc::interpreter::AtomicWriteText(Path, Text, &Error))
        {
            AppendOutput("[ERROR] " + Error, DBCSeverityError);
            return false;
        }
        if (Generated)
            [Delegate studioReplaceText:ToNs(Text) atPath:ToNs(Path) saved:YES];
        [Delegate studioProjectChanged];
        return true;
    }

    void OpenFile(const std::filesystem::path& File)
    {
        Session& S = TheSession();
        if (dbc::language::IsDbProProject(File))
        {
            core::ProjectSettings Loaded = core::ProjectSettings::Load(File.parent_path());
            if (!Loaded.LoadError.empty())
            {
                SetStatus(Loaded.LoadError);
                AppendOutput(Loaded.LoadError, DBCSeverityError);
                return;
            }
            S.Root = File.parent_path();
            S.ProjectSettings = std::move(Loaded);
            S.Settings.ProjectFolder = dbc::PathToUtf8(File.parent_path());
            S.Settings.Save();
            S.DialectLocked = true;
            if (ResolveWorkspace(File, LanguageProfile::DbPro) && S.Resolved)
            {
                [TheStudio().delegate studioOpenProjectSettings];
                OpenFile(S.Resolved->EntrySource);
            }
            return;
        }
        const DBCFileKind Kind = [DBCStudio kindOfFile:ToNs(File)];
        if (Kind == DBCFileKindImage || Kind == DBCFileKindAudio)
        {
            [TheStudio().delegate studioPreviewFile:ToNs(File)];
            SetStatus(dbc::PathToUtf8(File.filename()));
            return;
        }
        OpenInEditor(File);
    }

    std::string SceneBlocker()
    {
        const Session& S = TheSession();
        if (S.Root.empty())
            return "Open a project folder to edit its levels.";
        if (!S.Resolved)
            return "The project's language and main source are not resolved yet.";
        if (S.Resolved->Profile != LanguageProfile::DbPro)
            return "The Scene view edits DBPro projects only. Classic levels keep running as BASIC.";
        if (S.Resolved->EntrySource.empty() || !dbc::language::IsLanguageSource(S.Resolved->EntrySource))
            return "The project needs a main .dba source that includes the level.";
        if (!S.Workspace.IsWritable(S.Resolved->ProjectRoot))
            return "The project folder is read-only.";
        return {};
    }
} // namespace dbc::studio::swiftui

@implementation DBCStudio
{
    std::unique_ptr<Session> _session;
    CADisplayLink* _displayLink;
    BOOL _started;
}

+ (DBCStudio*)shared
{
    static DBCStudio* Instance = nil;
    static dispatch_once_t Once;
    dispatch_once(&Once, ^{
        Instance = [DBCStudio new];
    });
    return Instance;
}

+ (NSString*)tr:(NSString*)source
{
    return ToNs(Tr(source.UTF8String));
}

- (BOOL)start
{
    if (_started)
        return YES;
    // The interface is UIKit's: SDL neither owns the application nor runs its event loop.
    SDL_SetMainReady();
    SDL_SetiOSEventPump(false);
    SDL_SetAppMetadata("OpenDBC", OPENDBC_VERSION_STRING, "net.demonixis.opendbc");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        NSLog(@"SDL_Init: %s", SDL_GetError());
        return NO;
    }
    _session = std::make_unique<Session>();
    SessionValue = _session.get();
    StudioValue = self;
    Session& S = *_session;

    if (std::string Error; !core::PrepareBundledResources(Error))
        NSLog(@"Bundled resources: %s", Error.c_str());

    // The palette lives in the ImGui style, and the Scene viewport draws its gizmos with ImGui.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    S.Settings = core::Settings::Load();
    S.SystemLight = UIScreen.mainScreen.traitCollection.userInterfaceStyle == UIUserInterfaceStyleLight;
    ApplyAppearance();
    ApplyLanguage();
    S.Status = Tr("Ready");
    S.Workspace.Initialize();

    S.RunOptions.LogLevel = S.Settings.LogLevel;
    S.RunOptions.DebugOverlay = S.Settings.DebugOverlay;
    S.RunOptions.VirtualPadStick = S.Settings.Editor.VirtualPadStick;
    S.Scene.SetEditorSettings(S.Settings.Editor);

    S.Reference.Load(core::paths::FindReferenceDoc());
    S.Commands.Build(S.Reference);
    S.IconGenerator.SetLogHandler([](const std::string& Message) { AppendOutput(Message, DBCSeverityNormal); });

    S.Execution.SetHandlers(
        [](const std::string& Line, bool IsError)
        { AppendOutput(Line, IsError ? DBCSeverityError : DBCSeverityNormal); },
        [](int ExitCode, const std::vector<toolchain::RunDiagnostic>& Diagnostics)
        {
            Session& Current = TheSession();
            Current.Busy = false;
            std::error_code Ignored;
            std::filesystem::remove(RunMarker(), Ignored);
            ShowProblems(Diagnostics);
            std::string Status;
            if (ExitCode == 0)
                Status = Tr("Run finished");
            else if (ExitCode == 130)
                Status = Tr("Stopped");
            else
                Status = TrFmt("Run failed (exit {0})", {std::to_string(ExitCode)});
            SetStatus(Status);
            AppendOutput(Status, ExitCode == 0 || ExitCode == 130 ? DBCSeverityNotice : DBCSeverityError);
            [TheStudio().delegate studioRunStateChanged:NO];
        });

    // The app's container, and with it every saved path, moves when the app is updated.
    const auto Relocate = [&S](std::string& Saved)
    { Saved = dbc::PathToUtf8(core::RelocateUnder(dbc::PathFromUtf8(Saved), S.Workspace.ImportRoot())); };
    Relocate(S.Settings.ProjectFolder);
    Relocate(S.Settings.LastFile);

    S.Recovery.SetDirectory(core::paths::PrefDir() / "recovery");
    S.RecoveryCopies = S.Recovery.Pending();
    S.NextRecovery = SDL_GetTicks() + core::Recovery::KRecoveryInterval;

    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick)];
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    _started = YES;
    return YES;
}

- (void)restoreSession
{
    Session& S = *_session;
    std::error_code Error;
    const std::string LastFile = S.Settings.LastFile;
    if (!S.Settings.ProjectFolder.empty() &&
        std::filesystem::is_directory(dbc::PathFromUtf8(S.Settings.ProjectFolder), Error))
    {
        OpenFolder(dbc::PathFromUtf8(S.Settings.ProjectFolder));
        if (!LastFile.empty() && std::filesystem::is_regular_file(dbc::PathFromUtf8(LastFile), Error))
            OpenFile(dbc::PathFromUtf8(LastFile));
    }

    // Left behind by a run that did not end: the program took the Studio down with it.
    std::ifstream Marker(RunMarker());
    std::string Program;
    if (Marker && std::getline(Marker, Program))
        AppendOutput(TrFmt("The Studio closed while {0} was running.", {Program}), DBCSeverityError);
    Marker.close();
    std::filesystem::remove(RunMarker(), Error);
}

- (void)shutdown
{
    if (!_started)
        return;
    Session& S = *_session;
    S.Execution.Shutdown();
    S.Settings.Save();
    S.Recovery.Clear();
}

- (void)tick
{
    Session& S = *_session;
    S.Execution.BeginEventFrame();
    SDL_Event Event;
    while (SDL_PollEvent(&Event))
    {
        S.Execution.HandleEvent(Event);
        // Three fingers stop the program: its window covers the interface and its Stop button.
        if (S.Busy && Event.type == SDL_EVENT_FINGER_DOWN)
        {
            int Count = 0;
            if (SDL_Finger** Fingers = SDL_GetTouchFingers(Event.tfinger.touchID, &Count))
                SDL_free(Fingers);
            if (Count >= 3)
                [self stopRun];
        }
    }
    if (core::TakeStopRequest())
        [self stopRun];
    if (!S.InBackground)
        S.Execution.Poll();

    if (SDL_GetTicks() >= S.NextRecovery)
    {
        S.NextRecovery = SDL_GetTicks() + core::Recovery::KRecoveryInterval;
        UpdateRecovery();
    }
}

- (void)applicationWillResignActive
{
    SDL_OnApplicationWillEnterBackground();
}

- (void)applicationDidEnterBackground
{
    _session->InBackground = true;
    UpdateRecovery();
    _session->Settings.Save();
    SDL_OnApplicationDidEnterBackground();
}

- (void)applicationWillEnterForeground
{
    SDL_OnApplicationWillEnterForeground();
}

- (void)applicationDidBecomeActive
{
    _session->InBackground = false;
    SDL_OnApplicationDidEnterForeground();
}

- (NSString*)versionString
{
    return @OPENDBC_VERSION_STRING;
}

- (NSString*)noticePath
{
    return ToNs(core::paths::ResourceDir() / "NOTICE.md");
}

#pragma mark Preferences

- (NSString*)theme
{
    return ToNs(_session->Settings.Theme);
}

- (void)setTheme:(NSString*)theme
{
    _session->Settings.Theme = FromNs(theme);
    _session->Settings.Save();
    ApplyAppearance();
}

- (void)setSystemAppearanceLight:(BOOL)light
{
    if (_session->SystemLight == static_cast<bool>(light))
        return;
    _session->SystemLight = light;
    ApplyAppearance();
}

- (BOOL)appearanceIsLight
{
    return Theme::Current == Theme::Appearance::Light;
}

- (NSString*)language
{
    return ToNs(_session->Settings.Language);
}

- (void)setLanguage:(NSString*)language
{
    _session->Settings.Language = FromNs(language);
    _session->Settings.Save();
    ApplyLanguage();
}

- (NSArray<NSArray<NSString*>*>*)interfaceLanguages
{
    NSMutableArray* Out = [NSMutableArray array];
    for (const core::InterfaceLanguage& Value : core::InterfaceLanguages())
        [Out addObject:@[ToNs(Value.Code), ToNs(Value.Name)]];
    return Out;
}

- (NSString*)logLevel
{
    return ToNs(_session->RunOptions.LogLevel);
}

- (void)setLogLevel:(NSString*)logLevel
{
    _session->RunOptions.LogLevel = FromNs(logLevel);
    _session->Settings.LogLevel = _session->RunOptions.LogLevel;
    _session->Settings.Save();
}

- (BOOL)debugOverlay
{
    return _session->RunOptions.DebugOverlay;
}

- (void)setDebugOverlay:(BOOL)debugOverlay
{
    _session->RunOptions.DebugOverlay = debugOverlay;
    _session->Settings.DebugOverlay = debugOverlay;
    _session->Settings.Save();
}

- (BOOL)showStatusBar
{
    return _session->Settings.ShowStatusBar;
}

- (void)setShowStatusBar:(BOOL)showStatusBar
{
    _session->Settings.ShowStatusBar = showStatusBar;
    _session->Settings.Save();
}

- (BOOL)virtualPadStick
{
    return _session->Settings.Editor.VirtualPadStick;
}

- (void)setVirtualPadStick:(BOOL)virtualPadStick
{
    _session->Settings.Editor.VirtualPadStick = virtualPadStick;
    _session->RunOptions.VirtualPadStick = virtualPadStick;
    _session->Settings.Save();
}

- (double)textZoom
{
    return _session->Settings.Editor.TextZoom;
}

- (void)setTextZoom:(double)textZoom
{
    _session->Settings.Editor.TextZoom =
        std::clamp(static_cast<float>(textZoom), core::EditorSettings::KMinTextZoom, core::EditorSettings::KMaxTextZoom);
    _session->Settings.Save();
}

- (double)scrollSpeed
{
    return _session->Settings.Editor.ScrollSpeed;
}

- (void)setScrollSpeed:(double)scrollSpeed
{
    _session->Settings.Editor.ScrollSpeed =
        std::clamp(static_cast<float>(scrollSpeed), core::EditorSettings::KMinSpeed, core::EditorSettings::KMaxSpeed);
    _session->Scene.SetEditorSettings(_session->Settings.Editor);
    _session->Settings.Save();
}

- (double)cameraSpeed
{
    return _session->Settings.Editor.CameraSpeed;
}

- (void)setCameraSpeed:(double)cameraSpeed
{
    _session->Settings.Editor.CameraSpeed =
        std::clamp(static_cast<float>(cameraSpeed), core::EditorSettings::KMinSpeed, core::EditorSettings::KMaxSpeed);
    _session->Scene.SetEditorSettings(_session->Settings.Editor);
    _session->Settings.Save();
}

- (NSInteger)interfacePercent
{
    return _session->Settings.Editor.InterfacePercent;
}

- (void)setInterfacePercent:(NSInteger)interfacePercent
{
    _session->Settings.Editor.InterfacePercent =
        std::clamp(static_cast<int>(interfacePercent), core::EditorSettings::KMinInterfacePercent,
                   core::EditorSettings::KMaxInterfacePercent);
    _session->Settings.Save();
}

- (BOOL)hasPathRemappings
{
    return !_session->Settings.ProjectPathRemappings.empty();
}

- (void)clearPathRemappings
{
    _session->Settings.ClearProjectPathRemappings();
    _session->Settings.Save();
}

- (NSInteger)paletteColor:(NSString*)name
{
    static const std::map<std::string, const ImU32*> Colors = {
        {"Background", &Theme::Background}, {"Panel", &Theme::Panel},           {"PanelAlt", &Theme::PanelAlt},
        {"Border", &Theme::Border},         {"Hover", &Theme::Hover},           {"Selection", &Theme::Selection},
        {"Accent", &Theme::Accent},         {"Folder", &Theme::Folder},         {"Text", &Theme::Text},
        {"TextDim", &Theme::TextDim},       {"LineNumber", &Theme::LineNumber}, {"Keyword", &Theme::Keyword},
        {"Identifier", &Theme::Identifier}, {"Command", &Theme::Command},       {"String", &Theme::String},
        {"Number", &Theme::Number},         {"Comment", &Theme::Comment},       {"Control", &Theme::Control},
        {"Error", &Theme::Error},           {"ErrorLine", &Theme::ErrorLine},   {"Run", &Theme::Run},
        {"Warning", &Theme::Warning},
    };
    const auto Found = Colors.find(FromNs(name));
    return Found != Colors.end() ? ToArgb(*Found->second) : 0xFF808080;
}

#pragma mark Projects

- (NSArray<DBCProjectLocation*>*)locations
{
    NSMutableArray<DBCProjectLocation*>* Out = [NSMutableArray array];
    for (const core::Workspace::Location& Value : _session->Workspace.Locations())
    {
        DBCProjectLocation* Location = [DBCProjectLocation new];
        Location.name = ToNs(Tr(Value.Name.c_str()));
        Location.path = ToNs(Value.Path);
        Location.hint = ToNs(Tr(Value.Hint.c_str()));
        Location.writable = Value.Writable;
        Location.samples = Value.Type == core::Workspace::Kind::BundledSamples;
        Location.projects = ToNsArray(core::ProjectFolders(Value.Path));
        [Out addObject:Location];
    }
    return Out;
}

- (NSString*)importRoot
{
    return ToNs(_session->Workspace.ImportRoot());
}

- (NSString*)projectRoot
{
    return ToNs(_session->Root);
}

- (BOOL)hasResolvedProject
{
    return _session->Resolved.has_value();
}

- (DBCProfile)profile
{
    return ToBridge(_session->Profile());
}

- (NSString*)dialectOrigin
{
    if (!_session->Resolved)
        return @"";
    switch (_session->Resolved->ProfileOrigin)
    {
        case dbc::interpreter::DialectOrigin::Forced:
            return ToNs(TrC("dialect origin|forced"));
        case dbc::interpreter::DialectOrigin::Configuration:
            return ToNs(TrC("dialect origin|configuration"));
        case dbc::interpreter::DialectOrigin::Project:
            return ToNs(TrC("dialect origin|project"));
        case dbc::interpreter::DialectOrigin::Detected:
            return ToNs(TrC("dialect origin|detected"));
        case dbc::interpreter::DialectOrigin::Interactive:
            return ToNs(TrC("dialect origin|interactive"));
    }
    return @"";
}

- (BOOL)dialectLocked
{
    return _session->DialectLocked;
}

- (NSString*)descriptorPath
{
    if (!_session->Resolved || !_session->Resolved->DescriptorPath)
        return nil;
    return ToNs(*_session->Resolved->DescriptorPath);
}

- (NSString*)entrySource
{
    return _session->Resolved ? ToNs(_session->Resolved->EntrySource) : @"";
}

- (NSArray<NSString*>*)orderedSources
{
    return _session->Resolved ? ToNsArray(_session->Resolved->OrderedSources) : @[];
}

- (NSString*)assetRoot
{
    if (!_session->Resolved)
        return ToNs(_session->Root);
    return ToNs(_session->Resolved->AssetRoot.empty() ? _session->Resolved->ProjectRoot
                                                      : _session->Resolved->AssetRoot);
}

- (void)openLocationProject:(NSString*)folder samples:(BOOL)samples
{
    if (_session->Busy)
        return;
    if (!samples)
    {
        OpenProjectFolder(PathFromNs(folder));
        return;
    }
    // The bundle is read-only and a run writes its log beside the program.
    const core::NewProjectResult Copy = core::CopySampleProject(PathFromNs(folder), _session->Workspace.ImportRoot());
    if (!Copy)
    {
        SetStatus(Copy.Error);
        AppendOutput(Copy.Error, DBCSeverityError);
        return;
    }
    OpenProjectFolder(Copy.Root);
}

- (void)openProjectFolder:(NSString*)folder
{
    OpenProjectFolder(PathFromNs(folder));
}

- (void)openFile:(NSString*)path
{
    OpenFile(PathFromNs(path));
}

- (NSString*)validateProjectName:(NSString*)name
{
    return ToNs(core::ValidateProjectName(FromNs(name)));
}

- (NSString*)createProjectNamed:(NSString*)name profile:(DBCProfile)profile basicScene:(BOOL)basicScene
{
    Session& S = *_session;
    const std::filesystem::path Location = S.Workspace.ImportRoot();
    if (!S.Workspace.IsWritable(Location))
        return ToNs(Tr("The location is read-only."));
    const core::NewProjectResult Created = core::CreateProject(
        {Location, FromNs(name), FromBridge(profile),
         basicScene ? core::ProjectTemplate::BasicScene : core::ProjectTemplate::Empty});
    if (!Created)
        return ToNs(Created.Error);
    AppendOutput(TrFmt("Created project {0}", {dbc::PathToUtf8(Created.Root)}), DBCSeverityNormal);
    OpenFolder(Created.Root);
    if (S.Resolved)
        OpenFile(Created.MainSource);
    return @"";
}

- (void)setProjectDialect:(DBCProfile)profile
{
    Session& S = *_session;
    if (S.Busy || S.DialectLocked || S.Root.empty())
        return;
    const LanguageProfile Profile = FromBridge(profile);
    if (!ResolveWorkspace(S.Root, Profile))
        return;
    S.ProjectSettings.LanguageDialect = Profile;
    if (Profile == LanguageProfile::Dbc)
        S.ProjectSettings.LanguageProjectFile.clear();
    else if (S.Resolved && S.Resolved->DescriptorPath)
        S.ProjectSettings.LanguageProjectFile =
            dbc::GenericPathToUtf8(S.Resolved->DescriptorPath->lexically_relative(S.Root));
    if (S.Resolved)
        S.ProjectSettings.LanguageEntry = dbc::GenericPathToUtf8(S.Resolved->EntrySource.lexically_relative(S.Root));
    if (!S.ProjectSettings.Save(S.Root))
        AppendOutput(Tr("Cannot save the project dialect"), DBCSeverityError);
}

- (DBCProjectResolution*)pendingResolution
{
    const Session& S = *_session;
    if (S.Pending || (S.Pending.Error.empty() && !S.Pending.DialectUndecided))
        return nil;
    DBCProjectResolution* Out = [DBCProjectResolution new];
    Out.message = ToNs(ResolutionMessage(S.Pending));
    Out.remappablePath = ToNs(S.Pending.RemappablePath);
    Out.candidates = ToNsArray(S.Pending.ProjectCandidates);
    return Out;
}

- (void)resolveWithCandidate:(NSString*)path
{
    ResolveWorkspace(PathFromNs(path), LanguageProfile::DbPro);
}

- (void)grantRemapping:(NSString*)localPath
{
    Session& S = *_session;
    const std::filesystem::path Descriptor = S.Pending.PendingDescriptor   ? *S.Pending.PendingDescriptor
                                             : !S.Pending.ProjectCandidates.empty() ? S.Pending.ProjectCandidates.front()
                                                                                    : S.PendingTarget;
    S.Settings.SetProjectPathRemapping(Descriptor, S.Pending.RemappablePath, PathFromNs(localPath));
    S.Settings.Save();
    ResolveWorkspace(S.PendingTarget, S.PendingProfile);
}

- (NSArray<DBCFileEntry*>*)entriesOfFolder:(NSString*)folder
{
    std::vector<std::filesystem::directory_entry> Entries;
    std::error_code Error;
    for (std::filesystem::directory_iterator It(PathFromNs(folder), Error), End; !Error && It != End;
         It.increment(Error))
    {
        const std::string Name = dbc::PathToUtf8(It->path().filename());
        if (!Name.empty() && Name.front() != '.')
            Entries.push_back(*It);
    }
    const auto IsFolder = [](const std::filesystem::directory_entry& Entry)
    {
        std::error_code Ignored;
        return Entry.is_directory(Ignored);
    };
    std::sort(Entries.begin(), Entries.end(),
              [&](const std::filesystem::directory_entry& Left, const std::filesystem::directory_entry& Right)
              {
                  const bool LeftFolder = IsFolder(Left);
                  const bool RightFolder = IsFolder(Right);
                  if (LeftFolder != RightFolder)
                      return LeftFolder;
                  return ToLower(dbc::PathToUtf8(Left.path().filename())) <
                         ToLower(dbc::PathToUtf8(Right.path().filename()));
              });
    NSMutableArray<DBCFileEntry*>* Out = [NSMutableArray arrayWithCapacity:Entries.size()];
    for (const std::filesystem::directory_entry& Entry : Entries)
    {
        DBCFileEntry* Value = [DBCFileEntry new];
        Value.name = ToNs(Entry.path().filename());
        Value.path = ToNs(Entry.path());
        Value.directory = IsFolder(Entry);
        Value.kind = Value.directory ? DBCFileKindNone : [DBCStudio kindOfFile:Value.path];
        [Out addObject:Value];
    }
    return Out;
}

+ (DBCFileKind)kindOfFile:(NSString*)path
{
    const std::filesystem::path Path = PathFromNs(path);
    if (dbc::language::IsLanguageSource(Path))
        return DBCFileKindBasic;
    const std::string Extension = LowerExtension(Path);
    if (Extension == ".fx" || Extension == ".hlsl")
        return DBCFileKindEffect;
    if (dbc::language::IsDbProProject(Path) || Extension == ".json")
        return DBCFileKindProjectData;
    for (const char* Image : {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp"})
    {
        if (Extension == Image)
            return DBCFileKindImage;
    }
    for (const char* Audio : {".wav", ".ogg", ".mp3", ".flac", ".mod", ".xm"})
    {
        if (Extension == Audio)
            return DBCFileKindAudio;
    }
    return DBCFileKindNone;
}

- (NSArray<DBCMediaFile*>*)mediaFilesTruncated:(BOOL*)truncated
{
    const core::MediaScan Scan = core::ScanMedia(PathFromNs(self.assetRoot), 5000);
    *truncated = Scan.Truncated;
    NSMutableArray<DBCMediaFile*>* Out = [NSMutableArray arrayWithCapacity:Scan.Files.size()];
    for (const core::MediaFile& File : Scan.Files)
    {
        DBCMediaFile* Value = [DBCMediaFile new];
        Value.path = ToNs(File.Path);
        Value.relativeName = ToNs(File.RelativeName);
        Value.kind = static_cast<NSInteger>(File.Kind);
        Value.sizeText = ToNs(core::FormatFileSize(File.Size));
        [Out addObject:Value];
    }
    return Out;
}

+ (NSString*)mediaKindLabel:(NSInteger)kind
{
    return ToNs(Tr(core::MediaKindLabel(static_cast<core::MediaKind>(kind))));
}

#pragma mark Files

- (DBCTextFile*)readTextFile:(NSString*)path
{
    std::ifstream Stream(PathFromNs(path), std::ios::binary);
    if (!Stream)
        return nil;
    const std::string Bytes(std::istreambuf_iterator<char>(Stream), {});
    core::TextFormat Format;
    DBCTextFile* Out = [DBCTextFile new];
    Out.text = ToNs(core::DecodeText(Bytes, Format));
    Out.windows1252 = Format.Windows1252;
    Out.bom = Format.Bom;
    Out.crlf = Format.Crlf;
    return Out;
}

- (DBCTextFile*)writeDocument:(DBCOpenDocument*)document
                       toPath:(NSString*)path
                       notice:(NSString* _Nullable* _Nonnull)notice
                        error:(NSString* _Nullable* _Nonnull)error
{
    *notice = nil;
    *error = nil;
    const std::filesystem::path Target = PathFromNs(path);
    const std::string Text = FromNs(document.text);
    core::TextFormat Format = FormatOf(document);
    std::optional<std::string> Encoded = core::EncodeText(Text, Format);
    if (!Encoded)
    {
        Format.Windows1252 = false;
        Encoded = core::EncodeText(Text, Format);
        *notice = ToNs(TrFmt("{0} is now saved as UTF-8: Windows-1252 cannot store some of its characters.",
                             {dbc::PathToUtf8(Target.filename())}));
    }
    std::string Failure;
    if (!Encoded || !dbc::interpreter::AtomicWriteText(Target, *Encoded, &Failure))
    {
        *error = ToNs(Failure.empty() ? TrFmt("Cannot save {0}", {dbc::PathToUtf8(Target)}) : Failure);
        return nil;
    }
    DBCTextFile* Out = [DBCTextFile new];
    Out.text = document.text;
    Out.windows1252 = Format.Windows1252;
    Out.bom = Format.Bom;
    Out.crlf = Format.Crlf;
    return Out;
}

- (void)documentSaved
{
    const std::filesystem::path Current = CurrentPath();
    if (Current.empty() || !dbc::language::IsLanguageSource(Current))
        return;
    // Saving a file the run plan cannot use is not an error worth reporting.
    if (std::optional<toolchain::ActiveRunPlan> Plan = PlanCurrentRun(false))
        ShowProblems(_session->Execution.Check(std::move(*Plan->Document)));
}

- (void)noteLastFile:(NSString*)path
{
    if (dbc::language::IsDbProProject(PathFromNs(path)))
        return;
    _session->Settings.LastFile = FromNs(path);
    _session->Settings.Save();
}

#pragma mark The language

- (NSArray<NSString*>*)keywordsForProfile:(DBCProfile)profile
{
    NSMutableSet<NSString*>* Out = [NSMutableSet set];
    for (const dbc::language::SymbolDescriptor* Descriptor : dbc::language::Symbols(FromBridge(profile)))
    {
        if (Descriptor->Kind == dbc::language::SymbolKind::Statement ||
            Descriptor->Kind == dbc::language::SymbolKind::Directive)
            [Out addObject:ToNs(ToLower(Descriptor->CanonicalName))];
    }
    return Out.allObjects;
}

- (NSArray<NSString*>*)commandWordsForProfile:(DBCProfile)profile
{
    NSMutableSet<NSString*>* Out = [NSMutableSet set];
    for (const dbc::language::SymbolDescriptor* Descriptor : dbc::language::Symbols(FromBridge(profile)))
    {
        if (Descriptor->Kind != dbc::language::SymbolKind::Command &&
            Descriptor->Kind != dbc::language::SymbolKind::Function)
            continue;
        std::istringstream Words{std::string(Descriptor->CanonicalName)};
        std::string Word;
        while (Words >> Word)
            [Out addObject:ToNs(ToLower(Word))];
    }
    return Out.allObjects;
}

- (DBCOutline*)outlineOfText:(NSString*)text path:(NSString*)path
{
    const dba::CodeOutline Outline = dba::BuildCodeOutline(PathFromNs(path), FromNs(text), _session->Profile());
    const auto Convert = [](const std::vector<dba::OutlineEntry>& Entries)
    {
        NSMutableArray<DBCOutlineEntry*>* Out = [NSMutableArray arrayWithCapacity:Entries.size()];
        for (const dba::OutlineEntry& Entry : Entries)
        {
            DBCOutlineEntry* Value = [DBCOutlineEntry new];
            Value.name = ToNs(Entry.Array ? Entry.Name + "()" : Entry.Name);
            Value.line = Entry.Line;
            Value.count = Entry.Count;
            [Out addObject:Value];
        }
        return Out;
    };
    DBCOutline* Out = [DBCOutline new];
    Out.functions = Convert(Outline.Functions);
    Out.labels = Convert(Outline.Labels);
    Out.types = Convert(Outline.Types);
    Out.variables = Convert(Outline.Variables);
    return Out;
}

- (void)refreshSymbolsForText:(NSString*)text path:(NSString*)path
{
    _session->Symbols = dba::BuildProgramSymbols(PathFromNs(path), FromNs(text), _session->Profile(),
                                                 [](const std::filesystem::path& File) { return ReadSource(File); });
    _session->HasSymbols = true;
}

- (DBCCompletion*)completeLine:(NSString*)line
                        cursor:(NSInteger)cursor
                        manual:(BOOL)manual
                    lineNumber:(NSInteger)lineNumber
                    textBefore:(NSString*)textBefore
{
    Session& S = *_session;
    cursor = std::clamp<NSInteger>(cursor, 0, static_cast<NSInteger>(line.length));
    const std::string Line = FromNs(line);
    const std::string Before = FromNs([line substringToIndex:static_cast<NSUInteger>(cursor)]);
    bool InBlock = InBlockComment(textBefore);
    if (dba::EndsInCommentOrString(Before, InBlock))
        return nil;
    const dba::Completion Result =
        dba::Complete(S.Commands, S.Profile(), Before, manual, S.HasSymbols ? &S.Symbols : nullptr,
                      static_cast<std::uint32_t>(lineNumber));
    if (Result.Items.empty())
        return nil;
    DBCCompletion* Out = [DBCCompletion new];
    NSMutableArray<DBCCompletionItem*>* Items = [NSMutableArray array];
    // More than this is a list nobody reads; typing narrows it.
    constexpr std::size_t KMaxItems = 200;
    for (std::size_t Index = 0; Index < Result.Items.size() && Index < KMaxItems; ++Index)
    {
        const dba::CompletionItem& Item = Result.Items[Index];
        DBCCompletionItem* Value = [DBCCompletionItem new];
        Value.name = ToNs(Item.Name);
        Value.detail = ToNs(Item.Detail);
        Value.kind = static_cast<NSInteger>(Item.What);
        Value.insertion = ToNs(dba::CompletionText(Item, Result.Typed));
        Value.help = ToBridge(dba::DescribeItem(S.Commands, S.Profile(), Item));
        [Items addObject:Value];
    }
    Out.items = Items;
    Out.replaceStart = Utf16Length(std::string_view(Line).substr(0, std::min(Result.ReplaceStart, Line.size())));
    Out.replaceEnd = Utf16Length(std::string_view(Line).substr(0, dba::CompletionWordEnd(Line, Before.size())));
    Out.selection = static_cast<NSInteger>(Result.Selection);
    return Out;
}

- (NSArray<DBCSignature*>*)parameterHintForLine:(NSString*)lineBeforeCursor textBefore:(NSString*)textBefore
{
    Session& S = *_session;
    const std::optional<dba::ParameterHint> Hint =
        dba::FindParameterHint(S.Commands, S.Profile(), FromNs(lineBeforeCursor), InBlockComment(textBefore),
                               S.HasSymbols ? &S.Symbols : nullptr);
    if (!Hint)
        return @[];
    NSMutableArray<DBCSignature*>* Out = [NSMutableArray array];
    for (const dba::Signature& Overload : Hint->Overloads)
    {
        DBCSignature* Value = [DBCSignature new];
        Value.prefix = ToNs(Overload.Prefix);
        Value.parameters = ToNsArray(Overload.Parameters);
        Value.suffix = ToNs(Overload.Suffix);
        const std::size_t Active = Overload.ParameterFor(Hint->Argument);
        Value.active = Active == dba::Signature::NoParameter ? -1 : static_cast<NSInteger>(Active);
        [Out addObject:Value];
    }
    return Out;
}

- (DBCCommandHelp*)helpForWord:(NSString*)word
{
    Session& S = *_session;
    const dba::Command* Command = S.Commands.Find(S.Profile(), FromNs(word));
    if (Command == nullptr)
        return nil;
    return ToBridge(dba::DescribeCommand(S.Commands, S.Profile(), Command->Classic));
}

- (NSString*)snippetFolder
{
    return ToNs(core::paths::PrefDir() / "snippets");
}

- (NSArray<DBCSnippet*>*)snippets
{
    Session& S = *_session;
    S.Snippets = core::BuiltinSnippets();
    const std::size_t Builtin = S.Snippets.size();
    for (core::Snippet& Value : core::LoadSnippets(core::paths::PrefDir() / "snippets"))
        S.Snippets.push_back(std::move(Value));
    NSMutableArray<DBCSnippet*>* Out = [NSMutableArray array];
    for (std::size_t Index = 0; Index < S.Snippets.size(); ++Index)
    {
        const core::Snippet& Value = S.Snippets[Index];
        if (!Value.Supports(S.Profile()))
            continue;
        const bool User = Index >= Builtin;
        DBCSnippet* Snippet = [DBCSnippet new];
        Snippet.name = ToNs(User ? Value.Name : std::string(Tr(Value.Name.c_str())));
        Snippet.summary = ToNs(User ? Value.Description : std::string(Tr(Value.Description.c_str())));
        Snippet.body = ToNs(Value.Body);
        Snippet.user = User;
        Snippet.index = static_cast<NSInteger>(Index);
        [Out addObject:Snippet];
    }
    return Out;
}

- (DBCSnippetInsertion*)planSnippet:(NSInteger)index inText:(NSString*)text
{
    Session& S = *_session;
    DBCSnippetInsertion* Out = [DBCSnippetInsertion new];
    Out.text = @"";
    Out.error = @"";
    if (index < 0 || static_cast<std::size_t>(index) >= S.Snippets.size())
        return Out;
    const core::SnippetInsertion Plan =
        core::PlanSnippetInsertion(FromNs(text), S.Snippets[static_cast<std::size_t>(index)], S.Profile());
    Out.place = static_cast<NSInteger>(Plan.Where);
    Out.line = static_cast<NSInteger>(Plan.Line);
    Out.text = ToNs(Plan.Text);
    Out.error = ToNs(Plan.Error);
    return Out;
}

- (NSString*)saveSnippetNamed:(NSString*)name
                      summary:(NSString*)summary
                         body:(NSString*)body
                    bothKinds:(BOOL)bothKinds
{
    core::Snippet Value;
    Value.Name = FromNs(name);
    Value.Description = FromNs(summary);
    Value.Body = FromNs(body);
    const bool DbPro = _session->Profile() == LanguageProfile::DbPro;
    Value.DbPro = DbPro || bothKinds;
    Value.Dbc = !DbPro || bothKinds;
    std::string Error;
    core::SaveSnippet(core::paths::PrefDir() / "snippets", Value, Error);
    return ToNs(Error);
}

#pragma mark Runs

- (BOOL)running
{
    return _session->Busy;
}

- (void)runCurrentDocument
{
    RunCurrent();
}

- (void)stopRun
{
    Session& S = *_session;
    if (!S.Busy)
        return;
    S.Execution.Stop();
    SetStatus(Tr("Stopping..."));
    AppendOutput(S.Status, DBCSeverityNotice);
}

- (BOOL)canDumpDiagnostics
{
    return _session->Execution.CanDumpDiagnostics();
}

- (void)dumpDiagnostics
{
    _session->Execution.DumpDiagnostics();
}

#pragma mark Recovery

- (NSArray<DBCOpenDocument*>*)pendingRecoveries
{
    NSMutableArray<DBCOpenDocument*>* Out = [NSMutableArray array];
    for (const core::RecoveryCopy& Copy : _session->RecoveryCopies)
    {
        DBCOpenDocument* Document = [DBCOpenDocument new];
        Document.path = ToNs(Copy.Content.Path);
        Document.title = ToNs(Copy.Content.Title);
        Document.text = ToNs(Copy.Content.Text);
        Document.dirty = YES;
        [Out addObject:Document];
    }
    return Out;
}

- (void)discardRecoveries
{
    for (const core::RecoveryCopy& Copy : _session->RecoveryCopies)
        _session->Recovery.Remove(Copy.File);
    _session->RecoveryCopies.clear();
}

- (void)updateRecovery
{
    UpdateRecovery();
}

#pragma mark Project settings

- (NSString*)projectSettingsPath
{
    return ToNs(core::ProjectSettings::FilePath(_session->Root));
}

- (NSDictionary<NSString*, id>*)projectSettings
{
    const core::ProjectSettings& P = _session->ProjectSettings;
    return @{
        @"appName" : ToNs(P.AppName),
        @"bundleId" : ToNs(P.BundleId),
        @"versionName" : ToNs(P.VersionName),
        @"versionCode" : @(P.VersionCode),
        @"iconSource" : ToNs(P.IconSource),
        @"iosTeamId" : ToNs(P.IosTeamId),
        @"iosDeploymentTarget" : ToNs(P.IosDeploymentTarget),
        @"iosOrientations" : ToNsArray(P.IosOrientations),
        @"iosCameraDesc" : ToNs(P.IosCameraDesc),
        @"iosMicrophoneDesc" : ToNs(P.IosMicrophoneDesc),
        @"iosLocationDesc" : ToNs(P.IosLocationDesc),
        @"iosPhotoDesc" : ToNs(P.IosPhotoDesc),
        @"androidMinSdk" : @(P.AndroidMinSdk),
        @"androidTargetSdk" : @(P.AndroidTargetSdk),
        @"androidOrientation" : ToNs(P.AndroidOrientation),
        @"androidPermissions" : ToNsArray(P.AndroidPermissions),
        @"androidKeystorePath" : ToNs(P.AndroidKeystorePath),
        @"androidKeyAlias" : ToNs(P.AndroidKeyAlias),
    };
}

- (NSString*)saveProjectSettings:(NSDictionary<NSString*, id>*)values
{
    Session& S = *_session;
    if (S.Root.empty())
        return ToNs(Tr("Open a folder first."));
    core::ProjectSettings Draft = S.ProjectSettings;
    const auto Text = [&](NSString* Key, std::string& Target)
    {
        if ([values[Key] isKindOfClass:[NSString class]])
            Target = FromNs(values[Key]);
    };
    const auto Number = [&](NSString* Key, int& Target)
    {
        if ([values[Key] isKindOfClass:[NSNumber class]])
            Target = [values[Key] intValue];
    };
    const auto List = [&](NSString* Key, std::vector<std::string>& Target)
    {
        if ([values[Key] isKindOfClass:[NSArray class]])
            Target = FromNsArray(values[Key]);
    };
    Text(@"appName", Draft.AppName);
    Text(@"bundleId", Draft.BundleId);
    Text(@"versionName", Draft.VersionName);
    Number(@"versionCode", Draft.VersionCode);
    Text(@"iconSource", Draft.IconSource);
    Text(@"iosTeamId", Draft.IosTeamId);
    Text(@"iosDeploymentTarget", Draft.IosDeploymentTarget);
    List(@"iosOrientations", Draft.IosOrientations);
    Text(@"iosCameraDesc", Draft.IosCameraDesc);
    Text(@"iosMicrophoneDesc", Draft.IosMicrophoneDesc);
    Text(@"iosLocationDesc", Draft.IosLocationDesc);
    Text(@"iosPhotoDesc", Draft.IosPhotoDesc);
    Number(@"androidMinSdk", Draft.AndroidMinSdk);
    Number(@"androidTargetSdk", Draft.AndroidTargetSdk);
    Text(@"androidOrientation", Draft.AndroidOrientation);
    List(@"androidPermissions", Draft.AndroidPermissions);
    Text(@"androidKeystorePath", Draft.AndroidKeystorePath);
    Text(@"androidKeyAlias", Draft.AndroidKeyAlias);
    if (!Draft.Save(S.Root))
        return ToNs(TrFmt("Cannot save {0}", {dbc::PathToUtf8(core::ProjectSettings::FilePath(S.Root))}));
    S.ProjectSettings = std::move(Draft);
    SetStatus(Tr("Project settings saved"));
    const std::filesystem::path Target =
        S.Resolved && S.Resolved->DescriptorPath ? *S.Resolved->DescriptorPath : S.Root;
    ResolveWorkspace(Target, S.DialectLocked ? std::optional<LanguageProfile>(LanguageProfile::DbPro) : std::nullopt);
    return @"";
}

- (void)generateIcons
{
    Session& S = *_session;
    if (!S.Root.empty() && !S.ProjectSettings.IconSource.empty())
        S.IconGenerator.Generate(S.Root, S.ProjectSettings.IconSource);
}

#pragma mark Reference and library

- (NSString*)referenceIndexPath
{
    Session& S = *_session;
    if (!S.Documentation.IsOpen() && !S.Documentation.Open(KStudioReferenceFiles[0].second))
    {
        SetStatus(Tr("Could not load the offline DBA reference."));
        return nil;
    }
    const std::optional<std::filesystem::path> Index =
        S.Documentation.Materialize(core::paths::PrefDir() / "reference");
    if (!Index)
    {
        SetStatus(Tr("Could not load the offline DBA reference."));
        return nil;
    }
    return ToNs(*Index);
}

- (NSArray<DBCLibraryTexture*>*)libraryTextures
{
    Session& S = *_session;
    if (S.Library.empty())
        S.Library = core::ScanTextureLibrary(core::paths::ResourceDir() / "asset_library" / "textures");
    NSMutableArray<DBCLibraryTexture*>* Out = [NSMutableArray arrayWithCapacity:S.Library.size()];
    for (std::size_t Index = 0; Index < S.Library.size(); ++Index)
    {
        const core::LibraryTexture& Texture = S.Library[Index];
        DBCLibraryTexture* Value = [DBCLibraryTexture new];
        Value.name = ToNs(Texture.Name);
        Value.previewPath = ToNs(Texture.Preview);
        Value.cubeMap = Texture.CubeMap;
        Value.normalMap = Texture.NormalMap;
        Value.inProject = !S.Root.empty() && core::HasLibraryTexture(Texture, S.Root);
        Value.index = static_cast<NSInteger>(Index);
        [Out addObject:Value];
    }
    return Out;
}

- (void)addLibraryTexture:(NSInteger)index
{
    Session& S = *_session;
    if (S.Root.empty() || index < 0 || static_cast<std::size_t>(index) >= S.Library.size())
        return;
    const core::LibraryTexture& Texture = S.Library[static_cast<std::size_t>(index)];
    std::string Error;
    if (!core::AddLibraryTexture(Texture, S.Root, Error))
    {
        SetStatus(TrFmt("Cannot add {0}: {1}", {Texture.Name, Error}));
        AppendOutput(S.Status, DBCSeverityError);
        return;
    }
    SetStatus(TrFmt("Added {0} to {1}", {Texture.Name, dbc::PathToUtf8(core::LibraryTextureFolder(S.Root).filename())}));
    AppendOutput(S.Status, DBCSeverityNormal);
    S.Scene.ProjectFilesChanged();
    [self.delegate studioProjectChanged];
}

@end
