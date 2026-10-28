# OpenDBC

[![License: MIT](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

OpenDBC runs DarkBASIC Classic and DarkBASIC Professional programs on modern
systems. It is a single `opendbc` executable, a Studio and a command line, written
in C++20 on SDL3 GPU (Vulkan, Metal). It is an independent project, not
affiliated with The Game Creators or Dark Basic Software Limited.

```basic
sync on
make object cube 1, 10
position object 1, 0, 0, 50
do
    yrotate object 1, object angle y(1) + 1
    sync
loop
```

## Build and run

Requires CMake 3.24+, Ninja and a C++20 compiler (Visual Studio 2022+ on Windows).

```bash
cmake --preset default
cmake --build --preset default
```

```bash
opendbc                                   # open the Studio
opendbc run game.dba --dialect dbc        # run a Classic program
opendbc run project.dbpro                 # run a Professional project
opendbc check game.dba --dialect dbpro    # compile and verify only
```

## Test

```bash
ctest --preset default
```

<!-- BEGIN GENERATED COMPATIBILITY METRICS -->
- BASIC execution goldens: **77**.
- Full corpus expectations: **95** sources, including **85** that reach verified bytecode.
- Observable implemented-overload contracts: **1685**.
<!-- END GENERATED COMPATIBILITY METRICS -->

## Documentation

| Topic | Where |
| --- | --- |
| CLI, Studio, exports, debugging | [docs/usage.md](docs/usage.md) |
| Studio Scene view (level editor) | [docs/level-editor.md](docs/level-editor.md) |
| Code layout, language pipeline, compatibility rules, tests | [docs/architecture.md](docs/architecture.md) |
| Commands and their support state, per profile | [docs/reference/](docs/reference/) |
| Desktop VR | [docs/vr.md](docs/vr.md) |
| DBPro BSP worlds | [docs/bsp.md](docs/bsp.md) |
| Classic animations (video) | [docs/video.md](docs/video.md) |
| Classic multiplayer | [docs/network.md](docs/network.md) |
| Native plugins (Classic DLLs) | [docs/plugins.md](docs/plugins.md) |
| Example programs | [examples/README.md](examples/README.md) |
| Logos, social headers, wallpapers | [docs/branding.md](docs/branding.md) |
| Contributing | [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) |
| Remaining work | [docs/roadmap.md](docs/roadmap.md) |

## Licence

MIT, see [LICENSE](LICENSE). Trademarks and third-party content are listed in
[docs/NOTICE.md](docs/NOTICE.md).
