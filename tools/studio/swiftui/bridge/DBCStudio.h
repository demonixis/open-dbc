// What the SwiftUI Studio sees of the Studio's C++: plain Objective-C, so that Swift imports it
// through the bridging header. The interface owns the text of the open tabs; everything else
// (projects, the language, runs, the level editor) lives behind this file.
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, DBCSeverity) {
    DBCSeverityNormal,
    DBCSeverityError,
    DBCSeverityNotice,
};

typedef NS_ENUM(NSInteger, DBCProfile) {
    DBCProfileDbc,
    DBCProfileDbPro,
};

// How a file is opened: by the code editor, by the preview, or not at all.
typedef NS_ENUM(NSInteger, DBCFileKind) {
    DBCFileKindNone,
    DBCFileKindBasic,
    DBCFileKindEffect,
    DBCFileKindProjectData,
    DBCFileKindImage,
    DBCFileKindAudio,
};

@interface DBCDiagnostic : NSObject
@property(nonatomic, copy) NSString* path;
@property(nonatomic) NSInteger line;
@property(nonatomic) NSInteger column;
@property(nonatomic, copy) NSString* code;
@property(nonatomic, copy) NSString* message;
@property(nonatomic) BOOL isError;
@end

// A file's text as UTF-8 with LF line ends, and how the file stores it.
@interface DBCTextFile : NSObject
@property(nonatomic, copy) NSString* text;
@property(nonatomic) BOOL windows1252;
@property(nonatomic) BOOL bom;
@property(nonatomic) BOOL crlf;
@end

// A tab of the editor, as the bridge asks the interface for it.
@interface DBCOpenDocument : NSObject
// Empty for an untitled tab.
@property(nonatomic, copy) NSString* path;
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* text;
@property(nonatomic) BOOL dirty;
@property(nonatomic) BOOL basic;
@property(nonatomic) BOOL windows1252;
@property(nonatomic) BOOL bom;
@property(nonatomic) BOOL crlf;
@end

@interface DBCProjectLocation : NSObject
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* path;
@property(nonatomic, copy) NSString* hint;
@property(nonatomic) BOOL writable;
@property(nonatomic) BOOL samples;
@property(nonatomic, copy) NSArray<NSString*>* projects;
@end

@interface DBCFileEntry : NSObject
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* path;
@property(nonatomic) BOOL directory;
@property(nonatomic) DBCFileKind kind;
@end

@interface DBCOutlineEntry : NSObject
@property(nonatomic, copy) NSString* name;
@property(nonatomic) NSInteger line;
@property(nonatomic) NSInteger count;
@end

@interface DBCOutline : NSObject
@property(nonatomic, copy) NSArray<DBCOutlineEntry*>* functions;
@property(nonatomic, copy) NSArray<DBCOutlineEntry*>* labels;
@property(nonatomic, copy) NSArray<DBCOutlineEntry*>* types;
@property(nonatomic, copy) NSArray<DBCOutlineEntry*>* variables;
@end

@interface DBCMediaFile : NSObject
@property(nonatomic, copy) NSString* path;
@property(nonatomic, copy) NSString* relativeName;
// A core::MediaKind.
@property(nonatomic) NSInteger kind;
@property(nonatomic, copy) NSString* sizeText;
@end

@interface DBCSnippet : NSObject
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* summary;
@property(nonatomic, copy) NSString* body;
@property(nonatomic) BOOL user;
@property(nonatomic) NSInteger index;
@end

// Where a snippet goes; `error` is set when it cannot be inserted.
@interface DBCSnippetInsertion : NSObject
// 0 at the cursor, 1 at the start of `line` (0-based), 2 at the end of the document.
@property(nonatomic) NSInteger place;
@property(nonatomic) NSInteger line;
@property(nonatomic, copy) NSString* text;
@property(nonatomic, copy) NSString* error;
@end

@interface DBCLibraryTexture : NSObject
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* previewPath;
@property(nonatomic) BOOL cubeMap;
@property(nonatomic) BOOL normalMap;
@property(nonatomic) BOOL inProject;
@property(nonatomic) NSInteger index;
@end

