import SwiftUI
import UIKit
import UniformTypeIdentifiers

/// The Scene mode: Hierarchy, Viewport, Inspector, Level Settings, Assets and Output.
struct SceneModeView: View {
    @ObservedObject var model: StudioModel
    let compact: Bool
    @State private var compactPanel = 1
    @AppStorage("scene.sidebarWidth") private var sidebarWidth = 240.0
    @AppStorage("scene.inspectorWidth") private var inspectorWidth = 330.0
    @AppStorage("scene.bottomHeight") private var bottomHeight = 240.0

    var body: some View {
        if compact {
            VStack(spacing: 0) {
                Picker("", selection: $compactPanel) {
                    Text(tr("Hierarchy")).tag(0)
                    Text(tr("Viewport")).tag(1)
                    Text(tr("Inspector")).tag(2)
                    Text(tr("Assets")).tag(3)
                }
                .pickerStyle(.segmented)
                .padding(6)
                switch compactPanel {
                case 0: HierarchyView(model: model)
                case 2: InspectorPanel(model: model)
                case 3: AssetsView(model: model)
                default: ViewportPanel(model: model)
                }
            }
        } else {
            HStack(spacing: 0) {
                if model.showSidebar {
                    HierarchyView(model: model).frame(width: sidebarWidth)
                    ResizeHandle(size: $sidebarWidth, range: 160...520, vertical: false, grows: 1)
                }
                VStack(spacing: 0) {
                    ViewportPanel(model: model)
                    if model.showBottomPanel {
                        ResizeHandle(size: $bottomHeight, range: 120...700, vertical: true, grows: -1)
                        VStack(spacing: 0) {
                            PanelHeader(title: "") {
                                Picker("", selection: $model.sceneBottomTab) {
                                    Text(tr("Assets")).tag(0)
                                    Text(tr("Output")).tag(1)
                                }
                                .pickerStyle(.segmented)
                                .frame(maxWidth: 260)
                                Spacer()
                                Button { model.showBottomPanel = false } label: { Image(systemName: "xmark") }
                            }
                            if model.sceneBottomTab == 0 {
                                AssetsView(model: model)
                            } else {
                                OutputView(model: model)
                            }
                        }
                        .frame(height: bottomHeight)
                    }
                }
                if model.showInspector {
                    ResizeHandle(size: $inspectorWidth, range: 260...620, vertical: false, grows: -1)
                    InspectorPanel(model: model).frame(width: inspectorWidth)
                }
            }
            .onAppear { model.showBottomPanel = true }
        }
    }
}

// MARK: Hierarchy

struct HierarchyView: View {
    @ObservedObject var model: StudioModel
    /// A tap adds to the selection instead of replacing it: what Ctrl or Cmd does with a mouse.
    @State private var multiSelect = false
    @State private var expanded: Set<String> = []

    var body: some View {
        let studio = model.studio
        let _ = model.sceneRevision
        VStack(spacing: 0) {
            PanelHeader(title: studio.levelName + (studio.sceneDirty ? " *" : "")) {
                Toggle(isOn: $multiSelect) { Image(systemName: "checklist") }
                    .toggleStyle(.button)
                    .help(tr("Select several nodes"))
                Menu {
                    CreateMenu(model: model)
                } label: {
                    Image(systemName: "plus")
                }
                .disabled(!studio.sceneEditable)
            }
            let errors = studio.sceneReadErrors
            if !errors.isEmpty {
                VStack(alignment: .leading, spacing: 2) {
                    Text(tr("Read-only: {0} has lines the Scene view did not write.", studio.levelName))
                    ForEach(errors, id: \.self) { Text("• " + $0) }
                }
                .font(.caption)
                .foregroundColor(Palette.color("Error"))
                .padding(8)
            }
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    ForEach(studio.sceneRows(), id: \.key) { row in
                        if let children = row.children {
                            HierarchyRow(model: model, row: row, depth: 0, multiSelect: multiSelect,
                                         disclosure: expanded.contains(row.key)) {
                                if expanded.contains(row.key) {
                                    expanded.remove(row.key)
                                } else {
                                    expanded.insert(row.key)
                                }
                            }
                            if expanded.contains(row.key) {
                                ForEach(children, id: \.key) { child in
                                    HierarchyRow(model: model, row: child, depth: 1, multiSelect: multiSelect)
                                }
                            }
                        } else {
                            if row.kind == -1 { Divider() }
                            HierarchyRow(model: model, row: row, depth: 0, multiSelect: multiSelect)
                        }
                    }
                }
            }
            .contentShape(Rectangle())
            .onTapGesture { studio.clearSceneSelection() }
        }
        .background(Palette.color("Panel"))
    }
}

