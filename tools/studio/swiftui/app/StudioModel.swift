import SwiftUI
import UIKit

/// A tab of the code editor. The text view holds the text while the tab is on screen.
final class EditorDocument: ObservableObject, Identifiable {
    let id = UUID()
    @Published var path: String
    @Published var title: String
    @Published var dirty = false
    /// 1-based line -> message, from the last run or check.
    @Published var errors: [Int: String] = [:]
    var text: String
    var savedText: String
    let kind: DBCFileKind
    var windows1252 = false
    var bom = false
    var crlf = false
    /// Kept with the tab, and with it the undo history.
    var textView: CodeTextView?
    /// Bumped at every edit, for what follows the text: the outline.
    @Published var revision = 0
    /// A line to show once the text view exists.
    var pendingLine: Int?

    init(path: String, title: String, text: String, kind: DBCFileKind) {
        self.path = path
        self.title = title
        self.text = text
        self.savedText = text
        self.kind = kind
    }

    var isBasic: Bool { kind == .basic }

    func bridged() -> DBCOpenDocument {
        let document = DBCOpenDocument()
        document.path = path
        document.title = title
        document.text = text
        document.dirty = dirty
        document.basic = isBasic
        document.windows1252 = windows1252
        document.bom = bom
        document.crlf = crlf
        return document
    }
}

struct OutputLine: Identifiable {
    let id: Int
    let text: String
    let severity: DBCSeverity
}

enum StudioMode {
    case code
    case scene
}

enum StudioSheet: String, Identifiable {
    case newProject, resolution, recovery, editorSettings, projectSettings, snippets, saveSnippet
    case textureLibrary, about, reference, newLevel, saveAs, openLocation

    var id: String { rawValue }
}

/// What the interface shows, and the delegate the bridge reports to.
final class StudioModel: NSObject, ObservableObject, DBCStudioDelegate {
    static let shared = StudioModel()
    let studio = DBCStudio.shared

    @Published var documents: [EditorDocument] = []
    @Published var currentID: UUID?
    @Published var output: [OutputLine] = []
    @Published var status = ""
    @Published var problems: [DBCDiagnostic] = []
    @Published var running = false
    @Published var mode: StudioMode = .code
    @Published var sheet: StudioSheet?
    /// Bumped when the project's folder, language or files change.
    @Published var projectRevision = 0
    /// Bumped when the level, its selection or its tools change.
    @Published var sceneRevision = 0
    /// Bumped when the theme changes, so that colours are read again.
    @Published var themeRevision = 0
    @Published var previewPath: String?
    @Published var bottomPanel = 0
    /// The Scene mode's bottom panel: 0 the assets, 1 the output.
    @Published var sceneBottomTab = 0
    @Published var showBottomPanel = false
    @Published var showSidebar = true
    @Published var showInspector = true
    @Published var closing: EditorDocument?
    @Published var textZoom: Double = 1.0
    @Published var message: String?
    /// The system's file browser is up, to pick a project folder.
    @Published var pickingFolder = false

    private var nextOutputID = 0
    private var untitledCount = 0
    private var sceneChangePending = false
    private static let maxOutputLines = 5000

    var current: EditorDocument? {
        documents.first { $0.id == currentID }
    }

    func start() {
        studio.delegate = self
        textZoom = studio.textZoom
        status = tr("Ready")
        studio.restoreSession()
        if !studio.pendingRecoveries().isEmpty {
            sheet = .recovery
        }
    }

    // MARK: Documents

    func document(at path: String) -> EditorDocument? {
        let target = (path as NSString).standardizingPath
        return documents.first { !$0.path.isEmpty && ($0.path as NSString).standardizingPath == target }
    }

    func newFile() {
        untitledCount += 1
        let document = EditorDocument(path: "", title: "untitled-\(untitledCount).dba", text: "", kind: .basic)
        documents.append(document)
        currentID = document.id
        mode = .code
    }