@interface DBCCommandHelp : NSObject
@property(nonatomic, copy) NSArray<NSString*>* signatures;
@property(nonatomic, copy) NSString* warning;
@property(nonatomic, copy) NSString* text;
@end

@interface DBCCompletionItem : NSObject
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* detail;
// A dba::CompletionItem::Kind.
@property(nonatomic) NSInteger kind;
// What accepting the item writes.
@property(nonatomic, copy) NSString* insertion;
@property(nonatomic, strong) DBCCommandHelp* help;
@end

@interface DBCCompletion : NSObject
@property(nonatomic, copy) NSArray<DBCCompletionItem*>* items;
// UTF-16 offsets in the line of what accepting an item replaces.
@property(nonatomic) NSInteger replaceStart;
@property(nonatomic) NSInteger replaceEnd;
// 0 none, 1 soft (Tab accepts), 2 hard (Tab and Return accept).
@property(nonatomic) NSInteger selection;
@end

@interface DBCSignature : NSObject
@property(nonatomic, copy) NSString* prefix;
@property(nonatomic, copy) NSArray<NSString*>* parameters;
@property(nonatomic, copy) NSString* suffix;
// The parameter being typed, or -1 when this overload stops earlier.
@property(nonatomic) NSInteger active;
@end

@interface DBCProjectResolution : NSObject
@property(nonatomic, copy) NSString* message;
@property(nonatomic, copy) NSString* remappablePath;
@property(nonatomic, copy) NSArray<NSString*>* candidates;
@end

// One row of a form: the Inspector, the Level Settings and the paint tool are lists of these, drawn
// one below the other.
typedef NS_ENUM(NSInteger, DBCFieldKind) {
    DBCFieldKindHeading,
    DBCFieldKindHint,
    DBCFieldKindWarning,
    DBCFieldKindText,
    DBCFieldKindNumber,
    DBCFieldKindToggle,
    DBCFieldKindColor,
    DBCFieldKindChoice,
    DBCFieldKindAsset,
    DBCFieldKindButton,
};

@interface DBCField : NSObject
@property(nonatomic) NSInteger identifier;
@property(nonatomic) DBCFieldKind kind;
@property(nonatomic, copy) NSString* label;
@property(nonatomic, copy) NSString* tooltip;
@property(nonatomic) BOOL enabled;
// A value the level may leave to the engine: `isSet` says whether a statement sets it.
@property(nonatomic) BOOL optional;
@property(nonatomic) BOOL isSet;
@property(nonatomic) double number;
@property(nonatomic) double minimum;
@property(nonatomic) double maximum;
@property(nonatomic) double step;
@property(nonatomic) BOOL integer;
@property(nonatomic) BOOL slider;
@property(nonatomic, copy) NSString* text;
@property(nonatomic) BOOL flag;
// 0x00RRGGBB.
@property(nonatomic) NSInteger color;
@property(nonatomic, copy) NSArray<NSString*>* choices;
@property(nonatomic) NSInteger choice;
// The core::MediaKind an asset field takes, or -1 for any file.
@property(nonatomic) NSInteger assetKind;
@end

typedef NS_ENUM(NSInteger, DBCForm) {
    DBCFormInspector,
    DBCFormLevelSettings,
    DBCFormPaint,
    DBCFormSnapSteps,
    DBCFormSculpt,
};

@interface DBCSceneRow : NSObject
// Unique in the list: a node, a painted group or the level camera.
@property(nonatomic, copy) NSString* key;
@property(nonatomic, copy) NSString* name;
@property(nonatomic, copy) NSString* tooltip;
// A scene::NodeKind, or -1 for the level camera and -2 for a painted group.
@property(nonatomic) NSInteger kind;
@property(nonatomic) BOOL hidden;
@property(nonatomic) BOOL selected;
@property(nonatomic, copy, nullable) NSArray<DBCSceneRow*>* children;
@end

typedef NS_ENUM(NSInteger, DBCSceneTool) {
    DBCSceneToolView,
    DBCSceneToolMove,
    DBCSceneToolRotate,
    DBCSceneToolScale,
    DBCSceneToolPaint,
    DBCSceneToolSculpt,
};

