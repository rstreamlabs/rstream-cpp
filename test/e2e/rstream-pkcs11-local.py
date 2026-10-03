#!/usr/bin/env python3
"""Loopback mTLS using a disposable SoftHSM key and an external public provider."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--provider", default="pkcs11prov", choices=["pkcs11prov", "pkcs11"]
    )
    parser.add_argument("--tls-version", choices=["1.2", "1.3"], default="1.3")
    parser.add_argument("--openssl", type=Path, required=True)
    parser.add_argument(
        "--tools-prefix",
        type=Path,
        required=True,
        help="Prefix containing bin/softhsm2-util and bin/pkcs11-tool",
    )
    parser.add_argument(
        "--module",
        type=Path,
        required=True,
        help="SoftHSM2 module; never use a physical HSM for this fixture",
    )
    parser.add_argument("--provider-dir", type=Path, required=True)
    parser.add_argument("--rsa", action="store_true")
    args = parser.parse_args()
    OPENSSL = args.openssl.resolve()
    MODULE = args.module.resolve()
    if "softhsm" not in MODULE.name.lower():
        parser.error(
            "--module must identify a SoftHSM library for disposable test tokens"
        )
    BASE = os.environ | {
        "OPENSSL_MODULES": str(args.provider_dir.resolve()),
        "OPENSSL_CONF": "/dev/null",
    }
    # Keep the fixture isolated from configured provider/HSM credentials.
    for name in (
        "PKCS11_MODULE_PATH",
        "PKCS11_PIN",
        "PKCS11_PROVIDER_MODULE",
        "PKCS11_PROVIDER_DEBUG",
        "RSTREAM_TEST_PIN",
        "RSTREAM_CONTROL_PIN",
    ):
        BASE.pop(name, None)
    for name in list(BASE):
        if name.startswith("RSTREAM_"):
            BASE.pop(name)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "validation.json").unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="rstream-pkcs11-") as directory:
        temp = Path(directory)
        (temp / "tokens").mkdir()
        config = temp / "softhsm2.conf"
        config.write_text(
            f'directories.tokendir = {temp / "tokens"}\nobjectstore.backend = file\nlog.level = ERROR\nslots.removable = false\n'
        )
        env = BASE | {"SOFTHSM2_CONF": str(config)}

        def run(command, name, extra=None):
            result = subprocess.run(
                command,
                env=env | (extra or {}),
                capture_output=True,
                text=True,
                timeout=30,
            )
            (args.output / (name + ".log")).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"{name} failed with exit{result.returncode}")
            return result

        # These fixed PINs protect disposable test keys only; no user credentials are used.
        run(
            [
                str(args.tools_prefix / "bin/softhsm2-util"),
                "--module",
                str(MODULE),
                "--init-token",
                "--free",
                "--label",
                "rstream-local-pilot",
                "--pin",
                "123456",
                "--so-pin",
                "12345678",
            ],
            "init",
        )
        run(
            [
                str(args.tools_prefix / "bin/pkcs11-tool"),
                "--module",
                str(MODULE),
                "--login",
                "--pin",
                "123456",
                "--keypairgen",
                "--key-type",
                ("RSA:2048" if args.rsa else "EC:prime256v1"),
                "--label",
                "client",
                "--id",
                "01",
                "--usage-sign",
            ],
            "keygen",
        )
        provider_env = {"PKCS11_MODULE_PATH": str(MODULE), "PKCS11_PIN": "123456"}
        if args.provider == "pkcs11":
            provider_env = {
                "PKCS11_PROVIDER_MODULE": str(MODULE),
                "RSTREAM_CONTROL_PIN": "123456",
            }
        uri = "pkcs11:token=rstream-local-pilot;object=client;type=private"
        openssl_uri = uri + (
            "?pin-source=env:RSTREAM_CONTROL_PIN" if args.provider == "pkcs11" else ""
        )
        run(
            [
                str(OPENSSL),
                "req",
                "-new",
                "-x509",
                "-provider",
                args.provider,
                "-provider",
                "default",
                "-key",
                openssl_uri,
                "-sha256",
                "-days",
                "1",
                "-subj",
                "/CN=rstream-local-client",
                "-out",
                str(temp / "client.crt"),
            ],
            "certificate",
            provider_env,
        )
        run(
            [
                str(OPENSSL),
                "req",
                "-new",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-days",
                "1",
                "-subj",
                "/CN=localhost",
                "-addext",
                "subjectAltName=DNS:localhost,IP:127.0.0.1",
                "-keyout",
                str(temp / "server.key"),
                "-out",
                str(temp / "server.crt"),
            ],
            "server-certificate",
        )
        results = []
        for name, pin in [("accepted", "123456"), ("rejected-pin", "000000")]:
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            with (args.output / (name + "-server.log")).open("w") as log:
                server = subprocess.Popen(
                    [
                        str(OPENSSL),
                        "s_server",
                        "-accept",
                        "127.0.0.1:" + str(port),
                        "-cert",
                        str(temp / "server.crt"),
                        "-key",
                        str(temp / "server.key"),
                        "-CAfile",
                        str(temp / "client.crt"),
                        "-Verify",
                        "1",
                        "-verify_return_error",
                        "-www",
                        "-tls1_2" if args.tls_version == "1.2" else "-tls1_3",
                    ],
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                )
                try:
                    for _ in range(50):
                        if server.poll() is not None:
                            raise RuntimeError("TLS server exited before readiness")
                        try:
                            with socket.create_connection(
                                ("127.0.0.1", port), timeout=0.1
                            ):
                                break
                        except OSError:
                            time.sleep(0.1)
                    else:
                        raise RuntimeError("TLS server did not listen")
                    if name == "accepted":
                        control = subprocess.run(
                            [
                                str(OPENSSL),
                                "s_client",
                                "-connect",
                                "127.0.0.1:" + str(port),
                                "-cert",
                                str(temp / "client.crt"),
                                "-key",
                                openssl_uri,
                                "-provider",
                                "default",
                                "-provider",
                                args.provider,
                                "-CAfile",
                                str(temp / "server.crt"),
                                "-verify_return_error",
                                "-quiet",
                                "-security_debug_verbose",
                            ],
                            input="GET / HTTP/1.0\r\n\r\n",
                            capture_output=True,
                            text=True,
                            env=env | provider_env,
                            timeout=30,
                        )
                        (args.output / "openssl-control.log").write_text(
                            control.stdout + control.stderr
                        )
                        assert (
                            "HTTP/1.0 200" in control.stdout
                        ), "OpenSSL control mTLS failed"
                    options = {
                        "ssl.peer_verification": "true",
                        "ssl.request_peer_cert": "true",
                        "ssl.cacert_file": str(temp / "server.crt"),
                        "ssl.cert_file": str(temp / "client.crt"),
                        "ssl.key": uri,
                        "ssl.key_type": "pkcs11",
                        "ssl.pkcs11_module": str(MODULE),
                        "ssl.pkcs11_provider": args.provider,
                        "ssl.pkcs11_pin_env": "RSTREAM_TEST_PIN",
                    }
                    address = f"tcp://127.0.0.1:{port}?ssl&" + urllib.parse.urlencode(
                        options
                    )
                    result = subprocess.run(
                        [str(args.binary), address, "--jobs=1"],
                        input="GET / HTTP/1.0\r\n\r\n",
                        capture_output=True,
                        text=True,
                        env=env | {"RSTREAM_TEST_PIN": pin},
                        timeout=30,
                    )
                    (args.output / (name + "-client.log")).write_text(
                        result.stdout + result.stderr
                    )
                    ok = "HTTP/1.0 200" in result.stdout
                    if name == "accepted":
                        assert ok, "mTLS request did not return HTTP200"
                    else:
                        assert (
                            not ok and result.returncode != 0
                        ), "Wrong PIN did not reject the request"
                    results.append(
                        {"case": name, "exit_code": result.returncode, "passed": True}
                    )
                finally:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()
        (args.output / "validation.json").write_text(
            json.dumps(
                {
                    "binary": str(args.binary),
                    "binary_sha256": hashlib.sha256(
                        args.binary.read_bytes()
                    ).hexdigest(),
                    "openssl": str(OPENSSL),
                    "openssl_version": subprocess.check_output(
                        [str(OPENSSL), "version"], env=BASE, text=True
                    ).strip(),
                    "provider": BASE["OPENSSL_MODULES"],
                    "key_type": ("RSA2048" if args.rsa else "EC P-256"),
                    "protocol": "TLS" + args.tls_version,
                    "token": "ephemeral SoftHSM2",
                    "cases": results,
                    "scope": "Loopback mTLS; does not qualify physical HSMs or hosted project policies",
                },
                indent=2,
            )
            + "\n"
        )


if __name__ == "__main__":
    main()