struct HierarchyRow: View {
    @ObservedObject var model: StudioModel
    let row: DBCSceneRow
    let depth: Int
    let multiSelect: Bool
    var disclosure: Bool?
    var toggleDisclosure: (() -> Void)?

    var body: some View {
        let studio = model.studio
        HStack(spacing: 6) {
            if let disclosure {
                Button { toggleDisclosure?() } label: {
                    Image(systemName: disclosure ? "chevron.down" : "chevron.right").frame(width: 22, height: 30)
                }
                .buttonStyle(.plain)
            }
            Image(systemName: icon).frame(width: 20).foregroundColor(Palette.color("TextDim"))
            Text(row.name).lineLimit(1)
            Spacer(minLength: 0)
        }
        .padding(.leading, CGFloat(depth) * 18 + 10)
        .frame(minHeight: 34)
        .foregroundColor(row.hidden ? Palette.color("TextDim") : Palette.color("Text"))
        .background(row.selected ? Palette.color("Selection") : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture(count: 2) {
            studio.selectSceneRow(row.key, toggle: false, range: false)
            studio.frameSceneSelection()
        }
        .onTapGesture { studio.selectSceneRow(row.key, toggle: multiSelect, range: false) }
        .help(row.tooltip)
        .contextMenu {
            Button(tr("Frame Selection")) {
                studio.selectSceneRow(row.key, toggle: false, range: false)
                studio.frameSceneSelection()
            }
            Button(tr("Select up to here")) { studio.selectSceneRow(row.key, toggle: false, range: true) }
            if row.kind != -1 {
                Button(tr("Duplicate")) {
                    if !row.selected { studio.selectSceneRow(row.key, toggle: false, range: false) }
                    studio.sceneDuplicate()
                }
                Button(tr("Delete"), role: .destructive) {
                    if !row.selected { studio.selectSceneRow(row.key, toggle: false, range: false) }
                    studio.sceneDelete()
                }
            }
        }
    }

    private var icon: String {
        switch row.kind {
        case -2: return "square.stack.3d.up"
        case -1: return "video"
        case NodeKind.model: return "cube.transparent"
        case NodeKind.matrix: return "square.grid.3x3"
        case NodeKind.terrain: return "mountain.2"
        case NodeKind.light: return "lightbulb"
        case NodeKind.sound: return "speaker.wave.2"
        default: return NodeKind.primitives.first { $0.0 == row.kind }?.2 ?? "cube"
        }
    }
}

// MARK: Inspector and Level Settings

struct InspectorPanel: View {
    @ObservedObject var model: StudioModel
    @State private var tab = 0

    var body: some View {
        VStack(spacing: 0) {
            Picker("", selection: $tab) {
                Text(tr("Inspector")).tag(0)
                Text(tr("Level Settings")).tag(1)
            }
            .pickerStyle(.segmented)
            .padding(6)
            FormView(model: model, form: tab == 0 ? .inspector : .levelSettings)
                .id(tab)
        }
        .background(Palette.color("Panel"))
    }
}

/// A form of the level editor: one row per value, its label on the left.
struct FormView: View {
    @ObservedObject var model: StudioModel
    let form: DBCForm
    var scrolls = true

    var body: some View {
        let _ = model.sceneRevision
        let fields = model.studio.sceneForm(form)
        let rows = LazyVStack(alignment: .leading, spacing: 0) {
            ForEach(fields, id: \.identifier) { field in
                FieldRow(model: model, form: form, field: field)
            }
        }
        if scrolls {
            ScrollView { rows.padding(.bottom, 20) }
        } else {
            rows
        }
    }
}

struct FieldRow: View {
    @ObservedObject var model: StudioModel
    let form: DBCForm
    let field: DBCField

