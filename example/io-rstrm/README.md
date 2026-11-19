# Native Asio integration programs

Use [Build this checkout](../../docs/001-building-from-source.md) and select a
[context](../../docs/002-configuration.md). The application supplies the
`io_context`, executor and threads; these programs show the corresponding
object lifetimes and error paths. They need a permitted project/engine to make
real tunnels. Compilation and the loopback library tests need no credentials.

| Target | Source | Demonstrates |
| --- | --- | --- |
| `rstream-example-beast-local` | [beast-local.cpp](beast-local.cpp) | Ordinary Boost.Asio/Beast local listener, with no SDK dependency. |
| `rstream-example-beast-tunnel` | [beast-tunnel.cpp](beast-tunnel.cpp) | The rstream listener using exactly the same [templated HTTP handler](http-handler.hpp). |
| `rstream-example-http-server` | [http-server.cpp](http-server.cpp) | Direct client/tunnel/socket with a Beast HTTP/1.1 handler and public HTTPS endpoint. Uses the existing HTTP API shown in the 1.15.1 application guide. |
| `rstream-example-tls-http-server` | [tls-http-server.cpp](tls-http-server.cpp) | Application-owned TLS certificate and handshake over a passthrough tunnel. Pass certificate-chain and key PEM paths. |
| `rstream-example-websocket-server` | [websocket-server.cpp](websocket-server.cpp) | Beast upgrade, bounded messages and explicit socket close using the new optional source-candidate adapter. |
| `rstream-example-private-echo` | [private-echo.cpp](private-echo.cpp) | Private accept/dial, observed errors, ten-second exchange deadline and client shutdown. |
| `rstream-example-callback` | [callback.cpp](callback.cpp) | Callback owner retained until completion; one connection and one private tunnel. |
| `rstream-example-future` | [future.cpp](future.cpp) | Application-created loop worker; future waits on the caller; close and join. |
| `rstream-example-uri-http-server` | [generic HTTP server](../io/http/example_io_http_server.cpp) | Runtime-selected TCP or rstream acceptor/socket through `--uri`. |

After `cmake --build --preset conan-debug`, these targets are under
`build/native/build/Debug/example/io-rstrm/`. The HTTP/TLS/WebSocket servers
print a forwarding address and run until an interrupt. The callback/future
programs print the private endpoint and exit; the echo prints `Echo: hello`.
Failures return a nonzero status in the direct examples. Interrupting a server
stops its event loop; it does not promise a graceful drain of every session.

See [integration choices](../../docs/003-tunnel-patterns.md) and
[HTTP/TLS/WebSocket layers](../../docs/008-http-tls-websocket.md) before adapting
a program. The older [client load example](client/example_io_rstrm_client.cpp)
is a configurable multi-tunnel exercise, rather than the first application.
