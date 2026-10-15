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
    cors_options = [
        "--cors-allow-origin=https://dashboard.example",
        "--cors-allow-origin=https://second.example",
        "--cors-allow-method=GET", "--cors-allow-method=PUT",
        "--cors-allow-header=Authorization", "--cors-expose-header=ETag",
        "--cors-max-age=600",
        "--cors-allow-credentials",
    ]
    cors = subprocess.run(
        [str(binary), "--http", *cors_options, "--help"],
        capture_output=True, text=True, timeout=timeout,
    )
    if cors.returncode != 0:
        raise RuntimeError("Shared Go/C++ CORS options were rejected: " + cors.stderr)
    for option in cors_options:
        if option.split("=")[0] not in cors.stdout:
            raise RuntimeError("CORS option missing from CLI help: " + option)
    removed = subprocess.run(
        [str(binary), "--http-use-tls"],
        capture_output=True, text=True, timeout=timeout,
    )
    if removed.returncode == 0 or "Unexpected argument: --http-use-tls" not in removed.stderr:
        raise RuntimeError("Removed upstream TLS option was accepted")


if __name__ == "__main__":
    main()
