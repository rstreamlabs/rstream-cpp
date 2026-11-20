# Native Asio integration programs

The application supplies the Asio event loop and owns the client, tunnel
and socket lifetimes. Start with the pair that keeps one Beast handler
while replacing the listener.

## Prepare the example binaries

[Configure the native checkout](../../docs/001-building-from-source.md#configure-a-native-checkout)
and run `cmake --preset conan-debug` from its root. In each terminal where
you run an example, add its directory to the path.

On macOS or Linux:

```sh
export PATH="$PWD/build/native/build/Debug/example/io-rstrm:$PATH"
```

On Windows with the detected MSVC profile:

```powershell
$env:Path = "$(Resolve-Path .\build\native\build\bin);$env:Path"
```

The local Beast listener uses loopback and needs no Engine. Before running
a tunnel example, [select a context](../../docs/002-configuration.md) with
permission to create the corresponding tunnel.

## Build the migration pair

From the configured checkout, build the two listeners.

```sh
cmake --build --preset conan-debug --target rstream-example-beast-local rstream-example-beast-tunnel
```

For a different example, replace those targets with its target below.

## Choose an example

| Target                             | Source                                                       | Demonstrates                                                                                                         |
| ---------------------------------- | ------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------- |
| `rstream-example-beast-local`      | [beast-local.cpp](beast-local.cpp)                           | Ordinary Boost.Asio/Beast local listener, with no SDK dependency.                                                    |
| `rstream-example-beast-tunnel`     | [beast-tunnel.cpp](beast-tunnel.cpp)                         | The rstream listener using exactly the same [templated HTTP handler](http-handler.hpp).                              |
| `rstream-example-http-server`      | [http-server.cpp](http-server.cpp)                           | Direct client/tunnel/socket with a Beast HTTP/1.1 handler and public HTTPS endpoint.                                 |
| `rstream-example-tls-http-server`  | [tls-http-server.cpp](tls-http-server.cpp)                   | Application-owned TLS handshake. Pass certificate, key and the requested hostname as shown in the [TLS guide](../../docs/beast/000-tls.md). |
| `rstream-example-websocket-server` | [websocket-server.cpp](websocket-server.cpp)                 | Beast upgrade, bounded messages and explicit socket close using the optional Beast adapter.                          |
| `rstream-example-private-echo`     | [private-echo.cpp](private-echo.cpp)                         | Private accept/dial, observed errors, ten-second exchange deadline and client shutdown.                              |
| `rstream-example-callback`         | [callback.cpp](callback.cpp)                                 | Callback owner retained until completion; one connection and one private tunnel.                                     |
| `rstream-example-coroutine`        | [coroutine.cpp](coroutine.cpp)                               | The same private tunnel lifecycle using `co_await` on the application loop.                                          |
| `rstream-example-future`           | [future.cpp](future.cpp)                                     | Application-created loop worker; future waits on the caller; close and join.                                         |
| `rstream-example-uri-http-server`  | [generic HTTP server](../io/http/example_io_http_server.cpp) | Runtime-selected TCP or rstream acceptor/socket through `--uri`.                                                     |

The HTTP, TLS and WebSocket servers print a forwarding address and run
until interrupted. The callback, future and coroutine examples print the
private endpoint, close it and exit. The private echo example prints
`Echo: hello`. Startup and one-shot exchange failures return a nonzero status.

See [tunnel patterns](../../docs/003-tunnel-patterns.md) and
[HTTP/TLS/WebSocket layers](../../docs/005-http-tls-websocket.md) before adapting
a program. The older [client load example](client/example_io_rstrm_client.cpp)
is a configurable multi-tunnel exercise, rather than the first application.
