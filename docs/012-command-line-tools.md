# C++ command-line tools

These programs are built from the C++ SDK and distributed as `rstream-utils`. The [Go CLI](https://github.com/rstreamlabs/rstream-go) provides the reference rstream command and shared context setup.

## Obtain the tools

Select `rstream-utils` in the public installer to obtain the native
executables. On macOS and Linux:

```sh
curl -fsSL https://rstream.io/scripts/install.sh -o rstream-install.sh
PACKAGE_NAME=rstream-utils PACKAGE_PREFIX="$HOME/.rstream-utils" sh rstream-install.sh
export PATH="$HOME/.rstream-utils/bin:$PATH"
```

In Windows PowerShell:

```powershell
$env:PACKAGE_NAME = 'rstream-utils'
Invoke-RestMethod 'https://rstream.io/scripts/install.ps1' | Invoke-Expression
Remove-Item Env:PACKAGE_NAME
```

On macOS and Linux, add the same path entry to your shell profile if you
want it available in new terminals. Run `rstream-tunnel --version` or `rstream-webtty-client --version` to
verify which executable starts. The [rstream CLI](https://rstream.io/docs/getting-started/installation)
sets up login and shared project contexts.

For a [source build](001-building-from-source.md), find the tools in their
target subdirectories on macOS and Linux, such as `bin/tunnel/bin/` inside
the build directory. An MSVC build places them in its `bin/`. Use
`--help` to check the flags accepted by each executable.

| Tool                                                           | Detailed usage                                                                         |
| -------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| `rstream-tunnel`                                               | The examples below and [URI reference](011-uri-reference.md).                          |
| `rstream-webtty-client`                                        | [Native WebTTY walkthrough](013-webtty.md) and [client reference](../bin/webtty/bin/client/README.md).            |
| `rstream-webtty-server`                                        | [Server reference](../bin/webtty/bin/server/README.md). |
| `rstream-ncat`, `rstream-nperf-client`, `rstream-nperf-server` | `--help`; these use the same native stream/URI model.                                  |
| `rstream-inspect`                                              | Debug builds only; `--help` describes version and plugin inspection.                   |
| `rstream-gping`, `rstream-runpy`                               | Require Python bindings in the source build.                                           |

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
appropriate trust settings instead. [TLS layers](005-http-tls-websocket.md) are
independent of the agent connection.

For client certificate admission, use the [published mTLS policy](https://rstream.io/docs/security/mtls#published-tunnel-access).

To publish a passthrough TLS tunnel, keep edge TLS policy options off so the upstream service owns the certificate and handshake:

```bash
rstream-tunnel 127.0.0.1:8443 --tls --tls-mode passthrough
```
