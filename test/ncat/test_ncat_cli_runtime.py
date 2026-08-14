#!/usr/bin/env python3

import signal
import socket
import subprocess
import sys
import time


def allocate_address() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return f"127.0.0.1:{listener.getsockname()[1]}"


def run_case(binary: str, command_arguments: list[str], expected: bytes) -> None:
    address = allocate_address()
    server = subprocess.Popen(
        [binary, "-L", address, *command_arguments, "--jobs=2"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        deadline = time.monotonic() + 10
        while True:
            client = subprocess.run(
                [binary, address, "-I", "--jobs=2"],
                capture_output=True,
                timeout=10,
                check=False,
            )
            if client.returncode == 0:
                if client.stdout != expected:
                    raise AssertionError(
                        f"unexpected output {client.stdout!r}, expected {expected!r}; "
                        f"client stderr={client.stderr!r}"
                    )
                break
            if server.poll() is not None:
                stdout, stderr = server.communicate()
                raise AssertionError(
                    f"server exited with {server.returncode}; stdout={stdout!r}; stderr={stderr!r}"
                )
            if time.monotonic() >= deadline:
                raise AssertionError(f"client did not connect; stderr={client.stderr!r}")
            time.sleep(0.05)
    finally:
        if server.poll() is None:
            server.send_signal(signal.SIGTERM)
        try:
            server.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_ncat_cli_runtime.py <rstream-ncat>")
    command = "exec printf cli-shell-ok"
    run_case(sys.argv[1], ["-c", command], b"cli-shell-ok")
    run_case(sys.argv[1], [f"-c={command}"], b"cli-shell-ok")


if __name__ == "__main__":
    main()
