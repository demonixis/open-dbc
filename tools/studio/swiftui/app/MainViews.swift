import SwiftUI
import UniformTypeIdentifiers
import UIKit

/// The Studio's window: a toolbar, the Code or the Scene mode, and the status bar.
struct RootView: View {
    @ObservedObject var model: StudioModel
    @Environment(\.colorScheme) private var colorScheme

    var body: some View {
        GeometryReader { geometry in
            let compact = geometry.size.width < 700
            VStack(spacing: 0) {
                ToolbarView(model: model, compact: compact)
                Divider()
                if model.mode == .code {
                    CodeModeView(model: model, compact: compact)
                } else {
                    SceneModeView(model: model, compact: compact)
                }
                if model.studio.showStatusBar {
                    Divider()
                    StatusBar(model: model)
                }
            }
        }
        .background(Palette.color("Background").ignoresSafeArea())
        .foregroundColor(Palette.color("Text"))
        .tint(Palette.color("Accent"))
        .preferredColorScheme(preferredScheme)
        .dynamicTypeSize(interfaceSize)
        .onAppear { model.studio.setSystemAppearanceLight(colorScheme == .light) }
        .onChange(of: colorScheme) { scheme in
            model.studio.setSystemAppearanceLight(scheme == .light)
            model.themeRevision += 1
        }
        .sheet(item: $model.sheet) { sheet in
            StudioSheetView(model: model, sheet: sheet)
        }
        .alert(model.message ?? "", isPresented: Binding(get: { model.message != nil },
                                                         set: { if !$0 { model.message = nil } })) {
            Button(tr("OK")) { model.message = nil }
        }
        .fileImporter(isPresented: $model.pickingFolder, allowedContentTypes: [.folder]) { result in
            if case .success(let folder) = result {
                model.studio.openPickedFolder(folder)
            }
        }
        .sheet(item: Binding(get: { model.previewPath.map(PreviewItem.init) },
                             set: { model.previewPath = $0?.path })) { item in
            AssetPreviewView(path: item.path) { model.previewPath = nil }
        }
        .confirmationDialog(tr("Unsaved changes"), isPresented: Binding(get: { model.closing != nil },
                                                                           set: { if !$0 { model.closing = nil } }),
                            titleVisibility: .visible) {
            Button(tr("Save")) {
                if let document = model.closing {
                    model.save(document)
                    if !document.dirty { model.close(document) }
                }
                model.closing = nil
            }
            Button(tr("Discard"), role: .destructive) {
                if let document = model.closing { model.close(document) }
                model.closing = nil
            }
            Button(tr("Cancel"), role: .cancel) { model.closing = nil }
        }
        .id(model.themeRevision)
    }

    private var preferredScheme: ColorScheme? {
        switch model.studio.theme {
        case "light": return .light
        case "auto": return nil
        default: return .dark
        }
    }

    /// The interface size setting, as the nearest text size the system has.
    private var interfaceSize: DynamicTypeSize {
        switch model.studio.interfacePercent {
        case ..<60: return .xSmall
        case ..<80: return .small
        case ..<95: return .medium
        case ..<108: return .large
        case ..<118: return .xLarge
        case ..<132: return .xxLarge
        default: return .xxxLarge
        }
    }
}

struct PreviewItem: Identifiable {
    let path: String
    var id: String { path }
}

struct ToolbarView: View {
    @ObservedObject var model: StudioModel
    let compact: Bool

