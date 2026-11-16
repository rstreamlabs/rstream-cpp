# HTTP, TLS and Boost.Beast WebSocket

The SDK carries asynchronous bytestreams. Boost.Beast can parse HTTP/1.1 on
those streams; TLS termination and the public protocol are separate choices.
This guide refers to complete [source programs](../example/io-rstrm/README.md),
built through the [checkout guide](001-building-from-source.md).

## Move a Beast service from local TCP to a tunnel

The migration example keeps one handler in
[`http-handler.hpp`](../example/io-rstrm/http-handler.hpp). It is templated on
`AsyncStream`; the HTTP read, response construction, write and explicit close
are identical for an ordinary `asio::ip::tcp::socket` and an
`io_rstrm::socket`. The two complete entry points can be built and run:

```sh
rstream-example-beast-local 8080
curl --fail http://127.0.0.1:8080/
# Prints: Hello from the shared Beast handler

# After selecting a permitted project/context:
rstream-example-beast-tunnel
curl --fail https://YOUR-FORWARDING-HOST/
# The same handler returns the same body.
```

The [local entry point](../example/io-rstrm/beast-local.cpp) is plain
Boost.Asio/Beast and has no SDK dependency. Its listener accepts a TCP socket:

```cpp
tcp::acceptor acceptor(executor, tcp::endpoint(asio::ip::address_v4::loopback(), port));
auto socket = co_await acceptor.async_accept(asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

The [tunnel entry point](../example/io-rstrm/beast-tunnel.cpp) must first create
an authenticated outbound control connection and a permitted published tunnel.
That setup belongs to the listener, not to the request handler:

```cpp
rstrm::client client(executor);  // Snapshots the selected context and credentials.
co_await client.async_connect(asio::use_awaitable);
rstrm::tunnel_properties properties = {.m_publish = true, .m_protocol = rstrm::protocol::http};
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);

rstrm::socket socket(executor);
rstrm::endpoint peer;
co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
```

Only the accept/socket path changes at the call to the shared handler. The
whole integration also includes [agent configuration](002-configuration.md),
project authorization, control/tunnel ownership, forwarding-address handling
and shutdown. Both entry points use the application's existing event loop.

For a standalone local build, the essentials are a C++20 target and Boost:

```cmake
find_package(Boost CONFIG REQUIRED)
find_package(Threads REQUIRED)
add_executable(service beast-local.cpp)
target_compile_features(service PRIVATE cxx_std_20)
target_link_libraries(service PRIVATE Boost::boost Threads::Threads)
```

For the tunnel build, use the [published SDK Conan recipe](000-getting-started.md)
and change the application target to:

```cmake
find_package(rstream CONFIG REQUIRED)
add_executable(service beast-tunnel.cpp)
target_link_libraries(service PRIVATE rstream::rstream)
rstream_enable_runtime_plugins(service rstream::plugin::io-generic rstream::plugin::io-rstrm)
```

Copy `http-handler.hpp` beside either main when building it outside the checkout.
For a long-running service, add session admission and shutdown appropriate to
the application. The two entry points share their request handling and response.

## Choose where the service protocol terminates

| Service                      | Ordinary client to edge                                            | Handler or upstream sees                                             | Certificate owner                                                                 |
| ---------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| Published HTTP tunnel        | HTTPS managed by the edge.                                         | HTTP bytes by default; the Beast example serves HTTP/1.1.            | Edge for the public HTTPS certificate.                                            |
| Published TLS, `terminated`  | TLS terminated at the edge, with the configured edge policy.       | Clear application bytes unless upstream TLS is explicitly requested. | Edge for the public certificate; upstream owns a separate certificate if enabled. |
| Published TLS, `passthrough` | TLS handshake passes through the edge.                             | The original TLS handshake and encrypted application data.           | The application owns the certificate and TLS server.                              |
| Private raw stream           | Authenticated rstream dial instead of a public forwarding address. | The bytes exchanged by the two applications.                         | The engine TLS identity is independent of any application TLS.                    |

The outbound agent-to-engine connection uses its own verified TLS certificate
and credentials in all these normal configurations. `ssl.*` settings on its
engine URI configure that connection. `rstrm.tls_*` sets the published service
policy. `rstrm.upstream_tls` selects TLS on the application side; it does not
replace either certificate identity. Use [configuration](002-configuration.md#trust-the-engine)
for an engine private CA.

TLS passthrough cannot use edge TLS termination options or `--upstream-tls`.
The application must provide the handshake. Raw published TCP is also available,
but adds no downstream encryption or authentication by itself. Datagram, QUIC
runtime transport and WebTransport are not implemented by this C++ runtime.

## Serve HTTP in process

[`http-server.cpp`](../example/io-rstrm/http-server.cpp) creates a published HTTP
tunnel, prints its forwarding address and uses Beast `http::async_read` and
`http::async_write` on a direct `io_rstrm::socket`. Run
`rstream-example-http-server`, then open its printed HTTPS address. The handler
reads clear HTTP bytes after edge termination. It serves one request per
connection and explicitly advertises connection close.

This example serves one HTTP/1.1 request per connection. Extend its parser limits,
deadlines and session admission when adding request bodies, keep-alive, routing
or streaming. Select an application parser matching the configured upstream protocol.

## Own the TLS service handshake

[`tls-http-server.cpp`](../example/io-rstrm/tls-http-server.cpp) creates a
published TLS passthrough tunnel. Its handler wraps the accepted socket in
`asio::ssl::stream`, performs a TLS server handshake, serves one HTTP/1.1 request
and attempts TLS shutdown before closing the underlying socket. It sets an
application minimum of TLS 1.2. The program loads the certificate/key before
running its event loop:

```sh
rstream-example-tls-http-server /etc/my-service/chain.pem /etc/my-service/key.pem
```

The certificate must cover the forwarding hostname the client uses. Open that
hostname with `https://`; clients verify the application's chain and identity.
For local testing use a locally issued certificate and pass its CA to the test
client (for example `curl --cacert test-ca.pem https://HOSTNAME/`). Do not bypass
certificate verification. The engine connection still uses its separate trust
configuration. No mTLS requirement is added to the application TLS server by
this example; add client-certificate policy when the application requires it.

