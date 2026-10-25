# Contributing to OpenDBC

## Build and test

```bash
cmake --preset default
cmake --build build
ctest --preset default
```

The test presets run headless: no window takes focus and audio is silent. A single
program can be checked the same way, saving its last frame:

```bash
build/tools/studio/opendbc.app/Contents/MacOS/opendbc run program.dba --dialect dbc --headless --max-frames 120 --capture-output frame.bmp
```

## Rules

- Code conventions, error handling, architecture constraints and the validation
  gates are described in [AGENTS.md](../AGENTS.md); they apply to everyone.
- Every change of behaviour comes with a regression test.
- Derived files (language references, corpus expectations, README metrics,
  fixtures, example media) are regenerated with their scripts in `scripts/`, never
  edited by hand.
- Contributions are released under the MIT Licence ([LICENSE](../LICENSE)).

## Content from DarkBASIC

- Do not add anything from the DarkBASIC Classic help, examples, demos, media or
  binaries, nor code from other The Game Creators products. Compatibility is
  rebuilt from behaviour, tests and the MIT DBPro sources only.
- Code or text taken from the [Dark-Basic-Pro](https://github.com/Dark-Basic-Software-Limited/Dark-Basic-Pro)
  repository keeps its MIT notice, listed in [NOTICE.md](NOTICE.md).
- Every media file added under `examples/` needs a row in `examples/ASSETS.tsv`
  with its digest, licence and source. `scripts/audit_example_assets.py`
  (CTest `example_asset_provenance`) checks it.
