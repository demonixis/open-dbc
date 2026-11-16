import SwiftUI

/// The left side of the Code mode: the project's files, the outline of the tab, the media.
struct SidebarView: View {
    @ObservedObject var model: StudioModel
    /// Called once a file was opened, where the panel covers the editor.
    let opened: () -> Void
    @State private var tab = 0

    var body: some View {
        VStack(spacing: 0) {
            Picker("", selection: $tab) {
                Text(tr("Project")).tag(0)
                Text(tr("Code")).tag(1)
                Text(tr("Media")).tag(2)
            }
            .pickerStyle(.segmented)
            .padding(6)
            switch tab {
            case 1: OutlineView(model: model)
            case 2: MediaView(model: model, opened: opened)
            default: ProjectTreeView(model: model, opened: opened)
            }
        }
        .background(Palette.color("Panel"))
    }
}

struct ProjectTreeView: View {
    @ObservedObject var model: StudioModel
    let opened: () -> Void

    var body: some View {
        let root = model.studio.projectRoot
        if root.isEmpty {
            VStack(spacing: 12) {
                Text(tr("No folder opened.")).foregroundColor(Palette.color("TextDim"))
                Button(tr("Projects...")) { model.sheet = .openLocation }
                Button(tr("New Project...")) { model.sheet = .newProject }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            VStack(spacing: 0) {
                PanelHeader(title: root.fileName) {
                    Button { model.projectRevision += 1 } label: { Image(systemName: "arrow.clockwise") }
                        .accessibilityLabel(tr("Refresh"))
                }
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 0) {
                        if model.studio.descriptorPath != nil {
                            sources
                        }
                        FolderContents(model: model, folder: root, depth: 0, opened: opened)
                    }
                    .padding(.vertical, 4)
                }
                .id(model.projectRevision)
            }
        }
    }

    @ViewBuilder private var sources: some View {
        let ordered = model.studio.orderedSources
        TreeHeading(title: tr("Sources"))
        ForEach(Array(ordered.enumerated()), id: \.offset) { index, path in
            FileRow(name: (index == 0 ? tr("Main: {0}", path.fileName) : "\(index): \(path.fileName)"),
                    icon: "doc.text", depth: 1) {
                model.studio.openFile(path)
                opened()
            }
        }
        // Worth a line only when the media are somewhere else than the project.
        if (model.studio.assetRoot as NSString).standardizingPath != (model.studio.projectRoot as NSString).standardizingPath {
            TreeHeading(title: tr("Media"))
            Text(model.studio.assetRoot)
                .font(.caption)
                .foregroundColor(Palette.color("TextDim"))
                .padding(.leading, 30)
        }
        TreeHeading(title: tr("Other files"))
    }
}

struct TreeHeading: View {
    let title: String

    var body: some View {
        Text(title)
            .font(.caption.weight(.semibold))
            .foregroundColor(Palette.color("TextDim"))
            .padding(.horizontal, 10)
            .padding(.top, 8)
            .padding(.bottom, 2)
    }
}

struct FileRow: View {
    let name: String
    let icon: String
    let depth: Int
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Image(systemName: icon).frame(width: 20)
                Text(name).lineLimit(1)
                Spacer(minLength: 0)
            }
            .padding(.leading, CGFloat(depth) * 16 + 10)
            .frame(minHeight: 34)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// The entries of a folder, read when the folder is first shown.
struct FolderContents: View {
    @ObservedObject var model: StudioModel
    let folder: String
    let depth: Int
    let opened: () -> Void
    @State private var expanded: Set<String> = []

    var body: some View {
        // Read as the folder is drawn: listing one folder is cheap, and an empty one draws nothing
        // that could ask for it later.
        ForEach(model.studio.entries(ofFolder: folder), id: \.path) { entry in
            if entry.directory {
                FileRow(name: entry.name, icon: expanded.contains(entry.path) ? "folder.fill" : "folder",
                        depth: depth) {
                    if expanded.contains(entry.path) {
                        expanded.remove(entry.path)
                    } else {
                        expanded.insert(entry.path)
                    }
                }
                .foregroundColor(Palette.color("Folder"))
                if expanded.contains(entry.path) {
                    FolderContents(model: model, folder: entry.path, depth: depth + 1, opened: opened)
                }
            } else {
                FileRow(name: entry.name, icon: icon(entry.kind), depth: depth) {
                    open(entry)
                }
                .contextMenu {
                    Button(tr("Open")) { open(entry) }
                }
            }
        }
    }

    private func open(_ entry: DBCFileEntry) {
        if entry.kind == .none {
            model.status = tr("No preview for {0}", entry.name)
            return
        }
        model.studio.openFile(entry.path)
        opened()
    }

