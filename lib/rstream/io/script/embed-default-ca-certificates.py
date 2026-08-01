#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# See LICENSE file in the project root for license information.

import argparse
import hashlib
import os
from pathlib import Path
import tempfile


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_CERTIFICATE_BUNDLE = SCRIPT_DIRECTORY.parent / "data" / "cacert.pem"


def escape_string(s):
    return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")


def split_string(s, chunk_size=8192):
    return [s[index:index + chunk_size] for index in range(0, len(s), chunk_size)]


def write_cpp_file(file, cert_data):
    chunks = split_string(cert_data)
    file.write("// See LICENSE file in the project root for license information.\n\n")
    file.write("#include <array>\n")
    file.write("#include <string>\n")
    file.write("#include <string_view>\n\n")
    file.write("namespace rstream {\n")
    file.write("namespace io {\n\n")
    file.write(f"static const std::array<std::string_view, {len(chunks)}> g_default_ca_certificate_chunks{{\n")
    for chunk in chunks:
        file.write(f"    \"{escape_string(chunk)}\",\n")
    file.write("};\n\n")
    file.write("static const std::string g_default_ca_certificates = []() {\n")
    file.write("  std::string certificates;\n")
    file.write(f"  certificates.reserve({len(cert_data)});\n")
    file.write("  for (const auto chunk : g_default_ca_certificate_chunks) {\n")
    file.write("    certificates.append(chunk.data(), chunk.size());\n")
    file.write("  }\n")
    file.write("  return certificates;\n")
    file.write("}();\n\n")
    file.write("const std::string& get_default_ca_certificates()\n")
    file.write("{\n")
    file.write("  return g_default_ca_certificates;\n")
    file.write("}\n\n")
    file.write("} // namespace io\n")
    file.write("} // namespace rstream\n")


def load_certificate_bundle(input_path, sha256_path):
    bundle = input_path.read_bytes()
    expected_hash = sha256_path.read_text(encoding="ascii").split()[0].lower()
    actual_hash = hashlib.sha256(bundle).hexdigest()
    if actual_hash != expected_hash:
        raise ValueError(
            f"certificate bundle checksum mismatch: expected {expected_hash}, got {actual_hash}"
        )
    return bundle.decode("utf-8")


def generate_cpp_file(input_path, sha256_path, output_path):
    cert_data = load_certificate_bundle(input_path, sha256_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    file_descriptor, temporary_path = tempfile.mkstemp(
        dir=output_path.parent,
        prefix=f".{output_path.name}.",
    )
    try:
        with os.fdopen(file_descriptor, "w", encoding="utf-8", newline="\n") as file:
            write_cpp_file(file, cert_data)
        os.replace(temporary_path, output_path)
    except BaseException:
        if os.path.exists(temporary_path):
            os.unlink(temporary_path)
        raise


def main():
    parser = argparse.ArgumentParser(description="Embed a PEM certificate bundle in a C++ source file.")
    parser.add_argument(
        "--input",
        default=DEFAULT_CERTIFICATE_BUNDLE,
        type=Path,
        help="PEM certificate bundle to embed",
    )
    parser.add_argument(
        "--sha256",
        type=Path,
        help="file containing the expected SHA-256 checksum",
    )
    parser.add_argument("--cpp-out", default="default_ca_certificates.cpp", help="C++ file to generate")
    args = parser.parse_args()
    sha256_path = args.sha256 or args.input.with_suffix(f"{args.input.suffix}.sha256")
    generate_cpp_file(args.input, sha256_path, Path(args.cpp_out))


if __name__ == "__main__":
    main()
