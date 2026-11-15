import AVFoundation
import SwiftUI
import UIKit
import WebKit

/// The Studio's dialogs, each a sheet.
struct StudioSheetView: View {
    @ObservedObject var model: StudioModel
    let sheet: StudioSheet

    var body: some View {
        NavigationStack {
            content
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button(tr("Close")) { model.sheet = nil }
                    }
                }
        }
        .foregroundColor(Palette.color("Text"))
        .tint(Palette.color("Accent"))
    }

    @ViewBuilder private var content: some View {
        switch sheet {
        case .newProject: NewProjectSheet(model: model)
        case .openLocation: OpenLocationSheet(model: model)
        case .resolution: ResolutionSheet(model: model)
        case .recovery: RecoverySheet(model: model)
        case .editorSettings: EditorSettingsSheet(model: model)
        case .projectSettings: ProjectSettingsSheet(model: model)
        case .snippets: SnippetSheet(model: model)
        case .saveSnippet: SaveSnippetSheet(model: model)
        case .textureLibrary: TextureLibrarySheet(model: model)
        case .about: AboutSheet(model: model)
        case .reference: ReferenceSheet(model: model)
        case .newLevel: NewLevelSheet(model: model)
        case .saveAs: SaveAsSheet(model: model)
        }
    }
}

struct NewProjectSheet: View {
    @ObservedObject var model: StudioModel
    @State private var name = ""
    @State private var profile = DBCProfile.dbPro
    @State private var basicScene = true
    @State private var error = ""
    @FocusState private var nameFocused: Bool

    var body: some View {
        let nameError = name.isEmpty ? "" : model.studio.validateProjectName(name)
        Form {
            Section {
                TextField(tr("Name"), text: $name)
                    .autocorrectionDisabled()
                    .focused($nameFocused)
                    .onSubmit { create() }
                Text(tr("Location: {0}", model.studio.importRoot))
                    .font(.footnote)
                    .foregroundColor(Palette.color("TextDim"))
            }
            Section(tr("Language")) {
                Picker(tr("Language"), selection: $profile) {
                    Text("DarkBASIC Professional").tag(DBCProfile.dbPro)
                    Text("DarkBASIC Classic").tag(DBCProfile.dbc)
                }
                .pickerStyle(.inline)
                .labelsHidden()
            }
            Section(tr("Template")) {
                Picker(tr("Template"), selection: $basicScene) {
                    VStack(alignment: .leading) {
                        Text(tr("Basic Scene"))
                        Text(tr("A 1280x720 window, sync at 60 FPS and a spinning cube")).font(.caption)
                    }.tag(true)
                    VStack(alignment: .leading) {
                        Text(tr("Empty"))
                        Text(tr("A main loop and nothing else")).font(.caption)
                    }.tag(false)
                }
                .pickerStyle(.inline)
                .labelsHidden()
            }
            Section {
                if !nameError.isEmpty {
                    Text(nameError).foregroundColor(Palette.color("Error"))
                } else if !error.isEmpty {
                    Text(error).foregroundColor(Palette.color("Error"))
                } else if !name.isEmpty {
                    Text(tr("Creates {0} with main.dba and {1}", model.studio.importRoot + "/" + name,
                            profile == .dbPro ? name + ".dbpro" : ".opendbc-project.json"))
                        .font(.footnote)
                        .foregroundColor(Palette.color("TextDim"))
                }
                Button(tr("Create")) { create() }
                    .disabled(name.isEmpty || !nameError.isEmpty)
            }
        }
        .navigationTitle(tr("New Project"))
        .onAppear { nameFocused = true }
    }

    private func create() {
        guard !name.isEmpty, model.studio.validateProjectName(name).isEmpty else { return }
        error = model.studio.createProjectNamed(name, profile: profile, basicScene: basicScene)
        if error.isEmpty {
            model.sheet = nil
        }
    }
}