    var body: some View {
        switch field.kind {
        case .heading:
            Text(field.label)
                .font(.subheadline.weight(.semibold))
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 10)
                .padding(.top, 14)
                .padding(.bottom, 4)
        case .hint:
            Text(field.label)
                .font(.caption)
                .foregroundColor(Palette.color("TextDim"))
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 10)
                .padding(.vertical, 3)
        case .warning:
            Text(field.label)
                .font(.caption)
                .foregroundColor(Palette.color("Error"))
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 10)
                .padding(.vertical, 3)
        case .button:
            Button(field.label) { model.studio.sceneForm(form, press: field.identifier) }
                .buttonStyle(.bordered)
                .disabled(!field.enabled)
                .help(field.tooltip)
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
        default:
            HStack(spacing: 8) {
                if field.optional {
                    // Off leaves the value to the engine: the level writes no statement for it.
                    Toggle("", isOn: Binding(get: { field.isSet },
                                             set: { model.studio.sceneForm(form, setOptional: $0, field: field.identifier) }))
                        .labelsHidden()
                        .toggleStyle(CheckboxStyle())
                        .help(tr("Unchecked: the engine default, and no statement"))
                        .disabled(!field.enabled)
                        .opacity(field.enabled ? 1 : 0.5)
                }
                // The check mark stays live beside a value it switched off.
                Group {
                    Text(field.label)
                        .font(.subheadline)
                        .lineLimit(2)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    control
                }
                .disabled(!field.enabled || !field.isSet)
                .opacity(field.enabled && field.isSet ? 1 : 0.5)
            }
            .padding(.horizontal, 10)
            .frame(minHeight: 40)
            .help(field.tooltip)
        }
    }

    @ViewBuilder private var control: some View {
        let studio = model.studio
        switch field.kind {
        case .toggle:
            Toggle("", isOn: Binding(get: { field.flag },
                                     set: { studio.sceneForm(form, setFlag: $0, field: field.identifier) }))
                .labelsHidden()
        case .number:
            NumberControl(field: field) { value, commit in
                studio.sceneForm(form, setNumber: value, field: field.identifier, commit: commit)
            }
        case .text:
            TextControl(text: field.text) { value in
                studio.sceneForm(form, setText: value, field: field.identifier, commit: true)
            }
        case .color:
            ColorPicker("", selection: Binding(get: { Color(rgb: field.color) },
                                               set: { studio.sceneForm(form, setNumber: Double($0.rgb),
                                                                       field: field.identifier, commit: true) }),
                        supportsOpacity: false)
                .labelsHidden()
        case .choice:
            Picker("", selection: Binding(get: { field.choice },
                                          set: { studio.sceneForm(form, setNumber: Double($0),
                                                                  field: field.identifier, commit: true) })) {
                ForEach(Array(field.choices.enumerated()), id: \.offset) { index, name in
                    Text(name).tag(index)
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
        case .asset:
            AssetControl(model: model, field: field) { value in
                studio.sceneForm(form, setText: value, field: field.identifier, commit: true)
            }
        default:
            EmptyView()
        }
    }
}

/// A square check mark: a switch would read as the value itself.
struct CheckboxStyle: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        Button { configuration.isOn.toggle() } label: {
            Image(systemName: configuration.isOn ? "checkmark.square.fill" : "square")
                .font(.system(size: 20))
        }
        .buttonStyle(.plain)
    }
}

/// A number: typed, stepped, or slid when it has a short range.
struct NumberControl: View {
    let field: DBCField
    let apply: (Double, Bool) -> Void
    @State private var text = ""
    @State private var sliding: Double?
    @FocusState private var focused: Bool

    private func format(_ value: Double) -> String {
        if field.integer { return String(Int(value.rounded())) }
        let rounded = (value * 1000).rounded() / 1000
        return rounded == rounded.rounded() ? String(Int(rounded)) : String(rounded)
    }

    private func clamp(_ value: Double) -> Double {
        min(max(value, field.minimum), field.maximum)
    }

