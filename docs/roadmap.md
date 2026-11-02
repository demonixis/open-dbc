# Roadmap

## Before 0.5

Repository files:
- [ ] README screenshots.

Publication:
- [ ] Signed tag `v0.5.0`, release with `SHA256SUMS`.

## Code quality

- [ ] Code quality audit.
- [ ] Architecture audit.

## DBPro parity

The catalogue counts 1,058 of DBPro's 1,266 core commands and functions as implemented,
against 702 of 731 in Classic. The other 208 are sorted here by name only; confirm each
one when it is picked up.

152 stay `Unsupported`: DirectX 9 capability queries, Windows, disc and registry access
(89); raw memory and pointers, assembly shaders and DLLs (42); FTP and HTTP without TLS
(21).

To rethink rather than port:

- [ ] Static objects, portals and lightmaps (31 forms).
- [ ] CSG (7 forms).

Validation and scope:

- [ ] More DBPro demos and qualified programs: 3 demos and 38 programs, against 6 and
  57 in Classic.

## Modern features

DBPro additions for after 0.5. Rendering comes in dependency order; the Studio and
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
- [ ] Baked lightmaps, reflection and light probes, from one pipeline: the runner bakes
  them without a window into files that commands load, with HDR cube textures for the
  probes.
- [ ] SSR when a game calls for it: normals and roughness in every pipeline, effects
  included.
- [ ] TAA when a game calls for it: the CPU path has no previous-frame positions, so
  GPU skinning comes first.
- [ ] Share the lighting and tone mapping HLSL with the Scene view's own renderer.
- [ ] GPU cost baselines from Metal captures or RenderDoc: SDL_GPU has no timer queries
  and `--benchmark` measures only the CPU.

Studio:

- [ ] Debugger, as the DBPro IDE had: breakpoints, stepping and variable inspection, with
  the program running in its child process.
- [ ] Normal maps generated from existing textures, then full PBR sets by heuristics
  with sliders: metalness cannot be derived from an albedo. No ML dependency.
- [ ] 2D view for UI and HUD, written and read back as BASIC like levels, over a BASIC
  library using `TYPE` and `#CONSTANT`. The language has no callbacks, so an update
  function returns the widget clicked. Check the 2D commands first for nine-slice and
  clipping. Tilemaps come after the UI.
- [ ] Split each generated files into specific folders. 
  - [ ] Levels folder for levels: Contains the level and the world settings, two files
  - [ ] UI folder for UI: UI are not called directly in the code, the editor is just a designer, similar to the Widget Blueprint Editor in Unreal

Gameplay:

- [ ] More physics on Jolt ([physics.md](physics.md)): commands for what `PhysicsSystem`
  already does (torque, impulses, kinematic bodies, damping, contact points), then
  vehicles, ragdolls, heightfield terrains and the character's water line. Decide what
  replaces the ODE terrain forms, which take a memory pointer.
- [ ] Animation clips: cross-fades and blending between them.
- [ ] Navigation: pathfinding over data baked in the Scene view. Weigh a dependency such
  as Recast against a grid in a BASIC library.

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

## Multiplayer

Net games run over ENet ([network.md](network.md)).

- [ ] Host migration for peer-to-peer games, with `NET GAME NOW HOSTING()`.
- [ ] Platform permissions: iOS local network description and multicast
  entitlement for discovery, Android multicast lock, macOS sandbox network
  entitlements.

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

## Towards 1.0

- [ ] Remove the `DbcNew` / `DbProNew` remnants: the reserved touch, orientation and
  accelerometer commands become DBPro additions or go away; update the editorial data
  and the generators.
- [ ] Studio: embedded help, installed samples, welcome screen.
- [ ] Split the largest source files; measure coverage; negative verifier tests.
- [ ] One policy for missing objects, documented integer semantics, deterministic `RND`.