    @discardableResult
    func open(path: String) -> Bool {
        if let existing = document(at: path) {
            currentID = existing.id
            return true
        }
        let kind = DBCStudio.kind(ofFile: path)
        guard kind == .basic || kind == .effect || kind == .projectData else { return false }
        guard let file = studio.readTextFile(path) else { return false }
        let document = EditorDocument(path: path, title: path.fileName, text: file.text, kind: kind)
        document.windows1252 = file.windows1252
        document.bom = file.bom
        document.crlf = file.crlf
        documents.append(document)
        currentID = document.id
        return true
    }

    func textChanged(_ document: EditorDocument, text: String) {
        document.text = text
        document.revision += 1
        let dirty = text != document.savedText
        if document.dirty != dirty {
            document.dirty = dirty
        }
    }

    /// Saves the tab; one without a file asks for a name first.
    func save(_ document: EditorDocument?) {
        guard let document else { return }
        if document.path.isEmpty {
            currentID = document.id
            sheet = .saveAs
            return
        }
        write(document, to: document.path)
    }

    @discardableResult
    func write(_ document: EditorDocument, to path: String) -> Bool {
        var notice: NSString?
        var error: NSString?
        guard let written = studio.write(document.bridged(), toPath: path, notice: &notice, error: &error) else {
            let text = (error as String?) ?? tr("Cannot save {0}", path)
            append(text, severity: .error)
            status = text
            return false
        }
        if let notice {
            append(notice as String, severity: .notice)
        }
        document.windows1252 = written.windows1252
        document.bom = written.bom
        document.crlf = written.crlf
        document.path = path
        document.title = path.fileName
        document.savedText = document.text
        document.dirty = false
        status = tr("Saved {0}", path.fileName)
        studio.noteLastFile(path)
        if document.id == currentID {
            studio.documentSaved()
        }
        projectRevision += 1
        return true
    }

    func saveAll() {
        for document in documents where document.dirty && !document.path.isEmpty {
            write(document, to: document.path)
        }
    }

    var anyDirty: Bool { documents.contains { $0.dirty } }

    func requestClose(_ document: EditorDocument) {
        if document.dirty {
            closing = document
        } else {
            close(document)
        }
    }

    func close(_ document: EditorDocument) {
        guard let index = documents.firstIndex(where: { $0.id == document.id }) else { return }
        documents.remove(at: index)
        if currentID == document.id {
            currentID = documents.indices.contains(index) ? documents[index].id : documents.last?.id
        }
    }

    func closeAll() {
        documents.removeAll()
        currentID = nil
    }

    func closeOthers(after document: EditorDocument) {
        guard let index = documents.firstIndex(where: { $0.id == document.id }) else { return }
        for other in documents[(index + 1)...].reversed() where !other.dirty {
            close(other)
        }
    }

    /// Asks for a folder anywhere on the device, once the sheet that asked is gone.
    func pickFolder() {
        sheet = nil
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { self.pickingFolder = true }
    }

    /// Copies the project's folder where the system's file browser says.
    func exportProject() {
        let root = studio.projectRoot
        guard !root.isEmpty else { return }
        saveAll()
        if mode == .scene, studio.sceneDirty { saveScene() }
        let picker = UIDocumentPickerViewController(forExporting: [URL(fileURLWithPath: root)], asCopy: true)
        UIApplication.shared.topController?.present(picker, animated: true)
    }

    func goToLine(_ line: Int) {
        guard let document = current else { return }
        if let textView = document.textView {
            textView.goToLine(line)
        } else {
            document.pendingLine = line
        }
    }

    func trimTrailingWhitespace() {
        current?.textView?.trimTrailingWhitespace()
    }

    func setTextZoom(_ zoom: Double) {
        textZoom = min(max(zoom, 0.5), 4.0)
        studio.textZoom = textZoom
    }

    // MARK: Runs