    var body: some View {
        HStack(spacing: 6) {
            if field.slider {
                Slider(value: Binding(get: { sliding ?? field.number },
                                      set: { value in
                                          sliding = value
                                          apply(field.integer ? value.rounded() : value, false)
                                      }),
                       in: field.minimum...field.maximum) { editing in
                    if !editing, let value = sliding {
                        sliding = nil
                        apply(field.integer ? value.rounded() : value, true)
                    }
                }
                .frame(width: 110)
                Text(format(sliding ?? field.number))
                    .font(.subheadline.monospacedDigit())
                    .frame(width: 38, alignment: .trailing)
            } else {
                TextField("", text: $text)
                    .keyboardType(.numbersAndPunctuation)
                    .multilineTextAlignment(.trailing)
                    .textFieldStyle(.roundedBorder)
                    .font(.subheadline.monospacedDigit())
                    .frame(width: 96)
                    .focused($focused)
                    .onSubmit { commitText() }
                Stepper("", onIncrement: { apply(clamp(field.number + field.step), true) },
                        onDecrement: { apply(clamp(field.number - field.step), true) })
                    .labelsHidden()
            }
        }
        .onAppear { text = format(field.number) }
        .onChange(of: field.number) { value in
            if !focused { text = format(value) }
        }
        .onChange(of: focused) { isFocused in
            if !isFocused { commitText() }
        }
    }

    private func commitText() {
        let normalized = text.replacingOccurrences(of: ",", with: ".")
        if let value = Double(normalized), value != field.number {
            apply(clamp(value), true)
        } else {
            text = format(field.number)
        }
    }
}

struct TextControl: View {
    let text: String
    let apply: (String) -> Void
    @State private var value = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $value)
            .textFieldStyle(.roundedBorder)
            .font(.subheadline)
            .autocorrectionDisabled()
            .frame(width: 170)
            .focused($focused)
            .onSubmit { commit() }
            .onAppear { value = text }
            .onChange(of: text) { newText in
                if !focused { value = newText }
            }
            .onChange(of: focused) { isFocused in
                if !isFocused { commit() }
            }
    }

    private func commit() {
        if value != text { apply(value) }
    }
}

/// A file of the project: picked from a list, dropped from the Assets panel, or cleared.
struct AssetControl: View {
    @ObservedObject var model: StudioModel
    let field: DBCField
    let apply: (String) -> Void

    var body: some View {
        HStack(spacing: 4) {
            Menu {
                ForEach(candidates, id: \.self) { name in
                    Button(name) { apply(name) }
                }
            } label: {
                Text(field.text.isEmpty ? tr("None") : field.text.fileName)
                    .font(.subheadline)
                    .lineLimit(1)
                    .truncationMode(.head)
                    .frame(width: 130, alignment: .trailing)
            }
            Button { apply("") } label: { Image(systemName: "xmark.circle") }
                .buttonStyle(.plain)
                .disabled(field.text.isEmpty)
                .accessibilityLabel(tr("Clear"))
        }
        .help(field.text)
        .onDrop(of: [UTType.plainText], isTargeted: nil) { providers in
            guard let provider = providers.first else { return false }
            _ = provider.loadObject(ofClass: NSString.self) { object, _ in
                guard let name = object as? String, !name.hasPrefix(AssetsView.primitivePrefix) else { return }
                DispatchQueue.main.async {
                    if field.assetKind < 0 || AssetsView.kind(of: name, in: model) == field.assetKind {
                        apply(name)
                    }
                }
            }
            return true
        }
    }

    private var candidates: [String] {
        var truncated: ObjCBool = false
        return model.studio.mediaFilesTruncated(&truncated)
            .filter { field.assetKind < 0 || $0.kind == field.assetKind }
            .prefix(200)
            .map(\.relativeName)
    }
}

// MARK: Viewport

struct ViewportHost: UIViewRepresentable {
    func makeUIView(context: Context) -> DBCViewportView {
        DBCViewportView(frame: .zero)
    }

    func updateUIView(_ view: DBCViewportView, context: Context) {}
}

struct ViewportPanel: View {
    @ObservedObject var model: StudioModel
    @State private var showSteps = false
    @State private var showPaint = false

