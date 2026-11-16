#if DEBUG
import SwiftUI
import UIKit

/// Lets a Debug build be checked without a hand on it. Environment variables:
/// OPENDBC_STUDIO_OPEN (a project folder to open), OPENDBC_STUDIO_MODE ("scene"),
/// OPENDBC_STUDIO_SHEET (a StudioSheet name), OPENDBC_STUDIO_CREATE (a node kind to add),
/// OPENDBC_STUDIO_RUN (runs the program), OPENDBC_STUDIO_STEPS (steps a second apart, separated by
/// ";": "level:<name>", "create:<kind>", "save", "closeall", "run", "stop", "sample:<name>", "delete:<project>", "duplicate:<project>",
/// "rename:<project>=<name>", "project:<name>", "sceneproject:<name>", "folder:<path>", "scene", "language:<code>", "select:<row>", "number:<label>=<value>", "flag:<label>=<0|1>", "text:<label>=<value>", "assets",
/// "menus", "menu:<menu>><item>"), OPENDBC_STUDIO_OUTPUT (a file the Output panel is
/// written to), OPENDBC_STUDIO_VIEWPORT (a PNG of what the viewport's renderer drew) and
/// OPENDBC_STUDIO_SNAPSHOT (a PNG the window is drawn to every two seconds; the
/// Metal viewport is not part of that drawing).
enum DebugHooks {
    static func start(_ model: StudioModel) {
        let environment = ProcessInfo.processInfo.environment
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            if let folder = environment["OPENDBC_STUDIO_OPEN"] {
                model.studio.openProjectFolder(folder)
            }
            if environment["OPENDBC_STUDIO_MODE"] == "scene" {
                model.showScene()
            }
            if let name = environment["OPENDBC_STUDIO_SHEET"], let sheet = StudioSheet(rawValue: name) {
                model.sheet = sheet
            }
            if let kind = environment["OPENDBC_STUDIO_CREATE"].flatMap({ Int($0) }) {
                model.studio.sceneCreate(kind, light: 0)
            }
            if environment["OPENDBC_STUDIO_RUN"] != nil {
                model.run()
            }
        }
        if let steps = environment["OPENDBC_STUDIO_STEPS"] {
            for (index, step) in steps.split(separator: ";").enumerated() {
                DispatchQueue.main.asyncAfter(deadline: .now() + 2.0 + Double(index)) { run(String(step), model) }
            }
        }
        if let path = environment["OPENDBC_STUDIO_OUTPUT"] {
            Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
                var text = model.output.map(\.text).joined(separator: "\n") + "\nstatus: " + model.status + "\n"
                if model.mode == .scene {
                    text += "camera: " + model.studio.sceneCameraDescription
                        + (model.studio.sceneHasSelection ? " selected" : "") + "\n"
                }
                try? text.write(toFile: path, atomically: true, encoding: .utf8)
            }
        }
        if let path = environment["OPENDBC_STUDIO_VIEWPORT"] {
            Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { _ in
                if model.mode == .scene { _ = model.studio.writeViewportImage(path) }
            }
        }
        guard let path = environment["OPENDBC_STUDIO_SNAPSHOT"] else { return }
        Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { _ in
            let windows = UIApplication.shared.connectedScenes
                .compactMap { $0 as? UIWindowScene }
                .flatMap(\.windows)
            guard let window = windows.first(where: \.isKeyWindow) ?? windows.first else { return }
            // A sheet is another controller's view, drawn over the window's.
            var top = window.rootViewController
            while let presented = top?.presentedViewController { top = presented }
            let image = UIGraphicsImageRenderer(bounds: window.bounds).image { _ in
                window.drawHierarchy(in: window.bounds, afterScreenUpdates: false)
                if let sheet = top?.view, top !== window.rootViewController {
                    sheet.drawHierarchy(in: sheet.convert(sheet.bounds, to: window), afterScreenUpdates: false)
                }
            }
            try? image.pngData()?.write(to: URL(fileURLWithPath: path))
        }
    }

    private static func run(_ step: String, _ model: StudioModel) {
        let studio = model.studio
        let parts = step.split(separator: ":", maxSplits: 1).map(String.init)
        let argument = parts.count > 1 ? parts[1] : ""
        switch parts[0] {
        case "level": studio.newLevel(argument)
        case "create": studio.sceneCreate(Int(argument) ?? 0, light: 0)
        case "save": model.saveCurrent()
        case "closeall": model.closeAll()
        case "run": model.run()
        case "sample":
            if let folder = studio.locations.first(where: \.samples)?.projects.first(where: { $0.fileName == argument }) {
                studio.openLocationProject(folder, samples: true)
            }
        case "delete", "duplicate", "rename":
            let pair = argument.split(separator: "=", maxSplits: 1).map(String.init)
            let folder = (studio.importRoot as NSString).appendingPathComponent(pair[0])
            let failure = parts[0] == "delete" ? studio.deleteProject(folder)
                : parts[0] == "duplicate" ? studio.duplicateProject(folder)
                : studio.renameProject(folder, to: pair.count > 1 ? pair[1] : "")
            if !failure.isEmpty { model.append(failure, severity: .error) }
            let names = studio.locations.first { !$0.samples }?.projects.map(\.fileName) ?? []
            model.append("projects: " + names.joined(separator: " | "), severity: .notice)
        case "stop": studio.stopRun()
        case "project":
            let failure = studio.createProjectNamed(argument, profile: .dbPro, basicScene: false)
            if !failure.isEmpty { model.append(failure, severity: .error) }
        case "sceneproject":
            let failure = studio.createProjectNamed(argument, profile: .dbPro, basicScene: true)
            if !failure.isEmpty { model.append(failure, severity: .error) }
        case "folder": studio.openPickedFolder(URL(fileURLWithPath: argument))
        case "scene": model.showScene()
        case "language":
            studio.language = argument
            model.themeRevision += 1
        case "select":
            if let row = studio.sceneRows().first(where: { $0.name == argument }) {
                studio.selectSceneRow(row.key, toggle: false, range: false)
            }
        case "number":
            let pair = argument.split(separator: "=").map(String.init)
            if pair.count == 2, let value = Double(pair[1]),
               let field = studio.sceneForm(.inspector).first(where: { $0.label == pair[0] }) {
                if field.optional && !field.isSet {
                    studio.sceneForm(.inspector, setOptional: true, field: field.identifier)
                }
                studio.sceneForm(.inspector, setNumber: value, field: field.identifier, commit: true)
            } else {
                model.append("no field " + argument, severity: .error)
            }
        case "flag", "text":
            let pair = argument.split(separator: "=", maxSplits: 1).map(String.init)
            guard pair.count == 2,
                  let field = studio.sceneForm(.inspector).first(where: { $0.label == pair[0] }) else {
                model.append("no field " + argument, severity: .error)
                break
            }
            if parts[0] == "flag" {
                studio.sceneForm(.inspector, setFlag: pair[1] == "1", field: field.identifier)
            } else {
                studio.sceneForm(.inspector, setText: pair[1], field: field.identifier, commit: true)
            }
        case "assets":
            let rows = studio.sceneForm(.inspector).filter { $0.kind == .asset }.map { "\($0.label)=\($0.text)" }
            model.append("assets: " + rows.joined(separator: " | "), severity: .notice)
        case "menus":
            model.append("menus: " + menuItems(mainMenu()).map(title).joined(separator: " | "), severity: .notice)
        case "menu":
            // Picks the item as a click would, validation included.
            var menu = mainMenu()
            let path = argument.split(separator: ">").map(String.init)
            for (depth, name) in path.enumerated() {
                let items = menuItems(menu)
                guard let index = items.firstIndex(where: { title($0) == name }) else {
                    model.append("no item \(name) in: " + items.map(title).joined(separator: " | "), severity: .error)
                    break
                }
                if depth + 1 < path.count {
                    menu = items[index].value(forKey: "submenu") as? NSObject
                    _ = menu?.perform(NSSelectorFromString("update"))
                } else if let menu {
                    let selector = NSSelectorFromString("performActionForItemAtIndex:")
                    typealias Perform = @convention(c) (NSObject, Selector, Int) -> Void
                    unsafeBitCast(menu.method(for: selector), to: Perform.self)(menu, selector, index)
                }
            }
        default: break
        }
        model.append("step \(step): project \(studio.projectRoot.fileName) blocker '\(studio.sceneBlocker)' level \(studio.levelName) dirty \(studio.sceneDirty) editable \(studio.sceneEditable)",
                     severity: .notice)
    }

    /// The Mac's menu bar, read from AppKit since UIKit does not list it.
    private static func mainMenu() -> NSObject? {
        let application = (NSClassFromString("NSApplication") as? NSObject.Type)?
            .value(forKey: "sharedApplication") as? NSObject
        return application?.value(forKey: "mainMenu") as? NSObject
    }

    private static func menuItems(_ menu: NSObject?) -> [NSObject] {
        menu?.value(forKey: "itemArray") as? [NSObject] ?? []
    }

    private static func title(_ item: NSObject) -> String {
        item.value(forKey: "title") as? String ?? ""
    }
}
#endif
