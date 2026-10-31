# Roadmap

## Decisions

- The public repository is `demonixis/open-dbc`; its history starts from a single initial import.
- Classic is frozen: it keeps running historical programs on every target, mobile
  included, and takes no new commands. DBPro is the language that evolves: OpenDBC's
  additions go into the `dbpro` profile, with no third profile (`DbcNew`, `DbProNew`
  or a hybrid).
- An addition changes nothing for programs that do not call it, pixels included, and
  has a name of several words.
- Features reach the engine through commands. The Studio writes those commands
  (`SetupGame()`, levels) rather than sending settings to the runner another way, so
  the CLI, exports and in-game options menus reach them too. Only asset generation
  and baking stay in the Studio; what they produce is a file that a command loads.
- New features go into the engine, not into native plugins: a plugin needs a binary
  and a signature per target, cannot reach the renderer and escapes the typed
  catalogue. `LOAD DLL` stays for Classic programs.
- The Scene view and the future 2D view are for DBPro only; levels already written
  for Classic are BASIC and keep running. New projects default to DBPro.
- Signing OpenDBC's own releases (Apple notarisation, Windows) is done later, in one batch with
  openblood2 and oxrsys. Exported games are signed by the export ([usage.md](usage.md#export)).
- Yannick Comte owns the code. No DarkBASIC Classic content is published or
  linked; reused DBPro code comes from the MIT repository and keeps its notice.
- Project assets are CC0. Version 0.4 ships with the current examples: six demos
  and tutorials covering the 747 Classic help symbols.
- macOS releases target Apple Silicon only, exported games included: one binary per
  target, no universal build.
- No SBOM, `CODEOWNERS` or Dependabot: one maintainer, dependencies pinned in
  `cmake/fetch_dependency.cmake`, no CI. AppStream and Flatpak wait until someone
  asks for them.

## Before 0.4

Language:

- [ ] Remove the VR forms and the `X/Y/Z VECTOR3` readers from Classic; `vr_tracking.dba`
  moves to DBPro.
- [ ] Reserve the Scene view for DBPro.
- [ ] Port to DBPro the common commands only Classic implements, such as `WAIT`,
  `SET OBJECT FRAME`, `APPEND OBJECT` and `SCROLL OBJECT TEXTURE`.
- [ ] Then default new projects to DBPro.

Build and release:

- [ ] Linux AppImage on demand, built on an old glibc so that it starts on older
  distributions, with the Studio's `.desktop` entry.

Validation:

- [ ] Run the GPU mesh path on Vulkan, on Windows and Linux: `VSMesh` draws nearly
  every opaque object and has only run on Metal.
- [ ] Play the demos by hand, and export a game end to end with the new shader stage,
  on each announced target.

Robustness:

- [ ] Check on Windows that a Studio run opens no console window beside the program:
  `opendbc-runner` is a console executable and SDL only hides consoles for
  background processes.

Repository files:

- [ ] Issue and PR templates. No CI workflow for now: it took too many iterations to
  get working.
- [ ] README screenshots.

Publication:

- [ ] Signed tag `v0.4.0`, release with `SHA256SUMS`; its notes say that multiplayer
  and animations are still Classic only.

## Code quality

- [ ] Code quality audit.
- [ ] Architecture audit.

## DBPro parity

The catalogue counts 923 of DBPro's 1,264 core commands and functions as implemented,
against 719 of 748 in Classic; 116 exist in both profiles and run only in Classic.

- [ ] Audit the 341 missing forms: obsolete, covered by another form, or to port, such
  as `SET CAMERA TO IMAGE`.
- [ ] Multiplayer ([network.md](network.md)) and animations ([video.md](video.md)) in
  DBPro. DLL commands stay unsupported there.
- [ ] More DBPro demos and qualified programs: 3 demos and 38 programs, against 6 and
  57 in Classic.
- [ ] Decide whether to remove the DBPro extensions from the catalogue, with their
  reference and audits: 1,270 commands and functions, some redundant, of which only
  20 ODE ones run (on Jolt). OpenDBC's own commands would replace them.

## Modern features

DBPro additions for after 0.4. Rendering comes in dependency order; the Studio and
network work does not depend on it.

Rendering:

- [ ] HDR target, opt-in per program: linear output where the shader encodes gamma
  today, the 2D layer drawn after tone mapping, depth that can be sampled.
- [ ] Tone mapping, FXAA and a procedural sky.
- [ ] Materials and PBR: linear textures (every image is sRGB today), tangents derived
  in the fragment shader, glTF materials on import, lighting from the sky.
- [ ] Shadow maps alongside PBR: stencil volumes darken surfaces by a fixed 25 % and
  keep casters on the CPU path.
- [ ] GTAO with normals rebuilt from depth, SMAA, and MSAA for VR.
- [ ] Baked reflection and light probes: HDR cube textures, baked by the runner without
  a window and loaded by a command.
- [ ] SSR when a game calls for it: normals and roughness in every pipeline, effects
  included.
- [ ] TAA when a game calls for it: the CPU path has no previous-frame positions, so
  GPU skinning comes first.
- [ ] Share the lighting and tone mapping HLSL with the Scene view's own renderer.
- [ ] GPU cost baselines from Metal captures or RenderDoc: SDL_GPU has no timer queries
  and `--benchmark` measures only the CPU.

Studio:

- [ ] Normal maps generated from existing textures, then full PBR sets by heuristics
  with sliders: metalness cannot be derived from an albedo. No ML dependency.
- [ ] 2D view for UI and HUD, written and read back as BASIC like levels, over a BASIC
  library using `TYPE` and `#CONSTANT`. The language has no callbacks, so an update
  function returns the widget clicked. Check the 2D commands first for nine-slice and
  clipping.

Network:

- [ ] Packets of several values and an unreliable channel: each integer, float or string
  message is one value today, reliable and ordered.
- [ ] A replication library in BASIC.
- [ ] Direct UDP and a headless dedicated server; relays, HTTP and TLS wait until a game
  needs them.

## Rendering performance

Frozen: the heaviest demo, `wave_racer`, takes 3.1 ms per frame, a fifth of the
budget at 60 fps. The work below reopens only when a real program exceeds 8 ms per
frame on a target machine, [measured](rendering.md#measuring).

Decisions:

- One build of the world serves every view; cameras and VR eyes change uniforms only.
- GPU culling with compute and indirect draws waits until CPU culling measures as
  a bottleneck.
- No static batching while draw calls stay cheap (at most 0.16 ms per frame in the
  scenes measured). If thousands of distinct static meshes ever call for it,
  merging their geometry is the portable answer: SDL_GPU cannot give each draw of
  a multi-draw indirect call its own instance offset.

Work, in order, when a measurement calls for it:

- [ ] GPU skinning, the only wall left: about 50,000 skinned triangles fill the
  budget. Bone indices and weights in the mesh's vertex buffer, the object's bone
  palette in a vertex storage buffer, positions and normals skinned in the vertex
  shader. Queries, collision and shadow volumes keep skinning on the CPU from the
  same palette.
- [ ] Reuse each view's buffers: its vertices are reallocated every frame, and a
  second camera or VR eye allocates a whole scene (about 11 MB reserved).
- [ ] Update GPU mesh draws per object: one object that moves rebuilds them all, and
  20,000 painted instances would take about 3 ms to walk.
- [ ] GPU meshes for what still takes the CPU path: ghosted objects, auxiliary stages
  and built-in effects in the vertex shader, with vertex formats sized to the stages
  a mesh uses; locked and glued objects; matrices; reflection passes.
- [ ] Frustum culling and the other visibility tests: per limb, bounds centred on
  the mesh, a spatial grid for large scenes, shadow casters, then occlusion if
  measurements call for it.

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

- [ ] Studio packaging with the runner: `Contents/Helpers/opendbc-runner.app` inside
  the macOS bundle, signed inside out; sibling executables in a shared folder on
  Windows and Linux.

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
  volumes against room geometry.
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
- [ ] Interactive UI coverage: `test_studio_scene_view` drives gizmo moves and
  snapping with simulated input; navigation, drag and drop, the other gizmos and
  the Inspector are checked by hand only.
- [ ] Remove the Classic branches and tests once the view is reserved for DBPro.

## Towards 1.0

- [ ] Remove the `DbcNew` / `DbProNew` remnants: the reserved touch, orientation and
  accelerometer commands become DBPro additions or go away; update the editorial data
  and the generators.
- [ ] Studio: embedded help, installed samples, welcome screen.
- [ ] Split the largest source files; measure coverage; negative verifier tests.
- [ ] One policy for missing objects, documented integer semantics, deterministic `RND`.