    var body: some View {
        let blocker = model.studio.sceneBlocker
        HStack(spacing: compact ? 10 : 14) {
            Menu {
                StudioMenus(model: model)
            } label: {
                Image(systemName: "line.3.horizontal").frame(width: 30, height: 30)
            }
            .accessibilityLabel(tr("Menus"))
            Button { model.newFile() } label: { Image(systemName: "doc.badge.plus") }
                .accessibilityLabel(tr("New"))
            Button { model.saveCurrent() } label: { Image(systemName: "square.and.arrow.down") }
                .disabled(model.mode == .code && model.current == nil)
                .accessibilityLabel(tr("Save"))
            Divider().frame(height: 22)
            Button { model.run() } label: { Image(systemName: "play.fill").foregroundColor(Palette.color("Run")) }
                .disabled(model.running)
                .accessibilityLabel(tr("Run"))
            Button { model.studio.stopRun() } label: { Image(systemName: "stop.fill") }
                .disabled(!model.running)
                .accessibilityLabel(tr("Stop"))
            Spacer(minLength: 4)
            Picker("", selection: Binding(get: { model.mode == .code ? 0 : 1 },
                                          set: { $0 == 0 ? model.showCode() : model.showScene() })) {
                Text(tr("Code")).tag(0)
                Text(tr("Scene")).tag(1)
            }
            .pickerStyle(.segmented)
            .frame(width: compact ? 130 : 200)
            .disabled(!blocker.isEmpty)
            .help(blocker)
            Spacer(minLength: 4)
            Button { withAnimation { model.showSidebar.toggle() } } label: { Image(systemName: "sidebar.left") }
            Button { withAnimation { model.showBottomPanel.toggle() } } label: {
                Image(systemName: "rectangle.bottomthird.inset.filled")
            }
            if model.mode == .scene {
                Button { withAnimation { model.showInspector.toggle() } } label: { Image(systemName: "sidebar.right") }
            }
        }
        .font(.system(size: 18))
        .padding(.horizontal, 12)
        .frame(height: 46)
        .background(Palette.color("Panel"))
        .id(model.projectRevision)
    }
}

struct StatusBar: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        HStack(spacing: 10) {
            Text(model.status).lineLimit(1)
            Spacer()
            if model.studio.hasResolvedProject {
                Text(tr("Dialect ({0}):", model.studio.dialectOrigin)).foregroundColor(Palette.color("TextDim"))
                Picker("", selection: Binding(get: { model.studio.profile },
                                              set: { model.studio.setProjectDialect($0) })) {
                    Text("DBC").tag(DBCProfile.dbc)
                    Text("DBPro").tag(DBCProfile.dbPro)
                }
                .pickerStyle(.menu)
                .disabled(model.running || model.studio.dialectLocked)
            }
        }
        .font(.footnote)
        .padding(.horizontal, 10)
        .frame(height: 28)
        .background(Palette.color("Panel"))
        .id(model.projectRevision)
    }
}

// MARK: Code mode

struct CodeModeView: View {
    @ObservedObject var model: StudioModel
    let compact: Bool
    @State private var compactPanel = 1
    @AppStorage("code.sidebarWidth") private var sidebarWidth = 290.0
    @AppStorage("code.bottomHeight") private var bottomHeight = 230.0

    var body: some View {
        if compact {
            VStack(spacing: 0) {
                Picker("", selection: $compactPanel) {
                    Text(tr("Project")).tag(0)
                    Text(tr("Editor")).tag(1)
                    Text(tr("Output")).tag(2)
                }
                .pickerStyle(.segmented)
                .padding(6)
                switch compactPanel {
                case 0: SidebarView(model: model) { compactPanel = 1 }
                case 2: BottomPanel(model: model, closable: false)
                default: EditorArea(model: model)
                }
            }
        } else {
            HStack(spacing: 0) {
                if model.showSidebar {
                    SidebarView(model: model) {}
                        .frame(width: sidebarWidth)
                    ResizeHandle(size: $sidebarWidth, range: 180...560, vertical: false, grows: 1)
                }
                VStack(spacing: 0) {
                    EditorArea(model: model)
                    if model.showBottomPanel {
                        ResizeHandle(size: $bottomHeight, range: 100...700, vertical: true, grows: -1)
                        BottomPanel(model: model, closable: true)
                            .frame(height: bottomHeight)
                    }
                }
            }
        }
    }
}

struct EditorArea: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        VStack(spacing: 0) {
            if !model.documents.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 1) {
                        ForEach(model.documents) { document in
                            EditorTab(model: model, document: document)
                        }
                    }
                }
                .frame(height: 36)
                .background(Palette.color("Panel"))
            }
            if let document = model.current {
                CodeEditor(document: document, zoom: model.textZoom, themeRevision: model.themeRevision,
                           projectRevision: model.projectRevision)
                    .id(document.id)
            } else {
                Spacer()
                Text(tr("No file open. Use File > Open, or pick one in the Project panel."))
                    .foregroundColor(Palette.color("TextDim"))
                    .multilineTextAlignment(.center)
                    .padding()
                Spacer()
            }
        }
    }
}

struct EditorTab: View {
    @ObservedObject var model: StudioModel
    @ObservedObject var document: EditorDocument

