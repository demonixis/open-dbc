# Roadmap

## Decisions

- The public repository is `demonixis/open-dbc`; its history starts from a single initial import.
- No `DbcNew` / `DbProNew` profiles: OpenDBC runs Classic and Pro programs with
  its own additional commands, and stays that way.
- Signing OpenDBC's own releases (Apple notarisation, Windows) is done later, in one batch with
  openblood2 and oxrsys. Exported games are signed by the export ([usage.md](usage.md#export)).
- Yannick Comte owns the code. No DarkBASIC Classic content is published or
  linked; reused DBPro code comes from the MIT repository and keeps its notice.
- Project assets are CC0. Version 0.4 ships with the current examples: six demos
  and tutorials covering the 747 Classic help symbols.

## Before 0.4

Build and release:

- [ ] Release presets: universal2 macOS and Linux on an old glibc.
- [ ] Platform metadata: macOS entitlements, `.desktop` and AppStream. The macOS
  `Info.plist` comes from CMake's bundle properties.

Robustness:

- [ ] Studio: backup before each run, autosave, keep CP1252, CRLF and BOM, Problems panel.
- [ ] Run the BASIC program in a child process so a crash cannot take the Studio down;
  no `try`/`catch` around the tick.
- [ ] Depth limits for the parser, call stack, `GOSUB` and `DIM`; out-of-range literals.
- [ ] Fuzzing: frontend, DDS, DBO, WAV, memblocks.

Repository files:

- [ ] `CHANGELOG.md` at the root; `docs/SECURITY.md` (running a `.dba` is running a
  native program) and `docs/CODE_OF_CONDUCT.md` beside `docs/CONTRIBUTING.md`.
- [ ] Issue and PR templates, `CODEOWNERS`, Dependabot. No CI workflow for now: it
  took too many iterations to get working.
- [ ] README screenshots.

Publication:

- [ ] Signed tag `v0.4.0`, release with `SHA256SUMS` and an SBOM.
- [ ] FAQ: no DarkBASIC content provided; unrelated to comma.ai `opendbc`.

## Code quality

- [ ] Code quality audit.
- [ ] Separate the smoke-test code from the main code: a dedicated smoke `main`, or
  one class compiled in Debug only.
- [ ] Reformat every C++ source once with `.clang-format`, in its own commit: 66 of
  276 files drift from it, before the blank lines `SeparateDefinitionBlocks` adds
  between glued definitions. Replace `.editorconfig`, which holds a copy of the
  clang-format YAML instead of EditorConfig settings.

## Classic multiplayer

Net games run over ENet ([network.md](network.md)).

- [ ] Host migration for peer-to-peer games, with `NET GAME NOW HOSTING()`.
- [ ] Platform permissions: iOS local network description and multicast
  entitlement for discovery, Android multicast lock, macOS sandbox network
  entitlements.
- [ ] DBPro: string parameters typed as numbers in the multiplayer signatures,
  `DEFAULT NET GAME`, `NET BUFFER SIZE`.

## Native plugins

`LOAD DLL` and `CALL DLL` run OpenDBC plugins ([plugins.md](plugins.md)).

- [ ] Exports: bundle each platform's plugin and sign it with the runner; iOS and
  Android need them inside the app package.
- [ ] Ship `sdk/opendbc_plugin.h` with the Studio.
- [ ] `CALL DLL()` string results need a string-typed form in the catalogue.

## DBPro BSP worlds

DBPro's own BSP format, its drawing and collision are done ([bsp.md](bsp.md)).

- [ ] Quake 3 maps from pk3 archives: zip reader, shaders, patches, CC0 test map.
- [ ] Quake 1/2 maps with lightmaps, then `SET BSP MULTITEXTURING ON/OFF`.

## Standalone runtime

Decisions:

- An exported game ships `opendbc-runner`, not the Studio: VM, engine and asset
  loaders, without the editor, the BASIC frontend, ImGui or DXC. The Studio keeps
  DXC and still runs unbuilt projects directly.
- A Studio release packages the runner beside it: `Contents/Helpers/opendbc-runner.app`
  inside the macOS bundle, sibling executables in the Windows and Linux folders.
- Exports contain verified bytecode, shaders in the target backend's format and
  assets as they are, never sources. Assets keep their formats because programs
  build file names at run time.
- Engine shaders are HLSL, compiled by the pinned DXC like effects.
- Metal shaders ship as MSL source; the engine no longer transpiles SPIR-V at run time.

Work:

- [ ] Studio packaging with the runner: nested macOS bundle signed inside out,
  shared folder on Windows and Linux.

## Exports

Exports build signed packages from the Build window ([usage.md](usage.md#export)).

- [ ] Qualify Windows export signing and verification with a trusted code signing
  certificate.
- [ ] Run the Linux export on Linux, then against the Steam Runtime.
- [ ] Notarise a macOS export with a real notary profile, and archive and upload
  an iOS export from Xcode.
- [ ] Mac App Store target: sandboxed runner, Apple Distribution signing, `.pkg`.

## Rendering and media

- [ ] Android video decoder and an iOS run of AVFoundation; platforms without a
  decoder load every animation inert today ([video.md](video.md)).
- [ ] Qualify Windows Media Foundation with audible output and hardware decoding.
- [ ] Qualify Linux GStreamer playback with real audio output, hardware decoding
  and exported games against the Steam Runtime ([video.md](video.md)).
- [ ] `SET SHADOW SHADING ON` indoors: a cube in a closed box room, lit by a
  point light inside it (`SET SHADOW POSITION 1, ...` or the same fixed
  position), casts no visible shadow, and a lighter patch appears where the
  shadow should be when the room's normals face outwards. Check the z-fail
  volumes against room geometry and the far plane.
- [ ] Reproducible example meshes: Blender 5.1's Subdivision Surface modifier
  returns its vertices in a different order, with last-bit differences, on
  every evaluation, even in one process with one thread, so `xexport.py` output
  changes on each run. Sorting or rounding in the exporter cannot fix it;
  replace the modifier in the generators with deterministic geometry.
- [ ] Regenerate the baked textures: `bake_textures.save_jpeg` now writes the
  exact sRGB encoding (linear 0.5 gives 188, the committed JPEGs have 170), so
  every texture gets brighter; review the examples when regenerating.

## Studio Scene view

- [ ] Preview `.dbo` models (Assimp cannot read them; a box stands in) and
  `.dds` textures and thumbnails (SDL_image cannot read them).
- [ ] Interactive UI coverage: navigation, gizmos, drag and drop and the
  Inspector are checked by hand only.

## Towards 1.0

- [ ] Remove the `DbcNew` / `DbProNew` remnants: the reserved touch, orientation and
  accelerometer commands become shared additions to both profiles or go away;
  update `architecture.md`, the editorial data, the generators and the extension
  rule in `AGENTS.md`.
- [ ] Studio: embedded help, installed samples, welcome screen.
- [ ] Split the largest source files; measure coverage; negative verifier tests.
- [ ] One policy for missing objects, documented integer semantics, deterministic `RND`.
- [ ] Translatable interface strings.