@protocol DBCStudioDelegate <NSObject>
- (void)studioAppendOutput:(NSString*)line severity:(DBCSeverity)severity;
- (void)studioStatusChanged:(NSString*)status;
- (void)studioProblemsChanged:(NSArray<DBCDiagnostic*>*)problems;
// The project root, its language or its sources changed.
- (void)studioProjectChanged;
- (void)studioRunStateChanged:(BOOL)running;
// The project needs an answer: its language, its file or a local path.
- (void)studioNeedsProjectResolution;
- (void)studioOpenProjectSettings;
// The level, its selection or its tools changed outside a form edit.
- (void)studioSceneChanged;

// The tabs of the editor.
- (NSArray<DBCOpenDocument*>*)studioOpenDocuments;
- (nullable NSString*)studioCurrentPath;
// False when the file cannot be shown as text.
- (BOOL)studioOpenInEditor:(NSString*)path;
// Replaces the text of an open tab as one undoable edit; false when no tab holds the file.
- (BOOL)studioReplaceText:(NSString*)text atPath:(NSString*)path saved:(BOOL)saved;
- (void)studioGoToLine:(NSInteger)line;
- (void)studioPreviewFile:(NSString*)path;
- (void)studioCloseAllDocuments;
- (void)studioShowCodeMode;
@end

@interface DBCStudio : NSObject

@property(class, nonatomic, readonly) DBCStudio* shared;
@property(nonatomic, weak, nullable) id<DBCStudioDelegate> delegate;

// Starts SDL, loads the settings and the language data. False when the Studio cannot run.
- (BOOL)start;
// Reopens the last project and reports what a crash left behind. Call once the delegate is set.
- (void)restoreSession;
- (void)shutdown;
// Called by the interface about sixty times a second: runs the program in progress.
- (void)tick;
- (void)applicationWillResignActive;
- (void)applicationDidEnterBackground;
- (void)applicationWillEnterForeground;
- (void)applicationDidBecomeActive;

// Interface text. The English source is the key.
+ (NSString*)tr:(NSString*)source;
@property(nonatomic, readonly) NSString* versionString;
@property(nonatomic, readonly) NSString* noticePath;

// Preferences.
@property(nonatomic, copy) NSString* theme;
@property(nonatomic, copy) NSString* language;
@property(nonatomic, readonly) NSArray<NSArray<NSString*>*>* interfaceLanguages;
@property(nonatomic, copy) NSString* logLevel;
@property(nonatomic) BOOL debugOverlay;
@property(nonatomic) BOOL showStatusBar;
@property(nonatomic) BOOL virtualPadStick;
@property(nonatomic) double textZoom;
@property(nonatomic) double scrollSpeed;
@property(nonatomic) double cameraSpeed;
@property(nonatomic) NSInteger interfacePercent;
@property(nonatomic, readonly) BOOL hasPathRemappings;
- (void)clearPathRemappings;
// A colour of the Studio's palette as 0xAARRGGBB, for the current appearance.
- (NSInteger)paletteColor:(NSString*)name;
// Follows the system's appearance when the theme setting is automatic.
- (void)setSystemAppearanceLight:(BOOL)light;
@property(nonatomic, readonly) BOOL appearanceIsLight;

