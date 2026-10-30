# Rendering

The engine draws through SDL_GPU with one HLSL shader, `shaders/dbclassic_tri.hlsl`,
a depth-only one for shadow volumes, `shaders/shadow_volume.hlsl`, and the pipelines
of loaded effects. Opaque object meshes and terrains draw from
GPU buffers; the rest of the 3D geometry is still built on the CPU every frame.
[roadmap.md](roadmap.md#rendering-performance) lists the work that moves it to the
GPU.

## A frame

`SYNC` renders every view: each DBPro camera, camera 0 first and the others by
number, or both eyes in VR ([vr.md](vr.md)). The 2D layer is drawn once, in screen
coordinates, under all cameras after `DRAW TO BACK` and over them otherwise.

`RendererSystem` splits a frame into steps:

1. `PrepareFrame` advances animations and particles and refreshes the text atlas.
2. `BuildPreparedView`, once per view, gathers world triangles from matrices,
   terrains left on the CPU, the BSP world, objects and particles. A mesh that the
   GPU can draw (below) becomes a `GpuMeshDraw` instead. Any other object mesh
   skins and transforms each vertex once, to world then view space, and resolves
   its textures and style on its first visible triangle. Each triangle is culled
   if it faces away; its corners take their normal, colour and the coordinates of
   the eight texture stages (sphere and reflection mapping and the built-in
   effects included), once per vertex when the mesh has normals. It is clipped to
   the frustum into 624-byte `Triangle` records, each with a hash of its draw
   state. `OrderWorldTriangles` sorts packed keys, never the records: very early
   first, then opaque, then transparent and additive back to front, render
   priority first within each phase. Opaque triangles go front to back, except
   that each run of triangles that are depth tested and written and not blended
   is grouped by draw-state hash; the others keep their place between the runs.
   Reflection shading gathers the scene again with the mirrored view.
3. The ordered triangles become 164-byte `GpuVertex` records. Consecutive
   triangles that share textures, blending, depth state and `SurfaceStyle` merge
   into one `GpuBatch`. Static terrains and GPU meshes take the place of their CPU
   triangles: terrains at render priority −1 (1 when drawn after the objects),
   meshes at 0, each after the very early phase and lower priorities, before
   higher ones (particles, bounds) and transparency. The 2D layer follows: bitmap 0 as one quad when it
   shows anything, text, primitives, pasted images and sprites.
4. `UploadPreparedViews` copies the vertices of all views into one vertex buffer,
   each view's GPU mesh instance records after its vertices.
5. `RecordPreparedView` draws the batches in passes: backdrop, 2D under the scene,
   opaque, transparent then additive pipelines, the reflection mask and reflected
   scene, shadow volumes and their overlay, 2D over the scene. Each batch is one
   draw call that binds nine textures and pushes its texture-stage uniforms; a GPU
   mesh batch also binds its buffers and draws every instance of its group. All
   shadow volumes make one draw call that binds no texture.
6. `FinalizeFrame` records `STATISTIC(1)` (triangles) and `STATISTIC(5)`
   (batches), then clears bitmap 0 after a 3D frame.

A view's triangle lists are cached while the camera, every object, the visible
particle emitters and the animations stay still and no image texture is released,
and shared with the view without a copy. Any change, or a second view, rebuilds
them whole.

Bitmap 0, the screen's 2D layer, keeps one GPU texture. Every write to a bitmap
reports the rectangle it changed; before the layer is drawn, the texture receives
those rectangles, up to eight kept apart, and the clear after a 3D frame erases
only what was drawn since the previous one.

Terrains are the exception. Their world-space vertices are uploaded once; each
view picks the visible 16-cell blocks and their LOD step and draws them indexed,
and the vertex shader applies the view through the `ModelView` uniform. Ghosted,
transparent and unculled terrains stay on the CPU path.

Object meshes take the same route where the result cannot change. A mesh draws from
GPU buffers when its object is opaque (not ghosted, faded by alpha, alpha-mapped,
black-keyed or given a transparency mode), depth tested and written, and neither
locked, glued nor reflecting, and when the mesh has no bones, built-in
effect, effect, auxiliary or cube stage, texture-coordinate mode or zero-length
vertex normal. Its vertices and indices are uploaded once per `MeshGeometry`
revision and released when that geometry changes or goes 300 frames undrawn.
Clones, instances, the `MAKE OBJECT` primitives made with the same parameters and
the meshes without bones of objects loaded from the same file share one geometry
until one of them is edited. Each object brings a 176-byte instance record:
its world and normal matrices, colour, material, texture scale and scroll, fade and
lighting, which `VSMesh`, the engine shader's second vertex entry, reads at
instance rate; the GPU culls back faces. Draws of one geometry with one texture and
`SurfaceStyle` make one instanced draw call: their records are sorted together and
bound at the group's first, so `first_instance` stays zero. A mesh without vertex
normals is uploaded with one vertex per triangle corner and that
triangle's normal, which the inverse transpose of the world matrix turns into the
world face normal the CPU path would use. While reflection shading is on, or an
opaque object depends on the depth order (no depth test or write, alpha-tested
transparency modes), every mesh keeps the CPU path.

Stencil shadow volumes are extruded on the CPU from each caster's whole mesh, since a
caster out of frame or turned away from the camera still shadows what is in frame.
The caster's lit faces make the two caps and the edges they do not share make the
sides, found once per `MeshGeometry` revision. A volume is kept in world space, so
cameras and views share it: it is extruded again only when its caster moves, is
edited, is animated or is glued to another object, or when the light moves. Any
change uploads the volumes of all casters again, as positions and indices, and
`shaders/shadow_volume.hlsl` draws them with no colour, varyings or textures. An
object locked to the screen casts only from a shadow mesh of its own.

## Where frame time goes

On the CPU path, time grows with visible triangles, not objects: clipping and
filling `Triangle` records, ordering them, then converting and uploading
`GpuVertex` records. A group of GPU meshes costs one draw call whatever its size and
count, but distinct meshes cost one each, so with many of them recording the draws
takes most of the frame. The VM is a small share of a 3D frame. Shadow volumes cost
the CPU only on frames where a caster or the light changes: those casters are
extruded again and all volumes uploaded.

## Compatibility constraints

A faster path keeps what programs can observe:

- Draw order where depth does not decide it: objects under `DISABLE OBJECT
  ZDEPTH`, `ZREAD` or `ZWRITE`, transparency mode 7 (very early), render
  priorities, and blended or additive surfaces, which are sorted back to front
  per triangle. Sorting them per object changes how intersecting ones overlap.
  Among the grouped opaque surfaces, only coplanar ones of different draw states
  depend on the grouping order.
- Transparent objects below a transparency-mode-5 surface sort as if they were
  at the far plane.
- Materials travel in vertex attributes: ambient, specular, emissive, alpha
  override and alpha-test threshold. `FADE OBJECT` scales the normal's length,
  which the shader reads as a light multiplier; unlit surfaces have a zero normal.
- Vertex alpha only counts for objects that blend or alpha-test.
- `STATISTIC(1)` counts the triangles sent, back faces of GPU meshes included,
  and `STATISTIC(5)` the batches drawn.
- `LOCK OBJECT ON` objects are projected on the CPU into the 2D overlay.
- Queries such as `LIMB POSITION X`, vertex data and collision transform on the
  CPU in `ObjectManager`, independently of drawing.

SDL_GPU bounds the GPU side. It has no bindless textures, so a draw binds one set
of textures, and no GPU timer queries. Shader built-ins such as `SV_InstanceID`
are not portable with a non-zero `first_instance`, and indirect draws need
`first_instance` at zero on devices without `drawIndirectFirstInstance`.

## Measuring

```text
opendbc run game.dba --headless --max-frames 300 --benchmark --benchmark-output bench.json
```

writes frame times, the last frame's counts (triangles, draw calls, vertices,
views, objects, those shown and those culled) and each renderer step's mean time:
build, order, convert, upload and record. Headless frames ignore `SYNC RATE`, so
frame times are the real cost. Measure Release builds: this path runs about ten
times slower in Debug.

`scripts/benchmark_renderer.py run` measures every program of
`tests/benchmarks/benchmarks.tsv`, the demos and the stress scenes beside it (one
shared mesh, many distinct meshes, transparency, skinned characters, two views,
shadow casters), and keeps each one's last frame; `compare` puts two builds side by side, pixels
included. The stress scenes advance a fixed step per frame, so they draw the same
frames on every run.

On macOS, `sample <pid> 5` or Instruments attribute CPU time to functions; GPU
time needs a Metal capture, PIX or RenderDoc.