struct OpenLocationSheet: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        List {
            Section {
                Button(tr("Open Folder...")) { model.pickFolder() }
                    .disabled(model.running)
            } footer: {
                Text(tr("Any folder of this device or of a drive. The Studio keeps access to the folders picked here."))
            }
            ForEach(model.studio.locations, id: \.path) { location in
                Section {
                    if location.projects.isEmpty {
                        Text(tr("No project here")).foregroundColor(Palette.color("TextDim"))
                    }
                    ForEach(location.projects, id: \.self) { folder in
                        Button(folder.fileName) {
                            model.sheet = nil
                            model.studio.openLocationProject(folder, samples: location.samples)
                        }
                        .disabled(model.running)
                    }
                } header: {
                    Text(location.name)
                } footer: {
                    Text(location.hint)
                }
            }
        }
        .navigationTitle(tr("Open Location"))
    }
}

struct ResolutionSheet: View {
    @ObservedObject var model: StudioModel
    @State private var localPath = ""

    var body: some View {
        let resolution = model.studio.pendingResolution
        Form {
            Section {
                Text(resolution?.message ?? "")
            }
            if let resolution, !resolution.remappablePath.isEmpty {
                Section(resolution.remappablePath) {
                    TextField(tr("Local replacement"), text: $localPath)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                    Button(tr("Grant local remapping")) {
                        model.sheet = nil
                        model.studio.grantRemapping(localPath)
                    }
                    .disabled(localPath.isEmpty)
                }
            }
            Section {
                if let resolution, !resolution.candidates.isEmpty {
                    ForEach(resolution.candidates, id: \.self) { candidate in
                        Button(candidate.fileName) {
                            model.sheet = nil
                            model.studio.resolve(withCandidate: candidate)
                        }
                    }
                    Button(tr("Open folder as DBC")) { choose(.dbc) }
                } else {
                    Button("DBC") { choose(.dbc) }
                    Button("DBPro") { choose(.dbPro) }
                }
            }
        }
        .navigationTitle(tr("Select OpenDBC project language"))
    }

    private func choose(_ profile: DBCProfile) {
        model.sheet = nil
        model.studio.setProjectDialect(profile)
    }
}

struct RecoverySheet: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        let copies = model.studio.pendingRecoveries()
        Form {
            Section(tr("OpenDBC did not quit normally. These tabs had unsaved changes:")) {
                ForEach(Array(copies.enumerated()), id: \.offset) { _, copy in
                    Text(copy.path.isEmpty ? copy.title : copy.path)
                }
            }
            Section {
                Button(tr("Restore")) {
                    for copy in copies { restore(copy) }
                    model.studio.discardRecoveries()
                    // The restored tabs get fresh copies before anything else can fail.
                    model.studio.updateRecovery()
                    model.sheet = nil
                }
                Button(tr("Discard"), role: .destructive) {
                    model.studio.discardRecoveries()
                    model.sheet = nil
                }
            }
        }
        .navigationTitle(tr("Recover unsaved changes"))
        .interactiveDismissDisabled()
    }

    private func restore(_ copy: DBCOpenDocument) {
        if !copy.path.isEmpty, model.open(path: copy.path), let document = model.document(at: copy.path) {
            document.textView?.replaceAll(with: copy.text)
            model.textChanged(document, text: copy.text)
            return
        }
        let document = EditorDocument(path: "", title: copy.title.isEmpty ? copy.path.fileName : copy.title,
                                      text: copy.text, kind: .basic)
        document.savedText = ""
        document.dirty = true
        model.documents.append(document)
        model.currentID = document.id
    }
}

struct EditorSettingsSheet: View {
    @ObservedObject var model: StudioModel
    @State private var scrollSpeed = 1.0
    @State private var cameraSpeed = 1.0
    @State private var interface = 100.0
    @State private var stick = false

