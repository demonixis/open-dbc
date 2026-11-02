#!/usr/bin/env python3
"""Verify that incremental builds refresh the runtime identity after source changes without reconfiguring."""

import argparse
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CMAKE = "cmake"


class RuntimeFingerprintTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="opendbc-runtime-fingerprint-")
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / "source"
        self.build = Path(self.temporary.name) / "build"
        self.write("CMakeLists.txt", """cmake_minimum_required(VERSION 3.24)
project(RuntimeFingerprintTest LANGUAGES NONE)
set(OPENDBC_VERSION 0.0.0)
set(OPENDBC_REVISION fixture)
include(cmake/runtime_fingerprint.cmake)
add_custom_target(fixture_runtime ALL DEPENDS opendbc_runtime_fingerprint)
""")
        for directory in ("src/engine", "src/interpreter", "src/language", "src/vm",
                          "tools/studio", "tools/runner"):
            self.write(f"{directory}/CMakeLists.txt", "# Fixture build contract\n")
        (self.source / "cmake").mkdir(parents=True, exist_ok=True)
        for name in ("runtime_fingerprint.cmake", "write_runtime_fingerprint.cmake", "opendbc-runtime.json.in"):
            shutil.copyfile(ROOT / "cmake" / name, self.source / "cmake" / name)
        self.inputs = ("src/vm/bytecode.hpp", "src/vm/execution_session.cpp",
                       "src/engine/systems/video_decoder_avfoundation.mm", "tools/runner/main.cpp",
                       "tools/runner/CMakeLists.txt", "tools/studio/src/app.cpp", "shaders/basic.hlsl",
                       "sdk/opendbc_plugin.h", "resources/midi/VintageDreamsWaves-v2.sf2")
        for path in self.inputs:
            self.write(path, "// Original fixture input\n")
        self.run_cmake("-S", str(self.source), "-B", str(self.build), "-G", "Ninja")
        self.run_cmake("--build", str(self.build))

    def write(self, relative: str, content: str) -> None:
        path = self.source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def run_cmake(self, *arguments: str) -> str:
        result = subprocess.run([CMAKE, *arguments], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def fingerprint(self) -> str:
        header = (self.build / "generated/runtime/opendbc_runtime_fingerprint.h").read_text(encoding="utf-8")
        manifest = (self.build / "opendbc-runtime.json").read_text(encoding="utf-8")
        value = header.split('"')[1]
        self.assertIn(f'"fingerprint": "{value}"', manifest)
        return value

    def test_edited_runtime_inputs_refresh_identity_without_reconfiguring(self) -> None:
        for path in self.inputs:
            with self.subTest(path=path):
                original = self.fingerprint()
                self.write(path, "// Changed fixture input\n")
                output = self.run_cmake("--build", str(self.build))
                self.assertNotIn("Re-running CMake", output)
                self.assertNotEqual(self.fingerprint(), original)

    def test_platform_sources_are_discovered_and_removed_incrementally(self) -> None:
        original = self.fingerprint()
        path = "src/engine/systems/new_platform.mm"
        self.write(path, "// New fixture input\n")
        self.run_cmake("--build", str(self.build))
        self.assertNotEqual(self.fingerprint(), original)
        (self.source / path).unlink()
        self.run_cmake("--build", str(self.build))
        self.assertEqual(self.fingerprint(), original)

    def test_unchanged_build_preserves_identity(self) -> None:
        original = self.fingerprint()
        self.run_cmake("--build", str(self.build))
        self.assertEqual(self.fingerprint(), original)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", default="cmake")
    options, remaining = parser.parse_known_args()
    CMAKE = options.cmake
    unittest.main(argv=[__file__, *remaining])
