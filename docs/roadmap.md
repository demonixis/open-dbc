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

## Towards 1.0

- [ ] Decide whether to keep the `DbcNew` / `DbProNew` profiles.
- [ ] Studio: embedded help, installed samples, welcome screen.
- [ ] Split the largest source files; measure coverage; negative verifier tests.
- [ ] One policy for missing objects, documented integer semantics, deterministic `RND`.
- [ ] Translatable interface strings.
