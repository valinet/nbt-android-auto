"""Regression tests for open-build selection and output validation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import build_open


class OpenBuildTests(unittest.TestCase):
    def test_selects_requested_programs_without_duplicates(self):
        self.assertEqual(build_open.select_programs("native_service nbt_reset native_service"),
                         ("native_service", "nbt_reset"))

    def test_rejects_empty_or_unknown_program_selection(self):
        for value in ("  ", "native_service typo"):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                build_open.select_programs(value)

    def test_missing_or_empty_selected_output_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, destination = root / "source", root / "destination"
            source.mkdir()
            destination.mkdir()
            for empty in (False, True):
                if empty:
                    (source / "nbt_reset").touch()
                with self.subTest(empty=empty), self.assertRaisesRegex(SystemExit, "nonempty nbt_reset"):
                    build_open.copy_built_programs(source, destination, ("nbt_reset",))

    def test_copies_only_selected_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, destination = root / "source", root / "destination"
            source.mkdir()
            destination.mkdir()
            (source / "nbt_reset").write_bytes(b"binary")
            (source / "other").write_bytes(b"other")
            build_open.copy_built_programs(source, destination, ("nbt_reset",))
            self.assertEqual((destination / "nbt_reset").read_bytes(), b"binary")
            self.assertFalse((destination / "other").exists())

    def test_reuse_cannot_hide_missing_selected_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            (root / "scripts" / "build-open.sh").write_text("#!/bin/sh\n")
            (root / "payload" / "rt").mkdir(parents=True)
            (root / "payload" / "rt" / "nbt_reset").write_bytes(b"old binary")
            args = ["build_open.py", "--programs", "nbt_reset", "--reuse", str(root / "payload"),
                    "--output-dir", "build/open-test"]
            with mock.patch.object(build_open, "ROOT", root), \
                    mock.patch.object(build_open, "add_mbedtls"), \
                    mock.patch.object(build_open, "add_nbt_aad"), \
                    mock.patch.object(build_open, "add_repo_sources"), \
                    mock.patch.object(sys, "argv", args), \
                    mock.patch.object(build_open.subprocess, "run",
                                      return_value=subprocess.CompletedProcess([], 0, b"BUILD_OPEN_DONE")):
                with self.assertRaisesRegex(SystemExit, "nonempty nbt_reset"):
                    build_open.main()

    def test_output_must_be_new_and_within_build_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build" / "existing").mkdir(parents=True)
            for path, error in (("elsewhere", SystemExit), ("build/existing", FileExistsError)):
                with self.subTest(path=path), mock.patch.object(build_open, "ROOT", root), \
                        mock.patch.object(sys, "argv", ["build_open.py", "--output-dir", path]), \
                        self.assertRaises(error):
                    build_open.main()


if __name__ == "__main__":
    unittest.main()
