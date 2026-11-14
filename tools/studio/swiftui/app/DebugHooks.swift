#if DEBUG
import SwiftUI
import UIKit

/// Lets a Debug build be checked without a hand on it. Environment variables:
/// OPENDBC_STUDIO_OPEN (a project folder to open), OPENDBC_STUDIO_MODE ("scene"),
/// OPENDBC_STUDIO_SHEET (a StudioSheet name), OPENDBC_STUDIO_CREATE (a node kind to add),
/// OPENDBC_STUDIO_RUN (runs the program), OPENDBC_STUDIO_OUTPUT (a file the Output panel is
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
        if let path = environment["OPENDBC_STUDIO_OUTPUT"] {
            Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
                var text = model.output.map(\.text).joined(separator: "\n") + "\nstatus: " + model.status + "\n"
                if model.mode == .scene { text += "camera: " + model.studio.sceneCameraDescription + "\n" }
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
}
#endif
