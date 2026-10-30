# Contributing to OpenDBC

## Build and test

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

On Windows, run these commands from an **x64 Native Tools Command Prompt** or
Developer PowerShell for Visual Studio 2022 or newer, with the Windows SDK,
Ninja and Python 3 available. `windows-release` configures and builds the x64
release from the same developer shell.

The test presets run headless: no window takes focus and audio is silent. A single
program can be checked the same way, saving its last frame:

```bash
build/default/tools/studio/opendbc.app/Contents/MacOS/opendbc run program.dba --dialect dbc --headless --max-frames 120 --capture-output frame.bmp
```

A macOS release builds for Apple silicon with `cmake --preset release-macos` and
`cmake --build --preset release-macos`.

ASan and UBSan (GCC or Clang):

```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

With Clang, that build also fuzzes what OpenDBC reads from files: BASIC, program
images, DDS, WAV, DBO and memblocks. Each distinct crash lands with its input in
`<work>/<format>/crashes`, and becomes a regression test once fixed.

```bash
python3 scripts/fuzz_inputs.py --seconds 300 --work build/fuzz
```

Dependencies are pinned commits, fetched as shallow clones into
`build/_deps-cache`, which every build directory shares; `-DOPENDBC_DEPENDENCY_CACHE=<dir>`
moves it. Warnings in OpenDBC's own sources are errors; `-DOPENDBC_WERROR=OFF`
relaxes this for a compiler newer than the maintainers'.

## Exports

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

To qualify Windows signing, set `OPENDBC_WINDOWS_CERTIFICATE` to a trusted code
signing certificate and run the skipped test, which signs, timestamps and verifies
the runner and the OpenXR loader:

```bash
build/default/tests/unit/test_artifact_export.exe --no-skip --test-case="Windows signed exports pass Authenticode verification"
```

## Rules

- Code conventions, error handling, architecture constraints and the validation
  gates are described in [AGENTS.md](../AGENTS.md); they apply to everyone.
- Every change of behaviour comes with a regression test.
- Derived files (language references, corpus expectations, README metrics,
  fixtures, example media) are regenerated with their scripts in `scripts/`, never
  edited by hand.
- Contributions are released under the MIT Licence ([LICENSE](../LICENSE)).

## Content from DarkBASIC

- Do not add anything from the DarkBASIC Classic help, examples, demos, media or
  binaries, nor code from other The Game Creators products. Compatibility is
  rebuilt from behaviour, tests and the MIT DBPro sources only.
- Code or text taken from the [Dark-Basic-Pro](https://github.com/Dark-Basic-Software-Limited/Dark-Basic-Pro)
  repository keeps its MIT notice, listed in [NOTICE.md](NOTICE.md).
- Every media file added under `examples/` needs a row in `examples/ASSETS.tsv`
  with its digest, licence and source. `scripts/audit_example_assets.py`
  (CTest `example_asset_provenance`) checks it.
