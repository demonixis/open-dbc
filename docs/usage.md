# Usage

## Command line

```text
opendbc                                   open the Studio
opendbc studio [file-or-directory]
opendbc run     <source.dba|project.dbpro|program.obc> [--dialect dbc|dbpro] [options]
opendbc check   <source.dba|project.dbpro> [--dialect dbc|dbpro]
opendbc inspect <source.dba|project.dbpro> [--json] [--dialect dbc|dbpro]
opendbc build   <source.dba|project.dbpro> [--dialect dbc|dbpro] --output <program.obc>
opendbc export  <source.dba|project.dbpro> --dialect dbc|dbpro --target <target> --output <dir> [--unsigned]
```

`check` and `inspect` never open a window. `opendbc --help` lists every option;
`opendbc --version` and `opendbc-runner --version` print the version.

`build` writes a program image: verified bytecode, the project's startup settings
and source lines for run-time errors, with paths relative to the project. `run`
verifies the image again and runs it without sources; assets resolve from the
image's directory unless `--asset-root` says otherwise. An image only loads into
the OpenDBC build that produced it (same runtime fingerprint); rebuild it after
an update.

`opendbc-runner [program.obc] [run options]` runs an image without the Studio;
without a path it runs `program.obc` from its own resources.

| `run` option | Effect |
| --- | --- |
| `--asset-root DIR` | Base directory for media and file paths (default: the source's directory) |
| `--max-frames N` | Stop after N presented frames |
| `--headless` | Hidden window, offscreen rendering, silent audio (also `OPENDBC_HEADLESS=1`) |
| `--capture-output F.bmp` | With `--headless`, save the last frame |
| `--log-level LEVEL` | `trace`, `debug`, `info`, `warn` (default), `error`; log file `open-dbc.log` |
| `--log-directory DIR` | Where `open-dbc.log` goes (default: the source's directory) |
| `--debug-overlay` | Start with the overlay shown (**F3** toggles it) |
| `--command-line TEXT` | Value returned by `CL$()` |
| `--builtin-coverage-output F` | Write the calls made per overload |
| `--benchmark --benchmark-output F` | Write frame timings and renderer statistics |

While a program runs, **F3** toggles the debug overlay and **F4** writes
`open-dbc-dump-scene.txt` and `open-dbc-dump-renderer.txt`.

## Profiles

Every BASIC source is `.dba`; a `.dbpro` file is a Professional project
descriptor. A run uses exactly one profile, chosen in this order: `--dialect`,
the project configuration, a unique `.dbpro` descriptor, then decisive syntax.
An ambiguous source is an error on the command line and a question in the Studio;
it never falls back to Classic silently. Includes inherit the root's profile.

Paths in BASIC accept `/`, `\` or a mix, and resolve from the asset root. POSIX
hosts do not emulate drive letters or UNC paths.

## Studio

**F5** runs the active `.dba` tab, unsaved edits included; **Shift+F5** stops it.
The Studio compiles the program, then runs it in `opendbc-runner`, a separate
process with its own window, so a crash or hang in the program leaves the Studio
running; its `open-dbc.log` still goes beside the source. The **Problems** panel
lists the diagnostics of the last run, or of the check that follows each save of
a BASIC file; selecting one opens its file at its line.
A standalone source finds its media beside itself; a DBPro project uses its
descriptor's media root and source order. The Output panel shows the source and
asset root of each run, and diagnostics are marked in the matching tab.

Saving keeps a file's encoding, line endings and byte order mark. A file that is
not valid UTF-8 is read as Windows-1252, DarkBASIC's code page, and saved back
in it; if it gains a character Windows-1252 cannot store, it is saved as UTF-8
and the Output panel says so. Before each run and every 30 seconds, the Studio
copies the unsaved tabs, untitled ones included, to the `recovery` folder of its
preferences directory; it never writes your files until you save. After a crash
it offers to restore those tabs, and a normal quit deletes the copies.

**File → New Project…** (Ctrl+Shift+N) creates a folder holding `main.dba`,
from an empty main loop or a basic 1280×720 scene. A DBPro project also gets a
historical `.dbpro` descriptor; a DBC project gets an `.opendbc-project.json`
that sets its dialect and entry.

**Edit → Insert Snippet…** (Ctrl+J) adds a snippet: cameras (FPS, TPS, orbit),
a basic scene, a delta-time loop, lighting, fog, sound, game states, saving and
more. A snippet made only of functions joins the `rem #region Snippets` …
`rem #endregion` block at the end of the file, created on first use, and is
refused when the file already declares one of its functions; any other snippet
goes at the cursor. **Edit → Save Selection as Snippet…** adds your own to
the `snippets` folder of the Studio's preferences directory. A snippet is a
`.dba` file whose optional leading `rem Snippet:`, `rem Profiles: dbc dbpro`
and `rem Description:` lines name it, restrict it to profiles and describe it;
the dialog only lists those the current tab's profile accepts.

The toolbar holds New, Open, Save, Run and Stop. Beside the Project panel, the
**Code** panel outlines the active tab: functions with their call counts, labels
with their GOTO/GOSUB jumps, types and main-program variables; selecting one
goes to its line. Counts cover that file only, not its includes. The **Media**
panel lists the asset root's images, audio, models, effects, videos and fonts,
filtered by kind and name; double-clicking an image or sound previews it.
`.fx` and `.hlsl` effects open in the editor as HLSL text, without completion,
outline or Run.

**Build → Compile HLSL Effect…** compiles a `.fx` or `.hlsl` file with `VSMain`
and `PSMain` entry points into SPIR-V and MSL stages (`<name>.vert.spv`,
`<name>.vert.msl`, `<name>.frag.spv`, `<name>.frag.msl`) and `<name>.effect.json`,
which DBPro `LOAD EFFECT` loads. More techniques are
declared with `// opendbc:technique <name> <VS> <PS>`. Effect inputs: vertex
locations 0–6 (position, normal, colour, UV, three gradient colours), vertex
constants `b0, space1` (with an automatic `ViewProj`), fragment texture
`t0/s0, space2` and fragment constants `b0, space3`. DirectX 9 assembly and
`technique/pass` blocks are rejected.

## Export

**Build > Build...** chooses the target and a Development or Distribution
build, lists what the target still needs on this machine, then exports in the
background: the Output panel shows each tool's output and **Cancel** stops it.
The command line runs the same checks; `--unsigned` makes a Development build.

```bash
opendbc export game.dba --dialect dbc --target desktop --output dist
opendbc export game.dbpro --dialect dbpro --target android-aab --output dist
```

| Target | Development build | Distribution build |
| --- | --- | --- |
| `desktop`, macOS | `Name.app`, ad hoc signed | Developer ID signature with the hardened runtime, notarised and stapled with a notary profile, and `Name.zip` |
| `desktop`, Windows | `Name/`, whose `Name.exe` has the project icon and version | signed with signtool, and `Name.zip` |
| `desktop`, Linux | `Name/` with the executable, its icon and a `.desktop` entry | and `Name.tar.gz` |
| `android-apk`, `android-aab` | `Name.apk` or `Name.aab` signed with the debug key | signed with the project keystore and verified |
| `ios`, on macOS | `Name-Xcode/`, an Xcode project | signed in Xcode: choose the team, then Product > Archive |

An export holds the runner, the compiled program image and the project's
assets, never its `.dba` or `.dbpro` files; it refuses path traversal, symbolic
links, secret files and `#INCLUDE` files outside the project. At first launch the
payload is copied to per-user storage, where the program writes its files and
log. The project's identity, version, icon, permissions and keystore live in
`.opendbc-project.json`; tools and signing identities belong to the machine, in
the Studio preferences. Exported macOS apps run on macOS 13.3 and later, the
`CMAKE_OSX_DEPLOYMENT_TARGET` OpenDBC is built with.

OpenDBC never stores a password. The Studio keeps them for its session; the
command line and build machines read the environment:

| Variable | Value |
| --- | --- |
| `OPENDBC_MAC_SIGNING_IDENTITY` | Developer ID Application identity |
| `OPENDBC_NOTARY_PROFILE` | profile saved by `xcrun notarytool store-credentials` |
| `OPENDBC_WINDOWS_CERTIFICATE` | SHA-1 thumbprint of a code signing certificate in the user's store (`certutil -user -importPFX` imports a `.pfx`) |
| `OPENDBC_WINDOWS_TIMESTAMP_URL` | RFC 3161 timestamp server, DigiCert's by default |
| `OPENDBC_SIGNTOOL` | `signtool.exe`, when no Windows SDK holds it |
| `OPENDBC_ANDROID_KEYSTORE_PASSWORD`, `OPENDBC_ANDROID_KEY_PASSWORD` | Android keystore and key passwords |

Windows native export tests inspect PE icon/version resources and launch the exported
runner from Unicode directories. To qualify signing with an installed certificate,
set `OPENDBC_WINDOWS_CERTIFICATE` and run
`build/default/tests/unit/test_artifact_export.exe --no-skip --test-case="Windows signed exports pass Authenticode verification"`.
This signs and timestamps both the runner and OpenXR loader, then verifies each
with `signtool verify /pa`. It requires a trusted code signing certificate and
access to the configured timestamp server.

Android builds need a JDK 17 or newer and the Android SDK, found through
`JAVA_HOME`, `ANDROID_HOME`, Android Studio or the Studio preferences; the first
build downloads Gradle. The Build window creates the upload keystore Google Play
ties to the app: keep a copy of the file and its password.

Mobile targets embed runtimes built from the same sources as the Studio (macOS,
Android NDK `26.3.11579264`); configure them again together after a change:

```bash
cmake --preset ios-runtime && cmake --build --preset ios-runtime
cmake --preset ios-simulator-runtime && cmake --build --preset ios-simulator-runtime
cmake --preset android-host && cmake --build --preset android-host
cmake --preset default \
  -DOPENDBC_IOS_RUNTIME_LIBRARY="$PWD/build/ios-runtime/tools/runner/libOpenDBCRuntime.a" \
  -DOPENDBC_IOS_SIMULATOR_RUNTIME_LIBRARY="$PWD/build/ios-simulator-runtime/tools/runner/libOpenDBCRuntime.a" \
  -DOPENDBC_ANDROID_EXPORT_HOST_LIBRARY="$PWD/build/android-host/tools/runner/libmain.so"
cmake --build --preset default
```

`scripts/qualify_exports.py --mobile --run-ios-simulator` exports, builds and
runs every target this Mac can.

On touch screens the top, bottom, left, right and centre zones act as the arrow
keys and space; the back action is escape.
