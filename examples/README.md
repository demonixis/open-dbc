# Examples

Three source families, split by language profile and provenance. Every media file outside the imported `dbpro/tutorials/` and `dbpro/Hello World/` is listed in [`ASSETS.tsv`](ASSETS.tsv) with its licence and source.

| Folder | What it is | How to run |
| --- | --- | --- |
| [`dbc/`](dbc) | Programs for the DarkBASIC Classic profile written for OpenDBC: demos and tutorials. Their media are project-made (see `ASSETS.tsv`). | `opendbc run examples/dbc/demos/cavern_dash/cavern_dash.dba --dialect dbc` |
| [`dbpro/`](dbpro) | Programs for the DarkBASIC Professional profile: demo games written for OpenDBC (`demos/`, project-made media) and the help examples imported from the [Dark-Basic-Pro](https://github.com/Dark-Basic-Software-Limited/Dark-Basic-Pro) repository (`tutorials/`, `Hello World/`). Compiler, runtime and observable qualification are tracked separately for every tutorial project. | `opendbc run examples/dbpro/demos/dusk_rally/dusk_rally.dbpro` |
| [`open-dbc/`](open-dbc) | BASIC programs written for this engine in whichever historical profile their extension selects. | `opendbc run examples/open-dbc/interpreter_test.dba` |

`android/` contains the Gradle wrapper and the Android export template used by
the static BASIC artifact exporter. It is not a public source kit.

Each demo under `dbc/demos/` and `dbpro/demos/` keeps its source at the top of its
folder and its media in `textures/`, `models/` and `sounds/`; models name their
textures as `../textures/<file>`.

The runtime resolves relative media against the script's own directory, so run the
examples from wherever you like — `--asset-root` only matters when a script and
its media have been separated.

`tests/unit/corpus_expectations.txt` is the authoritative list of the compiler
stage each tracked source reaches. DBPro runtime dispositions and oracles live
in `tests/qualification/dbpro_demo_expectations.tsv`. A directory name or a
successful compiler stage alone never implies runtime support.
