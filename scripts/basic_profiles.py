from pathlib import Path


def load_test_profiles(root: Path) -> dict[str, str]:
    manifest = root / "tests/dba/test_profiles.tsv"
    result: dict[str, str] = {}
    for raw in manifest.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 2 or fields[1] not in {"dbc", "dbpro"}:
            raise SystemExit(f"invalid BASIC profile row: {raw}")
        if fields[0] in result:
            raise SystemExit(f"duplicate BASIC profile source: {fields[0]}")
        result[fields[0]] = fields[1]
    sources = {path.name for path in (root / "tests/dba").glob("test_*.dba")}
    if sources != set(result):
        missing = sorted(sources - set(result))
        stale = sorted(set(result) - sources)
        raise SystemExit(f"test_profiles.tsv mismatch; missing={missing}, stale={stale}")
    return result


def load_corpus_profiles(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.is_file():
        return result
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("|")
        if len(fields) != 4 or fields[1] not in {"dbc", "dbpro"}:
            raise SystemExit(f"invalid corpus profile row: {raw}")
        result[fields[0]] = fields[1]
    return result
