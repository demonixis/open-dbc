import SwiftUI

private let logLevels = ["trace", "debug", "info", "warn", "error", "off"]

private struct MenuShortcutsKey: EnvironmentKey {
    static let defaultValue = false
}

extension EnvironmentValues {
    /// True in the menu bar. The toolbar's menu shows the same items, whose shortcuts would
    /// otherwise be registered twice.
    var menuShortcuts: Bool {
        get { self[MenuShortcutsKey.self] }
        set { self[MenuShortcutsKey.self] = newValue }
    }
}

private struct StudioShortcut: ViewModifier {
    @Environment(\.menuShortcuts) private var enabled
    let key: KeyEquivalent
    let modifiers: EventModifiers

    func body(content: Content) -> some View {
        content.keyboardShortcut(enabled ? KeyboardShortcut(key, modifiers: modifiers) : nil)
    }
}

extension View {
    func studioShortcut(_ key: KeyEquivalent, modifiers: EventModifiers = .command) -> some View {
        modifier(StudioShortcut(key: key, modifiers: modifiers))
    }
}

/// Every menu of the Studio. The toolbar's menu button shows them as submenus; with a keyboard,
/// the same items are the menu bar's.
struct StudioMenus: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        Menu(tr("File")) { FileMenu(model: model) }
        Menu(tr("Edit")) { EditMenu(model: model) }
        if model.mode == .scene {
            Menu(tr("Scene")) { SceneMenu(model: model) }
            Menu(tr("Create")) { CreateMenu(model: model) }
        }
        Menu(tr("Assets")) {
            Button(tr("Texture Library...")) { model.sheet = .textureLibrary }
        }
        Menu(tr("Build")) { BuildMenu(model: model) }
        Menu(tr("View")) { ViewMenu(model: model) }
        Menu(tr("Help")) {
            Button(tr("DBA Reference")) { model.sheet = .reference }
            Button(tr("About")) { model.sheet = .about }
        }
    }
}

struct FileMenu: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        Button(tr("New")) { model.newFile() }
            .studioShortcut("n")
        Button(tr("New Project...")) { model.sheet = .newProject }
            .studioShortcut("n", modifiers: [.command, .shift])
            .disabled(model.running)
        Button(tr("New Level...")) {
            model.showScene()
            if model.mode == .scene { model.sheet = .newLevel }
        }
        .disabled(!model.studio.sceneBlocker.isEmpty)
        Button(tr("Projects...")) { model.sheet = .openLocation }
            .studioShortcut("o")
            .disabled(model.running)
        Button(tr("Open Folder...")) { model.pickFolder() }
            .studioShortcut("o", modifiers: [.command, .shift])
            .disabled(model.running)
        Button(tr("Export Project...")) { model.exportProject() }
            .disabled(model.studio.projectRoot.isEmpty)
        Divider()
        Button(tr("Save")) { model.saveCurrent() }
            .studioShortcut("s")
            .disabled(model.mode == .code && model.current == nil)
        Button(tr("Save As...")) { model.sheet = .saveAs }
            .studioShortcut("s", modifiers: [.command, .shift])
            .disabled(model.mode != .code || model.current == nil)
        Button(tr("Save All")) {
            model.saveAll()
            model.studio.documentSaved()
        }
        .disabled(!model.anyDirty)
    }
}

struct EditMenu: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        let textView = model.mode == .code ? model.current?.textView : nil
        UndoRedoItems(model: model)
        Button(tr("Find")) { textView?.findInteraction?.presentFindNavigator(showingReplace: false) }
            .studioShortcut("r")
            .disabled(textView == nil)
        Button(tr("Find and Replace")) { textView?.findInteraction?.presentFindNavigator(showingReplace: true) }
            .studioShortcut("r", modifiers: [.command, .shift])
            .disabled(textView == nil)
        Button(tr("Trim Trailing Whitespace")) { model.trimTrailingWhitespace() }
            .disabled(textView == nil)
        Divider()
        Button(tr("Insert Snippet...")) { model.sheet = .snippets }
            .studioShortcut("j")
            .disabled(!(model.mode == .code && (model.current?.isBasic ?? false)))
        Button(tr("Save Selection as Snippet...")) { model.sheet = .saveSnippet }
            .disabled((textView?.selectedRange.length ?? 0) == 0)
        Divider()
        Button(tr("Editor Settings...")) { model.sheet = .editorSettings }
    }
}

