#!/usr/bin/env python3

# See LICENSE file in the project root for license information.

from pathlib import Path
import subprocess
import sys


def main():
    binary = Path(sys.argv[1]).resolve()
    argument = sys.argv[2]
    if argument not in ("--help", "--version"):
        raise ValueError("Expected --help or --version")
    result = subprocess.run(
        [str(binary), argument], capture_output=True, text=True, timeout=10
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{binary.name} {argument} failed ({result.returncode:#x}): "
            + result.stderr[:1024]
        )
    if binary.stem not in result.stdout or (
        argument == "--help" and "usage:" not in result.stdout.lower()
    ):
        raise RuntimeError(f"{binary.name} {argument} returned incomplete output")


if __name__ == "__main__":
    main()
