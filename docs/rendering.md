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
   terrains left on the CPU, the BSP world, objects and particles. For each object
   triangle it skins the vertices, transforms them to world then view space, culls
   back faces, resolves textures and the coordinates of the eight texture stages
   (sphere and reflection mapping and the built-in effects included), and clips
   the result to the frustum into 624-byte `Triangle` records. The records are
   sorted one by one: very early first, then opaque front to back, then
   transparent and additive back to front, render priority first within each.
   Reflection shading gathers the scene again with the mirrored view; stencil
   shadow volumes are extruded on the CPU from the casters' triangles.
3. The sorted triangles become 164-byte `GpuVertex` records. Consecutive
   triangles that share textures, blending, depth state and `SurfaceStyle` merge
   into one `GpuBatch`. The 2D layer follows: bitmap 0 as one quad, text,
   primitives, pasted images and sprites.
4. `UploadPreparedViews` copies the vertices of all views into one vertex buffer.
5. `RecordPreparedView` draws the batches in passes: backdrop, 2D under the scene,
   opaque, transparent then additive pipelines, the reflection mask and reflected
   scene, shadow volumes and their overlay, 2D over the scene. Each batch is one
   draw call that binds nine textures and pushes its texture-stage uniforms.
6. `FinalizeFrame` records `STATISTIC(1)` (triangles) and `STATISTIC(5)`
   (batches), then clears bitmap 0 after a 3D frame.

A view's triangle list is cached while the camera, every object, the visible
particle emitters and the animations stay still. Any change, or a second view,
rebuilds it whole.

Terrains are the exception. Their world-space vertices are uploaded once; each
view picks the visible 16-cell blocks and their LOD step and draws them indexed,
and the vertex shader applies the view through the `ModelView` uniform. Ghosted,
transparent and unculled terrains stay on the CPU path.

## Where frame time goes

CPU time grows with visible triangles, not objects: per-triangle transforms and
texture-stage lookups, the sort and copies of `Triangle` records, then the
conversion and upload of `GpuVertex` records. The VM is a small share of a 3D
frame. Sorting opaque triangles by depth interleaves objects, so consecutive
triangles rarely share a texture and batches stay a few triangles long. The clear
after each 3D frame destroys bitmap 0's GPU texture; the next frame creates and
uploads a full-window texture again, even when nothing was drawn on it.

## Compatibility constraints

A faster path keeps what programs can observe:

- Draw order where depth does not decide it: objects under `DISABLE OBJECT
  ZDEPTH`, `ZREAD` or `ZWRITE`, transparency mode 7 (very early), render
  priorities, and blended or additive surfaces, which are sorted back to front
  per triangle. Sorting them per object changes how intersecting ones overlap.
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

writes frame times and both statistics. Headless frames ignore `SYNC RATE`, so
frame times are the real cost. Measure Release builds: this path runs about ten
times slower in Debug. On macOS, `sample <pid> 5` or Instruments attribute CPU
time to functions; GPU time needs a Metal capture, PIX or RenderDoc.