    var body: some View {
        let selected = model.currentID == document.id
        HStack(spacing: 6) {
            Text(document.title + (document.dirty ? " *" : ""))
                .font(.subheadline)
                .lineLimit(1)
            Button { model.requestClose(document) } label: {
                Image(systemName: "xmark").font(.system(size: 11, weight: .bold))
            }
            .buttonStyle(.plain)
            .foregroundColor(Palette.color("TextDim"))
            .accessibilityLabel(tr("Close"))
        }
        .padding(.horizontal, 12)
        .frame(height: 36)
        .background(selected ? Palette.color("Background") : Palette.color("PanelAlt"))
        .contentShape(Rectangle())
        .onTapGesture { model.currentID = document.id }
        .help(document.path)
        .contextMenu {
            Button(tr("Close")) { model.requestClose(document) }
            Button(tr("Close All")) {
                for other in model.documents.reversed() { model.requestClose(other) }
            }
            Button(tr("Close to the Right")) { model.closeOthers(after: document) }
        }
    }
}

// MARK: Output and problems

struct BottomPanel: View {
    @ObservedObject var model: StudioModel
    let closable: Bool

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: "") {
                Picker("", selection: $model.bottomPanel) {
                    Text(tr("Output")).tag(0)
                    Text(tr("Problems") + (model.problems.isEmpty ? "" : " (\(model.problems.count))")).tag(1)
                }
                .pickerStyle(.segmented)
                .frame(maxWidth: 320)
                Spacer()
                if closable {
                    Button { model.showBottomPanel = false } label: { Image(systemName: "xmark") }
                }
            }
            if model.bottomPanel == 0 {
                OutputView(model: model)
            } else {
                ProblemsView(model: model)
            }
        }
        .background(Palette.color("Background"))
    }
}

struct OutputView: View {
    @ObservedObject var model: StudioModel
    @State private var filter = ""
    @State private var autoScroll = true

    private var lines: [OutputLine] {
        filter.isEmpty ? model.output : model.output.filter { $0.text.localizedCaseInsensitiveContains(filter) }
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Button(tr("Clear")) { model.output.removeAll() }
                Button(tr("Copy all")) {
                    UIPasteboard.general.string = lines.map(\.text).joined(separator: "\n")
                }
                Toggle(tr("Auto-scroll"), isOn: $autoScroll).fixedSize()
                TextField(tr("Filter"), text: $filter)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
            }
            .font(.footnote)
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(lines) { line in
                            Text(line.text)
                                .font(.system(size: 12 * model.textZoom, design: .monospaced))
                                .foregroundColor(color(line.severity))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .textSelection(.enabled)
                                .contextMenu {
                                    Button(tr("Copy line")) { UIPasteboard.general.string = line.text }
                                    Button(tr("Copy all")) {
                                        UIPasteboard.general.string = lines.map(\.text).joined(separator: "\n")
                                    }
                                }
                                .id(line.id)
                        }
                    }
                    .padding(.horizontal, 10)
                }
                .onChange(of: model.output.count) { _ in
                    if autoScroll, let last = lines.last { proxy.scrollTo(last.id, anchor: .bottom) }
                }
            }
        }
    }

    private func color(_ severity: DBCSeverity) -> Color {
        switch severity {
        case .error: return Palette.color("Error")
        case .notice: return Palette.color("Identifier")
        default: return Palette.color("Text")
        }
    }
}

struct ProblemsView: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        if model.problems.isEmpty {
            Text(tr("No problems in the last run or check."))
                .foregroundColor(Palette.color("TextDim"))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            List(Array(model.problems.enumerated()), id: \.offset) { _, problem in
                Button {
                    guard !problem.path.isEmpty, problem.line > 0 else { return }
                    model.mode = .code
                    model.studio.openFile(problem.path)
                    model.goToLine(problem.line)
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(problem.message)
                            .foregroundColor(Palette.color(problem.isError ? "Error" : "Identifier"))
                        Text("\(problem.path.fileName):\(problem.line)  \(problem.code)")
                            .font(.caption)
                            .foregroundColor(Palette.color("TextDim"))
                    }
                }
                .help(problem.path)
                .listRowBackground(Palette.color("Background"))
            }
            .listStyle(.plain)
        }
    }
}