    var body: some View {
        let studio = model.studio
        Form {
            Section {
                Picker(tr("Language"), selection: Binding(get: { studio.language },
                                                          set: { studio.language = $0; model.themeRevision += 1 })) {
                    Text(tr("Automatic")).tag("auto")
                    ForEach(studio.interfaceLanguages, id: \.self) { language in
                        Text(language[1]).tag(language[0])
                    }
                }
                Picker(tr("Theme"), selection: Binding(get: { studio.theme }, set: { model.applyTheme($0) })) {
                    Text(tr("Automatic")).tag("auto")
                    Text(tr("theme|Dark")).tag("dark")
                    Text(tr("theme|Light")).tag("light")
                }
            }
            Section {
                slider(tr("Scroll speed"), $scrollSpeed, 0.1...5) { studio.scrollSpeed = scrollSpeed }
                slider(tr("Camera speed"), $cameraSpeed, 0.1...5) { studio.cameraSpeed = cameraSpeed }
                slider(tr("Text zoom"), Binding(get: { model.textZoom }, set: { model.setTextZoom($0) }), 0.5...4) {}
                slider(tr("Interface size"), $interface, 25...150, percent: true) {
                    studio.interfacePercent = Int(interface)
                    model.themeRevision += 1
                }
            }
            Section {
                Picker(tr("Virtual pad"), selection: $stick) {
                    Text(tr("Arrow keys")).tag(false)
                    Text(tr("Analog stick")).tag(true)
                }
                .onChange(of: stick) { studio.virtualPadStick = $0 }
            }
        }
        .navigationTitle(tr("Editor Settings"))
        .onAppear {
            scrollSpeed = studio.scrollSpeed
            cameraSpeed = studio.cameraSpeed
            interface = Double(studio.interfacePercent)
            stick = studio.virtualPadStick
        }
    }

    private func slider(_ title: String, _ value: Binding<Double>, _ range: ClosedRange<Double>,
                        percent: Bool = false, done: @escaping () -> Void) -> some View {
        HStack {
            Text(title)
            Spacer()
            Slider(value: value, in: range) { editing in
                if !editing { done() }
            }
            .frame(maxWidth: 220)
            Text(percent ? "\(Int(value.wrappedValue)) %" : String(format: "%.2f", value.wrappedValue))
                .font(.subheadline.monospacedDigit())
                .frame(width: 58, alignment: .trailing)
        }
    }
}

struct ProjectSettingsSheet: View {
    @ObservedObject var model: StudioModel
    @State private var values: [String: Any] = [:]
    @State private var error = ""
    @State private var tab = 0

    private static let iosOrientations = ["LandscapeLeft", "LandscapeRight", "Portrait", "PortraitUpsideDown"]
    private static let androidOrientations = ["sensorLandscape", "landscape", "sensorPortrait", "portrait", "fullSensor"]
    private static let androidPermissions = [
        "INTERNET", "CAMERA", "RECORD_AUDIO", "ACCESS_FINE_LOCATION", "READ_EXTERNAL_STORAGE",
        "WRITE_EXTERNAL_STORAGE", "VIBRATE",
    ]

    var body: some View {
        Form {
            Section {
                Text(model.studio.projectSettingsPath).font(.footnote).foregroundColor(Palette.color("TextDim"))
                Picker("", selection: $tab) {
                    Text(tr("General")).tag(0)
                    Text("iOS").tag(1)
                    Text("Android").tag(2)
                }
                .pickerStyle(.segmented)
            }
            switch tab {
            case 1:
                Section {
                    text("Development team ID", "iosTeamId")
                    text("Deployment target", "iosDeploymentTarget")
                }
                Section(tr("Supported")) { list("iosOrientations", Self.iosOrientations) }
                Section {
                    text("Camera", "iosCameraDesc")
                    text("Microphone", "iosMicrophoneDesc")
                    text("Location", "iosLocationDesc")
                    text("Photo library", "iosPhotoDesc")
                }
            case 2:
                Section {
                    number("Min SDK", "androidMinSdk")
                    number("Target SDK", "androidTargetSdk")
                    Picker(tr("Screen orientation"), selection: stringBinding("androidOrientation")) {
                        ForEach(Self.androidOrientations, id: \.self) { Text($0).tag($0) }
                    }
                }
                Section(tr("Requested")) { list("androidPermissions", Self.androidPermissions) }
                Section {
                    text("Keystore path", "androidKeystorePath")
                    text("Key alias", "androidKeyAlias")
                }
            default:
                Section {
                    text("App name", "appName")
                    text("Bundle / package ID", "bundleId")
                    text("Version name", "versionName")
                    number("Version code", "versionCode")
                    text("Icon source (1024x1024 PNG)", "iconSource")
                }
                if let descriptor = model.studio.descriptorPath {
                    Section {
                        Button(tr("Open {0}", descriptor.fileName)) {
                            model.sheet = nil
                            _ = model.open(path: descriptor)
                        }
                    }
                }
            }
            Section {
                if !error.isEmpty { Text(error).foregroundColor(Palette.color("Error")) }
                Button(tr("Save")) {
                    error = model.studio.saveProjectSettings(values)
                    if error.isEmpty { model.sheet = nil }
                }
                .disabled(model.studio.projectRoot.isEmpty)
            }
        }
        .navigationTitle(tr("Project Settings"))
        .onAppear { values = model.studio.projectSettings() }
    }

