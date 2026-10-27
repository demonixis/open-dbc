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

Goal: `examples/dbpro/tutorials/world/world1.dba` runs with its level and camera
collision. Behaviour follows the MIT DBPro sources (`Shared/Q3BSP/CBSPFile.cpp`
dispatch, `Shared/CustomBSP/`).

Decisions:

- `ikzdm1.BSP` is the snapshot file, byte for byte, in DBPro's own BSP format
  (nodes, planes, leaves with polygon ranges, run-length PVS, 21-byte texture
  names). The tutorial needs no Quake map, pk3 or replacement asset.
- `LOAD BSP` keeps DBPro's order: Quake 3, Quake 1/2, own format, each tried with
  the given name then inside the pk3's folder. OpenDBC reads the own format; a
  Quake file fails with a runtime error naming its format.
- Texture names keep their `.bmp` spelling; a missing file falls back to the
  converted media with the same stem next to the map.
- The world draws with the lit object pipeline (white material, wrapped linear
  sampling). Each frame the leaf of the BSP camera (`SET BSP CAMERA`, default 0)
  selects the PVS leaves, culled by the rendering camera's frustum; a BSP camera
  in solid space draws nothing, as in DBPro.
- Collision indices 1-24 (others raise DBPro's error) bind a camera or object to
  an ellipsoid: radius r gives (r, 2r, r), the `RADIUS` forms give x, y, z.
  Response 0 slides, 1 returns to the last free position; the height adjustment
  offsets the centre. Collisions resolve at `SYNC` before drawing or on
  `PROCESS BSP COLLISION`; start positions are captured after the frame.
- The solver sweeps the ellipsoid against every triangle with at most 5 slide
  steps and a 0.001 skin: DBPro's contract, not its numerical quirks.
  `BSP COLLISION HIT` reads and clears; `BSP COLLISION X/Y/Z` return the resolved
  minus the requested position.
- `SET BSP COLLISION THRESHHOLD` is the minimum residual move (Quake 3 meaning);
  the own-format default stays 0.003.
- Collision commands do nothing while no world is loaded, as in DBPro.
- `SET BSP MULTITEXTURING ON/OFF` only drive Quake 1/2 lightmaps in DBPro; they
  stay unsupported until such a format is read.

Remaining work:

- [ ] Camera and object collision commands and functions, with solver unit tests;
  `world1` lands on the floor (camera y about -30.5).
- [ ] Qualification: `world1` verified in `dbpro_demo_expectations.tsv`, the corpus
  expectations and `qualify_dbpro_examples.py`, with a frame row.
- [ ] Later: Quake 3 maps from pk3 (zip reader, CC0 test map), Quake 1/2 maps with
  lightmaps and multitexturing.

## Towards 1.0

- [ ] Decide whether to keep the `DbcNew` / `DbProNew` profiles.
- [ ] Studio: embedded help, installed samples, welcome screen.
- [ ] Split the largest source files; measure coverage; negative verifier tests.
- [ ] One policy for missing objects, documented integer semantics, deterministic `RND`.
- [ ] Translatable interface strings.