    var body: some View {
        let studio = model.studio
        let _ = model.sceneRevision
        VStack(spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 10) {
                    Picker("", selection: Binding(get: { studio.sceneTool.rawValue },
                                                  set: { studio.sceneTool = DBCSceneTool(rawValue: $0) ?? .move })) {
                        Label(tr("View"), systemImage: "hand.draw").tag(0)
                        Label(tr("Move"), systemImage: "arrow.up.and.down.and.arrow.left.and.right").tag(1)
                        Label(tr("Rotate"), systemImage: "arrow.triangle.2.circlepath").tag(2)
                        Label(tr("Scale"), systemImage: "arrow.up.left.and.arrow.down.right").tag(3)
                        Label(tr("Paint"), systemImage: "paintbrush").tag(4)
                    }
                    .pickerStyle(.segmented)
                    .labelStyle(.iconOnly)
                    .frame(width: 230)
                    if studio.sceneTool == .paint {
                        Button { showPaint = true } label: { Label(tr("Paint"), systemImage: "slider.horizontal.3") }
                            .popover(isPresented: $showPaint) {
                                FormView(model: model, form: .paint)
                                    .frame(minWidth: 340, minHeight: 380)
                            }
                    }
                    toggle(tr("Local"), "cube.transparent", get: { studio.sceneLocalGizmo }, set: { studio.sceneLocalGizmo = $0 })
                    toggle(tr("Snap"), "square.grid.3x3", get: { studio.sceneSnap }, set: { studio.sceneSnap = $0 })
                    Button { showSteps = true } label: {
                        Text(String(format: "%g", studio.sceneMoveStep)).font(.subheadline.monospacedDigit())
                    }
                    .help(tr("Snap steps"))
                    .popover(isPresented: $showSteps) {
                        FormView(model: model, form: .snapSteps, scrolls: false)
                            .frame(minWidth: 320)
                            .padding(.vertical, 10)
                    }
                    toggle(tr("Grid"), "grid", get: { studio.sceneGrid }, set: { studio.sceneGrid = $0 })
                    toggle(tr("Level camera view"), "video", get: { studio.sceneCameraView },
                           set: { studio.sceneCameraView = $0 })
                        .disabled(!studio.sceneUsesLevelCamera)
                    Divider().frame(height: 22)
                    Button { studio.sceneUndo() } label: { Image(systemName: "arrow.uturn.backward") }
                        .disabled(!studio.sceneCanUndo)
                        .accessibilityLabel(tr("Undo"))
                    Button { studio.sceneRedo() } label: { Image(systemName: "arrow.uturn.forward") }
                        .disabled(!studio.sceneCanRedo)
                        .accessibilityLabel(tr("Redo"))
                    Button { studio.sceneDuplicate() } label: { Image(systemName: "plus.square.on.square") }
                        .disabled(!studio.sceneHasSelection || !studio.sceneEditable)
                        .accessibilityLabel(tr("Duplicate"))
                    Button(role: .destructive) { studio.sceneDelete() } label: { Image(systemName: "trash") }
                        .disabled(!studio.sceneHasSelection || !studio.sceneEditable)
                        .accessibilityLabel(tr("Delete"))
                    Button { studio.frameSceneSelection() } label: { Image(systemName: "scope") }
                        .disabled(!studio.sceneHasSelection)
                        .accessibilityLabel(tr("Frame Selection"))
                }
                .padding(.horizontal, 10)
            }
            .frame(height: 44)
            .background(Palette.color("Panel"))
            GeometryReader { geometry in
                ViewportHost()
                    .onDrop(of: [UTType.plainText], delegate: ViewportDrop(model: model))
                    .frame(width: geometry.size.width, height: geometry.size.height)
            }
        }
    }

    private func toggle(_ title: String, _ icon: String, get: @escaping () -> Bool,
                        set: @escaping (Bool) -> Void) -> some View {
        Toggle(isOn: Binding(get: get, set: { set($0); model.sceneRevision += 1 })) {
            Label(title, systemImage: icon)
        }
        .toggleStyle(.button)
        .labelStyle(.titleOnly)
        .font(.subheadline)
    }
}

/// A tile of the Assets panel let go over the viewport.
struct ViewportDrop: DropDelegate {
    let model: StudioModel

    func performDrop(info: DropInfo) -> Bool {
        guard let provider = info.itemProviders(for: [UTType.plainText]).first else { return false }
        let point = info.location
        _ = provider.loadObject(ofClass: NSString.self) { object, _ in
            guard let name = object as? String else { return }
            DispatchQueue.main.async {
                if name.hasPrefix(AssetsView.primitivePrefix) {
                    let kind = Int(name.dropFirst(AssetsView.primitivePrefix.count)) ?? 0
                    model.studio.sceneDropPrimitive(kind, at: point)
                } else {
                    model.studio.sceneDropAsset(name, at: point)
                }
            }
        }
        return true
    }
}

// MARK: Assets

/// Small pictures of the project's images, read once per file.
final class ThumbnailCache {
    static let shared = ThumbnailCache()
    private let cache = NSCache<NSString, UIImage>()

