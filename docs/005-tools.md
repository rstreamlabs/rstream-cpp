# C++ command-line tools

These programs are built from the C++ SDK and distributed as `rstream-utils`. The [Go CLI](https://github.com/rstreamlabs/rstream-go) provides the reference rstream command and shared context setup.

## Obtain the tools

Use the public [rstream downloads](https://rstream.io/download) for the
`rstream-utils` distribution matching the host platform. For a source build,
follow [Build this checkout](001-building-from-source.md); its native Debug
configuration includes the tools, under `build/native/build/Debug/bin/`.
Run each program with `--help` for its current arguments. The Go `rstream` CLI
is a separate program used for login and shared context setup.

| Tool | Detailed usage |
| --- | --- |
| `rstream-tunnel` | The examples below and [URI reference](004-uri-model.md). |
| `rstream-webtty-client` | [Transport, trust and client identity](../bin/webtty/bin/client/README.md). |
| `rstream-webtty-server` | [Enrollment, authorization and execution account](../bin/webtty/bin/server/README.md). |
| `rstream-ncat`, `rstream-nperf-client`, `rstream-nperf-server` | `--help`; these use the same native stream/URI model. |
| `rstream-inspect` | Debug builds only; `--help` describes version and plugin inspection. |
| `rstream-gping`, `rstream-runpy` | Require Python bindings in the source build. |

## Programs

This repository also builds native tools that reuse the same transport and tunnel primitives:

`rstream-tunnel` is the C++ alternative to `rstream forward` for tunnel creation and forwarding workflows.

`rstream-ncat` provides netcat-like capabilities with explicit compatibility for rstream tunnel transport paths.

`rstream-nperf-client` and `rstream-nperf-server` provide network performance testing utilities.

`rstream-webtty-client` and `rstream-webtty-server` implement **rstream WebTTY** in C++.

`rstream-webtty-server -v --rstream` serves WebTTY through an rstream tunnel URI with the standard discovery labels, including `application-protocol=rstream.webtty`, `rstream.webtty.capabilities=exec`, and `rstream.webtty.exec.path=/`. Use `--uri` for a direct local listener. The C++ tools expose the SDK URI model in their command line, so the same argument can describe a TCP listener or an `rstrm://` endpoint; the higher-level `rstream webtty` workflow separates listener selection and registered-server resolution.

The WebTTY protobuf contract includes endpoint identities and payload crypto metadata for encrypted sessions. The C++ SDK exposes E2E payload crypto helpers for the nominal suite: AES-256-GCM for stdin/stdout/stderr payloads with a fresh 96-bit nonce per WebTTY data message, and HPKE Base with DHKEM(X25519, HKDF-SHA256), HKDF-SHA256, and AES-256-GCM for wrapping per-session payload keys. Endpoint-authenticated sessions also use ECDSA P-256 with SHA-256 for `ServerHello` and `ClientProof` transcripts. The protobuf enum reserves ChaCha20-Poly1305 identifiers for future negotiation. Current C++ helpers do not accept those suites and fail closed if they are requested.

Managed attach and control transfer are engine-coordinated capabilities. The
C++ WebTTY server accepts direct `Open` sessions and rejects `Attach` handshakes
with a clear protocol error instead of attempting local multi-user routing.

The client exposes `async_request_termination(token)` to request that the native
process stop while continuing to receive its final output. It checks the
server's advertised capability and reports `termination_unsupported` for an
older server. Completion of this operation confirms only the request write;
the `async_run` completion reports the native exit code. A local transport error
does not establish a remote exit code. `cancel()` remains a local disconnect.

Only one termination write may be pending. Overlapping requests report
`operation_in_progress`; a repeated request after a successful write completes
without writing again. The request uses the configured close timeout, capped at
30 seconds, or five seconds when unset. Cancellation before the write leaves
the reader connected; cancellation during a write closes the transport because
a partial protocol frame cannot be retried safely. The completion token's
executor, allocator and cancellation slot are preserved.

Native termination targets the owned Unix process group or Windows Job Object.
Unix pipes receive SIGINT and PTYs receive SIGHUP before bounded escalation to
SIGKILL. Windows terminates the job and reports the actual process status.
This ownership does not create a Unix sandbox for descendants that deliberately
leave the process group. Managed readers must also check the Engine's
`explicit_termination` capability and obtain control before requesting a stop.

On Unix, child commands inherit only their redirected standard input, output
and error. Other open descriptors in the server are closed in the child before
execution, for both pipe and PTY sessions. This keeps a command from retaining
another session's pipes, client connections or the persistent server's listener.
Stopping or disconnecting a session leaves other sessions and new admission
available on that server.

`rstream-runpy` and `rstream-gping` are available when Python bindings are enabled, and `rstream-inspect` is built in Debug configurations.

## rstream-tunnel command examples

The `rstream-tunnel` CLI uses the same tunnel model as the SDK. The examples below are practical starting points for local forwarding, private tunnels, and edge policy options.
When neither `--publish` nor `--no-publish` is provided, the project policy decides the final exposure mode. On projects where public access is forbidden, the tunnel falls back to a private tunnel instead of sending public-only defaults.

To publish a local HTTP service:

```bash
rstream-tunnel 127.0.0.1:8080 --http --publish
```

To publish an SSH server through a raw TCP tunnel:

```bash
rstream-tunnel 127.0.0.1:22 --tcp
```

Use `--tcp-port` with a port reserved for the project. Published TCP does not secure the downstream connection, so use it only with protocols such as SSH that provide their own encryption and authentication.

For a global project, `--allow-cross-region-routing` permits the owner region to carry downstream traffic when ingress is elsewhere. To pin the control channel to a known regional engine without querying the control plane, pass its full address through `--engine`.

To expose a private TLS tunnel with a stable tunnel name:

```bash
rstream-tunnel 127.0.0.1:22 --tls --no-publish --name ssh-private
```

To enable edge authentication and basic network policy controls on an HTTP tunnel:

```bash
rstream-tunnel 127.0.0.1:8080 --http --token-auth --challenge-mode --trusted-ips 203.0.113.0/24,198.51.100.12/32 --geoip FR,US
```

To configure terminated TLS publication with mTLS and explicit cipher policy:

```bash
rstream-tunnel 127.0.0.1:8080 --tls --tls-mode terminated --tls-min-version tls1.2 --tls-ciphers TLS_AES_128_GCM_SHA256,TLS_AES_256_GCM_SHA384 --mtls --label env=prod --label app=api
```

Here the local service on port 8080 receives plaintext after edge TLS
termination. For a TLS-speaking upstream, configure `--upstream-tls` and the
appropriate trust settings instead. [TLS layers](008-http-tls-websocket.md) are
independent of the agent connection.

mTLS Tunnel access uses mTLS credentials to decide which client certificates are allowed.

To publish a passthrough TLS tunnel, keep edge TLS policy options off so the upstream service owns the certificate and handshake:

```bash
rstream-tunnel 127.0.0.1:8443 --tls --tls-mode passthrough
```

