# SwiftUI Studio

An experiment: a second Studio for iOS, iPadOS and Mac Catalyst whose interface is
SwiftUI instead of Dear ImGui. It installs beside the other one, as
`OPENDBC_SWIFTUI_BUNDLE_ID` (`<OPENDBC_BUNDLE_ID>.swiftui` unless set), and opens the
same projects.

## Layout

```text
tools/studio/swiftui/app/     the interface, in Swift
tools/studio/swiftui/bridge/  DBCStudio: the Studio's C++ as Objective-C, for Swift
```

The target `opendbc_swiftui` compiles the Studio's sources without `app.cpp` and the
ImGui panels. `DBCStudio.mm` does what `app.cpp` does for projects, files, the
language and runs; the interface owns the text of the tabs and answers the bridge
through `DBCStudioDelegate`.

The Scene mode keeps `panels::SceneEditor`. Its Hierarchy, Inspector and Level
Settings reach Swift as rows (`studio_forms.hpp`), one value per row; the viewport is
SDL's Metal view inside the interface, where the editor still draws the level and
its gizmos. A program runs in an SDL window of its own, in front of the interface.

## Build

```sh
cmake --preset ios
xcodebuild -project build/ios/OpenDBC.xcodeproj -scheme opendbc_swiftui \
  -destination 'generic/platform=macOS,variant=Mac Catalyst' CODE_SIGNING_ALLOWED=NO build
```

The app is `build/ios/tools/studio/Debug-maccatalyst/OpenDBCSwiftUI.app`. For a
device, configure with `-DOPENDBC_IOS_TEAM_ID=<team>`, which signs the apps, then
run the scheme from Xcode. An unsigned
Catalyst build is not sandboxed: it shares the desktop Studio's preferences folder.

A Debug build reads `OPENDBC_STUDIO_*` environment variables to open a project,
show a mode or a dialog and write what it draws
(`tools/studio/swiftui/app/DebugHooks.swift`).

## Differences from the Dear ImGui Studio

- Panels have fixed places; the toolbar shows or hides them, and a handle on their
  edge resizes them.
- File > Open Folder asks the system's file browser for a project folder anywhere
  (Files on iPadOS, the Finder on a Mac) and keeps access to it; File > Export
  Project copies the project's folder to a place picked the same way.
- The Hierarchy's title lists the levels, and the Scene mode reopens the last one.
- Find and replace are the system's.
- With a mouse or a trackpad the viewport takes the primary button only: a drag off
  the gizmo or with Alt turns the view, with Shift it pans, and scrolling zooms. The
  movement keys still fly while the pointer is over it.
- In the Hierarchy, the list button makes a tap add to the selection, and a row's
  menu selects up to that row.
- Not there yet: thumbnails of models in Assets, the `.dbpro` tabs of Project
  Settings (the file opens as text), previews of `.ogg`, `.mod` and `.xm` sounds,
  and help on the word under the pointer.