    private func icon(_ kind: DBCFileKind) -> String {
        switch kind {
        case .basic: return "doc.text"
        case .effect: return "wand.and.stars"
        case .projectData: return "gearshape"
        case .image: return "photo"
        case .audio: return "speaker.wave.2"
        default: return "doc"
        }
    }
}

/// What the current tab declares: functions, labels, types and variables.
struct OutlineView: View {
    @ObservedObject var model: StudioModel
    @State private var sortByName = false

    var body: some View {
        if let document = model.current, document.isBasic {
            OutlineList(model: model, document: document, sortByName: $sortByName)
        } else {
            Text(tr("No source open."))
                .foregroundColor(Palette.color("TextDim"))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

struct OutlineList: View {
    @ObservedObject var model: StudioModel
    @ObservedObject var document: EditorDocument
    @Binding var sortByName: Bool
    @State private var outline: DBCOutline?

    var body: some View {
        VStack(spacing: 0) {
            Toggle(tr("Sort by name"), isOn: $sortByName)
                .font(.footnote)
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
            List {
                section(tr("Functions"), outline?.functions ?? [], detail: "{0} call(s)")
                section(tr("Labels"), outline?.labels ?? [], detail: "{0} GOTO/GOSUB jump(s)")
                section(tr("Types"), outline?.types ?? [], detail: "{0} field(s)")
                section(tr("Variables"), outline?.variables ?? [], detail: nil)
            }
            .listStyle(.plain)
        }
        // Read again a moment after the last edit, at once on another tab.
        .task(id: "\(document.id)-\(document.revision)") {
            if outline != nil {
                try? await Task.sleep(nanoseconds: 300_000_000)
            }
            if !Task.isCancelled {
                outline = model.studio.outline(ofText: document.text, path: document.path)
            }
        }
    }

    @ViewBuilder
    private func section(_ title: String, _ entries: [DBCOutlineEntry], detail: String?) -> some View {
        if !entries.isEmpty {
            Section(title) {
                let sorted = sortByName
                    ? entries.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
                    : entries
                ForEach(Array(sorted.enumerated()), id: \.offset) { _, entry in
                    Button {
                        model.goToLine(entry.line)
                    } label: {
                        HStack {
                            Text(entry.name).lineLimit(1)
                            Spacer()
                            if detail != nil { Text("\(entry.count)").foregroundColor(Palette.color("TextDim")) }
                            Text("\(entry.line)").foregroundColor(Palette.color("LineNumber")).frame(width: 44, alignment: .trailing)
                        }
                        .font(.subheadline)
                    }
                    .help(detail.map { tr($0, "\(entry.count)") } ?? "")
                    .listRowBackground(Palette.color("Panel"))
                }
            }
        }
    }
}

/// The media files of the project, by kind.
struct MediaView: View {
    @ObservedObject var model: StudioModel
    let opened: () -> Void
    @State private var files: [DBCMediaFile] = []
    @State private var truncated = false
    @State private var kind = -1
    @State private var filter = ""

    private var shown: [DBCMediaFile] {
        files.filter { file in
            (kind < 0 || file.kind == kind)
                && (filter.isEmpty || file.relativeName.localizedCaseInsensitiveContains(filter))
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Picker("", selection: $kind) {
                    Text(tr("All ({0})", "\(files.count)")).tag(-1)
                    ForEach(0..<6, id: \.self) { value in
                        let count = files.filter { $0.kind == value }.count
                        Text("\(DBCStudio.mediaKindLabel(value)) (\(count))").tag(value)
                    }
                }
                .pickerStyle(.menu)
                Spacer()
                Button { reload() } label: { Image(systemName: "arrow.clockwise") }
                    .accessibilityLabel(tr("Refresh"))
            }
            .padding(.horizontal, 10)
            TextField(tr("Filter by name"), text: $filter)
                .textFieldStyle(.roundedBorder)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
                .padding(.horizontal, 10)
                .padding(.bottom, 4)
            if truncated {
                Text(tr("Showing the first {0} files.", "\(files.count)"))
                    .font(.caption)
                    .foregroundColor(Palette.color("TextDim"))
            }
            List(shown, id: \.path) { file in
                Button {
                    if DBCStudio.kind(ofFile: file.path) == .none {
                        model.status = tr("No preview for {0}", file.path.fileName)
                    } else {
                        model.studio.openFile(file.path)
                        opened()
                    }
                } label: {
                    HStack {
                        Text(file.relativeName).lineLimit(1)
                        Spacer()
                        Text(file.sizeText).foregroundColor(Palette.color("TextDim"))
                    }
                    .font(.subheadline)
                }
                .help(file.path)
                .listRowBackground(Palette.color("Panel"))
            }
            .listStyle(.plain)
        }
        .task(id: model.projectRevision) { reload() }
    }

    private func reload() {
        var cut: ObjCBool = false
        files = model.studio.mediaFilesTruncated(&cut)
        truncated = cut.boolValue
    }
}