    func run() {
        if mode == .scene {
            studio.runScene()
        } else {
            current?.textView?.flushText()
            studio.runCurrentDocument()
        }
    }

    // MARK: Modes

    func showScene() {
        if studio.enterSceneMode() {
            mode = .scene
            sceneRevision += 1
        }
    }

    func showCode() {
        studio.leaveSceneMode()
        mode = .code
    }

    func saveCurrent() {
        if mode == .scene {
            saveScene()
        } else {
            save(current)
        }
    }

    /// Saves the level, and says why when it cannot be.
    func saveScene() {
        let errors = output.last?.id ?? 0
        if !studio.saveScene() {
            let reason = output.last { $0.id > errors && $0.severity == .error }?.text
            message = reason ?? tr("The level could not be saved.")
        }
    }

    // MARK: Output

    func append(_ text: String, severity: DBCSeverity) {
        nextOutputID += 1
        output.append(OutputLine(id: nextOutputID, text: text, severity: severity))
        if output.count > Self.maxOutputLines {
            output.removeFirst(output.count - Self.maxOutputLines)
        }
    }

    func applyTheme(_ theme: String) {
        studio.theme = theme
        themeRevision += 1
    }

    // MARK: DBCStudioDelegate

    func studioAppendOutput(_ line: String, severity: DBCSeverity) {
        append(line, severity: severity)
        if severity == .error {
            bottomPanel = 0
            sceneBottomTab = 1
            showBottomPanel = true
        }
    }

    func studioStatusChanged(_ status: String) {
        self.status = status
    }

    func studioProblemsChanged(_ problems: [DBCDiagnostic]) {
        self.problems = problems
        for document in documents {
            document.errors = [:]
        }
        for problem in problems where problem.isError && problem.line > 0 {
            if let document = document(at: problem.path), document.errors[problem.line] == nil {
                document.errors[problem.line] = problem.message
            }
        }
        if !problems.isEmpty {
            bottomPanel = 1
            showBottomPanel = true
        }
    }

    func studioProjectChanged() {
        projectRevision += 1
    }

    func studioRunStateChanged(_ running: Bool) {
        self.running = running
    }

    func studioNeedsProjectResolution() {
        sheet = .resolution
    }

    func studioOpenProjectSettings() {
        sheet = .projectSettings
    }

    func studioSceneChanged() {
        // Several changes in one pass of the run loop redraw the panels once.
        guard !sceneChangePending else { return }
        sceneChangePending = true
        DispatchQueue.main.async {
            self.sceneChangePending = false
            self.sceneRevision += 1
        }
    }

    func studioOpenDocuments() -> [DBCOpenDocument] {
        documents.map { document in
            document.textView?.flushText()
            return document.bridged()
        }
    }

    func studioCurrentPath() -> String? {
        current?.path
    }

    func studioOpen(inEditor path: String) -> Bool {
        open(path: path)
    }

    func studioReplaceText(_ text: String, atPath path: String, saved: Bool) -> Bool {
        guard let document = document(at: path) else { return false }
        let wasDirty = document.dirty
        if let textView = document.textView {
            textView.replaceAll(with: text)
            // A view that is not on screen may refuse the edit, and would then give its old text back.
            if textView.text != text { textView.text = text }
        }
        document.text = text
        document.revision += 1
        if saved {
            document.savedText = text
        }
        document.dirty = document.text != document.savedText
        // A tab without edits of its own goes to its file at once: the change is the Studio's, as
        // when a saved level links itself into the main source.
        if document.dirty && !wasDirty {
            write(document, to: document.path)
        }
        return true
    }

    func studioGo(toLine line: Int) {
        goToLine(line)
    }

    func studioPreviewFile(_ path: String) {
        previewPath = path
    }

    func studioCloseAllDocuments() {
        closeAll()
    }

    func studioShowCodeMode() {
        mode = .code
    }
}
