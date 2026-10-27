# DarkBASIC Professional examples

`demos/` holds complete games written for OpenDBC; their media are project-made
(CC0, listed in `../ASSETS.tsv`, built by `assets-src/blender/` and
`scripts/generate_example_media.py`). Everything else in this folder is the
official help example import described below.

Imported from `Install/Help/examples` in the official
[Dark-Basic-Pro source snapshot](https://github.com/Dark-Basic-Software-Limited/Dark-Basic-Pro/tree/af6f19d7fc2919c356c683f7f5b8ec0c37a7d7d9/Install/Help/examples),
commit `af6f19d7fc2919c356c683f7f5b8ec0c37a7d7d9`, MIT-licensed,
© 1999-2015 The Game Creators Ltd. The pinned licence is preserved in
`licenses/Dark-Basic-Pro-LICENSE.md`.

Their `.dbpro` extension selects the Professional profile before lexing. No
source-content detector or flag is involved:

```bash
opendbc check examples/dbpro/tutorials/light/light1.dba.dbpro
```

## What was imported, and what was changed

All 32 tutorial projects are tracked, including the examples that intentionally
remain unsupported because they require video, FTP, native pointers, DLLs,
network multiplayer, BSP or legacy DirectX shader assembly. Keeping those
sources in the corpus makes their diagnostics and dispositions auditable rather
than silently omitting them. A separate Hello World project is also tracked.

Two edits were made to the originals:

- **BMP and TGA textures were re-encoded as PNG** wherever that made the file
  smaller, which cut the import from 51 MB to 40 MB. `.BMP`, `.TGA` and `.PNG`
  are all four characters, so the texture names inside the binary `.X` models
  could be patched in place without shifting a single offset.
- **`bitmap/2DFiles/` was left untouched.** That example exists to show one logo
  loading from seven different containers; converting them all to PNG would
  delete the point of it.

The imported source filenames were normalized from `*-example.DBA` to `.dbpro`
and line endings were normalized. Source references to converted textures were
changed from BMP/TGA to PNG. `core/core1.dba` also contains an encoding repair
for two numeric-range comments.

Everything else — models, sounds, the MP3 soundtracks, the AVI — is byte for
byte what TGC shipped. A handful of textures (`ALIEN3.BMP`, `earth.bmp`,
`TILEA.BMP`, `SB1..SB6.BMP`) are named by models that ship without them; they
were already missing upstream, and those surfaces render untextured.

## Status

Every imported example is checked through `parse → bind → lower → verify` under
the immutable DBPro profile. The exact expected stage and diagnostic codes are
tracked in `tests/unit/corpus_expectations.txt`; a source may not silently drop
out of that gate. Official commands that OpenDBC does not implement remain in
the canonical catalogue as `Unsupported` and fail binding with `DBC2043`.

`tests/qualification/dbpro_demo_expectations.tsv` is the current inventory of
all 32 tutorial projects. It keeps compiler stage, runtime route, disposition,
interaction and observable oracle separate. Validate the inventory with:

```bash
python3 scripts/qualify_dbpro_examples.py --inventory-only
```

`system/system1.dba` was rewritten for OpenDBC: the original demonstrated a
Windows-only transition DLL and carried an all-rights-reserved header, so the
new program shows the same idea (screen transitions) in plain BASIC.

This document intentionally makes no broad “runs” claim. A program is only
claimed executable when it reaches verified bytecode and its observable runtime
behavior passes the qualification manifest.

The non-target dispositions are fixed policy, not temporary test skips:

| Disposition | Tutorial projects | Contract |
| --- | --- | --- |
| `Deferred` | `animation1`, `multiplayer1` | Video playback and network multiplayer stay catalogued but have no handler until a separately approved portable backend exists. |
| `Never` | `3dmaths1`, `basic2d1`, `basic3d2`, `basic3d5`, `basic3d8`, `display1`, `ftp1` | FTP, DLL/native-pointer escape and DirectX shader assembly never receive a silent compatibility shim; compilation or runtime emits the tracked diagnostic. |
| `Study` | `world1` | The GameFX BSP surface remains blocked pending a geometry and collision design with real behavior. |