// Projects.
@property(nonatomic, readonly) NSArray<DBCProjectLocation*>* locations;
@property(nonatomic, readonly) NSString* importRoot;
@property(nonatomic, readonly) NSString* projectRoot;
@property(nonatomic, readonly) BOOL hasResolvedProject;
@property(nonatomic, readonly) DBCProfile profile;
@property(nonatomic, readonly) NSString* dialectOrigin;
@property(nonatomic, readonly) BOOL dialectLocked;
@property(nonatomic, readonly, nullable) NSString* descriptorPath;
@property(nonatomic, readonly) NSString* entrySource;
@property(nonatomic, readonly) NSArray<NSString*>* orderedSources;
@property(nonatomic, readonly) NSString* assetRoot;
- (void)openLocationProject:(NSString*)folder samples:(BOOL)samples;
- (void)openProjectFolder:(NSString*)folder;
// A project of `importRoot`: empty on success, else why not. Deleting or renaming the open
// project closes it first.
- (NSString*)deleteProject:(NSString*)folder;
- (NSString*)duplicateProject:(NSString*)folder;
- (NSString*)renameProject:(NSString*)folder to:(NSString*)name;
// Opens a folder picked in the system's file browser, anywhere on the device, and keeps the access
// the pick granted for the next launches.
- (void)openPickedFolder:(NSURL*)url;
- (void)openFile:(NSString*)path;
- (NSString*)validateProjectName:(NSString*)name;
// Empty on success, else why the project was not created.
- (NSString*)createProjectNamed:(NSString*)name profile:(DBCProfile)profile basicScene:(BOOL)basicScene;
- (void)setProjectDialect:(DBCProfile)profile;
@property(nonatomic, readonly, nullable) DBCProjectResolution* pendingResolution;
- (void)resolveWithCandidate:(NSString*)path;
- (void)grantRemapping:(NSString*)localPath;
- (NSArray<DBCFileEntry*>*)entriesOfFolder:(NSString*)folder;
+ (DBCFileKind)kindOfFile:(NSString*)path;
- (NSArray<DBCMediaFile*>*)mediaFilesTruncated:(BOOL*)truncated;
+ (NSString*)mediaKindLabel:(NSInteger)kind;

// Files.
- (nullable DBCTextFile*)readTextFile:(NSString*)path;
// Writes `document` to its path, or to `path` when given. Returns the format written, with `notice`
// set when it had to change; nil with `error` set on failure.
- (nullable DBCTextFile*)writeDocument:(DBCOpenDocument*)document
                                toPath:(NSString*)path
                                notice:(NSString* _Nullable* _Nonnull)notice
                                 error:(NSString* _Nullable* _Nonnull)error;
// A tab was saved: compiles it and lists its diagnostics.
- (void)documentSaved;
- (void)noteLastFile:(NSString*)path;

// The language.
- (NSArray<NSString*>*)keywordsForProfile:(DBCProfile)profile;
- (NSArray<NSString*>*)commandWordsForProfile:(DBCProfile)profile;
- (DBCOutline*)outlineOfText:(NSString*)text path:(NSString*)path;
// Tells completion which names the program declares. Call after the text settles.
- (void)refreshSymbolsForText:(NSString*)text path:(NSString*)path;
- (nullable DBCCompletion*)completeLine:(NSString*)line
                                 cursor:(NSInteger)cursor
                                 manual:(BOOL)manual
                             lineNumber:(NSInteger)lineNumber
                            textBefore:(NSString*)textBefore;
- (NSArray<DBCSignature*>*)parameterHintForLine:(NSString*)lineBeforeCursor textBefore:(NSString*)textBefore;
- (nullable DBCCommandHelp*)helpForWord:(NSString*)word;
- (NSArray<DBCSnippet*>*)snippets;
@property(nonatomic, readonly) NSString* snippetFolder;
- (DBCSnippetInsertion*)planSnippet:(NSInteger)index inText:(NSString*)text;
// Empty on success.
- (NSString*)saveSnippetNamed:(NSString*)name
                      summary:(NSString*)summary
                         body:(NSString*)body
                    bothKinds:(BOOL)bothKinds;

// Runs.
@property(nonatomic, readonly) BOOL running;
- (void)runCurrentDocument;
- (void)stopRun;
@property(nonatomic, readonly) BOOL canDumpDiagnostics;
- (void)dumpDiagnostics;

// Recovery of unsaved tabs.
- (NSArray<DBCOpenDocument*>*)pendingRecoveries;
- (void)discardRecoveries;
- (void)updateRecovery;

// Project settings.
- (NSDictionary<NSString*, id>*)projectSettings;
// Empty on success.
- (NSString*)saveProjectSettings:(NSDictionary<NSString*, id>*)values;
@property(nonatomic, readonly) NSString* projectSettingsPath;
- (void)generateIcons;

// The offline reference: the folder of its pages, published on first use.
- (nullable NSString*)referenceIndexPath;

