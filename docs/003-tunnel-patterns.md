# Choose a C++ integration model

The application owns its `io_context`, executor and threads. rstream operations
use the caller's event loop; the SDK is not a replacement event loop. Start
with [a published-package application](000-getting-started.md), or [build this
checkout](001-building-from-source.md) to run the maintained programs below.

## Choose the API that fits the application

| Model | Use it when | Ownership and reconnect |
| --- | --- | --- |
| `io_rstrm::client`, `tunnel`, `socket` | The application wants explicit control over engine connection, tunnel creation and private dialing. | Keep the control client/tunnel alive while accepting. Close the client explicitly at shutdown. The program decides how to handle disconnect and recreate tunnels. |
| `io_rstrm::acceptor` | Existing code expects an acceptor and the application supplies typed tunnel settings. | It manages its control client and can reconnect/recreate the tunnel, according to `settings_acceptor`. Close the acceptor to stop it. |
| `io::stream` with URI/plugins | A service should select TCP, TLS or rstream through configuration while retaining Asio read/write code. | The URI selects the plugin and its acceptor/socket behavior. Preserve the runtime plugin layout in dynamic builds. |

A direct `socket::async_connect(endpoint, token)` opens a private tunnel stream;
it does not require a separate control `client` in the dialing process. Its
endpoint names the tunnel and engine. The control client in the private-echo
example exists to create the tunnel being dialed.

All three approaches are asynchronous. The socket is not a drop-in synchronous
TCP API. Keep each object and its referenced buffers/endpoints alive until its
operations complete. Copying a direct rstream socket shares its implementation;
closing a copy affects that same connection. Keep application state serialized
on its executor/strand when running the loop from several threads. Do not infer
that the socket's internal serialization makes arbitrary application state or
concurrent Beast operations safe.

## Callbacks, futures or coroutines

These are independent programs, each establishing one control connection and
creating one private tunnel before closing it. They do not initiate overlapping
connects on the same client.

| Style | Complete program | What the application supplies |
| --- | --- | --- |
| Callback | [`callback.cpp`](../example/io-rstrm/callback.cpp) | A shared owner retained by its handlers, an `io_context`, and an error result. |
| Future | [`future.cpp`](../example/io-rstrm/future.cpp) | A worker running the loop, a work guard, `get()` on the calling thread, and a joined worker at shutdown. |
| Coroutine | [`private-echo.cpp`](../example/io-rstrm/private-echo.cpp) | `co_spawn`, observed exceptions, buffers living in coroutine frames, and explicit cleanup. |

A future does not run its executor. Never call `get()` on the only thread
running the event loop: that prevents the operation from completing. Coroutine
syntax also leaves the event loop and object lifetimes with the caller.

The completion handler's executor, allocator and cancellation slot remain
associated with SDK operations, including move-only handlers. Use Asio's
`bind_executor`, `bind_allocator` and `bind_cancellation_slot` when those choices
belong to a particular operation. Cancellation completes the operation; keep
its buffers alive until that completion. A request to close a socket is not a
synchronous barrier proving physical network shutdown.

## Private accept and dial

Run `rstream-example-private-echo` after selecting a [context](002-configuration.md).
It creates a private `echo` tunnel, accepts and dials it in the same process,
prints `Echo: hello`, closes the tunnel and control client, and exits. The two
halves are awaited together so an error cancels its peer; a ten-second deadline
bounds the exchange. Its completion reports an error through a nonzero exit
status. A private name must be available in the selected project.

The central steps are:

```cpp
rstrm::tunnel_properties properties = {
    .m_name = std::string("echo"),
    .m_publish = false,
    .m_protocol = rstrm::protocol::tls,
};
auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
```

The `tls` tunnel protocol label here does not perform an application TLS
handshake inside the private echo handler. The engine connection is verified
TLS; this handler exchanges raw bytes. [TLS layers](008-http-tls-websocket.md)
explains the service-side distinction.

`tunnel.close()` removes the tunnel; it does not close the control client.
Call `client.close()` when that client is no longer needed. Let the event loop
drain pending completions. The long-running server examples stop their loop on
an interrupt; that is process shutdown, not a graceful drain of every session.

## Published TCP tunnels

Set `m_protocol` to `rstrm::protocol::tcp` for a raw published TCP bytestream.
Leave `m_port` unset for an ephemeral address, or use a port already reserved
by the project through the Control plane:

```cpp
rstrm::tunnel_properties properties = {
    .m_publish = true,
    .m_protocol = rstrm::protocol::tcp,
    .m_port = 10042,
};
```

The SDK does not reserve the port. Published TCP does not add downstream
application encryption or authentication; use a secure protocol such as SSH.
A TLS engine connection alone does not secure an ordinary client's raw public
TCP connection.

```sh
rstream-tunnel 127.0.0.1:22 --tcp
rstream-tunnel 127.0.0.1:22 --tcp --tcp-port 10042
```

For a global project, `--allow-cross-region-routing` permits ingress in another
region to route through the tunnel's owner region. The SDK does not query the
Control plane to resolve a region name. When the regional engine address is
already known, select it explicitly:

```sh
rstream-tunnel 127.0.0.1:22 --tcp \
  --engine 'tcp://regional-engine.example.com:443?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1'
```

Library callers can pass that engine address to `make_endpoint`. The engine
still validates that the project may use the region.

## URI-configured HTTP handler

`rstream-example-uri-http-server` uses `io::stream::resolver`, `acceptor` and
`socket`. The same HTTP handler can accept a local TCP connection or create an
rstream listener by changing its URI:

```sh
rstream-example-uri-http-server --uri '127.0.0.1:8080'
rstream-example-uri-http-server --uri 'rstrm://?rstrm.publish=true&rstrm.protocol=http&rstream.retry=true'
```

The engine and credentials come from the selected context. `rstream.retry`
belongs to the rstream acceptor; adding it to an ordinary `tcp://` listener does
not create a tunnel or reconnect an rstream client. See the [URI reference](004-uri-model.md)
for namespaces, nested engine overrides and credential restrictions.
