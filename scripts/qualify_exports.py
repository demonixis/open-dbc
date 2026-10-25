#!/usr/bin/env python3

import argparse
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


PROFILES = (
    ("classic", "dbc", "net.demonixis.opendbc.qaclassic"),
    ("professional", "dbpro", "net.demonixis.opendbc.qaprofessional"),
)
PROBE = b"OpenDBC export asset\n"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def execute(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    print("+", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(command, text=True, capture_output=capture, check=False)
    if result.returncode != 0:
        if capture:
            if result.stdout:
                print(result.stdout, file=sys.stderr, end="")
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
        raise SystemExit(f"command failed with exit code {result.returncode}")
    return result


def prepare_project(root: Path, name: str, dialect: str, bundle_id: str,
                    keystore: Path | None) -> Path:
    project = root / name
    include = project / "include"
    assets = project / "assets"
    include.mkdir(parents=True)
    assets.mkdir()
    entry = project / "main.dba"
    shared = include / "shared.dba"
    entry.write_text(
        f'#include "include/{shared.name}"\nprint "profile={name}"\nprint shared_value\nend\n',
        encoding="utf-8",
    )
    shared.write_text("shared_value = 42\n", encoding="utf-8")
    (assets / "probe.txt").write_bytes(PROBE)
    settings = {
        "general": {
            "appName": f"OpenDBC {name.title()} QA",
            "bundleId": bundle_id,
            "versionName": "1.0.0",
            "versionCode": 1,
        },
        "ios": {
            "deploymentTarget": "16.0",
            "orientations": ["LandscapeLeft", "LandscapeRight"],
        },
        "android": {
            "minSdk": 29,
            "targetSdk": 36,
            "screenOrientation": "sensorLandscape",
            "permissions": [],
        },
        "language": {
            "dialect": dialect,
            "entry": "main.dba",
        },
    }
    target = entry
    if dialect == "dbpro":
        descriptor = project / "game.dbpro"
        descriptor.write_text(
            "version=DBP1.00\nmain=main.dba\ninclude1=include\\shared.dba\n",
            encoding="utf-8",
        )
        settings["language"]["projectFile"] = descriptor.name
        target = descriptor
    if keystore is not None:
        settings["android"]["keystorePath"] = str(keystore)
        settings["android"]["keyAlias"] = "opendbc-qa"
    (project / ".opendbc-project.json").write_text(
        json.dumps(settings, indent=2) + "\n", encoding="utf-8"
    )
    return target


def only_artifact(directory: Path, suffix: str) -> Path:
    values = list(directory.glob(f"*{suffix}"))
    require(len(values) == 1, f"expected exactly one {suffix} artifact in {directory}")
    return values[0]


def verify_manifest(data: bytes, dialect: str) -> None:
    manifest = json.loads(data)
    require(manifest.get("format") == 4, "artifact manifest has the wrong format")
    require(manifest.get("dialect") == dialect, "artifact manifest has the wrong dialect")
    require(str(manifest.get("entry", "")).endswith(".dba"), "artifact manifest entry is not DBA")
    paths = {entry.get("path") for entry in manifest.get("files", [])}
    require("assets/probe.txt" in paths, "artifact manifest omits the probe asset")
    require(any(str(path).startswith("include/") for path in paths), "artifact manifest omits the include")


def verify_directory_payload(root: Path, dialect: str) -> None:
    manifest = root / "opendbc-artifact.json"
    require(manifest.is_file(), f"artifact manifest is missing: {manifest}")
    verify_manifest(manifest.read_bytes(), dialect)
    require((root / "artifact/assets/probe.txt").read_bytes() == PROBE,
            "artifact probe asset differs")


def verify_zip_payload(archive: Path, manifest_name: str, asset_name: str, dialect: str) -> None:
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        manifests = [name for name in names if name.endswith(manifest_name)]
        assets = [name for name in names if name.endswith(asset_name)]
        require(len(manifests) == 1, f"archive has no unique {manifest_name}")
        require(len(assets) == 1, f"archive has no unique {asset_name}")
        verify_manifest(package.read(manifests[0]), dialect)
        require(package.read(assets[0]) == PROBE, "archived probe asset differs")


def export(executable: Path, entry: Path, dialect: str, target: str, output: Path,
           unsigned: bool) -> Path:
    output.mkdir(parents=True)
    command = [
        str(executable), "export", str(entry), "--dialect", dialect,
        "--target", target, "--output", str(output),
    ]
    if unsigned:
        command.append("--unsigned")
    execute(command)
    suffix = {
        "desktop": ".app" if sys.platform == "darwin" else "",
        "ios": ".ipa",
        "ios-simulator": ".app",
        "android-apk": ".apk",
        "android-aab": ".aab",
    }[target]
    if suffix:
        return only_artifact(output, suffix)
    entries = list(output.iterdir())
    require(len(entries) == 1, f"expected exactly one Desktop artifact in {output}")
    return entries[0]


def boot_ios_simulator() -> str:
    devices = json.loads(execute(["xcrun", "simctl", "list", "devices", "available", "-j"], capture=True).stdout)
    candidates = [device for runtime in devices.get("devices", {}).values() for device in runtime
                  if device.get("isAvailable") and "iPhone" in device.get("name", "")]
    require(candidates, "no available iOS Simulator device")
    selected = next((device for device in candidates if device.get("state") == "Booted"), candidates[0])
    udid = selected["udid"]
    if selected.get("state") != "Booted":
        execute(["xcrun", "simctl", "boot", udid])
    execute(["xcrun", "simctl", "bootstatus", udid, "-b"])
    return udid


def expected_output(name: str) -> str:
    return f"profile={name}\n42\n"


def launch_ios_simulator(app: Path, bundle_id: str, udid: str, name: str) -> None:
    execute(["xcrun", "simctl", "install", udid, str(app)])
    command = [
        "xcrun", "simctl", "launch", "--console", "--terminate-running-process", udid, bundle_id,
    ]
    print("+", subprocess.list2cmdline(command), flush=True)
    launched = subprocess.Popen(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = launched.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        execute(["xcrun", "simctl", "terminate", udid, bundle_id])
        try:
            stdout, stderr = launched.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            launched.kill()
            stdout, stderr = launched.communicate()
            raise SystemExit("iOS Simulator launch did not stop after terminating the application")
    if stdout:
        print(stdout, end="")
    if stderr:
        print(stderr, file=sys.stderr, end="")
    require(launched.returncode == 0, f"iOS Simulator launch failed with exit code {launched.returncode}")
    require(expected_output(name) in stdout,
            f"iOS Simulator artifact did not execute the expected {name} BASIC output")


def launch_android(apk: Path, bundle_id: str, adb: str) -> None:
    devices = execute([adb, "devices"], capture=True).stdout.splitlines()[1:]
    require(any(line.rstrip().endswith("\tdevice") for line in devices), "no ready Android emulator/device")
    execute([adb, "install", "-r", str(apk)])
    activity = f"{bundle_id}/net.demonixis.opendbc.host.OpenDbcActivity"
    execute([adb, "shell", "am", "start", "-W", "-n", activity])
    execute([adb, "shell", "am", "force-stop", bundle_id])


def create_qa_keystore(root: Path) -> Path:
    keystore = root / "opendbc-qa.keystore"
    password = "opendbc-qa-password"
    result = subprocess.run([
        "keytool", "-genkeypair", "-noprompt", "-keystore", str(keystore),
        "-storepass", password, "-keypass", password, "-alias", "opendbc-qa",
        "-keyalg", "RSA", "-keysize", "2048", "-validity", "2",
        "-dname", "CN=OpenDBC QA, O=OpenDBC, C=FR",
    ], text=True, capture_output=True, check=False)
    require(result.returncode == 0 and keystore.is_file(), "cannot create the temporary Android QA keystore")
    os.environ["OPENDBC_ANDROID_KEYSTORE_PASSWORD"] = password
    os.environ["OPENDBC_ANDROID_KEY_PASSWORD"] = password
    return keystore


def main() -> None:
    parser = argparse.ArgumentParser(description="Export and qualify BASIC artifacts")
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mobile", action="store_true", help="also qualify iOS and Android packages")
    parser.add_argument("--run-ios-simulator", action="store_true")
    parser.add_argument("--run-android", action="store_true")
    parser.add_argument("--adb", default="adb")
    args = parser.parse_args()

    executable = args.executable.resolve()
    output = args.output.resolve()
    require(executable.is_file(), f"OpenDBC executable is missing: {executable}")
    require(not output.exists(), f"qualification output already exists: {output}")
    output.mkdir(parents=True)

    simulator_udid = boot_ios_simulator() if args.run_ios_simulator else ""
    with tempfile.TemporaryDirectory(prefix="opendbc-export-qualification-") as temporary:
        projects = Path(temporary)
        keystore = create_qa_keystore(projects) if args.run_android else None
        for name, dialect, bundle_id in PROFILES:
            entry = prepare_project(projects, name, dialect, bundle_id, keystore)
            desktop = export(executable, entry, dialect, "desktop", output / f"desktop-{name}", False)
            if sys.platform == "darwin":
                desktop_resources = desktop / "Contents/Resources"
                desktop_executable = desktop / "Contents/MacOS/opendbc"
                execute(["codesign", "--verify", "--deep", "--strict", str(desktop)])
            else:
                desktop_resources = desktop
                desktop_executable = desktop / ("opendbc.exe" if os.name == "nt" else "opendbc")
            verify_directory_payload(desktop_resources, dialect)
            launched = execute([str(desktop_executable)], capture=True)
            require(launched.stdout == expected_output(name),
                    f"Desktop artifact did not execute the expected {name} BASIC output")

            if not args.mobile:
                continue
            require(sys.platform == "darwin", "mobile export qualification currently requires macOS")
            simulator = export(executable, entry, dialect, "ios-simulator", output / f"ios-simulator-{name}", False)
            verify_directory_payload(simulator, dialect)
            execute(["codesign", "--verify", "--deep", "--strict", str(simulator)])
            if simulator_udid:
                launch_ios_simulator(simulator, bundle_id, simulator_udid, name)

            apk = export(executable, entry, dialect, "android-apk", output / f"android-apk-{name}", not args.run_android)
            verify_zip_payload(apk, "assets/opendbc-artifact.json",
                               "assets/artifact/assets/probe.txt", dialect)
            if args.run_android:
                launch_android(apk, bundle_id, args.adb)

        if args.mobile:
            classic_entry = projects / "classic/main.dba"
            ipa = export(executable, classic_entry, "dbc", "ios", output / "ios-device-classic", True)
            verify_zip_payload(ipa, "opendbc-artifact.json", "artifact/assets/probe.txt", "dbc")
            aab = export(executable, classic_entry, "dbc", "android-aab", output / "android-aab-classic",
                         not args.run_android)
            verify_zip_payload(aab, "assets/opendbc-artifact.json",
                               "assets/artifact/assets/probe.txt", "dbc")

    print(f"BASIC artifact exports qualified: {output}")


if __name__ == "__main__":
    main()
