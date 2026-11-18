#!/usr/bin/env python3
# See LICENSE file in the project root for license information.

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SSLVersionTest(unittest.TestCase):
    def check_version(self, version, accepted):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "check.cmake"
            script.write_text(
                f'include("{ROOT / "cmake/ssl-version.cmake"}")\n'
                f'rstream_validate_openssl_version("{version}")\n',
                encoding="utf-8",
            )
            result = subprocess.run(
                ["cmake", "-P", str(script)], capture_output=True, text=True, check=False
            )
        self.assertEqual(result.returncode == 0, accepted, result.stderr)
        if not accepted:
            self.assertIn("Unsupported OpenSSL", result.stderr)

    def test_supported_lts_and_current_patches(self):
        for version in ("3.5.9", "3.5.10", "3.6.5", "3.6.6"):
            with self.subTest(version=version):
                self.check_version(version, True)

    def test_older_patches_and_unqualified_major_fail(self):
        for version in ("3.0.23", "3.5.8", "3.6.0", "3.6.3", "3.6.4", "4.0.3"):
            with self.subTest(version=version):
                self.check_version(version, False)


if __name__ == "__main__":
    unittest.main()
