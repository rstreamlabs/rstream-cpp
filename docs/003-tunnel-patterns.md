# Tunnel patterns

The C++ SDK lets a process own a published or private bytestream tunnel.
Choose an interface based on who creates the tunnel and whether the
application chooses the transport in code or at startup.

## Direct API, acceptor or URI

| Choose | Use it when | Who owns the control connection? |
| --- | --- | --- |
| [Direct client and tunnel](#use-a-direct-client-and-tunnel) | The application creates named tunnels, reports their properties, or supervises several tunnels together. | The application owns the client and recreates its tunnels after control loss. |
| [Typed rstream acceptor](#use-a-typed-rstream-acceptor) | Existing Asio code accepts sockets and the listener should manage its own tunnel. | The acceptor connects on demand and can reconnect or recreate the tunnel. |
| [URI-selected stream](#uri-configured-listeners) | The same application should listen and dial over different transports selected by configuration. | The transport plugin owns the listener implementation. The rstream plugin uses the typed acceptor's lifecycle. |

A published tunnel has a forwarding address for an ordinary HTTP, TLS or TCP
caller. A private tunnel is dialed by an authenticated rstream client using
its name or ID. The engine connection uses verified TLS in either case. The
application protocol and its own encryption are separate choices. Check
[edition and project policy](https://rstream.io/docs/support/editions) before
selecting a protocol or publishing a service.

The examples use a [configured native build](../example/io-rstrm/README.md#prepare-the-example-binaries)
and, for tunnel runs, a [project or direct Engine context](002-configuration.md)
with the authority to create or dial the chosen tunnel. Use one terminal for
the listener and another for the request.

## Use a direct client and tunnel

Choose the direct API when creating a tunnel is an explicit step in your
service's lifecycle. The client authenticates to the Engine and opens a
control connection. Create one or more tunnels from that client. Each
tunnel provides its own properties and accepts its own bytestreams.

The [Beast application](../example/io-rstrm/beast-tunnel.cpp) connects,
publishes HTTP, and prints the forwarding address from the created tunnel.

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
```

Pass the accepted socket to your handler. Keep one outstanding accept per
tunnel and keep its output socket and endpoint alive until completion.
Read the forwarding address with
`format_forwarding_address(tunnel.properties())`. Then build and run the
[runnable migration pair](005-http-tls-websocket.md#move-a-beast-service-from-local-tcp-to-a-tunnel)
to verify the same HTTP response from a TCP listener and from a tunnel.

When supervising recovery, register the client's callbacks before
connecting. Reinstall them on reconnect and recreate the tunnels from the
new control generation. Existing tunnel IDs and accepts belong to the old
connection. For shutdown, stop accepting, close or drain the accepted
connections you own, then close the tunnels and client while the executor
continues to run. See [cancellation and recovery](008-cancellation-and-recovery.md)
for the boundary between cancelling an operation and closing its owner.

### Private accept and dial

Set `m_publish=false` and choose a name unique in the project. A caller
can use that name or the tunnel ID to resolve an endpoint using its own
context. A dial-only process needs a socket.

```cpp
auto endpoint = rstrm::make_endpoint(std::string("echo"), boost::none).value();
rstrm::socket socket(executor);
co_await socket.async_connect(endpoint, asio::use_awaitable);
```

The first argument is the tunnel name or ID. The second argument can supply
an explicit Engine address. Use a [full verified TLS URI](002-configuration.md#trust-the-engine)
with credentials for that Engine. Use its provisioned context or set the
socket's token with `settings_socket::m_config.m_token`. Passing just an
address still allows token selection from the environment or context.
The publisher needs permission to create a private tunnel
and the caller needs permission to dial it. A private raw stream carries
the application's own protocol. Enable TLS within that protocol when the
application requires end-to-end protection.

Build and run the [private echo example](../example/io-rstrm/private-echo.cpp).
It creates a private tunnel named `echo`, dials and accepts it on the
same context, exchanges a payload and closes.

```sh
cmake --build --preset conan-debug --target rstream-example-private-echo
rstream-example-private-echo
```

It prints `Echo: hello`. Its accept and dial cooperate within a bounded
operation so an error on one side cancels the other.

## Use a typed rstream acceptor

Construct an `io_rstrm::acceptor` from typed settings when the application
already has an accept loop and each listener should manage its own tunnel.
Bind an endpoint resolved from the context. Install status and tunnel
callbacks before accepting. The first `async_accept` opens the Engine
connection and creates the tunnel.

```cpp
rstrm::settings_acceptor settings;
settings.m_tunnel_properties.m_publish = true;
settings.m_tunnel_properties.m_protocol = rstrm::protocol::http;
settings.m_tunnel_properties.m_http_version = std::string("http/1.1");
rstrm::acceptor acceptor(executor, settings);

boost::system::error_code error;
acceptor.bind(rstrm::make_endpoint(boost::none, boost::none).value(), error);
if (error) {
  throw boost::system::system_error(error);
}
```

Use the tunnel properties callback to report the assigned forwarding address,
or listen for the `online` status. The local endpoint reflects the bound
input. At startup, the acceptor may still be connecting or waiting to create
the tunnel.

Build and run the [typed acceptor example](../example/io-rstrm/acceptor-http-server.cpp).
It serves the same Beast handler and keeps one active request for clarity.

```sh
cmake --build --preset conan-debug --target rstream-example-acceptor-http-server
rstream-example-acceptor-http-server
```

Wait for `Forwarding address:` and use that HTTPS address in the second
terminal. Expect `Hello from the shared Beast handler`. Use the required
[caller credential](https://rstream.io/docs/tunnels/http-tunnels/authentication#token-authentication)
on a protected project. If the Engine uses a private CA, pass it to your HTTP
client and keep the published hostname for verification.

| Setting | What it controls |
| --- | --- |
| `m_auto_reconnect`, `m_reconnect_timeout_ms` | Whether and when to reconnect after control failure. |
| `m_auto_recreate_tunnel`, `m_recreate_tunnel_timeout_ms` | Whether and when to retry creating the tunnel. |

Both retries default to enabled with a five-second interval. An accept can
remain pending while the listener recovers. Supply an application deadline
where bounded startup is required. To stop the listener, call
`acceptor.close(error)` and close or drain accepted sockets you still own.
The example does both on Ctrl+C and lets the loop process completions.

## URI-configured listeners

Use `io::stream` when the same executable should obtain its sockets,
acceptors and resolvers from a configured URI. The resolver selects a
registered transport for the scheme on its first resolution. Use a separate
resolver for each scheme when one process handles multiple transports. The
application then reads and writes using the same Asio interfaces. Examples include `tcp://host:port` for a
TCP connection and `rstrm://tunnel-name` for a private tunnel.
[Package the matching plugins](009-plugins-and-packaging.md) with the
application.

Configure and build the existing HTTP server and client:

```sh
cmake --build --preset conan-debug --target rstream-example-uri-http-server rstream-example-io-http-client
```

On macOS or Linux the server is under `example/io-rstrm/` in the build
directory and the client under `example/io/http/`. On Windows both
executables are in the build directory's `bin/`. Run each by its full
path or add those directories to the terminal path.

| Run | Listener URI | Caller URI |
| --- | --- | --- |
| Local TCP | `tcp://127.0.0.1:8080` | `tcp://127.0.0.1:8080` |
| Private rstream | `rstrm://cpp-uri-http?rstrm.publish=false&rstrm.protocol=tls` | `rstrm://cpp-uri-http` |

Start with TCP. Then stop the server and start the same program with the
private URI. In the caller's terminal, use the corresponding URI.

```sh
rstream-example-uri-http-server --uri 'rstrm://cpp-uri-http?rstrm.publish=false&rstrm.protocol=tls'
```

```sh
rstream-example-io-http-client --uri 'rstrm://cpp-uri-http'
```

Check for `HTTP/1.1 200 OK` and the server's hostname in the output.
The caller exits with status 1 on an I/O or HTTP parsing error. It also
prints valid HTTP error responses, so use the status and body to verify
the request.

For a private run, the startup line shows the configured endpoint. Wait
for the tunnel to appear in the project inventory or retry the request
within a fixed startup window. If it stays unavailable, check the Engine
address and CA, the owner and caller contexts, and the project's tunnel
creation and dial permissions. The generic listener can keep trying to
create the tunnel. Use [the typed acceptor](#use-a-typed-rstream-acceptor)
when the service needs readiness and failure callbacks.

On Ctrl+C, the listener closes its acceptor and active streams before
letting their completions drain.

`rstrm.*` options describe the tunnel. `rstream.retry` controls
Engine reconnect independently of tunnel recreation. See the
[URI reference](011-uri-reference.md) for the namespaces,
and [completion styles](004-completion-models.md) for alternatives to
coroutines.

## Published TCP

Use `m_protocol=rstrm::protocol::tcp` and `m_publish=true` when a standard
TCP client needs the assigned public host and port. Leave `m_port` unset
for an ephemeral port, or supply a port already reserved by the project.

```cpp
rstrm::tunnel_properties properties{};
properties.m_publish = true;
properties.m_protocol = rstrm::protocol::tcp;
```

The application protocol owns caller authentication and encryption.
Use the [TCP forwarding example](tools/000-forwarding.md#choose-tcp-or-tls-for-a-different-service)
to exercise a TCP service from an ordinary client. For a Global project,
`m_allow_cross_region_routing` permits an ingress in another region to
reach the tunnel's owner region when project policy allows it.