    private func stringBinding(_ key: String) -> Binding<String> {
        Binding(get: { values[key] as? String ?? "" }, set: { values[key] = $0 })
    }

    private func text(_ label: String, _ key: String) -> some View {
        HStack {
            Text(tr(label))
            Spacer()
            TextField("", text: stringBinding(key))
                .multilineTextAlignment(.trailing)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
        }
    }

    private func number(_ label: String, _ key: String) -> some View {
        HStack {
            Text(tr(label))
            Spacer()
            TextField("", value: Binding(get: { (values[key] as? NSNumber)?.intValue ?? 0 },
                                         set: { values[key] = NSNumber(value: $0) }), format: .number)
                .multilineTextAlignment(.trailing)
                .keyboardType(.numberPad)
        }
    }

    private func list(_ key: String, _ names: [String]) -> some View {
        ForEach(names, id: \.self) { name in
            Toggle(name, isOn: Binding(
                get: { (values[key] as? [String] ?? []).contains(name) },
                set: { on in
                    var current = values[key] as? [String] ?? []
                    current.removeAll { $0 == name }
                    if on { current.append(name) }
                    values[key] = current
                }))
        }
    }
}

struct SnippetSheet: View {
    @ObservedObject var model: StudioModel
    @State private var snippets: [DBCSnippet] = []
    @State private var filter = ""
    @State private var selected: Int?

    private var shown: [DBCSnippet] {
        filter.isEmpty ? snippets : snippets.filter { $0.name.localizedCaseInsensitiveContains(filter) }
    }

    var body: some View {
        VStack(spacing: 0) {
            TextField(tr("Filter"), text: $filter)
                .textFieldStyle(.roundedBorder)
                .autocorrectionDisabled()
                .padding(10)
            List {
                Section {
                    ForEach(shown.filter { !$0.user }, id: \.index) { row($0) }
                }
                Section(tr("My Snippets")) {
                    let mine = shown.filter(\.user)
                    if mine.isEmpty {
                        Text(tr("Edit > Save Selection as Snippet")).foregroundColor(Palette.color("TextDim"))
                    }
                    ForEach(mine, id: \.index) { row($0) }
                }
            }
            .listStyle(.plain)
            if let snippet = snippets.first(where: { $0.index == selected }) {
                Divider()
                ScrollView {
                    Text(snippet.body)
                        .font(.system(size: 12, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(8)
                }
                .frame(height: 160)
                Button(tr("Insert")) { insert(snippet) }
                    .buttonStyle(.borderedProminent)
                    .padding(8)
            }
            Text(tr("My Snippets: {0}", model.studio.snippetFolder))
                .font(.caption2)
                .foregroundColor(Palette.color("TextDim"))
                .padding(.bottom, 6)
        }
        .navigationTitle(tr("Insert Snippet"))
        .onAppear {
            snippets = model.studio.snippets()
            selected = snippets.first?.index
        }
    }

    private func row(_ snippet: DBCSnippet) -> some View {
        Button {
            selected = snippet.index
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(snippet.name)
                if !snippet.summary.isEmpty {
                    Text(snippet.summary).font(.caption).foregroundColor(Palette.color("TextDim"))
                }
            }
        }
        .listRowBackground(snippet.index == selected ? Palette.color("Selection") : Color.clear)
    }

    private func insert(_ snippet: DBCSnippet) {
        guard let document = model.current, let textView = document.textView else { return }
        let plan = model.studio.planSnippet(snippet.index, inText: document.text)
        if !plan.error.isEmpty {
            model.status = plan.error
            model.append(plan.error, severity: .notice)
        } else {
            textView.insertSnippet(plan)
            if plan.place != 0 {
                model.status = tr("Added {0} to the Snippets region at the end of the file", snippet.name)
            }
        }
        model.sheet = nil
    }
}

struct SaveSnippetSheet: View {
    @ObservedObject var model: StudioModel
    @State private var name = ""
    @State private var summary = ""
    @State private var both = false
    @State private var error = ""

