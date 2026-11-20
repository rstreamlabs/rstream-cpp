# HTTP, TLS and WebSocket with Boost.Beast

A Beast session reads and writes an asynchronous stream. An rstream socket
can supply that stream while your application keeps the HTTP handler and the
Asio executor. Choose the published protocol for the caller before choosing
where the application handles TLS.

| What callers use | What the C++ application handles |
| --- | --- |
| HTTPS with a [published HTTP tunnel](https://rstream.io/docs/tunnels/http-tunnels) | The shared Beast handler reads HTTP/1.1 after edge TLS termination. |
| TLS with a [published TLS tunnel](beast/000-tls.md) | The Engine can terminate TLS, or the application can present its certificate and handle TLS itself. |
| WSS with a [classic WebSocket upgrade](beast/001-websocket.md) | A published HTTP tunnel with an HTTP/1.1 upstream carries the upgrade to a Beast WebSocket session. |

The outbound agent connection verifies the Engine independently of these
choices. Use [SDK configuration](002-configuration.md#trust-the-engine)
for that certificate and [editions and plans](https://rstream.io/docs/support/editions)
for protocol availability.

## Move a Beast service from local TCP to a tunnel

Build the [two migration targets](../example/io-rstrm/README.md#build-the-migration-pair).
Both entry points call the same
[Beast handler](../example/io-rstrm/http-handler.hpp). It reads one HTTP
request, writes the response and closes its stream.

Run the [ordinary TCP listener](../example/io-rstrm/beast-local.cpp) first.

```sh
rstream-example-beast-local 8080
```

In a second terminal, check the response. Use `curl.exe` on Windows.

```sh
curl -fsS http://127.0.0.1:8080/
```

Expect `Hello from the shared Beast handler`. Then stop the local listener
with Ctrl+C. Its accept loop hands a normal TCP socket to the handler.

```cpp
auto socket = co_await acceptor.async_accept(asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

Select a [context](002-configuration.md) with permission to publish HTTP.
Start the [tunneled listener](../example/io-rstrm/beast-tunnel.cpp).

```sh
rstream-example-beast-tunnel
```

Open the printed HTTPS address from a second terminal or browser. The
same handler returns the same response. Use the [required caller credential](https://rstream.io/docs/tunnels/http-tunnels/authentication#token-authentication) if the project requires it. For an Engine with a private CA,
pass that CA to curl with `--cacert`. Use its printed hostname and port
so curl can verify the certificate. Stop the listener with Ctrl+C.

The tunneled listener first creates a control connection and the published
tunnel. It accepts an rstream socket on the application's executor.

```cpp
rstrm::client client(executor);
co_await client.async_connect(asio::use_awaitable);

rstrm::tunnel_properties properties{};
properties.m_publish = true;
properties.m_protocol = rstrm::protocol::http;
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);

rstrm::socket socket(executor);
rstrm::endpoint peer;
co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

When adapting your own project, [link its CMake target](000-getting-started.md#link-an-existing-cmake-target).
Keep the handler compatible with the stream it receives. The complete
checkout example shares `http-handler.hpp` and uses the
[tunnel runner](../example/io-rstrm/tunnel-server.hpp) to drive the event
loop and close the client on shutdown. Use [tunnel patterns](003-tunnel-patterns.md)
to choose the direct API, an Asio-style acceptor or a URI-selected stream.

## Choose where the service protocol terminates

The [TLS guide](beast/000-tls.md) explains which endpoint presents a
certificate and how to verify each connection. The
[WebSocket guide](beast/001-websocket.md) verifies upgrade, echo and close
over the tunnel.
