# Architecture

## Layout

```text
src/language/    profiles, lexer, parser, typed binder, catalogue (no engine, SDL or UI)
src/vm/          verified bytecode and cooperative VM
src/interpreter/ typed bridge from VM builtins to the engine
src/engine/      SDL_GPU renderer and runtime systems
shaders/         HLSL sources and their tracked SPIR-V and MSL
tools/studio/    the opendbc Studio and CLI
examples/        BASIC examples (see examples/README.md)
tests/           unit tests, BASIC goldens, qualification manifests
scripts/         generators, audits and their input data (scripts/data/)
```

## Pipeline

```text
source → lexer → AST → typed binder → bytecode → verifier → VM → builtin bridge → engine
```

- The profile is resolved before lexing and never changes during a run.
- The VM never reparses text and has no engine dependency; the builtin bridge is
  the only layer that knows both VM values and engine calls.
- `SYNC` and waits yield, so the Studio stays responsive and can stop a program.
- A program image (`.obc`, `src/interpreter/program_image.cpp`) stores the
  bytecode and project startup under the runtime fingerprint; loading it checks
  the container, then the verifier runs as for freshly compiled bytecode.
- `ExecutionService` turns the profile into one immutable
  `RuntimeCompatibilityPolicy`. Engine systems read that policy, never a file
  name or dialect string. Each run starts from a fresh policy.

## Catalogue

`src/language/language_catalogue.inc` is the only list of commands, functions,
statements and operators. It records spelling, kind, profiles, arities, typed
overloads, support state, stable `SymbolId` and provenance. The binder, verifier,
VM dispatch, Studio completion and generated references all read it.

Each callable has a per-profile state:

| State | Meaning |
| --- | --- |
| Implemented | Real behaviour, with a witness in `tests/unit/builtin_behavior_manifest.tsv` |
| Compatibility stub | Classic only: runs with a neutral result (no-op, `0`, `""`); permanent only for FTP, video, networking, DLLs and native pointers, CD audio, the registry and obsolete hardware switches |
| Unsupported | Rejected at bind time with `DBC2043` |

Shared VR forms and the Classic `X/Y/Z VECTOR3` readers are the only additions to
the historical profiles ([vr.md](vr.md)). Anything else new waits for the future
`DbcNew` / `DbProNew` profiles.

## Engine

`EngineKernel` owns audio, cameras, window, diagnostics, 2D, files, GPU device,
images, input, lights, matrices, DBPro BSP worlds, memblocks, objects, physics
(Jolt for the ODE core), renderer, sprites, text and VR. Engine shaders are HLSL
that the pinned DXC compiles to SPIR-V, then SDL_shadercross to MSL, as the Studio
does for effects; the engine loads whichever the GPU device accepts and never
transpiles at run time. After `cmake --preset shader-verify`, the
`shader-verify` build preset checks the tracked files byte for byte and
`shader-update` regenerates them. There is no video playback and no FFmpeg.

Most 3D geometry is transformed on the CPU into view-space triangles, rebuilt
only when the camera or an object changes. Terrains are uploaded once as static
GPU buffers; each frame only picks the visible blocks and their LOD, and the
vertex shader applies the view.

Profile differences the policy selects, among others:

| Classic | Professional |
| --- | --- |
| Polygon collision by default | Sphere collision by default |
| Image memblocks ignore alpha | Alpha kept |
| `HIDE LIMB` affects one limb | Propagates to child limbs |
| Screen text lasts one frame | Screen text stays until `CLS` |

Choices where the original cannot be reproduced portably:

- Fonts are Unicode, so the charset argument changes nothing.
- 16-bit display modes are emulated over RGBA32, with RGB565 bitmap memblocks.
- Static line of sight is exact; its accuracy argument is ignored.
- Native pointers never cross into the VM.
- No MIDI bank ships: a failed `.mid` load tries the same name as `.mp3`, then
  `.wav` (`scripts/generate_dbc_midi_fallbacks.py`).
- Missing force-feedback hardware makes force commands no-ops.

## Tests and generated files

`ctest --preset default` runs everything headless:

- unit tests (doctest);
- BASIC goldens in `tests/dba/` (`.dba` + `.expected`);
- example frames (`tests/qualification/example_frames.tsv`), which must not be
  black;
- audits and generator checks.

Checks whose result does not depend on the host carry the `portable` label:
`ctest -L portable` runs them alone and `ctest -LE portable` skips them.

Generated files are never edited by hand; each has a `--check` gate:

| File | Generator | Input |
| --- | --- | --- |
| `docs/reference/*.md` | `generate_language_references.py` | the catalogue, `scripts/data/language_catalogue_editorial.json` |
| `docs/api/` (HTML, not tracked) | `generate_api_docs.py` | the catalogue, `scripts/data/dbc_catalogue_evidence.tsv` |
| README metrics | `generate_readme_metrics.py` | `tests/unit/corpus_expectations.txt` |

`scripts/data/` also holds the audit inputs:

- per-symbol provenance for Classic, DBPro and the DBPro extensions;
- the digests of removed DarkBASIC files, which must never return;
- the MIDI fallback list and the DBPro project inventory.

The Classic examples are not redistributed. A maintainer who owns them can run
`scripts/qualify_dbc_examples.py --corpus-root DIR` locally (macOS Debug). A
passing run proves compilation and a bounded launch, not visual, audio or
hardware correctness.