    var body: some View {
        Form {
            Section {
                TextField(tr("Name"), text: $name).autocorrectionDisabled()
                TextField(tr("Description"), text: $summary)
                Toggle(tr(model.studio.profile == .dbPro ? "Also offer it to DBC projects"
                                                         : "Also offer it to DBPro projects"), isOn: $both)
            }
            Section {
                if !error.isEmpty { Text(error).foregroundColor(Palette.color("Error")) }
                Button(tr("Save")) {
                    error = model.studio.saveSnippetNamed(name, summary: summary,
                                                          body: model.current?.textView?.selectedText ?? "",
                                                          bothKinds: both)
                    if error.isEmpty { model.sheet = nil }
                }
                .disabled(name.isEmpty)
            } footer: {
                Text(tr("Saved in {0}", model.studio.snippetFolder))
            }
        }
        .navigationTitle(tr("Save as Snippet"))
    }
}

struct TextureLibrarySheet: View {
    @ObservedObject var model: StudioModel
    @State private var textures: [DBCLibraryTexture] = []

    var body: some View {
        let hasProject = !model.studio.projectRoot.isEmpty
        VStack(spacing: 0) {
            Text(tr(!hasProject ? "Open a project to add textures to it."
                    : textures.isEmpty ? "No texture library is installed."
                    : "Click a texture to copy it into the project's textures folder."))
                .font(.footnote)
                .foregroundColor(Palette.color("TextDim"))
                .padding(8)
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 100), spacing: 10)], spacing: 10) {
                    ForEach(textures, id: \.index) { texture in
                        Button {
                            model.studio.addLibraryTexture(texture.index)
                            textures = model.studio.libraryTextures()
                        } label: {
                            VStack(spacing: 4) {
                                ZStack(alignment: .topTrailing) {
                                    if let image = ThumbnailCache.shared.image(at: texture.previewPath, size: 96) {
                                        Image(uiImage: image).resizable().scaledToFill()
                                            .frame(width: 96, height: 96).clipped().cornerRadius(6)
                                    } else {
                                        RoundedRectangle(cornerRadius: 6).fill(Palette.color("PanelAlt"))
                                            .frame(width: 96, height: 96)
                                    }
                                    if texture.inProject {
                                        Image(systemName: "checkmark.circle.fill")
                                            .foregroundColor(Palette.color("Run"))
                                            .padding(4)
                                    }
                                }
                                Text(texture.name).font(.caption2).lineLimit(1)
                            }
                        }
                        .buttonStyle(.plain)
                        .disabled(!hasProject)
                        .help(tr(texture.cubeMap ? "A cube map: six faces for SET CUBE MAPPING ON"
                                 : texture.normalMap ? "A tileable texture, with its normal map"
                                 : "A tileable texture"))
                    }
                }
                .padding(10)
            }
        }
        .navigationTitle(tr("Texture Library"))
        .onAppear { textures = model.studio.libraryTextures() }
    }
}

struct AboutSheet: View {
    @ObservedObject var model: StudioModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Text("OpenDBC " + model.studio.versionString).font(.title3.weight(.semibold))
                Text(tr("OpenDBC is an independent open-source implementation compatible with the DarkBASIC Classic and DarkBASIC Professional programming languages. OpenDBC is not affiliated with, endorsed by, or sponsored by Dark Basic Software Limited."))
                Divider()
                Text(tr("Trademarks")).font(.headline)
                Text(tr("DarkBASIC and DarkBASIC Professional are names and trademarks belonging to their respective rights holders. They are referenced solely to describe OpenDBC's language compatibility. OpenDBC is an independent project and is not affiliated with or endorsed by Dark Basic Software Limited."))
                Divider()
                Text(tr("Licence")).font(.headline)
                Text(tr("OpenDBC is released under the MIT Licence, Copyright (c) 2026 Yannick Comte. Third-party content and its licences are listed in NOTICE.md."))
            }
            .padding(20)
        }
        .navigationTitle(tr("About OpenDBC"))
    }
}

