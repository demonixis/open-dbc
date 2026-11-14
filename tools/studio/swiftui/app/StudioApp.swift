import SwiftUI

/// The SwiftUI Studio: the same Studio as the Dear ImGui one, with the system's own interface.
@main
struct StudioApp: App {
    @StateObject private var model = StudioModel.shared
    @Environment(\.scenePhase) private var scenePhase
    private let started: Bool

    init() {
        started = DBCStudio.shared.start()
        if started {
            StudioModel.shared.start()
            #if DEBUG
            DebugHooks.start(StudioModel.shared)
            #endif
        }
    }

    var body: some Scene {
        WindowGroup {
            if started {
                RootView(model: model)
                    .onChange(of: scenePhase) { phase in
                        let studio = DBCStudio.shared
                        switch phase {
                        case .active:
                            studio.applicationWillEnterForeground()
                            studio.applicationDidBecomeActive()
                        case .inactive:
                            studio.applicationWillResignActive()
                        case .background:
                            studio.applicationDidEnterBackground()
                        @unknown default:
                            break
                        }
                    }
            } else {
                Text("OpenDBC could not start.")
            }
        }
        .commands {
            StudioCommands(model: model)
        }
    }
}