/// Undo and Redo of what the mode edits: the tab's text or the level. The menu bar has one pair of
/// them, with one pair of shortcuts.
struct UndoRedoItems: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        let studio = model.studio
        let manager = model.current?.textView?.undoManager
        let scene = model.mode == .scene
        Button(tr("Undo")) { scene ? studio.sceneUndo() : manager?.undo() }
            .studioShortcut("z")
            .disabled(scene ? !studio.sceneCanUndo : !(manager?.canUndo ?? false))
        Button(tr("Redo")) { scene ? studio.sceneRedo() : manager?.redo() }
            .studioShortcut("z", modifiers: [.command, .shift])
            .disabled(scene ? !studio.sceneCanRedo : !(manager?.canRedo ?? false))
    }
}

struct SceneMenu: View {
    @ObservedObject var model: StudioModel
    @Environment(\.menuShortcuts) private var inMenuBar

    var body: some View {
        let studio = model.studio
        // The menu bar drops a menu that repeats an item of another one, and File has this one.
        if !inMenuBar {
            Button(tr("New Level...")) { model.sheet = .newLevel }
        }
        Menu(tr("Open Level")) {
            ForEach(studio.levelNames, id: \.self) { name in
                Button {
                    studio.openLevel(name)
                } label: {
                    if name == studio.levelName {
                        Label(name, systemImage: "checkmark")
                    } else {
                        Text(name)
                    }
                }
            }
        }
        Button(tr("Save Level")) { model.saveScene() }
        Divider()
        Button(tr("Duplicate")) { studio.sceneDuplicate() }
            .studioShortcut("d")
            .disabled(!studio.sceneHasSelection || !studio.sceneEditable)
        Button(tr("Delete"), role: .destructive) { studio.sceneDelete() }
            .disabled(!studio.sceneHasSelection || !studio.sceneEditable)
        Button(tr("Select All")) { studio.sceneSelectAll() }
        Button(tr("Frame Selection")) { studio.frameSceneSelection() }
            .disabled(!studio.sceneHasSelection)
        Button(tr("Align to Grid")) { studio.sceneAlignToGrid() }
            .disabled(!studio.sceneHasSelection)
    }
}

/// The scene::NodeKind values the Create menu makes.
enum NodeKind {
    static let primitives: [(Int, String, String)] = [
        (0, "Cube", "cube"), (1, "Box", "shippingbox"), (2, "Sphere", "circle"), (3, "Cylinder", "cylinder"),
        (4, "Cone", "cone"), (5, "Plain", "square"), (6, "Triangle", "triangle"),
    ]
    static let model = 7
    static let matrix = 8
    static let terrain = 9
    static let light = 10
    static let sound = 11
}

struct CreateMenu: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        let studio = model.studio
        Menu(tr("3D Object")) {
            ForEach(NodeKind.primitives, id: \.0) { kind, name, _ in
                Button(tr(name)) { studio.sceneCreate(kind, light: 0) }
            }
            Menu(tr("Model")) {
                let models = assets(kind: 2)
                ForEach(models.prefix(40), id: \.self) { name in
                    Button(name) { studio.sceneCreate(fromAsset: name) }
                }
                if models.count > 40 { Text(tr("More in the Assets panel")) }
            }
        }
        Menu(tr("Landscape")) {
            Button(tr("Matrix")) { studio.sceneCreate(NodeKind.matrix, light: 0) }
            Button(tr("Terrain")) { studio.sceneCreate(NodeKind.terrain, light: 0) }
        }
        Menu(tr("Light")) {
            Button(tr("Point Light")) { studio.sceneCreate(NodeKind.light, light: 0) }
            Button(tr("Spot Light")) { studio.sceneCreate(NodeKind.light, light: 1) }
            Button(tr("Directional Light")) { studio.sceneCreate(NodeKind.light, light: 2) }
        }
        Menu(tr("Audio")) {
            Menu(tr("3D Sound")) {
                ForEach(assets(kind: 1).prefix(40), id: \.self) { name in
                    Button(name) { studio.sceneCreate(fromAsset: name) }
                }
            }
        }
        Menu(tr("Camera")) {
            Button {
                studio.sceneToggleLevelCamera()
            } label: {
                if studio.sceneUsesLevelCamera {
                    Label(tr("Level Camera"), systemImage: "checkmark")
                } else {
                    Text(tr("Level Camera"))
                }
            }
        }
    }

    private func assets(kind: Int) -> [String] {
        var truncated: ObjCBool = false
        return model.studio.mediaFilesTruncated(&truncated).filter { $0.kind == kind }.map(\.relativeName)
    }
}

