# C++ command-line tools

The native tools forward services, relay byte streams, measure a connection
and open terminals. They share [connection configuration](002-configuration.md)
with C++ applications. Use the [rstream CLI](https://rstream.io/docs/getting-started/installation)
for browser login and project selection.

## Obtain the tools

Select `rstream-utils` in the public installer. On macOS and Linux:

```sh
curl -fsSL https://rstream.io/scripts/install.sh -o rstream-install.sh
PACKAGE_NAME=rstream-utils PACKAGE_PREFIX="$HOME/.rstream-utils" sh rstream-install.sh
export PATH="$HOME/.rstream-utils/bin:$PATH"
rstream-tunnel --version
```

To use the tools in new terminals, add the same path entry to your shell
profile. In Windows PowerShell:

```powershell
$env:PACKAGE_NAME = 'rstream-utils'
Invoke-RestMethod 'https://rstream.io/scripts/install.ps1' | Invoke-Expression
Remove-Item Env:PACKAGE_NAME
rstream-tunnel --version
```

From a [configured source checkout](001-building-from-source.md), build the
tool by its executable name. For example:

```sh
cmake --build --preset conan-debug --target rstream-tunnel rstream-ncat
```

In a macOS or Linux build, the two executables are under `bin/tunnel/bin`
and `bin/ncat/bin` relative to the build directory. In an MSVC build,
use the build directory's `bin/`. Add the relevant directory to your
terminal path or invoke the executable by its full path.

## Choose a tool

| Task | Tool and guide |
| --- | --- |
| Publish a local service or expose it privately | `rstream-tunnel` and [Forward a service](tools/000-forwarding.md). |
| Dial a private stream, relay it to a local port or connect a pipeline | `rstream-ncat` and [Connect and relay streams](tools/001-streams.md). |
| Observe status, measure latency or inspect plugins | `rstream-nperf-client`, `rstream-nperf-server`, the Debug `rstream-inspect`, and [Measure and diagnose](tools/002-measurement.md). |
| Run an interactive terminal session | `rstream-webtty-client`, `rstream-webtty-server`, and [WebTTY](013-webtty.md). |

Use [Go datagram mode](https://rstream.io/docs/tunnels/private-tunnels#netcat-datagram-mode)
for UDP and rstream datagram tunnels. The native tools work with bytestreams.

## rstream-tunnel command examples

[Start with a local HTTP service](tools/000-forwarding.md#start-a-loopback-service),
then [publish it](tools/000-forwarding.md#publish-the-http-service) or
[dial it privately](tools/001-streams.md#reach-a-private-service).
[Forward a service](tools/000-forwarding.md) also covers public TLS, TCP,
protected HTTP and output interpretation.
