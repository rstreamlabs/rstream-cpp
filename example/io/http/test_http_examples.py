#!/usr/bin/env python3
# See LICENSE file in the project root for license information.

from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time


def main():
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        address = "127.0.0.1:" + str(reservation.getsockname()[1])
    with tempfile.TemporaryDirectory() as directory:
        log_path = Path(directory) / "server.log"
        with log_path.open("w") as log:
            server = subprocess.Popen(
                [sys.argv[1], "--uri=" + address], stdout=log, stderr=subprocess.STDOUT
            )
            try:
                deadline = time.monotonic() + 15
                while "server started on" not in log_path.read_text(errors="replace"):
                    if server.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(
                            "HTTP example failed to start: "
                            + log_path.read_text(errors="replace")
                        )
                    time.sleep(0.05)
                result = subprocess.run(
                    [sys.argv[2], "--uri=" + address],
                    capture_output=True, text=True, timeout=15, check=True,
                )
                if result.stderr or "HTTP/1.1 200 OK" not in result.stdout:
                    raise RuntimeError("HTTP example response failed: " + repr(result))
                if not result.stdout.rstrip().endswith(socket.gethostname()):
                    raise RuntimeError("HTTP example response hostname mismatch")
                failed = subprocess.run(
                    [sys.argv[2], "--uri=unsupported://127.0.0.1:8080"],
                    capture_output=True, text=True, timeout=5,
                )
                assert failed.returncode == 1 and "request failed" in failed.stderr, failed
                if os.name != "nt":
                    server.terminate()
                    assert server.wait(timeout=5) == 0, log_path.read_text(errors="replace")
            finally:
                if server.poll() is None:
                    server.kill()
                server.wait(timeout=5)


if __name__ == "__main__":
    main()
