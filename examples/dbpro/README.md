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

## What was changed

All 32 tutorial projects and a Hello World project are tracked, including those
that stay unsupported, so that their diagnostics can be audited.

- BMP and TGA textures were re-encoded as PNG where that made them smaller, which
  cut the import from 51 MB to 40 MB, and the names inside binary `.X` models were
  patched in place. `bitmap/2DFiles/` is untouched: it shows one logo loading from
  seven containers.
- Source file names went from `*-example.DBA` to `.dbpro`, line endings were
  normalized, and sources name the converted textures. `core/core1.dba` has an
  encoding repair in two comments.
- `system/system1.dba` was rewritten for OpenDBC: the original demonstrated a
  Windows-only transition DLL and carried an all-rights-reserved header, so the new
  program shows screen transitions in plain BASIC.

Everything else is byte for byte what TGC shipped. A few textures (`ALIEN3.BMP`,
`earth.bmp`, `TILEA.BMP`, `SB1..SB6.BMP`) are named by models that ship without
them, as upstream, and those surfaces render untextured.

## Status

`tests/unit/corpus_expectations.txt` tracks the compiler stage and diagnostics each
source reaches, under the DBPro profile. `tests/qualification/dbpro_demo_expectations.tsv`
adds each tutorial's runtime route, disposition, interaction and observable oracle;
a program is only claimed to run when it passes there. Check it with:

```bash
python3 scripts/qualify_dbpro_examples.py --inventory-only
```

Commands OpenDBC does not implement fail binding with `DBC2043`. The projects that
stay out are fixed policy:

| Disposition | Tutorial projects | Why |
| --- | --- | --- |
| `Deferred` | `animation1`, `multiplayer1` | Video playback and network multiplayer exist for Classic only |
| `Never` | `3dmaths1`, `basic2d1`, `basic3d2`, `basic3d5`, `basic3d8`, `display1`, `ftp1` | FTP, DLLs and native pointers, DirectX shader assembly |
