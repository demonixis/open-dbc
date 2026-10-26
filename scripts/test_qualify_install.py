#!/usr/bin/env python3
"""Check shader completeness and integrity in installed products and export hosts."""

import shutil
import tempfile
import unittest
from collections.abc import Callable
from pathlib import Path

import qualify_install


class InstalledShaderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="opendbc-install-shaders-")
        self.addCleanup(self.temporary.cleanup)
        self.resources = Path(self.temporary.name) / "OpenDBC installed product"
        self.shaders = sorted((qualify_install.ROOT / "shaders").glob("*.spv"))

    def write(self, relative: str, content: str = "") -> None:
        path = self.resources / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def copy_shaders(self, directory: Path) -> None:
        directory.mkdir(parents=True)
        for source in self.shaders:
            shutil.copyfile(source, directory / source.name)

    def check_incomplete_shaders(self, directory: Path, verify: Callable[[Path], None]) -> None:
        for source in self.shaders:
            shader = directory / source.name
            original = shader.read_bytes()
            with self.subTest(directory=directory, shader=source.name, failure="missing"):
                shader.unlink()
                with self.assertRaises(SystemExit) as error:
                    verify(self.resources)
                self.assertIn(f"installed shader is missing: {shader}", str(error.exception))
                shader.write_bytes(original)
            with self.subTest(directory=directory, shader=source.name, failure="changed"):
                shader.write_bytes(b"stale shader")
                with self.assertRaises(SystemExit) as error:
                    verify(self.resources)
                self.assertIn(f"installed shader differs: {shader}", str(error.exception))
                shader.write_bytes(original)

    def test_product_requires_every_canonical_shader(self) -> None:
        self.resources.mkdir(parents=True)
        for source in qualify_install.REFERENCE_SOURCES:
            shutil.copyfile(source, self.resources / source.name)
        self.write("NOTICE.md")
        self.write("opendbc-runtime.json", "{}")
        for name in qualify_install.REQUIRED_LICENSES:
            self.write(f"licenses/{name}")
        shaders = self.resources / "shaders"
        self.copy_shaders(shaders)

        qualify_install.verify_resources(self.resources)
        self.check_incomplete_shaders(shaders, qualify_install.verify_resources)

    def test_mobile_hosts_require_every_canonical_shader(self) -> None:
        for host in ("ios", "ios-simulator"):
            self.write(f"export-hosts/{host}/OpenDBC.app/opendbc")
            self.write(f"export-hosts/{host}/opendbc-host.json", "{}")
        android = "export-hosts/android/template"
        self.write(f"{android}/app/src/main/jniLibs/arm64-v8a/libmain.so")
        self.write(f"{android}/app/src/main/assets/opendbc-host.json", "{}")
        self.write(f"{android}/gradle/wrapper/gradle-wrapper.properties")
        self.write(f"{android}/gradlew")
        self.write(f"{android}/app/build.gradle", "android { compileSdk 36 }")
        directories = (
            self.resources / "export-hosts/ios/OpenDBC.app/shaders",
            self.resources / "export-hosts/ios-simulator/OpenDBC.app/shaders",
            self.resources / android / "app/src/main/assets/shaders",
        )
        for directory in directories:
            self.copy_shaders(directory)

        qualify_install.verify_export_hosts(self.resources)
        for directory in directories:
            self.check_incomplete_shaders(directory, qualify_install.verify_export_hosts)


if __name__ == "__main__":
    unittest.main()
