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

`check` and `inspect` never open a window. `opendbc --help` lists every option.

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
A standalone source finds its media beside itself; a DBPro project uses its
descriptor's media root and source order. The Output panel shows the source and
asset root of each run, and diagnostics are marked in the matching tab.

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

```bash
opendbc export game.dba --dialect dbc --target desktop --output dist
opendbc export game.dbpro --dialect dbpro --target android-apk --output dist
```

Targets: `desktop` (host OS), `ios`, `ios-simulator`, `android-apk`,
`android-aab`. An export holds `opendbc-runner`, the compiled program image and
the project's assets, never its `.dba` or `.dbpro` files; it refuses path
traversal, symbolic links, secret files and `#INCLUDE` files outside the project.
At first launch the payload is copied to per-user storage, where the program
writes its files and log. Per-project settings (identity,
icons, signing, permissions) live in `.opendbc-project.json`. Android passwords
come from `OPENDBC_ANDROID_KEYSTORE_PASSWORD` and `OPENDBC_ANDROID_KEY_PASSWORD`
and are never stored. `--unsigned` macOS exports get an ad hoc signature: they
launch locally but cannot be notarised.

Mobile targets need their `opendbc-runner` hosts built first (macOS, Android NDK
`26.3.11579264`):

```bash
cmake --preset ios && cmake --build --preset ios
cmake --preset ios-simulator && cmake --build --preset ios-simulator
cmake --preset android-host && cmake --build --preset android-host
cmake --preset default \
  -DOPENDBC_IOS_EXPORT_HOST="$PWD/build-ios/tools/runner/Release-iphoneos/opendbc-runner.app" \
  -DOPENDBC_IOS_SIMULATOR_EXPORT_HOST="$PWD/build-ios-simulator/tools/runner/Release-iphonesimulator/opendbc-runner.app" \
  -DOPENDBC_ANDROID_EXPORT_HOST_LIBRARY="$PWD/build-android-host/tools/runner/libmain.so"
cmake --build --preset default
```

On touch screens the top, bottom, left, right and centre zones act as the arrow
keys and space; the back action is escape.