struct BuildMenu: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        let studio = model.studio
        Button(tr("Run")) { model.run() }
            .studioShortcut("r", modifiers: [.command, .option])
            .disabled(model.running)
        Button(tr("Stop")) { studio.stopRun() }
            .studioShortcut(".", modifiers: [.command])
            .disabled(!model.running)
        Picker(tr("Log Level"), selection: Binding(get: { studio.logLevel }, set: { studio.logLevel = $0 })) {
            // The setting's own values, shown as they are written.
            ForEach(logLevels, id: \.self) { Text($0).tag($0) }
        }
        .pickerStyle(.menu)
        Toggle(tr("Debug Overlay on Launch"), isOn: Binding(get: { studio.debugOverlay },
                                                            set: { studio.debugOverlay = $0 }))
        Button(tr("Dump Scene and Renderer")) { studio.dumpDiagnostics() }
            .disabled(!studio.canDumpDiagnostics)
        Divider()
        Button(tr("Project Settings...")) { model.sheet = .projectSettings }
            .disabled(studio.projectRoot.isEmpty)
        Button(tr("Generate Icons")) { studio.generateIcons() }
            .disabled(studio.projectRoot.isEmpty)
        Button(tr("Clear Local Path Remappings")) { studio.clearPathRemappings() }
            .disabled(!studio.hasPathRemappings)
    }
}

struct ViewMenu: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        let studio = model.studio
        Button(tr("Code")) { model.showCode() }
            .studioShortcut("1")
        Button(tr("Scene")) { model.showScene() }
            .studioShortcut("2")
            .disabled(!studio.sceneBlocker.isEmpty)
        Divider()
        Toggle(tr("Project"), isOn: $model.showSidebar)
        Toggle(tr("Output"), isOn: Binding(get: { model.showBottomPanel && model.bottomPanel == 0 },
                                           set: { model.bottomPanel = 0; model.showBottomPanel = $0 }))
        Toggle(tr("Problems"), isOn: Binding(get: { model.showBottomPanel && model.bottomPanel == 1 },
                                             set: { model.bottomPanel = 1; model.showBottomPanel = $0 }))
        Toggle(tr("Status Bar"), isOn: Binding(get: { studio.showStatusBar },
                                               set: { studio.showStatusBar = $0; model.projectRevision += 1 }))
        Divider()
        Button(tr("Increase Text Size")) { model.setTextZoom(model.textZoom * 1.1) }
            .studioShortcut("+")
        Button(tr("Decrease Text Size")) { model.setTextZoom(model.textZoom / 1.1) }
            .studioShortcut("-")
        Button(tr("Reset Text Size")) { model.setTextZoom(1) }
            .studioShortcut("0")
        Button(tr("Increase Interface Size")) { setInterface(studio.interfacePercent + 10) }
        Button(tr("Decrease Interface Size")) { setInterface(studio.interfacePercent - 10) }
        Button(tr("Reset Interface Size")) { setInterface(100) }
        Divider()
        Picker(tr("Theme"), selection: Binding(get: { studio.theme }, set: { model.applyTheme($0) })) {
            Text(tr("Automatic")).tag("auto")
            Text(tr("theme|Dark")).tag("dark")
            Text(tr("theme|Light")).tag("light")
        }
        .pickerStyle(.menu)
        Picker(tr("Language"), selection: Binding(get: { studio.language },
                                                  set: { studio.language = $0; model.themeRevision += 1 })) {
            Text(tr("Automatic")).tag("auto")
            ForEach(studio.interfaceLanguages, id: \.self) { language in
                Text(language[1]).tag(language[0])
            }
        }
        .pickerStyle(.menu)
    }

    private func setInterface(_ percent: Int) {
        model.studio.interfacePercent = percent
        model.themeRevision += 1
    }
}

/// The same menus in the menu bar a keyboard brings.
struct StudioCommands: Commands {
    @ObservedObject var model: StudioModel

    var body: some Commands {
        CommandGroup(replacing: .newItem) { FileMenu(model: model).environment(\.menuShortcuts, true) }
        CommandMenu(tr("Scene")) {
            SceneMenu(model: model).disabled(model.mode != .scene).environment(\.menuShortcuts, true)
        }
        CommandMenu(tr("Create")) { CreateMenu(model: model).disabled(model.mode != .scene) }
        CommandMenu(tr("Assets")) {
            Button(tr("Texture Library...")) { model.sheet = .textureLibrary }
        }
        CommandMenu(tr("Build")) { BuildMenu(model: model).environment(\.menuShortcuts, true) }
        CommandGroup(after: .toolbar) { ViewMenu(model: model).environment(\.menuShortcuts, true) }
        CommandGroup(replacing: .undoRedo) { UndoRedoItems(model: model).environment(\.menuShortcuts, true) }
        CommandGroup(after: .textEditing) {
            Button(tr("Trim Trailing Whitespace")) { model.trimTrailingWhitespace() }
            Button(tr("Insert Snippet...")) { model.sheet = .snippets }
            Button(tr("Editor Settings...")) { model.sheet = .editorSettings }
        }
        CommandGroup(replacing: .help) {
            Button(tr("DBA Reference")) { model.sheet = .reference }
            Button(tr("About")) { model.sheet = .about }
        }
    }
}
