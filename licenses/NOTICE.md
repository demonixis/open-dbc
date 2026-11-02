# Notices

OpenDBC is released under the MIT Licence; see [LICENSE](../LICENSE). This file
lists what OpenDBC contains or ships that comes from someone else, and under which
licence, as those licences require.

## Trademarks

DarkBASIC and DarkBASIC Professional are trademarks of their respective holders.
They are named only to describe the languages OpenDBC is compatible with. OpenDBC
is an independent open-source project, not affiliated with, endorsed by or
sponsored by The Game Creators Ltd or Dark Basic Software Limited.

## Third-party content in this repository

| Path | Origin | Licence |
| --- | --- | --- |
| `examples/dbpro/tutorials/`, `examples/dbpro/Hello World/` | Help examples from the [pinned Dark-Basic-Pro snapshot](https://github.com/Dark-Basic-Software-Limited/Dark-Basic-Pro/tree/af6f19d7fc2919c356c683f7f5b8ec0c37a7d7d9) | MIT, © 1999-2015 The Game Creators Ltd; see [Dark-Basic-Pro-LICENSE.md](../licenses/Dark-Basic-Pro-LICENSE.md) |
| DBO block codes in `src/engine/systems/object_manager.cpp`; DBPro extension inventory in `scripts/data/` and the catalogue | Same DBPro snapshot | MIT, as above; the licence ships with every binary |
| `src/engine/noto_sans_regular_ttf.hpp` | Noto Sans Regular, embedded as a byte array | SIL Open Font License; see [NotoSans-LICENSE.md](../licenses/NotoSans-LICENSE.md) |
| Studio fonts, embedded at build time from the pinned Dear ImGui's `misc/fonts` | Roboto Medium | Apache License 2.0, © 2011 Google Inc.; see [Roboto-LICENSE.md](../licenses/Roboto-LICENSE.md) |
| Same | Cousine Regular | SIL Open Font License, © 2010 Google Corporation; see [Cousine-LICENSE.md](../licenses/Cousine-LICENSE.md) |
| `resources/midi/VintageDreamsWaves-v2.sf2` | Vintage Dreams Waves v2.0 by Ian Wilson, unmodified and embedded at build time, from [FluidSynth](https://github.com/FluidSynth/fluidsynth/tree/master/sf2) | Freeware, redistributed under its terms; see [VintageDreamsWaves-COPYRIGHT.txt](../licenses/VintageDreamsWaves-COPYRIGHT.txt) |

Build dependencies (SDL3, SDL3_image, SDL3_mixer, SDL3_ttf, SDL3_shadercross with DXC
and SPIRV-Cross, TinySoundFont, Assimp, GLM, spdlog, unordered_dense, ENet, miniz, Jolt Physics, Dear ImGui,
ImGuiColorTextEdit, ImGuizmo, nlohmann/json, OpenXR-SDK, and doctest for the tests) are
fetched at build time and carry their own licences; none of their source is vendored
here. Desktop builds ship the OpenXR loader with its OpenXR-SDK and jsoncpp notices.
MoltenVK and the headset runtime are installed separately.

Linux builds also use the system GStreamer libraries (LGPL); OpenDBC does not
bundle them or their codec plugins. See [video.md](video.md) for runtime dependencies.

OpenDBC binaries use FreeType, dr_mp3 and stb_image through the
statically linked SDL libraries. This software is based in part on the work of
the FreeType Team. Their notices are shipped with every installed product and
exported artifact.

## Example media

Every other media file under `examples/`, including `examples/dbpro/demos/`, is made by the
project and released under CC0 1.0.
