# AGENTS.md

## Style and scope
- Write code, identifiers, comments, documentation and diagnostics in English.
- Follow existing C++20 conventions: namespace `dbc`, PascalCase identifiers, snake_case filenames.
- Format C++ with `.clang-format`; leave one blank line between function definitions, never glue them together.
- Read relevant code and reuse existing utilities. Prefer simple, explicit, lightweight code; avoid speculative abstractions and unnecessary dependencies.
- Keep changes focused and preserve unrelated user changes. Use RAII and clear ownership; avoid unnecessary work and allocations in hot paths.
- Comment non-obvious decisions, not self-explanatory code.

## Error handling
- Prefer `bool`, error enums or existing status types. Avoid `throw` and `try`/`catch`; isolate unavoidable third-party exceptions at integration boundaries.
- Use `assert` for internal invariants, never for expected failures or required side effects.
- Validate BASIC input, assets and device availability explicitly. Required error handling must work in Release builds.
- Use `DBC_LOG_*`. Do not swallow errors or flood logs every frame.

## Architecture and compatibility
- Keep **OpenDBC** as the product name. Preserve the single `opendbc` Studio/CLI binary and SDL_GPU architecture; exported games use a separate runner without Studio code. No public C++ command API.
- Keep `dbc_language` independent of engine, SDL and UI. Use the typed frontend and verified VM; never add text-execution fallbacks or bypass bytecode verification.
- `src/language/language_catalogue.inc` is the sole language inventory. Preserve stable `SymbolId`s, historical signatures and per-profile support states. Dispatch builtins by `SymbolId`.
- Resolve profiles before lexing and keep them immutable. Engine compatibility uses `RuntimeCompatibilityPolicy`, never filenames or textual dialect checks.
- Preserve historical Classic/Pro behavior. Classic is frozen and takes no additions; the VR forms and Vector3 readers documented in `docs/vr.md` are the only exception until they leave it. OpenDBC's additions go into the `dbpro` profile only, with no new profile: they change nothing for programs that do not call them and have multi-word names. Do not widen Pro while restoring Classic. Stubs are not implementations; unsupported forms keep explicit diagnostics.
- Features reach the engine through catalogue commands. The Studio writes those commands instead of sending settings to the runner another way; only asset generation and baking stay in the Studio, as files that a command loads.
- Keep engine HLSL shaders, CPU layouts, bindings and CMake contracts consistent. Preserve include/export path confinement and secret handling.

## Build and validation
- Use `cmake --preset default`, `cmake --build --preset default`, then `ctest --preset default`. The local Classic qualification (`scripts/qualify_dbc_examples.py --corpus-root`, corpus not redistributed) remains macOS Debug only; no clean-first, Release, sanitizers or packaging/mobile builds for that gate.
- Add regression tests for changed behavior. Catalogue edits require audits and regenerated references; implemented overloads need witnesses in `tests/unit/builtin_behavior_manifest.tsv`.
- Edit sources and regenerate derived files with existing tools. Never bypass validators to make a build pass.
- Consult `README.md` and relevant `docs/` pages as needed, not wholesale. Update affected documentation only; keep feature details and migration history out of `AGENTS.md`.
- Report what was tested and remains unverified. Compilation does not prove runtime, visual, audio or hardware correctness.

## Documentation
- Keep documentation compact and precise: one topic per file, stated once, no repetition across files.
- `README.md` holds one example, the build/run/test commands and links to `docs/`; details belong in `docs/`.
- Keep a single committed form of any data; do not add parallel JSON/CSV/XLSX exports.
- Audit and generator inputs live in `scripts/data/`, not in `docs/`.
- `docs/roadmap.md` lists decisions and remaining work only; remove items once done.

## Commits
- Commit each completed step separately, with a focused English message.
- Commit with the repository's configured Git identity (the maintainer). Never set another author and never add `Co-Authored-By` or other AI attribution trailers.