// The texture library.
- (NSArray<DBCLibraryTexture*>*)libraryTextures;
- (void)addLibraryTexture:(NSInteger)index;

@end

// The Scene mode (DBCScene.mm).
@interface DBCStudio (Scene)
// Why the Scene mode cannot open, already translated; empty when it can.
@property(nonatomic, readonly) NSString* sceneBlocker;
- (BOOL)enterSceneMode;
// Saves the level, then runs the project's main source.
- (void)runScene;
- (void)leaveSceneMode;
@property(nonatomic, readonly) NSString* levelName;
@property(nonatomic, readonly) NSArray<NSString*>* levelNames;
@property(nonatomic, readonly) BOOL sceneDirty;
@property(nonatomic, readonly) BOOL sceneEditable;
@property(nonatomic, readonly) NSArray<NSString*>* sceneReadErrors;
- (void)openLevel:(NSString*)name;
- (void)newLevel:(NSString*)name;
- (BOOL)saveScene;
- (NSArray<DBCSceneRow*>*)sceneRows;
- (void)selectSceneRow:(NSString*)key toggle:(BOOL)toggle range:(BOOL)range;
- (void)clearSceneSelection;
- (void)frameSceneSelection;
@property(nonatomic, readonly) BOOL sceneHasSelection;
@property(nonatomic, readonly) BOOL sceneCanUndo;
@property(nonatomic, readonly) BOOL sceneCanRedo;
- (void)sceneUndo;
- (void)sceneRedo;
- (void)sceneDuplicate;
- (void)sceneDelete;
- (void)sceneSelectAll;
- (void)sceneAlignToGrid;
// A scene::NodeKind and, for a light, a scene::LightKind.
- (void)sceneCreate:(NSInteger)kind light:(NSInteger)light;
- (void)sceneCreateFromAsset:(NSString*)relativePath;
- (void)sceneToggleLevelCamera;
@property(nonatomic, readonly) BOOL sceneUsesLevelCamera;
// Double tap of an Assets tile: textures the selection, or adds a model or a sound.
- (void)sceneActivateAsset:(NSString*)relativePath;
// A tile dropped on the viewport, at a point of the viewport view.
- (void)sceneDropAsset:(NSString*)relativePath at:(CGPoint)point;
- (void)sceneDropPrimitive:(NSInteger)kind at:(CGPoint)point;
- (NSArray<DBCField*>*)sceneForm:(DBCForm)form;
// Applies a value to a field of the form as it was last built. `commit` ends the edit: it becomes
// one undo step.
- (void)sceneForm:(DBCForm)form setNumber:(double)value field:(NSInteger)field commit:(BOOL)commit;
- (void)sceneForm:(DBCForm)form setText:(NSString*)value field:(NSInteger)field commit:(BOOL)commit;
- (void)sceneForm:(DBCForm)form setFlag:(BOOL)value field:(NSInteger)field;
- (void)sceneForm:(DBCForm)form setOptional:(BOOL)isSet field:(NSInteger)field;
- (void)sceneForm:(DBCForm)form press:(NSInteger)field;
@property(nonatomic) DBCSceneTool sceneTool;
@property(nonatomic) BOOL sceneLocalGizmo;
@property(nonatomic) BOOL sceneSnap;
@property(nonatomic) BOOL sceneGrid;
@property(nonatomic) BOOL sceneCameraView;
@property(nonatomic, readonly) double sceneMoveStep;
// Writes what the viewport's renderer last drew, without its gizmos, as a PNG: a check for
// builds nobody is looking at. False when there is nothing to write.
- (BOOL)writeViewportImage:(NSString*)path;
// Where the editor's camera is, as text: the same kind of check.
@property(nonatomic, readonly) NSString* sceneCameraDescription;
// The model an Assets tile shows in the Inspector instead of the selection; empty for none.
@property(nonatomic, copy) NSString* sceneInspectedModel;

@end

// The Scene viewport: a Metal view the level editor draws into, with its gizmos.
@interface DBCViewportView : UIView
@end

NS_ASSUME_NONNULL_END
