#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    (ROOT / "tests/dba/test_strings.dba", "dbc", "DBC"),
    (ROOT / "tests/dba/test_constants.dba", "dbpro", "DBPro"),
)
REFERENCE_SOURCES = tuple(
    ROOT / "docs/reference" / Name
    for Name in ("darkbasic_reference.md", "dbpro_reference.md", "open-dbc_reference.md")
)
OPENXR_LOADER_PREFIXES = ("libopenxr_loader", "openxr_loader")
SHADER_NAMES = tuple(
    f"{shader}.{stage}.{format}"
    for shader in ("dbclassic_tri", "vr_transfer")
    for format in ("spv", "msl")
    for stage in ("vert", "frag")
)
REQUIRED_LICENSES = ("LICENSE", "Dark-Basic-Pro-LICENSE.md", "MuseScore-General-SoundFont-LICENSE.md", "RapidJSON.txt")


def execute(command: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    print("+", subprocess.list2cmdline(command), flush=True)
    completed = subprocess.run(command, cwd=cwd, text=True, check=False, capture_output=True)
    if completed.returncode != 0:
        if completed.stdout:
            print(completed.stdout, file=sys.stderr, end="")
        if completed.stderr:
            print(completed.stderr, file=sys.stderr, end="")
        raise SystemExit(f"command failed with exit code {completed.returncode}")
    return completed


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def installed_layout(destination: Path) -> tuple[Path, Path]:
    if sys.platform == "darwin":
        entries = list(destination.iterdir())
        require(len(entries) == 1 and entries[0].suffix == ".app",
                "the Runtime component must install exactly one application bundle")
        app = entries[0]
        return app / "Contents/MacOS/opendbc", app / "Contents/Resources"

    require({path.name for path in destination.iterdir()} == {"bin"},
            "the Runtime component must install only bin/")
    executable = "opendbc.exe" if os.name == "nt" else "opendbc"
    return destination / "bin" / executable, destination / "bin"


def verify_shaders(directory: Path) -> None:
    for name in SHADER_NAMES:
        installed = directory / name
        source = ROOT / "shaders" / name
        require(installed.is_file(), f"installed shader is missing: {installed}")
        require(installed.read_bytes() == source.read_bytes(), f"installed shader differs: {installed}")


def verify_resources(resources: Path) -> None:
    for source in REFERENCE_SOURCES:
        name = source.name
        installed = resources / name
        require(installed.is_file(), f"installed reference is missing: {installed}")
        require(installed.read_bytes() == source.read_bytes(), f"installed reference differs: {name}")
    verify_shaders(resources / "shaders")
    require((resources / "NOTICE.md").is_file(), "installed NOTICE.md is missing")
    runtime_manifest = resources / "opendbc-runtime.json"
    require(runtime_manifest.is_file(), "installed runtime manifest is missing")
    json.loads(runtime_manifest.read_text(encoding="utf-8"))
    for name in REQUIRED_LICENSES:
        require((resources / "licenses" / name).is_file(), f"installed license is missing: {name}")


def verify_export_hosts(resources: Path) -> None:
    ios = resources / "export-hosts/ios"
    simulator = resources / "export-hosts/ios-simulator"
    android = resources / "export-hosts/android/template"
    required = (
        ios / "OpenDBC.app/opendbc-runner",
        ios / "opendbc-host.json",
        simulator / "OpenDBC.app/opendbc-runner",
        simulator / "opendbc-host.json",
        android / "app/src/main/jniLibs/arm64-v8a/libmain.so",
        android / "app/src/main/assets/opendbc-host.json",
        android / "gradle/wrapper/gradle-wrapper.properties",
        android / "gradlew",
    )
    for installed in required:
        require(installed.is_file(), f"installed export host input is missing: {installed}")
    for directory in (ios / "OpenDBC.app/shaders", simulator / "OpenDBC.app/shaders",
                      android / "app/src/main/assets/shaders"):
        verify_shaders(directory)
    for manifest in (ios / "opendbc-host.json", simulator / "opendbc-host.json",
                     android / "app/src/main/assets/opendbc-host.json"):
        json.loads(manifest.read_text(encoding="utf-8"))
    android_build = (android / "app/build.gradle").read_text(encoding="utf-8")
    require("compileSdk 36" in android_build,
            "installed Android export template does not compile against API 36")


def verify_relocation(destination: Path, executable: Path, build_dir: Path) -> None:
    require(executable.is_file(), f"installed executable is missing: {executable}")
    for path in destination.rglob("*"):
        require(not path.is_symlink(), f"installed product contains a symbolic link: {path}")

    dynamic_suffixes = (".dll",) if os.name == "nt" else ((".dylib",) if sys.platform == "darwin" else ())
    forbidden_suffixes = dynamic_suffixes + (".a", ".lib", ".h", ".hpp", ".cmake")
    for suffix in forbidden_suffixes:
        # The OpenXR loader is the one shipped dynamic library (desktop VR, see docs/vr.md).
        bundled = [path for path in destination.rglob(f"*{suffix}")
                   if path.is_file() and not path.name.lower().startswith(OPENXR_LOADER_PREFIXES)]
        if bundled:
            raise SystemExit(f"installed product contains a forbidden SDK/dynamic file: {bundled[0]}")

    forbidden = (str(ROOT.resolve()).encode(), str(build_dir.resolve()).encode())
    for path in destination.rglob("*"):
        if not path.is_file():
            continue
        data = path.read_bytes()
        for value in forbidden:
            require(value not in data, f"installed product embeds a checkout/build path: {path}")


def verify_cli(executable: Path, run_programs: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="opendbc-installed-cli-") as temporary:
        working = Path(temporary)
        for source, dialect, expected_profile in PROFILES:
            execute([str(executable), "check", str(source), "--dialect", dialect], cwd=working)
            inspected = execute(
                [str(executable), "inspect", str(source), "--dialect", dialect, "--json"], cwd=working)
            require(json.loads(inspected.stdout)["profile"] == expected_profile,
                    f"installed inspect selected the wrong profile for {source.name}")
            if not run_programs:
                continue
            output = working / f"{source.stem}.out"
            execute([
                str(executable), "run", str(source), "--dialect", dialect,
                "--asset-root", str(source.parent), "--log-output", str(output), "--max-frames", "5",
            ], cwd=working)
            expected = source.with_suffix(".expected")
            require(output.read_bytes() == expected.read_bytes(),
                    f"installed run output differs for {source.name}")


def install_from_cpack(build_dir: Path, destination: Path, temporary: Path) -> None:
    package_dir = temporary / "package"
    execute([
        "cpack", "--config", str(build_dir / "CPackConfig.cmake"),
        "-C", "Release", "-G", "TGZ", "-B", str(package_dir),
    ])
    archives = list(package_dir.glob("*.tar.gz")) + list(package_dir.glob("*.tgz"))
    require(len(archives) == 1, "CPack must produce exactly one TGZ archive")
    extracted = temporary / "extracted"
    extracted.mkdir()
    with tarfile.open(archives[0], "r:gz") as package:
        for member in package.getmembers():
            require(not member.issym() and not member.islnk() and not member.isdev(),
                    f"CPack archive contains a link or device: {member.name}")
            member_path = Path(member.name)
            require(not member_path.is_absolute() and ".." not in member_path.parts,
                    f"CPack archive contains an unsafe path: {member.name}")
    execute(["cmake", "-E", "tar", "xzf", str(archives[0])], cwd=extracted)
    roots = [path for path in extracted.iterdir() if not path.name.startswith("._")]
    require(roots, "CPack archive is empty")
    destination.mkdir()
    for root in roots:
        shutil.move(str(root), str(destination / root.name))


def main() -> None:
    parser = argparse.ArgumentParser(description="Install, relocate and qualify the OpenDBC product")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--run", action="store_true",
                        help="also run one program for each historical profile")
    parser.add_argument("--require-export-hosts", action="store_true",
                        help="require qualified iOS device, simulator and Android export hosts")
    parser.add_argument("--cpack", action="store_true",
                        help="qualify the extracted CPack TGZ instead of cmake --install")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    destination = args.destination.resolve()
    require(build_dir.is_dir(), f"build directory does not exist: {build_dir}")
    require(not destination.exists(), f"qualification destination already exists: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="opendbc-install-origin-", dir=destination.parent) as temporary:
        temporary_root = Path(temporary)
        if args.cpack:
            install_from_cpack(build_dir, destination, temporary_root)
        else:
            origin = temporary_root / "installed origin"
            execute([
                "cmake", "--install", str(build_dir), "--config", "Release",
                "--component", "Runtime", "--prefix", str(origin),
            ])
            shutil.move(str(origin), str(destination))

    executable, resources = installed_layout(destination)
    verify_resources(resources)
    if args.require_export_hosts:
        verify_export_hosts(resources)
    verify_relocation(destination, executable, build_dir)
    verify_cli(executable, args.run)
    print(f"Installed product qualified after relocation: {destination}")


if __name__ == "__main__":
    main()