For edge-terminated TLS forwarding, a local plaintext service can instead use
`rstream-tunnel 127.0.0.1:8080 --tls --tls-mode terminated --publish`. A
TLS-speaking local upstream needs `--upstream-tls` and appropriate trust and
identity settings; port 8443 alone does not enable TLS.

## Serve a WebSocket with Beast

Include `rstream/io-rstrm/beast.hpp` to adapt a direct rstream socket to Beast's
WebSocket teardown and timeout hooks. The complete
[`websocket-server.cpp`](../example/io-rstrm/websocket-server.cpp) program is built
with the checkout examples.

The WebSocket program publishes an HTTP tunnel and wraps each accepted direct
socket in `boost::beast::websocket::stream<io_rstrm::socket>`. It performs an
upgrade, echoes text/binary messages up to 64 KiB and applies Beast's suggested
server handshake/idle timeouts. Run `rstream-example-websocket-server`, open its
printed address's origin in a browser, and use its `wss://` equivalent:

```javascript
const ws = new WebSocket("wss://YOUR-FORWARDING-HOST/");
ws.onopen = () => ws.send("hello");
ws.onmessage = ({ data }) => {
  console.log(data);
  ws.close(1000);
};
```

The handler processes the peer's close handshake, then explicitly closes
`ws.next_layer()`. The adapter uses the existing rstream generic-stream teardown
contract: completion is posted without an extra transport handshake, preserving
the handler's executor, allocator and cancellation association. This completion
is not proof of a physical transport shutdown. Beast timeouts call the SDK's
ADL `beast_close_socket` hook. Keep all WebSocket operations and buffers alive
until completion, and serialize the application session as Beast requires.

The echo server has no application authorization or origin allowlist. Use the
project/tunnel access policy and the service's own authorization/origin policy
before adopting it for an application. WebTTY's authenticated WebSocket server
is a separate protocol, not a substitute for an application WebSocket handler.
