#!/usr/bin/env python3

# See LICENSE file in the project root for license information.

from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def check_startup(binary, timeout, config_path, options):
    with socket.socket() as engine:
        engine.bind(("127.0.0.1", 0))
        engine.listen(1)
        engine.settimeout(timeout)
        command = [
            str(binary), "--config=" + str(config_path), "--engine=127.0.0.1:" + str(engine.getsockname()[1]),
            "--http", "--publish", "--no-token", "--no-retry", *options,
        ]
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as process:
            try:
                connection, _ = engine.accept()
                connection.close()
            except TimeoutError as error:
                process.kill()
                output, _ = process.communicate()
                raise RuntimeError("Tunnel CLI did not reach the Engine: " + output) from error
            finally:
                if process.poll() is None:
                    process.kill()


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
    with tempfile.TemporaryDirectory() as scratch:
        # Keep the caller's configured project and credentials out of the startup checks.
        config_path = Path(scratch) / "config.yaml"
        config_path.write_text("version: 1\n")
        check_startup(binary, timeout, config_path, [])
        check_startup(binary, timeout, config_path, cors_options)
        invalid_age = subprocess.run(
            [str(binary), "--config=" + str(config_path), "--engine=127.0.0.1:1", "--no-token",
             "--http", "--publish", "--cors-allow-origin=https://dashboard.example", "--cors-max-age=86401"],
            capture_output=True, text=True, timeout=timeout,
        )
    if invalid_age.returncode == 0 or "CORS max age must be between 0 and 86400" not in invalid_age.stderr:
        raise RuntimeError("CLI did not validate the requested CORS max age")

    removed = subprocess.run(
        [str(binary), "--http-use-tls"],
        capture_output=True, text=True, timeout=timeout,
    )
    if removed.returncode == 0 or "Unexpected argument: --http-use-tls" not in removed.stderr:
        raise RuntimeError("Removed upstream TLS option was accepted")


if __name__ == "__main__":
    main()
