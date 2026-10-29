# Rendering

The engine draws through SDL_GPU with one HLSL shader, `shaders/dbclassic_tri.hlsl`,
plus the pipelines of loaded effects. Most 3D geometry is still built on the CPU
every frame; [roadmap.md](roadmap.md#rendering-performance) lists the work that
moves it to the GPU.

## A frame

`SYNC` renders every view: each DBPro camera, camera 0 first and the others by
number, or both eyes in VR ([vr.md](vr.md)). The 2D layer is drawn once, in screen
coordinates, under all cameras after `DRAW TO BACK` and over them otherwise.

`RendererSystem` splits a frame into steps:

1. `PrepareFrame` advances animations and particles and refreshes the text atlas.
2. `BuildPreparedView`, once per view, gathers world triangles from matrices,
   terrains left on the CPU, the BSP world, objects and particles. An object mesh
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
   Reflection shading gathers the scene again with the mirrored view; stencil
   shadow volumes are extruded on the CPU from the casters' triangles.
3. The ordered triangles become 164-byte `GpuVertex` records. Consecutive
   triangles that share textures, blending, depth state and `SurfaceStyle` merge
   into one `GpuBatch`. The 2D layer follows: bitmap 0 as one quad when it
   shows anything, text, primitives, pasted images and sprites.
4. `UploadPreparedViews` copies the vertices of all views into one vertex buffer.
5. `RecordPreparedView` draws the batches in passes: backdrop, 2D under the scene,
   opaque, transparent then additive pipelines, the reflection mask and reflected
   scene, shadow volumes and their overlay, 2D over the scene. Each batch is one
   draw call that binds nine textures and pushes its texture-stage uniforms.
6. `FinalizeFrame` records `STATISTIC(1)` (triangles) and `STATISTIC(5)`
   (batches), then clears bitmap 0 after a 3D frame.

A view's triangle lists are cached while the camera, every object, the visible
particle emitters and the animations stay still, and shared with the view
without a copy. Any change, or a second view, rebuilds them whole.

Bitmap 0, the screen's 2D layer, keeps one GPU texture. Every write to a bitmap
reports the rectangle it changed; before the layer is drawn, the texture receives
those rectangles, up to eight kept apart, and the clear after a 3D frame erases
only what was drawn since the previous one.

Terrains are the exception. Their world-space vertices are uploaded once; each
view picks the visible 16-cell blocks and their LOD step and draws them indexed,
and the vertex shader applies the view through the `ModelView` uniform. Ghosted,
transparent and unculled terrains stay on the CPU path.

## Where frame time goes

CPU time grows with visible triangles, not objects: clipping and filling
`Triangle` records, ordering them, then converting and uploading `GpuVertex`
records. The VM is a small share of a 3D frame.

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
- `STATISTIC(1)` counts the triangles sent and `STATISTIC(5)` the batches drawn.
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
shared mesh, many distinct meshes, transparency, skinned characters, two views),
and keeps each one's last frame; `compare` puts two builds side by side, pixels
included. The stress scenes advance a fixed step per frame, so they draw the same
frames on every run.

On macOS, `sample <pid> 5` or Instruments attribute CPU time to functions; GPU
time needs a Metal capture, PIX or RenderDoc.