struct NewLevelSheet: View {
    @ObservedObject var model: StudioModel
    @State private var name = ""
    @FocusState private var focused: Bool

    var body: some View {
        Form {
            Section {
                TextField(tr("Name"), text: $name)
                    .autocorrectionDisabled()
                    .focused($focused)
                    .onSubmit { create() }
            } footer: {
                Text(tr("Saved as {0}", "levels/\(name)/\(name).dba"))
            }
            Section {
                Button(tr("Create")) { create() }.disabled(name.isEmpty)
            }
        }
        .navigationTitle(tr("New Level"))
        .onAppear { focused = true }
    }

    private func create() {
        guard !name.isEmpty else { return }
        model.studio.newLevel(name)
        model.sheet = nil
    }
}

struct SaveAsSheet: View {
    @ObservedObject var model: StudioModel
    @State private var name = ""
    @State private var error = ""

    private var folder: String {
        if let path = model.current?.path, !path.isEmpty { return path.parentPath }
        let root = model.studio.projectRoot
        return root.isEmpty ? model.studio.importRoot : root
    }

    var body: some View {
        Form {
            Section {
                TextField(tr("Name"), text: $name)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
            } footer: {
                Text(tr("Location: {0}", folder))
            }
            Section {
                if !error.isEmpty { Text(error).foregroundColor(Palette.color("Error")) }
                Button(tr("Save")) { save() }.disabled(name.isEmpty)
            }
        }
        .navigationTitle(tr("Save As..."))
        .onAppear { name = model.current?.title ?? "" }
    }

    private func save() {
        guard let document = model.current else { return }
        let path = (folder as NSString).appendingPathComponent(name)
        // A tab keeps its kind: BASIC stays BASIC, an effect an effect.
        guard DBCStudio.kind(ofFile: path) == document.kind else {
            error = tr("{0} cannot hold this document", name)
            return
        }
        if FileManager.default.fileExists(atPath: path), path != document.path {
            error = tr("{0} already exists", name)
            return
        }
        if model.write(document, to: path) {
            model.sheet = nil
        }
    }
}

// MARK: Reference

struct ReferenceEntry: Decodable, Identifiable {
    let s: String
    let u: String
    let c: String?
    let k: String?
    let p: String?
    var id: String { u + s }
}

final class ReferenceBrowser: ObservableObject {
    let webView = WKWebView()
    @Published var entries: [ReferenceEntry] = []
    private var folder: URL?

    func open(index path: String) {
        let index = URL(fileURLWithPath: path)
        let root = index.deletingLastPathComponent()
        folder = root
        webView.loadFileURL(index, allowingReadAccessTo: root)
        if let data = try? Data(contentsOf: root.appendingPathComponent("search.json")),
           let decoded = try? JSONDecoder().decode([ReferenceEntry].self, from: data) {
            entries = decoded
        }
    }

    func show(_ entry: ReferenceEntry) {
        guard let folder, let url = URL(string: entry.u, relativeTo: folder) else { return }
        webView.loadFileURL(url, allowingReadAccessTo: folder)
    }

    func home() {
        guard let folder else { return }
        webView.loadFileURL(folder.appendingPathComponent("index.html"), allowingReadAccessTo: folder)
    }
}

struct WebViewHost: UIViewRepresentable {
    let webView: WKWebView

    func makeUIView(context: Context) -> WKWebView {
        webView
    }

    func updateUIView(_ view: WKWebView, context: Context) {}
}

struct ReferenceSheet: View {
    @ObservedObject var model: StudioModel
    @StateObject private var browser = ReferenceBrowser()
    @State private var query = ""
    @State private var failed = false

