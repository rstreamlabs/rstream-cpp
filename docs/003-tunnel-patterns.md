# Tunnel patterns

Choose who can reach the service, then how the application owns it. A
published tunnel gives callers a forwarding address. A private tunnel is
dialed by an authenticated rstream client using the tunnel name or ID. Both
carry bytestreams in this SDK. The connection from the agent to the Engine
uses TLS independently of the application's protocol.

| Access | When to choose it |
| --- | --- |
| Published HTTP, TLS or TCP | The caller uses an ordinary HTTP, TLS or TCP client. Protocol and access policy are applied by the Engine for the selected tunnel. |
| Private | The caller can dial through rstream. Hosted deployments require a project that allows private tunnels. Self-hosted Community Edition supports private bytestreams. |

See [editions and plans](https://rstream.io/docs/support/editions) for
published protocol availability, and [Beast service protocols](005-http-tls-websocket.md)
for certificate ownership and HTTP upstreams.

## Direct API, acceptor or URI

| Interface                              | Application model                                                                        | Connection ownership                                                                                                         |
| -------------------------------------- | ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `io_rstrm::client`, `tunnel`, `socket` | Create a control connection and tunnels explicitly; dial private streams.                | The application closes its sockets, tunnels and control client, and recreates them after disconnection.                      |
| `io_rstrm::acceptor`                   | Supply typed tunnel settings to code already built around an Asio acceptor.              | The acceptor owns its control connection. Its settings select automatic reconnect and tunnel recreation; `close()` stops it. |
| `io::stream` with URI plugins          | Select TCP, TLS or rstream through configuration while keeping the same read/write code. | The selected plugin supplies the socket/acceptor behavior. Package its runtime modules with the application.                 |

The [Beast walkthrough](005-http-tls-websocket.md) shows the direct API beside
an ordinary TCP listener, both using the same HTTP handler. The
[URI reference](011-uri-reference.md) covers configuration-driven selection.

## Private accept and dial

The process exposing a private service creates a tunnel with `m_publish=false`.
A peer uses `socket::async_connect(endpoint, completion_token)` to dial it by name or ID.
That dial opens a stream directly; the dialing process only needs a control
client when it also creates or manages tunnels.

The complete [private echo](../example/io-rstrm/private-echo.cpp) creates a
private tunnel named `echo`, accepts and dials it in one process, exchanges
`hello` and prints `Echo: hello`. Its two sides cancel together on error and a
ten-second deadline bounds the exchange. Select a [context](002-configuration.md)
and run `rstream-example-private-echo` from the source build.

```cpp
rstrm::tunnel_properties properties = {
    .m_name = std::string("echo"),
    .m_publish = false,
    .m_protocol = rstrm::protocol::tls,
};
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
```

Here `tls` selects a raw private stream with optional upstream TLS settings.
The echo application exchanges plain bytes over its authenticated engine
connection. Use an application TLS handshake when the service requires one;
[TLS layers](005-http-tls-websocket.md#choose-where-the-service-protocol-terminates)
explains where each certificate belongs.

## Published TCP

Use `m_protocol=rstrm::protocol::tcp` for an ordinary client connecting to a
public TCP port. Leave `m_port` unset for an ephemeral port, or request a port
already reserved by the project:

```cpp
rstrm::tunnel_properties properties = {
    .m_publish = true,
    .m_protocol = rstrm::protocol::tcp,
    .m_port = 10042,
};
```

The upstream application supplies downstream encryption and authentication,
for example through SSH. Port reservations are managed through the Control plane.

```sh
rstream-tunnel 127.0.0.1:22 --tcp
rstream-tunnel 127.0.0.1:22 --tcp --tcp-port 10042
```

For global projects, `--allow-cross-region-routing` permits ingress in another
region to reach the tunnel's owner region. To create in a particular region,
provide its known engine address through `--engine` or `make_endpoint`; the
engine enforces the project's region policy.

## URI-configured listeners

`rstream-example-uri-http-server` uses the same `io::stream` HTTP code with a
local listener or an rstream listener:

```sh
rstream-example-uri-http-server --uri '127.0.0.1:8080'
rstream-example-uri-http-server --uri 'rstrm://?rstrm.publish=true&rstrm.protocol=http&rstream.retry=true'
```

The selected context supplies engine and credentials. `rstream.retry` controls
the rstream acceptor's recovery. Compare [completion styles](004-completion-models.md) to choose how the application drives these operations.
