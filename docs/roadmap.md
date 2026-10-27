# Roadmap

## Decisions

- Publication from the GitHub repository `demonixis/open-dbc`, starting from a single initial commit.
- Code signing (Apple notarisation, Windows) is done later, in one batch with openblood2 and oxrsys.
- Yannick Comte owns the code. No DarkBASIC Classic content is published or
  linked; reused DBPro code comes from the MIT repository and keeps its notice.
- Project assets are CC0. Version 0.4 ships with the current examples: six demos
  and tutorials covering the 747 Classic help symbols.

## Before 0.4

Build and release:

- [ ] Release presets: macOS 13 universal2, Linux on an old glibc, Windows.
- [ ] Tests: 120 s default timeout, parallel runs where no lock is needed.
- [ ] FetchContent: shallow clones and a cache; `OPENDBC_WERROR` option.
- [ ] Version `0.4.0` and `opendbc --version`.
- [ ] Platform metadata: `Info.plist` and entitlements, `.desktop` and AppStream, Windows `VERSIONINFO`.

Robustness:

- [ ] Studio: backup before each run, autosave, keep CP1252, CRLF and BOM, Problems panel.
- [ ] Run the BASIC program in a child process, or catch exceptions around the tick.
- [ ] Depth limits for the parser, call stack, `GOSUB` and `DIM`; out-of-range literals.
- [ ] UTF-8 paths on Windows, with a test.
- [ ] Fuzzing: frontend, DDS, DBO, WAV, memblocks.

Repository files:

- [ ] `SECURITY.md` (running a `.dba` is running a native program), `CODE_OF_CONDUCT.md`, `CHANGELOG.md`.
- [ ] Issue and PR templates, `CODEOWNERS`, Dependabot.
- [ ] README screenshots.

Publication:

- [ ] GitHub settings: secret scanning, private vulnerability reporting, protected `main`.
- [ ] Public switch, signed tag `v0.4.0`, release with `SHA256SUMS` and an SBOM.
- [ ] FAQ: no DarkBASIC content provided; unrelated to comma.ai `opendbc`.

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

- [ ] **Compile HLSL Effect** also writes `.msl`, effects load it like engine
  shaders, and the engine stops linking SDL_shadercross.
- [ ] Versioned bytecode files bound to the runtime fingerprint, keeping line
  information for run-time errors, verified at load.
- [ ] `opendbc-runner` target without the BASIC frontend, ImGui or SDL_shadercross.
- [ ] Desktop, iOS and Android exports use the runner as host and ship bytecode.
- [ ] Studio packaging with the runner: nested macOS bundle signed inside out,
  shared folder on Windows and Linux.

## Towards 1.0

- [ ] Decide whether to keep the `DbcNew` / `DbProNew` profiles.
- [ ] Studio: embedded help, installed samples, welcome screen.
- [ ] Split the largest source files; measure coverage; negative verifier tests.
- [ ] One policy for missing objects, documented integer semantics, deterministic `RND`.
- [ ] Translatable interface strings.