    private var results: [ReferenceEntry] {
        guard !query.isEmpty else { return [] }
        return Array(browser.entries.filter { $0.s.localizedCaseInsensitiveContains(query) }.prefix(200))
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                Button(tr("Back")) { browser.webView.goBack() }
                Button(tr("Reference index")) { browser.home() }
                TextField(tr("Search commands and examples"), text: $query)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
            }
            .padding(8)
            if failed {
                Text(tr("Could not load the offline DBA reference.")).padding()
                Spacer()
            } else if !query.isEmpty {
                if results.isEmpty {
                    Text(tr("No results")).foregroundColor(Palette.color("TextDim")).padding()
                    Spacer()
                } else {
                    List(results) { entry in
                        Button {
                            browser.show(entry)
                            query = ""
                        } label: {
                            VStack(alignment: .leading) {
                                Text(entry.s)
                                Text([entry.c, entry.k, entry.p].compactMap { $0 }.joined(separator: " — "))
                                    .font(.caption)
                                    .foregroundColor(Palette.color("TextDim"))
                            }
                        }
                    }
                    .listStyle(.plain)
                }
            } else {
                WebViewHost(webView: browser.webView)
            }
        }
        .navigationTitle(tr("DBA Reference"))
        .onAppear {
            if let index = model.studio.referenceIndexPath() {
                browser.open(index: index)
            } else {
                failed = true
            }
        }
    }
}

// MARK: Preview

/// An image or a sound of the project.
struct AssetPreviewView: View {
    let path: String
    let close: () -> Void
    @State private var player: AVAudioPlayer?
    @State private var playing = false
    @State private var error = ""

    var body: some View {
        NavigationStack {
            VStack(spacing: 14) {
                if DBCStudio.kind(ofFile: path) == .image {
                    if let image = UIImage(contentsOfFile: path) {
                        Image(uiImage: image)
                            .resizable()
                            .scaledToFit()
                            .frame(maxWidth: image.size.width, maxHeight: image.size.height)
                        Text("\(Int(image.size.width * image.scale)) x \(Int(image.size.height * image.scale))")
                            .foregroundColor(Palette.color("TextDim"))
                    } else {
                        Text(tr("Cannot preview {0}", path.fileName)).foregroundColor(Palette.color("Error"))
                    }
                } else {
                    Image(systemName: "speaker.wave.2").font(.system(size: 60))
                    if !error.isEmpty {
                        Text(error).foregroundColor(Palette.color("Error")).multilineTextAlignment(.center)
                    } else {
                        TimelineView(.periodic(from: .now, by: 0.25)) { _ in
                            VStack(spacing: 8) {
                                ProgressView(value: player?.currentTime ?? 0, total: max(player?.duration ?? 1, 0.01))
                                    .frame(maxWidth: 320)
                                Text("\(time(player?.currentTime ?? 0)) / \(time(player?.duration ?? 0))")
                                    .font(.subheadline.monospacedDigit())
                            }
                        }
                        HStack(spacing: 20) {
                            Button(tr(playing ? "Pause" : "Play")) {
                                if playing { player?.pause() } else { player?.play() }
                                playing.toggle()
                            }
                            Button(tr("Stop")) {
                                player?.stop()
                                player?.currentTime = 0
                                playing = false
                            }
                        }
                        .buttonStyle(.bordered)
                    }
                }
            }
            .padding()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Palette.color("Background"))
            .navigationTitle(path.fileName)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button(tr("Close"), action: close) }
            }
        }
        .foregroundColor(Palette.color("Text"))
        .onAppear {
            guard DBCStudio.kind(ofFile: path) == .audio else { return }
            do {
                player = try AVAudioPlayer(contentsOf: URL(fileURLWithPath: path))
                player?.prepareToPlay()
            } catch {
                self.error = tr("Cannot load audio: {0}", error.localizedDescription)
            }
        }
        .onDisappear { player?.stop() }
    }

    private func time(_ seconds: TimeInterval) -> String {
        String(format: "%02d:%02d", Int(seconds) / 60, Int(seconds) % 60)
    }
}
