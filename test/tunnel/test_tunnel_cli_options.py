#!/usr/bin/env python3

# See LICENSE file in the project root for license information.

from pathlib import Path
import subprocess
import sys


def main():
    binary = Path(sys.argv[1]).resolve()
    timeout = int(sys.argv[2])
    if timeout <= 0:
        raise ValueError("Expected a positive startup timeout")
    canonical = subprocess.run(
        [str(binary), "--http", "--upstream-tls", "--help"],
        capture_output=True, text=True, timeout=timeout,
    )
    if canonical.returncode != 0 or "--upstream-tls" not in canonical.stdout:
        raise RuntimeError("Canonical upstream TLS option was rejected")
    if "--http-use-tls" in canonical.stdout:
        raise RuntimeError("Help still advertises the removed upstream TLS option")
    removed = subprocess.run(
        [str(binary), "--http-use-tls"],
        capture_output=True, text=True, timeout=timeout,
    )
    if removed.returncode == 0 or "Unexpected argument: --http-use-tls" not in removed.stderr:
        raise RuntimeError("Removed upstream TLS option was accepted")


if __name__ == "__main__":
    main()
