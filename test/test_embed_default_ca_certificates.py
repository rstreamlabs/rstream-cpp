#!/usr/bin/env python3

import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPOSITORY_ROOT / "lib" / "rstream" / "io" / "script" / "embed-default-ca-certificates.py"
SPEC = importlib.util.spec_from_file_location("rstream_embed_default_ca_certificates", SCRIPT_PATH)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class CertificateBundleGeneratorTest(unittest.TestCase):
    def test_repository_bundle_matches_checksum(self):
        bundle_path = REPOSITORY_ROOT / "lib" / "rstream" / "io" / "data" / "cacert.pem"
        checksum_path = bundle_path.with_suffix(".pem.sha256")
        bundle = GENERATOR.load_certificate_bundle(bundle_path, checksum_path)
        self.assertGreater(len(bundle), 0)

    def test_generates_deterministic_cpp_from_verified_bundle(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            input_path = directory / "cacert.pem"
            sha256_path = directory / "cacert.pem.sha256"
            output_path = directory / "default_ca_certificates.cpp"
            bundle = b"-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n"
            input_path.write_bytes(bundle)
            sha256_path.write_text(f"{hashlib.sha256(bundle).hexdigest()}  cacert.pem\n", encoding="ascii")
            GENERATOR.generate_cpp_file(input_path, sha256_path, output_path)
            first_output = output_path.read_bytes()
            GENERATOR.generate_cpp_file(input_path, sha256_path, output_path)
            self.assertEqual(output_path.read_bytes(), first_output)
            source = first_output.decode("utf-8")
            self.assertIn("-----BEGIN CERTIFICATE-----\\n", source)
            self.assertIn(f"certificates.reserve({len(bundle)})", source)

    def test_rejects_bundle_with_unexpected_checksum(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            input_path = directory / "cacert.pem"
            sha256_path = directory / "cacert.pem.sha256"
            output_path = directory / "default_ca_certificates.cpp"
            input_path.write_text("invalid", encoding="utf-8")
            sha256_path.write_text(f"{'0' * 64}  cacert.pem\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "certificate bundle checksum mismatch"):
                GENERATOR.generate_cpp_file(input_path, sha256_path, output_path)
            self.assertFalse(output_path.exists())


if __name__ == "__main__":
    unittest.main()