    func image(at path: String, size: CGFloat) -> UIImage? {
        if let cached = cache.object(forKey: path as NSString) { return cached }
        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceThumbnailMaxPixelSize: size * UIScreen.main.scale,
            kCGImageSourceCreateThumbnailWithTransform: true,
        ]
        guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else { return nil }
        let thumbnail = UIImage(cgImage: image)
        cache.setObject(thumbnail, forKey: path as NSString)
        return thumbnail
    }
}

struct AssetsView: View {
    @ObservedObject var model: StudioModel
    @State private var files: [DBCMediaFile] = []
    @State private var truncated = false
    @State private var kind = -1
    @State private var filter = ""
    static let primitivePrefix = "opendbc-primitive:"
    private static let tile: CGFloat = 76

    static func kind(of name: String, in model: StudioModel) -> Int {
        var truncated: ObjCBool = false
        return model.studio.mediaFilesTruncated(&truncated).first { $0.relativeName == name }?.kind ?? -1
    }

    private var shown: [DBCMediaFile] {
        files.filter { file in
            // Images, sounds and models: what a level places.
            file.kind <= 2 && (kind < 0 || file.kind == kind)
                && (filter.isEmpty || file.relativeName.localizedCaseInsensitiveContains(filter))
        }
    }

    var body: some View {
        let studio = model.studio
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                Button { reload() } label: { Image(systemName: "arrow.clockwise") }
                    .accessibilityLabel(tr("Refresh"))
                Picker("", selection: $kind) {
                    Text(tr("All")).tag(-1)
                    Text(tr("Images")).tag(0)
                    Text(tr("Models")).tag(2)
                    Text(tr("Sounds")).tag(1)
                }
                .pickerStyle(.menu)
                TextField(tr("Filter"), text: $filter)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                if truncated {
                    Text(tr("Only the first {0} files", "\(files.count)")).font(.caption)
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: Self.tile + 8), spacing: 8)], spacing: 8) {
                    if (kind < 0 || kind == 2) && filter.isEmpty {
                        ForEach(NodeKind.primitives, id: \.0) { value, name, icon in
                            AssetTile(title: tr(name), image: nil, icon: icon, highlighted: false)
                                .onTapGesture(count: 2) { studio.sceneCreate(value, light: 0) }
                                .onDrag { NSItemProvider(object: "\(Self.primitivePrefix)\(value)" as NSString) }
                                .help(tr("Drag onto the viewport or double-click to add."))
                        }
                    }
                    ForEach(shown, id: \.path) { file in
                        AssetTile(title: file.path.fileName,
                                  image: file.kind == 0 ? ThumbnailCache.shared.image(at: file.path, size: Self.tile) : nil,
                                  icon: file.kind == 2 ? "cube.transparent" : file.kind == 1 ? "speaker.wave.2" : "photo",
                                  highlighted: file.kind == 2 && studio.sceneInspectedModel == file.relativeName)
                            .onTapGesture(count: 2) { studio.sceneActivateAsset(file.relativeName) }
                            .onTapGesture {
                                // A model shows its details in the Inspector.
                                studio.sceneInspectedModel = file.kind == 2 ? file.relativeName : ""
                            }
                            .onDrag { NSItemProvider(object: file.relativeName as NSString) }
                            .help(file.relativeName)
                    }
                }
                .padding(10)
            }
        }
        .background(Palette.color("Background"))
        .task(id: model.projectRevision) { reload() }
    }

    private func reload() {
        var cut: ObjCBool = false
        files = model.studio.mediaFilesTruncated(&cut)
        truncated = cut.boolValue
    }
}

struct AssetTile: View {
    let title: String
    let image: UIImage?
    let icon: String
    let highlighted: Bool

    var body: some View {
        VStack(spacing: 3) {
            ZStack {
                RoundedRectangle(cornerRadius: 6).fill(Palette.color("PanelAlt"))
                if let image {
                    Image(uiImage: image).resizable().scaledToFit().padding(3)
                } else {
                    Image(systemName: icon).font(.system(size: 30)).foregroundColor(Palette.color("Keyword"))
                }
            }
            .frame(width: 76, height: 76)
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(highlighted ? Palette.color("Accent") : Color.clear, lineWidth: 2))
            Text(title)
                .font(.caption2)
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .foregroundColor(Palette.color("TextDim"))
                .frame(width: 84)
        }
        .contentShape(Rectangle())
    }
}
